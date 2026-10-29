# ISA-L comparison — summary

CPU: **Intel(R) Core(TM) i9-14900HX** · SIMD: `avx2 gfni vpclmulqdq` · governor: `n/a`

Kernel compared: **avx2** (same ISA for both libraries).

## Decode latency, 4 KB packets (median µs, single pinned core)

| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |
|---|---|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 6.1 | 0.62 | 0.28 | 0.33 | 0.30 | 2.0× | 1.10× |
| RS(8,4) | 1 | 6.0 | 0.64 | 0.31 | 0.33 | 0.33 | 1.9× | 0.98× |
| RS(8,4) | 2 | 12.1 | 0.87 | 0.33 | 0.54 | 0.49 | 1.8× | 1.09× |
| RS(8,4) | 4 | 25.0 | 1.14 | 0.29 | 0.84 | 0.90 | 1.3× | 0.94× |
| RS(16,8) | 1 | 12.1 | 2.44 | 1.81 | 0.63 | 0.59 | 4.1× | 1.06× |
| RS(16,8) | 2 | 23.0 | 2.85 | 1.86 | 0.99 | 0.85 | 3.3× | 1.16× |
| RS(16,8) | 4 | 50.4 | 3.63 | 1.98 | 1.65 | 1.59 | 2.3× | 1.04× |
| RS(16,8) | 8 | 90.8 | 5.26 | 1.89 | 3.32 | 3.22 | 1.6× | 1.03× |
| RS(18,16) | 1 | 23.7 | 11.00 | 9.54 | 1.46 | 1.22 | 9.0× | 1.19× |
| RS(18,16) | 2 | 45.3 | 12.60 | 10.29 | 2.30 | 1.76 | 7.2× | 1.31× |
| RS(20,16) | 1 | 23.6 | 11.19 | 9.72 | 1.46 | 1.22 | 9.2× | 1.20× |
| RS(20,16) | 2 | 45.5 | 12.57 | 10.29 | 2.25 | 1.76 | 7.1× | 1.28× |
| RS(20,16) | 4 | 91.2 | 14.74 | 11.14 | 3.60 | 3.36 | 4.4× | 1.07× |
| RS(32,16) | 1 | 23.4 | 10.88 | 9.48 | 1.38 | 1.20 | 9.1× | 1.15× |
| RS(32,16) | 2 | 44.7 | 12.54 | 10.25 | 2.30 | 1.75 | 7.1× | 1.31× |
| RS(32,16) | 4 | 100.1 | 14.84 | 11.26 | 3.57 | 3.35 | 4.4× | 1.07× |
| RS(32,16) | 16 | 378.0 | 26.66 | 12.90 | 13.74 | 14.35 | 1.9× | 0.96× |
| RS(36,32) | 1 | 46.0 | 62.11 | 58.91 | 3.18 | 2.38 | 26.1× | 1.33× |
| RS(36,32) | 2 | 90.5 | 62.41 | 57.86 | 4.43 | 3.59 | 17.4× | 1.23× |
| RS(36,32) | 4 | 178.9 | 67.08 | 59.91 | 7.14 | 5.95 | 11.3× | 1.20× |

*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, so only the region multiply remains. Speedup < 1 there means the cached baseline wins.

## Best available kernel (ISA-L dispatcher; GFNI if the CPU has it), 4 KB, median µs

| code | m' | ISA-L total | ISA-L cached (region only) | m×m + ISA-L kernel | speedup vs ISA-L | vs cached ISA-L |
|---|---|---|---|---|---|---|
| RS(5,4) | 1 | 0.39 | 0.12 | 0.22 | 1.8× | 0.54× |
| RS(8,4) | 1 | 0.39 | 0.12 | 0.17 | 2.3× | 0.70× |
| RS(8,4) | 2 | 0.50 | 0.19 | 0.25 | 2.0× | 0.76× |
| RS(8,4) | 4 | 0.63 | 0.37 | 0.48 | 1.3× | 0.77× |
| RS(16,8) | 1 | 2.01 | 0.21 | 0.28 | 7.3× | 0.77× |
| RS(16,8) | 2 | 2.18 | 0.34 | 0.49 | 4.5× | 0.71× |
| RS(16,8) | 4 | 2.81 | 0.72 | 0.83 | 3.4× | 0.87× |
| RS(16,8) | 8 | 3.20 | 1.46 | 1.88 | 1.7× | 0.78× |
| RS(18,16) | 1 | 10.04 | 0.55 | 0.67 | 14.9× | 0.82× |
| RS(18,16) | 2 | 10.64 | 0.80 | 0.88 | 12.0× | 0.90× |
| RS(20,16) | 1 | 10.10 | 0.55 | 0.69 | 14.6× | 0.79× |
| RS(20,16) | 2 | 10.77 | 0.81 | 0.97 | 11.1× | 0.83× |
| RS(20,16) | 4 | 12.55 | 1.70 | 1.99 | 6.3× | 0.85× |
| RS(32,16) | 1 | 9.89 | 0.54 | 0.61 | 16.1× | 0.88× |
| RS(32,16) | 2 | 11.01 | 0.84 | 1.08 | 10.2× | 0.78× |
| RS(32,16) | 4 | 12.61 | 1.68 | 2.10 | 6.0× | 0.80× |
| RS(32,16) | 16 | 20.90 | 7.25 | 10.05 | 2.1× | 0.72× |
| RS(36,32) | 1 | 58.65 | 1.25 | 1.30 | 45.2× | 0.96× |
| RS(36,32) | 2 | 59.67 | 1.69 | 1.86 | 32.2× | 0.91× |
| RS(36,32) | 4 | 62.70 | 3.48 | 3.94 | 15.9× | 0.88× |

## Packet-size crossover

- RS(8,4) m'=1: speedup vs ISA-L 1.3–3.5×; vs cached ISA-L 0.40–1.25× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(8,4) m'=2: speedup vs ISA-L 1.1–2.7×; vs cached ISA-L 0.37–1.25× (cached ISA-L faster at 256, 512, 1024, 2048 B)
- RS(18,16) m'=1: speedup vs ISA-L 1.6–54.6×; vs cached ISA-L 0.65–1.29× (cached ISA-L faster at 256, 1024 B)
- RS(18,16) m'=2: speedup vs ISA-L 1.6–37.9×; vs cached ISA-L 0.65–1.28× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=1: speedup vs ISA-L 1.8–57.2×; vs cached ISA-L 0.66–1.25× (cached ISA-L faster at 256, 512, 1024 B)
- RS(32,16) m'=2: speedup vs ISA-L 1.8–35.8×; vs cached ISA-L 0.62–1.41× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=1: speedup vs ISA-L 2.3–181.1×; vs cached ISA-L 0.69–1.32× (cached ISA-L faster at 256, 512, 1024 B)
- RS(36,32) m'=2: speedup vs ISA-L 2.2–120.6×; vs cached ISA-L 0.69–1.40× (cached ISA-L faster at 256, 512, 1024 B)

## Cores needed for line rate (4 KB packets)

Decode assumes every codeword lost m' data packets (worst case). Encode runs on every codeword, so it usually sets the core count.

| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |
|---|---|---|---|---|---|---|---|
| RS(18,16) | decode | 1 | isal_avx2 | 44.8 | 256.4 @ 7 | 3 | 11 |
| RS(18,16) | decode | 2 | isal_avx2 | 40.0 | 225.1 @ 7 | 4 | 13 |
| RS(18,16) | decode | 1 | isal_default | 47.4 | 255.7 @ 7 | 3 | 11 |
| RS(18,16) | decode | 2 | isal_default | 49.2 | 247.8 @ 7 | 3 | 12 |
| RS(18,16) | decode | 1 | ours_avx2 | 369.4 | 1692.6 @ 7 | 1 | 2 |
| RS(18,16) | decode | 2 | ours_avx2 | 262.1 | 1269.9 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_avx2 | 220.9 | 1055.8 @ 7 | 1 | 3 |
| RS(18,16) | encode | - | isal_default | 532.2 | 2327.3 @ 7 | 1 | 2 |
| RS(18,16) | encode | - | ours_avx2 | 278.3 | 1450.9 @ 7 | 1 | 2 |
| RS(32,16) | decode | 1 | isal_avx2 | 43.9 | 237.6 @ 7 | 3 | 12 |
| RS(32,16) | decode | 2 | isal_avx2 | 38.2 | 227.2 @ 7 | 4 | 13 |
| RS(32,16) | decode | 1 | isal_default | 47.0 | 269.3 @ 7 | 3 | 11 |
| RS(32,16) | decode | 2 | isal_default | 44.9 | 240.6 @ 7 | 3 | 12 |
| RS(32,16) | decode | 1 | ours_avx2 | 359.2 | 1747.8 @ 7 | 1 | 2 |
| RS(32,16) | decode | 2 | ours_avx2 | 249.1 | 1312.2 @ 7 | 1 | 3 |
| RS(32,16) | encode | - | isal_avx2 | 35.4 | 185.7 @ 7 | 4 | 16 |
| RS(32,16) | encode | - | isal_default | 78.3 | 375.2 @ 7 | 2 | 8 |
| RS(32,16) | encode | - | ours_avx2 | 49.2 | 223.4 @ 7 | 4 | 13 |
| RS(36,32) | decode | 1 | isal_avx2 | 16.8 | 88.1 @ 7 | 8 | 32 |
| RS(36,32) | decode | 2 | isal_avx2 | 16.1 | 85.3 @ 7 | 9 | 33 |
| RS(36,32) | decode | 1 | isal_default | 18.1 | 88.8 @ 7 | 8 | 32 |
| RS(36,32) | decode | 2 | isal_default | 16.9 | 90.8 @ 7 | 8 | 31 |
| RS(36,32) | decode | 1 | ours_avx2 | 322.6 | 1163.2 @ 7 | 1 | 3 |
| RS(36,32) | decode | 2 | ours_avx2 | 246.8 | 921.8 @ 7 | 1 | 4 |
| RS(36,32) | encode | - | isal_avx2 | 137.1 | 607.6 @ 7 | 2 | 5 |
| RS(36,32) | encode | - | isal_default | 254.7 | 1018.1 @ 7 | 1 | 3 |
| RS(36,32) | encode | - | ours_avx2 | 176.5 | 719.2 @ 7 | 1 | 4 |

Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); beyond the measured range this assumes linear scaling.

