// fecsim.cpp — When does erasure coding beat retransmission for RDMA?
//
// Discrete-event simulation of one RDMA message (or the slowest of N parallel flows,
// as in one step of a collective) over a lossy link. Compares:
//
//   gbn   Go-back-N, as in commodity RoCE NICs: receiver drops out-of-order packets and
//         sends one NAK per sequence gap; sender rewinds to the NAKed PSN. Tail losses
//         (nothing arrives after the loss) are recovered only by the retransmission timeout.
//   sr    Selective repeat, IRN-style: receiver keeps out-of-order packets and NAKs each
//         newly detected hole once; sender retransmits only lost packets, ahead of new
//         data. A retransmission lost again, or a tail loss, waits for the timeout.
//   srtlp Selective repeat + one tail-loss probe (as in TCP TLP): PTO after the last new
//         packet is sent (default 2×RTT), the sender retransmits the highest unacked packet,
//         so a lost final packet is recovered in ~PTO instead of a full RTO.
//   fec   Systematic RS(n,k): each group of k data packets is followed by n-k parity
//         packets. A codeword is complete when all k data packets arrive, or when any k of
//         its n packets arrive and the decoder finishes. Decode time is the MEASURED median
//         from bench_isal (method selectable: isal_default, cm256, hybrid_default, ...),
//         queued on a fixed number of decoder cores. Codewords that lose more than n-k
//         packets fall back to selective repeat of their missing data packets.
//
// Timing: link rate B, MTU L, one-way delay d = RTT/2; serialization L*8/B per packet;
// sender transmits back-to-back at line rate (no window limit); feedback packets
// (ACK/NAK) are never lost and take d. Losses are drawn per transmitted packet from a
// channel model stepped once per transmission (bernoulli, gilbert-elliott, dc3).
//
// Output: one CSV row per (protocol, configuration) with completion-time percentiles
// in microseconds, measured at the receiver.
//
// Build:  g++ -std=c++17 -O3 -o fecsim fecsim.cpp
// Usage:  ./fecsim --help

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// ── channel models (same parameterisation as FEC_Codec_v3/channel.hpp) ──────
struct Channel {
    std::string kind = "bernoulli";
    double p = 1e-4;                                   // bernoulli
    double ge_gb = 0.05, ge_bg = 0.3, ge_pg = 0.001, ge_pb = 0.4;
    double nd = 0.01, di = 0.10, dn = 0.20, id = 0.30; // dc3 transitions
    double pn = 0.001, pd = 0.03, pi = 0.35;           // dc3 loss per state
    int state = 0;
    std::vector<long> forced; long txi = 0;            // kind "forced": lose exactly these transmission indices
    std::mt19937_64 rng;
    std::uniform_real_distribution<double> U{0.0, 1.0};

    void reset(uint64_t seed) { rng.seed(seed); state = 0; txi = 0; }

    bool lose() {
        if (kind == "bernoulli") return U(rng) < p;
        if (kind == "forced") return std::find(forced.begin(), forced.end(), txi++) != forced.end();
        if (kind == "ge") {
            bool l = U(rng) < (state == 0 ? ge_pg : ge_pb);
            double u = U(rng);
            if (state == 0 && u < ge_gb) state = 1;
            else if (state == 1 && u < ge_bg) state = 0;
            return l;
        }
        // dc3: NORMAL(0) / DEGRADED(1) / INCAST(2)
        double lp = state == 0 ? pn : state == 1 ? pd : pi;
        bool l = U(rng) < lp;
        double u = U(rng);
        if (state == 0) { if (u < nd) state = 1; }
        else if (state == 1) { if (u < dn) state = 0; else if (u < dn + di) state = 2; }
        else { if (u < id) state = 1; }
        return l;
    }

    double mean_loss() const {
        if (kind == "bernoulli") return p;
        if (kind == "forced") return 0;
        if (kind == "ge") { double pb = ge_gb / (ge_gb + ge_bg); return (1 - pb) * ge_pg + pb * ge_pb; }
        double r = nd / dn, s = nd * di / (dn * id), sum = 1 + r + s;
        return (pn + r * pd + s * pi) / sum;
    }
};

// ── decode-cost table from bench_isal latency.csv ───────────────────────────
struct DecodeCost {
    std::string method = "hybrid_default";
    std::map<std::tuple<int, int, int>, std::vector<std::pair<int, double>>> t; // (n,k,L) -> [(m', µs)]

    void load(const std::string& path) {
        if (method == "zero") return;
        std::ifstream f(path);
        if (!f) { std::fprintf(stderr, "cannot open decode table %s\n", path.c_str()); std::exit(2); }
        std::string line; std::getline(f, line);
        std::vector<std::string> hdr; { std::stringstream ss(line); std::string x; while (std::getline(ss, x, ',')) hdr.push_back(x); }
        auto col = [&](const char* name) { for (size_t i = 0; i < hdr.size(); ++i) if (hdr[i] == name) return (int)i; std::fprintf(stderr, "no column %s\n", name); std::exit(2); };
        int cop = col("op"), cme = col("method"), cn = col("n"), ck = col("k"), cm = col("m_lost"), cp = col("pkt"), ct = col("total_med_ns");
        while (std::getline(f, line)) {
            std::vector<std::string> v; std::stringstream ss(line); std::string x;
            while (std::getline(ss, x, ',')) v.push_back(x);
            if ((int)v.size() < (int)hdr.size() || v[cop] != "decode" || v[cme] != method) continue;
            t[{std::stoi(v[cn]), std::stoi(v[ck]), std::stoi(v[cp])}].push_back({std::stoi(v[cm]), std::stod(v[ct]) / 1000.0});
        }
        for (auto& [key, vec] : t) std::sort(vec.begin(), vec.end());
        if (t.empty()) { std::fprintf(stderr, "decode table has no rows for method %s\n", method.c_str()); std::exit(2); }
    }

    // Linear interpolation in m' between measured points (clamped extrapolation by slope).
    double us(int n, int k, int L, int ml) const {
        if (method == "zero" || ml <= 0) return 0.0;
        auto it = t.find({n, k, L});
        if (it == t.end()) { std::fprintf(stderr, "no decode cost for RS(%d,%d) L=%d method %s\n", n, k, L, method.c_str()); std::exit(2); }
        const auto& v = it->second;
        if (v.size() == 1) return v[0].second * ml / v[0].first;
        for (size_t i = 0; i + 1 < v.size(); ++i)
            if (ml <= v[i + 1].first || i + 2 == v.size()) {
                double x0 = v[i].first, y0 = v[i].second, x1 = v[i + 1].first, y1 = v[i + 1].second;
                return std::max(0.0, y0 + (y1 - y0) * (ml - x0) / (x1 - x0));
            }
        return v.back().second;
    }
};

// ── simulation parameters ───────────────────────────────────────────────────
struct Params {
    std::string proto = "sr";
    int M = 256;            // data packets per message
    int L = 4096;           // MTU bytes
    double gbps = 100;      // link rate
    double rtt = 5;         // µs
    double rto = 100;       // µs (same for every protocol)
    double pto = -1;        // tail-loss probe timeout (µs); <0 → 2×RTT
    int n = 18, k = 16;     // FEC code
    int cores = 1;          // decoder cores
    double t_pkt() const { return L * 8.0 / (gbps * 1000.0); } // µs
    double d() const { return rtt / 2; }
};

// ── event engine ────────────────────────────────────────────────────────────
enum Ev { SENDER_FREE, ARRIVE, FB, RTO_FIRE, DECODE_DONE, TLP_FIRE };
struct Event {
    double t; uint64_t seq; Ev type; int a; int b; std::vector<int> list;
    bool operator>(const Event& o) const { return t != o.t ? t > o.t : seq > o.seq; }
};

struct Sim {
    Params P; Channel& ch; const DecodeCost& dc;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> q;
    uint64_t seq = 0;
    double now = 0;
    void push(double t, Ev ty, int a = 0, int b = 0, std::vector<int> l = {}) { q.push({t, seq++, ty, a, b, std::move(l)}); }

    Sim(const Params& p, Channel& c, const DecodeCost& d) : P(p), ch(c), dc(d) {}

    // ---- generic sender with priority retransmission queue ----
    std::deque<int> retx, fresh;   // unit ids
    bool sender_busy = false;
    std::vector<char> acked, queued_retx, sent_once;
    int nunits = 0;
    double last_progress = 0; uint64_t rto_gen = 0;
    long tx_count = 0;

    // FEC bookkeeping
    int G = 0;
    std::vector<int> cw_count, cw_data;     // received units / data units per codeword
    std::vector<char> cw_done, cw_decoding, cw_naked, have;
    std::vector<double> core_free;
    int done_codewords = 0, stall_scan = 0;

    // SR/GBN receiver
    std::vector<char> nak_sent;
    int exp_psn = 0; bool gbn_nak_pending = false; int una = 0;
    int received_data = 0;
    double completion = -1;

    int unit_cw(int u) const { return u / P.n; }
    int unit_pos(int u) const { return u % P.n; }
    bool is_data_unit(int u) const { return P.proto != "fec" || unit_pos(u) < P.k; }
    bool sr_like() const { return P.proto == "sr" || P.proto == "srtlp"; }
    bool tlp_armed = false;

    void kick() { if (!sender_busy) { sender_busy = true; push(now, SENDER_FREE); } }

    void arm_rto() { push(last_progress + P.rto, RTO_FIRE, (int)++rto_gen); }

    int next_unit() {
        while (!retx.empty()) { int u = retx.front(); retx.pop_front(); queued_retx[u] = 0; if (!acked[u] && !done_unit(u)) return u; }
        while (!fresh.empty()) {
            int u = fresh.front(); fresh.pop_front();
            if (P.proto == "gbn" && u < una) continue;
            if (!acked[u] && !done_unit(u)) return u;
        }
        return -1;
    }
    bool done_unit(int u) const { return P.proto == "fec" && cw_done[unit_cw(u)]; }

    void on_sender_free() {
        int u = next_unit();
        if (u < 0) { sender_busy = false; return; }
        ++tx_count; sent_once[u] = 1;
        if (P.proto == "srtlp" && !tlp_armed && fresh.empty() && retx.empty()) {
            tlp_armed = true;                       // last new packet: arm one probe
            push(now + P.t_pkt() + (P.pto >= 0 ? P.pto : 2 * P.rtt), TLP_FIRE);
        }
        bool lost = ch.lose();
        if (!lost) push(now + P.t_pkt() + P.d(), ARRIVE, u);
        push(now + P.t_pkt(), SENDER_FREE);
    }

    void send_fb(int kind, int a, std::vector<int> l = {}) { push(now + P.d(), FB, kind, a, std::move(l)); }
    enum { FB_ACK = 0, FB_NAK = 1, FB_CUM = 2, FB_DONE = 3 };

    // ---- receiver ----
    void on_arrive(int u) {
        if (P.proto == "gbn") {
            if (u == exp_psn) {
                ++exp_psn; gbn_nak_pending = false;
                send_fb(FB_CUM, exp_psn);
                if (exp_psn == P.M && completion < 0) completion = now;
            } else if (u > exp_psn && !gbn_nak_pending) {
                gbn_nak_pending = true;
                send_fb(FB_NAK, exp_psn);
            }
            return;
        }
        if (sr_like()) {
            if (have[u]) return;
            have[u] = 1; ++received_data;
            send_fb(FB_ACK, u);
            while (exp_psn < P.M && have[exp_psn]) ++exp_psn;
            if (u > exp_psn) {
                std::vector<int> holes;
                for (int x = exp_psn; x < u; ++x) if (!have[x] && !nak_sent[x]) { nak_sent[x] = 1; holes.push_back(x); }
                if (!holes.empty()) send_fb(FB_NAK, 0, holes);
            }
            if (received_data == P.M && completion < 0) completion = now;
            return;
        }
        // fec
        int g = unit_cw(u), pos = unit_pos(u);
        // In-order delivery: a packet from codeword g means the first transmission of every
        // earlier codeword is over (each packet arrived or was lost).
        for (; stall_scan < g; ++stall_scan) fec_check_stalled(stall_scan);
        if (cw_done[g] || have[u]) return;
        have[u] = 1; ++cw_count[g]; if (pos < P.k) ++cw_data[g];
        send_fb(FB_ACK, u);
        if (cw_data[g] == P.k) { fec_finish(g); return; }
        // The codeword's last packet arrived but it still has < k: its first transmission is
        // over, so request the missing data now instead of waiting for a later codeword.
        if (pos == P.n - 1 && cw_count[g] < P.k) fec_check_stalled(g);
        if (cw_count[g] >= P.k && !cw_decoding[g]) {
            cw_decoding[g] = 1;
            int ml = P.k - cw_data[g];
            double dur = dc.us(P.n, P.k, P.L, ml);
            auto it = std::min_element(core_free.begin(), core_free.end());
            double start = std::max(now, *it);
            *it = start + dur;
            push(start + dur, DECODE_DONE, g);
        }
    }

    // A codeword whose first transmission is complete but which has < k packets: NAK its missing data.
    void fec_check_stalled(int g) {
        if (cw_done[g] || cw_decoding[g] || cw_naked[g] || cw_count[g] >= P.k) return;
        cw_naked[g] = 1;
        std::vector<int> miss;
        for (int i = 0; i < P.k; ++i) { int u = g * P.n + i; if (!have[u]) miss.push_back(u); }
        send_fb(FB_NAK, 0, miss);
    }

    void fec_finish(int g) {
        if (cw_done[g]) return;
        cw_done[g] = 1; ++done_codewords;
        send_fb(FB_DONE, g);
        if (done_codewords == G && completion < 0) completion = now;
    }

    // ---- sender feedback ----
    void on_fb(int kind, int a, const std::vector<int>& l) {
        bool progress = false;
        if (P.proto == "gbn") {
            if (kind == FB_CUM && a > una) { una = a; progress = true; }
            if (kind == FB_NAK && a >= una) {
                una = a;
                fresh.clear(); for (int x = a; x < P.M; ++x) fresh.push_back(x);
                kick();
            }
        } else {
            if (kind == FB_ACK && !acked[a]) { acked[a] = 1; progress = true; }
            if (kind == FB_DONE) {
                progress = true;
                for (int i = 0; i < P.n; ++i) acked[a * P.n + i] = 1;   // stop sending this codeword
            }
            if (kind == FB_NAK) {
                for (int u : l) if (!acked[u] && !queued_retx[u]) { queued_retx[u] = 1; retx.push_back(u); }
                kick();
            }
        }
        if (progress) { last_progress = now; arm_rto(); }
    }

    void on_rto(int gen) {
        if ((uint64_t)gen != rto_gen || completion >= 0) return;
        // Retransmit everything sent but not acknowledged (GBN: rewind to una).
        if (P.proto == "gbn") {
            fresh.clear(); for (int x = una; x < P.M; ++x) fresh.push_back(x);
        } else if (sr_like()) {
            for (int u = 0; u < nunits; ++u) if (sent_once[u] && !acked[u] && !queued_retx[u]) { queued_retx[u] = 1; retx.push_back(u); }
        } else {
            // FEC: for codewords not done, resend their unacked data packets.
            for (int g = 0; g < G; ++g) {
                if (cw_done[g]) continue;
                bool any_sent = false;
                for (int i = 0; i < P.n; ++i) any_sent |= sent_once[g * P.n + i];
                if (!any_sent) continue;
                for (int i = 0; i < P.k; ++i) { int u = g * P.n + i; if (!acked[u] && !queued_retx[u]) { queued_retx[u] = 1; retx.push_back(u); } }
            }
        }
        last_progress = now; arm_rto(); kick();
    }

    void on_tlp() {
        if (completion >= 0) return;
        for (int u = nunits - 1; u >= 0; --u)
            if (sent_once[u] && !acked[u]) {
                if (!queued_retx[u]) { queued_retx[u] = 1; retx.push_front(u); }
                kick();
                return;
            }
    }

    double run() {
        if (P.proto == "fec") {
            if (P.M % P.k) { std::fprintf(stderr, "message packets M=%d must be a multiple of k=%d\n", P.M, P.k); std::exit(2); }
            G = P.M / P.k; nunits = G * P.n;
            cw_count.assign(G, 0); cw_data.assign(G, 0); cw_done.assign(G, 0); cw_decoding.assign(G, 0); cw_naked.assign(G, 0);
            core_free.assign(P.cores, 0.0);
        } else nunits = P.M;
        acked.assign(nunits, 0); queued_retx.assign(nunits, 0); sent_once.assign(nunits, 0);
        have.assign(nunits, 0); nak_sent.assign(nunits, 0);
        for (int u = 0; u < nunits; ++u) fresh.push_back(u);
        last_progress = 0; arm_rto(); kick();
        long guard = 0;
        while (!q.empty() && completion < 0) {
            Event e = q.top(); q.pop(); now = e.t;
            switch (e.type) {
                case SENDER_FREE: on_sender_free(); break;
                case ARRIVE:      on_arrive(e.a); break;
                case FB:          on_fb(e.a, e.b, e.list); break;
                case RTO_FIRE:    on_rto(e.a); break;
                case DECODE_DONE: fec_finish(e.a); break;
                case TLP_FIRE:    on_tlp(); break;
            }
            if (++guard > 400000000L) { std::fprintf(stderr, "event limit hit\n"); std::exit(3); }
        }
        if (completion < 0) { std::fprintf(stderr, "simulation ended without completion (%s)\n", P.proto.c_str()); std::exit(3); }
        return completion;
    }
};

// ── driver ──────────────────────────────────────────────────────────────────
static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> v; std::stringstream ss(s); std::string x;
    while (std::getline(ss, x, d)) if (!x.empty()) v.push_back(x);
    return v;
}

static double pct(std::vector<double>& v, double p) {
    size_t i = std::min(v.size() - 1, (size_t)std::llround(p * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
}

static void usage() {
    std::puts(
        "fecsim — FEC vs retransmission for RDMA messages\n"
        "  --protos gbn,sr,srtlp,fec    protocols to run\n"
        "  --sizes 64K,1M               message sizes (bytes; K/M suffix), rounded up to whole packets\n"
        "  --mtu 4096 --gbps 100        packet size and link rate\n"
        "  --rtts 2,5,10                round-trip times (µs)\n"
        "  --rto 100                    retransmission timeout (µs), all protocols\n"
        "  --pto -1                     tail-loss probe timeout for srtlp (µs; default 2xRTT)\n"
        "  --codes 18:16,20:16,36:32    FEC codes n:k\n"
        "  --decode hybrid_default      decode-cost method from bench_isal latency.csv, or 'zero'\n"
        "  --decode-table PATH          bench_isal latency.csv (pkt must equal --mtu)\n"
        "  --cores 1                    decoder cores for FEC\n"
        "  --channel bernoulli|ge|dc3   loss model\n"
        "  --loss 1e-5,1e-4,1e-3        bernoulli loss rates\n"
        "  --dc3-nd 0.0001,0.001        dc3 NORMAL->DEGRADED rates (burst frequency); other dc3/ge params via --dc3/--ge\n"
        "  --dc3 dn,di,id,pn,pd,pi      override dc3 parameters (defaults 0.2,0.1,0.3,0.001,0.03,0.35)\n"
        "  --flows 1                    parallel flows; completion = slowest (collective step)\n"
        "  --trials 2000 --seed 1 --out results.csv");
}

int main(int argc, char** argv) {
    std::vector<std::string> protos = {"gbn", "sr", "srtlp", "fec"};
    std::vector<std::string> sizes = {"64K", "1M"};
    std::vector<double> rtts = {5}, losses = {1e-4}, nds = {0.001};
    std::vector<std::pair<int, int>> codes = {{18, 16}};
    std::string chan = "bernoulli", table = "", out = "", dc3 = "", forced = "";
    std::string decode = "hybrid_default";
    int mtu = 4096, cores = 1, flows = 1, trials = 2000; double gbps = 100, rto = 100, pto = -1; uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (i + 1 >= argc) { usage(); return 2; }
        std::string v = argv[++i];
        auto dl = [&](const std::string& s) { std::vector<double> r; for (auto& x : split(s, ',')) r.push_back(std::stod(x)); return r; };
        if (a == "--protos") protos = split(v, ',');
        else if (a == "--sizes") sizes = split(v, ',');
        else if (a == "--mtu") mtu = std::stoi(v);
        else if (a == "--gbps") gbps = std::stod(v);
        else if (a == "--rtts") rtts = dl(v);
        else if (a == "--rto") rto = std::stod(v);
        else if (a == "--pto") pto = std::stod(v);
        else if (a == "--codes") { codes.clear(); for (auto& c : split(v, ',')) { auto p = split(c, ':'); codes.push_back({std::stoi(p[0]), std::stoi(p[1])}); } }
        else if (a == "--decode") decode = v;
        else if (a == "--decode-table") table = v;
        else if (a == "--cores") cores = std::stoi(v);
        else if (a == "--channel") chan = v;
        else if (a == "--loss") losses = dl(v);
        else if (a == "--dc3-nd") nds = dl(v);
        else if (a == "--dc3") dc3 = v;
        else if (a == "--force") { forced = v; chan = "forced"; }
        else if (a == "--flows") flows = std::stoi(v);
        else if (a == "--trials") trials = std::stoi(v);
        else if (a == "--seed") seed = std::stoull(v);
        else if (a == "--out") out = v;
        else { std::fprintf(stderr, "unknown flag %s\n", a.c_str()); usage(); return 2; }
    }
    DecodeCost dcost; dcost.method = decode;
    bool need_table = std::find(protos.begin(), protos.end(), "fec") != protos.end() && decode != "zero";
    if (need_table) { if (table.empty()) { std::fprintf(stderr, "--decode-table required for fec (or --decode zero)\n"); return 2; } dcost.load(table); }

    FILE* fo = out.empty() ? stdout : std::fopen(out.c_str(), "w");
    std::fprintf(fo, "proto,code,decode,cores,channel,loss_param,mean_loss,msg_bytes,packets,rtt_us,rto_us,gbps,mtu,flows,trials,"
                     "ideal_us,mean_us,p50_us,p99_us,p999_us,max_us,overhead,tx_per_msg\n");

    std::vector<double> loss_params = chan == "dc3" ? nds : chan == "bernoulli" ? losses : std::vector<double>{0};
    for (auto& sz : sizes) {
        double bytes = std::stod(sz);
        char suf = sz.back();
        if (suf == 'K' || suf == 'k') bytes *= 1024; else if (suf == 'M' || suf == 'm') bytes *= 1024 * 1024;
        int M = (int)std::ceil(bytes / mtu);
        for (double rtt : rtts) for (double lp : loss_params) for (auto& proto : protos) {
            auto code_list = proto == "fec" ? codes : std::vector<std::pair<int, int>>{{0, 0}};
            for (auto [n, k] : code_list) {
                Params P; P.proto = proto; P.L = mtu; P.gbps = gbps; P.rtt = rtt; P.rto = rto; P.pto = pto; P.cores = cores;
                P.n = n ? n : 1; P.k = k ? k : 1;
                P.M = M;
                if (proto == "fec" && M % k) P.M = ((M + k - 1) / k) * k;   // pad to whole codewords
                Channel ch; ch.kind = chan;
                if (chan == "forced") for (auto& x : split(forced, ',')) ch.forced.push_back(std::stol(x));
                if (chan == "bernoulli") ch.p = lp;
                if (chan == "dc3") {
                    ch.nd = lp;
                    if (!dc3.empty()) { auto v = split(dc3, ','); ch.dn = std::stod(v[0]); ch.di = std::stod(v[1]); ch.id = std::stod(v[2]); ch.pn = std::stod(v[3]); ch.pd = std::stod(v[4]); ch.pi = std::stod(v[5]); }
                }
                std::vector<double> ct; ct.reserve(trials);
                long tx = 0;
                for (int t = 0; t < trials; ++t) {
                    double worst = 0;
                    for (int f = 0; f < flows; ++f) {
                        // Same seed per (trial, flow) across protocols → common random numbers.
                        ch.reset(seed * 1000003ULL + (uint64_t)t * 7919ULL + (uint64_t)f * 104729ULL);
                        // Warm the Markov chain to stationarity so a message starts in a random state.
                        if (chan == "ge" || chan == "dc3") for (int w = 0; w < 2000; ++w) ch.lose();
                        Sim s(P, ch, dcost);
                        worst = std::max(worst, s.run());
                        tx += s.tx_count;
                    }
                    ct.push_back(worst);
                }
                double tp = P.t_pkt();
                double units = proto == "fec" ? (double)P.M / P.k * P.n : P.M;
                double last_data = proto == "fec" ? (double)(P.M / P.k - 1) * P.n + P.k : P.M;
                double ideal = last_data * tp + P.d();   // no-loss completion (FEC: last data packet)
                double mean = 0; for (double x : ct) mean += x; mean /= ct.size();
                std::string code = proto == "fec" ? std::to_string(n) + ":" + std::to_string(k) : "-";
                std::fprintf(fo, "%s,%s,%s,%d,%s,%g,%.3g,%.0f,%d,%g,%g,%g,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.1f\n",
                             proto.c_str(), code.c_str(), proto == "fec" ? decode.c_str() : "-", cores, chan.c_str(), lp, ch.mean_loss(),
                             bytes, P.M, rtt, rto, gbps, mtu, flows, trials,
                             ideal, mean, pct(ct, 0.5), pct(ct, 0.99), pct(ct, 0.999), *std::max_element(ct.begin(), ct.end()),
                             units / P.M - 1.0, (double)tx / trials / flows);
                std::fflush(fo);
            }
        }
    }
    if (fo != stdout) std::fclose(fo);
    return 0;
}
