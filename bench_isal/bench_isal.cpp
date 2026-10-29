// bench_isal.cpp — v3 m×m decoder (+SIMD) vs Intel ISA-L, same machine, same code.
//
// Every method decodes the SAME codewords (same Cauchy generator, same erasure
// patterns, same buffers), and every decode is verified byte-for-byte against
// the original data outside the timed region.
//
// Methods
//   v3_scalar        thesis codec as submitted (FEC_Codec_v3, cache off)   total only
//   ours_<isa>       m×m inversion  + our split-nibble kernel   isa ∈ scalar|avx2|avx512
//   isal_<kern>      k×k inversion  + ISA-L kernel               kern ∈ avx2|avx512|default
//   hybrid_<kern>    m×m inversion  + ISA-L kernel  (isolates the inversion contribution)
//
// Phases (reported separately)
//   matrix  = find lost set, invert, build decode matrix, expand 32-byte tables
//   region  = the packet work (m'·k·L byte-multiplies)
// With a decode-matrix cache (either library), steady-state cost = region only.
//
// Modes
//   latency     single thread pinned to --cpu; per-decode timing; median/p99
//   throughput  T pinned threads, each with its own codewords; aggregate Gbps
//
// Examples
//   ./bench_isal latency --configs 5:4,8:4,16:8,32:16 --ml 1,2,4,max
//       --pkts 1024,4096,9216 --methods all --iters 5000 --cpu 2 --out results/latency.csv
//   ./bench_isal throughput --configs 32:16 --ml 2 --pkts 4096 --threads 1,2,4,8
//       --cpus 0,1,2,3,4,5,6,7 --methods ours_avx2,isal_avx2 --seconds 2 --out results/tput.csv
//   ("all" excludes ours_scalar, a reference kernel only; request it by name.)

#include "rs_fast.hpp"
#include "isal_ref.hpp"
#include "cm256_ref.hpp"
#include "../FEC_Codec_v3/rs_codec.hpp"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// ── argument parsing ─────────────────────────────────────────────────────────
struct Args {
    std::string mode = "latency";
    std::vector<std::pair<int, int>> configs = {{5, 4}, {8, 4}, {16, 8}, {32, 16}};
    std::string ml_spec = "1,2,4,max";
    std::vector<int> pkts = {1024, 4096, 9216};
    std::string methods = "all";
    int iters = 5000, warmup = 500, pool = 64;
    int cpu = -1;
    std::vector<int> threads = {1};
    std::vector<int> cpus;
    double seconds = 2.0;
    std::string out = "";
    std::string data = "warm";   // warm | cold : cache state of packets before each timed op
    uint64_t seed = 1;
};

// Put the packets an op will touch into a defined cache state before timing it.
//   warm: read every line (models a NIC that DMAs into LLC/L2 via DDIO just before decode)
//   cold: clflush every line (data in DRAM)
// Without this, a method with a slow matrix phase sees its data evicted between pool
// reuses and is charged extra region time that is not its kernel's fault.
static void set_cache_state(bool cold, unsigned char* const* p, int cnt, int L) {
    uint8_t acc = 0;
    for (int i = 0; i < cnt; ++i)
        for (int b = 0; b < L; b += 64) {
            if (cold) _mm_clflush(p[i] + b);
            else acc ^= p[i][b];
        }
    if (cold) _mm_mfence();
    asm volatile("" : : "r"(acc) : "memory");  // keep the warming reads
}

static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> v; std::stringstream ss(s); std::string x;
    while (std::getline(ss, x, d)) if (!x.empty()) v.push_back(x);
    return v;
}
static std::vector<int> ints(const std::string& s) {
    std::vector<int> v; for (auto& x : split(s, ',')) v.push_back(std::stoi(x)); return v;
}

static Args parse(int argc, char** argv) {
    Args a;
    if (argc > 1) a.mode = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--configs") {
            a.configs.clear();
            for (auto& c : split(v, ',')) { auto p = split(c, ':'); a.configs.push_back({std::stoi(p[0]), std::stoi(p[1])}); }
        } else if (k == "--ml") a.ml_spec = v;
        else if (k == "--pkts") a.pkts = ints(v);
        else if (k == "--methods") a.methods = v;
        else if (k == "--iters") a.iters = std::stoi(v);
        else if (k == "--warmup") a.warmup = std::stoi(v);
        else if (k == "--pool") a.pool = std::stoi(v);
        else if (k == "--cpu") a.cpu = std::stoi(v);
        else if (k == "--threads") a.threads = ints(v);
        else if (k == "--cpus") a.cpus = ints(v);
        else if (k == "--seconds") a.seconds = std::stod(v);
        else if (k == "--out") a.out = v;
        else if (k == "--seed") a.seed = std::stoull(v);
        else if (k == "--data") { a.data = v; if (v != "warm" && v != "cold") { std::fprintf(stderr, "--data warm|cold\n"); std::exit(2); } }
        else { std::fprintf(stderr, "unknown flag %s\n", k.c_str()); std::exit(2); }
    }
    return a;
}

static std::vector<int> ml_list(const std::string& spec, int m, int k) {
    std::vector<int> v;
    int cap = std::min(m, k);
    for (auto& x : split(spec, ',')) {
        int y = (x == "max") ? cap : std::stoi(x);
        if (y >= 1 && y <= cap && std::find(v.begin(), v.end(), y) == v.end()) v.push_back(y);
    }
    return v;
}

static bool pin_to(int cpu) {
    if (cpu < 0) return true;
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    return pthread_setaffinity_np(pthread_self(), sizeof(s), &s) == 0;
}

// ── method descriptors ───────────────────────────────────────────────────────
enum class Kind { V3, Ours, Isal, Hybrid, CM256 };
struct Method { std::string name; Kind kind; GFSIMD::Isa isa; IsalRef::IsalKernel kern; };

static std::vector<Method> methods_from(const std::string& spec) {
    using GFSIMD::Isa; using IsalRef::IsalKernel;
    const bool a2 = GFSIMD::isa_supported(Isa::AVX2), a5 = GFSIMD::isa_supported(Isa::AVX512);
    std::vector<Method> all = {{"v3_scalar", Kind::V3, Isa::Scalar, IsalKernel::Default},
                               {"ours_scalar", Kind::Ours, Isa::Scalar, IsalKernel::Default}};
    if (a2) all.push_back({"ours_avx2", Kind::Ours, Isa::AVX2, IsalKernel::AVX2});
    if (a5) all.push_back({"ours_avx512", Kind::Ours, Isa::AVX512, IsalKernel::AVX512});
    if (a2) all.push_back({"isal_avx2", Kind::Isal, Isa::AVX2, IsalKernel::AVX2});
    if (a5) all.push_back({"isal_avx512", Kind::Isal, Isa::AVX512, IsalKernel::AVX512});
    all.push_back({"isal_default", Kind::Isal, Isa::Scalar, IsalKernel::Default});
    if (a2) all.push_back({"hybrid_avx2", Kind::Hybrid, Isa::AVX2, IsalKernel::AVX2});
    if (a5) all.push_back({"hybrid_avx512", Kind::Hybrid, Isa::AVX512, IsalKernel::AVX512});
    all.push_back({"hybrid_default", Kind::Hybrid, Isa::Scalar, IsalKernel::Default});
    if (a2) all.push_back({"cm256", Kind::CM256, Isa::AVX2, IsalKernel::AVX2});   // built with -mavx2
    if (spec == "all") {
        all.erase(std::remove_if(all.begin(), all.end(), [](const Method& m) { return m.name == "ours_scalar"; }), all.end());
        return all;
    }
    std::vector<Method> out;
    for (auto& n : split(spec, ',')) {
        auto it = std::find_if(all.begin(), all.end(), [&](const Method& m) { return m.name == n; });
        if (it == all.end()) { std::fprintf(stderr, "method %s unknown or unsupported on this CPU\n", n.c_str()); std::exit(2); }
        out.push_back(*it);
    }
    return out;
}

// ── workload: a pool of encoded codewords + erasure patterns ────────────────
struct Workload {
    int n, k, m, ml, L, P;
    std::vector<std::unique_ptr<uint8_t[], void (*)(void*)>> buf; // one 64B-aligned block per codeword
    std::vector<std::vector<int>> patterns; // survivors (ascending), size n-ml
    std::vector<std::vector<int>> lost;     // lost data indices (ascending)
    std::vector<uint8_t> G;

    uint8_t* pkt(int cw, int i) { return buf[cw].get() + static_cast<size_t>(i) * L; }

    // cm256 uses its own generator, so it gets its own parity for the same data (lazy).
    std::vector<std::vector<uint8_t>> cmpar;               // per codeword: m*L contiguous
    uint8_t* cm(int cw, int r) { return cmpar[cw].data() + static_cast<size_t>(r) * L; }
    void ensure_cm256() {
        if (!cmpar.empty()) return;
        Cm256Ref::Codec cc(n, k, L);
        cmpar.assign(P, std::vector<uint8_t>(static_cast<size_t>(m) * L));
        std::vector<uint8_t*> d(k);
        for (int c = 0; c < P; ++c) {
            for (int j = 0; j < k; ++j) d[j] = pkt(c, j);
            cc.encode(d.data(), cmpar[c].data());
        }
    }

    Workload(int n_, int k_, int ml_, int L_, int P_, int npat, uint64_t seed)
        : n(n_), k(k_), m(n_ - k_), ml(ml_), L(L_), P(P_) {
        std::mt19937_64 rng(seed);
        RSFast::FastCauchy enc(n, k, GFSIMD::Isa::Scalar);
        G = enc.G;
        for (int c = 0; c < P; ++c) {
            void* p = nullptr;
            if (posix_memalign(&p, 64, static_cast<size_t>(n) * L)) throw std::bad_alloc();
            buf.emplace_back(static_cast<uint8_t*>(p), std::free);
            for (int j = 0; j < k; ++j) for (int b = 0; b < L; ++b) pkt(c, j)[b] = static_cast<uint8_t>(rng());
            std::vector<const uint8_t*> d(k); std::vector<uint8_t*> q(m);
            for (int j = 0; j < k; ++j) d[j] = pkt(c, j);
            for (int i = 0; i < m; ++i) q[i] = pkt(c, k + i);
            enc.encode(d.data(), q.data(), L);
        }
        std::vector<int> idx(k);
        std::iota(idx.begin(), idx.end(), 0);
        for (int t = 0; t < npat; ++t) {
            std::shuffle(idx.begin(), idx.end(), rng);
            std::vector<int> l(idx.begin(), idx.begin() + ml);
            std::sort(l.begin(), l.end());
            std::vector<int> s;
            for (int i = 0; i < n; ++i) if (!std::binary_search(l.begin(), l.end(), i)) s.push_back(i);
            lost.push_back(l); patterns.push_back(s);
        }
    }
};

// One decoder instance for a method; holds reusable plan/scratch (no per-call allocation
// except inside v3, which is measured as submitted).
struct Decoder {
    Method M; int n, k, L;
    std::unique_ptr<RSFast::FastCauchy> fc;
    std::unique_ptr<IsalRef::Codec> ic;
    std::unique_ptr<RSCodec::CauchyCodec> v3;
    RSFast::Plan fp; IsalRef::Plan ip;
    std::vector<unsigned char> htbls;
    std::unique_ptr<Cm256Ref::Codec> cm;
    std::vector<uint8_t> cm_out;                 // encode output (m*L contiguous)
    const std::vector<int>* cm_surv = nullptr;   // survivors of the pending decode (throughput)
    uint8_t* cm_ptr[256]; int cm_idx[256];

    Decoder(const Method& m, const std::vector<uint8_t>& G, int n_, int k_, int L_) : M(m), n(n_), k(k_), L(L_) {
        if (M.kind == Kind::V3) v3 = std::make_unique<RSCodec::CauchyCodec>(n, k);
        if (M.kind == Kind::Ours || M.kind == Kind::Hybrid) fc = std::make_unique<RSFast::FastCauchy>(n, k, M.isa);
        if (M.kind == Kind::Isal) ic = std::make_unique<IsalRef::Codec>(G, n, k, M.kern);
        if (M.kind == Kind::Hybrid) htbls.resize(static_cast<size_t>(RSFast::MAX_M) * k * 32);
        if (M.kind == Kind::CM256) { cm = std::make_unique<Cm256Ref::Codec>(n, k, L);
                                     cm_out.resize(static_cast<size_t>(n - k) * L); }
    }

    // phase 1
    int plan(const std::vector<int>& surv) {
        switch (M.kind) {
            case Kind::Ours:   fc->make_plan(surv.data(), (int)surv.size(), fp, true); return fp.nlost;
            case Kind::Hybrid: fc->make_plan(surv.data(), (int)surv.size(), fp, false);
                               IsalRef::init_tables(M.kern, k, fp.nlost, fp.D, htbls.data()); return fp.nlost;
            case Kind::Isal:   ic->make_plan(surv.data(), (int)surv.size(), ip); return ip.nlost;
            case Kind::CM256:  cm_surv = &surv; return 0;   // cm256 has no separate plan phase
            default: return 0;
        }
    }
    // phase 2; recv[i] = packet for surv[i]
    void region(unsigned char* const* recv, unsigned char** out) {
        switch (M.kind) {
            case Kind::Ours:   fc->apply(fp, recv, out, L); break;
            case Kind::Isal:   ic->apply(ip, recv, out, L); break;
            case Kind::Hybrid: {
                unsigned char* srcs[256];
                for (int j = 0; j < k; ++j) srcs[j] = recv[fp.src_pos[j]];
                IsalRef::run(M.kern, L, k, fp.nlost, htbls.data(), srcs, out);
                break;
            }
            case Kind::CM256:   // throughput mode only: decodes in place into recv[] (timing is data-independent)
                for (int i = 0; i < k; ++i) { cm_ptr[i] = recv[i]; cm_idx[i] = (*cm_surv)[i]; }
                cm->decode(cm_ptr, cm_idx, nullptr);
                break;
            default: break;
        }
    }
    const int* lost_order() const { return M.kind == Kind::Isal ? ip.lost.data() : fp.lost; }

    void encode(unsigned char** data, unsigned char** par) {
        if (M.kind == Kind::Isal) ic->encode(data, par, L);
        else if (M.kind == Kind::CM256) cm->encode(data, cm_out.data());
        else if (fc) fc->encode(const_cast<const uint8_t* const*>(data), par, L);
    }
};

static double pct(std::vector<uint64_t> v, double p) {
    if (v.empty()) return 0;
    size_t i = std::min(v.size() - 1, static_cast<size_t>(p * (v.size() - 1) + 0.5));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return static_cast<double>(v[i]);
}

static std::string cpu_model() {
    std::ifstream f("/proc/cpuinfo"); std::string line;
    while (std::getline(f, line)) if (line.rfind("model name", 0) == 0) return line.substr(line.find(':') + 2);
    return "unknown";
}

// ── latency mode ─────────────────────────────────────────────────────────────
static int run_latency(const Args& a, FILE* out) {
    if (!pin_to(a.cpu)) std::fprintf(stderr, "warning: could not pin to cpu %d\n", a.cpu);
    auto methods = methods_from(a.methods);
    std::fprintf(out, "op,method,n,k,m_lost,pkt,iters,matrix_med_ns,matrix_p99_ns,region_med_ns,region_p99_ns,"
                      "total_med_ns,total_p99_ns,total_mean_ns,gbps_total_med,gbps_region_med\n");
    long errors = 0;
    const bool cold = a.data == "cold";
    for (auto [n, k] : a.configs) for (int L : a.pkts) {
        const int m = n - k;
        // encode rows (once per config/pkt)
        {
            Workload W(n, k, 1, L, a.pool, 1, a.seed);
            for (auto& M : methods) {
                if (M.kind != Kind::Ours && M.kind != Kind::Isal && M.kind != Kind::CM256) continue;
                Decoder D(M, W.G, n, k, L);
                std::vector<std::vector<uint8_t>> par(m, std::vector<uint8_t>(L));
                std::vector<unsigned char*> pp(m); for (int i = 0; i < m; ++i) pp[i] = par[i].data();
                if (M.kind == Kind::CM256) { W.ensure_cm256(); for (int i = 0; i < m; ++i) pp[i] = D.cm_out.data() + static_cast<size_t>(i) * L; }
                std::vector<uint64_t> t; t.reserve(a.iters);
                for (int it = 0; it < a.warmup + a.iters; ++it) {
                    int c = it % W.P;
                    std::vector<unsigned char*> dp(k); for (int j = 0; j < k; ++j) dp[j] = W.pkt(c, j);
                    set_cache_state(cold, dp.data(), k, L);
                    set_cache_state(cold, pp.data(), m, L);
                    uint64_t t0 = now_ns(); D.encode(dp.data(), pp.data()); uint64_t t1 = now_ns();
                    if (it >= a.warmup) t.push_back(t1 - t0);
                    if (it == 0) for (int i = 0; i < m; ++i)
                        if (std::memcmp(pp[i], M.kind == Kind::CM256 ? W.cm(c, i) : W.pkt(c, k + i), L)) ++errors;
                }
                double med = pct(t, 0.5), p99 = pct(t, 0.99);
                double mean = std::accumulate(t.begin(), t.end(), 0.0) / t.size();
                double gb = static_cast<double>(k) * L * 8 / med;
                std::fprintf(out, "encode,%s,%d,%d,0,%d,%d,0,0,%.0f,%.0f,%.0f,%.0f,%.1f,%.3f,%.3f\n",
                             M.name.c_str(), n, k, L, a.iters, med, p99, med, p99, mean, gb, gb);
            }
        }
        for (int ml : ml_list(a.ml_spec, m, k)) {
            Workload W(n, k, ml, L, a.pool, a.warmup + a.iters, a.seed + ml * 7919 + L);
            std::vector<std::vector<uint8_t>> outb(ml, std::vector<uint8_t>(L));
            std::vector<unsigned char*> op(ml); for (int i = 0; i < ml; ++i) op[i] = outb[i].data();
            for (auto& M : methods) {
                Decoder D(M, W.G, n, k, L);
                std::vector<uint64_t> t1v, t2v, ttv;
                t1v.reserve(a.iters); t2v.reserve(a.iters); ttv.reserve(a.iters);
                std::vector<unsigned char*> recv(n);
                for (int it = 0; it < a.warmup + a.iters; ++it) {
                    const int c = it % W.P;
                    const auto& surv = W.patterns[it];
                    for (size_t i = 0; i < surv.size(); ++i) recv[i] = W.pkt(c, surv[i]);
                    bool ok = true;
                    set_cache_state(cold, recv.data(), static_cast<int>(surv.size()), L);
                    set_cache_state(cold, op.data(), ml, L);
                    if (M.kind == Kind::CM256) {
                        // cm256 decodes in place: hand it copies of its parity (untimed)
                        W.ensure_cm256();
                        uint8_t* ptr[256]; int idx[256], oidx[256], r = 0;
                        for (int i = 0; i < k; ++i) {
                            idx[i] = surv[i];
                            if (surv[i] < k) ptr[i] = W.pkt(c, surv[i]);
                            else { std::memcpy(op[r], W.cm(c, surv[i] - k), L); ptr[i] = op[r++]; }
                        }
                        set_cache_state(cold, ptr, k, L);
                        uint64_t t0 = now_ns();
                        int rc = D.cm->decode(ptr, idx, oidx);
                        uint64_t t1 = now_ns();
                        ok &= rc == 0 && r == ml;
                        for (int i = 0; i < k; ++i)
                            if (idx[i] >= k) ok &= oidx[i] < k && std::memcmp(ptr[i], W.pkt(c, oidx[i]), L) == 0;
                        if (it >= a.warmup) ttv.push_back(t1 - t0);
                    } else if (M.kind == Kind::V3) {
                        RSCodec::Packets rp(k);
                        std::vector<int> ri(surv.begin(), surv.begin() + k);
                        for (int i = 0; i < k; ++i) rp[i].assign(recv[i], recv[i] + L);
                        if (cold) { std::vector<unsigned char*> q(k); for (int i = 0; i < k; ++i) q[i] = rp[i].data();
                                    set_cache_state(true, q.data(), k, L); }
                        uint64_t t0 = now_ns();
                        auto rec = D.v3->decode(rp, ri, false);
                        uint64_t t1 = now_ns();
                        for (int e : W.lost[it]) ok &= std::memcmp(rec[e].data(), W.pkt(c, e), L) == 0;
                        if (it >= a.warmup) ttv.push_back(t1 - t0);
                    } else {
                        uint64_t t0 = now_ns();
                        int nl = D.plan(surv);
                        uint64_t t1 = now_ns();
                        // Re-establish the cache state after the matrix phase (untimed), so the
                        // region kernel of every method starts from identical data placement.
                        set_cache_state(cold, recv.data(), k, L);
                        set_cache_state(cold, op.data(), ml, L);
                        uint64_t t1b = now_ns();
                        D.region(recv.data(), op.data());
                        uint64_t t2 = now_ns();
                        t2 = t1 + (t2 - t1b);
                        const int* lo = D.lost_order();
                        for (int e = 0; e < nl; ++e) ok &= std::memcmp(op[e], W.pkt(c, lo[e]), L) == 0;
                        ok &= nl == ml;
                        if (it >= a.warmup) { t1v.push_back(t1 - t0); t2v.push_back(t2 - t1); ttv.push_back(t2 - t0); }
                    }
                    if (!ok) ++errors;
                }
                double tm = pct(ttv, 0.5);
                double mean = std::accumulate(ttv.begin(), ttv.end(), 0.0) / ttv.size();
                double rm = t2v.empty() ? 0 : pct(t2v, 0.5);
                std::fprintf(out, "decode,%s,%d,%d,%d,%d,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.1f,%.3f,%.3f\n",
                             M.name.c_str(), n, k, ml, L, a.iters,
                             t1v.empty() ? NAN : pct(t1v, 0.5), t1v.empty() ? NAN : pct(t1v, 0.99),
                             t2v.empty() ? NAN : rm, t2v.empty() ? NAN : pct(t2v, 0.99),
                             tm, pct(ttv, 0.99), mean,
                             static_cast<double>(k) * L * 8 / tm,
                             t2v.empty() ? NAN : static_cast<double>(k) * L * 8 / rm);
                std::fflush(out);
                std::fprintf(stderr, "  RS(%d,%d) m'=%d L=%d %-15s total_med=%8.0f ns\n", n, k, ml, L, M.name.c_str(), tm);
            }
        }
    }
    if (errors) { std::fprintf(stderr, "ERROR: %ld decode/encode mismatches\n", errors); return 1; }
    return 0;
}

// ── throughput mode ──────────────────────────────────────────────────────────
static int run_throughput(const Args& a, FILE* out) {
    auto methods = methods_from(a.methods);
    std::vector<int> cpus = a.cpus;
    if (cpus.empty()) { int nc = std::thread::hardware_concurrency(); for (int i = 0; i < nc; ++i) cpus.push_back(i); }
    std::fprintf(out, "op,method,n,k,m_lost,pkt,threads,seconds,codewords,gbps,gbps_per_thread\n");
    for (auto [n, k] : a.configs) for (int L : a.pkts) for (int ml : ml_list(a.ml_spec, n - k, k))
    for (auto& M : methods) for (const char* op : {"encode", "decode"}) {
        if (M.kind == Kind::V3) continue;
        if (M.kind == Kind::Hybrid && std::string(op) == "encode") continue;
        if (std::string(op) == "encode" && ml != ml_list(a.ml_spec, n - k, k).front()) continue; // encode ignores m
        for (int T : a.threads) {
            if (T > (int)cpus.size()) continue;
            std::atomic<int> ready{0}; std::atomic<bool> go{false}, stop{false};
            std::vector<long> counts(T, 0);
            std::vector<std::thread> th;
            for (int t = 0; t < T; ++t) th.emplace_back([&, t] {
                pin_to(cpus[t]);
                Workload W(n, k, ml, L, a.pool, 4096, a.seed + 1000 * t + ml);
                Decoder D(M, W.G, n, k, L);
                if (M.kind == Kind::CM256) {   // put cm256's own parity in the parity slots
                    W.ensure_cm256();
                    for (int c = 0; c < W.P; ++c) std::memcpy(W.pkt(c, k), W.cm(c, 0), static_cast<size_t>(n - k) * L);
                }
                const int m = n - k;
                std::vector<std::vector<uint8_t>> ob(std::max(m, ml), std::vector<uint8_t>(L));
                std::vector<unsigned char*> op_(ob.size()); for (size_t i = 0; i < ob.size(); ++i) op_[i] = ob[i].data();
                std::vector<unsigned char*> recv(n), dp(k);
                const bool enc = std::string(op) == "encode";
                ready++;
                while (!go.load(std::memory_order_acquire)) {}
                long cnt = 0; int it = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    const int c = it % W.P;
                    if (enc) {
                        for (int j = 0; j < k; ++j) dp[j] = W.pkt(c, j);
                        D.encode(dp.data(), op_.data());
                    } else {
                        const auto& surv = W.patterns[it % W.patterns.size()];
                        for (size_t i = 0; i < surv.size(); ++i) recv[i] = W.pkt(c, surv[i]);
                        D.plan(surv);
                        D.region(recv.data(), op_.data());
                    }
                    ++cnt; ++it;
                }
                counts[t] = cnt;
            });
            while (ready.load() < T) std::this_thread::yield();
            uint64_t t0 = now_ns();
            go.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::duration<double>(a.seconds));
            stop.store(true);
            uint64_t t1 = now_ns();
            for (auto& x : th) x.join();
            long tot = std::accumulate(counts.begin(), counts.end(), 0L);
            double secs = (t1 - t0) / 1e9;
            double gbps = static_cast<double>(tot) * k * L * 8 / secs / 1e9;
            std::fprintf(out, "%s,%s,%d,%d,%d,%d,%d,%.3f,%ld,%.3f,%.3f\n", op, M.name.c_str(), n, k,
                         std::string(op) == "encode" ? 0 : ml, L, T, secs, tot, gbps, gbps / T);
            std::fflush(out);
            std::fprintf(stderr, "  %s RS(%d,%d) m'=%d L=%d %-14s T=%d  %.2f Gbps\n", op, n, k, ml, L, M.name.c_str(), T, gbps);
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    Args a = parse(argc, argv);
    GF256::init();
    FILE* out = a.out.empty() ? stdout : std::fopen(a.out.c_str(), "w");
    if (!out) { std::perror("open out"); return 2; }
    std::fprintf(stderr, "CPU: %s | best ISA: %s | data: %s\n", cpu_model().c_str(),
                 GFSIMD::isa_name(GFSIMD::best_isa()), a.data.c_str());
    int rc;
    if (a.mode == "latency") rc = run_latency(a, out);
    else if (a.mode == "throughput") rc = run_throughput(a, out);
    else { std::fprintf(stderr, "mode must be latency|throughput\n"); rc = 2; }
    if (out != stdout) std::fclose(out);
    return rc;
}
