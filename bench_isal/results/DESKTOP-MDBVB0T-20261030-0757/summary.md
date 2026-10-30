# ISA-L comparison — summary

CPU: **Intel(R) Core(TM) i9-14900HX** · SIMD: `avx2 gfni vpclmulqdq` · governor: `n/a`

Kernel compared: **avx2** (same ISA for both libraries).

## Decode latency, 4 KB packets (median µs, single pinned core)

| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |
|---|---|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 5.9 | 0.61 | 0.28 | 0.34 | 0.31 | 2.0× | 1.10× |
| RS(8,4) | 1 | 6.4 | 0.57 | 0.28 | 0.29 | 0.29 | 1.9× | 0.99× |
| RS(8,4) | 2 | 11.2 | 0.77 | 0.30 | 0.47 | 0.43 | 1.8× | 1.07× |
| RS(8,4) | 4 | 22.4 | 1.19 | 0.31 | 0.87 | 0.79 | 1.5× | 1.11× |
| RS(16,8) | 1 | 11.9 | 2.58 | 1.92 | 0.66 | 0.57 | 4.5× | 1.15× |
| RS(16,8) | 2 | 22.4 | 2.75 | 1.80 | 0.95 | 0.83 | 3.3× | 1.15× |
| RS(16,8) | 4 | 47.8 | 3.60 | 1.98 | 1.63 | 1.54 | 2.3× | 1.06× |
| RS(16,8) | 8 | 93.9 | 5.18 | 1.86 | 3.32 | 3.34 | 1.5× | 0.99× |
| RS(18,16) | 1 | 27.8 | 12.98 | 11.25 | 1.74 | 1.44 | 9.0× | 1.21× |
| RS(18,16) | 2 | 52.7 | 12.35 | 10.24 | 2.13 | 2.08 | 5.9× | 1.02× |
| RS(20,16) | 1 | 25.3 | 11.03 | 9.47 | 1.39 | 1.29 | 8.5× | 1.07× |
| RS(20,16) | 2 | 45.9 | 12.55 | 10.22 | 2.30 | 1.73 | 7.2× | 1.33× |
| RS(20,16) | 4 | 91.4 | 14.70 | 11.10 | 3.58 | 3.11 | 4.7× | 1.15× |
| RS(32,16) | 1 | 22.7 | 10.38 | 9.07 | 1.32 | 1.17 | 8.9× | 1.12× |
| RS(32,16) | 2 | 44.7 | 11.74 | 9.73 | 2.04 | 1.81 | 6.5× | 1.13× |
| RS(32,16) | 4 | 89.5 | 13.88 | 10.55 | 3.34 | 2.96 | 4.7× | 1.13× |
| RS(32,16) | 16 | 369.5 | 25.34 | 12.28 | 13.02 | 14.69 | 1.7× | 0.89× |
| RS(36,32) | 1 | 47.5 | 57.49 | 54.42 | 3.04 | 2.41 | 23.9× | 1.27× |
| RS(36,32) | 2 | 90.1 | 59.51 | 55.02 | 4.44 | 3.43 | 17.4× | 1.30× |
| RS(36,32) | 4 | 188.7 | 68.44 | 60.95 | 7.36 | 6.13 | 11.2× | 1.20× |

*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, so only the region multiply remains. Speedup < 1 there means the cached baseline wins.

## Best available kernel (ISA-L dispatcher; GFNI if the CPU has it), 4 KB, median µs

| code | m' | ISA-L total | ISA-L cached (region only) | m×m + ISA-L kernel | speedup vs ISA-L | vs cached ISA-L |
|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.39 | 0.12 | 0.15 | 2.5× | 0.75× |
| RS(8,4) | 1 | 0.42 | 0.12 | 0.16 | 2.6× | 0.73× |
| RS(8,4) | 2 | 0.45 | 0.17 | 0.25 | 1.8× | 0.68× |
| RS(8,4) | 4 | 0.59 | 0.34 | 0.44 | 1.3× | 0.78× |
| RS(16,8) | 1 | 1.94 | 0.20 | 0.26 | 7.4× | 0.77× |
| RS(16,8) | 2 | 2.09 | 0.33 | 0.43 | 4.8× | 0.76× |
| RS(16,8) | 4 | 2.78 | 0.77 | 0.99 | 2.8× | 0.78× |
| RS(16,8) | 8 | 3.05 | 1.36 | 2.09 | 1.5× | 0.65× |
| RS(18,16) | 1 | 11.45 | 0.62 | 0.66 | 17.4× | 0.95× |
| RS(18,16) | 2 | 11.14 | 0.83 | 1.00 | 11.1× | 0.83× |
| RS(20,16) | 1 | 10.41 | 0.56 | 0.65 | 16.1× | 0.87× |
| RS(20,16) | 2 | 11.01 | 0.84 | 0.98 | 11.2× | 0.86× |
| RS(20,16) | 4 | 11.48 | 1.53 | 1.87 | 6.1× | 0.82× |
| RS(32,16) | 1 | 9.46 | 0.51 | 0.60 | 15.8× | 0.86× |
| RS(32,16) | 2 | 10.45 | 0.79 | 0.94 | 11.2× | 0.84× |
| RS(32,16) | 4 | 11.92 | 1.62 | 1.92 | 6.2× | 0.84× |
| RS(32,16) | 16 | 17.33 | 5.89 | 9.95 | 1.7× | 0.59× |
| RS(36,32) | 1 | 55.19 | 1.17 | 1.40 | 39.4× | 0.84× |
| RS(36,32) | 2 | 56.00 | 1.59 | 1.81 | 31.0× | 0.88× |
| RS(36,32) | 4 | 68.64 | 3.71 | 4.22 | 16.3× | 0.88× |

## vs cm256 (open-source erasure-count decoder), 4 KB, median µs

| code | m' | cm256 | m×m + ISA-L kernel | this work (avx2) | best of ours vs cm256 |
|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.09 | 0.15 | 0.31 | 0.61× |
| RS(8,4) | 1 | 0.12 | 0.16 | 0.29 | 0.77× |
| RS(8,4) | 2 | 0.43 | 0.25 | 0.43 | 1.72× |
| RS(8,4) | 4 | 0.92 | 0.44 | 0.79 | 2.10× |
| RS(16,8) | 1 | 0.24 | 0.26 | 0.57 | 0.92× |
| RS(16,8) | 2 | 0.77 | 0.43 | 0.83 | 1.78× |
| RS(16,8) | 4 | 1.91 | 0.99 | 1.54 | 1.93× |
| RS(16,8) | 8 | 4.11 | 2.09 | 3.34 | 1.97× |
| RS(18,16) | 1 | 0.64 | 0.66 | 1.44 | 0.97× |
| RS(18,16) | 2 | 1.93 | 1.00 | 2.08 | 1.92× |
| RS(20,16) | 1 | 0.61 | 0.65 | 1.29 | 0.95× |
| RS(20,16) | 2 | 1.72 | 0.98 | 1.73 | 1.75× |
| RS(20,16) | 4 | 3.65 | 1.87 | 3.11 | 1.95× |
| RS(32,16) | 1 | 0.58 | 0.60 | 1.17 | 0.97× |
| RS(32,16) | 2 | 1.67 | 0.94 | 1.81 | 1.79× |
| RS(32,16) | 4 | 3.80 | 1.92 | 2.96 | 1.98× |
| RS(32,16) | 16 | 18.18 | 9.95 | 14.69 | 1.83× |
| RS(36,32) | 1 | 1.38 | 1.40 | 2.41 | 0.99× |
| RS(36,32) | 2 | 3.51 | 1.81 | 3.43 | 1.94× |
| RS(36,32) | 4 | 8.23 | 4.22 | 6.13 | 1.95× |

cm256's first recovery row is all-ones (plain XOR), so a single loss recovered from it needs no multiplications; compare m'≥2 for the decoder itself. cm256 gets its own parity for the same data and loss patterns.

## Packet-size crossover

- RS(8,4) m'=1: vs cm256 0.39–0.64×
- RS(8,4) m'=1: speedup vs ISA-L 1.3–3.9×; vs cached ISA-L 0.44–1.26× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(8,4) m'=2: vs cm256 0.61–1.06×
- RS(8,4) m'=2: speedup vs ISA-L 1.2–2.6×; vs cached ISA-L 0.35–1.21× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(18,16) m'=1: vs cm256 0.50–0.74×
- RS(18,16) m'=1: speedup vs ISA-L 1.7–57.1×; vs cached ISA-L 0.68–1.28× (cached ISA-L faster at 256, 512, 1024 B)
- RS(18,16) m'=2: vs cm256 0.78–1.21×
- RS(18,16) m'=2: speedup vs ISA-L 1.6–37.2×; vs cached ISA-L 0.61–1.33× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=1: vs cm256 0.43–0.70×
- RS(32,16) m'=1: speedup vs ISA-L 1.8–52.9×; vs cached ISA-L 0.63–1.36× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=2: vs cm256 0.85–1.21×
- RS(32,16) m'=2: speedup vs ISA-L 1.6–35.6×; vs cached ISA-L 0.61–1.32× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=1: vs cm256 0.43–1.20×
- RS(36,32) m'=1: speedup vs ISA-L 2.1–190.8×; vs cached ISA-L 0.75–1.31× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=2: vs cm256 0.79–1.15×
- RS(36,32) m'=2: speedup vs ISA-L 1.8–113.2×; vs cached ISA-L 0.64–1.25× (cached ISA-L faster at 256, 512, 1024 B)

## Cores needed for line rate (4 KB packets)

Decode assumes every codeword lost m' data packets (worst case). Encode runs on every codeword, so it usually sets the core count.

| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |
|---|---|---|---|---|---|---|---|
| RS(18,16) | decode | 1 | cm256 | 543.3 | 2120.3 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | cm256 | 248.9 | 1128.3 @ 7 | 1 | 3 |
| RS(18,16) | decode | 1 | isal_avx2 | 42.6 | 232.0 @ 7 | 4 | 13 |
| RS(18,16) | decode | 2 | isal_avx2 | 38.8 | 204.2 @ 7 | 4 | 14 |
| RS(18,16) | decode | 1 | isal_default | 45.7 | 240.4 @ 7 | 3 | 12 |
| RS(18,16) | decode | 2 | isal_default | 44.6 | 236.0 @ 7 | 3 | 12 |
| RS(18,16) | decode | 1 | ours_avx2 | 359.5 | 1572.2 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | ours_avx2 | 255.5 | 1162.4 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | cm256 | 238.4 | 1085.0 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx2 | 217.8 | 1012.9 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_default | 523.4 | 2221.2 @ 7 | 1 | 2 |
| RS(18,16) | encode | - | ours_avx2 | 277.5 | 1359.6 @ 7 | 1 | 3 |
| RS(32,16) | decode | 1 | cm256 | 534.3 | 2120.7 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | cm256 | 254.6 | 1078.3 @ 7 | 1 | 3 |
| RS(32,16) | decode | 1 | isal_avx2 | 41.7 | 232.0 @ 7 | 4 | 13 |
| RS(32,16) | decode | 2 | isal_avx2 | 39.1 | 205.3 @ 7 | 4 | 14 |
| RS(32,16) | decode | 1 | isal_default | 46.2 | 243.9 @ 7 | 3 | 12 |
| RS(32,16) | decode | 2 | isal_default | 44.4 | 234.2 @ 7 | 3 | 12 |
| RS(32,16) | decode | 1 | ours_avx2 | 360.8 | 1575.3 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | ours_avx2 | 255.3 | 1190.1 @ 7 | 1 | 3 |
| RS(32,16) | encode | - | cm256 | 27.8 | 134.0 @ 7 | 6 | 21 |
| RS(32,16) | encode | - | isal_avx2 | 35.3 | 169.4 @ 7 | 5 | 17 |
| RS(32,16) | encode | - | isal_default | 75.3 | 370.4 @ 7 | 2 | 8 |
| RS(32,16) | encode | - | ours_avx2 | 44.6 | 212.6 @ 7 | 4 | 14 |
| RS(36,32) | decode | 1 | cm256 | 512.9 | 1115.5 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | cm256 | 247.3 | 730.5 @ 7 | 1 | 4 |
| RS(36,32) | decode | 1 | isal_avx2 | 16.6 | 85.6 @ 7 | 9 | 33 |
| RS(36,32) | decode | 2 | isal_avx2 | 15.8 | 84.3 @ 7 | 9 | 34 |
| RS(36,32) | decode | 1 | isal_default | 16.8 | 90.7 @ 7 | 8 | 31 |
| RS(36,32) | decode | 2 | isal_default | 16.5 | 92.6 @ 7 | 8 | 31 |
| RS(36,32) | decode | 1 | ours_avx2 | 349.0 | 1086.1 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx2 | 242.3 | 875.3 @ 7 | 1 | 4 |
| RS(36,32) | encode | - | cm256 | 104.8 | 489.5 @ 7 | 2 | 6 |
| RS(36,32) | encode | - | isal_avx2 | 130.1 | 568.7 @ 7 | 2 | 5 |
| RS(36,32) | encode | - | isal_default | 263.2 | 918.0 @ 7 | 1 | 4 |
| RS(36,32) | encode | - | ours_avx2 | 175.7 | 684.6 @ 7 | 2 | 5 |

Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); beyond the measured range this assumes linear scaling.

