# fecsim — FEC vs retransmission for RDMA messages

Decode costs: measured medians from `decode_table.csv` (bench_isal, 4 KB packets).

Metric: p99 message completion time at the receiver. Ratio = p99(best of selective repeat, selective repeat + tail-loss probe) / p99(best FEC code); >1 means FEC finishes the 99th-percentile message sooner.

## Where FEC wins (Bernoulli loss, single flow, RTO 100 µs)

Cells (loss × size) where p99(FEC) < p99(best retransmission), out of all cells, per decoder:

| RTT (µs) | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|
| 2 | 0/24 | 3/24 | 4/24 | 4/24 |
| 5 | 0/24 | 6/24 | 6/24 | 8/24 |
| 10 | 1/24 | 8/24 | 8/24 | 9/24 |
| 25 | 4/24 | 11/24 | 11/24 | 11/24 |

## p99 completion (µs), RTT 5 µs, best FEC code per point

**128 KB**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 1e-05 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 0.0001 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 0.001 | 18.9 | 15.4 | 15.4 | 34.4 | 14.6 | 14.5 | 13.3 |
| 0.003 | 18.9 | 17.7 | 17.7 | 34.4 | 14.6 | 14.5 | 13.3 |
| 0.01 | 118.0 | 118.0 | 23.3 | 49.5 | 16.4 | 15.3 | 13.6 |

**2 MB**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 170.3 | 170.3 | 170.3 | 180.1 | 180.1 | 180.1 | 180.1 |
| 1e-05 | 170.3 | 170.3 | 170.3 | 180.1 | 180.1 | 180.1 | 180.1 |
| 0.0001 | 176.2 | 170.6 | 170.6 | 180.4 | 180.1 | 180.1 | 180.1 |
| 0.001 | 188.0 | 173.3 | 173.3 | 201.5 | 181.7 | 181.6 | 180.4 |
| 0.003 | 205.7 | 175.9 | 175.9 | 205.5 | 181.7 | 181.6 | 180.4 |
| 0.01 | 322.5 | 278.5 | 183.2 | 248.9 | 186.0 | 183.3 | 181.4 |

## Collective step: slowest of 64 flows, RTT 5 µs (p99 µs)

**128 KB per flow**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 1e-05 | 18.9 | 13.7 | 13.7 | 28.8 | 13.3 | 13.3 | 13.3 |
| 0.0001 | 18.9 | 18.3 | 18.3 | 34.4 | 14.6 | 14.5 | 13.3 |
| 0.001 | 118.0 | 118.0 | 23.3 | 49.5 | 14.6 | 14.5 | 13.6 |
| 0.003 | 123.6 | 119.0 | 23.6 | 50.0 | 16.4 | 15.3 | 14.0 |
| 0.01 | 218.0 | 123.6 | 118.3 | 50.3 | 18.9 | 16.7 | 14.3 |

**512 KB per flow**

| loss | go-back-N | SR | SR+TLP | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 1e-06 | 50.1 | 44.8 | 44.8 | 46.7 | 46.4 | 46.4 | 46.4 |
| 1e-05 | 50.3 | 46.8 | 46.8 | 64.4 | 48.0 | 47.9 | 46.7 |
| 0.0001 | 50.3 | 49.8 | 49.8 | 67.8 | 48.0 | 47.9 | 46.7 |
| 0.001 | 155.3 | 149.8 | 55.1 | 83.0 | 48.0 | 47.9 | 47.1 |
| 0.003 | 161.0 | 150.4 | 55.4 | 97.8 | 51.8 | 49.1 | 47.4 |
| 0.01 | 260.3 | 249.8 | 160.8 | 133.8 | 55.7 | 53.1 | 50.0 |

## DC3 (bursty) loss

Burst frequency → mean loss rate: 1e-05 → 0.001, 0.0001 → 0.0011, 0.001 → 0.0017, 0.01 → 0.0078

## Tail latency vs parity overhead (m×m + ISA-L decode, RTO 100 µs)

| setting | loss | code | overhead | FEC p99 (µs) | best retransmission p99 (µs) |
|---|---|---|---|---|---|
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 14.5 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 14.8 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 15.5 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 15.2 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 15.1 | 18.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 17:16 | 6.25% | 14.5 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 18:16 | 12.50% | 14.8 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 20:16 | 25.00% | 15.5 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 34:32 | 6.25% | 15.7 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.001 | 36:32 | 12.50% | 15.7 | 23.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 17:16 | 6.25% | 118.6 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 18:16 | 12.50% | 20.3 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 20:16 | 25.00% | 16.7 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 34:32 | 6.25% | 119.0 | 118.3 |
| slowest of 64 flows, 128 KB each, RTT 5 µs | 0.01 | 36:32 | 12.50% | 17.7 | 118.3 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 47.9 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 50.2 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 54.8 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 48.6 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 50.5 | 49.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 17:16 | 6.25% | 47.9 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 18:16 | 12.50% | 50.2 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 20:16 | 25.00% | 54.8 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 34:32 | 6.25% | 49.1 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.001 | 36:32 | 12.50% | 51.1 | 55.1 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 17:16 | 6.25% | 152.4 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 18:16 | 12.50% | 55.7 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 20:16 | 25.00% | 56.1 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 34:32 | 6.25% | 152.4 | 160.8 |
| slowest of 64 flows, 512 KB each, RTT 5 µs | 0.01 | 36:32 | 12.50% | 53.1 | 160.8 |
| single 2 MB message, RTT 5 µs | 0.0001 | 17:16 | 6.25% | 180.4 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 18:16 | 12.50% | 190.6 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 20:16 | 25.00% | 210.9 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 34:32 | 6.25% | 180.1 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.0001 | 36:32 | 12.50% | 189.9 | 170.6 |
| single 2 MB message, RTT 5 µs | 0.001 | 17:16 | 6.25% | 181.6 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 18:16 | 12.50% | 191.7 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 20:16 | 25.00% | 212.1 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 34:32 | 6.25% | 182.3 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.001 | 36:32 | 12.50% | 192.0 | 173.3 |
| single 2 MB message, RTT 5 µs | 0.01 | 17:16 | 6.25% | 186.4 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 18:16 | 12.50% | 192.3 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 20:16 | 25.00% | 212.4 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 34:32 | 6.25% | 183.3 | 183.2 |
| single 2 MB message, RTT 5 µs | 0.01 | 36:32 | 12.50% | 192.6 | 183.2 |

## Bursty loss calibrated to Meta measurements (64-flow step, RTT 5 µs, p99 µs)

Lossy bursts of mean 2.9 ms occupying a fraction of time per rack type (Ghabashneh et al., IMC 2022); loss inside a burst is swept because it is not published.

| size | rack | loss in burst | best retransmission | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 128 KB | typical rack | 0.01 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 128 KB | typical rack | 0.1 | 15.7 | 20.0 | 15.5 | 15.3 | 14.3 |
| 128 KB | typical rack | 0.3 | 124.3 | 43.5 | 23.3 | 22.9 | 21.9 |
| 128 KB | busy rack | 0.01 | 13.7 | 28.8 | 14.6 | 14.5 | 13.3 |
| 128 KB | busy rack | 0.1 | 25.0 | 51.7 | 19.2 | 17.1 | 15.6 |
| 128 KB | busy rack | 0.3 | 331.3 | 164.8 | 129.9 | 129.4 | 128.2 |
| 128 KB | contended rack | 0.01 | 18.3 | 34.4 | 14.6 | 14.5 | 13.6 |
| 128 KB | contended rack | 0.1 | 129.0 | 57.3 | 21.9 | 21.9 | 20.9 |
| 128 KB | contended rack | 0.3 | 435.0 | 269.8 | 234.2 | 231.6 | 229.9 |
| 512 KB | typical rack | 0.01 | 44.8 | 46.7 | 46.4 | 46.4 | 46.4 |
| 512 KB | typical rack | 0.1 | 57.4 | 117.7 | 50.9 | 50.7 | 50.0 |
| 512 KB | typical rack | 0.3 | 272.2 | 254.6 | 173.9 | 172.8 | 170.7 |
| 512 KB | busy rack | 0.01 | 46.2 | 67.8 | 47.4 | 47.4 | 46.7 |
| 512 KB | busy rack | 0.1 | 165.3 | 177.6 | 58.4 | 57.0 | 55.6 |
| 512 KB | busy rack | 0.3 | 486.5 | 300.4 | 186.6 | 183.0 | 181.3 |
| 512 KB | contended rack | 0.01 | 55.1 | 89.9 | 48.0 | 48.0 | 47.1 |
| 512 KB | contended rack | 0.1 | 264.7 | 179.1 | 64.5 | 62.2 | 61.6 |
| 512 KB | contended rack | 0.3 | 592.8 | 454.6 | 375.2 | 372.0 | 370.2 |

