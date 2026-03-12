#ifndef GF256_MATRIX_HPP
#define GF256_MATRIX_HPP

/**
 * gf256_matrix.hpp — Matrix Operations over GF(2^8)  [v3]
 *
 * KEY CONTRIBUTION: O(m³) ERASURE-COUNT SCALING
 *
 * Standard decoders (ISA-L, Jerasure) invert a k×k matrix → O(k³).
 * We exploit systematic code structure to invert only the m×m sub-problem
 * for the m LOST DATA packets:
 *
 *   G_sub[r][c] = G[parity_r][lost_data[c]]   (m×m Cauchy submatrix)
 *
 * This is always invertible (Cauchy guarantee), and costs O(m³) not O(k³).
 *
 * For m=1 (most common): scalar division → O(pkt_size × k)
 * For m=2: 2×2 inversion → trivial
 * For m=k/4: speedup ≈ (k/m)³ = 64×
 *
 * Decoded by: data_lost = G_sub^{-1} × (parity_recv ⊕ G_known × data_known)
 */

#include "gf256.hpp"
#include <vector>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <list>
#include <unordered_map>
#include <cstring>

namespace MatrixGF256 {

    using ByteMatrix = std::vector<std::vector<uint8_t>>;
    using ByteVec    = std::vector<uint8_t>;

    inline ByteMatrix make_zero(int rows, int cols) {
        return ByteMatrix(rows, ByteVec(cols, 0));
    }
    inline ByteMatrix make_identity(int n) {
        ByteMatrix I = make_zero(n, n);
        for (int i = 0; i < n; ++i) I[i][i] = 1;
        return I;
    }

    // ── Cauchy Generator Matrix ──────────────────────────────────────────────
    inline ByteMatrix build_cauchy_generator(int n, int k) {
        GF256::init();
        if (n > 200 || k >= n || k <= 0)
            throw std::invalid_argument("need 0 < k < n <= 200");
        int m = n - k;
        ByteMatrix G = make_zero(n, k);
        for (int i = 0; i < k; ++i) G[i][i] = 1;
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < k; ++j)
                G[k + i][j] = GF256::inv(GF256::add(
                    static_cast<uint8_t>(i),
                    static_cast<uint8_t>(n + j)));
        return G;
    }

    // ── GJE Inversion (used for the small m×m sub-problems) ─────────────────
    inline std::optional<ByteMatrix> gf256_invert(ByteMatrix A) {
        GF256::init();
        int n = static_cast<int>(A.size());
        if (n == 0) return ByteMatrix{};
        ByteMatrix aug = make_zero(n, 2 * n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) aug[i][j] = A[i][j];
            aug[i][n + i] = 1;
        }
        for (int col = 0; col < n; ++col) {
            int piv = -1;
            for (int row = col; row < n; ++row)
                if (aug[row][col]) { piv = row; break; }
            if (piv == -1) return std::nullopt;
            std::swap(aug[col], aug[piv]);
            uint8_t pvi = GF256::inv(aug[col][col]);
            for (int j = 0; j < 2*n; ++j) aug[col][j] = GF256::mul(aug[col][j], pvi);
            for (int row = 0; row < n; ++row) {
                if (row == col || !aug[row][col]) continue;
                uint8_t f = aug[row][col];
                auto tbl = GF256::make_mul_table(f);
                for (int j = 0; j < 2*n; ++j) aug[row][j] ^= tbl[aug[col][j]];
            }
        }
        ByteMatrix inv_m = make_zero(n, n);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                inv_m[i][j] = aug[i][n + j];
        return inv_m;
    }

    // ── LRU Decode Cache ──────────────────────────────────────────────────────
    static constexpr int CACHE_SIZE = 32;

    struct CacheEntry {
        std::vector<int> lost_key;    // sorted lost data indices (m entries)
        ByteMatrix        G_sub_inv;   // m×m inverse
        std::vector<int>  par_rows;    // which parity row indices were used
        int n_codec = 0, k_codec = 0; // codec parameters (part of key)
    };

    static std::list<CacheEntry>                                      lru_list;
    static std::unordered_map<size_t, std::list<CacheEntry>::iterator> lru_map;

    inline size_t hash_key(const std::vector<int>& v, int n=0, int k=0) {
        size_t h = 0x9e3779b9ULL;
        h = h * 1000003 + n * 10000 + k;
        for (int x : v) h = h * 1000003 + static_cast<size_t>(x + 1);
        return h;
    }

    struct CachedDecode {
        const ByteMatrix*         G_sub_inv = nullptr;
        const std::vector<int>*   par_rows  = nullptr;
    };

    inline CachedDecode cache_lookup(const std::vector<int>& key, int n=0, int k=0) {
        size_t h = hash_key(key, n, k);
        auto it = lru_map.find(h);
        if (it == lru_map.end() || it->second->lost_key != key || it->second->n_codec != n || it->second->k_codec != k)
            return {};
        lru_list.splice(lru_list.begin(), lru_list, it->second);
        return {&lru_list.front().G_sub_inv, &lru_list.front().par_rows};
    }

    inline void cache_insert(const std::vector<int>& key,
                             ByteMatrix inv,
                             std::vector<int> par_rows,
                             int n=0, int k=0) {
        size_t h = hash_key(key, n, k);
        if (lru_map.count(h)) return;
        if ((int)lru_list.size() >= CACHE_SIZE) {
            lru_map.erase(hash_key(lru_list.back().lost_key, lru_list.back().n_codec, lru_list.back().k_codec));
            lru_list.pop_back();
        }
        CacheEntry ce;
        ce.lost_key = key; ce.G_sub_inv = std::move(inv); ce.par_rows = std::move(par_rows); ce.n_codec = n; ce.k_codec = k;
        lru_list.push_front(std::move(ce));
        lru_map[h] = lru_list.begin();
    }

    inline void cache_clear() { lru_list.clear(); lru_map.clear(); }

    // ── Packet-level multiply helpers ─────────────────────────────────────────

    inline ByteVec matvec(const ByteMatrix& M, const ByteVec& vec) {
        GF256::init();
        int rows = M.size(), cols = M[0].size();
        ByteVec res(rows, 0);
        for (int i = 0; i < rows; ++i) {
            uint8_t acc = 0;
            for (int j = 0; j < cols; ++j) acc ^= GF256::mul(M[i][j], vec[j]);
            res[i] = acc;
        }
        return res;
    }

    inline ByteMatrix matmul(const ByteMatrix& A, const ByteMatrix& B) {
        GF256::init();
        int r = A.size(), m2 = A[0].size(), c = B[0].size();
        ByteMatrix C = make_zero(r, c);
        for (int i = 0; i < r; ++i)
            for (int k2 = 0; k2 < m2; ++k2)
                if (A[i][k2]) {
                    auto tbl = GF256::make_mul_table(A[i][k2]);
                    for (int j = 0; j < c; ++j) C[i][j] ^= tbl[B[k2][j]];
                }
        return C;
    }

} // namespace MatrixGF256

#endif // GF256_MATRIX_HPP
