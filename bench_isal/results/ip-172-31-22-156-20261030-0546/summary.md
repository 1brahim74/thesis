# ISA-L comparison — summary

CPU: **Intel(R) Xeon(R) Platinum 8488C** · SIMD: `avx2 avx512bw avx512f gfni vpclmulqdq` · governor: `n/a`

Kernel compared: **avx512** (same ISA for both libraries).

## Decode latency, 4 KB packets (median µs, single pinned core)

| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |
|---|---|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 9.5 | 0.99 | 0.53 | 0.45 | 0.39 | 2.5× | 1.14× |
| RS(8,4) | 1 | 9.4 | 1.01 | 0.53 | 0.48 | 0.39 | 2.6× | 1.23× |
| RS(8,4) | 2 | 18.3 | 1.22 | 0.55 | 0.67 | 0.58 | 2.1× | 1.15× |
| RS(8,4) | 4 | 36.4 | 1.85 | 0.68 | 1.17 | 0.94 | 2.0× | 1.25× |
| RS(16,8) | 1 | 19.0 | 3.81 | 2.86 | 0.95 | 0.82 | 4.6× | 1.16× |
| RS(16,8) | 2 | 35.7 | 4.32 | 2.91 | 1.40 | 1.19 | 3.6× | 1.18× |
| RS(16,8) | 4 | 70.3 | 5.52 | 2.97 | 2.55 | 2.00 | 2.8× | 1.28× |
| RS(16,8) | 8 | 142.6 | 7.45 | 2.70 | 4.74 | 3.61 | 2.1× | 1.31× |
| RS(17,16) | 1 | 36.9 | 22.16 | 20.16 | 1.99 | 1.28 | 17.4× | 1.56× |
| RS(18,16) | 1 | 37.0 | 22.31 | 20.39 | 1.93 | 1.27 | 17.6× | 1.52× |
| RS(18,16) | 2 | 70.6 | 23.35 | 20.31 | 3.03 | 1.83 | 12.7× | 1.65× |
| RS(20,16) | 1 | 37.0 | 22.39 | 20.42 | 1.95 | 1.28 | 17.5× | 1.52× |
| RS(20,16) | 2 | 70.5 | 23.27 | 20.29 | 2.96 | 1.82 | 12.8× | 1.63× |
| RS(20,16) | 4 | 138.2 | 25.34 | 20.22 | 5.12 | 3.06 | 8.3× | 1.67× |
| RS(32,16) | 1 | 36.8 | 22.46 | 20.46 | 2.04 | 1.51 | 14.8× | 1.35× |
| RS(32,16) | 2 | 70.3 | 23.24 | 20.24 | 2.98 | 1.90 | 12.2× | 1.57× |
| RS(32,16) | 4 | 138.6 | 25.27 | 20.15 | 5.13 | 3.09 | 8.2× | 1.66× |
| RS(32,16) | 16 | 566.5 | 36.73 | 18.14 | 17.72 | 14.80 | 2.5× | 1.20× |
| RS(34,32) | 1 | 72.9 | 164.46 | 160.55 | 3.88 | 2.47 | 66.6× | 1.57× |
| RS(34,32) | 2 | 139.5 | 162.55 | 156.67 | 5.83 | 3.44 | 47.3× | 1.70× |
| RS(36,32) | 1 | 72.5 | 164.04 | 160.25 | 3.73 | 2.49 | 65.9× | 1.50× |
| RS(36,32) | 2 | 139.1 | 162.50 | 156.37 | 5.90 | 3.44 | 47.3× | 1.72× |
| RS(36,32) | 4 | 275.8 | 163.60 | 154.28 | 8.92 | 5.78 | 28.3× | 1.54× |

*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, so only the region multiply remains. Speedup < 1 there means the cached baseline wins.

## Best available kernel (ISA-L dispatcher; GFNI if the CPU has it), 4 KB, median µs

| code | m' | ISA-L total | ISA-L cached (region only) | m×m + ISA-L kernel | speedup vs ISA-L | vs cached ISA-L |
|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.67 | 0.21 | 0.34 | 2.0× | 0.61× |
| RS(8,4) | 1 | 0.67 | 0.21 | 0.34 | 2.0× | 0.61× |
| RS(8,4) | 2 | 0.71 | 0.24 | 0.34 | 2.1× | 0.72× |
| RS(8,4) | 4 | 0.83 | 0.41 | 0.59 | 1.4× | 0.70× |
| RS(16,8) | 1 | 3.14 | 0.38 | 0.48 | 6.5× | 0.78× |
| RS(16,8) | 2 | 3.28 | 0.51 | 0.68 | 4.8× | 0.75× |
| RS(16,8) | 4 | 3.69 | 0.93 | 1.12 | 3.3× | 0.83× |
| RS(16,8) | 8 | 4.04 | 1.66 | 2.22 | 1.8× | 0.75× |
| RS(17,16) | 1 | 20.74 | 0.78 | 0.84 | 24.8× | 0.93× |
| RS(18,16) | 1 | 20.95 | 0.77 | 0.83 | 25.2× | 0.93× |
| RS(18,16) | 2 | 20.91 | 1.18 | 1.03 | 20.3× | 1.14× |
| RS(20,16) | 1 | 20.96 | 0.79 | 0.84 | 24.8× | 0.93× |
| RS(20,16) | 2 | 21.17 | 1.13 | 1.07 | 19.7× | 1.05× |
| RS(20,16) | 4 | 21.71 | 1.89 | 1.85 | 11.7× | 1.02× |
| RS(32,16) | 1 | 20.95 | 0.77 | 0.95 | 22.0× | 0.81× |
| RS(32,16) | 2 | 21.14 | 1.16 | 1.25 | 16.9× | 0.93× |
| RS(32,16) | 4 | 21.65 | 1.91 | 2.18 | 9.9× | 0.88× |
| RS(32,16) | 16 | 23.40 | 6.44 | 10.47 | 2.2× | 0.62× |
| RS(34,32) | 1 | 162.19 | 1.86 | 1.84 | 88.2× | 1.01× |
| RS(34,32) | 2 | 158.85 | 2.42 | 2.05 | 77.6× | 1.18× |
| RS(36,32) | 1 | 162.21 | 1.84 | 1.79 | 90.8× | 1.03× |
| RS(36,32) | 2 | 158.73 | 2.42 | 2.05 | 77.4× | 1.18× |
| RS(36,32) | 4 | 157.56 | 3.85 | 3.42 | 46.1× | 1.13× |

## vs cm256 (open-source erasure-count decoder), 4 KB, median µs

| code | m' | cm256 | m×m + ISA-L kernel | this work (avx512) | best of ours vs cm256 |
|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.19 | 0.34 | 0.39 | 0.57× |
| RS(8,4) | 1 | 0.20 | 0.34 | 0.39 | 0.58× |
| RS(8,4) | 2 | 0.56 | 0.34 | 0.58 | 1.64× |
| RS(8,4) | 4 | 1.22 | 0.59 | 0.94 | 2.06× |
| RS(16,8) | 1 | 0.43 | 0.48 | 0.82 | 0.89× |
| RS(16,8) | 2 | 1.17 | 0.68 | 1.19 | 1.71× |
| RS(16,8) | 4 | 2.45 | 1.12 | 2.00 | 2.19× |
| RS(16,8) | 8 | 5.24 | 2.22 | 3.61 | 2.36× |
| RS(17,16) | 1 | 0.94 | 0.84 | 1.28 | 1.13× |
| RS(18,16) | 1 | 1.01 | 0.83 | 1.27 | 1.22× |
| RS(18,16) | 2 | 2.12 | 1.03 | 1.83 | 2.05× |
| RS(20,16) | 1 | 0.88 | 0.84 | 1.28 | 1.04× |
| RS(20,16) | 2 | 2.29 | 1.07 | 1.82 | 2.13× |
| RS(20,16) | 4 | 4.99 | 1.85 | 3.06 | 2.70× |
| RS(32,16) | 1 | 1.28 | 0.95 | 1.51 | 1.34× |
| RS(32,16) | 2 | 2.64 | 1.25 | 1.90 | 2.11× |
| RS(32,16) | 4 | 5.02 | 2.18 | 3.09 | 2.31× |
| RS(32,16) | 16 | 22.10 | 10.47 | 14.80 | 2.11× |
| RS(34,32) | 1 | 1.85 | 1.84 | 2.47 | 1.01× |
| RS(34,32) | 2 | 5.29 | 2.05 | 3.44 | 2.58× |
| RS(36,32) | 1 | 2.61 | 1.79 | 2.49 | 1.46× |
| RS(36,32) | 2 | 4.26 | 2.05 | 3.44 | 2.08× |
| RS(36,32) | 4 | 10.80 | 3.42 | 5.78 | 3.16× |

cm256's first recovery row is all-ones (plain XOR), so a single loss recovered from it needs no multiplications; compare m'≥2 for the decoder itself. cm256 gets its own parity for the same data and loss patterns.

## Packet-size crossover

- RS(8,4) m'=1: vs cm256 0.46–0.97×
- RS(8,4) m'=1: speedup vs ISA-L 1.4–4.4×; vs cached ISA-L 0.79–1.33× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(8,4) m'=2: vs cm256 0.62–1.53×
- RS(8,4) m'=2: speedup vs ISA-L 1.5–3.3×; vs cached ISA-L 0.62–1.47× (cached ISA-L faster at 256, 512, 1024 B)
- RS(18,16) m'=1: vs cm256 0.58–1.14×
- RS(18,16) m'=1: speedup vs ISA-L 2.2–81.2×; vs cached ISA-L 0.96–1.61× (cached ISA-L faster at 512 B)
- RS(18,16) m'=2: vs cm256 0.81–1.88×
- RS(18,16) m'=2: speedup vs ISA-L 2.1–51.8×; vs cached ISA-L 0.85–1.78× (cached ISA-L faster at 256 B)
- RS(32,16) m'=1: vs cm256 0.58–1.07×
- RS(32,16) m'=1: speedup vs ISA-L 2.1–80.7×; vs cached ISA-L 1.05–1.61×
- RS(32,16) m'=2: vs cm256 0.81–1.76×
- RS(32,16) m'=2: speedup vs ISA-L 2.1–51.3×; vs cached ISA-L 0.84–1.78× (cached ISA-L faster at 256 B)
- RS(36,32) m'=1: vs cm256 0.69–1.09×
- RS(36,32) m'=1: speedup vs ISA-L 3.4–400.1×; vs cached ISA-L 1.05–1.53×
- RS(36,32) m'=2: vs cm256 0.86–1.92×
- RS(36,32) m'=2: speedup vs ISA-L 2.8–217.4×; vs cached ISA-L 0.96–1.70× (cached ISA-L faster at 256, 512 B)

## Cores needed for line rate (4 KB packets)

Decode assumes every codeword lost m' data packets (worst case). Encode runs on every codeword, so it usually sets the core count.

| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |
|---|---|---|---|---|---|---|---|
| RS(18,16) | decode | 1 | cm256 | 254.2 | 738.3 @ 3 | 1 | 2 |
| RS(18,16) | decode | 2 | cm256 | 158.1 | 463.1 @ 3 | 1 | 3 |
| RS(18,16) | decode | 1 | isal_avx2 | 22.5 | 67.5 @ 3 | 5 | 18 |
| RS(18,16) | decode | 2 | isal_avx2 | 21.4 | 64.8 @ 3 | 5 | 19 |
| RS(18,16) | decode | 1 | isal_avx512 | 23.0 | 69.0 @ 3 | 5 | 18 |
| RS(18,16) | decode | 2 | isal_avx512 | 22.3 | 67.2 @ 3 | 5 | 18 |
| RS(18,16) | decode | 1 | isal_default | 23.6 | 71.1 @ 3 | 5 | 17 |
| RS(18,16) | decode | 2 | isal_default | 23.5 | 70.8 @ 3 | 5 | 17 |
| RS(18,16) | decode | 1 | ours_avx2 | 198.3 | 573.2 @ 3 | 1 | 3 |
| RS(18,16) | decode | 2 | ours_avx2 | 162.7 | 485.1 @ 3 | 1 | 3 |
| RS(18,16) | decode | 1 | ours_avx512 | 210.6 | 599.9 @ 3 | 1 | 3 |
| RS(18,16) | decode | 2 | ours_avx512 | 189.5 | 553.7 @ 3 | 1 | 3 |
| RS(18,16) | encode | - | cm256 | 143.2 | 426.0 @ 3 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx2 | 144.7 | 432.5 @ 3 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx512 | 192.6 | 567.9 @ 3 | 1 | 3 |
| RS(18,16) | encode | - | isal_default | 242.9 | 683.6 @ 3 | 1 | 2 |
| RS(18,16) | encode | - | ours_avx2 | 175.7 | 526.1 @ 3 | 1 | 3 |
| RS(18,16) | encode | - | ours_avx512 | 211.1 | 614.3 @ 3 | 1 | 2 |
| RS(32,16) | decode | 1 | cm256 | 253.8 | 735.9 @ 3 | 1 | 2 |
| RS(32,16) | decode | 2 | cm256 | 155.5 | 454.8 @ 3 | 1 | 3 |
| RS(32,16) | decode | 1 | isal_avx2 | 22.7 | 68.1 @ 3 | 5 | 18 |
| RS(32,16) | decode | 2 | isal_avx2 | 21.2 | 64.4 @ 3 | 5 | 19 |
| RS(32,16) | decode | 1 | isal_avx512 | 23.2 | 69.7 @ 3 | 5 | 18 |
| RS(32,16) | decode | 2 | isal_avx512 | 22.4 | 66.8 @ 3 | 5 | 18 |
| RS(32,16) | decode | 1 | isal_default | 23.9 | 71.4 @ 3 | 5 | 17 |
| RS(32,16) | decode | 2 | isal_default | 23.7 | 71.0 @ 3 | 5 | 17 |
| RS(32,16) | decode | 1 | ours_avx2 | 195.5 | 564.5 @ 3 | 1 | 3 |
| RS(32,16) | decode | 2 | ours_avx2 | 157.5 | 467.0 @ 3 | 1 | 3 |
| RS(32,16) | decode | 1 | ours_avx512 | 208.6 | 594.0 @ 3 | 1 | 3 |
| RS(32,16) | decode | 2 | ours_avx512 | 181.6 | 529.9 @ 3 | 1 | 3 |
| RS(32,16) | encode | - | cm256 | 20.8 | 62.3 @ 3 | 5 | 20 |
| RS(32,16) | encode | - | isal_avx2 | 25.6 | 76.8 @ 3 | 4 | 16 |
| RS(32,16) | encode | - | isal_avx512 | 29.1 | 87.5 @ 3 | 4 | 14 |
| RS(32,16) | encode | - | isal_default | 87.6 | 262.8 @ 3 | 2 | 5 |
| RS(32,16) | encode | - | ours_avx2 | 31.2 | 93.5 @ 3 | 4 | 13 |
| RS(32,16) | encode | - | ours_avx512 | 50.3 | 150.1 @ 3 | 2 | 8 |
| RS(36,32) | decode | 1 | cm256 | 255.6 | 744.2 @ 3 | 1 | 2 |
| RS(36,32) | decode | 2 | cm256 | 156.6 | 456.2 @ 3 | 1 | 3 |
| RS(36,32) | decode | 1 | isal_avx2 | 6.3 | 19.0 @ 3 | 16 | 64 |
| RS(36,32) | decode | 2 | isal_avx2 | 6.3 | 18.8 @ 3 | 16 | 64 |
| RS(36,32) | decode | 1 | isal_avx512 | 6.3 | 19.0 @ 3 | 16 | 64 |
| RS(36,32) | decode | 2 | isal_avx512 | 6.4 | 19.0 @ 3 | 16 | 64 |
| RS(36,32) | decode | 1 | isal_default | 6.4 | 19.0 @ 3 | 16 | 64 |
| RS(36,32) | decode | 2 | isal_default | 6.5 | 19.3 @ 3 | 16 | 63 |
| RS(36,32) | decode | 1 | ours_avx2 | 191.1 | 560.6 @ 3 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx2 | 153.6 | 456.7 @ 3 | 1 | 3 |
| RS(36,32) | decode | 1 | ours_avx512 | 207.0 | 598.5 @ 3 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx512 | 176.4 | 517.9 @ 3 | 1 | 3 |
| RS(36,32) | encode | - | cm256 | 73.4 | 237.2 @ 3 | 2 | 6 |
| RS(36,32) | encode | - | isal_avx2 | 91.5 | 276.5 @ 3 | 2 | 5 |
| RS(36,32) | encode | - | isal_avx512 | 110.0 | 330.3 @ 3 | 1 | 4 |
| RS(36,32) | encode | - | isal_default | 202.0 | 593.0 @ 3 | 1 | 3 |
| RS(36,32) | encode | - | ours_avx2 | 116.3 | 346.4 @ 3 | 1 | 4 |
| RS(36,32) | encode | - | ours_avx512 | 158.9 | 460.1 @ 3 | 1 | 3 |

Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); beyond the measured range this assumes linear scaling.

