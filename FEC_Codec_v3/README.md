# FEC Codec v3 — Reed-Solomon Erasure Coding for AI Data Center Networks

**Ibrahim Mammadov** — ZJU-UIUC Institute, 2022–2026  
**Supervisor:** 金浩 (Jin Hao)  
**Related paper:** ["On Invertibility of Large Binary Matrices"](https://doi.org/10.3390/math14020270) — Mammadov, Loskot, Honold. *Mathematics* 2026, 14, 270.

---

## Overview

This repository contains the C++17 implementation of an optimized Reed-Solomon erasure codec designed for high-speed AI data center networks (GPU clusters, RDMA fabrics, AllReduce traffic).

### Key Contributions

| # | Contribution | Complexity |
|---|---|---|
| 1 | **O(m³) erasure-count decoder** — inverts only the m×m sub-problem for lost packets, not the full k×k matrix | O(m³) vs O(k³) |
| 2 | **LRU decode cache** — amortizes repeated burst patterns to O(1) | O(1) amortized |
| 3 | **Three-state DC channel model** — NORMAL/DEGRADED/INCAST, models PFC cascade dynamics | — |
| 4 | **Region-multiply encoding** — per-coefficient 256-byte LUT, ~3–5× faster encoding | O(m·k·L) |

### Benchmark Results (P99 decode latency, 1500-byte packets)

| Config | v1 (baseline) | v3 (this work) | vs TCP RTO |
|--------|--------------|----------------|------------|
| RS(5,4) | 111 µs | 3.5 µs | **2841×** faster |
| RS(8,4) | 113 µs | 9.1 µs | **1101×** faster |
| RS(16,8) | 202 µs | 19.7 µs | **508×** faster |
| RS(32,16) | 719 µs | 55 µs | **182×** faster |

---

## File Structure

```
FEC_Codec_v3/
├── gf256.hpp               # GF(2^8) arithmetic — branchless mul, region LUTs
├── gf256_matrix.hpp        # Matrix ops, O(m³) GJE inversion, LRU cache
├── rs_codec.hpp            # Reed-Solomon encoder/decoder (main codec)
├── channel.hpp             # Channel models: GE 2-state + DC 3-state + Incast
├── main.cpp                # Benchmark driver, CSV output
├── test_suite.cpp          # Test suite 1 — 86 tests (core correctness)
├── test_suite_extended.cpp # Test suite 2 — 77 tests (edge cases, stress)
├── test_suite_pressure.cpp # Test suite 3 — 89 tests (exhaustive, pressure)
├── visualize_v3.py         # Python script to generate thesis graphs
└── results_v3.csv          # Benchmark results
```

---

## Build & Run

**Requirements:** GCC 9+ or Clang 10+, C++17, Python 3 + matplotlib (for graphs)

```bash
# Benchmark
g++ -std=c++17 -O3 -march=native -o fec_bench_v3 main.cpp
./fec_bench_v3          # full benchmark (~2 min)
./fec_bench_v3 quick    # quick sweep

# Test suite (252 tests total, all must pass)
g++ -std=c++17 -O2 -o test_suite      test_suite.cpp
g++ -std=c++17 -O2 -o test_suite_ext  test_suite_extended.cpp
g++ -std=c++17 -O2 -o test_pressure   test_suite_pressure.cpp

./test_suite && ./test_suite_ext && ./test_pressure

# Graphs
python3 visualize_v3.py
```

---

## Test Coverage (252 tests, 0 failures)

| Suite | Tests | Categories |
|-------|-------|------------|
| `test_suite.cpp` | 86 | GF arithmetic, encoder, exhaustive erasure patterns, stress, edge cases, cache, invariants, channel, mixed |
| `test_suite_extended.cpp` | 77 | Matrix algebra, partial erasure, determinism, boundaries, fault injection, channel extended, asymmetric erasures, code properties, performance floor, hardened stress |
| `test_suite_pressure.cpp` | 89 | GF exhaustive (all 256×256), region_mul_set, exceptions, matrix edge cases, cache internals, API wrappers, decoder edge cases, channel accessors, IncastChannel boundaries, DC3 boundary transitions, special data values, 50k long-haul, field axioms, regression |

---

## Algorithm: O(m³) Decoder

Standard RS decoders (ISA-L, Jerasure) invert a k×k matrix → **O(k³)**.  
This decoder exploits the systematic code structure:

```
Only invert the m×m submatrix for LOST packets:
  G_sub[r][c] = G[parity_r][lost_data[c]]   (m×m Cauchy submatrix)

Cost: O(m³)  where m = number of erasures << k
```

Guaranteed invertible by the Cauchy matrix property (proved in the companion paper).

**Speedup example** — RS(32,16), m_actual=2:
- Standard: O(k³) = O(16³) = 4096 field ops
- This work: O(m³) = O(2³) = 8 field ops → **512× reduction**

---

## Three-State DC Channel Model

Replaces the standard two-state Gilbert-Elliott model with a model calibrated to PFC cascade dynamics in AI data center networks:

```
NORMAL ──p_nd──▶ DEGRADED ──p_di──▶ INCAST
  ▲                  │                  │
  └──────────────────p_dn               p_id
                     ▲                  │
                     └──────────────────┘

Loss probs:  p_n=0.001,  p_d=0.03,  p_i=0.35 (defaults)
Stationary:  π ≈ [0.88,  0.10,     0.02]
```

---

## Cache Bug Fix (Important)

A silent data corruption bug was discovered and fixed during development.

**Root cause:** The LRU cache key used only `lost_data` indices. But the G_sub inverse depends on *which parity rows* were used. Two calls with the same lost data but different available parity rows would collide, applying the wrong inverse → wrong output with no exception.

**Fix:** Cache key = `lost_data indices` + sentinel `-1` + `par_row_global indices`.  
A hit only occurs when the exact same subproblem repeats.

**Impact:** Was causing ~50% failure rate when m_actual < m. Fixed in `rs_codec.hpp`.

---

## Citation

If you use this code, please cite:

```bibtex
@article{mammadov2026invertibility,
  title   = {On Invertibility of Large Binary Matrices},
  author  = {Mammadov, Ibrahim and Loskot, Pavel and Honold, Thomas},
  journal = {Mathematics},
  volume  = {14},
  number  = {2},
  pages   = {270},
  year    = {2026},
  doi     = {10.3390/math14020270}
}
```

---

## License

MIT License — see LICENSE file.
