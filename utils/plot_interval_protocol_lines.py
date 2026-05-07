#!/usr/bin/env python3
"""Plot interval sweep metrics with separate BGP and SCION lines.

Input layout expected:
  <base-dir>/pcb<interval>_clk<interval>/combined_metrics.csv

Each combined metrics CSV is expected to contain columns:
  scenario,protocol,mean_s,p50_s,p95_s,p99_s,max_s,cp_total,cp_propagation,...
"""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


METRICS = {
    "mean_s": "Recovery mean (s)",
    "p50_s": "Recovery p50 (s)",
    "p95_s": "Recovery p95 (s)",
    "p99_s": "Recovery p99 (s)",
    "max_s": "Recovery max (s)",
    "cp_total": "Control-plane total messages",
    "cp_propagation": "Control-plane propagation messages",
    "cp_exploration": "Control-plane exploration messages",
    "cp_prop_to_explore_ratio": "Propagation/Exploration ratio",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Plot BGP/SCION lines vs interval")
    p.add_argument(
        "--base-dir",
        default="build/protocol_comparison",
        help="Directory containing pcb*_clk* subdirectories",
    )
    p.add_argument(
        "--out-dir",
        default="build/protocol_comparison/interval_plots",
        help="Directory for generated line plots",
    )
    p.add_argument(
        "--metrics",
        default="mean_s,p50_s,p95_s,p99_s,cp_total",
        help="Comma-separated metric columns to plot",
    )
    p.add_argument(
        "--scenarios",
        default="",
        help="Optional comma-separated scenario filter (e.g. A,C,D)",
    )
    return p.parse_args()


def parse_interval(dirname: str) -> float | None:
    m = re.match(r"^pcb([0-9]+(?:\.[0-9]+)?)_clk([0-9]+(?:\.[0-9]+)?)$", dirname)
    if not m:
        return None
    return float(m.group(1))


def to_float(v: str) -> float | None:
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


# For each metric, defines which columns to use as (lower_anchor, upper_anchor)
# for asymmetric error bars.  The bars show [center - lower, upper - center].
# None means no bar on that side.
METRIC_ERROR_BOUNDS: Dict[str, Tuple[str | None, str | None]] = {
    "mean_s":   ("p50_s", "p95_s"),
    "p50_s":    (None,    "p95_s"),
    "p95_s":    ("p50_s", "p99_s"),
    "p99_s":    ("p95_s", "max_s"),
    "max_s":    ("p99_s", None),
}


def discover_rows(base_dir: Path) -> List[Tuple[float, Dict[str, str]]]:
    rows: List[Tuple[float, Dict[str, str]]] = []
    for sub in sorted(base_dir.iterdir()):
        if not sub.is_dir():
            continue
        interval = parse_interval(sub.name)
        if interval is None:
            continue
        csv_path = sub / "combined_metrics.csv"
        if not csv_path.exists():
            continue

        with csv_path.open("r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                rows.append((interval, row))
    return rows


def _err(value: float, anchor: str | None, row: Dict[str, str], direction: str) -> float:
    """Return non-negative half-width of an error bar in one direction."""
    if anchor is None:
        return 0.0
    anchor_val = to_float(row.get(anchor, ""))
    if anchor_val is None:
        return 0.0
    if direction == "low":
        return max(0.0, value - anchor_val)
    return max(0.0, anchor_val - value)


def make_plot(
    protocol_rows: Dict[str, List[Tuple[float, Dict[str, str]]]],
    scenario: str,
    metric: str,
    ylabel: str,
    out_path: Path,
) -> bool:
    lower_col, upper_col = METRIC_ERROR_BOUNDS.get(metric, (None, None))
    has_error_bars = (lower_col is not None) or (upper_col is not None)

    protocols = [
        ("BGP",   "#d62728"),
        ("SCION", "#1f77b4"),
    ]

    any_data = False
    plt.figure(figsize=(8, 5))

    for proto, color in protocols:
        pts = sorted(protocol_rows.get(proto, []), key=lambda x: x[0])
        if not pts:
            continue
        any_data = True

        xs = [interval for interval, _ in pts]
        ys_raw = [to_float(row.get(metric)) for _, row in pts]
        # drop intervals where metric is absent
        valid = [(x, y, row) for (x, row), y in zip(pts, ys_raw) if y is not None]
        if not valid:
            continue
        xs_v = [x for x, _, _ in valid]
        ys_v = [y for _, y, _ in valid]

        if has_error_bars:
            yerr_low  = [_err(y, lower_col, row, "low")  for _, y, row in valid]
            yerr_high = [_err(y, upper_col, row, "high") for _, y, row in valid]
            plt.errorbar(
                xs_v, ys_v,
                yerr=[yerr_low, yerr_high],
                marker="o",
                linewidth=2,
                capsize=5,
                capthick=1.5,
                elinewidth=1.5,
                color=color,
                label=proto,
            )
        else:
            plt.plot(xs_v, ys_v, marker="o", linewidth=2, color=color, label=proto)

    if not any_data:
        plt.close()
        return False

    plt.xlabel("Interval (s)")
    plt.ylabel(ylabel)
    plt.title(f"Scenario {scenario}: {ylabel} vs interval")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()
    return True


def main() -> None:
    args = parse_args()
    base_dir = Path(args.base_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    requested_metrics = [m.strip() for m in args.metrics.split(",") if m.strip()]
    invalid = [m for m in requested_metrics if m not in METRICS]
    if invalid:
        raise SystemExit(f"Unknown metric(s): {', '.join(invalid)}")

    scenario_filter = set()
    if args.scenarios.strip():
        scenario_filter = {s.strip().upper() for s in args.scenarios.split(",") if s.strip()}

    data = discover_rows(base_dir)
    if not data:
        raise SystemExit("No interval sweep combined_metrics.csv files found")

    # scenario -> protocol -> [(interval, full_row)]
    series: Dict[str, Dict[str, List[Tuple[float, Dict[str, str]]]]] = defaultdict(
        lambda: defaultdict(list)
    )

    for interval, row in data:
        scenario = row.get("scenario", "").strip().upper()
        protocol = row.get("protocol", "").strip().upper()
        if scenario_filter and scenario not in scenario_filter:
            continue
        if protocol not in {"BGP", "SCION"}:
            continue
        series[scenario][protocol].append((interval, row))

    written: List[Path] = []
    for scenario in sorted(series.keys()):
        for metric in requested_metrics:
            out_path = out_dir / f"scenario_{scenario.lower()}__{metric}__vs_interval.png"
            ok = make_plot(series[scenario], scenario, metric, METRICS[metric], out_path)
            if ok:
                written.append(out_path)

    if not written:
        raise SystemExit("No plots generated (no matching rows after filters)")

    for p in written:
        print(p)


if __name__ == "__main__":
    main()
