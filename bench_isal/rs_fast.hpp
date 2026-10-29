#ifndef RS_FAST_HPP
#define RS_FAST_HPP

/**
 * rs_fast.hpp — v3 m×m erasure decoder + SIMD region multiply.
 *
 * The algorithm is the thesis one (FEC_Codec_v3/rs_codec.hpp, [DEC-A]):
 *   1. lost = missing data indices (m' of them), take the first m' received parity rows
 *   2. G_sub[r][c] = G[par_r][lost_c]     (m'×m' Cauchy submatrix, always invertible)
 *   3. invert G_sub in O(m'^3)
 *   4. lost = G_sub^{-1} · (parity ⊕ G_known · data_known)
 *
 * The only change is how step 4 is executed. v3 did it as two passes of
 * per-coefficient region_mul_xor (first the RHS, then the inverse). Here the
 * two are folded into a single m'×k decode matrix
 *     D = [ G_sub^{-1}·G_known  |  G_sub^{-1} ]       over the k received packets
 * so the packet work is one fused SIMD dot product: m'·k·L byte-multiplies,
 * reading each received packet once per 4 outputs. This is exactly the shape
 * of ISA-L's decode (ec_encode_data with `nerrs` rows), so a benchmark against
 * ISA-L compares inversion strategies and kernels, not loop structure.
 * Cost of building D: O(m'^3) inversion + O(m'^2·k) product (vs ISA-L O(k^3)).
 *
 * invert_small() is the same Gauss-Jordan elimination as v3's gf256_invert(),
 * on flat stack arrays instead of vector<vector<>> (no heap allocation).
 * test_simd.cpp checks it against gf256_invert() on random Cauchy submatrices.
 */

#include "gf256_simd.hpp"
#include "../FEC_Codec_v3/gf256_matrix.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace RSFast {

static constexpr int MAX_M = 64;

// Scratch arrays below are only read over their first m×m entries, which are
// always written first; GCC cannot prove that through memcpy.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

// In-place-free GJE on an m×m row-major matrix. Returns false if singular.
inline bool invert_small(const uint8_t* A, uint8_t* inv, int m) {
    uint8_t a[MAX_M * MAX_M];
    std::memcpy(a, A, static_cast<size_t>(m) * m);
    std::memset(inv, 0, static_cast<size_t>(m) * m);
    for (int i = 0; i < m; ++i) inv[i * m + i] = 1;
    for (int col = 0; col < m; ++col) {
        int piv = -1;
        for (int r = col; r < m; ++r) if (a[r * m + col]) { piv = r; break; }
        if (piv < 0) return false;
        if (piv != col)
            for (int j = 0; j < m; ++j) {
                std::swap(a[col * m + j], a[piv * m + j]);
                std::swap(inv[col * m + j], inv[piv * m + j]);
            }
        const uint8_t p = GF256::inv(a[col * m + col]);
        for (int j = 0; j < m; ++j) {
            a[col * m + j]   = GF256::mul(a[col * m + j], p);
            inv[col * m + j] = GF256::mul(inv[col * m + j], p);
        }
        for (int r = 0; r < m; ++r) {
            if (r == col) continue;
            const uint8_t f = a[r * m + col];
            if (!f) continue;
            for (int j = 0; j < m; ++j) {
                a[r * m + j]   ^= GF256::mul(f, a[col * m + j]);
                inv[r * m + j] ^= GF256::mul(f, inv[col * m + j]);
            }
        }
    }
    return true;
}

struct Plan {
    int nlost = 0;
    int lost[MAX_M];          // lost data indices
    int src_pos[256];         // k positions into received[] that feed the dot product
    uint8_t D[MAX_M * 256];   // nlost × k decode matrix (row-major)
    std::vector<uint8_t> tbls;// nlost × k × 32 split-nibble tables
};

struct FastCauchy {
    int n, k, m;
    std::vector<uint8_t> G;        // n×k row-major, identical to v3 build_cauchy_generator
    std::vector<uint8_t> enc_tbls; // m×k×32
    GFSIMD::Isa isa;

    FastCauchy(int n_, int k_, GFSIMD::Isa isa_) : n(n_), k(k_), m(n_ - k_), isa(isa_) {
        if (m > MAX_M || k > 255) throw std::invalid_argument("FastCauchy: m too large");
        auto Gv = MatrixGF256::build_cauchy_generator(n, k);
        G.resize(static_cast<size_t>(n) * k);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < k; ++j) G[i * k + j] = Gv[i][j];
        enc_tbls.resize(static_cast<size_t>(m) * k * 32);
        GFSIMD::make_tbls(k, m, &G[static_cast<size_t>(k) * k], enc_tbls.data());
    }

    void encode(const uint8_t* const* data, uint8_t* const* parity, int len) const {
        GFSIMD::dot_prod(isa, len, k, m, enc_tbls.data(), data, parity);
    }

    /**
     * Build the decode plan from the indices of received packets.
     * recv_idx[0..nrecv) are codeword indices of received[] in the same order.
     * Returns false if fewer than k usable packets.
     * If build_tables is false only D is built (used by the ISA-L-region hybrid).
     */
    bool make_plan(const int* recv_idx, int nrecv, Plan& p, bool build_tables = true) const {
        int data_pos[256];
        for (int j = 0; j < k; ++j) data_pos[j] = -1;
        for (int i = 0; i < nrecv; ++i) if (recv_idx[i] < k) data_pos[recv_idx[i]] = i;

        p.nlost = 0;
        for (int j = 0; j < k; ++j) if (data_pos[j] < 0) p.lost[p.nlost++] = j;
        const int ml = p.nlost;
        if (ml == 0) return true;

        int par_pos[MAX_M], par_row[MAX_M], np = 0;
        for (int i = 0; i < nrecv && np < ml; ++i)
            if (recv_idx[i] >= k) { par_pos[np] = i; par_row[np] = recv_idx[i]; ++np; }
        if (np < ml) return false;

        // [DEC-A] m'×m' submatrix and its inverse
        uint8_t Gs[MAX_M * MAX_M], Gi[MAX_M * MAX_M];
        for (int r = 0; r < ml; ++r)
            for (int c = 0; c < ml; ++c)
                Gs[r * ml + c] = G[par_row[r] * k + p.lost[c]];
        if (!invert_small(Gs, Gi, ml)) throw std::runtime_error("singular G_sub");

        // D over sources: known data (ascending), then the ml parity packets
        int s = 0;
        for (int j = 0; j < k; ++j) {
            if (data_pos[j] < 0) continue;
            p.src_pos[s] = data_pos[j];
            for (int c = 0; c < ml; ++c) {
                uint8_t acc = 0;
                for (int r = 0; r < ml; ++r)
                    acc ^= GF256::mul(Gi[c * ml + r], G[par_row[r] * k + j]);
                p.D[c * k + s] = acc;
            }
            ++s;
        }
        for (int r = 0; r < ml; ++r, ++s) {
            p.src_pos[s] = par_pos[r];
            for (int c = 0; c < ml; ++c) p.D[c * k + s] = Gi[c * ml + r];
        }
        if (build_tables) {
            p.tbls.resize(static_cast<size_t>(ml) * k * 32);
            GFSIMD::make_tbls(k, ml, p.D, p.tbls.data());
        }
        return true;
    }

    // out[c] receives lost data packet p.lost[c]
    void apply(const Plan& p, const uint8_t* const* received, uint8_t* const* out, int len) const {
        if (p.nlost == 0) return;
        const uint8_t* srcs[256];
        for (int s = 0; s < k; ++s) srcs[s] = received[p.src_pos[s]];
        GFSIMD::dot_prod(isa, len, k, p.nlost, p.tbls.data(), srcs, out);
    }
};

#pragma GCC diagnostic pop

} // namespace RSFast

#endif // RS_FAST_HPP
