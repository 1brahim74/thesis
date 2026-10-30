# fecsim — FEC vs retransmission for RDMA messages

Decode costs: measured medians from `decode_table.csv` (bench_isal, 4 KB packets).

Metric: p99 message completion time at the receiver. Ratio = p99(best of selective repeat, selective repeat + tail-loss probe) / p99(best FEC code); >1 means FEC finishes the 99th-percentile message sooner.

## Bursty loss calibrated to Meta measurements (64-flow step, RTT 5 µs, p99 µs)

Lossy bursts of mean 2.9 ms occupying a fraction of time per rack type (Ghabashneh et al., IMC 2022); loss inside a burst is swept because it is not published.

| size | rack | loss in burst | best retransmission | FEC, ISA-L decode (k×k) | FEC, cm256 decode | FEC, m×m + ISA-L kernel | FEC, free decode (bound) |
|---|---|---|---|---|---|---|---|
| 128 KB | typical rack | 0.01 | 13.0 | 13.0 | 13.0 | 13.0 | 13.0 |
| 128 KB | typical rack | 0.1 | 15.7 | 19.6 | 15.2 | 15.2 | 14.3 |
| 128 KB | typical rack | 0.3 | 124.3 | 33.3 | 22.9 | 22.9 | 21.9 |
| 128 KB | busy rack | 0.01 | 13.7 | 19.3 | 14.2 | 14.3 | 13.3 |
| 128 KB | busy rack | 0.1 | 25.0 | 32.3 | 18.5 | 17.1 | 15.6 |
| 128 KB | busy rack | 0.3 | 331.3 | 145.7 | 129.9 | 129.2 | 128.2 |
| 128 KB | contended rack | 0.01 | 18.3 | 24.2 | 14.2 | 14.3 | 13.6 |
| 128 KB | contended rack | 0.1 | 129.0 | 37.6 | 21.9 | 21.9 | 20.9 |
| 128 KB | contended rack | 0.3 | 435.0 | 250.7 | 232.8 | 231.5 | 229.9 |
| 512 KB | typical rack | 0.01 | 44.8 | 46.7 | 46.4 | 46.4 | 46.4 |
| 512 KB | typical rack | 0.1 | 57.4 | 66.7 | 50.3 | 50.3 | 50.0 |
| 512 KB | typical rack | 0.3 | 272.2 | 193.9 | 173.5 | 172.7 | 170.7 |
| 512 KB | busy rack | 0.01 | 46.2 | 52.0 | 47.4 | 47.4 | 46.7 |
| 512 KB | busy rack | 0.1 | 165.3 | 98.4 | 58.3 | 56.9 | 55.6 |
| 512 KB | busy rack | 0.3 | 486.5 | 222.6 | 185.2 | 183.0 | 181.3 |
| 512 KB | contended rack | 0.01 | 55.1 | 62.6 | 48.0 | 48.0 | 47.1 |
| 512 KB | contended rack | 0.1 | 264.7 | 100.9 | 63.9 | 62.2 | 61.6 |
| 512 KB | contended rack | 0.3 | 592.8 | 402.4 | 374.2 | 372.2 | 370.2 |

