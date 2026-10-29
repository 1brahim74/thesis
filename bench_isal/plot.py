#!/usr/bin/env python3
"""plot.py <results_dir> — figures + summary.md from bench_isal CSVs.

Figures (results_dir/figures/):
  fig1_breakdown_4k.png   decode time split into matrix (inversion+tables) and region, 4 KB, m'=1 and 2
  fig2_crossover.png      ISA-L / ours speedup vs packet size; dotted = ISA-L with a cached decode matrix
  fig3_mprime.png         decode time vs number of lost packets m' (where the m×m advantage closes)
  fig4_scaling.png        multi-core encode/decode throughput, with 100 and 400 Gbps lines
"""
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker
import pandas as pd

D = sys.argv[1] if len(sys.argv) > 1 else "results"
FIG = os.path.join(D, "figures")
os.makedirs(FIG, exist_ok=True)

# Reference categorical palette (fixed order) + recessive ink.
C = {"isal": "#eb6834", "ours": "#2a78d6", "hybrid": "#1baf7a", "v3": "#52514e"}
INK, INK2, GRID, SURF = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
LABEL = {"isal": "ISA-L (k×k inverse)", "ours": "this work (m×m inverse + SIMD)",
         "hybrid": "m×m inverse + ISA-L kernel", "v3": "v3 thesis codec (scalar)"}

plt.rcParams.update({
    "figure.facecolor": SURF, "axes.facecolor": SURF, "savefig.facecolor": SURF,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8, "axes.axisbelow": True,
    "axes.spines.top": False, "axes.spines.right": False, "font.size": 9,
    "axes.titlesize": 10, "axes.titlecolor": INK, "legend.frameon": False, "lines.linewidth": 2,
})


def family(m):
    return m.split("_")[0]


def best_suffix(methods):
    """Compare like with like: AVX-512 kernels if both libraries have them, else AVX2."""
    for s in ("avx512", "avx2"):
        if f"ours_{s}" in methods and f"isal_{s}" in methods:
            return s
    return None


def load(name):
    p = os.path.join(D, name)
    return pd.read_csv(p) if os.path.exists(p) else None


lat, sweep, tput = load("latency.csv"), load("sweep.csv"), load("throughput.csv")
summary = ["# ISA-L comparison — summary", ""]
mtxt = os.path.join(D, "machine.txt")
if os.path.exists(mtxt):
    with open(mtxt) as f:
        head = f.read().splitlines()
    model = next((l.split(":", 1)[1].strip() for l in head if l.startswith("Model name")), "?")
    flags = next((l.split(":", 1)[1].strip() for l in head if l.startswith("simd flags")), "?")
    gov = next((l.split(":", 1)[1].strip() for l in head if l.startswith("governor")), "?")
    summary += [f"CPU: **{model}** · SIMD: `{flags}` · governor: `{gov}`", ""]

# ── fig 1: breakdown at 4 KB ────────────────────────────────────────────────
if lat is not None:
    dec = lat[lat.op == "decode"]
    sfx = best_suffix(set(dec.method))
    summary += [f"Kernel compared: **{sfx}** (same ISA for both libraries).", ""]
    rows = dec[(dec.pkt == 4096) & (dec.m_lost.isin([1, 2]))]
    cfgs = rows[["n", "k"]].drop_duplicates().values.tolist()
    meths = [f"isal_{sfx}", f"hybrid_{sfx}", f"ours_{sfx}"]
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.6), sharey=False)
    for ax, ml in zip(axes, (1, 2)):
        xs, w = range(len(cfgs)), 0.26
        for i, me in enumerate(meths):
            fam = family(me)
            mat, reg = [], []
            for n, k in cfgs:
                r = rows[(rows.n == n) & (rows.k == k) & (rows.m_lost == ml) & (rows.method == me)]
                mat.append(r.matrix_med_ns.iloc[0] / 1e3 if len(r) else 0)
                reg.append(r.region_med_ns.iloc[0] / 1e3 if len(r) else 0)
            pos = [x + (i - 1) * w for x in xs]
            ax.bar(pos, reg, w - 0.03, color=C[fam], label=f"{LABEL[fam]} — region")
            ax.bar(pos, mat, w - 0.03, bottom=reg, color="none", edgecolor=C[fam], hatch="////",
                   linewidth=1, label=f"{LABEL[fam]} — matrix")
        ax.set_xticks(list(xs))
        ax.set_xticklabels([f"RS({n},{k})" for n, k in cfgs], rotation=0)
        ax.set_title(f"m' = {ml} lost data packets, 4 KB packets")
        ax.set_ylabel("median decode time (µs)")
        ax.grid(axis="x", visible=False)
    axes[0].legend(fontsize=7, ncol=1, loc="upper left")
    fig.suptitle("Where decode time goes: matrix phase (hatched) vs region multiply (solid)", color=INK, fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "fig1_breakdown_4k.png"), dpi=200)
    plt.close(fig)

    # summary table at 4 KB
    summary += ["## Decode latency, 4 KB packets (median µs, single pinned core)", "",
                "| code | m' | v3 scalar | ISA-L total | ISA-L matrix | ISA-L region (= cached) | this work | speedup vs ISA-L | speedup vs cached ISA-L |",
                "|---|---|---|---|---|---|---|---|---|"]
    t4 = dec[dec.pkt == 4096]
    for (n, k, ml), g in t4.groupby(["n", "k", "m_lost"]):
        get = lambda me, col: g[g.method == me][col].iloc[0] if (g.method == me).any() else float("nan")
        v3 = get("v3_scalar", "total_med_ns") / 1e3
        it, im, ir = (get(f"isal_{sfx}", c) / 1e3 for c in ("total_med_ns", "matrix_med_ns", "region_med_ns"))
        ot = get(f"ours_{sfx}", "total_med_ns") / 1e3
        summary.append(f"| RS({n},{k}) | {ml} | {v3:.1f} | {it:.2f} | {im:.2f} | {ir:.2f} | {ot:.2f} | "
                       f"{it / ot:.1f}× | {ir / ot:.2f}× |")
    summary += ["", "*Cached ISA-L* = ISA-L whose decode matrix for this exact loss pattern is already cached, "
                "so only the region multiply remains. Speedup < 1 there means the cached baseline wins.", ""]

    # ── fig 3: m' sweep ────────────────────────────────────────────────────
    big = dec[(dec.pkt == 4096)]
    cands = [c for c in [(32, 16), (36, 32), (20, 16)] if ((big.n == c[0]) & (big.k == c[1])).any()]
    if cands:
        fig, axes = plt.subplots(1, len(cands), figsize=(4.2 * len(cands), 3.4), squeeze=False)
        for ax, (n, k) in zip(axes[0], cands):
            g = big[(big.n == n) & (big.k == k)]
            for me in (f"isal_{sfx}", f"hybrid_{sfx}", f"ours_{sfx}"):
                s = g[g.method == me].sort_values("m_lost")
                ax.plot(s.m_lost, s.total_med_ns / 1e3, marker="o", ms=5, color=C[family(me)], label=LABEL[family(me)])
            s = g[g.method == f"isal_{sfx}"].sort_values("m_lost")
            ax.plot(s.m_lost, s.region_med_ns / 1e3, ls=":", color=C["isal"], label="ISA-L, decode matrix cached")
            ax.set_title(f"RS({n},{k}), 4 KB")
            ax.set_xlabel("lost data packets m'")
            ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(integer=True))
            ax.set_ylabel("median decode time (µs)")
        axes[0][0].legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(os.path.join(FIG, "fig3_mprime.png"), dpi=200)
        plt.close(fig)

# ── fig 2: crossover vs packet size ─────────────────────────────────────────
if sweep is not None:
    dec = sweep[sweep.op == "decode"]
    sfx = best_suffix(set(dec.method))
    cfgs = dec[["n", "k"]].drop_duplicates().values.tolist()
    fig, axes = plt.subplots(1, len(cfgs), figsize=(3.3 * len(cfgs), 3.3), sharey=True, squeeze=False)
    summary += ["## Packet-size crossover", ""]
    for ax, (n, k) in zip(axes[0], cfgs):
        g = dec[(dec.n == n) & (dec.k == k)]
        for ml, ls in ((1, "-"), (2, "--")):
            o = g[(g.method == f"ours_{sfx}") & (g.m_lost == ml)].set_index("pkt")
            i = g[(g.method == f"isal_{sfx}") & (g.m_lost == ml)].set_index("pkt")
            if o.empty or i.empty:
                continue
            j = o.join(i, lsuffix="_o", rsuffix="_i", how="inner")
            sp = j.total_med_ns_i / j.total_med_ns_o
            spc = j.region_med_ns_i / j.total_med_ns_o
            ax.plot(j.index, sp, ls=ls, marker="o", ms=4, color=C["ours"], label=f"vs ISA-L, m'={ml}")
            ax.plot(j.index, spc, ls=ls, marker="o", ms=4, color=C["isal"], alpha=0.9, label=f"vs cached ISA-L, m'={ml}")
            below = [p for p, v in spc.items() if v < 1]
            summary.append(f"- RS({n},{k}) m'={ml}: speedup vs ISA-L {sp.min():.1f}–{sp.max():.1f}×; "
                           f"vs cached ISA-L {spc.min():.2f}–{spc.max():.2f}×"
                           + (f" (cached ISA-L faster at {', '.join(str(p) for p in below)} B)" if below else ""))
        ax.axhline(1, color=INK2, lw=1)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_title(f"RS({n},{k})")
        ax.set_xlabel("packet size (bytes)")
        ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(
            lambda v, _: f"{int(v // 1024)}K" if v >= 1024 else str(int(v))))
    axes[0][0].set_ylabel("speedup of this work (×, log)")
    axes[0][0].legend(fontsize=7)
    fig.suptitle("Decode speedup over ISA-L vs packet size (>1 = this work faster)", color=INK, fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "fig2_crossover.png"), dpi=200)
    plt.close(fig)
    summary.append("")

# ── fig 4: multi-core throughput ───────────────────────────────────────────
if tput is not None:
    cfgs = tput[["n", "k"]].drop_duplicates().values.tolist()
    tsfx = best_suffix(set(tput.method))
    fig, axes = plt.subplots(1, len(cfgs), figsize=(3.6 * len(cfgs), 3.4), squeeze=False)
    summary += ["## Cores needed for line rate (4 KB packets)", "",
                "Decode assumes every codeword lost m' data packets (worst case). "
                "Encode runs on every codeword, so it usually sets the core count.", "",
                "| code | op | m' | method | Gbps/core (1 thread) | Gbps at max threads | cores for 100G | cores for 400G |",
                "|---|---|---|---|---|---|---|---|"]
    for ax, (n, k) in zip(axes[0], cfgs):
        g = tput[(tput.n == n) & (tput.k == k)]
        mlmax = g[g.op == "decode"].m_lost.max()
        for (op, me, ml), s in g.groupby(["op", "method", "m_lost"]):
            s = s.sort_values("threads")
            per = s.gbps.iloc[-1] / s.threads.iloc[-1]   # per-core at the largest measured scale
            need = lambda target: math.ceil(target / per) if per > 0 else float("inf")
            summary.append(f"| RS({n},{k}) | {op} | {ml if op == 'decode' else '-'} | {me} | {s.gbps.iloc[0]:.1f} | "
                           f"{s.gbps.iloc[-1]:.1f} @ {s.threads.iloc[-1]} | {need(100)} | {need(400)} |")
            if op == "decode" and ml != mlmax:
                continue
            if tsfx and not me.endswith("_" + tsfx):
                continue   # plot only the matched-ISA pair; the table keeps every kernel
            ax.plot(s.threads, s.gbps, marker="o", ms=5, color=C[family(me)],
                    ls="-" if op == "decode" else "--",
                    label=f"{'ISA-L' if family(me) == 'isal' else 'this work'} {op}" + (f" (m'={ml})" if op == "decode" else ""))
        for lr in (100, 400):
            ax.axhline(lr, color=INK2, lw=1, ls=":")
            ax.text(ax.get_xlim()[0], lr, f" {lr} Gbps", va="bottom", fontsize=7, color=INK2)
        ax.set_title(f"RS({n},{k}), 4 KB")
        ax.set_xlabel("pinned cores")
        ts = sorted(g.threads.unique())
        ax.set_xticks(ts)
        if len(ts) == 1:
            ax.set_xlim(ts[0] - 1, ts[0] + 1)
        ax.set_ylabel("Gbps of application data")
    axes[0][0].legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, "fig4_scaling.png"), dpi=200)
    plt.close(fig)
    summary += ["", "Cores for X Gbps = ceil(X / per-core rate at the largest measured thread count); "
                "beyond the measured range this assumes linear scaling.", ""]

with open(os.path.join(D, "summary.md"), "w") as f:
    f.write("\n".join(summary) + "\n")
print("\n".join(summary))
