#!/usr/bin/env bash
# run_sweeps.sh — every fecsim figure from scratch.
#
#   ./run_sweeps.sh [DECODE_TABLE]       default: newest bench_isal/results/*/latency.csv
#   QUICK=1 ./run_sweeps.sh              fewer trials (pipeline check)
#   JOBS=16 ./run_sweeps.sh              parallel jobs (default: nproc)
#
# Output: results/<date>/ with sweep CSVs, figures/*.png and summary.md

set -euo pipefail
cd "$(dirname "$0")"
make -s fecsim

TABLE=${1:-$(ls -t ../bench_isal/results/*/latency.csv 2>/dev/null | head -1)}
[ -f "$TABLE" ] || { echo "no decode table found; run bench_isal first or pass a latency.csv"; exit 1; }
OUT=results/$(date +%Y%m%d-%H%M)
mkdir -p "$OUT/parts" "$OUT/figures"
cp "$TABLE" "$OUT/decode_table.csv"
JOBS=${JOBS:-$(nproc)}
if [ "${QUICK:-0}" = 1 ]; then TR=400; TRC=100; else TR=5000; TRC=1000; fi

LOSS=1e-6,1e-5,1e-4,1e-3,3e-3,1e-2
SIZES=128K,512K,2M,8M
RTTS=2,5,10,25
# Use every candidate code that the decode table has measurements for.
WANT=17:16,18:16,20:16,34:32,36:32
CODES=$(python3 - "$TABLE" "$WANT" <<'PY'
import sys, pandas as pd
t = pd.read_csv(sys.argv[1]); t = t[(t.op == "decode") & (t.pkt == 4096)]
have = {f"{n}:{k}" for n, k in zip(t.n, t.k)}
print(",".join(c for c in sys.argv[2].split(",") if c in have))
PY
)
echo ">> FEC codes with measured decode cost: $CODES"
DECODES="isal_default cm256 hybrid_default zero"

jobs_file=$OUT/jobs.txt; : > "$jobs_file"
add() { echo "./fecsim $* --decode-table $OUT/decode_table.csv" >> "$jobs_file"; }
i=0
for rtt in ${RTTS//,/ }; do
  for sz in ${SIZES//,/ }; do
    # A: single flow, bernoulli loss
    add --protos gbn,sr,srtlp --sizes $sz --rtts $rtt --loss $LOSS --trials $TR --out $OUT/parts/a_rt_${rtt}_${sz}.csv
    for d in $DECODES; do
      add --protos fec --codes $CODES --decode $d --sizes $sz --rtts $rtt --loss $LOSS --trials $TR --out $OUT/parts/a_fec_${d}_${rtt}_${sz}.csv
    done
    # B: single flow, bursty dc3 loss (sweep burst frequency)
    add --protos gbn,sr,srtlp --channel dc3 --dc3-nd 1e-5,1e-4,1e-3,1e-2 --sizes $sz --rtts $rtt --trials $TR --out $OUT/parts/b_rt_${rtt}_${sz}.csv
    for d in $DECODES; do
      add --protos fec --codes $CODES --decode $d --channel dc3 --dc3-nd 1e-5,1e-4,1e-3,1e-2 --sizes $sz --rtts $rtt --trials $TR --out $OUT/parts/b_fec_${d}_${rtt}_${sz}.csv
    done
  done
  # C: collective step = slowest of 64 flows, small messages
  add --protos gbn,sr,srtlp --sizes 128K,512K --rtts $rtt --loss $LOSS --flows 64 --trials $TRC --out $OUT/parts/c_rt_${rtt}.csv
  for d in $DECODES; do
    add --protos fec --codes $CODES --decode $d --sizes 128K,512K --rtts $rtt --loss $LOSS --flows 64 --trials $TRC --out $OUT/parts/c_fec_${d}_${rtt}.csv
  done
done
# D: RTO sensitivity (commodity RoCE timeouts are often far above IRN's 100 µs)
for rto in 100 1000; do
  add --protos gbn,sr,srtlp --sizes 128K,2M --rtts 5 --loss $LOSS --rto $rto --trials $TR --out $OUT/parts/d_rt_${rto}.csv
  add --protos fec --codes $CODES --decode hybrid_default --sizes 128K,2M --rtts 5 --loss $LOSS --rto $rto --trials $TR --out $OUT/parts/d_fec_${rto}.csv
done

echo ">> $(wc -l < "$jobs_file") jobs on $JOBS cores"
xargs -P "$JOBS" -I{} sh -c '{} 2>>'"$OUT"'/errors.log' < "$jobs_file"
[ -s "$OUT/errors.log" ] && { echo "errors:"; cat "$OUT/errors.log"; exit 1; }

python3 - "$OUT" <<'EOF'
import glob, os, sys, pandas as pd
d = sys.argv[1]
for prefix, name in (("a", "bernoulli"), ("b", "dc3"), ("c", "collective"), ("d", "rto")):
    fs = sorted(glob.glob(f"{d}/parts/{prefix}_*.csv"))
    if fs:
        pd.concat([pd.read_csv(f) for f in fs]).to_csv(f"{d}/{name}.csv", index=False)
EOF
python3 plot_sim.py "$OUT"
echo ">> done: $OUT"
