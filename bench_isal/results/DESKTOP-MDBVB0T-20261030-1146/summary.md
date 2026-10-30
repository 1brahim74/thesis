# ISA-L comparison — summary

CPU: **Intel(R) Core(TM) i9-14900HX** · SIMD: `avx2 gfni vpclmulqdq` · governor: `n/a`

Kernel compared: **avx2** (same ISA for both libraries).

## Decode latency, 4 KB packets (median µs, single pinned core)

| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |
|---|---|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 6.6 | 0.66 | 0.30 | 0.35 | 0.34 | 2.0× | 1.06× |
| RS(8,4) | 1 | 5.9 | 0.64 | 0.30 | 0.33 | 0.33 | 1.9× | 0.99× |
| RS(8,4) | 2 | 12.7 | 0.85 | 0.32 | 0.52 | 0.49 | 1.7× | 1.08× |
| RS(8,4) | 4 | 23.9 | 1.08 | 0.29 | 0.79 | 0.79 | 1.4× | 1.00× |
| RS(16,8) | 1 | 13.2 | 2.63 | 1.95 | 0.68 | 0.65 | 4.1× | 1.05× |
| RS(16,8) | 2 | 24.6 | 2.78 | 1.85 | 0.93 | 0.85 | 3.3× | 1.10× |
| RS(16,8) | 4 | 48.8 | 3.86 | 2.08 | 1.77 | 1.64 | 2.4× | 1.08× |
| RS(16,8) | 8 | 98.5 | 5.60 | 2.04 | 3.54 | 3.52 | 1.6× | 1.01× |
| RS(17,16) | 1 | 24.0 | 11.45 | 9.94 | 1.42 | 1.20 | 9.6× | 1.19× |
| RS(18,16) | 1 | 23.7 | 11.63 | 10.13 | 1.50 | 1.21 | 9.6× | 1.24× |
| RS(18,16) | 2 | 49.0 | 12.85 | 10.50 | 2.31 | 1.79 | 7.2× | 1.30× |
| RS(20,16) | 1 | 25.8 | 11.50 | 9.99 | 1.43 | 1.33 | 8.7× | 1.08× |
| RS(20,16) | 2 | 49.1 | 12.66 | 10.51 | 2.14 | 1.92 | 6.6× | 1.11× |
| RS(20,16) | 4 | 96.0 | 15.07 | 11.42 | 3.64 | 3.38 | 4.5× | 1.08× |
| RS(32,16) | 1 | 24.1 | 11.83 | 10.27 | 1.56 | 1.24 | 9.6× | 1.26× |
| RS(32,16) | 2 | 49.0 | 12.72 | 10.53 | 2.16 | 1.96 | 6.5× | 1.11× |
| RS(32,16) | 4 | 96.3 | 15.03 | 11.38 | 3.63 | 3.18 | 4.7× | 1.14× |
| RS(32,16) | 16 | 389.1 | 26.75 | 12.88 | 13.71 | 14.87 | 1.8× | 0.92× |
| RS(34,32) | 1 | 48.6 | 60.81 | 57.67 | 3.13 | 2.63 | 23.1× | 1.19× |
| RS(34,32) | 2 | 92.3 | 62.31 | 57.89 | 4.31 | 3.49 | 17.9× | 1.23× |
| RS(36,32) | 1 | 50.9 | 60.45 | 57.39 | 3.08 | 2.61 | 23.1× | 1.18× |
| RS(36,32) | 2 | 90.7 | 63.37 | 58.61 | 4.77 | 3.70 | 17.1× | 1.29× |
| RS(36,32) | 4 | 180.0 | 66.54 | 59.17 | 7.11 | 5.96 | 11.2× | 1.19× |

*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, so only the region multiply remains. Speedup < 1 there means the cached baseline wins.

## Best available kernel (ISA-L dispatcher; GFNI if the CPU has it), 4 KB, median µs

| code | m' | ISA-L total | ISA-L cached (region only) | m×m + ISA-L kernel | speedup vs ISA-L | vs cached ISA-L |
|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.40 | 0.12 | 0.18 | 2.2× | 0.69× |
| RS(8,4) | 1 | 0.42 | 0.12 | 0.17 | 2.4× | 0.70× |
| RS(8,4) | 2 | 0.46 | 0.17 | 0.24 | 1.9× | 0.71× |
| RS(8,4) | 4 | 0.66 | 0.38 | 0.50 | 1.3× | 0.76× |
| RS(16,8) | 1 | 2.14 | 0.22 | 0.29 | 7.3× | 0.76× |
| RS(16,8) | 2 | 2.31 | 0.36 | 0.48 | 4.8× | 0.76× |
| RS(16,8) | 4 | 2.55 | 0.69 | 0.97 | 2.6× | 0.71× |
| RS(16,8) | 8 | 3.19 | 1.41 | 2.04 | 1.6× | 0.69× |
| RS(17,16) | 1 | 10.54 | 0.58 | 0.62 | 17.1× | 0.94× |
| RS(18,16) | 1 | 10.70 | 0.58 | 0.68 | 15.8× | 0.86× |
| RS(18,16) | 2 | 10.64 | 0.80 | 0.95 | 11.2× | 0.84× |
| RS(20,16) | 1 | 10.74 | 0.58 | 0.67 | 16.1× | 0.87× |
| RS(20,16) | 2 | 11.26 | 0.85 | 0.95 | 11.8× | 0.89× |
| RS(20,16) | 4 | 12.51 | 1.70 | 1.99 | 6.3× | 0.85× |
| RS(32,16) | 1 | 10.86 | 0.59 | 0.68 | 16.0× | 0.87× |
| RS(32,16) | 2 | 10.94 | 0.84 | 0.98 | 11.2× | 0.86× |
| RS(32,16) | 4 | 12.92 | 1.73 | 2.11 | 6.1× | 0.82× |
| RS(32,16) | 16 | 18.23 | 6.21 | 10.20 | 1.8× | 0.61× |
| RS(34,32) | 1 | 58.33 | 1.23 | 1.36 | 42.9× | 0.91× |
| RS(34,32) | 2 | 59.66 | 1.70 | 1.92 | 31.0× | 0.88× |
| RS(36,32) | 1 | 58.19 | 1.24 | 1.38 | 42.0× | 0.90× |
| RS(36,32) | 2 | 57.83 | 1.62 | 1.83 | 31.7× | 0.89× |
| RS(36,32) | 4 | 63.84 | 3.44 | 4.16 | 15.4× | 0.83× |

## vs cm256 (open-source erasure-count decoder), 4 KB, median µs

| code | m' | cm256 | m×m + ISA-L kernel | this work (avx2) | best of ours vs cm256 |
|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.14 | 0.18 | 0.34 | 0.79× |
| RS(8,4) | 1 | 0.13 | 0.17 | 0.33 | 0.74× |
| RS(8,4) | 2 | 0.43 | 0.24 | 0.49 | 1.77× |
| RS(8,4) | 4 | 0.93 | 0.50 | 0.79 | 1.88× |
| RS(16,8) | 1 | 0.36 | 0.29 | 0.65 | 1.22× |
| RS(16,8) | 2 | 0.89 | 0.48 | 0.85 | 1.86× |
| RS(16,8) | 4 | 1.97 | 0.97 | 1.64 | 2.02× |
| RS(16,8) | 8 | 4.37 | 2.04 | 3.52 | 2.14× |
| RS(17,16) | 1 | 0.57 | 0.62 | 1.20 | 0.93× |
| RS(18,16) | 1 | 0.62 | 0.68 | 1.21 | 0.92× |
| RS(18,16) | 2 | 1.75 | 0.95 | 1.79 | 1.83× |
| RS(20,16) | 1 | 0.63 | 0.67 | 1.33 | 0.94× |
| RS(20,16) | 2 | 1.79 | 0.95 | 1.92 | 1.87× |
| RS(20,16) | 4 | 4.08 | 1.99 | 3.38 | 2.05× |
| RS(32,16) | 1 | 0.89 | 0.68 | 1.24 | 1.30× |
| RS(32,16) | 2 | 1.81 | 0.98 | 1.96 | 1.85× |
| RS(32,16) | 4 | 3.99 | 2.11 | 3.18 | 1.89× |
| RS(32,16) | 16 | 19.49 | 10.20 | 14.87 | 1.91× |
| RS(34,32) | 1 | 1.32 | 1.36 | 2.63 | 0.97× |
| RS(34,32) | 2 | 3.73 | 1.92 | 3.49 | 1.94× |
| RS(36,32) | 1 | 1.40 | 1.38 | 2.61 | 1.01× |
| RS(36,32) | 2 | 3.52 | 1.83 | 3.70 | 1.93× |
| RS(36,32) | 4 | 7.92 | 4.16 | 5.96 | 1.91× |

cm256's first recovery row is all-ones (plain XOR), so a single loss recovered from it needs no multiplications; compare m'≥2 for the decoder itself. cm256 gets its own parity for the same data and loss patterns.

## Packet-size crossover

- RS(8,4) m'=1: vs cm256 0.36–0.61×
- RS(8,4) m'=1: speedup vs ISA-L 1.1–3.9×; vs cached ISA-L 0.45–1.08× (cached ISA-L faster at 256, 512, 1024, 2048, 4096 B)
- RS(8,4) m'=2: vs cm256 0.60–1.16×
- RS(8,4) m'=2: speedup vs ISA-L 1.2–2.6×; vs cached ISA-L 0.35–1.27× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(18,16) m'=1: vs cm256 0.48–0.76×
- RS(18,16) m'=1: speedup vs ISA-L 1.6–61.7×; vs cached ISA-L 0.73–1.31× (cached ISA-L faster at 256, 512, 1024 B)
- RS(18,16) m'=2: vs cm256 0.77–1.22×
- RS(18,16) m'=2: speedup vs ISA-L 1.6–35.0×; vs cached ISA-L 0.57–1.27× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=1: vs cm256 0.51–0.67×
- RS(32,16) m'=1: speedup vs ISA-L 1.7–51.5×; vs cached ISA-L 0.61–1.37× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=2: vs cm256 0.84–1.24×
- RS(32,16) m'=2: speedup vs ISA-L 1.7–35.3×; vs cached ISA-L 0.62–1.34× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=1: vs cm256 0.43–1.14×
- RS(36,32) m'=1: speedup vs ISA-L 2.2–177.6×; vs cached ISA-L 0.71–1.28× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=2: vs cm256 0.79–1.21×
- RS(36,32) m'=2: speedup vs ISA-L 1.8–106.0×; vs cached ISA-L 0.60–1.25× (cached ISA-L faster at 256, 512, 1024 B)

## Cores needed for line rate (4 KB packets)

Decode assumes every codeword lost m' data packets (worst case). Encode runs on every codeword, so it usually sets the core count.

| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |
|---|---|---|---|---|---|---|---|
| RS(18,16) | decode | 1 | cm256 | 589.4 | 2396.1 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | cm256 | 272.2 | 1199.3 @ 7 | 1 | 3 |
| RS(18,16) | decode | 1 | isal_avx2 | 45.0 | 251.4 @ 7 | 3 | 12 |
| RS(18,16) | decode | 2 | isal_avx2 | 40.2 | 211.2 @ 7 | 4 | 14 |
| RS(18,16) | decode | 1 | isal_default | 47.9 | 257.5 @ 7 | 3 | 11 |
| RS(18,16) | decode | 2 | isal_default | 45.2 | 249.4 @ 7 | 3 | 12 |
| RS(18,16) | decode | 1 | ours_avx2 | 374.3 | 1664.0 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | ours_avx2 | 273.0 | 1249.0 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | cm256 | 229.7 | 1195.4 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx2 | 234.0 | 1098.4 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_default | 544.2 | 2285.5 @ 7 | 1 | 2 |
| RS(18,16) | encode | - | ours_avx2 | 294.6 | 1387.0 @ 7 | 1 | 3 |
| RS(32,16) | decode | 1 | cm256 | 573.3 | 2352.9 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | cm256 | 264.4 | 1124.4 @ 7 | 1 | 3 |
| RS(32,16) | decode | 1 | isal_avx2 | 43.8 | 245.0 @ 7 | 3 | 12 |
| RS(32,16) | decode | 2 | isal_avx2 | 40.0 | 219.1 @ 7 | 4 | 13 |
| RS(32,16) | decode | 1 | isal_default | 48.5 | 256.9 @ 7 | 3 | 11 |
| RS(32,16) | decode | 2 | isal_default | 46.5 | 241.9 @ 7 | 3 | 12 |
| RS(32,16) | decode | 1 | ours_avx2 | 374.4 | 1633.3 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | ours_avx2 | 256.1 | 1247.5 @ 7 | 1 | 3 |
| RS(32,16) | encode | - | cm256 | 29.2 | 139.5 @ 7 | 6 | 21 |
| RS(32,16) | encode | - | isal_avx2 | 38.1 | 179.0 @ 7 | 4 | 16 |
| RS(32,16) | encode | - | isal_default | 80.0 | 417.4 @ 7 | 2 | 7 |
| RS(32,16) | encode | - | ours_avx2 | 44.9 | 232.5 @ 7 | 4 | 13 |
| RS(36,32) | decode | 1 | cm256 | 550.6 | 1163.6 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | cm256 | 267.4 | 782.7 @ 7 | 1 | 4 |
| RS(36,32) | decode | 1 | isal_avx2 | 16.8 | 88.8 @ 7 | 8 | 32 |
| RS(36,32) | decode | 2 | isal_avx2 | 16.2 | 84.1 @ 7 | 9 | 34 |
| RS(36,32) | decode | 1 | isal_default | 18.1 | 93.9 @ 7 | 8 | 30 |
| RS(36,32) | decode | 2 | isal_default | 16.8 | 90.0 @ 7 | 8 | 32 |
| RS(36,32) | decode | 1 | ours_avx2 | 333.0 | 1183.6 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx2 | 257.2 | 927.9 @ 7 | 1 | 4 |
| RS(36,32) | encode | - | cm256 | 114.6 | 511.0 @ 7 | 2 | 6 |
| RS(36,32) | encode | - | isal_avx2 | 135.6 | 639.2 @ 7 | 2 | 5 |
| RS(36,32) | encode | - | isal_default | 270.0 | 950.0 @ 7 | 1 | 3 |
| RS(36,32) | encode | - | ours_avx2 | 186.2 | 747.6 @ 7 | 1 | 4 |

Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); beyond the measured range this assumes linear scaling.

