#ifndef CM256_REF_HPP
#define CM256_REF_HPP

/**
 * cm256_ref.hpp — thin wrapper around catid/cm256 (public domain), the open-source
 * erasure-count Cauchy RS decoder: it eliminates received originals from the
 * recovery rows, then solves only an N×N Cauchy system (N = lost originals)
 * with an O(N²) LDU decomposition. Built with -mavx2 so its gf256 kernels use AVX2.
 *
 * Differences from the other methods that matter when reading results:
 *   - cm256 uses its own Cauchy generator (first recovery row is all-ones = XOR parity),
 *     so it decodes its own parity, not the v3 generator's. Same data, same loss
 *     patterns, same survivor set; every decode is still verified byte-for-byte.
 *   - When the code has a single recovery block (RS(k+1,k)) cm256 takes a pure-XOR path.
 *   - cm256 decodes in place (recovery buffers are overwritten with recovered data),
 *     so the harness copies the parity into scratch buffers outside the timed region.
 *   - cm256 exposes no matrix/region split: only total decode time is reported.
 */

extern "C" {
#include "cm256.h"
}
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Cm256Ref {

inline void init_once() {
    static const bool ok = (cm256_init() == 0);
    if (!ok) throw std::runtime_error("cm256_init failed");
}

struct Codec {
    int n, k, m, L;
    cm256_encoder_params p;
    std::vector<cm256_block> blocks;   // reused per call

    Codec(int n_, int k_, int L_) : n(n_), k(k_), m(n_ - k_), L(L_), blocks(k_) {
        init_once();
        if (n > 256) throw std::invalid_argument("cm256: n must be <= 256");
        p.OriginalCount = k; p.RecoveryCount = m; p.BlockBytes = L;
    }

    // recovery_out: m*L contiguous bytes
    void encode(uint8_t* const* data, uint8_t* recovery_out) {
        for (int j = 0; j < k; ++j) { blocks[j].Block = data[j]; blocks[j].Index = static_cast<unsigned char>(j); }
        if (cm256_encode(p, blocks.data(), recovery_out)) throw std::runtime_error("cm256_encode failed");
    }

    // ptr[i], idx[i] for i<k: the k received blocks (originals keep their index,
    // recovery blocks use k + recovery row). Recovery buffers are overwritten.
    // After the call, out_index[i] holds the (possibly updated) original index.
    int decode(uint8_t* const* ptr, const int* idx, int* out_index) {
        for (int i = 0; i < k; ++i) { blocks[i].Block = ptr[i]; blocks[i].Index = static_cast<unsigned char>(idx[i]); }
        int rc = cm256_decode(p, blocks.data());
        if (out_index) for (int i = 0; i < k; ++i) out_index[i] = blocks[i].Index;
        return rc;
    }
};

} // namespace Cm256Ref

#endif // CM256_REF_HPP
