// test_simd.cpp — correctness checks for the SIMD kernels, the fused m×m decoder,
// and agreement with ISA-L. Every benchmark run depends on these passing.
//
// Build: make test   (links -lisal)

#include "rs_fast.hpp"
#include "isal_ref.hpp"
#include "../FEC_Codec_v3/rs_codec.hpp"
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
#include <functional>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using GFSIMD::Isa;
static std::mt19937_64 rng(12345);

static std::vector<Isa> isas() {
    std::vector<Isa> v{Isa::Scalar};
    if (GFSIMD::isa_supported(Isa::AVX2))   v.push_back(Isa::AVX2);
    if (GFSIMD::isa_supported(Isa::AVX512)) v.push_back(Isa::AVX512);
    return v;
}
static std::vector<IsalRef::IsalKernel> isal_kernels() {
    std::vector<IsalRef::IsalKernel> v{IsalRef::IsalKernel::Default};
    if (GFSIMD::isa_supported(Isa::AVX2))   v.push_back(IsalRef::IsalKernel::AVX2);
    if (GFSIMD::isa_supported(Isa::AVX512)) v.push_back(IsalRef::IsalKernel::AVX512);
    return v;
}

static void test_tables_match_isal() {
    for (int c = 0; c < 256; ++c) {
        uint8_t a[32], b[32];
        GFSIMD::make_tbl(static_cast<uint8_t>(c), a);
        gf_vect_mul_init(static_cast<unsigned char>(c), b);
        CHECK(std::memcmp(a, b, 32) == 0, "table mismatch c=%d", c);
        for (int x = 0; x < 256; ++x)
            if (GF256::mul(c, x) != gf_mul(c, x)) { CHECK(false, "gf_mul mismatch %d*%d", c, x); return; }
    }
}

static void test_dot_prod_vs_scalar() {
    const int lens[] = {1, 15, 16, 31, 32, 33, 63, 64, 65, 100, 127, 128, 129, 1024, 1500, 4096, 4097, 9000, 9216};
    for (int len : lens)
        for (int nsrc : {1, 2, 3, 4, 7, 8, 16, 17, 32})
            for (int nout : {1, 2, 3, 4, 5, 7, 8, 9, 16}) {
                std::vector<std::vector<uint8_t>> src(nsrc, std::vector<uint8_t>(len));
                for (auto& s : src) for (auto& x : s) x = rng();
                std::vector<uint8_t> coef(nout * nsrc);
                for (auto& c : coef) c = rng();
                std::vector<uint8_t> tbls(nout * nsrc * 32);
                GFSIMD::make_tbls(nsrc, nout, coef.data(), tbls.data());
                std::vector<const uint8_t*> sp(nsrc);
                for (int j = 0; j < nsrc; ++j) sp[j] = src[j].data();

                // ground truth from plain GF256::mul
                std::vector<std::vector<uint8_t>> ref(nout, std::vector<uint8_t>(len, 0));
                for (int o = 0; o < nout; ++o)
                    for (int j = 0; j < nsrc; ++j)
                        for (int i = 0; i < len; ++i) ref[o][i] ^= GF256::mul(coef[o * nsrc + j], src[j][i]);

                for (Isa isa : isas()) {
                    // +1 guard byte to catch overruns
                    std::vector<std::vector<uint8_t>> out(nout, std::vector<uint8_t>(len + 1, 0xA5));
                    std::vector<uint8_t*> dp(nout);
                    for (int o = 0; o < nout; ++o) dp[o] = out[o].data();
                    GFSIMD::dot_prod(isa, len, nsrc, nout, tbls.data(), sp.data(), dp.data());
                    bool ok = true;
                    for (int o = 0; o < nout && ok; ++o)
                        ok = std::memcmp(out[o].data(), ref[o].data(), len) == 0 && out[o][len] == 0xA5;
                    CHECK(ok, "dot_prod %s len=%d nsrc=%d nout=%d", GFSIMD::isa_name(isa), len, nsrc, nout);
                }
            }
}

static void test_invert_small_vs_v3() {
    for (int trial = 0; trial < 2000; ++trial) {
        int n = 4 + rng() % 60, k = 1 + rng() % (n - 1);
        int m = n - k, ml = 1 + rng() % std::min(m, k);
        auto G = MatrixGF256::build_cauchy_generator(n, k);
        std::vector<int> rows(m), cols(k);
        std::iota(rows.begin(), rows.end(), k); std::iota(cols.begin(), cols.end(), 0);
        std::shuffle(rows.begin(), rows.end(), rng); std::shuffle(cols.begin(), cols.end(), rng);
        MatrixGF256::ByteMatrix A = MatrixGF256::make_zero(ml, ml);
        uint8_t flat[RSFast::MAX_M * RSFast::MAX_M], inv[RSFast::MAX_M * RSFast::MAX_M];
        for (int r = 0; r < ml; ++r) for (int c = 0; c < ml; ++c) flat[r * ml + c] = A[r][c] = G[rows[r]][cols[c]];
        auto ref = MatrixGF256::gf256_invert(A);
        bool ok = ref.has_value() && RSFast::invert_small(flat, inv, ml);
        for (int r = 0; ok && r < ml; ++r) for (int c = 0; c < ml; ++c) ok = inv[r * ml + c] == (*ref)[r][c];
        CHECK(ok, "invert_small mismatch n=%d k=%d ml=%d", n, k, ml);
    }
}

// Encode with v3, fast (each ISA) and ISA-L (each kernel), require identical parity.
static void test_encoders_agree() {
    const int cfgs[][2] = {{5, 4}, {8, 4}, {11, 8}, {16, 8}, {21, 16}, {32, 16}, {48, 32}};
    for (auto& cf : cfgs) for (int len : {64, 1000, 1500, 4096, 9216}) {
        int n = cf[0], k = cf[1], m = n - k;
        RSCodec::Packets data(k, RSCodec::Packet(len));
        for (auto& p : data) for (auto& x : p) x = rng();
        auto cw = RSCodec::CauchyCodec(n, k).encode(data);
        std::vector<const uint8_t*> dp(k);
        std::vector<unsigned char*> dpm(k);
        for (int j = 0; j < k; ++j) { dp[j] = data[j].data(); dpm[j] = data[j].data(); }

        for (Isa isa : isas()) {
            RSFast::FastCauchy fc(n, k, isa);
            std::vector<std::vector<uint8_t>> par(m, std::vector<uint8_t>(len));
            std::vector<uint8_t*> pp(m);
            for (int i = 0; i < m; ++i) pp[i] = par[i].data();
            fc.encode(dp.data(), pp.data(), len);
            bool ok = true;
            for (int i = 0; i < m && ok; ++i) ok = par[i] == cw[k + i];
            CHECK(ok, "fast encode %s != v3, RS(%d,%d) len=%d", GFSIMD::isa_name(isa), n, k, len);
        }
        RSFast::FastCauchy fc0(n, k, Isa::Scalar);
        for (auto kern : isal_kernels()) {
            IsalRef::Codec ic(fc0.G, n, k, kern);
            std::vector<std::vector<uint8_t>> par(m, std::vector<uint8_t>(len));
            std::vector<unsigned char*> pp(m);
            for (int i = 0; i < m; ++i) pp[i] = par[i].data();
            ic.encode(dpm.data(), pp.data(), len);
            bool ok = true;
            for (int i = 0; i < m && ok; ++i) ok = par[i] == cw[k + i];
            CHECK(ok, "ISA-L(%s) encode != v3, RS(%d,%d) len=%d", IsalRef::kernel_name(kern), n, k, len);
        }
    }
}

// Decode every k-subset (small codes) or random subsets (large codes) with all methods.
static void test_decoders(int n, int k, int len, int max_subsets) {
    int m = n - k;
    std::vector<std::vector<uint8_t>> cw(n, std::vector<uint8_t>(len));
    for (int j = 0; j < k; ++j) for (auto& x : cw[j]) x = rng();
    RSFast::FastCauchy enc(n, k, Isa::Scalar);
    {
        std::vector<const uint8_t*> d(k); std::vector<uint8_t*> p(m);
        for (int j = 0; j < k; ++j) d[j] = cw[j].data();
        for (int i = 0; i < m; ++i) p[i] = cw[k + i].data();
        enc.encode(d.data(), p.data(), len);
    }
    std::vector<std::vector<int>> subsets;
    std::vector<int> sel(n, 0);
    std::fill(sel.end() - k, sel.end(), 1);
    long total = 0;
    do { ++total; } while (total <= max_subsets && std::next_permutation(sel.begin(), sel.end()));
    std::fill(sel.begin(), sel.end(), 0); std::fill(sel.end() - k, sel.end(), 1);
    if (total <= max_subsets) {
        do { std::vector<int> s; for (int i = 0; i < n; ++i) if (sel[i]) s.push_back(i); subsets.push_back(s); }
        while (std::next_permutation(sel.begin(), sel.end()));
    } else {
        std::vector<int> all(n); std::iota(all.begin(), all.end(), 0);
        for (int t = 0; t < max_subsets; ++t) {
            std::shuffle(all.begin(), all.end(), rng);
            std::vector<int> s(all.begin(), all.begin() + k); std::sort(s.begin(), s.end());
            subsets.push_back(s);
        }
    }

    std::vector<RSFast::FastCauchy> fasts;
    for (Isa isa : isas()) fasts.emplace_back(n, k, isa);
    std::vector<IsalRef::Codec> isals;
    for (auto kern : isal_kernels()) isals.emplace_back(enc.G, n, k, kern);

    int bad = 0;
    RSFast::Plan fp; IsalRef::Plan ip;
    std::vector<std::vector<uint8_t>> out(m, std::vector<uint8_t>(len));
    std::vector<uint8_t*> op(m);
    for (int i = 0; i < m; ++i) op[i] = out[i].data();

    for (auto& s : subsets) {
        std::vector<unsigned char*> recv(k);
        for (int i = 0; i < k; ++i) recv[i] = cw[s[i]].data();
        auto check = [&](int nl, const int* lost) {
            for (int c = 0; c < nl; ++c) if (std::memcmp(out[c].data(), cw[lost[c]].data(), len)) return false;
            return true;
        };
        for (auto& fc : fasts) {
            if (!fc.make_plan(s.data(), k, fp)) { ++bad; continue; }
            fc.apply(fp, recv.data(), op.data(), len);
            if (!check(fp.nlost, fp.lost)) ++bad;
        }
        for (auto& ic : isals) {
            if (!ic.make_plan(s.data(), k, ip)) { ++bad; continue; }
            ic.apply(ip, recv.data(), op.data(), len);
            if (!check(ip.nlost, ip.lost.data())) ++bad;
        }
        // hybrid: m×m plan, ISA-L region kernel
        for (auto kern : isal_kernels()) {
            if (!fasts[0].make_plan(s.data(), k, fp, false)) { ++bad; continue; }
            if (fp.nlost == 0) continue;
            std::vector<unsigned char> t(fp.nlost * k * 32);
            IsalRef::init_tables(kern, k, fp.nlost, fp.D, t.data());
            unsigned char* srcs[256];
            for (int j = 0; j < k; ++j) srcs[j] = recv[fp.src_pos[j]];
            IsalRef::run(kern, len, k, fp.nlost, t.data(), srcs, op.data());
            if (!check(fp.nlost, fp.lost)) ++bad;
        }
    }
    CHECK(bad == 0, "decode RS(%d,%d) len=%d: %d failures over %zu subsets", n, k, len, bad, subsets.size());
}

int main() {
    GF256::init();
    std::printf("ISAs available:");
    for (Isa i : isas()) std::printf(" %s", GFSIMD::isa_name(i));
    std::printf("\n");
    test_tables_match_isal();
    test_dot_prod_vs_scalar();
    test_invert_small_vs_v3();
    test_encoders_agree();
    test_decoders(5, 4, 1500, 100000);
    test_decoders(8, 4, 4096, 100000);     // all 70 subsets
    test_decoders(12, 8, 1024, 100000);    // all 495
    test_decoders(16, 8, 4097, 100000);    // all 12870
    test_decoders(32, 16, 9216, 3000);     // random
    test_decoders(48, 32, 1000, 2000);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
