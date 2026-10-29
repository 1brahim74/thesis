#ifndef ISAL_REF_HPP
#define ISAL_REF_HPP

/**
 * isal_ref.hpp — ISA-L baseline, following ISA-L's own examples/ec/ec_simple_example.c:
 *   b = rows of the encode matrix for the first k survivors        (k×k)
 *   gf_invert_matrix(b, d, k)                                       O(k^3), 2k^3 gf_mul
 *   decode_matrix row e = d row (lost data index e)
 *   ec_init_tables(k, nerrs, decode_matrix, tbls)
 *   ec_encode_data(len, k, nerrs, tbls, survivors, outs)           nerrs·k·L
 *
 * The encode matrix is the SAME Cauchy generator as the v3 codec (passed in),
 * so both libraries decode identical codewords.
 *
 * Kernel selection:
 *   IsalKernel::Default -> ec_init_tables + ec_encode_data (ISA-L's runtime
 *                          dispatcher; on CPUs with GFNI this picks the GFNI path)
 *   IsalKernel::AVX2    -> classic tables + ec_encode_data_avx2
 *   IsalKernel::AVX512  -> classic tables + ec_encode_data_avx512
 */

extern "C" {
#include <isa-l/erasure_code.h>
// Arch-specific entry points exported by libisal but not all declared in the header.
void ec_encode_data_avx512(int len, int k, int rows, unsigned char* gftbls,
                           unsigned char** data, unsigned char** coding);
void ec_init_tables_base(int k, int rows, unsigned char* a, unsigned char* gftbls);
}

#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace IsalRef {

enum class IsalKernel { Default, AVX2, AVX512 };

inline const char* kernel_name(IsalKernel k) {
    switch (k) {
        case IsalKernel::Default: return "default";
        case IsalKernel::AVX2:    return "avx2";
        case IsalKernel::AVX512:  return "avx512";
    }
    return "?";
}

inline void init_tables(IsalKernel kern, int k, int rows, unsigned char* a, unsigned char* tbls) {
    if (kern == IsalKernel::Default) ec_init_tables(k, rows, a, tbls);
    else                             ec_init_tables_base(k, rows, a, tbls);
}

inline void run(IsalKernel kern, int len, int k, int rows, unsigned char* tbls,
                unsigned char** src, unsigned char** dst) {
    switch (kern) {
        case IsalKernel::Default: ec_encode_data(len, k, rows, tbls, src, dst); break;
        case IsalKernel::AVX2:    ec_encode_data_avx2(len, k, rows, tbls, src, dst); break;
        case IsalKernel::AVX512:  ec_encode_data_avx512(len, k, rows, tbls, src, dst); break;
    }
}

struct Plan {
    int nlost = 0;
    std::vector<int> lost;
    std::vector<int> src_pos;          // first k survivors (positions in received[])
    std::vector<uint8_t> b, d, dm;     // k×k, k×k, nlost×k
    std::vector<uint8_t> tbls;         // nlost×k×32 (or GFNI layout for Default)
};

struct Codec {
    int n, k, m;
    IsalKernel kern;
    std::vector<uint8_t> enc;          // n×k encode matrix
    std::vector<uint8_t> enc_tbls;

    Codec(const std::vector<uint8_t>& G, int n_, int k_, IsalKernel kern_)
        : n(n_), k(k_), m(n_ - k_), kern(kern_), enc(G) {
        enc_tbls.resize(static_cast<size_t>(m) * k * 32);
        init_tables(kern, k, m, &enc[static_cast<size_t>(k) * k], enc_tbls.data());
    }

    void encode(unsigned char** data, unsigned char** parity, int len) {
        run(kern, len, k, m, enc_tbls.data(), data, parity);
    }

    // Survivor order: recv_idx as given (the harness passes them ascending).
    bool make_plan(const int* recv_idx, int nrecv, Plan& p) {
        p.lost.clear();
        std::vector<char> have(k, 0);
        for (int i = 0; i < nrecv; ++i) if (recv_idx[i] < k) have[recv_idx[i]] = 1;
        for (int j = 0; j < k; ++j) if (!have[j]) p.lost.push_back(j);
        p.nlost = static_cast<int>(p.lost.size());
        if (p.nlost == 0) return true;
        if (nrecv < k) return false;

        p.b.resize(static_cast<size_t>(k) * k);
        p.d.resize(static_cast<size_t>(k) * k);
        p.src_pos.resize(k);
        for (int i = 0; i < k; ++i) {
            p.src_pos[i] = i;
            std::memcpy(&p.b[static_cast<size_t>(i) * k], &enc[static_cast<size_t>(recv_idx[i]) * k], k);
        }
        if (gf_invert_matrix(p.b.data(), p.d.data(), k) < 0) return false;

        p.dm.resize(static_cast<size_t>(p.nlost) * k);
        for (int e = 0; e < p.nlost; ++e)
            std::memcpy(&p.dm[static_cast<size_t>(e) * k], &p.d[static_cast<size_t>(p.lost[e]) * k], k);
        p.tbls.resize(static_cast<size_t>(p.nlost) * k * 32);
        init_tables(kern, k, p.nlost, p.dm.data(), p.tbls.data());
        return true;
    }

    void apply(Plan& p, unsigned char* const* received, unsigned char** out, int len) {
        if (p.nlost == 0) return;
        unsigned char* srcs[256];
        for (int i = 0; i < k; ++i) srcs[i] = received[p.src_pos[i]];
        run(kern, len, k, p.nlost, p.tbls.data(), srcs, out);
    }
};

inline IsalKernel parse_kernel(const std::string& s) {
    if (s == "default") return IsalKernel::Default;
    if (s == "avx2")    return IsalKernel::AVX2;
    if (s == "avx512")  return IsalKernel::AVX512;
    throw std::invalid_argument("unknown isal kernel: " + s);
}

} // namespace IsalRef

#endif // ISAL_REF_HPP
