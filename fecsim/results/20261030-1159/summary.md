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

