#!/usr/bin/env bash
# run_all.sh — reproduce every ISA-L comparison figure from scratch.
#
#   ./run_all.sh            full run (~20–40 min depending on CPU)
#   QUICK=1 ./run_all.sh    smoke run (~2 min), for checking the pipeline only
#
# Output: results/<hostname>-<date>/ with machine.txt, *.csv, figures/*.png, summary.md
#
# For publishable numbers, before running:
#   - fix the CPU frequency:  sudo cpupower frequency-set -g performance
#   - disable turbo (Intel):  echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
#     (or AMD:                echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost)
#   - close other workloads; plug in the laptop if it is one
# The script records whether these were set in machine.txt; it does not change them.

set -euo pipefail
cd "$(dirname "$0")"

QUICK=${QUICK:-0}
OUT=results/$(hostname -s)-$(date +%Y%m%d-%H%M)
mkdir -p "$OUT/figures"

# ── build + correctness gate ────────────────────────────────────────────────
if ! ldconfig -p 2>/dev/null | grep -q libisal && [ ! -d isal-install ]; then
  echo ">> no system ISA-L found; building v2.31.0 from source"
  make isal
fi
make -s all
echo ">> correctness tests"
./test_simd | tee "$OUT/test_simd.txt"

# ── machine description ─────────────────────────────────────────────────────
{
  echo "date: $(date -Is)"
  echo "host: $(hostname)"
  echo "kernel: $(uname -r)"
  echo "compiler: $(${CXX:-g++} --version | head -1)"
  echo "isal: $(ls isal-install 2>/dev/null >/dev/null && echo 'source build' || dpkg -l libisal-dev 2>/dev/null | awk '/libisal-dev/{print $3}')"
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  echo "intel no_turbo: $(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo n/a)"
  echo "amd boost: $(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo n/a)"
  echo "simd flags: $(grep -o -wE 'avx2|avx512f|avx512bw|gfni|vpclmulqdq' /proc/cpuinfo | sort -u | tr '\n' ' ')"
  echo
  lscpu
} > "$OUT/machine.txt"

# One logical CPU per physical core, skipping core 0 (interrupts / OS noise).
CPUS=$(lscpu -p=CPU,CORE,SOCKET | grep -v '^#' | sort -t, -k3,3n -k2,2n -k1,1n | awk -F, '!seen[$3","$2]++ {print $1}' | tail -n +2 | paste -sd, -)
[ -z "$CPUS" ] && CPUS=0
PIN=$(echo "$CPUS" | cut -d, -f1)
NCORES=$(echo "$CPUS" | tr ',' '\n' | wc -l)
THREADS=$(python3 -c "n=$NCORES; s=[1,2,4,8,12,16,24,32,48,64]; print(','.join(str(x) for x in s if x<=n) + ('' if n in s else f',{n}'))")
echo "pinned latency core: $PIN; throughput cores: $CPUS" | tee -a "$OUT/machine.txt"

if [ "$QUICK" = 1 ]; then ITERS=300; WARM=50; SECS=0.5; else ITERS=10000; WARM=1000; SECS=3; fi

# Network-relevant codes: low overhead (18:16, 36:32) plus the thesis configs.
CONFIGS=5:4,8:4,16:8,18:16,20:16,32:16,36:32

echo ">> [1/3] latency grid (RDMA 4 KB plus 1 KB and 9 KB)"
./bench_isal latency --configs $CONFIGS --ml 1,2,4,max --pkts 1024,4096,9216 \
  --methods all --iters $ITERS --warmup $WARM --pool 64 --cpu "$PIN" --out "$OUT/latency.csv"

echo ">> [2/3] packet-size sweep (crossover)"
SWEEP_PKTS=256,512,1024,2048,4096,8192,9216,16384,32768,65536
[ "$QUICK" = 1 ] && SWEEP_PKTS=256,4096,65536
SWEEP_METHODS=$(./bench_isal latency --configs 5:4 --ml 1 --pkts 64 --methods all --iters 1 --warmup 0 --pool 1 2>/dev/null \
  | awk -F, 'NR>1 && $1=="decode" {print $2}' | grep -E '^(ours|isal|hybrid)_(avx2|avx512|default)$' | sort -u | paste -sd, -)
./bench_isal latency --configs 8:4,18:16,32:16,36:32 --ml 1,2 --pkts $SWEEP_PKTS \
  --methods "$SWEEP_METHODS" --iters $ITERS --warmup $WARM --pool 64 --cpu "$PIN" --out "$OUT/sweep.csv"

echo ">> [3/3] multi-core throughput"
TP_METHODS=$(echo "$SWEEP_METHODS" | tr ',' '\n' | grep -E '^(ours|isal)_' | paste -sd, -)
./bench_isal throughput --configs 18:16,32:16,36:32 --ml 1,2 --pkts 4096 \
  --threads "$THREADS" --cpus "$CPUS" --methods "$TP_METHODS" --seconds $SECS --pool 64 \
  --out "$OUT/throughput.csv"

echo ">> figures"
python3 plot.py "$OUT"
echo ">> done: $OUT"
