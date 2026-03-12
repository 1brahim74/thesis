/**
 * test_suite_extended.cpp — Extended Tests for FEC Codec v3
 * Ibrahim Mammadov, ZJU-UIUC 2026
 *
 * Compile: g++ -std=c++17 -O2 -o test_suite_ext test_suite_extended.cpp
 * Run:     ./test_suite_ext
 *
 * New categories (not in test_suite.cpp):
 *   [MAT]  Matrix algebra: gf256_invert, matmul, matvec, Cauchy properties
 *   [PAR]  Partial erasure: m_actual < m (fewer losses than max capacity)
 *   [DET]  Determinism under all receive orderings / packet permutations
 *   [BND]  Boundary: n=2, k=1 (minimum), k=n-1 (single parity), large n
 *   [REP]  Repeated encode/decode of same data — cache warm vs cold consistency
 *   [FLT]  Fault injection: corrupt a byte after encoding — verify decode detects/misses
 *   [CHN2] Channel extended: IncastChannel, GE reset, DC3 stationary dist, to_equiv_ge
 *   [PRP]  Code properties: MDS distance, systematic form, generator matrix structure
 *   [PER]  Performance regression: encode/decode throughput floor checks
 *   [XRS]  Cross-config isolation: simultaneous different codecs, no shared state leaks
 *   [SEQ]  Sequential pattern: same data re-encoded 1000x, results never drift
 *   [ASY]  Asymmetric erasure: lose only parity, only data, mixed — all cases
 *   [MTX]  Matrix: A * A^{-1} = I for random invertible matrices, all sizes 1..16
 *   [HAR]  Hardened stress: adversarial erasure patterns, worst-case m for each config
 */

#include "gf256.hpp"
#include "gf256_matrix.hpp"
#include "rs_codec.hpp"
#include "channel.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <set>
#include <map>
#include <numeric>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cmath>
#include <chrono>
#include <string>
#include <sstream>

// ── Framework ─────────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0, g_total = 0;

void begin_category(const std::string& name) {
    std::cout << "\n┌─────────────────────────────────────────────────────\n";
    std::cout << "│  " << name << "\n";
    std::cout << "└─────────────────────────────────────────────────────\n";
}

void check(bool ok, const std::string& desc) {
    ++g_total;
    if (ok) { ++g_pass; std::cout << "  ✓  " << desc << "\n"; }
    else    { ++g_fail; std::cout << "  ✗  FAIL: " << desc << "\n"; }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
using Packet  = RSCodec::Packet;
using Packets = RSCodec::Packets;
using BM      = MatrixGF256::ByteMatrix;

Packets rand_data(int k, int pkt, std::mt19937& rng) {
    std::uniform_int_distribution<int> d(0,255);
    Packets p(k, Packet(pkt));
    for (auto& pk : p) for (auto& b : pk) b = d(rng);
    return p;
}

bool pkts_eq(const Packets& a, const Packets& b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i) if(a[i]!=b[i]) return false;
    return true;
}

std::pair<Packets,std::vector<int>> drop(const Packets& cw, std::set<int> d) {
    Packets r; std::vector<int> idx;
    for (int i=0;i<(int)cw.size();++i) if(!d.count(i)){r.push_back(cw[i]);idx.push_back(i);}
    return {r,idx};
}

BM rand_invertible_matrix(int n, std::mt19937& rng) {
    // Build random invertible GF(2^8) matrix via LU with random pivots
    std::uniform_int_distribution<int> d(1,255); // nonzero
    while(true) {
        BM A(n, std::vector<uint8_t>(n));
        for(auto& row:A) for(auto& b:row) b=(uint8_t)d(rng);
        auto inv = MatrixGF256::gf256_invert(A);
        if(inv) return A;
        // singular by random chance — retry (very rare)
    }
}

BM identity(int n) { return MatrixGF256::make_identity(n); }

bool mat_eq(const BM& A, const BM& B) {
    if(A.size()!=B.size()) return false;
    for(size_t i=0;i<A.size();++i) {
        if(A[i].size()!=B[i].size()) return false;
        if(A[i]!=B[i]) return false;
    }
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// [MAT] Matrix Algebra
// ═════════════════════════════════════════════════════════════════════════════
void test_matrix_algebra() {
    begin_category("[MAT] Matrix Algebra — gf256_invert, matmul, matvec, Cauchy");
    GF256::init();
    std::mt19937 rng(111);

    // A * inv(A) = I for all sizes 1..16
    {
        bool ok = true;
        for (int n = 1; n <= 16 && ok; ++n) {
            for (int trial = 0; trial < 10 && ok; ++trial) {
                auto A = rand_invertible_matrix(n, rng);
                auto inv_opt = MatrixGF256::gf256_invert(A);
                if (!inv_opt) { ok=false; break; }
                auto P = MatrixGF256::matmul(A, *inv_opt);
                if (!mat_eq(P, identity(n))) ok=false;
            }
        }
        check(ok, "A * inv(A) == I for random invertible matrices, sizes 1..16 (10 trials each)");
    }

    // inv(A) * A = I  (left inverse == right inverse)
    {
        bool ok = true;
        for (int n = 1; n <= 12 && ok; ++n) {
            auto A = rand_invertible_matrix(n, rng);
            auto inv_opt = MatrixGF256::gf256_invert(A);
            if (!inv_opt) { ok=false; break; }
            auto P = MatrixGF256::matmul(*inv_opt, A);
            if (!mat_eq(P, identity(n))) ok=false;
        }
        check(ok, "inv(A) * A == I (left == right inverse), sizes 1..12");
    }

    // inv(inv(A)) == A
    {
        bool ok = true;
        for (int n = 2; n <= 10 && ok; ++n) {
            auto A = rand_invertible_matrix(n, rng);
            auto inv1 = MatrixGF256::gf256_invert(A);
            if (!inv1) { ok=false; break; }
            auto inv2 = MatrixGF256::gf256_invert(*inv1);
            if (!inv2) { ok=false; break; }
            if (!mat_eq(*inv2, A)) ok=false;
        }
        check(ok, "inv(inv(A)) == A for sizes 2..10");
    }

    // Singular matrix returns nullopt
    {
        // Zero matrix is always singular
        BM Z(4, std::vector<uint8_t>(4, 0));
        auto inv = MatrixGF256::gf256_invert(Z);
        check(!inv.has_value(), "gf256_invert(zero matrix) returns nullopt");

        // Rank-1 matrix (all rows identical) is singular
        BM R(4, std::vector<uint8_t>(4, 0));
        for (int j=0;j<4;++j) R[0][j]=R[1][j]=R[2][j]=R[3][j]=(j+1);
        auto inv2 = MatrixGF256::gf256_invert(R);
        check(!inv2.has_value(), "gf256_invert(rank-1 matrix) returns nullopt");
    }

    // 1x1 matrix: inv([a]) = [inv(a)]
    {
        bool ok = true;
        for (int a=1; a<256; ++a) {
            BM A = {{(uint8_t)a}};
            auto inv = MatrixGF256::gf256_invert(A);
            if (!inv) { ok=false; break; }
            if ((*inv)[0][0] != GF256::inv((uint8_t)a)) { ok=false; break; }
        }
        check(ok, "1x1 invert: inv([a]) == [inv(a)] for all 255 nonzero elements");
    }

    // matmul associativity: (AB)C == A(BC)
    {
        bool ok = true;
        for (int trial=0; trial<50 && ok; ++trial) {
            int n = 4;
            auto A = rand_invertible_matrix(n, rng);
            auto B = rand_invertible_matrix(n, rng);
            auto C = rand_invertible_matrix(n, rng);
            auto AB_C = MatrixGF256::matmul(MatrixGF256::matmul(A,B), C);
            auto A_BC = MatrixGF256::matmul(A, MatrixGF256::matmul(B,C));
            if (!mat_eq(AB_C, A_BC)) ok=false;
        }
        check(ok, "matmul associativity: (AB)C == A(BC) (50 trials, 4x4)");
    }

    // matvec: M*v matches matmul(M, col_matrix(v))
    {
        bool ok = true;
        std::uniform_int_distribution<int> d(0,255);
        for (int trial=0; trial<100 && ok; ++trial) {
            int n=4;
            auto M = rand_invertible_matrix(n, rng);
            MatrixGF256::ByteVec v(n);
            for(auto& b:v) b=d(rng);
            auto mv = MatrixGF256::matvec(M, v);
            // Compare with manual computation
            MatrixGF256::ByteVec ref(n,0);
            for(int i=0;i<n;++i)
                for(int j=0;j<n;++j)
                    ref[i] ^= GF256::mul(M[i][j], v[j]);
            if(mv != ref) ok=false;
        }
        check(ok, "matvec(M,v) matches manual row-dot-column (100 trials)");
    }

    // Cauchy generator: all k×k submatrices are invertible (MDS property proof)
    {
        bool all_inv = true;
        // Test RS(8,4): all C(8,4)=70 4x4 submatrices should be invertible
        int n=8, k=4;
        auto G = MatrixGF256::build_cauchy_generator(n, k);
        // Enumerate all C(n,k) subsets of k rows
        std::vector<int> rows(n); std::iota(rows.begin(), rows.end(), 0);
        std::vector<int> mask(n,0); std::fill(mask.begin(), mask.begin()+k, 1);
        std::sort(mask.begin(), mask.end());
        int count=0;
        do {
            BM sub(k, std::vector<uint8_t>(k));
            int ri=0;
            for(int i=0;i<n;++i) if(mask[i]) {
                for(int j=0;j<k;++j) sub[ri][j]=G[i][j];
                ++ri;
            }
            auto inv = MatrixGF256::gf256_invert(sub);
            if(!inv) { all_inv=false; break; }
            ++count;
        } while(std::next_permutation(mask.begin(), mask.end()));
        check(all_inv, "Cauchy MDS: all C(8,4)=70 submatrices of RS(8,4) generator are invertible (" + std::to_string(count) + " checked)");
    }

    // Cauchy generator systematic: top k rows = identity
    {
        bool ok=true;
        for(auto [n,k] : std::vector<std::pair<int,int>>{{6,4},{8,4},{12,8},{16,8},{21,16}}) {
            auto G = MatrixGF256::build_cauchy_generator(n,k);
            for(int i=0;i<k&&ok;++i)
                for(int j=0;j<k&&ok;++j)
                    if(G[i][j] != (i==j?1:0)) ok=false;
        }
        check(ok, "Cauchy generator top-k rows = identity (systematic form) for 5 configs");
    }

    // matmul with identity = original
    {
        bool ok=true;
        for(int n=1;n<=8&&ok;++n) {
            auto A = rand_invertible_matrix(n,rng);
            auto I = identity(n);
            if(!mat_eq(MatrixGF256::matmul(A,I),A)) ok=false;
            if(!mat_eq(MatrixGF256::matmul(I,A),A)) ok=false;
        }
        check(ok, "matmul(A,I)==A and matmul(I,A)==A for sizes 1..8");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [PAR] Partial Erasure: m_actual < m (fewer losses than capacity)
// ═════════════════════════════════════════════════════════════════════════════
void test_partial_erasure() {
    begin_category("[PAR] Partial Erasure — m_actual < m capacity");
    std::mt19937 rng(222);

    // For each config, test ALL values of m_actual from 0 to m
    struct Cfg { int n, k; };
    std::vector<Cfg> cfgs = {{6,4},{8,4},{10,8},{12,8},{16,8},{21,16}};

    for(auto& cfg : cfgs) {
        int m = cfg.n - cfg.k;
        bool ok = true;
        RSCodec::CauchyCodec codec(cfg.n, cfg.k);

        for(int m_act=0; m_act<=m && ok; ++m_act) {
            // 100 trials for each m_actual
            for(int trial=0; trial<100 && ok; ++trial) {
                auto data = rand_data(cfg.k, 64, rng);
                auto cw   = codec.encode(data);

                // Shuffle and drop exactly m_act packets
                std::vector<int> all(cfg.n); std::iota(all.begin(), all.end(), 0);
                std::shuffle(all.begin(), all.end(), rng);
                std::set<int> d(all.begin(), all.begin()+m_act);
                auto [recv,idx] = drop(cw, d);
                // Pad with dummy parity if needed to reach k received
                // (drop already gives us n-m_act packets, which is >=k)
                // But decode needs exactly k: take first k from recv
                Packets recv_k(recv.begin(), recv.begin()+cfg.k);
                std::vector<int> idx_k(idx.begin(), idx.begin()+cfg.k);

                try {
                    auto rec = codec.decode(recv_k, idx_k);
                    if(!pkts_eq(rec, data)) ok=false;
                } catch(...) { ok=false; }
            }
        }
        check(ok, "RS(" + std::to_string(cfg.n) + "," + std::to_string(cfg.k) +
              "): m_actual=0.." + std::to_string(m) + " all pass (100 trials each)");
    }

    // Specific: lose 0 out of 8 capacity — pure fast path
    {
        RSCodec::CauchyCodec codec(16,8);
        bool ok=true;
        for(int t=0;t<500;++t) {
            auto d=rand_data(8,1500,rng);
            auto cw=codec.encode(d);
            Packets recv(cw.begin(),cw.begin()+8);
            std::vector<int> idx(8); std::iota(idx.begin(),idx.end(),0);
            auto rec=codec.decode(recv,idx);
            if(!pkts_eq(rec,d)) { ok=false; break; }
        }
        check(ok,"RS(16,8) m_actual=0: fast path, 500 trials, no false decode triggered");
    }

    // Lose exactly 1 from high-capacity codec
    {
        RSCodec::CauchyCodec codec(32,16);
        bool ok=true;
        std::mt19937 rng2(333);
        for(int t=0;t<300;++t) {
            auto d=rand_data(16,1500,rng2);
            auto cw=codec.encode(d);
            std::vector<int> all(32); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng2);
            // Drop 1, keep 16
            std::set<int> dr={all[0]};
            auto [recv,idx]=drop(cw,dr);
            Packets recv_k(recv.begin(),recv.begin()+16);
            std::vector<int> idx_k(idx.begin(),idx.begin()+16);
            try {
                auto rec=codec.decode(recv_k,idx_k);
                if(!pkts_eq(rec,d)) { ok=false; break; }
            } catch(...){ ok=false; break; }
        }
        check(ok,"RS(32,16) m_actual=1 (capacity=16): O(m^3) scalar path, 300 trials");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [DET] Determinism under all receive orderings
// ═════════════════════════════════════════════════════════════════════════════
void test_determinism() {
    begin_category("[DET] Determinism — all receive orderings produce same result");
    std::mt19937 rng(444);

    // For a fixed erasure, ALL orderings of the k received packets
    // must produce the same decoded output
    {
        int n=6, k=4;
        RSCodec::CauchyCodec codec(n,k);
        bool ok=true;

        for(int trial=0; trial<50 && ok; ++trial) {
            auto data=rand_data(k,32,rng);
            auto cw=codec.encode(data);
            // Drop packets 0 and 1, keep [2,3,4,5]
            Packets base_recv={cw[2],cw[3],cw[4],cw[5]};
            std::vector<int> base_idx={2,3,4,5};

            // Decode with canonical order
            auto ref=codec.decode(base_recv, base_idx, false);

            // Try all 4!=24 permutations
            std::vector<int> perm={0,1,2,3};
            do {
                Packets perm_recv={base_recv[perm[0]],base_recv[perm[1]],base_recv[perm[2]],base_recv[perm[3]]};
                std::vector<int> perm_idx={base_idx[perm[0]],base_idx[perm[1]],base_idx[perm[2]],base_idx[perm[3]]};
                try {
                    auto res=codec.decode(perm_recv, perm_idx, false);
                    if(!pkts_eq(res,ref)) { ok=false; break; }
                } catch(...) { ok=false; break; }
            } while(std::next_permutation(perm.begin(),perm.end()));
        }
        check(ok,"RS(6,4): all 24 orderings of received packets produce same decode (50 trials)");
    }

    // Same test with cache enabled — cache must not make result order-dependent
    {
        int n=8, k=4;
        RSCodec::CauchyCodec codec(n,k);
        bool ok=true;
        MatrixGF256::cache_clear();

        for(int trial=0; trial<50 && ok; ++trial) {
            auto data=rand_data(k,32,rng);
            auto cw=codec.encode(data);
            // Keep [1,3,5,7]
            Packets base_recv={cw[1],cw[3],cw[5],cw[7]};
            std::vector<int> base_idx={1,3,5,7};
            auto ref=codec.decode(base_recv, base_idx, false);

            std::vector<int> perm={0,1,2,3};
            do {
                Packets pr={base_recv[perm[0]],base_recv[perm[1]],base_recv[perm[2]],base_recv[perm[3]]};
                std::vector<int> pi={base_idx[perm[0]],base_idx[perm[1]],base_idx[perm[2]],base_idx[perm[3]]};
                auto res=codec.decode(pr,pi,true);
                if(!pkts_eq(res,ref)) { ok=false; break; }
            } while(std::next_permutation(perm.begin(),perm.end()));
        }
        check(ok,"RS(8,4) WITH cache: all 24 orderings produce same result (50 trials)");
    }

    // Encoding is order-independent: same data packets in any order → same parity
    // (Note: data order IS significant in RS, but same data set → same parity)
    {
        RSCodec::CauchyCodec codec(8,4);
        auto data=rand_data(4,64,rng);
        auto cw1=codec.encode(data);
        auto cw2=codec.encode(data);
        auto cw3=codec.encode(data);
        check(cw1==cw2 && cw2==cw3, "Encode is fully deterministic: 3 calls with same data produce identical codewords");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [BND] Boundary Conditions
// ═════════════════════════════════════════════════════════════════════════════
void test_boundaries() {
    begin_category("[BND] Boundary Conditions — minimum/maximum configs");
    std::mt19937 rng(555);

    // Minimum: n=2, k=1 (1 data, 1 parity)
    {
        RSCodec::CauchyCodec codec(2,1);
        bool ok=true;
        for(int t=0;t<200;++t) {
            auto d=rand_data(1,64,rng);
            auto cw=codec.encode(d);
            // Lose data: recover from parity
            auto [r0,i0]=drop(cw,{0});
            // Lose parity: fast path
            auto [r1,i1]=drop(cw,{1});
            try {
                auto rec0=codec.decode(r0,i0);
                auto rec1=codec.decode(r1,i1);
                if(!pkts_eq(rec0,d)||!pkts_eq(rec1,d)) ok=false;
            } catch(...){ ok=false; }
        }
        check(ok,"RS(2,1): minimum config, lose data or parity, 200 trials");
    }

    // n=3, k=1 (1 data, 2 parity — any 1 received is enough)
    {
        RSCodec::CauchyCodec codec(3,1);
        bool ok=true;
        for(int t=0;t<100;++t) {
            auto d=rand_data(1,64,rng);
            auto cw=codec.encode(d);
            // Any single packet should allow recovery
            for(int keep=0;keep<3&&ok;++keep) {
                Packets recv={cw[keep]}; std::vector<int> idx={keep};
                try {
                    auto rec=codec.decode(recv,idx);
                    if(!pkts_eq(rec,d)) ok=false;
                } catch(...){ ok=false; }
            }
        }
        check(ok,"RS(3,1): any single packet sufficient for recovery (100 trials × 3 patterns)");
    }

    // k = n-1 (single parity packet): only 1 erasure tolerated
    {
        bool ok=true;
        for(auto k : {3,7,15,31}) {
            int n=k+1;
            RSCodec::CauchyCodec codec(n,k);
            for(int t=0;t<50&&ok;++t) {
                auto d=rand_data(k,64,rng);
                auto cw=codec.encode(d);
                // Lose any one packet
                std::vector<int> all(n); std::iota(all.begin(),all.end(),0);
                std::shuffle(all.begin(),all.end(),rng);
                int lose=all[0];
                auto [recv,idx]=drop(cw,{lose});
                try {
                    auto rec=codec.decode(recv,idx);
                    if(!pkts_eq(rec,d)) ok=false;
                } catch(...){ ok=false; }
            }
        }
        check(ok,"k=n-1 single parity: RS(4,3),RS(8,7),RS(16,15),RS(32,31) — 50 trials each");
    }

    // Large n: RS(50,40)
    {
        RSCodec::CauchyCodec codec(50,40);
        bool ok=true;
        for(int t=0;t<30&&ok;++t) {
            auto d=rand_data(40,256,rng);
            auto cw=codec.encode(d);
            // Random 10 erasures
            std::vector<int> all(50); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+10);
            auto [recv,idx]=drop(cw,dr);
            Packets recv_k(recv.begin(),recv.begin()+40);
            std::vector<int> idx_k(idx.begin(),idx.begin()+40);
            try {
                auto rec=codec.decode(recv_k,idx_k);
                if(!pkts_eq(rec,d)) ok=false;
            } catch(...){ ok=false; }
        }
        check(ok,"RS(50,40): large config, 30 trials with 10 random erasures");
    }

    // n=100, k=50 (50% overhead)
    {
        RSCodec::CauchyCodec codec(100,50);
        bool ok=true;
        for(int t=0;t<20&&ok;++t) {
            auto d=rand_data(50,128,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(100); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+50);
            auto [recv,idx]=drop(cw,dr);
            Packets recv_k(recv.begin(),recv.begin()+50);
            std::vector<int> idx_k(idx.begin(),idx.begin()+50);
            try {
                auto rec=codec.decode(recv_k,idx_k);
                if(!pkts_eq(rec,d)) ok=false;
            } catch(...){ ok=false; }
        }
        check(ok,"RS(100,50): very large config, 20 trials, max 50 erasures");
    }

    // Packet size = n*k (square data block)
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<50&&ok;++t) {
            auto d=rand_data(4,32,rng); // 4*8=32 byte packets
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(8,4): packet size = n*k = 32 bytes, 50 trials");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [FLT] Fault Injection: corrupted packets
// ═════════════════════════════════════════════════════════════════════════════
void test_fault_injection() {
    begin_category("[FLT] Fault Injection — packet corruption detection/behavior");
    std::mt19937 rng(666);
    std::uniform_int_distribution<int> bd(0,255);

    // Flipped bit in data packet: if no erasure, decode returns corrupted data
    // (FEC corrects erasures, not errors — this verifies we don't silently fix errors)
    {
        RSCodec::CauchyCodec codec(8,4);
        int corruption_detected = 0, total = 200;
        for(int t=0;t<total;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);
            // Flip a byte in cw[0] (no erasure — all packets received but one is corrupt)
            auto corrupted=cw;
            corrupted[0][0] ^= 0xFF;
            // Pass all k=4 data packets (including the corrupted one)
            Packets recv(corrupted.begin(), corrupted.begin()+4);
            std::vector<int> idx={0,1,2,3};
            auto rec=codec.decode(recv,idx);
            if(!pkts_eq(rec,d)) ++corruption_detected;
        }
        // With corruption, decoded data should NOT equal original (no error correction)
        check(corruption_detected == total,
              "Corrupted packet (no erasure) → wrong decode: " +
              std::to_string(corruption_detected) + "/" + std::to_string(total) +
              " trials show corruption (FEC corrects erasures, not errors)");
    }

    // All-zero corrupt packet: verify it propagates to wrong decode
    {
        RSCodec::CauchyCodec codec(6,4);
        bool ok=true;
        for(int t=0;t<50&&ok;++t) {
            auto d=rand_data(4,64,rng);
            // Make sure data is non-zero
            for(auto& b:d[0]) b=bd(rng)|1;
            auto cw=codec.encode(d);
            // Replace data[0] with zeros
            Packets recv={Packet(64,0),cw[1],cw[2],cw[3]};
            std::vector<int> idx={0,1,2,3};
            auto rec=codec.decode(recv,idx);
            if(pkts_eq(rec,d)) ok=false; // should NOT equal original
        }
        check(ok,"All-zero corrupt data packet produces wrong decode (verifies no magic error correction)");
    }

    // Corrupt parity, then erase data: decoder uses bad parity → wrong result
    {
        RSCodec::CauchyCodec codec(6,4);
        int wrong=0, total=100;
        for(int t=0;t<total;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);
            // Corrupt parity[0] (index 4), then erase data[0] (index 0)
            // Decode will try to use corrupted parity to recover data[0]
            Packets recv={cw[1],cw[2],cw[3],cw[4]};
            recv[3][0] ^= 0xFF; // corrupt the parity
            std::vector<int> idx={1,2,3,4};
            auto rec=codec.decode(recv,idx,false);
            if(!pkts_eq(rec,d)) ++wrong;
        }
        check(wrong==total,
              "Corrupt parity + erased data → wrong reconstruction: " +
              std::to_string(wrong) + "/" + std::to_string(total));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CHN2] Channel Extended Tests
// ═════════════════════════════════════════════════════════════════════════════
void test_channel_extended() {
    begin_category("[CHN2] Channel Extended — IncastChannel, reset, stationary, to_equiv_ge");

    // GE reset returns to GOOD state — verify by checking first packet loss rate
    // Use slow p_gb so channel stays in GOOD for a meaningful number of packets
    {
        Channel::GilbertElliott ge(0.001, 0.5, 0.01, 0.9, 42); // slow transition to BAD
        ge.simulate(50000); // drive to BAD steady state
        ge.reset(); // back to GOOD
        // Simulate 200 packets — with p_gb=0.001, transitions to BAD are rare
        // expected loss rate ≈ p_g=0.01, far from p_b=0.9
        auto lost=ge.simulate(200);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/200;
        check(rate<0.10, "GE reset() returns to GOOD state: loss rate <10% in 200 packets (got=" +
              std::to_string(rate).substr(0,5)+")");
    }

    // DC3 reset returns to NORMAL state
    {
        // Use slow transitions so NORMAL state persists after reset
        Channel::DataCenterChannel dc(0.001,0.10,0.50,0.50,0.001,0.03,0.35,42);
        dc.simulate(50000); // drive to non-NORMAL steady state
        dc.reset(); // back to NORMAL
        // Simulate 200 packets — with p_nd=0.001, transitions to DEGRADED are rare
        auto lost=dc.simulate(200);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/200;
        check(rate<0.01, "DC3 reset() returns to NORMAL state: loss rate <1% in 200 packets (got=" +
              std::to_string(rate).substr(0,6)+")");
    }

    // DC3 stationary distribution sums to 1.0
    {
        Channel::DataCenterChannel dc(0.01,0.10,0.20,0.30,0.001,0.03,0.35,42);
        auto sd=dc.stationary();
        double sum=sd.pi_n+sd.pi_d+sd.pi_i;
        check(std::abs(sum-1.0)<1e-9, "DC3 stationary distribution sums to 1.0 (sum=" +
              std::to_string(sum)+")");
    }

    // DC3 stationary distribution all non-negative
    {
        bool ok=true;
        uint64_t seed=100;
        for(double p_nd : {0.001,0.01,0.10,0.50}) {
            Channel::DataCenterChannel dc(p_nd,0.10,0.20,0.30,0.001,0.03,0.35,seed++);
            auto sd=dc.stationary();
            if(sd.pi_n<0||sd.pi_d<0||sd.pi_i<0) { ok=false; break; }
        }
        check(ok,"DC3 stationary distribution all non-negative for varied p_nd");
    }

    // DC3 mean_loss_rate matches simulated loss rate (within 2%)
    {
        Channel::DataCenterChannel dc(0.02,0.15,0.25,0.40,0.001,0.03,0.35,777);
        double theoretical=dc.mean_loss_rate();
        auto lost=dc.simulate(500000);
        int l=0; for(bool b:lost) if(b) ++l;
        double measured=(double)l/lost.size();
        check(std::abs(measured-theoretical)<0.02,
              "DC3 mean_loss_rate() matches simulation within 2% (theory=" +
              std::to_string(theoretical).substr(0,6)+" measured="+
              std::to_string(measured).substr(0,6)+")");
    }

    // IncastChannel: loss rate spikes during incast periods
    {
        Channel::IncastChannel ic(Channel::DataCenterChannel(0.01,0.10,0.20,0.30,0.001,0.03,0.35,42),50,10,0.80);
        auto lost=ic.simulate(5000);
        // Detect loss spikes at period boundaries
        // Compute max window loss rate in any 10-packet window
        double max_rate=0;
        for(int i=0;i<(int)lost.size()-10;++i) {
            int l=0; for(int j=0;j<10;++j) if(lost[i+j]) ++l;
            max_rate=std::max(max_rate,(double)l/10);
        }
        check(max_rate>0.5,"IncastChannel: max 10-packet window loss rate > 50% during incast bursts (max=" +
              std::to_string(max_rate).substr(0,5)+")");
    }

    // IncastChannel: overall loss rate lower than burst rate (NORMAL dominates)
    {
        Channel::IncastChannel ic(Channel::DataCenterChannel(0.001,0.05,0.30,0.50,0.001,0.02,0.35,42),100,5,0.90);
        auto lost=ic.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/lost.size();
        check(rate<0.10,"IncastChannel: overall loss rate < 10% when incast is rare (rate=" +
              std::to_string(rate).substr(0,6)+")");
    }

    // to_equiv_ge: the equivalent GE channel has valid probabilities
    {
        Channel::DataCenterChannel dc(0.02,0.15,0.25,0.40,0.001,0.03,0.35,42);
        auto ge=dc.to_equivalent_ge();
        // Check returned GE has reasonable loss rate
        auto lost=ge.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/100000;
        check(rate>0.0 && rate<1.0,"to_equivalent_ge() produces valid GE channel (loss rate=" +
              std::to_string(rate).substr(0,5)+")");
    }

    // GE with p_gb=0, p_bg=anything: always in GOOD state
    {
        Channel::GilbertElliott ge(0.0,0.5,0.001,0.9,42);
        auto lost=ge.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/100000;
        // Should only see GOOD state loss (p_g=0.001)
        check(rate<0.01,"GE p_gb=0: permanently in GOOD state, loss rate near p_g=0.001 (measured=" +
              std::to_string(rate).substr(0,6)+")");
    }

    // GE with p_bg=0, p_gb=anything: always in BAD state after first transition
    {
        Channel::GilbertElliott ge(1.0,0.0,0.001,0.9,42); // immediately enters BAD, never leaves
        auto lost=ge.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/100000;
        // Should see BAD state loss (p_b=0.9) almost always
        check(rate>0.85,"GE p_bg=0: permanently in BAD state after entry, loss rate near p_b=0.9 (measured=" +
              std::to_string(rate).substr(0,6)+")");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [ASY] Asymmetric Erasure Patterns
// ═════════════════════════════════════════════════════════════════════════════
void test_asymmetric_erasures() {
    begin_category("[ASY] Asymmetric Erasure — only data, only parity, all mixed combos");
    std::mt19937 rng(888);

    struct Cfg { int n,k; };
    std::vector<Cfg> cfgs = {{8,4},{12,8},{16,8},{21,16}};

    for(auto& cfg : cfgs) {
        int m=cfg.n-cfg.k;
        RSCodec::CauchyCodec codec(cfg.n, cfg.k);

        // Case A: lose ALL parity (m packets), keep all data
        {
            bool ok=true;
            for(int t=0;t<100&&ok;++t) {
                auto d=rand_data(cfg.k,64,rng);
                auto cw=codec.encode(d);
                // Keep only first k (data) packets
                Packets recv(cw.begin(),cw.begin()+cfg.k);
                std::vector<int> idx(cfg.k); std::iota(idx.begin(),idx.end(),0);
                try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
            check(ok,"RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+
                  "): lose ALL "+std::to_string(m)+" parity, keep all data (100 trials)");
        }

        // Case B: lose ALL data (k packets), keep all parity — hardest case
        // Only works if k <= m (i.e., enough parity to recover all data)
        // For our configs: RS(8,4) m=4=k, RS(12,8) m=4<k skip, RS(16,8) m=8=k, RS(21,16) m=5<k skip
        if(m >= cfg.k) {
            bool ok=true;
            for(int t=0;t<100&&ok;++t) {
                auto d=rand_data(cfg.k,64,rng);
                auto cw=codec.encode(d);
                // Keep only parity packets (last m, but need k of them)
                Packets recv;
                std::vector<int> idx;
                for(int i=cfg.k;i<cfg.n&&(int)recv.size()<cfg.k;++i) {
                    recv.push_back(cw[i]); idx.push_back(i);
                }
                try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
            check(ok,"RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+
                  "): m=k="+std::to_string(m)+", recover using ONLY parity (100 trials)");
        }

        // Case C: alternating — lose even-indexed data, keep odd-indexed data + parity
        {
            bool ok=true;
            for(int t=0;t<100&&ok;++t) {
                auto d=rand_data(cfg.k,64,rng);
                auto cw=codec.encode(d);
                std::set<int> dr;
                for(int i=0;i<cfg.k&&(int)dr.size()<(size_t)m;i+=2) dr.insert(i);
                auto [recv,idx]=drop(cw,dr);
                Packets recv_k(recv.begin(),recv.begin()+cfg.k);
                std::vector<int> idx_k(idx.begin(),idx.begin()+cfg.k);
                try { auto rec=codec.decode(recv_k,idx_k); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
            check(ok,"RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+
                  "): lose even-indexed data packets (up to m), 100 trials");
        }

        // Case D: lose consecutive data from middle
        {
            bool ok=true;
            for(int t=0;t<100&&ok;++t) {
                auto d=rand_data(cfg.k,64,rng);
                auto cw=codec.encode(d);
                // Lose data packets [k/2 .. k/2+m-1]
                std::set<int> dr;
                int start=cfg.k/2;
                for(int i=start;i<start+m;++i) dr.insert(i);
                auto [recv,idx]=drop(cw,dr);
                Packets recv_k(recv.begin(),recv.begin()+cfg.k);
                std::vector<int> idx_k(idx.begin(),idx.begin()+cfg.k);
                try { auto rec=codec.decode(recv_k,idx_k); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
            check(ok,"RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+
                  "): lose "+std::to_string(m)+" consecutive middle data packets (100 trials)");
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [REP] Repeated operations — no drift over long sequences
// ═════════════════════════════════════════════════════════════════════════════
void test_repeated_ops() {
    begin_category("[REP] Repeated Operations — no state drift over long sequences");
    std::mt19937 rng(1001);

    // 2000 sequential encode-decode cycles on the same codec object
    {
        RSCodec::CauchyCodec codec(12,8);
        bool ok=true;
        for(int t=0;t<2000&&ok;++t) {
            auto d=rand_data(8,64,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(12); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(12,8): 2000 sequential encode-decode cycles, same codec object");
    }

    // Same data encoded 500 times — always same codeword
    {
        RSCodec::CauchyCodec codec(8,4);
        auto d=rand_data(4,128,rng);
        auto ref=codec.encode(d);
        bool ok=true;
        for(int t=0;t<500&&ok;++t) {
            auto cw=codec.encode(d);
            if(cw!=ref) ok=false;
        }
        check(ok,"Same data encoded 500 times always produces identical codeword");
    }

    // Alternating different codecs: ensure no cross-contamination across 1000 calls
    {
        RSCodec::CauchyCodec c1(6,4), c2(10,8);
        bool ok=true;
        for(int t=0;t<500&&ok;++t) {
            auto d1=rand_data(4,64,rng);
            auto d2=rand_data(8,64,rng);
            auto cw1=c1.encode(d1); auto cw2=c2.encode(d2);
            auto [r1,i1]=drop(cw1,{0,1}); auto [r2,i2]=drop(cw2,{0,1});
            try {
                auto rec1=c1.decode(r1,i1);
                auto rec2=c2.decode(r2,i2);
                if(!pkts_eq(rec1,d1)||!pkts_eq(rec2,d2)) ok=false;
            } catch(...){ ok=false; }
        }
        check(ok,"Alternating RS(6,4) and RS(10,8): 500 interleaved cycles, no cross-contamination");
    }

    // Cache warms up then stays correct: 500 trials same pattern = 499 cache hits
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(12,8);
        bool ok=true;
        for(int t=0;t<500&&ok;++t) {
            auto d=rand_data(8,64,rng);
            auto cw=codec.encode(d);
            // Always the same erasure pattern → cache hit after first
            auto [recv,idx]=drop(cw,{0,1,2,3});
            try { auto rec=codec.decode(recv,idx,true); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(12,8): 500 trials with fixed erasure pattern {0,1,2,3}, cache hits always correct");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [PRP] Code Properties
// ═════════════════════════════════════════════════════════════════════════════
void test_code_properties() {
    begin_category("[PRP] Code Properties — MDS, systematic, Cauchy structure");
    std::mt19937 rng(2002);

    // MDS property: minimum distance d = m+1 = n-k+1
    // Verification: any k packets sufficient (proven by exhaustive decoder test above)
    // Here we verify the ENCODING side: k linearly independent codewords
    {
        // Check that the k standard basis vectors encode to k linearly independent codewords
        int n=8, k=4;
        RSCodec::CauchyCodec codec(n,k);
        std::vector<Packets> codewords;
        for(int i=0;i<k;++i) {
            Packets basis(k, Packet(1,0));
            basis[i][0]=1; // e_i
            codewords.push_back(codec.encode(basis));
        }
        // Check parity portions are linearly independent (columns of G are distinct)
        // Simplified: check all k parity rows of the k codewords are distinct
        bool distinct=true;
        for(int i=0;i<k&&distinct;++i)
            for(int j=i+1;j<k&&distinct;++j)
                if(codewords[i][k]==codewords[j][k]) distinct=false; // first parity col
        check(distinct,"MDS basis: k standard basis codewords have distinct parity (k=4)");
    }

    // Cauchy matrix entries are all nonzero
    {
        bool ok=true;
        for(auto [n,k] : std::vector<std::pair<int,int>>{{6,4},{12,8},{21,16}}) {
            auto G=MatrixGF256::build_cauchy_generator(n,k);
            int m=n-k;
            for(int i=0;i<m&&ok;++i)
                for(int j=0;j<k&&ok;++j)
                    if(G[k+i][j]==0) ok=false; // parity rows must be all nonzero
        }
        check(ok,"Cauchy generator: all parity-section entries nonzero (Cauchy matrix property)");
    }

    // Parity of XOR'd codewords = XOR of parities (linearity over GF(2^8))
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<100&&ok;++t) {
            auto a=rand_data(4,64,rng);
            auto b=rand_data(4,64,rng);
            // c = a XOR b
            Packets c(4, Packet(64));
            for(int i=0;i<4;++i) for(int j=0;j<64;++j) c[i][j]=a[i][j]^b[i][j];
            auto cwa=codec.encode(a);
            auto cwb=codec.encode(b);
            auto cwc=codec.encode(c);
            // cwa XOR cwb should == cwc
            bool lin=true;
            for(int i=0;i<8&&lin;++i)
                for(int j=0;j<64&&lin;++j)
                    if((cwa[i][j]^cwb[i][j])!=cwc[i][j]) lin=false;
            if(!lin) ok=false;
        }
        check(ok,"RS(8,4) linearity: encode(a⊕b)==encode(a)⊕encode(b) over full codeword (100 trials)");
    }

    // Parity check: G_parity * data = parity (verify encoding formula directly)
    {
        int n=6, k=4;
        auto G=MatrixGF256::build_cauchy_generator(n,k);
        RSCodec::CauchyCodec codec(n,k);
        bool ok=true;
        std::mt19937 rng2(303);
        for(int t=0;t<50&&ok;++t) {
            auto d=rand_data(k,1,rng2); // 1-byte packets for easy manual check
            auto cw=codec.encode(d);
            // Manually compute parity rows
            for(int i=k;i<n&&ok;++i) {
                uint8_t expected=0;
                for(int j=0;j<k;++j) expected^=GF256::mul(G[i][j],d[j][0]);
                if(cw[i][0]!=expected) ok=false;
            }
        }
        check(ok,"Parity check: cw[k+i][b] == sum_j G[k+i][j]*data[j][b] (50 trials, 1-byte pkts)");
    }

    // All-zero codeword only from all-zero data
    {
        int n=8, k=4;
        RSCodec::CauchyCodec codec(n,k);
        // Non-zero data should never produce all-zero parity (with overwhelming probability)
        bool ok=true;
        for(int t=0;t<100&&ok;++t) {
            auto d=rand_data(k,64,rng);
            // Make sure data is non-zero
            d[0][0]|=1;
            auto cw=codec.encode(d);
            bool any_nonzero_parity=false;
            for(int i=k;i<n;++i) for(auto b:cw[i]) if(b) { any_nonzero_parity=true; break; }
            if(!any_nonzero_parity) ok=false;
        }
        check(ok,"Non-zero data always produces non-zero parity (100 trials)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [PER] Performance Floor Checks
// ═════════════════════════════════════════════════════════════════════════════
void test_performance_floor() {
    begin_category("[PER] Performance Floor — minimum throughput sanity checks");
    std::mt19937 rng(3003);

    // Encoding: RS(32,16), 1500B packets, must do at least 1000 ops/sec
    {
        RSCodec::CauchyCodec codec(32,16);
        auto d=rand_data(16,1500,rng);
        int N=500;
        auto t0=std::chrono::steady_clock::now();
        for(int i=0;i<N;++i) codec.encode(d);
        auto t1=std::chrono::steady_clock::now();
        double us=std::chrono::duration<double,std::micro>(t1-t0).count()/N;
        double mbps=(16*1500*8.0)/(us); // bits/us = Mbps
        check(mbps>10.0,"RS(32,16) encode throughput > 10 Mbps (measured=" +
              std::to_string((int)mbps)+" Mbps, "+std::to_string(us).substr(0,6)+"µs/op)");
    }

    // Decoding m=1: RS(32,16) single erasure must be fast
    {
        RSCodec::CauchyCodec codec(32,16);
        auto d=rand_data(16,1500,rng);
        auto cw=codec.encode(d);
        // Lose data[0] only
        auto [recv,idx]=drop(cw,{0});
        Packets recv_k(recv.begin(),recv.begin()+16);
        std::vector<int> idx_k(idx.begin(),idx.begin()+16);
        MatrixGF256::cache_clear();
        int N=500;
        auto t0=std::chrono::steady_clock::now();
        for(int i=0;i<N;++i) codec.decode(recv_k,idx_k,false);
        auto t1=std::chrono::steady_clock::now();
        double us=std::chrono::duration<double,std::micro>(t1-t0).count()/N;
        check(us<500.0,"RS(32,16) m=1 decode < 500µs (measured="+std::to_string(us).substr(0,6)+"µs)");
    }

    // Decoding m=16 (worst case) RS(32,16): must be under 5ms
    {
        RSCodec::CauchyCodec codec(32,16);
        auto d=rand_data(16,1500,rng);
        auto cw=codec.encode(d);
        std::vector<int> all(32); std::iota(all.begin(),all.end(),0);
        std::shuffle(all.begin(),all.end(),rng);
        std::set<int> dr(all.begin(),all.begin()+16);
        auto [recv,idx]=drop(cw,dr);
        MatrixGF256::cache_clear();
        int N=100;
        auto t0=std::chrono::steady_clock::now();
        for(int i=0;i<N;++i) codec.decode(recv,idx,false);
        auto t1=std::chrono::steady_clock::now();
        double us=std::chrono::duration<double,std::micro>(t1-t0).count()/N;
        check(us<5000.0,"RS(32,16) m=16 decode < 5ms (measured="+std::to_string(us).substr(0,6)+"µs)");
    }

    // GF multiply throughput: 10M ops/sec minimum
    {
        GF256::init();
        std::uniform_int_distribution<int> d(1,255);
        std::vector<uint8_t> a(100000), b(100000);
        for(auto& x:a) x=d(rng); for(auto& x:b) x=d(rng);
        auto t0=std::chrono::steady_clock::now();
        volatile uint8_t acc=0;
        for(int i=0;i<100000;++i) acc^=GF256::mul(a[i],b[i]);
        auto t1=std::chrono::steady_clock::now();
        double us=std::chrono::duration<double,std::micro>(t1-t0).count();
        double mops=100000.0/us;
        check(mops>10.0,"GF multiply throughput > 10M ops/sec (measured="+
              std::to_string(mops).substr(0,6)+"M ops/sec)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [HAR] Hardened Stress — adversarial patterns
// ═════════════════════════════════════════════════════════════════════════════
void test_hardened_stress() {
    begin_category("[HAR] Hardened Stress — adversarial & worst-case patterns");
    std::mt19937 rng(4004);

    // Always lose the highest-index packets (worst cache pattern — different every config)
    {
        bool ok=true;
        for(auto [n,k] : std::vector<std::pair<int,int>>{{8,4},{12,8},{16,8},{21,16},{32,16}}) {
            RSCodec::CauchyCodec codec(n,k);
            int m=n-k;
            for(int t=0;t<200&&ok;++t) {
                auto d=rand_data(k,64,rng);
                auto cw=codec.encode(d);
                // Lose last m packets
                std::set<int> dr;
                for(int i=n-m;i<n;++i) dr.insert(i);
                auto [recv,idx]=drop(cw,dr);
                try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
        }
        check(ok,"Always lose LAST m packets (worst case parity loss): 200 trials × 5 configs");
    }

    // Always lose the first m data packets (hardest recovery — all data lost at start)
    {
        bool ok=true;
        for(auto [n,k] : std::vector<std::pair<int,int>>{{8,4},{12,8},{16,8}}) {
            RSCodec::CauchyCodec codec(n,k);
            int m=n-k;
            for(int t=0;t<200&&ok;++t) {
                auto d=rand_data(k,64,rng);
                auto cw=codec.encode(d);
                std::set<int> dr;
                for(int i=0;i<m;++i) dr.insert(i); // first m data packets
                auto [recv,idx]=drop(cw,dr);
                try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
        }
        check(ok,"Always lose FIRST m data packets: 200 trials × RS(8,4),(12,8),(16,8)");
    }

    // Alternating erasure patterns: even vs odd — stresses cache rotation
    {
        RSCodec::CauchyCodec codec(16,8);
        bool ok=true;
        MatrixGF256::cache_clear();
        for(int t=0;t<1000&&ok;++t) {
            auto d=rand_data(8,256,rng);
            auto cw=codec.encode(d);
            // Alternate between two erasure patterns
            std::set<int> dr=(t%2==0)?std::set<int>{0,2,4,6,8,10,12,14}
                                     :std::set<int>{1,3,5,7,9,11,13,15};
            auto [recv,idx]=drop(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(16,8): 1000 trials alternating 2 erasure patterns (cache thrash test)");
    }

    // Cycling through all 70 erasure patterns of RS(8,4) repeatedly
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        MatrixGF256::cache_clear();

        // Pre-generate all C(8,4)=70 patterns
        std::vector<std::set<int>> patterns;
        std::vector<int> mask(8,0); std::fill(mask.begin(),mask.begin()+4,1);
        std::sort(mask.begin(),mask.end());
        do {
            std::set<int> dr;
            for(int i=0;i<8;++i) if(mask[i]) dr.insert(i);
            patterns.push_back(dr);
        } while(std::next_permutation(mask.begin(),mask.end()));

        for(int cycle=0;cycle<5&&ok;++cycle) {
            for(auto& dr : patterns) {
                auto d=rand_data(4,64,rng);
                auto cw=codec.encode(d);
                auto [recv,idx]=drop(cw,dr);
                try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
                catch(...){ ok=false; }
            }
        }
        check(ok,"RS(8,4): cycle through all 70 erasure patterns 5 times (cache fill+evict cycle)");
    }

    // Worst case for O(m^3): large m, small packet — pure matrix overhead
    {
        RSCodec::CauchyCodec codec(32,16);
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(16,1,rng); // 1-byte packets: pure matrix overhead
            auto cw=codec.encode(d);
            std::vector<int> all(32); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+16);
            auto [recv,idx]=drop(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(32,16) m=16, 1-byte packets (pure O(m^3) overhead): 200 trials");
    }

    // Verify encode(decode(encode(x))) == encode(x) — double round trip
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw1=codec.encode(d);
            // Decode from some subset
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop(cw1,dr);
            auto rec=codec.decode(recv,idx);
            // Re-encode
            auto cw2=codec.encode(rec);
            if(cw1!=cw2) ok=false;
        }
        check(ok,"Double roundtrip: encode(decode(encode(x)))==encode(x) (200 trials)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [SEQ] Sequential Patterns — no accumulated error
// ═════════════════════════════════════════════════════════════════════════════
void test_sequential_patterns() {
    begin_category("[SEQ] Sequential Patterns — long sequence stability");
    std::mt19937 rng(5005);

    // 5000 sequential encode-decode with cache always enabled
    {
        RSCodec::CauchyCodec codec(16,8);
        bool ok=true;
        for(int t=0;t<5000&&ok;++t) {
            auto d=rand_data(8,128,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(16); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+8);
            auto [recv,idx]=drop(cw,dr);
            try { auto rec=codec.decode(recv,idx,true); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(16,8): 5000 sequential encode-decode, cache enabled throughout");
    }

    // Long GE simulation followed by FEC decode — real end-to-end
    {
        Channel::GilbertElliott ch(0.05,0.15,0.01,0.5,42);
        RSCodec::CauchyCodec codec(10,8);
        bool ok=true;
        int total=0,pass=0;

        for(int frame=0;frame<500&&ok;++frame) {
            auto d=rand_data(8,256,rng);
            auto cw=codec.encode(d);
            auto lost=ch.simulate(10);
            // Count losses
            int nlost=0; for(bool l:lost) if(l) ++nlost;
            if(nlost>2) continue; // beyond capacity, skip
            ++total;
            // Build received set from loss pattern
            Packets recv; std::vector<int> idx;
            for(int i=0;i<10;++i) if(!lost[i]){recv.push_back(cw[i]);idx.push_back(i);}
            if((int)recv.size()<8) continue;
            Packets recv_k(recv.begin(),recv.begin()+8);
            std::vector<int> idx_k(idx.begin(),idx.begin()+8);
            try { auto rec=codec.decode(recv_k,idx_k); if(pkts_eq(rec,d)) ++pass; else ok=false; }
            catch(...){ ok=false; }
        }
        check(ok && pass==total, "End-to-end GE channel → FEC decode: " +
              std::to_string(pass)+"/"+std::to_string(total)+" recoverable frames decoded");
    }

    // Long DC3 simulation
    {
        Channel::DataCenterChannel ch(0.01,0.10,0.20,0.30,0.001,0.03,0.35,99);
        RSCodec::CauchyCodec codec(12,8);
        bool ok=true;
        int total=0,pass=0;

        for(int frame=0;frame<500&&ok;++frame) {
            auto d=rand_data(8,256,rng);
            auto cw=codec.encode(d);
            auto lost=ch.simulate(12);
            int nlost=0; for(bool l:lost) if(l) ++nlost;
            if(nlost>4) continue;
            ++total;
            Packets recv; std::vector<int> idx;
            for(int i=0;i<12;++i) if(!lost[i]){recv.push_back(cw[i]);idx.push_back(i);}
            if((int)recv.size()<8) continue;
            Packets recv_k(recv.begin(),recv.begin()+8);
            std::vector<int> idx_k(idx.begin(),idx.begin()+8);
            try { auto rec=codec.decode(recv_k,idx_k); if(pkts_eq(rec,d)) ++pass; else ok=false; }
            catch(...){ ok=false; }
        }
        check(ok && pass==total, "End-to-end DC3 channel → FEC decode: " +
              std::to_string(pass)+"/"+std::to_string(total)+" recoverable frames decoded");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════════════
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEC Codec v3 — Extended Test Suite                              ║\n";
    std::cout << "║  Ibrahim Mammadov, ZJU-UIUC 2026                                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    GF256::init();

    auto t0=std::chrono::steady_clock::now();

    test_matrix_algebra();
    test_partial_erasure();
    test_determinism();
    test_boundaries();
    test_fault_injection();
    test_channel_extended();
    test_asymmetric_erasures();
    test_repeated_ops();
    test_code_properties();
    test_performance_floor();
    test_hardened_stress();
    test_sequential_patterns();

    auto t1=std::chrono::steady_clock::now();
    double elapsed=std::chrono::duration<double>(t1-t0).count();

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  RESULTS                                                         ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Passed: " << std::setw(5) << g_pass << " / " << std::setw(5) << g_total << "                                             ║\n";
    std::cout << "║  Failed: " << std::setw(5) << g_fail << "                                                     ║\n";
    std::cout << "║  Time:   " << std::fixed << std::setprecision(2) << elapsed << "s                                                  ║\n";
    if(g_fail==0)
        std::cout << "║  Status: ALL TESTS PASSED ✓                                      ║\n";
    else
        std::cout << "║  Status: " << g_fail << " FAILED ✗                                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    return g_fail>0?1:0;
}
