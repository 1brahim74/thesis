#ifndef GF256_SIMD_HPP
#define GF256_SIMD_HPP

/**
 * gf256_simd.hpp — SIMD region multiply for GF(2^8), split-nibble method.
 *
 * For a coefficient c, any byte x = (h<<4) ^ l, so c*x = c*(h<<4) ^ c*l.
 * Two 16-entry tables (c*l and c*(h<<4)) fit in one 128-bit register each,
 * and PSHUFB / VPSHUFB performs 16/32/64 parallel table lookups per instruction.
 *
 * Table layout is byte-for-byte identical to ISA-L's gf_vect_mul_init():
 *   tbl[0..15]  = c * {00,01,...,0f}
 *   tbl[16..31] = c * {00,10,...,f0}
 * and multi-row tables use ISA-L's ec_init_tables() layout:
 *   tbls[(out_row * nsrc + src) * 32]
 * so the same coefficient matrix can be fed to either library.
 *
 * Kernel: fused dot product, one pass over the sources.
 *   dst[o][i] = XOR_j  coeff(o, j) * src[j][i]     for o in [0, nout)
 * Up to 4 outputs share each source load (as ISA-L's gf_Nvect_dot_prod do),
 * so each source byte is read from memory ceil(nout/4) times, not nout times.
 *
 * Variants are compiled with function-level target attributes and chosen at
 * runtime, so the binary does not need -march=native to use AVX2/AVX-512.
 * Polynomial 0x11D (same as ISA-L and FEC_Codec_v3/gf256.hpp).
 */

#include "../FEC_Codec_v3/gf256.hpp"
#include <immintrin.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace GFSIMD {

enum class Isa { Scalar = 0, AVX2 = 1, AVX512 = 2 };

inline const char* isa_name(Isa i) {
    switch (i) {
        case Isa::Scalar: return "scalar";
        case Isa::AVX2:   return "avx2";
        case Isa::AVX512: return "avx512";
    }
    return "?";
}

inline bool isa_supported(Isa i) {
    __builtin_cpu_init();
    switch (i) {
        case Isa::Scalar: return true;
        case Isa::AVX2:   return __builtin_cpu_supports("avx2");
        case Isa::AVX512: return __builtin_cpu_supports("avx512f") &&
                                 __builtin_cpu_supports("avx512bw");
    }
    return false;
}

inline Isa best_isa() {
    if (isa_supported(Isa::AVX512)) return Isa::AVX512;
    if (isa_supported(Isa::AVX2))   return Isa::AVX2;
    return Isa::Scalar;
}

// 32-byte split-nibble table for coefficient c (ISA-L layout).
// Built from c·2^i by repeated doubling (xtime), so no log/exp lookups:
// t[x] = t[x & (x-1)] ^ c·2^{ctz(x)}.  Same output as 32 calls to GF256::mul.
inline void make_tbl(uint8_t c, uint8_t* t32) {
    uint8_t p[8];
    p[0] = c;
    for (int i = 1; i < 8; ++i)
        p[i] = static_cast<uint8_t>((p[i - 1] << 1) ^ ((p[i - 1] & 0x80) ? 0x1D : 0));
    t32[0] = 0; t32[16] = 0;
    for (int x = 1; x < 16; ++x) {
        const int b = __builtin_ctz(x);
        t32[x]      = t32[x & (x - 1)] ^ p[b];
        t32[16 + x] = t32[16 + (x & (x - 1))] ^ p[4 + b];
    }
}

// Tables for an nout x nsrc coefficient matrix stored row-major in `coef`.
inline void make_tbls(int nsrc, int nout, const uint8_t* coef, uint8_t* tbls) {
    for (int o = 0; o < nout; ++o)
        for (int j = 0; j < nsrc; ++j)
            make_tbl(coef[o * nsrc + j], tbls + (o * nsrc + j) * 32);
}

namespace detail {

inline void dot_scalar(int len, int nsrc, int nout, const uint8_t* tbls,
                       const uint8_t* const* src, uint8_t* const* dst, int start) {
    for (int o = 0; o < nout; ++o) {
        uint8_t* d = dst[o];
        for (int i = start; i < len; ++i) {
            uint8_t acc = 0;
            for (int j = 0; j < nsrc; ++j) {
                const uint8_t* t = tbls + (o * nsrc + j) * 32;
                uint8_t x = src[j][i];
                acc ^= t[x & 15] ^ t[16 + (x >> 4)];
            }
            d[i] = acc;
        }
    }
}

// ── AVX2: 64 bytes per iteration (two 256-bit lanes) ─────────────────────────
template <int NOUT>
__attribute__((target("avx2")))
void dot_avx2(int len, int nsrc, const uint8_t* tbls,
              const uint8_t* const* src, uint8_t* const* dst) {
    const __m256i mask = _mm256_set1_epi8(0x0f);
    int i = 0;
    for (; i + 64 <= len; i += 64) {
        __m256i a0[NOUT], a1[NOUT];
        for (int o = 0; o < NOUT; ++o) { a0[o] = _mm256_setzero_si256(); a1[o] = _mm256_setzero_si256(); }
        for (int j = 0; j < nsrc; ++j) {
            __m256i x0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src[j] + i));
            __m256i x1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src[j] + i + 32));
            __m256i l0 = _mm256_and_si256(x0, mask);
            __m256i h0 = _mm256_and_si256(_mm256_srli_epi64(x0, 4), mask);
            __m256i l1 = _mm256_and_si256(x1, mask);
            __m256i h1 = _mm256_and_si256(_mm256_srli_epi64(x1, 4), mask);
            for (int o = 0; o < NOUT; ++o) {
                const uint8_t* t = tbls + (o * nsrc + j) * 32;
                __m256i tl = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t)));
                __m256i th = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t + 16)));
                a0[o] = _mm256_xor_si256(a0[o], _mm256_xor_si256(_mm256_shuffle_epi8(tl, l0), _mm256_shuffle_epi8(th, h0)));
                a1[o] = _mm256_xor_si256(a1[o], _mm256_xor_si256(_mm256_shuffle_epi8(tl, l1), _mm256_shuffle_epi8(th, h1)));
            }
        }
        for (int o = 0; o < NOUT; ++o) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst[o] + i), a0[o]);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst[o] + i + 32), a1[o]);
        }
    }
    for (; i + 32 <= len; i += 32) {
        __m256i a0[NOUT];
        for (int o = 0; o < NOUT; ++o) a0[o] = _mm256_setzero_si256();
        for (int j = 0; j < nsrc; ++j) {
            __m256i x0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src[j] + i));
            __m256i l0 = _mm256_and_si256(x0, mask);
            __m256i h0 = _mm256_and_si256(_mm256_srli_epi64(x0, 4), mask);
            for (int o = 0; o < NOUT; ++o) {
                const uint8_t* t = tbls + (o * nsrc + j) * 32;
                __m256i tl = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t)));
                __m256i th = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t + 16)));
                a0[o] = _mm256_xor_si256(a0[o], _mm256_xor_si256(_mm256_shuffle_epi8(tl, l0), _mm256_shuffle_epi8(th, h0)));
            }
        }
        for (int o = 0; o < NOUT; ++o)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst[o] + i), a0[o]);
    }
    if (i < len) dot_scalar(len, nsrc, NOUT, tbls, src, dst, i);
}

// ── AVX-512BW: 64 bytes per iteration, masked tail (no scalar remainder) ────
template <int NOUT>
__attribute__((target("avx512f,avx512bw")))
void dot_avx512(int len, int nsrc, const uint8_t* tbls,
                const uint8_t* const* src, uint8_t* const* dst) {
    const __m512i mask = _mm512_set1_epi8(0x0f);
    for (int i = 0; i < len; i += 64) {
        const int rem = len - i;
        const __mmask64 km = rem >= 64 ? ~0ULL : ((1ULL << rem) - 1);
        __m512i a[NOUT];
        for (int o = 0; o < NOUT; ++o) a[o] = _mm512_setzero_si512();
        for (int j = 0; j < nsrc; ++j) {
            __m512i x = _mm512_maskz_loadu_epi8(km, src[j] + i);
            __m512i l = _mm512_and_si512(x, mask);
            __m512i h = _mm512_and_si512(_mm512_srli_epi64(x, 4), mask);
            for (int o = 0; o < NOUT; ++o) {
                const uint8_t* t = tbls + (o * nsrc + j) * 32;
                __m512i tl = _mm512_broadcast_i32x4(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t)));
                __m512i th = _mm512_broadcast_i32x4(_mm_loadu_si128(reinterpret_cast<const __m128i*>(t + 16)));
                a[o] = _mm512_xor_si512(a[o], _mm512_xor_si512(_mm512_shuffle_epi8(tl, l), _mm512_shuffle_epi8(th, h)));
            }
        }
        for (int o = 0; o < NOUT; ++o)
            _mm512_mask_storeu_epi8(dst[o] + i, km, a[o]);
    }
}

} // namespace detail

/**
 * dst[o] = XOR_j coeff(o,j) * src[j], for o in [0,nout), over `len` bytes.
 * tbls: nout*nsrc*32 bytes in ISA-L ec_init_tables layout. dst must not alias src.
 */
inline void dot_prod(Isa isa, int len, int nsrc, int nout, const uint8_t* tbls,
                     const uint8_t* const* src, uint8_t* const* dst) {
    for (int o0 = 0; o0 < nout; o0 += 4) {
        const int g = (nout - o0) < 4 ? (nout - o0) : 4;
        const uint8_t* t = tbls + static_cast<size_t>(o0) * nsrc * 32;
        uint8_t* const* d = dst + o0;
        switch (isa) {
            case Isa::Scalar:
                detail::dot_scalar(len, nsrc, g, t, src, d, 0);
                break;
            case Isa::AVX2:
                switch (g) {
                    case 1: detail::dot_avx2<1>(len, nsrc, t, src, d); break;
                    case 2: detail::dot_avx2<2>(len, nsrc, t, src, d); break;
                    case 3: detail::dot_avx2<3>(len, nsrc, t, src, d); break;
                    default: detail::dot_avx2<4>(len, nsrc, t, src, d); break;
                }
                break;
            case Isa::AVX512:
                switch (g) {
                    case 1: detail::dot_avx512<1>(len, nsrc, t, src, d); break;
                    case 2: detail::dot_avx512<2>(len, nsrc, t, src, d); break;
                    case 3: detail::dot_avx512<3>(len, nsrc, t, src, d); break;
                    default: detail::dot_avx512<4>(len, nsrc, t, src, d); break;
                }
                break;
        }
    }
}

inline Isa parse_isa(const std::string& s) {
    if (s == "scalar") return Isa::Scalar;
    if (s == "avx2")   return Isa::AVX2;
    if (s == "avx512") return Isa::AVX512;
    throw std::invalid_argument("unknown isa: " + s);
}

} // namespace GFSIMD

#endif // GF256_SIMD_HPP
