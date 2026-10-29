# ISA-L comparison — summary

CPU: **Intel(R) Core(TM) i9-14900HX** · SIMD: `avx2 gfni vpclmulqdq` · governor: `n/a`

Kernel compared: **avx2** (same ISA for both libraries).

## Decode latency, 4 KB packets (median µs, single pinned core)

| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |
|---|---|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 6.0 | 0.68 | 0.32 | 0.37 | 0.30 | 2.2× | 1.20× |
| RS(8,4) | 1 | 6.8 | 0.66 | 0.32 | 0.34 | 0.34 | 1.9× | 1.00× |
| RS(8,4) | 2 | 13.2 | 0.95 | 0.34 | 0.61 | 0.50 | 1.9× | 1.22× |
| RS(8,4) | 4 | 26.3 | 1.26 | 0.33 | 0.93 | 0.97 | 1.3× | 0.95× |
| RS(16,8) | 1 | 13.7 | 2.63 | 1.95 | 0.68 | 0.66 | 4.0× | 1.04× |
| RS(16,8) | 2 | 23.2 | 2.94 | 1.94 | 1.03 | 0.96 | 3.1× | 1.07× |
| RS(16,8) | 4 | 46.1 | 4.11 | 2.27 | 1.85 | 1.79 | 2.3× | 1.04× |
| RS(16,8) | 8 | 94.2 | 5.17 | 1.87 | 3.30 | 3.31 | 1.6× | 1.00× |
| RS(18,16) | 1 | 25.7 | 11.18 | 9.71 | 1.48 | 1.30 | 8.6× | 1.14× |
| RS(18,16) | 2 | 46.3 | 13.27 | 10.78 | 2.37 | 1.85 | 7.2× | 1.28× |
| RS(20,16) | 1 | 24.1 | 11.01 | 9.56 | 1.45 | 1.37 | 8.0× | 1.06× |
| RS(20,16) | 2 | 45.9 | 12.11 | 9.93 | 2.20 | 1.79 | 6.8× | 1.23× |
| RS(20,16) | 4 | 91.0 | 14.16 | 10.71 | 3.43 | 3.27 | 4.3× | 1.05× |
| RS(32,16) | 1 | 26.2 | 11.36 | 9.86 | 1.49 | 1.31 | 8.6× | 1.14× |
| RS(32,16) | 2 | 45.9 | 12.01 | 9.93 | 2.10 | 1.99 | 6.0× | 1.05× |
| RS(32,16) | 4 | 100.8 | 15.90 | 12.06 | 3.85 | 3.33 | 4.8× | 1.16× |
| RS(32,16) | 16 | 384.5 | 27.23 | 13.19 | 13.20 | 13.42 | 2.0× | 0.98× |
| RS(36,32) | 1 | 53.0 | 63.17 | 59.79 | 3.38 | 2.72 | 23.2× | 1.24× |
| RS(36,32) | 2 | 101.2 | 62.77 | 58.27 | 4.45 | 3.94 | 15.9× | 1.13× |
| RS(36,32) | 4 | 181.0 | 69.52 | 62.06 | 7.38 | 5.67 | 12.3× | 1.30× |

*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, so only the region multiply remains. Speedup < 1 there means the cached baseline wins.

## Packet-size crossover

- RS(8,4) m'=1: speedup vs ISA-L 1.3–3.6×; vs cached ISA-L 0.42–1.27× (cached ISA-L faster at 256, 4096 B)
- RS(8,4) m'=2: speedup vs ISA-L 1.3–2.7×; vs cached ISA-L 0.36–1.20× (cached ISA-L faster at 256 B)
- RS(18,16) m'=1: speedup vs ISA-L 1.7–46.0×; vs cached ISA-L 0.45–1.22× (cached ISA-L faster at 256 B)
- RS(18,16) m'=2: speedup vs ISA-L 1.6–30.5×; vs cached ISA-L 0.41–1.21× (cached ISA-L faster at 256 B)
- RS(32,16) m'=1: speedup vs ISA-L 1.7–50.7×; vs cached ISA-L 0.58–1.18× (cached ISA-L faster at 256 B)
- RS(32,16) m'=2: speedup vs ISA-L 1.5–29.5×; vs cached ISA-L 0.40–1.19× (cached ISA-L faster at 256 B)
- RS(36,32) m'=1: speedup vs ISA-L 1.8–180.2×; vs cached ISA-L 0.68–1.42× (cached ISA-L faster at 256, 65536 B)
- RS(36,32) m'=2: speedup vs ISA-L 1.5–96.8×; vs cached ISA-L 0.55–1.28× (cached ISA-L faster at 256, 65536 B)

## Cores needed for line rate (4 KB packets)

Decode assumes every codeword lost m' data packets (worst case). Encode runs on every codeword, so it usually sets the core count.

| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |
|---|---|---|---|---|---|---|---|
| RS(18,16) | decode | 1 | isal_avx2 | 40.8 | 240.7 @ 7 | 3 | 12 |
| RS(18,16) | decode | 2 | isal_avx2 | 38.8 | 217.0 @ 7 | 4 | 13 |
| RS(18,16) | decode | 1 | isal_default | 45.7 | 247.0 @ 7 | 3 | 12 |
| RS(18,16) | decode | 2 | isal_default | 44.6 | 241.9 @ 7 | 3 | 12 |
| RS(18,16) | decode | 1 | ours_avx2 | 348.6 | 1647.1 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | ours_avx2 | 244.6 | 1211.5 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx2 | 226.8 | 942.9 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_default | 508.8 | 2191.5 @ 7 | 1 | 2 |
| RS(18,16) | encode | - | ours_avx2 | 273.9 | 1339.1 @ 7 | 1 | 3 |
| RS(32,16) | decode | 1 | isal_avx2 | 42.0 | 230.5 @ 7 | 4 | 13 |
| RS(32,16) | decode | 2 | isal_avx2 | 39.1 | 212.7 @ 7 | 4 | 14 |
| RS(32,16) | decode | 1 | isal_default | 45.1 | 255.9 @ 7 | 3 | 11 |
| RS(32,16) | decode | 2 | isal_default | 40.8 | 233.7 @ 7 | 3 | 12 |
| RS(32,16) | decode | 1 | ours_avx2 | 347.8 | 1583.1 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | ours_avx2 | 247.3 | 1193.4 @ 7 | 1 | 3 |
| RS(32,16) | encode | - | isal_avx2 | 34.4 | 174.8 @ 7 | 5 | 17 |
| RS(32,16) | encode | - | isal_default | 74.3 | 371.2 @ 7 | 2 | 8 |
| RS(32,16) | encode | - | ours_avx2 | 43.7 | 213.8 @ 7 | 4 | 14 |
| RS(36,32) | decode | 1 | isal_avx2 | 16.0 | 93.4 @ 7 | 8 | 30 |
| RS(36,32) | decode | 2 | isal_avx2 | 15.4 | 84.1 @ 7 | 9 | 34 |
| RS(36,32) | decode | 1 | isal_default | 17.0 | 94.0 @ 7 | 8 | 30 |
| RS(36,32) | decode | 2 | isal_default | 16.3 | 83.5 @ 7 | 9 | 34 |
| RS(36,32) | decode | 1 | ours_avx2 | 307.0 | 1165.1 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx2 | 246.7 | 921.0 @ 7 | 1 | 4 |
| RS(36,32) | encode | - | isal_avx2 | 127.4 | 594.2 @ 7 | 2 | 5 |
| RS(36,32) | encode | - | isal_default | 253.6 | 992.0 @ 7 | 1 | 3 |
| RS(36,32) | encode | - | ours_avx2 | 177.1 | 672.6 @ 7 | 2 | 5 |

Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); beyond the measured range this assumes linear scaling.

