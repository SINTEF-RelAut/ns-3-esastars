#!/usr/bin/env python3
"""Plot BGP sweep metrics (convergence time, CP overhead, packet loss) vs timer settings.

Reads all sweep output directories matching the pattern:
  build/sweep_bgp_{mode}_{scenario}_mrai{X}_clk{Y}_bcn{Z}*/

For each directory computes:
  - Convergence time (s): CP quiescence -- for each real link event, elapsed time from
    link_down until the last UPDATE/NOTIFICATION message before the next event.
    Averaged over all events.
  - CP overhead (messages): total UPDATE + NOTIFICATION messages across all AS CP logs.
  - Packet loss (%): timeout fraction across probe pairs with >= 1% reply rate.

Produces one PNG per metric with one line per topology mode (baseline / split / direct).
"""
from __future__ import annotations

import argparse
import bisect
import csv
import re
from pathlib import Path
from typing import Dict, List, Optional

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

STARTUP_CUTOFF_S = 10.0   # ignore link events before this time (startup phase)
MIN_REPLY_RATE = 0.01      # probe pair must have >= 1% replies to be included in metrics

MODE_STYLES: Dict[str, Dict] = {
    "baseline": {"color": "#1f77b4", "marker": "o", "label": "Dual-IXP baseline"},
    "split":    {"color": "#ff7f0e", "marker": "s", "label": "Dual-IXP split-AS"},
    "direct":   {"color": "#2ca02c", "marker": "^", "label": "Direct links"},
}


# ---------------------------------------------------------------------------
# CSV helpers
# ---------------------------------------------------------------------------

def _load_csv(path: Path) -> List[Dict[str, str]]:
    with path.open(newline="") as fh:
        return list(csv.DictReader(fh))


# ---------------------------------------------------------------------------
# Metric: CP overhead
# ---------------------------------------------------------------------------

def compute_cp_overhead(run_dir: Path) -> int:
    """Count UPDATE + NOTIFICATION messages across all bgp_cp_as*.csv files."""
    total = 0
    for cp_file in sorted(run_dir.glob("bgp_cp_as*.csv")):
        for row in _load_csv(cp_file):
            ev = row.get("event", "").strip()
            if ev in ("UPDATE", "NOTIFICATION"):
                total += 1
    return total


# ---------------------------------------------------------------------------
# Metric: convergence time (CP quiescence)
# ---------------------------------------------------------------------------

def _load_all_update_times(run_dir: Path) -> List[float]:
    """Collect sorted timestamps of all UPDATE/NOTIFICATION events across all AS CP logs."""
    times: List[float] = []
    for cp_file in sorted(run_dir.glob("bgp_cp_as*.csv")):
        for row in _load_csv(cp_file):
            ev = row.get("event", "").strip()
            if ev in ("UPDATE", "NOTIFICATION"):
                times.append(float(row["time_s"]))
    times.sort()
    return times


def compute_convergence_time(run_dir: Path) -> Optional[float]:
    """Mean CP quiescence time (s) per link event.

    For each real link_down event at t_down, find the last UPDATE/NOTIFICATION
    message in the window [t_down, t_next), where t_next is the next distinct
    link_down time (or t_down + 300 if none).  Convergence = last_update - t_down.
    Only windows containing at least one UPDATE are counted.
    """
    link_events_path = run_dir / "link_events.csv"
    if not link_events_path.exists():
        return None

    # Deduplicate link_down times (multiple ASes may report the same event time)
    seen: set = set()
    link_downs: List[float] = []
    for row in _load_csv(link_events_path):
        t = float(row["time_s"])
        if row["event"] == "link_down" and t >= STARTUP_CUTOFF_S and t not in seen:
            seen.add(t)
            link_downs.append(t)
    link_downs.sort()
    if not link_downs:
        return None

    update_times = _load_all_update_times(run_dir)
    if not update_times:
        return None

    convergence_values: List[float] = []
    for i, t_down in enumerate(link_downs):
        t_next = link_downs[i + 1] if i + 1 < len(link_downs) else t_down + 300.0

        lo = bisect.bisect_left(update_times, t_down)
        hi = bisect.bisect_left(update_times, t_next)
        if lo >= hi:
            continue  # no updates in this window -- event had no CP effect

        last_update = update_times[hi - 1]
        convergence_values.append(last_update - t_down)

    if not convergence_values:
        return None
    return sum(convergence_values) / len(convergence_values)


# ---------------------------------------------------------------------------
# Metric: packet loss
# ---------------------------------------------------------------------------

def compute_packet_loss(run_dir: Path) -> Optional[float]:
    """Timeout fraction across probe pairs with >= MIN_REPLY_RATE reply rate (%)."""
    total_sent = 0
    total_timeout = 0

    for probe_path in sorted(run_dir.glob("probe_*.csv")):
        rows = _load_csv(probe_path)
        evs = [r["event"].strip() for r in rows]
        n_sent = sum(1 for e in evs if e == "sent")
        n_reply = sum(1 for e in evs if e == "reply")
        n_timeout = sum(1 for e in evs if e == "timeout")
        if n_sent == 0 or n_reply / n_sent < MIN_REPLY_RATE:
            continue
        total_sent += n_sent
        total_timeout += n_timeout

    if total_sent == 0:
        return None
    return 100.0 * total_timeout / total_sent


# ---------------------------------------------------------------------------
# Directory discovery
# ---------------------------------------------------------------------------

_DIR_RE = re.compile(
    r"sweep_bgp_(?P<mode>baseline|split|direct)_(?P<scenario>visible|hidden)"
    r"_mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_bcn(?P<bcn>\d+)"
)


def discover_runs(build_dir: Path, scenario: str) -> Dict[str, Dict[int, Path]]:
    """Return {mode: {mrai_s: dir_path}} for the given scenario."""
    result: Dict[str, Dict[int, Path]] = {}
    for d in sorted(build_dir.iterdir()):
        if not d.is_dir():
            continue
        m = _DIR_RE.match(d.name)
        if not m:
            continue
        if m.group("scenario") != scenario:
            continue
        mode = m.group("mode")
        mrai = int(m.group("mrai"))
        result.setdefault(mode, {})[mrai] = d
    return result


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def _save_line_plot(
    data: Dict[str, Dict[int, Optional[float]]],
    ylabel: str,
    title: str,
    out_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(7, 4))

    for mode, points in sorted(data.items()):
        style = MODE_STYLES.get(mode, {"color": "gray", "marker": "x", "label": mode})
        xs = sorted(k for k, v in points.items() if v is not None)
        ys = [points[x] for x in xs]
        if not xs:
            continue
        ax.plot(xs, ys, marker=style["marker"], color=style["color"],
                label=style["label"], linewidth=1.8, markersize=6)

    ax.set_xlabel("MRAI (s)")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.legend(framealpha=0.85)
    ax.grid(True, linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {out_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Plot BGP sweep metrics vs timer settings."
    )
    parser.add_argument(
        "--scenario",
        choices=["visible", "hidden"],
        default="visible",
        help="Which scenario to plot (default: visible)",
    )
    parser.add_argument(
        "--modes",
        nargs="+",
        choices=["baseline", "split", "direct"],
        default=["baseline", "split", "direct"],
        help="Topology modes to include (default: all)",
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Root build directory (default: build)",
    )
    parser.add_argument(
        "--out-dir",
        default="build/sweep_metric_plots",
        help="Directory for output PNGs (default: build/sweep_metric_plots)",
    )
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Discovering sweep runs in {build_dir}/ (scenario={args.scenario}) ...")
    all_runs = discover_runs(build_dir, args.scenario)

    runs = {m: v for m, v in all_runs.items() if m in args.modes}
    if not runs:
        print("No matching sweep directories found.")
        return 1

    print(f"Found modes: {sorted(runs.keys())}")
    for mode, pts in sorted(runs.items()):
        for mrai, d in sorted(pts.items()):
            print(f"  {mode:10s}  mrai={mrai:2d}  {d.name}")

    conv_data:    Dict[str, Dict[int, Optional[float]]] = {}
    cp_data:      Dict[str, Dict[int, Optional[float]]] = {}
    loss_data:    Dict[str, Dict[int, Optional[float]]] = {}

    for mode, pts in sorted(runs.items()):
        conv_data[mode] = {}
        cp_data[mode] = {}
        loss_data[mode] = {}
        for mrai, run_dir in sorted(pts.items()):
            print(f"  Computing metrics for {run_dir.name} ...")
            conv_data[mode][mrai] = compute_convergence_time(run_dir)
            cp_data[mode][mrai]   = float(compute_cp_overhead(run_dir))
            loss_data[mode][mrai] = compute_packet_loss(run_dir)

    scenario_label = args.scenario.capitalize()

    print("\nPlotting ...")
    _save_line_plot(
        conv_data,
        ylabel="Mean convergence time (s)",
        title=f"BGP convergence time vs MRAI – {scenario_label}",
        out_path=out_dir / "convergence_time.png",
    )
    _save_line_plot(
        cp_data,
        ylabel="Total UPDATE + NOTIFICATION messages",
        title=f"BGP CP overhead vs MRAI – {scenario_label}",
        out_path=out_dir / "cp_overhead.png",
    )
    _save_line_plot(
        loss_data,
        ylabel="Packet loss (%)",
        title=f"BGP packet loss vs MRAI – {scenario_label}",
        out_path=out_dir / "packet_loss.png",
    )

    print(f"\n{'Mode':<12} {'MRAI':>6}  {'Conv(s)':>10}  {'CP msgs':>10}  {'Loss%':>8}")
    print("-" * 56)
    for mode in sorted(runs.keys()):
        for mrai in sorted(runs[mode].keys()):
            c = conv_data[mode].get(mrai)
            p = cp_data[mode].get(mrai)
            lo = loss_data[mode].get(mrai)
            cs = f"{c:.2f}" if c is not None else "n/a"
            ps = f"{p:.0f}" if p is not None else "n/a"
            ls = f"{lo:.2f}" if lo is not None else "n/a"
            print(f"{mode:<12} {mrai:>6}  {cs:>10}  {ps:>10}  {ls:>8}")

    print(f"\nPlots written to {out_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
