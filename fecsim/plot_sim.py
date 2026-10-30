#!/usr/bin/env python3
"""plot_sim.py <results_dir> — figures + summary.md from fecsim sweeps.

fig1_ratio_bernoulli.png  p99(best of SR, SR+TLP) / p99(best FEC) over loss rate × message size; rows = RTT,
                          columns = decode method. >1 (blue) = FEC wins, <1 (orange) = SR wins.
fig2_p99_vs_loss.png      p99 completion vs loss, RTT 5 µs, 128 KB and 2 MB: GBN, SR, FEC per decoder.
fig3_collective.png       slowest of 64 flows (collective step), p99 vs loss, RTT 5 µs.
fig4_ratio_dc3.png        as fig1 for bursty DC3 loss (x = burst frequency), RTT 5 µs.
fig5_rto.png              p99 vs loss for RTO 100 µs vs 1000 µs (SR and FEC), 128 KB.
"""
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
import numpy as np
import pandas as pd

D = sys.argv[1]
FIG = os.path.join(D, "figures")
os.makedirs(FIG, exist_ok=True)

INK, INK2, GRID, SURF = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
C = {"gbn": "#52514e", "sr": "#eb6834", "srtlp": "#6250d6", "isal_default": "#e34948", "cm256": "#eda100",
     "hybrid_default": "#2a78d6", "zero": "#1baf7a"}
DLABEL = {"isal_default": "FEC, ISA-L decode (k×k)", "cm256": "FEC, cm256 decode",
          "hybrid_default": "FEC, m×m + ISA-L kernel", "zero": "FEC, free decode (bound)"}
DECODES = ["isal_default", "cm256", "hybrid_default", "zero"]
plt.rcParams.update({
    "figure.facecolor": SURF, "axes.facecolor": SURF, "savefig.facecolor": SURF,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8, "axes.axisbelow": True,
    "axes.spines.top": False, "axes.spines.right": False, "font.size": 8.5,
    "axes.titlesize": 9.5, "axes.titlecolor": INK, "legend.frameon": False, "lines.linewidth": 2,
})
# diverging: orange (SR wins) — neutral — blue (FEC wins), symmetric in log ratio
DIV = mcolors.LinearSegmentedColormap.from_list("div", ["#c2410c", "#f6c3a5", "#f0efec", "#a9c9f2", "#1c5cab"])


def load(name):
    p = os.path.join(D, name)
    return pd.read_csv(p) if os.path.exists(p) else None


def size_label(b):
    b = int(b)
    return f"{b // (1 << 20)} MB" if b >= 1 << 20 else f"{b // 1024} KB"


def best_fec(df):
    """Best FEC code per (decode, size, rtt, loss, flows, rto) by p99."""
    f = df[df.proto == "fec"]
    idx = f.groupby(["decode", "msg_bytes", "rtt_us", "loss_param", "flows", "rto_us"]).p99_us.idxmin()
    return f.loc[idx]


summary = ["# fecsim — FEC vs retransmission for RDMA messages", ""]
tbl = os.path.join(D, "decode_table.csv")
if os.path.exists(tbl):
    summary += [f"Decode costs: measured medians from `{os.path.basename(tbl)}` (bench_isal, 4 KB packets).", ""]
summary += ["Metric: p99 message completion time at the receiver. Ratio = p99(best of selective repeat, selective repeat + "
            "tail-loss probe) / p99(best FEC code); >1 means FEC finishes the 99th-percentile message sooner.", ""]


def ratio_grid(df, xcol, rtt, decode, flows=1, rto=100):
    sub = df[(df.rtt_us == rtt) & (df.flows == flows) & (df.rto_us == rto)]
    # best retransmission scheme per cell: selective repeat with or without tail-loss probe
    sr = sub[sub.proto.isin(["sr", "srtlp"])].groupby(["msg_bytes", xcol]).p99_us.min()
    bf = best_fec(sub)
    bf = bf[bf.decode == decode].set_index(["msg_bytes", xcol])
    sizes = sorted(sub.msg_bytes.unique()); xs = sorted(sub[xcol].unique())
    R = np.full((len(sizes), len(xs)), np.nan); codes = [[""] * len(xs) for _ in sizes]
    for i, s in enumerate(sizes):
        for j, x in enumerate(xs):
            if (s, x) in sr.index and (s, x) in bf.index:
                R[i, j] = sr[(s, x)] / bf.loc[(s, x)].p99_us
                codes[i][j] = bf.loc[(s, x)].code
    return sizes, xs, R, codes


def heat(ax, sizes, xs, R, codes, xlab, title, show_x=True, show_y=True):
    lim = 2.0
    im = ax.imshow(np.log2(np.clip(R, 1 / 2 ** lim, 2 ** lim)), cmap=DIV, vmin=-lim, vmax=lim, aspect="auto", origin="lower")
    for i in range(len(sizes)):
        for j in range(len(xs)):
            if not np.isnan(R[i, j]):
                ax.text(j, i, f"{R[i, j]:.2f}", ha="center", va="center", fontsize=7, color=INK)
    ax.set_xticks(range(len(xs))); ax.set_xticklabels([f"{x:.0e}".replace("e-0", "e-") for x in xs] if show_x else [], rotation=45)
    ax.set_yticks(range(len(sizes))); ax.set_yticklabels([size_label(s) for s in sizes] if show_y else [])
    ax.set_xlabel(xlab); ax.set_title(title); ax.grid(False)
    return im


# ── fig 1: bernoulli ratio maps ─────────────────────────────────────────────
bern = load("bernoulli.csv")
if bern is not None:
    rtts = sorted(bern.rtt_us.unique())
    fig, axes = plt.subplots(len(rtts), len(DECODES), figsize=(3.3 * len(DECODES), 2.4 * len(rtts)), squeeze=False,
                             constrained_layout=True)
    for r, rtt in enumerate(rtts):
        for c, dec in enumerate(DECODES):
            sizes, xs, R, codes = ratio_grid(bern, "loss_param", rtt, dec)
            im = heat(axes[r][c], sizes, xs, R, codes, "loss rate" if r == len(rtts) - 1 else "",
                      f"{DLABEL[dec]}\nRTT {rtt:g} µs" if r == 0 else f"RTT {rtt:g} µs",
                      show_x=(r == len(rtts) - 1), show_y=(c == 0))
    cb = fig.colorbar(im, ax=axes, shrink=0.6, label="p99(best retransmission) / p99(FEC)   (blue: FEC wins)")
    cb.set_ticks([-2, -1, 0, 1, 2]); cb.set_ticklabels(["¼", "½", "1", "2", "4"])
    fig.savefig(os.path.join(FIG, "fig1_ratio_bernoulli.png"), dpi=170, bbox_inches="tight")
    plt.close(fig)

    summary += ["## Where FEC wins (Bernoulli loss, single flow, RTO 100 µs)", "",
                "Cells (loss × size) where p99(FEC) < p99(best retransmission), out of all cells, per decoder:", "",
                "| RTT (µs) | " + " | ".join(DLABEL[d] for d in DECODES) + " |", "|---|" + "---|" * len(DECODES)]
    for rtt in rtts:
        row = []
        for dec in DECODES:
            _, _, R, _ = ratio_grid(bern, "loss_param", rtt, dec)
            row.append(f"{int(np.nansum(R > 1))}/{int(np.sum(~np.isnan(R)))}")
        summary.append(f"| {rtt:g} | " + " | ".join(row) + " |")
    summary.append("")

    # ── fig 2: p99 vs loss at RTT 5 ─────────────────────────────────────────
    rtt0 = 5 if 5 in rtts else rtts[0]
    pick = [s for s in sorted(bern.msg_bytes.unique()) if s in (131072, 2097152)] or sorted(bern.msg_bytes.unique())[:2]
    fig, axes = plt.subplots(1, len(pick), figsize=(4.6 * len(pick), 3.5), squeeze=False)
    bfa = best_fec(bern)
    for ax, s in zip(axes[0], pick):
        sub = bern[(bern.rtt_us == rtt0) & (bern.msg_bytes == s) & (bern.flows == 1) & (bern.rto_us == 100)]
        for p in ("gbn", "sr", "srtlp"):
            x = sub[sub.proto == p].sort_values("loss_param")
            ax.plot(x.loss_param, x.p99_us, marker="o", ms=4, color=C[p], ls="--" if p == "gbn" else "-",
                    label={"gbn": "go-back-N", "sr": "selective repeat", "srtlp": "SR + tail-loss probe"}[p])
        for dec in DECODES:
            x = bfa[(bfa.decode == dec) & (bfa.rtt_us == rtt0) & (bfa.msg_bytes == s) & (bfa.flows == 1) & (bfa.rto_us == 100)].sort_values("loss_param")
            ax.plot(x.loss_param, x.p99_us, marker="s", ms=4, color=C[dec], label=DLABEL[dec])
        ideal = sub[sub.proto == "sr"].ideal_us.iloc[0]
        ax.axhline(ideal, color=INK2, lw=1, ls=":"); ax.text(ax.get_xlim()[0], ideal, " no-loss time", fontsize=7, color=INK2, va="bottom")
        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlabel("loss rate"); ax.set_ylabel("p99 completion (µs)")
        ax.set_title(f"{size_label(s)} message, RTT {rtt0:g} µs, 100 Gbps")
    axes[0][0].legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig2_p99_vs_loss.png"), dpi=170); plt.close(fig)

    summary += [f"## p99 completion (µs), RTT {rtt0:g} µs, best FEC code per point", ""]
    for s in pick:
        sub = bern[(bern.rtt_us == rtt0) & (bern.msg_bytes == s) & (bern.flows == 1) & (bern.rto_us == 100)]
        losses = sorted(sub.loss_param.unique())
        summary += [f"**{size_label(s)}**", "", "| loss | go-back-N | SR | SR+TLP | " + " | ".join(DLABEL[d] for d in DECODES) + " |",
                    "|---|---|---|---|" + "---|" * len(DECODES)]
        for l in losses:
            g = sub[(sub.loss_param == l)]
            v = [g[g.proto == p].p99_us.min() for p in ("gbn", "sr", "srtlp")]
            for dec in DECODES:
                x = bfa[(bfa.decode == dec) & (bfa.rtt_us == rtt0) & (bfa.msg_bytes == s) & (bfa.loss_param == l) & (bfa.flows == 1) & (bfa.rto_us == 100)]
                v.append(x.p99_us.min() if len(x) else float("nan"))
            code = bfa[(bfa.decode == "hybrid_default") & (bfa.rtt_us == rtt0) & (bfa.msg_bytes == s) & (bfa.loss_param == l) & (bfa.flows == 1) & (bfa.rto_us == 100)].code
            summary.append(f"| {l:g} | " + " | ".join(f"{x:.1f}" for x in v) + " |")
        summary.append("")

# ── fig 3: collective ───────────────────────────────────────────────────────
coll = load("collective.csv")
if coll is not None:
    rtt0 = 5 if 5 in coll.rtt_us.unique() else sorted(coll.rtt_us.unique())[0]
    sizes = sorted(coll.msg_bytes.unique())
    fig, axes = plt.subplots(1, len(sizes), figsize=(4.6 * len(sizes), 3.5), squeeze=False)
    bfc = best_fec(coll)
    summary += [f"## Collective step: slowest of {int(coll.flows.iloc[0])} flows, RTT {rtt0:g} µs (p99 µs)", ""]
    for ax, s in zip(axes[0], sizes):
        sub = coll[(coll.rtt_us == rtt0) & (coll.msg_bytes == s)]
        rows = {}
        for p in ("gbn", "sr", "srtlp"):
            x = sub[sub.proto == p].sort_values("loss_param")
            ax.plot(x.loss_param, x.p99_us, marker="o", ms=4, color=C[p], ls="--" if p == "gbn" else "-",
                    label={"gbn": "go-back-N", "sr": "selective repeat", "srtlp": "SR + tail-loss probe"}[p])
            rows[p] = x.set_index("loss_param").p99_us
        for dec in DECODES:
            x = bfc[(bfc.decode == dec) & (bfc.rtt_us == rtt0) & (bfc.msg_bytes == s)].sort_values("loss_param")
            ax.plot(x.loss_param, x.p99_us, marker="s", ms=4, color=C[dec], label=DLABEL[dec])
            rows[dec] = x.set_index("loss_param").p99_us
        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlabel("loss rate"); ax.set_ylabel("p99 step completion (µs)")
        ax.set_title(f"{size_label(s)} per flow, slowest of {int(coll.flows.iloc[0])}")
        summary += [f"**{size_label(s)} per flow**", "", "| loss | go-back-N | SR | SR+TLP | " + " | ".join(DLABEL[d] for d in DECODES) + " |",
                    "|---|---|---|---|" + "---|" * len(DECODES)]
        for l in sorted(sub.loss_param.unique()):
            summary.append(f"| {l:g} | " + " | ".join(f"{rows[k].get(l, float('nan')):.1f}" for k in ["gbn", "sr", "srtlp"] + DECODES) + " |")
        summary.append("")
    axes[0][0].legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig3_collective.png"), dpi=170); plt.close(fig)

# ── fig 4: dc3 ratio maps ───────────────────────────────────────────────────
dc3 = load("dc3.csv")
if dc3 is not None:
    rtt0 = 5 if 5 in dc3.rtt_us.unique() else sorted(dc3.rtt_us.unique())[0]
    fig, axes = plt.subplots(1, len(DECODES), figsize=(3.3 * len(DECODES), 2.9), squeeze=False, constrained_layout=True)
    for c, dec in enumerate(DECODES):
        sizes, xs, R, codes = ratio_grid(dc3, "loss_param", rtt0, dec)
        im = heat(axes[0][c], sizes, xs, R, codes, "DC3 burst frequency (N→D)", f"{DLABEL[dec]}\nRTT {rtt0:g} µs", show_y=(c == 0))
    cb = fig.colorbar(im, ax=axes, shrink=0.8, label="p99(best retransmission) / p99(FEC)")
    cb.set_ticks([-2, -1, 0, 1, 2]); cb.set_ticklabels(["¼", "½", "1", "2", "4"])
    fig.savefig(os.path.join(FIG, "fig4_ratio_dc3.png"), dpi=170, bbox_inches="tight"); plt.close(fig)
    ml = dc3.groupby("loss_param").mean_loss.first()
    summary += ["## DC3 (bursty) loss", "", "Burst frequency → mean loss rate: " +
                ", ".join(f"{k:g} → {v:.2g}" for k, v in ml.items()), ""]

# ── fig 5: RTO sensitivity ──────────────────────────────────────────────────
rto = load("rto.csv")
if rto is not None:
    s = sorted(rto.msg_bytes.unique())[0]
    fig, ax = plt.subplots(figsize=(4.8, 3.5))
    bfr = best_fec(rto)
    for r, ls in ((100, "-"), (1000, "--")):
        x = rto[(rto.proto == "sr") & (rto.rto_us == r) & (rto.msg_bytes == s)].sort_values("loss_param")
        ax.plot(x.loss_param, x.p99_us, ls=ls, marker="o", ms=4, color=C["sr"], label=f"selective repeat, RTO {r} µs")
        x = rto[(rto.proto == "srtlp") & (rto.rto_us == r) & (rto.msg_bytes == s)].sort_values("loss_param")
        ax.plot(x.loss_param, x.p99_us, ls=ls, marker="o", ms=4, color=C["srtlp"], label=f"SR + TLP, RTO {r} µs")
        x = bfr[(bfr.rto_us == r) & (bfr.msg_bytes == s)].sort_values("loss_param")
        ax.plot(x.loss_param, x.p99_us, ls=ls, marker="s", ms=4, color=C["hybrid_default"], label=f"FEC (m×m), RTO {r} µs")
    ax.set_xscale("log"); ax.set_yscale("log"); ax.set_xlabel("loss rate"); ax.set_ylabel("p99 completion (µs)")
    ax.set_title(f"{size_label(s)}, RTT 5 µs: timeout sensitivity"); ax.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(FIG, "fig5_rto.png"), dpi=170); plt.close(fig)

with open(os.path.join(D, "summary.md"), "w") as f:
    f.write("\n".join(summary) + "\n")
print("\n".join(summary))
