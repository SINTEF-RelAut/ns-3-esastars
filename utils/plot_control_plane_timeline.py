#!/usr/bin/env python3
"""Plot control-plane message timelines for BGP and SCION.

For BGP: reads raw per-AS event logs (bgp_cp_as*.csv) and bins events over time.
For SCION: reads scion_control_plane_events.csv and bins events over time.
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Plot control-plane message timelines"
    )
    p.add_argument(
        "--base-dir",
        default="build",
        help="Base directory containing BGP/SCION output subdirectories",
    )
    p.add_argument(
        "--out-dir",
        default="build/protocol_comparison/timeline_plots",
        help="Output directory for timeline plots",
    )
    p.add_argument(
        "--scenarios",
        default="A,B,C",
        help="Comma-separated scenarios to plot (A,B,C)",
    )
    p.add_argument(
        "--bin-size-s",
        type=float,
        default=1.0,
        help="Bin size in seconds for event aggregation",
    )
    p.add_argument(
        "--interval",
        type=int,
        default=5,
        help="Interval variant to plot (uses *_pcbN_clkN outputs)",
    )
    return p.parse_args()


def _bin_time(ts: float, bin_size_s: float) -> float:
    return int(ts / bin_size_s) * bin_size_s


def read_bgp_cp_timeline(cp_csv_paths: Iterable[Path], bin_size_s: float) -> Dict[float, int]:
    """Read raw BGP per-AS control-plane event logs and bin them by time."""
    timeline: Dict[float, int] = defaultdict(int)
    for cp_csv_path in cp_csv_paths:
        if not cp_csv_path.exists():
            continue
        with cp_csv_path.open("r", newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                ts_str = (row.get("time_s", "") or "").strip()
                try:
                    ts = float(ts_str)
                except (ValueError, TypeError):
                    continue
                timeline[_bin_time(ts, bin_size_s)] += 1
    return dict(timeline)


def read_scion_cp_timeline(
    cp_csv_path: Path, bin_size_s: float
) -> Dict[float, int]:
    """Read SCION scion_control_plane_events.csv and bin events by time."""
    timeline = defaultdict(int)
    if not cp_csv_path.exists():
        return {}

    with cp_csv_path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ts_str = (row.get("timestamp_s", "") or "").strip()
            try:
                ts = float(ts_str)
            except (ValueError, TypeError):
                continue
            timeline[_bin_time(ts, bin_size_s)] += 1

    return dict(timeline)


def discover_bgp_cp_files(base_dir: Path, scenario: str, interval: int) -> list[Path]:
    """Locate raw BGP per-AS control-plane CSVs for a given scenario and interval."""
    run_dir = base_dir / f"bgp_scenario_{scenario.lower()}_pcb{interval}_clk{interval}"
    if not run_dir.exists():
        return []
    return sorted(run_dir.glob("bgp_cp_as*.csv"))


def discover_scion_cp_file(base_dir: Path, scenario: str, interval: int) -> Path | None:
    """Locate SCION scion_control_plane_events.csv for a given scenario and interval."""
    candidates = [
        base_dir / f"scion_scenario_{scenario.lower()}_pcb{interval}_clk{interval}" / "scion_control_plane_events.csv",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def make_timeline_plot(
    bgp_timeline: Dict[float, int],
    scion_timeline: Dict[float, int],
    scenario: str,
    out_path: Path,
    sim_duration_s: float,
) -> bool:
    """Generate a timeline plot showing cumulative or binned message counts."""
    if not bgp_timeline and not scion_timeline:
        return False

    fig, ax = plt.subplots(figsize=(10, 5))

    if scion_timeline:
        times_s = sorted(scion_timeline.keys())
        counts = [scion_timeline[t] for t in times_s]
        ax.plot(
            times_s, counts,
            marker=".", linewidth=1.5, color="#1f77b4", label="SCION"
        )

    if bgp_timeline:
        times_s = sorted(bgp_timeline.keys())
        counts = [bgp_timeline[t] for t in times_s]
        ax.plot(
            times_s, counts,
            marker=".", linewidth=1.5, color="#d62728", label="BGP"
        )

    ax.set_xlabel("Simulation Time (s)")
    ax.set_ylabel("Messages per bin")
    ax.set_title(f"Scenario {scenario}: Control-Plane Message Timeline")
    ax.grid(True, alpha=0.3)
    ax.legend()
    ax.set_xlim(left=0, right=sim_duration_s)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    return True


def main() -> None:
    args = parse_args()
    base_dir = Path(args.base_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    scenario_list = [s.strip().upper() for s in args.scenarios.split(",") if s.strip()]
    
    interval = args.interval

    written = []
    for scenario in scenario_list:
        bgp_cp_files = discover_bgp_cp_files(base_dir, scenario, interval)
        scion_cp = discover_scion_cp_file(base_dir, scenario, interval)

        bgp_timeline = read_bgp_cp_timeline(bgp_cp_files, args.bin_size_s) if bgp_cp_files else {}
        scion_timeline = read_scion_cp_timeline(scion_cp, args.bin_size_s) if scion_cp else {}

        out_path = out_dir / f"scenario_{scenario.lower()}_control_plane_timeline.png"
        # Determine simulation duration based on scenario
        sim_duration = {"A": 240, "B": 240, "C": 300}.get(scenario, 240)
        
        if make_timeline_plot(bgp_timeline, scion_timeline, scenario, out_path, sim_duration):
            written.append(out_path)

        if scenario == "A":
            if bgp_timeline:
                bgp_only_path = out_dir / "cp_messages_timeline.png"
                if make_timeline_plot(bgp_timeline, {}, scenario, bgp_only_path, sim_duration):
                    written.append(bgp_only_path)
            if scion_timeline:
                scion_only_path = out_dir / "scenario_a_control_plane.png"
                if make_timeline_plot({}, scion_timeline, scenario, scion_only_path, sim_duration):
                    written.append(scion_only_path)

    if not written:
        print(
            "Warning: No timeline plots generated (no matching event CSV files found)",
            file=__import__("sys").stderr,
        )
    else:
        for p in written:
            print(p)


if __name__ == "__main__":
    main()
