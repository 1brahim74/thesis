#ifndef RS_CODEC_HPP
#define RS_CODEC_HPP

/**
 * rs_codec.hpp — Reed-Solomon Erasure Codec over GF(2^8)  [v3]
 *
 * KEY OPTIMIZATIONS:
 *
 * [ENC] REGION MULTIPLY ENCODING:
 *   Old: per-byte scalar multiply per parity row
 *   New: per (parity_row, data_col) precomputed 256-byte lookup table
 *   → ~3-5× faster encoding (sequential access, L1 cache-friendly)
 *
 * [DEC-A] O(m³) ERASURE-COUNT DECODER (main thesis contribution):
 *   Only invert the m×m submatrix for LOST data packets.
 *   For m=1: scalar division only → O(k × pkt_size), no matrix
 *   For m=2: 2×2 inversion → O(1) field ops
 *   General: O(m³) << O(k³) when m << k
 *
 * [DEC-B] LRU DECODE CACHE:
 *   Cache key = sorted vector of lost data indices (m entries, not k).
 *   Tighter key → higher hit rate during correlated bursts.
 *   Hit → skip all inversion, go straight to packet reconstruction.
 *
 * [DEC-C] COLUMN-MAJOR PACKET RECONSTRUCT:
 *   Inner loop reads source packets sequentially → cache-friendly for 1500B packets.
 *
 * [DEC-D] FAST PATH: all data received → no computation needed.
 *   When all k data packets arrive, just return them in order.
 */

#include "gf256_matrix.hpp"
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <algorithm>

namespace RSCodec {

    using namespace MatrixGF256;

    using Packet  = std::vector<uint8_t>;
    using Packets = std::vector<Packet>;

    struct CauchyCodec {
        int n, k, m;
        ByteMatrix G;  // n×k generator (cached, built once)

        CauchyCodec(int n_, int k_) : n(n_), k(k_), m(n_-k_) {
            G = build_cauchy_generator(n, k);
        }

        // ── [ENC] Region multiply encoding ────────────────────────────────────
        Packets encode(const Packets& data) const {
            int pkt_size = static_cast<int>(data[0].size());
            Packets codeword(n, Packet(pkt_size, 0));

            // Systematic: copy data packets unchanged
            for (int i = 0; i < k; ++i) codeword[i] = data[i];

            // Parity rows via region_mul_xor
            for (int i = 0; i < m; ++i) {
                uint8_t* dst = codeword[k + i].data();
                for (int j = 0; j < k; ++j) {
                    uint8_t coeff = G[k + i][j];
                    if (coeff == 0) continue;
                    GF256::region_mul_xor(dst, data[j].data(), coeff, pkt_size);
                }
            }
            return codeword;
        }

        // ── [DEC-A] O(m³) decoder ─────────────────────────────────────────────
        /**
         * Decode from exactly k received packets.
         *
         * Algorithm:
         *  1. Find which data indices are MISSING (lost_data) and which are present.
         *  2. [DEC-D] If no data missing → return received data in order (O(1)).
         *  3. Identify m received parity rows needed for recovery.
         *  4. Build m×m sub-problem: G_sub[r][c] = G[par_r][lost_data[c]]
         *  5. [DEC-B] Check cache for G_sub^{-1}.
         *  6. [Cache miss] Invert G_sub (m×m, O(m³)) and cache it.
         *  7. Compute RHS: for each parity row, subtract contributions of known data.
         *  8. Apply G_sub^{-1} to RHS to get lost data packets.
         *  9. [DEC-C] Use region_mul_xor for packet-level multiply (cache-friendly).
         */
        Packets decode(const Packets& received,
                       const std::vector<int>& recv_indices,
                       bool use_cache = true) const {
            int kr = static_cast<int>(recv_indices.size());
            if (kr < k)
                throw std::runtime_error("decode: need exactly k packets");

            int pkt_size = static_cast<int>(received[0].size());

            // Build lookup: which data indices we HAVE and WHICH position in received[]
            std::vector<int> data_pos(k, -1); // data_pos[j] = position in received[] or -1
            for (int i = 0; i < k; ++i) {
                int idx = recv_indices[i];
                if (idx < k) data_pos[idx] = i;
            }

            // Find lost data indices
            std::vector<int> lost_data;
            for (int j = 0; j < k; ++j)
                if (data_pos[j] == -1) lost_data.push_back(j);

            int num_lost = static_cast<int>(lost_data.size());

            // [DEC-D] Fast path: no data lost
            if (num_lost == 0) {
                Packets recovered(k, Packet(pkt_size, 0));
                for (int i = 0; i < k; ++i) {
                    int idx = recv_indices[i];
                    if (idx < k) recovered[idx] = received[i];
                }
                return recovered;
            }

            // Find received parity rows (exactly num_lost needed)
            std::vector<int> par_recv_positions; // positions in received[]
            std::vector<int> par_row_global;     // absolute row index in G
            for (int i = 0; i < k && (int)par_recv_positions.size() < num_lost; ++i) {
                if (recv_indices[i] >= k) {
                    par_recv_positions.push_back(i);
                    par_row_global.push_back(recv_indices[i]);
                }
            }
            if ((int)par_recv_positions.size() < num_lost)
                throw std::runtime_error("decode: not enough parity packets");

            // [DEC-B] Cache lookup
            // KEY FIX: cache key must include BOTH lost_data AND par_row_global.
            // The G_sub inverse depends on which parity rows were used to build G_sub,
            // not just which data packets are lost. If only lost_data is used as key,
            // a cache hit can return an inverse built with different parity rows than
            // are available in the current received set — causing silent corruption.
            //
            // We build the full cache key as the concatenation:
            //   lost_data indices (sorted) + separator + par_row_global indices
            // This ensures a hit only occurs when the exact same subproblem repeats.
            std::vector<int> full_cache_key = lost_data;
            full_cache_key.push_back(-1); // separator
            for (int pr : par_row_global) full_cache_key.push_back(pr);

            const ByteMatrix* cached_inv = nullptr;
            if (use_cache) {
                auto cr = cache_lookup(full_cache_key, n, k);
                cached_inv = cr.G_sub_inv;
            }

            // [DEC-A] Build and invert m×m sub-problem
            ByteMatrix G_sub_inv;
            if (cached_inv) {
                G_sub_inv = *cached_inv;
            } else {
                // Build G_sub[r][c] = G[par_row_global[r]][lost_data[c]]
                ByteMatrix G_sub = make_zero(num_lost, num_lost);
                for (int r = 0; r < num_lost; ++r)
                    for (int c = 0; c < num_lost; ++c)
                        G_sub[r][c] = G[par_row_global[r]][lost_data[c]];

                // Invert G_sub — O(m³) where m = num_lost << k
                auto inv_opt = gf256_invert(G_sub);
                if (!inv_opt)
                    throw std::runtime_error("decode: G_sub singular (impossible with Cauchy)");
                G_sub_inv = *inv_opt;

                if (use_cache)
                    cache_insert(full_cache_key, G_sub_inv, par_row_global, n, k);
            }

            // Reconstruct recovered data
            Packets recovered(k, Packet(pkt_size, 0));

            // Copy known data packets
            for (int j = 0; j < k; ++j)
                if (data_pos[j] != -1)
                    recovered[j] = received[data_pos[j]];

            // Compute RHS for each of the num_lost parity equations:
            // RHS[r][b] = parity_recv[r][b] XOR sum_{j not in lost} G[par_r][j] * data[j][b]
            // We'll compute this directly into temp packets.
            Packets rhs(num_lost, Packet(pkt_size, 0));
            for (int r = 0; r < num_lost; ++r) {
                int pr = par_row_global[r];
                int pr_pos = par_recv_positions[r];

                // Start with the received parity packet
                rhs[r] = received[pr_pos];

                // Subtract (XOR) contributions of KNOWN data packets
                for (int j = 0; j < k; ++j) {
                    if (data_pos[j] == -1) continue; // this is a lost one, skip
                    uint8_t coeff = G[pr][j];
                    if (coeff == 0) continue;
                    // rhs[r] ^= coeff * data[j]
                    GF256::region_mul_xor(rhs[r].data(),
                                          received[data_pos[j]].data(),
                                          coeff, pkt_size);
                }
            }

            // [DEC-C] Apply G_sub^{-1} to RHS to get lost packets:
            // recovered[lost_data[c]][b] = sum_r G_sub_inv[c][r] * rhs[r][b]
            for (int c = 0; c < num_lost; ++c) {
                uint8_t* dst = recovered[lost_data[c]].data();
                for (int r = 0; r < num_lost; ++r) {
                    uint8_t coeff = G_sub_inv[c][r];
                    if (coeff == 0) continue;
                    GF256::region_mul_xor(dst, rhs[r].data(), coeff, pkt_size);
                }
            }

            return recovered;
        }
    };

    // ── API wrappers ──────────────────────────────────────────────────────────

    inline Packets encode(const Packets& data, int n) {
        return CauchyCodec(n, static_cast<int>(data.size())).encode(data);
    }

    inline Packets decode(const Packets& received,
                          const std::vector<int>& recv_indices,
                          int n, int k,
                          bool use_cache = true) {
        return CauchyCodec(n, k).decode(received, recv_indices, use_cache);
    }

} // namespace RSCodec

#endif // RS_CODEC_HPP
