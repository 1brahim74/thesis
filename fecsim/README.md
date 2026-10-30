# fecsim — when does erasure coding beat retransmission for RDMA?

`fecsim` is a discrete-event simulator for one RDMA message, or for the slowest of N parallel flows (one step of a collective), sent over a lossy link. It compares four ways of recovering lost packets:

| protocol | model |
|---|---|
| `gbn` | Go-back-N, as in commodity RoCE NICs. The receiver drops out-of-order packets and sends one NAK per gap; the sender rewinds. A lost final packet waits for the timeout (RTO). |
| `sr` | Selective repeat, IRN-style. The receiver keeps out-of-order packets and NAKs each hole once; only lost packets are resent, ahead of new data. Tail losses and retransmissions that are lost again wait for the RTO. |
| `srtlp` | `sr` plus one tail-loss probe: PTO (default 2×RTT) after the last new packet, the highest unacknowledged packet is resent. |
| `fec` | Systematic RS(n,k): k data packets followed by n−k parity packets. A codeword completes when its k data packets arrive, or when any k packets arrive and the decoder finishes. Decode time is the **measured** median from `bench_isal` for the chosen decoder, queued on a fixed number of decoder cores. A codeword that loses more than n−k packets requests its missing data (selective-repeat fallback) as soon as its last packet arrives, or after the RTO if that packet was also lost. |

Timing model:
- link rate B, MTU L, one-way delay RTT/2, serialization L·8/B per packet;
- the sender transmits back-to-back at line rate with no window limit;
- ACKs and NAKs are never lost;
- losses come from a per-transmission channel model: Bernoulli, Gilbert–Elliott, or DC3 from `FEC_Codec_v3/channel.hpp`.

Every protocol sees the same random loss draws (common random numbers).

## Run

```bash
cd fecsim
make test                 # 17 hand-computed checks (below)
./run_sweeps.sh           # all figures, using the newest bench_isal latency.csv
QUICK=1 ./run_sweeps.sh   # few trials, pipeline check only
```

Output goes to `results/<date>/`:
- `bernoulli.csv`, `dc3.csv`, `collective.csv` and `rto.csv`: percentiles of completion time per configuration;
- `summary.md`;
- `figures/fig1`–`fig5`.

The decode cost comes from `../bench_isal/results/*/latency.csv` (4 KB packets). The FEC codes swept are the candidates that table has measurements for.

## Validation (`make test`)

The checks use a 64 KB message (16 × 4 KB packets) at 100 Gbps, RTT 5 µs and RTO 100 µs. That gives t = 0.32768 µs per packet and d = 2.5 µs one-way. Each simulated completion time must match the hand calculation to within 2 ns.

| case | expected (µs) | derivation |
|---|---|---|
| no loss | 7.743 | 16t + d |
| tx 5 lost, SR | 10.121 | packet 6 arrives at 7t+d → NAK at sender 7t+2d → resend → +t+d |
| tx 5 lost, GBN | 13.398 | same NAK, then 11 packets resent: 7t+2d+11t+d |
| tx 5 lost, FEC 18:16 | 8.071 | first parity arrives at 17t+d, completing k packets |
| tx 15 (tail) lost, SR/GBN | 112.743 | last ACK at 15t+2d, RTO 100 µs, resend: +t+d |
| tx 15 lost, SR+TLP | 18.071 | probe at 16t+2·RTT, then +t+d |
| tx 15 lost, FEC | 8.071 | parity covers it |
| 3 lost > parity, FEC | 13.726 | last parity at 18t+d → NAK → first resent packet completes k |
| … and last parity lost | 113.726 | no end-of-codeword signal, so the RTO fires |
| retransmission lost again, SR | 212.743 | two RTOs |
| 2 codewords, 3 lost in the first | 13.969 | resends queue behind codeword 2 in transmission |

## First results

These come from a quick-mode run (400 trials per point, 100 for 64-flow steps). The decode costs are from the i9-14900HX laptop run. The trends are clear; the exact p99 values need the full run.

1. **At low loss (≤ 10⁻⁴), FEC only costs.** Its parity bandwidth (12.5% for every code measured so far) makes completion 8–11% slower for messages of 512 KB and up. Lower-overhead codes (17:16 and 34:32, about 6%) are now in the benchmark; their decode costs come with the next `bench_isal` run.
2. **At 10⁻³–10⁻² loss, FEC wins for small messages, and decode cost decides it.**
   - Example: 128 KB, RTT 5 µs, slowest of 64 flows at 10⁻³ loss. Selective repeat takes 118 µs, because a lost final packet waits for the RTO. Selective repeat with a tail-loss probe takes 23 µs.
   - FEC takes **25 µs with ISA-L's k×k decode**, which loses to the probe, and **14.6 µs with erasure-count decoding** (cm256 or m×m + ISA-L kernel), which wins by 1.6×.
   - With free decode it would take 13.6 µs, so erasure-count decoding gets within 7% of the bound.
3. **cm256 and m×m + ISA-L give almost the same network-level results.** Once decode costs about 1 µs, it stops mattering compared with RTT and serialization. The gap that matters is k×k versus erasure-count decoding, not which erasure-count implementation is used.
4. **Longer RTTs favour FEC.** At RTT 25 µs FEC wins up to 2 MB at 10⁻³ loss and above. At RTT 2 µs it wins only at 128 KB and 1% loss.

## Assumptions and limitations (state these in any write-up)

- **No congestion control, PFC, NIC pipeline or receive-side processing beyond decode.** Flows in a collective are independent and don't share a bottleneck.
- **The sender is never window-limited**, and feedback is never lost.
- **RTO is 100 µs for every protocol**, which is IRN's RTO_low. Commodity RoCE timeouts are often far larger (see `fig5_rto`), which favours FEC and SR+TLP even more.
- **DC3 is not calibrated.** Its NORMAL-state loss of 10⁻³ is far above measured RDMA fabrics, so the DC3 sweep floors at about 10⁻³ mean loss. Fitting DC3 to the incast measurements in Canel et al. (IMC 2024) is still open.
- **Decode cost is the single-core median from a laptop under WSL2.** Tail decode latency and server CPUs are not modelled.
