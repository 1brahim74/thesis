# bench_isal — m×m decoder vs Intel ISA-L

This benchmarks the thesis decoder (`FEC_Codec_v3`, unchanged) against Intel ISA-L v2.31 on the same machine. Both run the same code, on the same data, with the same SIMD width.

```bash
cd bench_isal
make test              # 7k+ correctness checks; must pass
./run_all.sh           # full run, ~20–40 min → results/<host>-<date>/
QUICK=1 ./run_all.sh   # 2-min pipeline check
```

Requirements: GCC ≥ 9 on x86-64 Linux with AVX2, git (the Makefile fetches cm256), `libisal-dev` (or `make isal` to build ISA-L from source; needs nasm/autotools), and Python 3 with pandas and matplotlib.

## What is compared

| method | inversion | region kernel |
|---|---|---|
| `v3_scalar` | m×m (thesis code as submitted) | scalar 256-byte LUT |
| `isal_<k>` | **k×k**, `gf_invert_matrix`, exactly as in ISA-L's `ec_simple_example.c` | ISA-L asm |
| `hybrid_<k>` | **m×m** (thesis algorithm) | ISA-L asm |
| `ours_<isa>` | **m×m** (thesis algorithm) | `gf256_simd.hpp` split-nibble `vpshufb` |
| `cm256` | **N×N, N = lost** (open-source erasure-count decoder, O(N²) LDU), catid/cm256 pinned at `634d1e3` | cm256's gf256 AVX2 |

`<k>` is the kernel: `avx2` or `avx512` pins both libraries to the same ISA, and `default` is ISA-L's own dispatcher, which picks GFNI on CPUs that have it.

The **isal → hybrid** comparison changes only the inversion, so it isolates the thesis contribution. The **hybrid → ours** comparison changes only the kernel.

Fairness measures:
- **Same codewords.** ISA-L gets the v3 Cauchy generator as its encode matrix, and the parity bytes are verified identical.
- **Same work shape.** The decoder folds `G_sub⁻¹` and the known-data correction into one m′×k decode matrix, so packet work is m′·k·L byte multiplies for both libraries.
- **Same survivors, same random loss patterns.**
- **Every decode is verified** byte for byte, outside the timed region.
- **Controlled cache state.** `--data warm|cold` sets where the packets are before each timed phase:
  - `warm` (default) models a NIC writing into LLC via DDIO.
  - `cold` flushes the packets to DRAM.

  Without this control, a method with a slow matrix phase sees its data evicted between pool reuses and is unfairly charged region time.

Reported per decode:
- **matrix**: find lost set, invert, build the decode matrix, expand tables
- **region**: the packet work
- **ISA-L region alone**: the cost of ISA-L with a cached decode matrix

## Output (`results/<host>-<date>/`)

- `latency.csv`: every code × m′ × {1, 4, 9} KB × method, single pinned core, median / p99 / mean.
- `sweep.csv`: packet sizes 256 B to 64 KB, used for the crossover figure.
- `throughput.csv`: encode and decode Gbps on 1..N pinned physical cores.
- `summary.md`: tables, plus "cores for 100/400 Gbps".
- `figures/`:
  - `fig1`: matrix vs region breakdown
  - `fig2`: crossover vs packet size
  - `fig3`: time vs m′
  - `fig4`: multi-core scaling
- `machine.txt`: CPU, flags, governor, turbo state, compiler, ISA-L version.

For publishable numbers, set the `performance` governor and disable turbo before running (commands are at the top of `run_all.sh`).

## What the first measurements show

These are from a 2-vCPU cloud VM (Xeon, AVX-512, no GFNI), quick mode, and are indicative only. Re-run on a quiet machine before quoting anything.

1. **ISA-L's cold decode is dominated by the k×k inversion, not the region multiply, at RDMA packet sizes.**
   - RS(32,16), 4 KB, m′=2: ISA-L ≈ 24 µs total, of which ≈ 21 µs is inversion. This work ≈ 3–4 µs.
   - RS(36,32), 4 KB, m′=1: ISA-L ≈ 170 µs, of which ≈ 167 µs is inversion. This work ≈ 4–5 µs.
   - The gap grows with k, and low-overhead network codes (small m, large k) are the regime that matters.
2. **Against ISA-L with a cached decode matrix, the advantage disappears.** This work is within about 0.7–1.1× of cached ISA-L, and the cached baseline wins at tiny packets. The honest claim: m×m inversion makes a *cache miss* cost about the same as a cache hit, which matters when loss patterns rarely repeat. RS(36,32) with m′ ≤ 4 has over 40,000 lost-data patterns.
3. **The SIMD kernel matches ISA-L's AVX2/AVX-512 assembly to within about 1.0–1.5×, but does not beat it.** For the paper, the strongest configuration is `hybrid` (m×m inversion on ISA-L's kernel). It shows the contribution is portable.
4. **cm256 (open-source erasure-count decoder) is the baseline that matters.** It wins when one packet is lost (0.4–0.9×), because its first parity row is all-ones, so it recovers with plain XOR and no multiplications. It also wins at small packets, where its fixed cost is lower. With m′ ≥ 2 this work is 1.1–1.5× faster than cm256 at 4 KB (AVX-512 VM). cm256 is measured on its own parity for the same data and loss patterns, total time only (it exposes no matrix/region split), and it decodes in place, so its parity is copied to scratch outside the timed region.
5. **The encoder, not the decoder, sets the core count for line rate.** It runs on every codeword, and its cost is m·k·L regardless of loss.

## Correction for the thesis text

Two places in the thesis give ISA-L's decoder cost as O(k³ + k²·L): Table 2.1 (ISA-L and Jerasure rows) and the section "The Standard Decoder", which also says the work "is independent of how many packets were actually lost". Both are wrong for ISA-L. ISA-L only computes the lost rows (`ec_encode_data(len, k, nerrs, …)`, see `examples/ec/ec_simple_example.c`), so its region work is **nerrs·k·L**, the same as v3. ISA-L's actual cost is O(k³ + m′·k·L). The difference from v3 is entirely the k³ term. The sentence "Compared to the standard O(k³ + k² · L), the inversion step is (k/m)³…" should compare inversion costs only.

## Files

| file | purpose |
|---|---|
| `gf256_simd.hpp` | split-nibble AVX2 / AVX-512BW fused dot product, runtime dispatch, ISA-L table layout |
| `rs_fast.hpp` | v3 m×m decoder (same GJE on flat arrays) + fused decode matrix |
| `isal_ref.hpp` | ISA-L baseline exactly as in `examples/ec/ec_simple_example.c` |
| `test_simd.cpp` | kernels vs scalar (tails, guard bytes), `invert_small` vs v3 `gf256_invert`, encoder agreement v3/ours/ISA-L, all-subset decoding |
| `bench_isal.cpp` | latency + throughput harness |
| `run_all.sh`, `plot.py` | one-command reproduction |
