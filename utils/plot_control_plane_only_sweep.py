#!/usr/bin/env python3
"""Plot control-plane-only convergence metrics with interval on x-axis.

Input: control_plane_only_metrics.csv (from control_plane_sweep_table.py)
Output: Line plots for each metric, showing BGP vs SCION across intervals.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


METRICS = {
    "cp_total": "Control-Plane Total Events",
    "cp_propagation": "Control-Plane Propagation Events",
    "cp_exploration": "Control-Plane Exploration Events",
    "cp_prop_to_explore_ratio": "Propagation/Exploration Ratio",
    "cp_detection_delay_mean_s": "Mean Detection Delay (s)",
    "cp_event_convergence_mean_s": "Mean Event Convergence (s)",
    "cp_quiet_convergence_mean_s": "Mean Quiet Convergence (s)",
    "best_path_return_mean_s": "Mean Best-Path Return Latency (s)",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Plot control-plane-only metrics vs interval"
    )
    p.add_argument(
        "--csv",
        default="build/protocol_comparison/control_plane_only_metrics.csv",
        help="Input control-plane metrics CSV",
    )
    p.add_argument(
        "--out-dir",
        default="build/protocol_comparison/control_plane_only_plots",
        help="Output directory for plots",
    )
    p.add_argument(
        "--metrics",
        default="cp_total,cp_propagation,cp_exploration,cp_prop_to_explore_ratio,cp_detection_delay_mean_s,cp_event_convergence_mean_s,cp_quiet_convergence_mean_s,best_path_return_mean_s",
        help="Comma-separated metric columns to plot",
    )
    p.add_argument(
        "--scenarios",
        default="",
        help="Optional comma-separated scenario filter (e.g. A,B,C)",
    )
    return p.parse_args()


def to_float(v: str) -> float | None:
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def discover_rows(csv_path: Path) -> List[Tuple[float, Dict[str, str]]]:
    """Load CSV and return (interval_value, row_dict) tuples."""
    rows: List[Tuple[float, Dict[str, str]]] = []
    if not csv_path.exists():
        return rows

    with csv_path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                interval = float(row.get("interval_s", 0) or 0)
                rows.append((interval, row))
            except (ValueError, TypeError):
                continue

    return sorted(rows, key=lambda x: x[0])


def make_plot(
    protocol_rows: Dict[str, List[Tuple[float, Dict[str, str]]]],
    scenario: str,
    metric: str,
    ylabel: str,
    out_path: Path,
) -> bool:
    """Generate line plot for a single metric across intervals."""
    protocols = [
        ("BGP", "#d62728"),
        ("SCION", "#1f77b4"),
    ]

    any_data = False
    plt.figure(figsize=(8, 5))

    for proto, color in protocols:
        pts = sorted(protocol_rows.get(proto, []), key=lambda x: x[0])
        if not pts:
            continue

        xs = [interval for interval, _ in pts]
        ys_raw = [to_float(row.get(metric)) for _, row in pts]
        # Drop intervals where metric is absent or non-numeric
        valid = [(x, y) for (x, _), y in zip(pts, ys_raw) if y is not None]
        
        if not valid:
            continue

        any_data = True
        xs_v = [x for x, _ in valid]
        ys_v = [y for _, y in valid]

        plt.plot(xs_v, ys_v, marker="o", linewidth=2, color=color, label=proto)

    if not any_data:
        plt.close()
        return False

    plt.xlabel("Interval (s)")
    plt.ylabel(ylabel)
    plt.title(f"Scenario {scenario}: {ylabel} vs Interval")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()
    return True


def main() -> None:
    args = parse_args()
    csv_path = Path(args.csv)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    requested_metrics = [m.strip() for m in args.metrics.split(",") if m.strip()]
    invalid = [m for m in requested_metrics if m not in METRICS]
    if invalid:
        raise SystemExit(f"Unknown metric(s): {', '.join(invalid)}")

    scenario_filter = set()
    if args.scenarios.strip():
        scenario_filter = {s.strip().upper() for s in args.scenarios.split(",") if s.strip()}

    data = discover_rows(csv_path)
    if not data:
        raise SystemExit(f"No rows found in {csv_path}")

    # scenario -> protocol -> [(interval, row_dict)]
    series: Dict[str, Dict[str, List[Tuple[float, Dict[str, str]]]]] = defaultdict(
        lambda: defaultdict(list)
    )

    for interval, row in data:
        scenario = (row.get("scenario") or "").strip().upper()
        protocol = (row.get("protocol") or "").strip().upper()
        
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
