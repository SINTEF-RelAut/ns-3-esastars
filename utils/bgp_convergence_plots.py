#!/usr/bin/env python3
"""Plot BGP convergence figures from bgp-convergence-first simulation outputs.

Reads:
  <dir>/probe_<src>_<dst>.csv    -- UDP probe RTT log (time-series)
  <dir>/link_events.csv          -- link failure/recovery schedule
  <dir>/metrics.csv              -- pre-computed per-pair metrics (from bgp_convergence_analysis.py)
  <dir>/bgp_cp_as<N>.csv        -- per-AS control-plane event logs (optional)

Produces (written to --out-dir):
  rtt_pair_<src>_<dst>.png          -- RTT time-series with event markers, one per probe pair
  rtt_all_pairs.png                 -- All probe pairs on one figure (normalised to baseline RTT)
  metrics_outage_by_class.png       -- Outage duration grouped by pair class
  metrics_switchover_by_class.png   -- Switchover delay grouped by pair class
  metrics_recovery_by_class.png     -- Recovery delay grouped by pair class
  metrics_bgp_detect_by_class.png   -- BGP detection delay grouped by pair class
  cp_messages_timeline.png          -- Cumulative control-plane UPDATE/NOTIFICATION counts over time
  cp_rtt_scatter.png                -- BGP detection delay vs data-plane outage duration scatter
"""

from __future__ import annotations

import argparse
import csv
import glob
import os
import statistics
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ---------------------------------------------------------------------------
# Colour / style constants
# ---------------------------------------------------------------------------

PAIR_COLORS = [
    "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728",
    "#9467bd", "#8c564b", "#e377c2", "#7f7f7f",
]

CLASS_COLORS: Dict[str, str] = {
    "core-core": "#1f77b4",
    "customer-customer": "#d62728",
    "customer-core": "#2ca02c",
    "other": "#7f7f7f",
}

EVENT_COLORS = {
    "link_down": "#d62728",
    "link_up":   "#2ca02c",
}

# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def load_csv_rows(path: str) -> List[Dict[str, str]]:
    with open(path) as f:
        return list(csv.DictReader(f))


def parse_float(s: str) -> Optional[float]:
    s = s.strip()
    return float(s) if s else None


def load_probe_csv(path: str) -> Tuple[List[float], List[Optional[float]], List[float], List[float]]:
    """Returns (reply_times, reply_rtts, timeout_times, sent_times)."""
    rows = load_csv_rows(path)
    reply_times: List[float] = []
    reply_rtts: List[Optional[float]] = []
    timeout_times: List[float] = []
    sent_times: List[float] = []
    for row in rows:
        t = float(row["time_s"])
        kind = row["event"].strip()
        if kind == "reply":
            reply_times.append(t)
            reply_rtts.append(parse_float(row["rtt_ms"]))
        elif kind == "timeout":
            timeout_times.append(t)
        elif kind == "sent":
            sent_times.append(t)
    return reply_times, reply_rtts, timeout_times, sent_times


def load_link_events(path: str) -> List[Dict[str, str]]:
    return load_csv_rows(path)


def load_cp_events(path: str) -> List[Dict[str, str]]:
    return load_csv_rows(path)


# ---------------------------------------------------------------------------
# Plot 1 – RTT time-series per probe pair
# ---------------------------------------------------------------------------

def plot_rtt_single_pair(
    src_as: int,
    dst_as: int,
    pair_class: str,
    probe_path: str,
    link_events: List[Dict[str, str]],
    out_path: str,
) -> None:
    reply_times, reply_rtts, timeout_times, _ = load_probe_csv(probe_path)

    fig, ax = plt.subplots(figsize=(12, 4))

    # RTT line
    if reply_times:
        ax.plot(reply_times, reply_rtts, color="#1f77b4", linewidth=1.2,
                label="RTT (ms)")

    # Timeout ticks on x-axis (rug plot)
    if timeout_times:
        ax.scatter(timeout_times,
                   [0] * len(timeout_times),
                   marker="|", s=80, color="#d62728", zorder=5,
                   label="Timeout")

    # Vertical lines for link events
    legend_handles = [
        mpatches.Patch(color="#1f77b4", label="RTT (ms)"),
        mpatches.Patch(color="#d62728", label="Timeout (no reply)"),
    ]
    seen_events: Dict[str, bool] = {}
    for ev in link_events:
        t = float(ev["time_s"])
        kind = ev["event"].strip()
        color = EVENT_COLORS.get(kind, "#888888")
        lbl = kind.replace("_", " ")
        ls = "--" if kind == "link_down" else ":"
        ax.axvline(t, color=color, linewidth=1.4, linestyle=ls, alpha=0.85)
        if kind not in seen_events:
            legend_handles.append(mpatches.Patch(color=color, label=lbl))
            seen_events[kind] = True

    ax.set_xlabel("Simulation time (s)")
    ax.set_ylabel("RTT (ms)")
    ax.set_title(f"RTT over time: AS{src_as} → AS{dst_as}  [{pair_class}]")
    ax.legend(handles=legend_handles, loc="upper right", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Plot 2 – All pairs on one figure (normalised RTT)
# ---------------------------------------------------------------------------

def plot_rtt_all_pairs(
    pairs: List[Tuple[int, int, str, str]],  # (src, dst, pair_class, probe_path)
    link_events: List[Dict[str, str]],
    out_path: str,
) -> None:
    fig, ax = plt.subplots(figsize=(14, 5))

    for i, (src_as, dst_as, pair_class, probe_path) in enumerate(pairs):
        reply_times, reply_rtts, timeout_times, _ = load_probe_csv(probe_path)
        if not reply_times:
            continue

        finite_rtts = [r for r in reply_rtts if r is not None]
        baseline = statistics.median(finite_rtts[:20]) if len(finite_rtts) >= 20 else (finite_rtts[0] if finite_rtts else 1.0)
        norm_rtts = [(r / baseline if r is not None else 0.0) for r in reply_rtts]

        color = PAIR_COLORS[i % len(PAIR_COLORS)]
        label = f"AS{src_as}→AS{dst_as} [{pair_class}]"
        ax.plot(reply_times, norm_rtts, color=color, linewidth=1.2, label=label, alpha=0.85)

        # Timeout ticks at y=0
        if timeout_times:
            ax.scatter(timeout_times,
                       [-0.05 - 0.04 * i] * len(timeout_times),
                       marker="|", s=60, color=color, zorder=5)

    seen_events: Dict[str, bool] = {}
    for ev in link_events:
        t = float(ev["time_s"])
        kind = ev["event"].strip()
        color = EVENT_COLORS.get(kind, "#888888")
        ls = "--" if kind == "link_down" else ":"
        ax.axvline(t, color=color, linewidth=1.2, linestyle=ls, alpha=0.75)
        if kind not in seen_events:
            seen_events[kind] = True

    # Legend additions for event lines
    extra = [mpatches.Patch(color=c, label=k.replace("_", " "))
             for k, c in EVENT_COLORS.items()]
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles + extra, labels + [e.get_label() for e in extra],
              loc="upper right", fontsize=7, ncol=2)
    ax.set_xlabel("Simulation time (s)")
    ax.set_ylabel("RTT (normalised to baseline median)")
    ax.set_title("BGP convergence – RTT time-series (all probe pairs, normalised)")
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Plot 3 – Bar charts per metric grouped by pair class and failure event
# ---------------------------------------------------------------------------

def _bar_metric_by_class(
    metrics_rows: List[Dict[str, str]],
    metric_col: str,
    metric_label: str,
    link_events: List[Dict[str, str]],
    out_path: str,
) -> None:
    # Collect event labels (unique failure events in order)
    failure_events: List[str] = []
    seen_fe: Dict[str, bool] = {}
    for row in metrics_rows:
        fe = row["failed_link"]
        if fe not in seen_fe:
            failure_events.append(fe)
            seen_fe[fe] = True

    # Get ordered pair classes
    classes: List[str] = []
    seen_cls: Dict[str, bool] = {}
    for row in metrics_rows:
        pc = row.get("pair_class", "other")
        if pc not in seen_cls:
            classes.append(pc)
            seen_cls[pc] = True
    classes.sort()

    # For each (pair_class, failure_event), collect values
    grouped: Dict[Tuple[str, str], List[float]] = {}
    for row in metrics_rows:
        v = parse_float(row.get(metric_col, ""))
        if v is None:
            continue
        key = (row.get("pair_class", "other"), row["failed_link"])
        grouped.setdefault(key, []).append(v)

    if not grouped:
        return

    # Build bar positions: x = failure events, groups = pair classes
    n_fe = len(failure_events)
    n_cls = len(classes)
    bar_w = 0.8 / max(n_cls, 1)
    fig, ax = plt.subplots(figsize=(max(6, n_fe * 2.5), 5))

    for ci, pc in enumerate(classes):
        color = CLASS_COLORS.get(pc, PAIR_COLORS[ci % len(PAIR_COLORS)])
        for fi, fe in enumerate(failure_events):
            vals = grouped.get((pc, fe), [])
            if not vals:
                continue
            x = fi + (ci - (n_cls - 1) / 2.0) * bar_w
            mean_val = statistics.mean(vals)
            ax.bar(x, mean_val, width=bar_w * 0.9, color=color,
                   label=pc if fi == 0 else "_nolabel_", alpha=0.85)
            if len(vals) > 1:
                ax.errorbar(x, mean_val,
                            yerr=[[mean_val - min(vals)], [max(vals) - mean_val]],
                            fmt="none", color="black", capsize=4, linewidth=1.2)

    ax.set_xticks(range(n_fe))
    ax.set_xticklabels([fe for fe in failure_events], rotation=20, ha="right")
    ax.set_ylabel(metric_label)
    ax.set_title(f"BGP convergence – {metric_label} by pair class")

    # Deduplicate legend
    handles, labels = ax.get_legend_handles_labels()
    seen_lbl: Dict[str, bool] = {}
    deduped_h, deduped_l = [], []
    for h, l in zip(handles, labels):
        if l not in seen_lbl and not l.startswith("_"):
            deduped_h.append(h)
            deduped_l.append(l)
            seen_lbl[l] = True
    ax.legend(deduped_h, deduped_l, title="Pair class", loc="upper right")
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Plot 4 – Control-plane message timeline (cumulative)
# ---------------------------------------------------------------------------

def plot_cp_timeline(
    data_dir: str,
    link_events: List[Dict[str, str]],
    out_path: str,
) -> None:
    cp_files = sorted(glob.glob(os.path.join(data_dir, "bgp_cp_as*.csv")))
    if not cp_files:
        return

    all_events: List[Dict[str, str]] = []
    for fp in cp_files:
        all_events.extend(load_cp_events(fp))
    all_events.sort(key=lambda r: float(r["time_s"]))

    # Collect event time-series per message type
    series: Dict[str, Tuple[List[float], List[int]]] = {}
    counts: Dict[str, int] = {}

    for row in all_events:
        t = float(row["time_s"])
        ev = row["event"].strip()
        detail = row["detail"].strip()

        if ev == "KEEPALIVE":
            continue  # omit keepalives for readability

        if ev == "UPDATE":
            key = f"UPDATE_{detail}" if detail in ("announce", "withdraw") else "UPDATE_other"
        elif ev == "STATE_CHANGE":
            key = f"STATE {detail}"
        else:
            key = ev

        counts[key] = counts.get(key, 0) + 1
        if key not in series:
            series[key] = ([], [])
        series[key][0].append(t)
        series[key][1].append(counts[key])

    if not series:
        return

    fig, ax = plt.subplots(figsize=(14, 5))
    color_cycle = plt.rcParams["axes.prop_cycle"].by_key()["color"]
    for ci, (key, (times, cum_counts)) in enumerate(sorted(series.items())):
        color = color_cycle[ci % len(color_cycle)]
        ax.plot(times, cum_counts, label=key, color=color, linewidth=1.3, drawstyle="steps-post")

    seen_events: Dict[str, bool] = {}
    for ev in link_events:
        t = float(ev["time_s"])
        kind = ev["event"].strip()
        color = EVENT_COLORS.get(kind, "#888888")
        ls = "--" if kind == "link_down" else ":"
        ax.axvline(t, color=color, linewidth=1.2, linestyle=ls, alpha=0.75)
        if kind not in seen_events:
            seen_events[kind] = True

    extra = [mpatches.Patch(color=c, label=k.replace("_", " "))
             for k, c in EVENT_COLORS.items()]
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles + extra, labels + [e.get_label() for e in extra],
              loc="upper left", fontsize=7, ncol=2)
    ax.set_xlabel("Simulation time (s)")
    ax.set_ylabel("Cumulative message count")
    ax.set_title("BGP control-plane messages over time (cumulative, no KEEPALIVE)")
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Plot 5 – Scatter: BGP detection delay vs data-plane outage duration
# ---------------------------------------------------------------------------

def plot_bgp_vs_outage_scatter(
    metrics_rows: List[Dict[str, str]],
    out_path: str,
) -> None:
    fig, ax = plt.subplots(figsize=(7, 5))
    plotted = False

    classes = sorted(set(r.get("pair_class", "other") for r in metrics_rows))
    for pc in classes:
        rows = [r for r in metrics_rows if r.get("pair_class", "other") == pc]
        xs, ys, labels = [], [], []
        for r in rows:
            x = parse_float(r.get("bgp_detection_delay_s", ""))
            y = parse_float(r.get("outage_duration_s", ""))
            if x is None or y is None:
                continue
            xs.append(x)
            ys.append(y)
            labels.append(f"AS{r['src_as']}→AS{r['dst_as']} evt{r['event_idx']}")

        if not xs:
            continue
        color = CLASS_COLORS.get(pc, "#888888")
        ax.scatter(xs, ys, label=pc, color=color, s=60, alpha=0.85, zorder=5)
        for x, y, lbl in zip(xs, ys, labels):
            ax.annotate(lbl, (x, y), fontsize=6, xytext=(3, 3),
                        textcoords="offset points", alpha=0.75)
        plotted = True

    if not plotted:
        plt.close(fig)
        return

    # Diagonal reference line y = x
    all_vals = [v for r in metrics_rows
                for v in [parse_float(r.get("bgp_detection_delay_s", "")),
                          parse_float(r.get("outage_duration_s", ""))]
                if v is not None]
    if all_vals:
        lim = max(all_vals) * 1.1
        ax.plot([0, lim], [0, lim], color="grey", linestyle="--",
                linewidth=0.8, label="detect = outage")
        ax.set_xlim(0, lim)
        ax.set_ylim(0, lim)

    ax.set_xlabel("BGP detection delay (s)  [time from link_down to ESTABLISHED→IDLE]")
    ax.set_ylabel("Data-plane outage duration (s)")
    ax.set_title("BGP detection delay vs data-plane outage duration")
    ax.legend(title="Pair class", fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Plot BGP convergence figures from bgp-convergence-first outputs")
    p.add_argument("--dir", default="build/bgp_convergence_first",
                   help="Directory with probe_*.csv, link_events.csv, metrics.csv, bgp_cp_as*.csv")
    p.add_argument("--metrics-csv", default="",
                   help="Override path to per-pair metrics CSV (default: <dir>/metrics.csv)")
    p.add_argument("--out-dir", default="",
                   help="Output directory for PNG plots (default: <dir>/plots)")
    return p.parse_args()


def main() -> None:
    args = parse_args()
    data_dir = args.dir
    metrics_csv = args.metrics_csv or os.path.join(data_dir, "metrics.csv")
    out_dir = args.out_dir or os.path.join(data_dir, "plots")
    os.makedirs(out_dir, exist_ok=True)

    # --- Load shared inputs ---
    link_events_path = os.path.join(data_dir, "link_events.csv")
    if not os.path.exists(link_events_path):
        print(f"ERROR: {link_events_path} not found")
        raise SystemExit(1)
    link_events = load_link_events(link_events_path)

    metrics_rows: List[Dict[str, str]] = []
    if os.path.exists(metrics_csv):
        metrics_rows = load_csv_rows(metrics_csv)
    else:
        print(f"WARNING: metrics CSV not found at {metrics_csv} — metric bar charts skipped.")
        print("  Run bgp_convergence_analysis.py --out-csv first.")

    # Discover probe files
    probe_files = sorted(glob.glob(os.path.join(data_dir, "probe_*.csv")))
    if not probe_files:
        print(f"ERROR: No probe_*.csv found in {data_dir}")
        raise SystemExit(1)

    # Build pair list with pair_class from metrics CSV (fall back to "unknown")
    pair_class_map: Dict[Tuple[int, int], str] = {}
    for row in metrics_rows:
        pair_class_map[(int(row["src_as"]), int(row["dst_as"]))] = row.get("pair_class", "other")

    pairs_info: List[Tuple[int, int, str, str]] = []
    for pf in probe_files:
        bn = os.path.basename(pf).replace(".csv", "").split("_")
        if len(bn) >= 3:
            src, dst = int(bn[1]), int(bn[2])
            pc = pair_class_map.get((src, dst), "other")
            pairs_info.append((src, dst, pc, pf))

    written: List[str] = []

    # --- Plot 1: RTT time-series per pair ---
    for src_as, dst_as, pair_class, probe_path in pairs_info:
        out_path = os.path.join(out_dir, f"rtt_pair_{src_as}_{dst_as}.png")
        plot_rtt_single_pair(src_as, dst_as, pair_class, probe_path, link_events, out_path)
        written.append(out_path)

    # --- Plot 2: All pairs normalised ---
    out_path = os.path.join(out_dir, "rtt_all_pairs.png")
    plot_rtt_all_pairs(pairs_info, link_events, out_path)
    written.append(out_path)

    # --- Plots 3a–3d: Per-metric, per-class bar charts ---
    if metrics_rows:
        metric_specs = [
            ("outage_duration_s",         "Outage duration (s)",           "metrics_outage_by_class.png"),
            ("switchover_delay_s",        "Switchover delay (s)",          "metrics_switchover_by_class.png"),
            ("recovery_delay_from_up_s",  "Recovery delay from link-up (s)", "metrics_recovery_by_class.png"),
            ("bgp_detection_delay_s",     "BGP detection delay (s)",       "metrics_bgp_detect_by_class.png"),
        ]
        for col, label, filename in metric_specs:
            out_path = os.path.join(out_dir, filename)
            _bar_metric_by_class(metrics_rows, col, label, link_events, out_path)
            written.append(out_path)

    # --- Plot 4: Control-plane message timeline ---
    out_path = os.path.join(out_dir, "cp_messages_timeline.png")
    plot_cp_timeline(data_dir, link_events, out_path)
    if os.path.exists(out_path):
        written.append(out_path)

    # --- Plot 5: BGP detection vs outage scatter ---
    if metrics_rows:
        out_path = os.path.join(out_dir, "cp_rtt_scatter.png")
        plot_bgp_vs_outage_scatter(metrics_rows, out_path)
        if os.path.exists(out_path):
            written.append(out_path)

    print(f"\nWrote {len(written)} plots to {out_dir}/")
    for p in written:
        print(f"  {p}")


if __name__ == "__main__":
    main()
