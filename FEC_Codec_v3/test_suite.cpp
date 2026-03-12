/**
 * test_suite.cpp — Comprehensive Correctness Tests for FEC Codec v3
 * Ibrahim Mammadov, ZJU-UIUC 2026
 *
 * Compile: g++ -std=c++17 -O2 -o test_suite test_suite.cpp
 * Run:     ./test_suite
 *
 * Test categories:
 *   [GF]   GF(2^8) arithmetic laws
 *   [ENC]  Encoder correctness
 *   [DEC]  Decoder correctness — all erasure patterns
 *   [EDG]  Edge cases and boundary conditions
 *   [STR]  Stress tests — random erasure patterns, many configs
 *   [CAC]  LRU cache correctness
 *   [CHN]  Channel model statistical properties
 *   [INV]  Invariant checks (encode->decode roundtrip, all configs)
 */

#include "gf256.hpp"
#include "gf256_matrix.hpp"
#include "rs_codec.hpp"
#include "channel.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <cassert>
#include <functional>
#include <set>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <string>
#include <chrono>

// ── Test framework ────────────────────────────────────────────────────────────

static int g_pass = 0, g_fail = 0, g_total = 0;
static std::string g_current_category;

void begin_category(const std::string& name) {
    g_current_category = name;
    std::cout << "\n┌─────────────────────────────────────────────────────\n";
    std::cout << "│  " << name << "\n";
    std::cout << "└─────────────────────────────────────────────────────\n";
}

void check(bool condition, const std::string& desc) {
    ++g_total;
    if (condition) {
        ++g_pass;
        std::cout << "  ✓  " << desc << "\n";
    } else {
        ++g_fail;
        std::cout << "  ✗  FAIL: " << desc << "\n";
    }
}

void check_eq(int a, int b, const std::string& desc) {
    check(a == b, desc + " (got " + std::to_string(a) + " expected " + std::to_string(b) + ")");
}
// ── Helpers ───────────────────────────────────────────────────────────────────

using Packet  = RSCodec::Packet;
using Packets = RSCodec::Packets;

Packets make_random_data(int k, int pkt_size, std::mt19937& rng) {
    std::uniform_int_distribution<int> bd(0, 255);
    Packets data(k, Packet(pkt_size));
    for (auto& pkt : data)
        for (auto& b : pkt)
            b = bd(rng);
    return data;
}

// Drop specific packet indices, return (received, recv_indices)
std::pair<Packets, std::vector<int>> drop_packets(
    const Packets& codeword,
    const std::set<int>& drop_set)
{
    Packets recv;
    std::vector<int> idx;
    for (int i = 0; i < (int)codeword.size(); ++i) {
        if (!drop_set.count(i)) {
            recv.push_back(codeword[i]);
            idx.push_back(i);
        }
    }
    return {recv, idx};
}

bool packets_equal(const Packets& a, const Packets& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < (int)a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// [GF] GF(2^8) Arithmetic Laws
// ═════════════════════════════════════════════════════════════════════════════

void test_gf_arithmetic() {
    begin_category("[GF] GF(2^8) Arithmetic Laws");
    GF256::init();

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> d(0, 255);
    std::uniform_int_distribution<int> dnz(1, 255); // non-zero

    // Commutativity: a*b == b*a
    {
        bool ok = true;
        for (int i = 0; i < 10000; ++i) {
            uint8_t a = d(rng), b = d(rng);
            if (GF256::mul(a,b) != GF256::mul(b,a)) { ok = false; break; }
        }
        check(ok, "Multiplication commutativity: a*b == b*a (10000 random pairs)");
    }

    // Associativity: (a*b)*c == a*(b*c)
    {
        bool ok = true;
        for (int i = 0; i < 5000; ++i) {
            uint8_t a = d(rng), b = d(rng), c = d(rng);
            if (GF256::mul(GF256::mul(a,b),c) != GF256::mul(a,GF256::mul(b,c))) { ok = false; break; }
        }
        check(ok, "Multiplication associativity: (a*b)*c == a*(b*c) (5000 triples)");
    }

    // Distributivity: a*(b+c) == a*b + a*c
    {
        bool ok = true;
        for (int i = 0; i < 5000; ++i) {
            uint8_t a = d(rng), b = d(rng), c = d(rng);
            uint8_t lhs = GF256::mul(a, GF256::add(b,c));
            uint8_t rhs = GF256::add(GF256::mul(a,b), GF256::mul(a,c));
            if (lhs != rhs) { ok = false; break; }
        }
        check(ok, "Distributivity: a*(b+c) == a*b + a*c (5000 triples)");
    }

    // Multiplicative identity: a*1 == a
    {
        bool ok = true;
        for (int a = 0; a < 256; ++a)
            if (GF256::mul(a,1) != a) { ok = false; break; }
        check(ok, "Multiplicative identity: a*1 == a (all 256 elements)");
    }

    // Additive identity: a+0 == a, a*0 == 0
    {
        bool ok = true;
        for (int a = 0; a < 256; ++a) {
            if (GF256::add(a,0) != a) { ok = false; break; }
            if (GF256::mul(a,0) != 0) { ok = false; break; }
        }
        check(ok, "Additive/multiplicative zero: a+0==a, a*0==0 (all 256)");
    }

    // Self-inverse under addition: a+a == 0 (characteristic 2)
    {
        bool ok = true;
        for (int a = 0; a < 256; ++a)
            if (GF256::add(a,a) != 0) { ok = false; break; }
        check(ok, "Characteristic 2: a+a == 0 (all 256 elements)");
    }

    // Multiplicative inverse: a * inv(a) == 1 for all a != 0
    {
        bool ok = true;
        for (int a = 1; a < 256; ++a)
            if (GF256::mul(a, GF256::inv(a)) != 1) { ok = false; break; }
        check(ok, "Multiplicative inverse: a*inv(a)==1 for all 255 nonzero elements");
    }

    // Division: a/b == a*inv(b)
    {
        bool ok = true;
        for (int i = 0; i < 5000; ++i) {
            uint8_t a = d(rng), b = dnz(rng);
            if (GF256::div(a,b) != GF256::mul(a, GF256::inv(b))) { ok = false; break; }
        }
        check(ok, "Division consistency: a/b == a*inv(b) (5000 random pairs)");
    }

    // mul_table matches scalar mul for all (c, x) pairs
    {
        bool ok = true;
        for (int c = 0; c < 256 && ok; ++c) {
            auto tbl = GF256::make_mul_table((uint8_t)c);
            for (int x = 0; x < 256; ++x) {
                if (tbl[x] != GF256::mul((uint8_t)c, (uint8_t)x)) { ok = false; break; }
            }
        }
        check(ok, "make_mul_table: tbl[x]==mul(c,x) for all 256×256 combinations");
    }

    // region_mul_xor: byte-by-byte vs region result must match
    {
        bool ok = true;
        std::vector<uint8_t> src(1500), dst1(1500,0), dst2(1500,0);
        for (auto& b : src) b = d(rng);
        uint8_t c = dnz(rng);
        // scalar
        for (int i = 0; i < 1500; ++i) dst1[i] ^= GF256::mul(c, src[i]);
        // region
        GF256::region_mul_xor(dst2.data(), src.data(), c, 1500);
        check(dst1 == dst2, "region_mul_xor matches scalar multiply (1500 bytes)");
    }

    // Exhaustive: mul is a bijection on nonzero elements for any nonzero c
    {
        bool ok = true;
        for (int c = 1; c < 256 && ok; ++c) {
            std::set<uint8_t> results;
            for (int x = 1; x < 256; ++x)
                results.insert(GF256::mul((uint8_t)c, (uint8_t)x));
            if ((int)results.size() != 255) { ok = false; }
        }
        check(ok, "GF mul is bijection on nonzero: for each c!=0, {c*x : x!=0} = all 255 nonzero");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [ENC] Encoder Correctness
// ═════════════════════════════════════════════════════════════════════════════

void test_encoder() {
    begin_category("[ENC] Encoder Correctness");
    std::mt19937 rng(123);

    // Systematic property: first k packets of codeword == data exactly
    {
        bool ok = true;
        for (int n : {5,8,11,16,21,32}) {
            for (int k : {2,4,8,16}) {
                if (k >= n) continue;
                auto data = make_random_data(k, 64, rng);
                RSCodec::CauchyCodec codec(n, k);
                auto cw = codec.encode(data);
                for (int i = 0; i < k; ++i)
                    if (cw[i] != data[i]) { ok = false; break; }
                if (!ok) break;
            }
            if (!ok) break;
        }
        check(ok, "Systematic property: codeword[0..k-1] == data exactly");
    }

    // Parity packets are nonzero for nonzero data (almost certainly)
    {
        bool ok = true;
        for (int trial = 0; trial < 20; ++trial) {
            auto data = make_random_data(8, 64, rng);
            RSCodec::CauchyCodec codec(12, 8);
            auto cw = codec.encode(data);
            // Parity packets should NOT all be zero
            bool all_zero = true;
            for (int i = 8; i < 12; ++i)
                for (auto b : cw[i])
                    if (b != 0) { all_zero = false; break; }
            if (all_zero) { ok = false; break; }
        }
        check(ok, "Parity packets nonzero for random nonzero data (20 trials)");
    }

    // Encoding is deterministic: same input → same output
    {
        auto data = make_random_data(8, 128, rng);
        RSCodec::CauchyCodec codec(12, 8);
        auto cw1 = codec.encode(data);
        auto cw2 = codec.encode(data);
        check(cw1 == cw2, "Encoding is deterministic: same data → same codeword");
    }

    // Linearity: encode(a XOR b) == encode(a) XOR encode(b)
    {
        int k = 4, n = 6, pkt_size = 64;
        auto da = make_random_data(k, pkt_size, rng);
        auto db = make_random_data(k, pkt_size, rng);
        // XOR data packets together
        Packets dab(k, Packet(pkt_size));
        for (int i = 0; i < k; ++i)
            for (int j = 0; j < pkt_size; ++j)
                dab[i][j] = da[i][j] ^ db[i][j];

        RSCodec::CauchyCodec codec(n, k);
        auto cwa = codec.encode(da);
        auto cwb = codec.encode(db);
        auto cwab_direct = codec.encode(dab);

        // XOR the two codewords
        Packets cwab_xor(n, Packet(pkt_size));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < pkt_size; ++j)
                cwab_xor[i][j] = cwa[i][j] ^ cwb[i][j];

        check(cwab_direct == cwab_xor, "Linearity: encode(a⊕b) == encode(a)⊕encode(b)");
    }

    // All-zero data encodes to all-zero codeword
    {
        int k = 8, n = 12, pkt_size = 100;
        Packets zeros(k, Packet(pkt_size, 0));
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(zeros);
        bool all_zero = true;
        for (auto& pkt : cw) for (auto b : pkt) if (b != 0) { all_zero = false; break; }
        check(all_zero, "All-zero data encodes to all-zero codeword");
    }

    // Different packet sizes work correctly
    {
        bool ok = true;
        for (int pkt_size : {1, 7, 64, 255, 512, 1500, 4096}) {
            auto data = make_random_data(4, pkt_size, rng);
            RSCodec::CauchyCodec codec(6, 4);
            auto cw = codec.encode(data);
            // Verify systematic property
            for (int i = 0; i < 4; ++i)
                if (cw[i] != data[i]) { ok = false; break; }
            if (!ok) break;
        }
        check(ok, "Encoding works for packet sizes: 1,7,64,255,512,1500,4096 bytes");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [DEC] Decoder — Exhaustive Erasure Patterns
// ═════════════════════════════════════════════════════════════════════════════

// Try ALL combinations of exactly `m` dropped packets from n total
// Returns (pass_count, total_count)
std::pair<int,int> test_all_erasure_patterns(int n, int k, int pkt_size, std::mt19937& rng) {
    auto data = make_random_data(k, pkt_size, rng);
    RSCodec::CauchyCodec codec(n, k);
    auto cw = codec.encode(data);

    int m = n - k;
    int pass = 0, total = 0;

    // Generate all subsets of size m to drop
    std::vector<int> all_indices(n);
    std::iota(all_indices.begin(), all_indices.end(), 0);

    // Use bitmask enumeration for small n
    // Drop exactly m packets = keep exactly k packets
    // We enumerate all C(n,k) subsets of k packets to keep
    std::vector<int> keep_mask(n, 0);
    std::fill(keep_mask.begin(), keep_mask.begin() + k, 1);
    std::sort(keep_mask.begin(), keep_mask.end());

    do {
        Packets recv;
        std::vector<int> idx;
        for (int i = 0; i < n; ++i)
            if (keep_mask[i]) { recv.push_back(cw[i]); idx.push_back(i); }

        try {
            auto recovered = codec.decode(recv, idx, false); // no cache for exhaustive
            if (packets_equal(recovered, data)) ++pass;
        } catch (...) {}
        ++total;
    } while (std::next_permutation(keep_mask.begin(), keep_mask.end()));

    return {pass, total};
}

void test_decoder_exhaustive() {
    begin_category("[DEC] Decoder — Exhaustive Erasure Patterns");
    std::mt19937 rng(777);

    // Small configs: test every possible combination of kept packets
    struct ExhConfig { int n, k, pkt_size; std::string desc; };
    std::vector<ExhConfig> configs = {
        {4,  2, 64,  "RS(4,2)  — C(4,2)=6 patterns"},
        {5,  3, 64,  "RS(5,3)  — C(5,3)=10 patterns"},
        {5,  4, 64,  "RS(5,4)  — C(5,4)=5 patterns"},
        {6,  4, 64,  "RS(6,4)  — C(6,4)=15 patterns"},
        {7,  4, 64,  "RS(7,4)  — C(7,4)=35 patterns"},
        {8,  4, 64,  "RS(8,4)  — C(8,4)=70 patterns"},
        {8,  6, 64,  "RS(8,6)  — C(8,6)=28 patterns"},
        {9,  6, 64,  "RS(9,6)  — C(9,6)=84 patterns"},
        {10, 8, 32,  "RS(10,8) — C(10,8)=45 patterns"},
        {10, 6, 32,  "RS(10,6) — C(10,6)=210 patterns"},
        {12, 8, 32,  "RS(12,8) — C(12,8)=495 patterns"},
    };

    for (auto& cfg : configs) {
        auto [pass, total] = test_all_erasure_patterns(cfg.n, cfg.k, cfg.pkt_size, rng);
        bool ok = (pass == total);
        check(ok, cfg.desc + " → " + std::to_string(pass) + "/" + std::to_string(total));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [STR] Stress Tests — Random erasure patterns, large configs
// ═════════════════════════════════════════════════════════════════════════════

void test_stress() {
    begin_category("[STR] Stress Tests — Random erasure patterns");
    std::mt19937 rng(314159);

    struct StressConfig { int n, k, pkt_size, trials; std::string desc; };
    std::vector<StressConfig> configs = {
        // Various m=1 (single erasure) configs
        {5,  4, 1500, 500,  "RS(5,4)  m=1  pkt=1500B (500 trials)"},
        {9,  8, 1500, 500,  "RS(9,8)  m=1  pkt=1500B (500 trials)"},
        {17,16, 1500, 300,  "RS(17,16) m=1 pkt=1500B (300 trials)"},

        // Various m=2
        {6,  4, 1500, 500,  "RS(6,4)  m=2  pkt=1500B (500 trials)"},
        {10, 8, 1500, 500,  "RS(10,8) m=2  pkt=1500B (500 trials)"},
        {18,16, 1500, 300,  "RS(18,16) m=2 pkt=1500B (300 trials)"},

        // Various m=4
        {8,  4, 1500, 300,  "RS(8,4)  m=4  pkt=1500B (300 trials)"},
        {12, 8, 1500, 300,  "RS(12,8) m=4  pkt=1500B (300 trials)"},
        {20,16, 1500, 200,  "RS(20,16) m=4 pkt=1500B (200 trials)"},

        // High redundancy
        {16, 8, 1500, 200,  "RS(16,8) m=8  pkt=1500B (200 trials)"},
        {32,16, 1500, 200,  "RS(32,16) m=16 pkt=1500B (200 trials)"},

        // Large parity counts — worst case for O(m^3)
        {24,12, 512,  100,  "RS(24,12) m=12 pkt=512B  (100 trials)"},
        {32,16, 512,  100,  "RS(32,16) m=16 pkt=512B  (100 trials)"},

        // Tiny packet size
        {8,  4,    1, 200,  "RS(8,4)  m=4  pkt=1B    (200 trials)"},
        {12, 8,    1, 200,  "RS(12,8) m=4  pkt=1B    (200 trials)"},

        // Large packet size
        {8,  4, 8192, 100,  "RS(8,4)  m=4  pkt=8192B (100 trials)"},
        {16, 8, 8192, 100,  "RS(16,8) m=8  pkt=8192B (100 trials)"},
    };

    for (auto& cfg : configs) {
        int pass = 0;
        RSCodec::CauchyCodec codec(cfg.n, cfg.k);
        int m = cfg.n - cfg.k;

        for (int trial = 0; trial < cfg.trials; ++trial) {
            auto data = make_random_data(cfg.k, cfg.pkt_size, rng);
            auto cw = codec.encode(data);

            // Random subset of exactly m packets to drop
            std::vector<int> all_idx(cfg.n);
            std::iota(all_idx.begin(), all_idx.end(), 0);
            std::shuffle(all_idx.begin(), all_idx.end(), rng);

            std::set<int> drop_set(all_idx.begin(), all_idx.begin() + m);
            auto [recv, idx] = drop_packets(cw, drop_set);

            try {
                auto recovered = codec.decode(recv, idx);
                if (packets_equal(recovered, data)) ++pass;
            } catch (...) {}
        }

        bool ok = (pass == cfg.trials);
        check(ok, cfg.desc + " → " + std::to_string(pass) + "/" + std::to_string(cfg.trials));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [EDG] Edge Cases
// ═════════════════════════════════════════════════════════════════════════════

void test_edge_cases() {
    begin_category("[EDG] Edge Cases and Boundary Conditions");
    std::mt19937 rng(999);

    // k=1 (single data packet)
    {
        int n = 4, k = 1;
        auto data = make_random_data(k, 64, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Recover from ANY single received packet
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            Packets recv = {cw[i]};
            std::vector<int> idx = {i};
            try {
                auto rec = codec.decode(recv, idx);
                if (!packets_equal(rec, data)) { ok = false; break; }
            } catch (...) { ok = false; break; }
        }
        check(ok, "RS(4,1): recover from any single received packet (4 cases)");
    }

    // m=1 (single parity packet) — minimal redundancy
    {
        int n = 9, k = 8;
        auto data = make_random_data(k, 64, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Lose any one packet
        bool ok = true;
        for (int drop = 0; drop < n; ++drop) {
            auto [recv, idx] = drop_packets(cw, {drop});
            try {
                auto rec = codec.decode(recv, idx);
                if (!packets_equal(rec, data)) { ok = false; break; }
            } catch (...) { ok = false; break; }
        }
        check(ok, "RS(9,8) m=1: recover from any single loss (9 cases)");
    }

    // No erasures — fast path [DEC-D]
    {
        int n = 16, k = 8;
        auto data = make_random_data(k, 1500, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Pass only the k data packets
        Packets recv(cw.begin(), cw.begin() + k);
        std::vector<int> idx(k);
        std::iota(idx.begin(), idx.end(), 0);
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "No erasures fast path [DEC-D]: all data present");
    }

    // Exactly at capacity: lose exactly m packets
    {
        int n = 12, k = 8, m = 4;
        auto data = make_random_data(k, 64, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Lose all 4 parity packets — worst case, only data received
        auto [recv, idx] = drop_packets(cw, {8,9,10,11});
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "RS(12,8): no-loss case (drop all parity, keep all data)");
    }

    // Lose all data, keep only parity — maximum stress
    {
        int n = 8, k = 4;
        auto data = make_random_data(k, 64, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Keep only parity packets
        auto [recv, idx] = drop_packets(cw, {0,1,2,3});
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "RS(8,4): recover using ONLY parity packets (drop all data)");
    }

    // Single byte packets
    {
        bool ok = true;
        std::mt19937 rng2(55);
        for (int trial = 0; trial < 200; ++trial) {
            int k = 4, n = 6;
            auto data = make_random_data(k, 1, rng2);
            RSCodec::CauchyCodec codec(n, k);
            auto cw = codec.encode(data);
            std::vector<int> all(n); std::iota(all.begin(), all.end(), 0);
            std::shuffle(all.begin(), all.end(), rng2);
            std::set<int> drop(all.begin(), all.begin()+2);
            auto [recv, idx] = drop_packets(cw, drop);
            try {
                auto rec = codec.decode(recv, idx);
                if (!packets_equal(rec, data)) { ok = false; break; }
            } catch (...) { ok = false; break; }
        }
        check(ok, "Single-byte packets: RS(6,4) 200 random trials");
    }

    // Interleaved receive order (not sorted by index)
    {
        int n = 8, k = 4;
        auto data = make_random_data(k, 64, rng);
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        // Receive packets in shuffled order
        std::vector<int> idx = {3, 7, 1, 5}; // non-sorted, mixed data+parity
        Packets recv = {cw[3], cw[7], cw[1], cw[5]};
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "Shuffled receive order: indices [3,7,1,5] for RS(8,4)");
    }

    // Identical data packets (all same byte pattern)
    {
        int k = 4, n = 6, pkt_size = 64;
        Packets data(k, Packet(pkt_size, 0xAB)); // all same
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        std::vector<int> all(n); std::iota(all.begin(), all.end(), 0);
        std::shuffle(all.begin(), all.end(), rng);
        std::set<int> drop(all.begin(), all.begin()+2);
        auto [recv, idx] = drop_packets(cw, drop);
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "Identical data packets (all 0xAB): RS(6,4)");
    }

    // Maximum field element values (all 0xFF)
    {
        int k = 4, n = 6, pkt_size = 64;
        Packets data(k, Packet(pkt_size, 0xFF));
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        auto [recv, idx] = drop_packets(cw, {0, 1});
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "All 0xFF data: RS(6,4) drop first 2 data packets");
    }

    // Alternating 0x00/0xFF pattern
    {
        int k = 4, n = 6, pkt_size = 64;
        Packets data(k, Packet(pkt_size));
        for (int i = 0; i < k; ++i)
            for (int j = 0; j < pkt_size; ++j)
                data[i][j] = (i+j) % 2 == 0 ? 0x00 : 0xFF;
        RSCodec::CauchyCodec codec(n, k);
        auto cw = codec.encode(data);
        auto [recv, idx] = drop_packets(cw, {1, 3});
        auto recovered = codec.decode(recv, idx);
        check(packets_equal(recovered, data), "Alternating 0x00/0xFF pattern: RS(6,4)");
    }

    // Encode-decode roundtrip with multiple codec instances (no shared state issues)
    {
        bool ok = true;
        for (int i = 0; i < 50; ++i) {
            RSCodec::CauchyCodec c1(8, 4), c2(8, 4);
            auto data = make_random_data(4, 64, rng);
            auto cw1 = c1.encode(data);
            auto cw2 = c2.encode(data);
            if (cw1 != cw2) { ok = false; break; }
        }
        check(ok, "Multiple codec instances produce identical codewords (50 trials)");
    }

    // Different configs don't interfere with each other
    {
        bool ok = true;
        RSCodec::CauchyCodec c1(8,4), c2(12,8), c3(16,8);
        for (int trial = 0; trial < 100 && ok; ++trial) {
            auto d1 = make_random_data(4, 64, rng);
            auto d2 = make_random_data(8, 64, rng);
            auto d3 = make_random_data(8, 64, rng);
            auto cw1 = c1.encode(d1); auto cw2 = c2.encode(d2); auto cw3 = c3.encode(d3);
            // Drop max erasures from each
            auto [r1,i1] = drop_packets(cw1, {0,1,2,3});
            auto [r2,i2] = drop_packets(cw2, {0,1,2,3});
            auto [r3,i3] = drop_packets(cw3, {0,1,2,3,4,5,6,7});
            auto rec1 = c1.decode(r1,i1); auto rec2 = c2.decode(r2,i2); auto rec3 = c3.decode(r3,i3);
            if (!packets_equal(rec1,d1)||!packets_equal(rec2,d2)||!packets_equal(rec3,d3)) ok=false;
        }
        check(ok, "Concurrent use of RS(8,4), RS(12,8), RS(16,8) codecs (100 trials, no interference)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CAC] LRU Cache Correctness
// ═════════════════════════════════════════════════════════════════════════════

void test_cache() {
    begin_category("[CAC] LRU Cache Correctness");
    std::mt19937 rng(2024);

    // Cache hit produces same result as cache miss
    {
        MatrixGF256::cache_clear();
        int n = 12, k = 8;
        RSCodec::CauchyCodec codec(n, k);
        bool ok = true;

        for (int trial = 0; trial < 200 && ok; ++trial) {
            auto data = make_random_data(k, 64, rng);
            auto cw = codec.encode(data);
            std::set<int> drop = {0, 1, 2, 3}; // same erasure pattern = cache hit after first
            auto [recv, idx] = drop_packets(cw, drop);

            auto rec_nocache = codec.decode(recv, idx, false);
            auto rec_cache   = codec.decode(recv, idx, true);
            if (!packets_equal(rec_nocache, rec_cache)) { ok = false; }
        }
        check(ok, "Cache hit result == cache miss result (200 trials, same erasure pattern)");
    }

    // Cache handles different erasure patterns correctly
    {
        MatrixGF256::cache_clear();
        int n = 12, k = 8;
        RSCodec::CauchyCodec codec(n, k);
        bool ok = true;

        // Rotate through several different erasure patterns
        std::vector<std::set<int>> patterns = {
            {0,1,2,3}, {1,2,3,4}, {2,3,4,5}, {3,4,5,6},
            {0,2,4,6}, {1,3,5,7}, {0,1,4,5}, {2,3,6,7}
        };

        for (int trial = 0; trial < 100 && ok; ++trial) {
            auto data = make_random_data(k, 64, rng);
            auto cw = codec.encode(data);
            for (auto& pat : patterns) {
                auto [recv, idx] = drop_packets(cw, pat);
                auto rec_nc = codec.decode(recv, idx, false);
                auto rec_c  = codec.decode(recv, idx, true);
                if (!packets_equal(rec_nc, rec_c)) { ok = false; break; }
            }
        }
        check(ok, "Cache correct across 8 different erasure patterns (100 trials each)");
    }

    // Cache doesn't mix up results between RS(8,4) and RS(12,4) [key includes n,k]
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec c1(8,4), c2(12,4);
        bool ok = true;

        for (int trial = 0; trial < 100 && ok; ++trial) {
            auto d1 = make_random_data(4, 64, rng);
            auto d2 = make_random_data(4, 64, rng);
            auto cw1 = c1.encode(d1);
            auto cw2 = c2.encode(d2);

            // Same logical erasure positions {0,1,2,3} but different codecs
            auto [r1,i1] = drop_packets(cw1, {0,1,2,3});
            auto [r2,i2] = drop_packets(cw2, {0,1,2,3});

            auto rec1_nc = c1.decode(r1,i1,false); auto rec1_c = c1.decode(r1,i1,true);
            auto rec2_nc = c2.decode(r2,i2,false); auto rec2_c = c2.decode(r2,i2,true);

            if (!packets_equal(rec1_nc,rec1_c)||!packets_equal(rec2_nc,rec2_c)) ok=false;
        }
        check(ok, "Cache key includes (n,k): RS(8,4) and RS(12,4) don't pollute each other");
    }

    // Cache eviction: use 40 different patterns to exceed CACHE_SIZE=32
    {
        MatrixGF256::cache_clear();
        int n = 16, k = 8;
        RSCodec::CauchyCodec codec(n, k);
        bool ok = true;

        // Generate 40 distinct erasure patterns (exceeds cache size of 32)
        std::vector<std::set<int>> patterns;
        for (int i = 0; i < n && (int)patterns.size() < 40; ++i)
            for (int j = i+1; j < n && (int)patterns.size() < 40; ++j)
                for (int l = j+1; l < n && (int)patterns.size() < 40; ++l)
                    for (int p = l+1; p < n && (int)patterns.size() < 40; ++p)
                        for (int q = p+1; q < n && (int)patterns.size() < 40; ++q)
                            for (int s = q+1; s < n && (int)patterns.size() < 40; ++s)
                                for (int t = s+1; t < n && (int)patterns.size() < 40; ++t)
                                    for (int u = t+1; u < n && (int)patterns.size() < 40; ++u)
                                        patterns.push_back({i,j,l,p,q,s,t,u});

        for (int trial = 0; trial < 20 && ok; ++trial) {
            auto data = make_random_data(k, 64, rng);
            auto cw = codec.encode(data);
            for (auto& pat : patterns) {
                auto [recv, idx] = drop_packets(cw, pat);
                auto rec_nc = codec.decode(recv, idx, false);
                auto rec_c  = codec.decode(recv, idx, true);
                if (!packets_equal(rec_nc, rec_c)) { ok = false; break; }
            }
        }
        check(ok, "Cache eviction (40 patterns > CACHE_SIZE=32): results still correct");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [INV] Full Roundtrip Invariants — encode then decode always returns original
// ═════════════════════════════════════════════════════════════════════════════

void test_roundtrip_invariants() {
    begin_category("[INV] Full Encode→Decode Roundtrip Invariants");
    std::mt19937 rng(54321);

    // All configs used in the benchmark
    struct Cfg { int n, k; };
    std::vector<Cfg> configs = {
        {5,4},{8,4},{11,8},{16,8},{21,16},{32,16},
        // Extra configs
        {6,4},{6,5},{10,8},{12,8},{15,10},{20,15},
        {7,4},{9,6},{13,9},{17,12},{25,20},{30,24},
    };

    for (auto& cfg : configs) {
        int m = cfg.n - cfg.k;
        int pass = 0, trials = 100;
        RSCodec::CauchyCodec codec(cfg.n, cfg.k);

        for (int trial = 0; trial < trials; ++trial) {
            auto data = make_random_data(cfg.k, 1500, rng);
            auto cw = codec.encode(data);

            // Random erasure pattern (drop exactly m)
            std::vector<int> all(cfg.n); std::iota(all.begin(), all.end(), 0);
            std::shuffle(all.begin(), all.end(), rng);
            std::set<int> drop(all.begin(), all.begin()+m);
            auto [recv, idx] = drop_packets(cw, drop);

            try {
                auto recovered = codec.decode(recv, idx);
                if (packets_equal(recovered, data)) ++pass;
            } catch (...) {}
        }

        bool ok = (pass == trials);
        check(ok, "RS(" + std::to_string(cfg.n) + "," + std::to_string(cfg.k) +
              ") m=" + std::to_string(m) + " — " +
              std::to_string(pass) + "/" + std::to_string(trials) + " roundtrips");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CHN] Channel Model Statistical Tests
// ═════════════════════════════════════════════════════════════════════════════

void test_channel_models() {
    begin_category("[CHN] Channel Model Statistical Properties");

    // GE channel: mean loss rate should be near theoretical value
    {
        double p_gb = 0.02, p_bg = 0.10, p_g = 0.01, p_b = 0.5;
        Channel::GilbertElliott ge(p_gb, p_bg, p_g, p_b, 42);
        auto lost = ge.simulate(200000);
        int losses = 0; for (bool l : lost) if (l) ++losses;
        double measured = (double)losses / (double)lost.size();
        double expected = ge.mean_loss_rate();
        bool ok = std::abs(measured - expected) < 0.01;
        check(ok, "GE mean loss rate within 1% of theoretical (expected=" +
              std::to_string(expected).substr(0,6) + " measured=" +
              std::to_string(measured).substr(0,6) + ")");
    }

    // DC3 channel: loss rate increases with p_nd parameter
    {
        double prev_loss = 0.0;
        bool monotone = true;
        uint64_t seed = 111;
        for (double p_nd : {0.001, 0.005, 0.01, 0.05, 0.10}) {
            Channel::DataCenterChannel dc(p_nd, 0.10, 0.20, 0.50, 0.001, 0.03, 0.35, seed++);
            auto lost = dc.simulate(100000);
            int losses = 0; for (bool l : lost) if (l) ++losses;
            double rate = (double)losses / (double)lost.size();
            if (rate < prev_loss - 0.005) { monotone = false; break; }
            prev_loss = rate;
        }
        check(monotone, "DC3 channel: loss rate increases monotonically with p_nd");
    }

    // DC3 channel: high transition probs -> high loss rate
    {
        Channel::DataCenterChannel dc(0.99, 0.99, 0.01, 0.01, 0.001, 0.03, 0.35, 222);
        auto lost = dc.simulate(100000);
        int losses = 0; for (bool l : lost) if (l) ++losses;
        double rate = (double)losses / (double)lost.size();
        check(rate > 0.10, "DC3 forced INCAST: loss rate > 10% (measured=" +
              std::to_string(rate).substr(0,6) + ")");
    }

    // GE channel: loss burst length mean = 1/(p_bg + (1-p_b))
    // (NOT 1/p_bg — that's the state duration; loss bursts also end when BAD state
    //  doesn't generate a loss, probability (1-p_b) per step)
    {
        double p_gb = 0.10, p_bg = 0.20, p_b = 0.9;
        Channel::GilbertElliott ge(p_gb, p_bg, 0.01, p_b, 333);
        auto lost = ge.simulate(500000);
        std::vector<int> bursts;
        bool in_burst = false; int burst_len = 0;
        for (bool l : lost) {
            if (l) { in_burst = true; burst_len++; }
            else if (in_burst) { bursts.push_back(burst_len); in_burst = false; burst_len = 0; }
        }
        double mean_burst = 0;
        for (int b : bursts) mean_burst += b;
        mean_burst /= std::max((int)bursts.size(), 1);
        double expected_mean = 1.0 / (p_bg + (1.0 - p_b)); // correct formula
        bool ok = std::abs(mean_burst - expected_mean) < 0.5;
        check(ok, "GE loss burst mean = 1/(p_bg+1-p_b)=" + std::to_string(expected_mean).substr(0,5) +
              " (measured=" + std::to_string(mean_burst).substr(0,5) + ")");
    }

    // DC3 near-NORMAL: very low p_nd -> loss rate near p_n
    {
        Channel::DataCenterChannel dc(0.0001, 0.10, 0.90, 0.90, 0.001, 0.03, 0.35, 444);
        auto lost = dc.simulate(200000);
        int losses = 0; for (bool l : lost) if (l) ++losses;
        double rate = (double)losses / (double)lost.size();
        check(rate < 0.01, "DC3 near-NORMAL: loss rate < 1% when p_nd=0.0001 (measured=" +
              std::to_string(rate).substr(0,6) + ")");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [MXT] Mixed concurrent erasure patterns (catches any global state bugs)
// ═════════════════════════════════════════════════════════════════════════════

void test_mixed_concurrent() {
    begin_category("[MXT] Mixed interleaved codec calls (global state isolation)");
    std::mt19937 rng(999999);

    // Interleave encode/decode calls across multiple codecs without resetting
    // If there's any shared mutable global state, this will catch it
    {
        RSCodec::CauchyCodec c1(8,4), c2(12,8), c3(16,8), c4(6,4);
        bool ok = true;
        int trials = 200;

        for (int t = 0; t < trials && ok; ++t) {
            // All four codecs encode simultaneously
            auto d1 = make_random_data(4, 64, rng);
            auto d2 = make_random_data(8, 64, rng);
            auto d3 = make_random_data(8, 64, rng);
            auto d4 = make_random_data(4, 64, rng);

            auto cw1 = c1.encode(d1);
            auto cw2 = c2.encode(d2);
            auto cw3 = c3.encode(d3);
            auto cw4 = c4.encode(d4);

            // Drop max erasures from each
            auto [r1,i1] = drop_packets(cw1, {0,1,2,3});
            auto [r2,i2] = drop_packets(cw2, {0,1,2,3});
            auto [r3,i3] = drop_packets(cw3, {0,1,2,3,4,5,6,7});
            auto [r4,i4] = drop_packets(cw4, {0,1});

            // Interleave decode calls
            auto rec2 = c2.decode(r2,i2);
            auto rec4 = c4.decode(r4,i4);
            auto rec1 = c1.decode(r1,i1);
            auto rec3 = c3.decode(r3,i3);

            if (!packets_equal(rec1,d1)||!packets_equal(rec2,d2)||
                !packets_equal(rec3,d3)||!packets_equal(rec4,d4)) ok = false;
        }
        check(ok, "Interleaved RS(8,4), RS(12,8), RS(16,8), RS(6,4) — 200 trials no interference");
    }

    // Same codec object, sequential calls don't corrupt each other
    {
        RSCodec::CauchyCodec codec(12, 8);
        bool ok = true;
        for (int t = 0; t < 500 && ok; ++t) {
            auto data = make_random_data(8, 64, rng);
            auto cw = codec.encode(data);
            std::vector<int> all(12); std::iota(all.begin(), all.end(), 0);
            std::shuffle(all.begin(), all.end(), rng);
            std::set<int> drop(all.begin(), all.begin()+4);
            auto [recv, idx] = drop_packets(cw, drop);
            auto recovered = codec.decode(recv, idx);
            if (!packets_equal(recovered, data)) ok = false;
        }
        check(ok, "RS(12,8) single codec object reused 500 times sequentially");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  FEC Codec v3 — Comprehensive Test Suite                         ║\n";
    std::cout << "║  Ibrahim Mammadov, ZJU-UIUC 2026                                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    GF256::init();

    auto t_start = std::chrono::steady_clock::now();

    test_gf_arithmetic();
    test_encoder();
    test_decoder_exhaustive();
    test_edge_cases();
    test_stress();
    test_cache();
    test_roundtrip_invariants();
    test_channel_models();
    test_mixed_concurrent();

    auto t_end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  RESULTS                                                         ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Passed:  " << std::setw(4) << g_pass  << " / " << std::setw(4) << g_total << "                                              ║\n";
    std::cout << "║  Failed:  " << std::setw(4) << g_fail  << "                                                    ║\n";
    std::cout << "║  Time:    " << std::fixed << std::setprecision(2) << elapsed << "s                                                 ║\n";
    if (g_fail == 0)
        std::cout << "║  Status:  ALL TESTS PASSED ✓                                     ║\n";
    else
        std::cout << "║  Status:  " << g_fail << " TEST(S) FAILED ✗                                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    return g_fail > 0 ? 1 : 0;
}
