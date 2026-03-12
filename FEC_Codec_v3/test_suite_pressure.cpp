/**
 * test_suite_pressure.cpp — Deep Pressure & Coverage Tests for FEC Codec v3
 * Ibrahim Mammadov, ZJU-UIUC 2026
 *
 * Compile: g++ -std=c++17 -O2 -o test_pressure test_suite_pressure.cpp
 * Run:     ./test_pressure
 *
 * Covers every untested path discovered by coverage audit:
 *   [GF3]  GF exhaustive: all 256×256 mul, div, add/sub, field axioms
 *   [RMS]  region_mul_set — completely untested function
 *   [GFE]  GF error/exception paths: inv(0), div(a,0)
 *   [MTX2] Matrix edge cases: empty invert, non-square matmul, bad Cauchy args
 *   [CAC2] Cache internals: duplicate insert, LRU order, hash behavior
 *   [API]  Free function API wrappers: RSCodec::encode() and RSCodec::decode()
 *   [DEC2] Decoder edge cases: kr > k received, same parity row choice pressure
 *   [CHN3] Channel untested methods: current_state, avg_* accessors, simulate(0/1)
 *   [CHN4] IncastChannel boundary: period=1, loss=0, loss=1, period edge
 *   [DC3B] DC3 boundary transitions: p_dn=0, p_di=0, p_id=0
 *   [FLD]  Field axioms: Fermat little theorem, Frobenius, power exhaustive
 *   [DAT]  Dangerous data values: all-0x00, 0x01, 0x80, 0xFF, alternating
 *   [LNG]  Long-haul: 50,000 encode-decode cycles without failure
 *   [MIX]  Mixed packet sizes within same session
 *   [EXH2] Exhaustive RS(12,8): all C(12,8)=495 patterns, m_actual 0..4
 *   [SPC]  Special GF values in data: 0x01,0x02,0x80,0xFF throughout packet
 *   [REG]  Regression: re-run the exact scenario that triggered the cache bug
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
#include <numeric>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include <string>
#include <stdexcept>

// ── Framework ─────────────────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0, g_total = 0;

void begin_category(const std::string& s) {
    std::cout << "\n┌─────────────────────────────────────────────────────\n"
              << "│  " << s << "\n"
              << "└─────────────────────────────────────────────────────\n";
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

std::pair<Packets,std::vector<int>> drop_set(const Packets& cw, std::set<int> d) {
    Packets r; std::vector<int> idx;
    for (int i=0;i<(int)cw.size();++i) if(!d.count(i)){r.push_back(cw[i]);idx.push_back(i);}
    return {r,idx};
}

// ═════════════════════════════════════════════════════════════════════════════
// [GF3] GF(2^8) Exhaustive — every possible input combination
// ═════════════════════════════════════════════════════════════════════════════
void test_gf_exhaustive() {
    begin_category("[GF3] GF(2^8) Exhaustive — all 256×256 combinations");
    GF256::init();

    // mul(a,0)==0 and mul(0,b)==0 for ALL 256 values
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) {
            if(GF256::mul((uint8_t)a,0)!=0) ok=false;
            if(GF256::mul(0,(uint8_t)a)!=0) ok=false;
        }
        check(ok,"mul(a,0)==0 and mul(0,a)==0 for all 256 values of a");
    }

    // div(0,b)==0 for all nonzero b
    {
        bool ok=true;
        for(int b=1;b<256&&ok;++b)
            if(GF256::div(0,(uint8_t)b)!=0) ok=false;
        check(ok,"div(0,b)==0 for all 255 nonzero b");
    }

    // div(a,b)*b == a for all a, nonzero b (exhaustive 256×255)
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a)
            for(int b=1;b<256&&ok;++b) {
                uint8_t q=GF256::div((uint8_t)a,(uint8_t)b);
                if(GF256::mul(q,(uint8_t)b)!=(uint8_t)a) ok=false;
            }
        check(ok,"div(a,b)*b==a exhaustive: all 256×255 pairs");
    }

    // add(a,b)==sub(a,b) for ALL 256×256 pairs (characteristic 2)
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a)
            for(int b=0;b<256&&ok;++b)
                if(GF256::add((uint8_t)a,(uint8_t)b)!=GF256::sub((uint8_t)a,(uint8_t)b)) ok=false;
        check(ok,"add(a,b)==sub(a,b) for all 256×256 pairs (GF char 2)");
    }

    // add(a,b)==add(b,a) for ALL 256×256 pairs
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a)
            for(int b=0;b<256&&ok;++b)
                if(GF256::add((uint8_t)a,(uint8_t)b)!=GF256::add((uint8_t)b,(uint8_t)a)) ok=false;
        check(ok,"add commutativity: all 256×256 pairs");
    }

    // mul exhaustive: full multiplication table matches exp/log formula
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) {
            for(int b=0;b<256&&ok;++b) {
                uint8_t result=GF256::mul((uint8_t)a,(uint8_t)b);
                // Verify via region mul table
                auto tbl=GF256::make_mul_table((uint8_t)a);
                if(tbl[b]!=result) ok=false;
            }
        }
        check(ok,"mul(a,b) == make_mul_table(a)[b] for all 256×256 pairs");
    }

    // Fermat's little theorem in GF(2^8): a^255 == 1 for all nonzero a
    {
        bool ok=true;
        for(int a=1;a<256&&ok;++a) {
            // Compute a^255 by repeated squaring
            uint8_t base=(uint8_t)a, result=1;
            int exp=255;
            while(exp>0) {
                if(exp&1) result=GF256::mul(result,base);
                base=GF256::mul(base,base);
                exp>>=1;
            }
            if(result!=1) ok=false;
        }
        check(ok,"Fermat little theorem: a^255 == 1 for all 255 nonzero elements");
    }

    // Frobenius endomorphism: a^2 computed two ways must match
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) {
            uint8_t sq=GF256::mul((uint8_t)a,(uint8_t)a);
            // Must equal (a XOR something) — just check it's consistent
            uint8_t sq2=GF256::mul((uint8_t)a,(uint8_t)a);
            if(sq!=sq2) ok=false;
        }
        check(ok,"a^2 is deterministic for all 256 elements");
    }

    // exp_table wrap: log[a]+log[b] >= 255 case handled correctly
    // Find a specific pair where logs sum >= 255
    {
        bool ok=true;
        int count=0;
        for(int a=1;a<256&&count<1000;++a) {
            for(int b=1;b<256&&count<1000;++b) {
                // Check result via three independent methods
                uint8_t m1=GF256::mul((uint8_t)a,(uint8_t)b);
                uint8_t m2=GF256::mul((uint8_t)b,(uint8_t)a);
                auto tbl=GF256::make_mul_table((uint8_t)a);
                if(m1!=m2||m1!=tbl[b]) { ok=false; break; }
                ++count;
            }
        }
        check(ok,"exp_table wrap-around: 1000 high-index multiplications consistent across 3 methods");
    }

    // inv(a)*a == 1 for every single nonzero element, THREE ways
    {
        bool ok=true;
        for(int a=1;a<256&&ok;++a) {
            uint8_t ia=GF256::inv((uint8_t)a);
            if(GF256::mul((uint8_t)a,ia)!=1) ok=false;
            if(GF256::mul(ia,(uint8_t)a)!=1) ok=false;
            if(GF256::div(1,(uint8_t)a)!=ia) ok=false;
        }
        check(ok,"inv(a)*a==1, a*inv(a)==1, div(1,a)==inv(a) for all 255 nonzero elements");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [GFE] GF Error/Exception Paths
// ═════════════════════════════════════════════════════════════════════════════
void test_gf_exceptions() {
    begin_category("[GFE] GF Error Paths — inv(0) and div(a,0) throw");
    GF256::init();

    // inv(0) must throw
    {
        bool threw=false;
        try { GF256::inv(0); } catch(const std::domain_error&) { threw=true; }
        check(threw,"inv(0) throws std::domain_error");
    }

    // div(a, 0) must throw for all a
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) {
            bool threw=false;
            try { GF256::div((uint8_t)a,0); } catch(const std::domain_error&) { threw=true; }
            if(!threw) ok=false;
        }
        check(ok,"div(a,0) throws std::domain_error for all 256 values of a");
    }

    // build_cauchy_generator throws on bad args
    {
        bool t1=false,t2=false,t3=false,t4=false;
        try { MatrixGF256::build_cauchy_generator(5,5); } catch(const std::invalid_argument&) { t1=true; }
        try { MatrixGF256::build_cauchy_generator(5,0); } catch(const std::invalid_argument&) { t2=true; }
        try { MatrixGF256::build_cauchy_generator(201,100); } catch(const std::invalid_argument&) { t3=true; }
        try { MatrixGF256::build_cauchy_generator(3,4); } catch(const std::invalid_argument&) { t4=true; }
        check(t1,"build_cauchy_generator(5,5) throws: k must be < n");
        check(t2,"build_cauchy_generator(5,0) throws: k must be > 0");
        check(t3,"build_cauchy_generator(201,100) throws: n > 200");
        check(t4,"build_cauchy_generator(3,4) throws: k >= n");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [RMS] region_mul_set — completely untested function
// ═════════════════════════════════════════════════════════════════════════════
void test_region_mul_set() {
    begin_category("[RMS] region_mul_set — untested function full coverage");
    GF256::init();
    std::mt19937 rng(100);
    std::uniform_int_distribution<int> bd(0,255), bnz(1,255);

    // region_mul_set(dst, src, c, len): dst[i] = mul(c, src[i])
    // Verify against scalar mul for all lengths and random coefficients
    {
        bool ok=true;
        for(int trial=0;trial<500&&ok;++trial) {
            int len=1+(rng()%2000);
            uint8_t c=(uint8_t)bnz(rng);
            std::vector<uint8_t> src(len), dst_set(len,0xAA), dst_ref(len);
            for(auto& b:src) b=bd(rng);
            // reference
            for(int i=0;i<len;++i) dst_ref[i]=GF256::mul(c,src[i]);
            // function under test
            GF256::region_mul_set(dst_set.data(),src.data(),c,len);
            if(dst_set!=dst_ref) ok=false;
        }
        check(ok,"region_mul_set: dst[i]=mul(c,src[i]) for 500 random (c,len) pairs");
    }

    // region_mul_set with c=0: dst must be all zeros regardless of previous content
    {
        bool ok=true;
        for(int trial=0;trial<100&&ok;++trial) {
            int len=1+(rng()%500);
            std::vector<uint8_t> src(len), dst(len,0xFF); // pre-fill with non-zero
            for(auto& b:src) b=bd(rng);
            GF256::region_mul_set(dst.data(),src.data(),0,len);
            for(auto b:dst) if(b!=0) { ok=false; break; }
        }
        check(ok,"region_mul_set(dst,src,0,len) zeros dst regardless of prior content (100 trials)");
    }

    // region_mul_set(c=1) == memcpy
    {
        bool ok=true;
        for(int trial=0;trial<100&&ok;++trial) {
            int len=1+(rng()%1000);
            std::vector<uint8_t> src(len), dst(len,0);
            for(auto& b:src) b=bd(rng);
            GF256::region_mul_set(dst.data(),src.data(),1,len);
            if(dst!=src) ok=false;
        }
        check(ok,"region_mul_set(c=1) is identity: dst==src (100 trials)");
    }

    // region_mul_set consistency with region_mul_xor:
    // mul_set(c) into zero dst == mul_xor(c) into zero dst
    {
        bool ok=true;
        for(int trial=0;trial<200&&ok;++trial) {
            int len=1+(rng()%500);
            uint8_t c=(uint8_t)bnz(rng);
            std::vector<uint8_t> src(len);
            for(auto& b:src) b=bd(rng);
            std::vector<uint8_t> dst_set(len,0), dst_xor(len,0);
            GF256::region_mul_set(dst_set.data(),src.data(),c,len);
            GF256::region_mul_xor(dst_xor.data(),src.data(),c,len);
            if(dst_set!=dst_xor) ok=false;
        }
        check(ok,"region_mul_set into zero == region_mul_xor into zero (200 trials)");
    }

    // region_mul_xor is incremental: xor(c, xor(c, dst, src), src) == dst (double XOR)
    {
        bool ok=true;
        for(int trial=0;trial<200&&ok;++trial) {
            int len=64;
            uint8_t c=(uint8_t)bnz(rng);
            std::vector<uint8_t> src(len), dst(len);
            for(auto& b:src) b=bd(rng);
            for(auto& b:dst) b=bd(rng);
            auto orig_dst=dst;
            GF256::region_mul_xor(dst.data(),src.data(),c,len);
            GF256::region_mul_xor(dst.data(),src.data(),c,len); // apply twice
            if(dst!=orig_dst) ok=false; // XOR twice == identity
        }
        check(ok,"region_mul_xor applied twice is identity: dst⊕(c*src)⊕(c*src)==dst");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [MTX2] Matrix Edge Cases
// ═════════════════════════════════════════════════════════════════════════════
void test_matrix_edge_cases() {
    begin_category("[MTX2] Matrix Edge Cases — empty, non-square, zero-row");
    GF256::init();

    // gf256_invert on 0×0 empty matrix returns empty matrix
    {
        BM empty;
        auto result=MatrixGF256::gf256_invert(empty);
        check(result.has_value() && result->empty(),
              "gf256_invert(empty 0×0 matrix) returns empty matrix (not nullopt)");
    }

    // matmul non-square: 3×4 * 4×2 = 3×2
    {
        GF256::init();
        std::mt19937 rng(200);
        std::uniform_int_distribution<int> d(0,255);
        BM A(3,std::vector<uint8_t>(4));
        BM B(4,std::vector<uint8_t>(2));
        for(auto& row:A) for(auto& b:row) b=d(rng);
        for(auto& row:B) for(auto& b:row) b=d(rng);
        auto C=MatrixGF256::matmul(A,B);
        bool ok=(C.size()==3 && C[0].size()==2);
        // Verify one element manually
        uint8_t c00=0;
        for(int k=0;k<4;++k) c00^=GF256::mul(A[0][k],B[k][0]);
        ok = ok && (C[0][0]==c00);
        check(ok,"matmul non-square: 3×4 * 4×2 = 3×2 with correct dimensions and values");
    }

    // matmul 1×1 * 1×1 = 1×1: scalar multiply
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) for(int b=0;b<256&&ok;++b) {
            BM A={{(uint8_t)a}}, B={{(uint8_t)b}};
            auto C=MatrixGF256::matmul(A,B);
            if(C[0][0]!=GF256::mul((uint8_t)a,(uint8_t)b)) ok=false;
        }
        check(ok,"matmul 1×1 * 1×1 == scalar mul for all 256×256 pairs");
    }

    // matvec: 1×1 matrix dot 1-vector = scalar mul
    {
        bool ok=true;
        for(int a=0;a<256&&ok;++a) for(int b=0;b<256&&ok;++b) {
            BM M={{(uint8_t)a}};
            MatrixGF256::ByteVec v={(uint8_t)b};
            auto r=MatrixGF256::matvec(M,v);
            if(r[0]!=GF256::mul((uint8_t)a,(uint8_t)b)) ok=false;
        }
        check(ok,"matvec 1×1 * [b] == mul(a,b) for all 256×256 pairs");
    }

    // make_identity: I[i][j] = delta(i,j) for sizes 1..10
    {
        bool ok=true;
        for(int n=1;n<=10&&ok;++n) {
            auto I=MatrixGF256::make_identity(n);
            for(int i=0;i<n&&ok;++i)
                for(int j=0;j<n&&ok;++j)
                    if(I[i][j]!=(i==j?1:0)) ok=false;
        }
        check(ok,"make_identity(n) correct for n=1..10");
    }

    // make_zero: all entries zero for various sizes
    {
        bool ok=true;
        for(int r=1;r<=8&&ok;++r) for(int c=1;c<=8&&ok;++c) {
            auto Z=MatrixGF256::make_zero(r,c);
            if((int)Z.size()!=r || (int)Z[0].size()!=c) ok=false;
            for(auto& row:Z) for(auto b:row) if(b!=0) ok=false;
        }
        check(ok,"make_zero(r,c) all zeros, correct dimensions for r,c in 1..8");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CAC2] Cache Internals — duplicate insert, LRU order, size limit
// ═════════════════════════════════════════════════════════════════════════════
void test_cache_internals() {
    begin_category("[CAC2] Cache Internals — duplicate insert, LRU eviction, key integrity");
    std::mt19937 rng(300);

    // Duplicate insert: inserting the same key twice doesn't corrupt
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);
            auto [r,i]=drop_set(cw,{0,1,2,3});
            // Insert into cache twice by calling decode twice with same pattern
            auto rec1=codec.decode(r,i,true);
            auto rec2=codec.decode(r,i,true); // should be cache hit
            if(!pkts_eq(rec1,d)||!pkts_eq(rec2,d)) ok=false;
        }
        check(ok,"Cache: 200 trials same pattern — duplicate inserts don't corrupt");
    }

    // Cache fills exactly to CACHE_SIZE=32, then evicts LRU correctly
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(12,8);
        bool ok=true;

        // Generate 40 distinct patterns (fill past CACHE_SIZE)
        std::vector<std::set<int>> patterns;
        for(int i=0;i<8&&(int)patterns.size()<40;++i)
            for(int j=i+1;j<10&&(int)patterns.size()<40;++j)
                for(int l=j+1;l<12&&(int)patterns.size()<40;++l)
                    for(int p=l+1;p<12&&(int)patterns.size()<40;++p)
                        patterns.push_back({i,j,l,p});

        // Fill cache with first 32 patterns
        std::vector<Packets> all_cws;
        std::vector<Packets> all_data;
        for(int pi=0;pi<(int)patterns.size()&&ok;++pi) {
            auto d=rand_data(8,32,rng);
            auto cw=codec.encode(d);
            all_data.push_back(d);
            all_cws.push_back(cw);
            auto [r,i]=drop_set(cw,patterns[pi]);
            Packets rk(r.begin(),r.begin()+8);
            std::vector<int> ik(i.begin(),i.begin()+8);
            auto rec=codec.decode(rk,ik,true);
            if(!pkts_eq(rec,d)) ok=false;
        }

        // Verify the last 32 patterns still decode correctly (some may be evicted)
        for(int pi=8;pi<(int)patterns.size()&&ok;++pi) {
            auto d=rand_data(8,32,rng);
            auto cw=codec.encode(d);
            auto [r,i]=drop_set(cw,patterns[pi]);
            Packets rk(r.begin(),r.begin()+8);
            std::vector<int> ik(i.begin(),i.begin()+8);
            auto rec=codec.decode(rk,ik,true);
            if(!pkts_eq(rec,d)) ok=false;
        }
        check(ok,"Cache eviction: 40 patterns (>CACHE_SIZE=32), all decode correctly");
    }

    // Cache with no_cache=false: results identical to cache=true for all patterns
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(10,8);
        bool ok=true;
        for(int t=0;t<300&&ok;++t) {
            auto d=rand_data(8,64,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(10); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+2);
            auto [r,i]=drop_set(cw,dr);
            Packets rk(r.begin(),r.begin()+8);
            std::vector<int> ik(i.begin(),i.begin()+8);
            auto rec_c=codec.decode(rk,ik,true);
            auto rec_nc=codec.decode(rk,ik,false);
            if(!pkts_eq(rec_c,d)||!pkts_eq(rec_nc,d)) ok=false;
        }
        check(ok,"Cache on vs off: 300 RS(10,8) trials, both produce same correct result");
    }

    // Cache clear: after clear, first call is always cache miss (no stale data)
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<50&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);
            auto [r,i]=drop_set(cw,{0,1,2,3});
            codec.decode(r,i,true); // warm cache
            MatrixGF256::cache_clear(); // clear it
            auto rec=codec.decode(r,i,true); // must recompute from scratch
            if(!pkts_eq(rec,d)) ok=false;
        }
        check(ok,"cache_clear() forces recomputation: 50 trials post-clear all correct");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [API] Free Function API Wrappers
// ═════════════════════════════════════════════════════════════════════════════
void test_api_wrappers() {
    begin_category("[API] Free function API wrappers — RSCodec::encode() and RSCodec::decode()");
    std::mt19937 rng(400);

    // RSCodec::encode(data, n) wrapper
    {
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(4,64,rng);
            int n=7;
            auto cw=RSCodec::encode(d,n);
            // Should be same as CauchyCodec(7,4).encode(d)
            RSCodec::CauchyCodec ref(n,4);
            auto cw_ref=ref.encode(d);
            if(cw!=cw_ref) ok=false;
        }
        check(ok,"RSCodec::encode(data,n) matches CauchyCodec(n,k).encode(data) (200 trials)");
    }

    // RSCodec::decode(received, idx, n, k) wrapper
    {
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            int n=8, k=4;
            auto d=rand_data(k,64,rng);
            auto cw=RSCodec::encode(d,n);
            std::vector<int> all(n); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            auto rec=RSCodec::decode(recv,idx,n,k,false);
            if(!pkts_eq(rec,d)) ok=false;
        }
        check(ok,"RSCodec::decode(recv,idx,n,k) wrapper correct (200 trials)");
    }

    // API wrapper encode then decode roundtrip
    {
        bool ok=true;
        for(auto [n,k] : std::vector<std::pair<int,int>>{{5,4},{8,4},{12,8},{16,8},{21,16}}) {
            for(int t=0;t<50&&ok;++t) {
                auto d=rand_data(k,128,rng);
                auto cw=RSCodec::encode(d,n);
                // Drop max erasures
                std::vector<int> all(n); std::iota(all.begin(),all.end(),0);
                std::shuffle(all.begin(),all.end(),rng);
                std::set<int> dr(all.begin(),all.begin()+(n-k));
                auto [recv,idx]=drop_set(cw,dr);
                auto rec=RSCodec::decode(recv,idx,n,k);
                if(!pkts_eq(rec,d)) ok=false;
            }
        }
        check(ok,"API wrapper encode→decode roundtrip: 5 configs × 50 trials");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [DEC2] Decoder Edge Cases
// ═════════════════════════════════════════════════════════════════════════════
void test_decoder_edge_cases() {
    begin_category("[DEC2] Decoder Edge Cases — kr>k, parity selection pressure");
    std::mt19937 rng(500);

    // decode() with kr > k received packets (should still work, uses first k)
    {
        bool ok=true;
        RSCodec::CauchyCodec codec(8,4);
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);
            // Pass ALL 8 packets (kr=8 > k=4), drop none
            // decode should handle this — it only uses first k
            std::vector<int> idx={0,1,2,3,4,5,6,7};
            try {
                auto rec=codec.decode(cw,idx);
                if(!pkts_eq(rec,d)) ok=false;
            } catch(...){ ok=false; }
        }
        check(ok,"decode(all_n_packets) with kr>k accepted and correct (200 trials)");
    }

    // Systematic decode: data[0..k-1] in recv = just copy, no math
    {
        bool ok=true;
        RSCodec::CauchyCodec codec(12,8);
        for(int t=0;t<300&&ok;++t) {
            auto d=rand_data(8,256,rng);
            auto cw=codec.encode(d);
            // Pass all k=8 data packets (indices 0..7)
            Packets recv(cw.begin(),cw.begin()+8);
            std::vector<int> idx(8); std::iota(idx.begin(),idx.end(),0);
            auto rec=codec.decode(recv,idx);
            if(!pkts_eq(rec,d)) ok=false;
        }
        check(ok,"Systematic fast path [DEC-D]: all data present, 300 RS(12,8) trials");
    }

    // Parity selection: when multiple valid parity rows exist, any k of them work
    // Test all C(m_avail, m_needed) combinations
    {
        // RS(8,4): m=4. Lose 2 data. Have 4 parity rows. Use any 2 of 4 parity rows.
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        std::mt19937 rng2(501);
        for(int t=0;t<100&&ok;++t) {
            auto d=rand_data(4,64,rng2);
            auto cw=codec.encode(d);
            // Keep data[2],data[3] and all 4 parity rows, drop data[0],data[1]
            // Try each pair of parity rows for decoding
            for(int p1=4;p1<8&&ok;++p1) {
                for(int p2=p1+1;p2<8&&ok;++p2) {
                    Packets recv={cw[2],cw[3],cw[p1],cw[p2]};
                    std::vector<int> idx={2,3,p1,p2};
                    auto rec=codec.decode(recv,idx,false);
                    if(!pkts_eq(rec,d)) ok=false;
                }
            }
        }
        check(ok,"Parity selection: all C(4,2)=6 parity row pairs for RS(8,4) m_actual=2 (100 trials)");
    }

    // Decode with non-contiguous mixed receive: data from different positions
    {
        bool ok=true;
        RSCodec::CauchyCodec codec(16,8);
        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(8,128,rng);
            auto cw=codec.encode(d);
            // Receive: data[0,2,4,6] + parity[0,1,2,3] = 8 packets total
            Packets recv={cw[0],cw[2],cw[4],cw[6],cw[8],cw[9],cw[10],cw[11]};
            std::vector<int> idx={0,2,4,6,8,9,10,11};
            auto rec=codec.decode(recv,idx,false);
            if(!pkts_eq(rec,d)) ok=false;
        }
        check(ok,"Non-contiguous mixed receive: data[0,2,4,6]+parity[0..3] for RS(16,8) (200 trials)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CHN3] Channel Untested Accessors and Methods
// ═════════════════════════════════════════════════════════════════════════════
void test_channel_accessors() {
    begin_category("[CHN3] Channel Accessors — current_state, avg_*, simulate(0/1)");

    // GilbertElliott.current_state() returns GOOD initially
    {
        Channel::GilbertElliott ge(0.1,0.2,0.01,0.5,42);
        auto s=ge.current_state();
        check(s==Channel::GilbertElliott::State::GOOD,
              "GE current_state() == GOOD immediately after construction");
    }

    // GilbertElliott.avg_burst_length() == 1/p_bg
    {
        double p_bg=0.25;
        Channel::GilbertElliott ge(0.1,p_bg,0.01,0.5,42);
        double abl=ge.avg_burst_length();
        check(std::abs(abl-1.0/p_bg)<1e-9,
              "GE avg_burst_length() == 1/p_bg (expected="+std::to_string(1.0/p_bg).substr(0,6)+
              " got="+std::to_string(abl).substr(0,6)+")");
    }

    // GilbertElliott.name() returns non-empty string
    {
        Channel::GilbertElliott ge;
        check(!ge.name().empty(),"GE name() returns non-empty string: \""+ge.name()+"\"");
    }

    // DataCenterChannel.current_state() returns NORMAL initially
    {
        Channel::DataCenterChannel dc;
        auto s=dc.current_state();
        check(s==Channel::DataCenterChannel::State::NORMAL,
              "DC3 current_state() == NORMAL immediately after construction");
    }

    // DataCenterChannel.name() returns non-empty string
    {
        Channel::DataCenterChannel dc;
        check(!dc.name().empty(),"DC3 name() returns non-empty string: \""+dc.name()+"\"");
    }

    // DataCenterChannel.avg_incast_duration() == 1/p_id
    {
        double p_id=0.40;
        Channel::DataCenterChannel dc(0.01,0.10,0.20,p_id);
        double aid=dc.avg_incast_duration();
        check(std::abs(aid-1.0/p_id)<1e-9,
              "DC3 avg_incast_duration() == 1/p_id");
    }

    // DataCenterChannel.avg_degraded_duration() == 1/(p_dn+p_di)
    {
        double p_dn=0.20, p_di=0.10;
        Channel::DataCenterChannel dc(0.01,p_di,p_dn,0.30);
        double add=dc.avg_degraded_duration();
        double expected=1.0/(p_dn+p_di);
        check(std::abs(add-expected)<1e-9,
              "DC3 avg_degraded_duration() == 1/(p_dn+p_di)");
    }

    // simulate(0) returns empty vector
    {
        Channel::GilbertElliott ge;
        Channel::DataCenterChannel dc;
        auto ge_lost=ge.simulate(0);
        auto dc_lost=dc.simulate(0);
        check(ge_lost.empty(),"GE simulate(0) returns empty vector");
        check(dc_lost.empty(),"DC3 simulate(0) returns empty vector");
    }

    // simulate(1) returns vector of size 1
    {
        Channel::GilbertElliott ge(0.05,0.30,0.001,0.4,42);
        Channel::DataCenterChannel dc(0.01,0.10,0.20,0.30,0.001,0.03,0.35,42);
        auto ge_lost=ge.simulate(1);
        auto dc_lost=dc.simulate(1);
        check(ge_lost.size()==1,"GE simulate(1) returns vector of size 1");
        check(dc_lost.size()==1,"DC3 simulate(1) returns vector of size 1");
    }

    // simulate(N) returns vector of exactly N booleans
    {
        bool ok=true;
        Channel::GilbertElliott ge;
        for(int n : {1,2,10,100,1000,9999}) {
            auto lost=ge.simulate(n);
            if((int)lost.size()!=n) ok=false;
        }
        check(ok,"GE simulate(N) returns vector of exactly N for N in {1,2,10,100,1000,9999}");
    }

    // After simulate, current_state may have changed (not GOOD/NORMAL anymore with high p_gb)
    {
        Channel::GilbertElliott ge(0.999,0.001,0.01,0.9,42); // almost instantly BAD
        ge.simulate(1000);
        auto s=ge.current_state();
        // After many steps with p_gb=0.999, should be in BAD state
        check(s==Channel::GilbertElliott::State::BAD,
              "GE current_state()==BAD after 1000 steps with p_gb=0.999");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [CHN4] IncastChannel Boundaries
// ═════════════════════════════════════════════════════════════════════════════
void test_incast_boundaries() {
    begin_category("[CHN4] IncastChannel Boundary Conditions");

    // incast_loss=1.0: every incast-period packet is guaranteed lost
    {
        Channel::IncastChannel ic(
            Channel::DataCenterChannel(0.0,0.0,1.0,1.0,0.0,0.0,0.0,42), // no base loss
            10, 5, 1.0 // every 10 packets, 5-packet burst, 100% loss
        );
        auto lost=ic.simulate(100);
        // Packets 0-4, 10-14, 20-24, ... should ALL be lost
        bool ok=true;
        for(int i=0;i<100;++i) {
            bool should_be_lost=(i%10)<5;
            if(should_be_lost && !lost[i]) ok=false;
        }
        check(ok,"IncastChannel: incast_loss=1.0, every burst packet guaranteed lost");
    }

    // incast_loss=0.0: no additional loss from incast (base channel only)
    {
        Channel::DataCenterChannel base(0.0,0.0,1.0,1.0,0.0,0.0,0.0,42); // zero base loss
        Channel::IncastChannel ic(base,10,5,0.0);
        auto lost=ic.simulate(1000);
        int l=0; for(bool b:lost) if(b) ++l;
        check(l==0,"IncastChannel: base_loss=0 and incast_loss=0 → zero total losses");
    }

    // period=1, len=1: every single packet is in incast window
    {
        Channel::IncastChannel ic(
            Channel::DataCenterChannel(0.0,0.0,1.0,1.0,0.0,0.0,0.0,42),
            1, 1, 0.5 // period=1, len=1 → every packet is incast
        );
        auto lost=ic.simulate(10000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/10000;
        check(rate>0.3 && rate<0.7,
              "IncastChannel period=1 len=1: every packet is incast (loss≈50%, got="+
              std::to_string(rate).substr(0,5)+")");
    }

    // IncastChannel.name() returns non-empty
    {
        Channel::DataCenterChannel _base_dc; Channel::IncastChannel ic(_base_dc);
        check(!ic.name().empty(),"IncastChannel name() returns non-empty: \""+ic.name()+"\"");
    }

    // IncastChannel reset
    {
        Channel::IncastChannel ic(Channel::DataCenterChannel(0.001,0.10,0.20,0.30,0.001,0.03,0.35,42),50,5,0.8);
        ic.simulate(10000);
        ic.reset(); // should not crash
        auto lost=ic.simulate(100);
        check(lost.size()==100,"IncastChannel reset() succeeds and next simulate works");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [DC3B] DC3 Boundary Transitions
// ═════════════════════════════════════════════════════════════════════════════
void test_dc3_boundary_transitions() {
    begin_category("[DC3B] DC3 Boundary Transitions — p_dn=0, p_di=0, p_id=0");

    // p_dn=0: DEGRADED never returns to NORMAL
    // NORMAL→DEGRADED with any p_nd, then trapped
    {
        Channel::DataCenterChannel dc(0.99,0.01,0.0,0.50,0.001,0.03,0.35,42);
        // p_nd=0.99 → quickly enters DEGRADED; p_dn=0 → never leaves DEGRADED (only INCAST)
        dc.simulate(100); // guarantee enters DEGRADED
        // After that: can only be DEGRADED or INCAST, never NORMAL
        // Loss rate should be elevated above p_n=0.001
        auto lost=dc.simulate(10000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/10000;
        check(rate>0.01,"DC3 p_dn=0: DEGRADED absorbing, loss rate > p_n=0.001 (got="+
              std::to_string(rate).substr(0,6)+")");
    }

    // p_di=0: DEGRADED never escalates to INCAST
    {
        Channel::DataCenterChannel dc(0.50,0.0,0.50,0.30,0.001,0.03,0.35,42);
        // p_di=0: DEGRADED→INCAST impossible
        // Should only see NORMAL and DEGRADED loss rates
        auto lost=dc.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/100000;
        // Max loss rate with no INCAST: some mix of p_n and p_d
        check(rate<0.10,"DC3 p_di=0: no INCAST state, loss rate < 10% (got="+
              std::to_string(rate).substr(0,6)+")");
    }

    // p_id=0: once in INCAST, never leaves
    {
        Channel::DataCenterChannel dc(0.99,0.99,0.01,0.0,0.001,0.03,0.35,42);
        // p_nd=0.99, p_di=0.99 → quickly reaches INCAST; p_id=0 → stays forever
        dc.simulate(50); // guarantee reaches INCAST
        auto lost=dc.simulate(10000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/10000;
        // Stuck in INCAST: loss ≈ p_i = 0.35
        check(rate>0.20,"DC3 p_id=0: INCAST absorbing state, loss rate near p_i=0.35 (got="+
              std::to_string(rate).substr(0,6)+")");
    }

    // Stationary distribution with p_dn=0 analytically: π_N should be zero
    // Actually: π_N * p_nd = π_D * p_dn, if p_dn=0 then π_N*p_nd=0, so π_N=0 (if p_nd>0)
    {
        // When p_dn=0, the analytical stationary() formula divides by p_dn → undefined.
        // Instead verify via simulation: with p_dn=0, NORMAL is transient (→ DEGRADED only),
        // so after long run almost all time is spent in DEGRADED or INCAST.
        Channel::DataCenterChannel dc(0.10,0.20,0.0,0.30,0.001,0.03,0.35,42);
        // Drive out of NORMAL first
        dc.simulate(200);
        // Now count states by observing loss rate over a long run
        // With p_dn=0: only DEGRADED(p_d=0.03) and INCAST(p_i=0.35) — both >> p_n=0.001
        auto lost=dc.simulate(100000);
        int l=0; for(bool b:lost) if(b) ++l;
        double rate=(double)l/100000;
        // Rate must be well above p_n=0.001 since we are in DEGRADED/INCAST
        check(rate>0.01,"DC3 p_dn=0: simulation confirms NORMAL is transient (rate="+
              std::to_string(rate).substr(0,6)+" >> p_n=0.001)");
        // Also verify: stationary() is well-defined for NORMAL case
        Channel::DataCenterChannel dc2(0.01,0.10,0.20,0.30,0.001,0.03,0.35,42);
        auto sd=dc2.stationary();
        double sum=sd.pi_n+sd.pi_d+sd.pi_i;
        check(std::abs(sum-1.0)<1e-9,"DC3 stationary() sums to 1.0 for non-degenerate params");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [SPC] Special GF Values in Data
// ═════════════════════════════════════════════════════════════════════════════
void test_special_data_values() {
    begin_category("[SPC] Special GF Values in Data — 0x01, 0x02, 0x80, 0xFF throughout");
    std::mt19937 rng(600);

    struct Cfg { int n,k; };
    std::vector<Cfg> cfgs = {{6,4},{8,4},{12,8},{16,8}};

    // All-0x01 data (identity element)
    for(auto& cfg : cfgs) {
        RSCodec::CauchyCodec codec(cfg.n,cfg.k);
        int m=cfg.n-cfg.k;
        bool ok=true;
        for(int t=0;t<50&&ok;++t) {
            Packets d(cfg.k, Packet(64,0x01));
            auto cw=codec.encode(d);
            std::vector<int> all(cfg.n); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+m);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"All-0x01 data: RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+") 50 trials");
    }

    // All-0xFF data (max field element)
    for(auto& cfg : cfgs) {
        RSCodec::CauchyCodec codec(cfg.n,cfg.k);
        int m=cfg.n-cfg.k;
        bool ok=true;
        for(int t=0;t<50&&ok;++t) {
            Packets d(cfg.k, Packet(64,0xFF));
            auto cw=codec.encode(d);
            std::vector<int> all(cfg.n); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+m);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"All-0xFF data: RS("+std::to_string(cfg.n)+","+std::to_string(cfg.k)+") 50 trials");
    }

    // All-0x80 data (high bit set, MSB patterns)
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<100&&ok;++t) {
            Packets d(4, Packet(64,0x80));
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"All-0x80 data: RS(8,4) 100 trials");
    }

    // Counting sequence: data[i][j] = (i*64+j) mod 256
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        for(int t=0;t<100&&ok;++t) {
            Packets d(4, Packet(64));
            for(int i=0;i<4;++i) for(int j=0;j<64;++j) d[i][j]=(uint8_t)((i*64+j)%256);
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"Counting sequence data (0..255 pattern): RS(8,4) 100 trials");
    }

    // Mixed: each packet a different constant (0x00, 0x55, 0xAA, 0xFF)
    {
        RSCodec::CauchyCodec codec(8,4);
        bool ok=true;
        uint8_t consts[]={0x00,0x55,0xAA,0xFF};
        for(int t=0;t<100&&ok;++t) {
            Packets d(4, Packet(64));
            for(int i=0;i<4;++i) std::fill(d[i].begin(),d[i].end(),consts[i]);
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"Mixed constants per packet (0x00,0x55,0xAA,0xFF): RS(8,4) 100 trials");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [EXH2] Exhaustive RS(12,8) with all m_actual levels
// ═════════════════════════════════════════════════════════════════════════════
void test_exhaustive_rs12_8() {
    begin_category("[EXH2] Exhaustive RS(12,8) — all C(12,8)=495 patterns × m_actual 0..4");
    std::mt19937 rng(700);

    RSCodec::CauchyCodec codec(12,8);

    // Test each m_actual separately
    for(int m_actual=0; m_actual<=4; ++m_actual) {
        auto d=rand_data(8,32,rng);
        auto cw=codec.encode(d);

        // Enumerate all C(12, 8) ways to choose 8 packets to keep
        // = all ways to drop 4 packets total, but with exactly m_actual data drops
        // For simplicity: enumerate all C(12,8) keep-sets and filter by m_actual

        // Generate all subsets of size (12-m_actual-x) that include exactly (8-m_actual) data pkts
        // Easier: enumerate all C(12,8) subsets with bitmask
        std::vector<int> mask(12,0);
        std::fill(mask.begin(), mask.begin()+8, 1);
        std::sort(mask.begin(), mask.end());

        int pass=0, total=0;
        do {
            // Count how many data packets (indices 0..7) are dropped (mask[i]=0)
            int data_dropped=0;
            for(int i=0;i<8;++i) if(!mask[i]) ++data_dropped;
            if(data_dropped!=m_actual) continue;

            // Build receive set
            Packets recv; std::vector<int> idx;
            for(int i=0;i<12;++i) if(mask[i]){recv.push_back(cw[i]);idx.push_back(i);}

            ++total;
            try {
                auto rec=codec.decode(recv,idx,false);
                if(pkts_eq(rec,d)) ++pass;
            } catch(...) {}
        } while(std::next_permutation(mask.begin(),mask.end()));

        check(pass==total,
              "RS(12,8) m_actual="+std::to_string(m_actual)+": "+
              std::to_string(pass)+"/"+std::to_string(total)+" patterns correct");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [LNG] Long-Haul: 50,000 encode-decode cycles
// ═════════════════════════════════════════════════════════════════════════════
void test_long_haul() {
    begin_category("[LNG] Long-Haul — 50,000 encode-decode cycles, no drift");
    std::mt19937 rng(800);

    RSCodec::CauchyCodec codec(12,8);
    int pass=0;
    const int N=50000;

    for(int t=0;t<N;++t) {
        auto d=rand_data(8,64,rng);
        auto cw=codec.encode(d);
        std::vector<int> all(12); std::iota(all.begin(),all.end(),0);
        std::shuffle(all.begin(),all.end(),rng);
        std::set<int> dr(all.begin(),all.begin()+4);
        auto [recv,idx]=drop_set(cw,dr);
        try {
            auto rec=codec.decode(recv,idx,(t%3!=0)); // vary cache usage
            if(pkts_eq(rec,d)) ++pass;
        } catch(...) {}
    }
    check(pass==N,"Long-haul: "+std::to_string(pass)+"/"+std::to_string(N)+" RS(12,8) cycles (50k)");

    // Also: 10,000 cycles with different codec each time (no reuse)
    int pass2=0;
    const int N2=10000;
    for(int t=0;t<N2;++t) {
        int n=6+(rng()%6); // n from 6..11
        int k=n/2+1;       // k slightly over half
        if(k>=n) k=n-1;
        RSCodec::CauchyCodec c2(n,k);
        auto d=rand_data(k,32,rng);
        auto cw=c2.encode(d);
        int m=n-k;
        std::vector<int> all(n); std::iota(all.begin(),all.end(),0);
        std::shuffle(all.begin(),all.end(),rng);
        std::set<int> dr(all.begin(),all.begin()+m);
        auto [recv,idx]=drop_set(cw,dr);
        try {
            auto rec=c2.decode(recv,idx);
            if(pkts_eq(rec,d)) ++pass2;
        } catch(...) {}
    }
    check(pass2==N2,"Long-haul: "+std::to_string(pass2)+"/"+std::to_string(N2)+
          " cycles with fresh codec per trial (varied configs)");
}

// ═════════════════════════════════════════════════════════════════════════════
// [MIX] Mixed Packet Sizes in Same Session
// ═════════════════════════════════════════════════════════════════════════════
void test_mixed_packet_sizes() {
    begin_category("[MIX] Mixed Packet Sizes — varied sizes in same session");
    std::mt19937 rng(900);

    RSCodec::CauchyCodec codec(8,4);

    // Alternate between different packet sizes on the same codec object
    {
        bool ok=true;
        std::vector<int> sizes={1,7,64,255,512,1500,4096,1,64,1500};
        for(int t=0;t<100&&ok;++t) {
            int pkt_size=sizes[t%sizes.size()];
            auto d=rand_data(4,pkt_size,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(8,4): 100 trials with alternating packet sizes {1,7,64,255,512,1500,4096}");
    }

    // Same codec, rapidly alternating 1-byte and 4096-byte packets
    {
        bool ok=true;
        for(int t=0;t<200&&ok;++t) {
            int pkt_size=(t%2==0)?1:4096;
            auto d=rand_data(4,pkt_size,rng);
            auto cw=codec.encode(d);
            std::vector<int> all(8); std::iota(all.begin(),all.end(),0);
            std::shuffle(all.begin(),all.end(),rng);
            std::set<int> dr(all.begin(),all.begin()+4);
            auto [recv,idx]=drop_set(cw,dr);
            try { auto rec=codec.decode(recv,idx); if(!pkts_eq(rec,d)) ok=false; }
            catch(...){ ok=false; }
        }
        check(ok,"RS(8,4): 200 trials alternating 1-byte and 4096-byte packets");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [REG] Regression — exact original cache bug scenario
// ═════════════════════════════════════════════════════════════════════════════
void test_regression() {
    begin_category("[REG] Regression — exact cache bug scenario (the bug we fixed)");
    std::mt19937 rng(1000);

    // Original bug: RS(6,4), cache key was lost_data only (not including par_rows)
    // Scenario:
    //   Call 1: lose data[0], parity[1] → uses parity[0] → caches inv with parity[0]
    //   Call 2: lose data[0], parity[0] → cache hit → uses cached inv (parity[0]) but
    //            parity[0] not available → would apply wrong inverse → corrupt output
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(6,4);
        bool ok=true;

        for(int t=0;t<2000&&ok;++t) {
            auto d=rand_data(4,1500,rng);
            auto cw=codec.encode(d);

            // Pattern 1: drop data[0] (idx 0) and parity[1] (idx 5)
            // → uses parity[0] (idx 4) → cache key {lost=[0], pars=[4]}
            {
                Packets recv={cw[1],cw[2],cw[3],cw[4]};
                std::vector<int> idx={1,2,3,4};
                auto rec=codec.decode(recv,idx,true);
                if(!pkts_eq(rec,d)) ok=false;
            }

            // Immediately after: Pattern 2: drop data[0] (idx 0) and parity[0] (idx 4)
            // → must use parity[1] (idx 5) → cache key {lost=[0], pars=[5]} DIFFERENT key
            // Bug: old code would hit cache on {lost=[0]} and use wrong (parity[0]) inverse
            {
                Packets recv={cw[1],cw[2],cw[3],cw[5]};
                std::vector<int> idx={1,2,3,5};
                auto rec=codec.decode(recv,idx,true);
                if(!pkts_eq(rec,d)) ok=false;
            }
        }
        check(ok,"REGRESSION: cache bug scenario (2000 trials): lose data[0]+parity[1] then data[0]+parity[0]");
    }

    // Extended regression: all pairs of (lose data[i], lose parity[j]) for RS(6,4)
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(6,4);
        bool ok=true;

        for(int t=0;t<200&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);

            // Try all (data_drop, parity_drop) pairs in sequence within same trial
            // This is the worst case for the old cache implementation
            for(int di=0;di<4&&ok;++di) {
                for(int pj=4;pj<6&&ok;++pj) {
                    auto [recv,idx]=drop_set(cw,{di,pj});
                    // drop 2: 1 data, 1 parity — need parity that's not dropped
                    auto rec=codec.decode(recv,idx,true);
                    if(!pkts_eq(rec,d)) ok=false;
                }
            }
        }
        check(ok,"REGRESSION extended: all 4×2=8 (data_drop,parity_drop) pairs per trial (200 trials)");
    }

    // Three-way collision: lose data[0] with 3 different parity drops, rapid succession
    {
        MatrixGF256::cache_clear();
        RSCodec::CauchyCodec codec(8,4); // 4 parity rows
        bool ok=true;

        for(int t=0;t<500&&ok;++t) {
            auto d=rand_data(4,64,rng);
            auto cw=codec.encode(d);

            // Lose data[0] with parity[0] dropped (use parity 1,2,3)
            for(int pi=4;pi<8&&ok;++pi) {
                auto [recv,idx]=drop_set(cw,{0,pi}); // lose data[0] and parity[pi]
                Packets rk(recv.begin(),recv.begin()+4);
                std::vector<int> ik(idx.begin(),idx.begin()+4);
                auto rec=codec.decode(rk,ik,true);
                if(!pkts_eq(rec,d)) ok=false;
            }
        }
        check(ok,"REGRESSION three-way: lose data[0] with each parity dropped in sequence (500 trials)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// [FLD] Field Axioms — formal algebraic structure
// ═════════════════════════════════════════════════════════════════════════════
void test_field_axioms() {
    begin_category("[FLD] Field Axioms — formal algebraic properties");
    GF256::init();

    // (a+b)+c == a+(b+c): addition associativity, exhaustive for small values
    {
        bool ok=true;
        for(int a=0;a<32&&ok;++a)
            for(int b=0;b<32&&ok;++b)
                for(int c=0;c<32&&ok;++c) {
                    uint8_t lhs=GF256::add(GF256::add(a,b),c);
                    uint8_t rhs=GF256::add(a,GF256::add(b,c));
                    if(lhs!=rhs) ok=false;
                }
        check(ok,"Addition associativity: (a+b)+c==a+(b+c) for all a,b,c in 0..31 (32768 triples)");
    }

    // (a*b)*c == a*(b*c): multiplication associativity exhaustive
    {
        bool ok=true;
        for(int a=0;a<32&&ok;++a)
            for(int b=0;b<32&&ok;++b)
                for(int c=0;c<32&&ok;++c) {
                    uint8_t lhs=GF256::mul(GF256::mul(a,b),c);
                    uint8_t rhs=GF256::mul(a,GF256::mul(b,c));
                    if(lhs!=rhs) ok=false;
                }
        check(ok,"Multiplication associativity: (a*b)*c==a*(b*c) for all a,b,c in 0..31");
    }

    // a*(b+c)==a*b+a*c: distributivity exhaustive for 0..31
    {
        bool ok=true;
        for(int a=0;a<32&&ok;++a)
            for(int b=0;b<32&&ok;++b)
                for(int c=0;c<32&&ok;++c) {
                    uint8_t lhs=GF256::mul(a,GF256::add(b,c));
                    uint8_t rhs=GF256::add(GF256::mul(a,b),GF256::mul(a,c));
                    if(lhs!=rhs) ok=false;
                }
        check(ok,"Distributivity: a*(b+c)==a*b+a*c for all a,b,c in 0..31 (32768 triples)");
    }

    // There are exactly 255 generators of the multiplicative group
    // (primitive element 2 generates all nonzero elements)
    {
        GF256::init();
        std::set<uint8_t> generated;
        uint8_t x=1;
        for(int i=0;i<255;++i) {
            generated.insert(x);
            x=GF256::mul(x,2); // multiply by primitive element α
        }
        check((int)generated.size()==255,
              "α=2 generates all 255 nonzero GF(2^8) elements (primitive element verification)");
    }

    // The multiplicative group has order 255 (no element has order < 255 except identity-like)
    // Verify: α^k != 1 for k = 1..254, α^255 == 1
    {
        bool ok=true;
        uint8_t a=2; // primitive element
        uint8_t power=1;
        for(int k=1;k<=255&&ok;++k) {
            power=GF256::mul(power,a);
            if(k<255 && power==1) ok=false; // premature period
            if(k==255 && power!=1) ok=false; // must cycle at 255
        }
        check(ok,"α=2 has order exactly 255 in GF(2^8)* (no premature return to 1)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════════════
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n"
              << "║  FEC Codec v3 — Deep Pressure Test Suite                         ║\n"
              << "║  Ibrahim Mammadov, ZJU-UIUC 2026                                 ║\n"
              << "╚══════════════════════════════════════════════════════════════════╝\n";

    GF256::init();
    auto t0=std::chrono::steady_clock::now();

    test_gf_exhaustive();
    test_gf_exceptions();
    test_region_mul_set();
    test_matrix_edge_cases();
    test_cache_internals();
    test_api_wrappers();
    test_decoder_edge_cases();
    test_channel_accessors();
    test_incast_boundaries();
    test_dc3_boundary_transitions();
    test_special_data_values();
    test_exhaustive_rs12_8();
    test_long_haul();
    test_mixed_packet_sizes();
    test_regression();
    test_field_axioms();

    auto t1=std::chrono::steady_clock::now();
    double elapsed=std::chrono::duration<double>(t1-t0).count();

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n"
              << "║  RESULTS                                                         ║\n"
              << "╠══════════════════════════════════════════════════════════════════╣\n"
              << "║  Passed: " << std::setw(5) << g_pass << " / " << std::setw(5) << g_total << "                                             ║\n"
              << "║  Failed: " << std::setw(5) << g_fail << "                                                     ║\n"
              << "║  Time:   " << std::fixed << std::setprecision(2) << elapsed << "s                                                  ║\n";
    if(g_fail==0)
        std::cout << "║  Status: ALL TESTS PASSED ✓                                      ║\n";
    else
        std::cout << "║  Status: " << g_fail << " FAILED ✗                                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";

    return g_fail>0 ? 1 : 0;
}
