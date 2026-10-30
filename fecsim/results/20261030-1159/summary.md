# fecsim — FEC vs retransmission for RDMA messages

Decode costs: measured medians from `decode_table.csv` (bench_isal, 4 KB packets).

Metric: p99 message completion time at the receiver. Ratio = p99(best of selective repeat, selective repeat + tail-loss probe) / p99(best FEC code); >1 means FEC finishes the 99th-percentile message sooner.

## Where FEC wins (Bernoulli loss, single flow, RTO 100 µs)

Cells (loss × size) where p99(FEC) < p99(best retransmission), out of all cells, per decoder:

| RTT (µs) | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|
| 2 | 1/24 | 3/24 | 4/24 | 4/24 |
| 5 | 1/24 | 6/24 | 7/24 | 8/24 |
| 10 | 3/24 | 8/24 | 8/24 | 9/24 |
| 25 | 11/24 | 11/24 | 11/24 | 11/24 |

## p99 completion (µs), RTT 5 µs, best FEC code per point

**128 KB**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 1e-05 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 0.0001 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 0.001 | 18.9 | 15.4 | 15.4 | 24.2 | 14.2 | 14.3 | 13.3 |
| 0.003 | 18.9 | 17.7 | 17.7 | 24.2 | 14.2 | 14.3 | 13.3 |
| 0.01 | 118.0 | 118.0 | 23.3 | 29.1 | 16.0 | 15.2 | 13.6 |

**2 MB**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 170.3 | 170.3 | 170.3 | 180.1 | 180.1 | 180.1 | 180.1 |
| 1e-05 | 170.3 | 170.3 | 170.3 | 180.1 | 180.1 | 180.1 | 180.1 |
| 0.0001 | 176.2 | 170.6 | 170.6 | 180.4 | 180.1 | 180.1 | 180.1 |
| 0.001 | 188.0 | 173.3 | 173.3 | 191.3 | 181.3 | 181.4 | 180.4 |
| 0.003 | 205.7 | 175.9 | 175.9 | 191.3 | 181.3 | 181.4 | 180.4 |
| 0.01 | 322.5 | 278.5 | 183.2 | 199.4 | 184.5 | 182.8 | 181.4 |

## Collective step: slowest of 64 flows, RTT 5 µs (p99 µs)

**128 KB per flow**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 1e-05 | 18.9 | 13.7 | 13.7 | 18.6 | 13.3 | 13.3 | 13.3 |
| 0.0001 | 18.9 | 18.3 | 18.3 | 24.2 | 14.2 | 14.3 | 13.3 |
| 0.001 | 118.0 | 118.0 | 23.3 | 29.1 | 14.2 | 14.3 | 13.6 |
| 0.003 | 123.6 | 119.0 | 23.6 | 29.5 | 16.0 | 15.2 | 14.0 |
| 0.01 | 218.0 | 123.6 | 118.3 | 29.8 | 18.2 | 16.8 | 14.3 |

**512 KB per flow**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 50.1 | 44.8 | 44.8 | 46.7 | 46.4 | 46.4 | 46.4 |
| 1e-05 | 50.3 | 46.8 | 46.8 | 54.2 | 47.6 | 47.7 | 46.7 |
| 0.0001 | 50.3 | 49.8 | 49.8 | 57.6 | 47.6 | 47.7 | 46.7 |
| 0.001 | 155.3 | 149.8 | 55.1 | 62.6 | 47.6 | 47.7 | 47.1 |
| 0.003 | 161.0 | 150.4 | 55.4 | 64.9 | 50.8 | 49.0 | 47.4 |
| 0.01 | 260.3 | 249.8 | 160.8 | 77.8 | 55.7 | 53.8 | 50.0 |

## DC3 (bursty) loss

Burst frequency → mean loss rate: 1e-05 → 0.001, 0.0001 → 0.0011, 0.001 → 0.0017, 0.01 → 0.0078

## Tail latency vs parity overhead (m×m + ISA-L decode, RTO 100 µs)

| setting | loss | code | overhead | FEC p99 (µs) | best retransmission p99 (µs) |
|---|---|---|---|---|---|
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 14.3 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 14.6 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 15.3 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 14.7 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 14.7 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 17:16 | 6.25% | 14.3 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 18:16 | 12.50% | 14.6 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 20:16 | 25.00% | 15.3 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 34:32 | 6.25% | 15.6 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 36:32 | 12.50% | 15.5 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 17:16 | 6.25% | 118.6 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 18:16 | 12.50% | 20.3 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 20:16 | 25.00% | 16.8 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 34:32 | 6.25% | 119.0 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 36:32 | 12.50% | 18.5 | 118.3 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 47.7 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 50.0 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 54.6 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 48.1 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 50.1 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 17:16 | 6.25% | 47.7 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 18:16 | 12.50% | 50.0 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 20:16 | 25.00% | 54.6 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 34:32 | 6.25% | 49.0 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 36:32 | 12.50% | 50.9 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 17:16 | 6.25% | 152.4 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 18:16 | 12.50% | 55.7 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 20:16 | 25.00% | 56.1 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 34:32 | 6.25% | 152.4 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 36:32 | 12.50% | 53.8 | 160.8 |
| single 2 MB message, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 180.4 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 190.6 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 210.9 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 180.1 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 189.9 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.001 | 17:16 | 6.25% | 181.4 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 18:16 | 12.50% | 191.6 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 20:16 | 25.00% | 211.9 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 34:32 | 6.25% | 181.8 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 36:32 | 12.50% | 191.6 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.01 | 17:16 | 6.25% | 186.4 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 18:16 | 12.50% | 192.2 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 20:16 | 25.00% | 212.2 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 34:32 | 6.25% | 182.8 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 36:32 | 12.50% | 192.4 | 183.2 |

