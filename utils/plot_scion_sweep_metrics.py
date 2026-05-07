#!/usr/bin/env python3
"""Plot SCION sweep metrics (convergence time, CP overhead, packet loss) vs timer settings.

Reads sweep outputs matching:
  build/sweep_scion_{scenario}_mrai{X}_clk{Y}_bcn{Z}/

For each run it derives:
  - Convergence time (s): mean control-plane quiescence after each link_down event,
    using path-snapshot changes as the control-plane signal.
  - CP overhead: total number of path-snapshot change events.
  - Packet loss (%): timeout fraction across probe pairs with >= 1% reply rate.

The raw SCION sweep results are present in the workspace. What is typically missing is the
post-processed scion_control_plane_events.csv file per run; this script reconstructs the same
signal directly from the path snapshots, so no rerun is required.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
from pathlib import Path
from typing import Dict, List, Optional

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

STARTUP_CUTOFF_S = 10.0
MIN_REPLY_RATE = 0.01
SCION_COLOR = "#1f77b4"
SCION_MARKER = "o"

_DIR_RE = re.compile(
    r"sweep_scion_(?P<scenario>visible|hidden)_mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_bcn(?P<bcn>\d+)"
)


def load_csv_rows(path: Path) -> List[Dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def discover_runs(build_dir: Path, scenario: str) -> Dict[int, Path]:
    runs: Dict[int, Path] = {}
    for path in sorted(build_dir.iterdir()):
        if not path.is_dir():
            continue
        match = _DIR_RE.fullmatch(path.name)
        if not match or match.group("scenario") != scenario:
            continue
        beacon_s = int(match.group("bcn"))
        runs[beacon_s] = path
    return runs


def parse_events_file_from_config(config_path: Path) -> Optional[Path]:
    if not config_path.exists():
        return None
    for line in config_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("events_file:"):
            rel = stripped.split(":", 1)[1].strip()
            return Path(rel)
    return None


def load_link_down_times(events_json: Path) -> List[float]:
    if not events_json.exists():
        return []
    data = json.loads(events_json.read_text(encoding="utf-8"))
    seen = set()
    times: List[float] = []
    for event in data.get("events", []):
        if event.get("type") != "link_down":
            continue
        raw_time = str(event.get("time", "")).strip().removesuffix("s")
        try:
            time_s = float(raw_time)
        except ValueError:
            continue
        if time_s < STARTUP_CUTOFF_S or time_s in seen:
            continue
        seen.add(time_s)
        times.append(time_s)
    times.sort()
    return times


def derive_snapshot_change_times(snapshot_csv: Path) -> List[float]:
    if not snapshot_csv.exists():
        return []
    previous_valid_paths: Dict[tuple[int, int], int] = {}
    change_times: List[float] = []
    for row in load_csv_rows(snapshot_csv):
        try:
            time_s = float(row["time_s"])
            src_as = int(row["src_as"])
            dst_as = int(row["dst_as"])
            valid_paths = int(row["valid_paths"])
        except (KeyError, ValueError):
            continue
        key = (src_as, dst_as)
        prev = previous_valid_paths.get(key)
        previous_valid_paths[key] = valid_paths
        if prev is None or prev == valid_paths:
            continue
        change_times.append(time_s)
    change_times.sort()
    return change_times


def compute_cp_overhead(run_dir: Path) -> int:
    snapshot_csv = Path(f"{run_dir}_path_snapshots.csv")
    return len(derive_snapshot_change_times(snapshot_csv))


def compute_convergence_time(run_dir: Path, scenario: str, repo_root: Path) -> Optional[float]:
    snapshot_csv = Path(f"{run_dir}_path_snapshots.csv")
    change_times = derive_snapshot_change_times(snapshot_csv)
    if not change_times:
        return None

    config_path = repo_root / "configs" / f"{run_dir.name}.yaml"
    events_file = parse_events_file_from_config(config_path)
    if events_file is None:
        return None
    link_down_times = load_link_down_times(repo_root / events_file)
    if not link_down_times:
        return None

    convergence_times: List[float] = []
    for index, t_down in enumerate(link_down_times):
        t_next = link_down_times[index + 1] if index + 1 < len(link_down_times) else t_down + 300.0
        lo = bisect.bisect_left(change_times, t_down)
        hi = bisect.bisect_left(change_times, t_next)
        if lo >= hi:
            continue
        convergence_times.append(change_times[hi - 1] - t_down)

    if not convergence_times:
        return None
    return sum(convergence_times) / len(convergence_times)


def compute_packet_loss(run_dir: Path) -> Optional[float]:
    total_reply = 0
    total_timeout = 0
    for probe_path in sorted(run_dir.glob("scion_probe_*.csv")):
        rows = load_csv_rows(probe_path)
        events = [row.get("event", "").strip() for row in rows]
        sent = sum(1 for event in events if event == "sent")
        reply = sum(1 for event in events if event == "reply")
        timeout = sum(1 for event in events if event == "timeout")
        if sent == 0 or reply / sent < MIN_REPLY_RATE:
            continue
        total_reply += reply
        total_timeout += timeout
    total_terminal = total_reply + total_timeout
    if total_terminal == 0:
        return None
    return 100.0 * total_timeout / total_terminal


def save_line_plot(
    data: Dict[int, Optional[float]],
    ylabel: str,
    title: str,
    out_path: Path,
) -> None:
    xs = sorted(x for x, y in data.items() if y is not None)
    ys = [data[x] for x in xs]
    fig, ax = plt.subplots(figsize=(7, 4))
    if xs:
        ax.plot(
            xs,
            ys,
            marker=SCION_MARKER,
            color=SCION_COLOR,
            linewidth=1.8,
            markersize=6,
            label="SCION",
        )
        ax.legend(framealpha=0.85)
    ax.set_xlabel("Beacon / expiration timer setting (s)")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, linestyle="--", alpha=0.5)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"  Saved {out_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot SCION sweep metrics vs timer settings.")
    parser.add_argument(
        "--scenario",
        choices=["visible", "hidden"],
        default="visible",
        help="Which sweep scenario to plot (default: visible)",
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory containing sweep outputs (default: build)",
    )
    parser.add_argument(
        "--out-dir",
        default="build/scion_sweep_metric_plots",
        help="Directory for output PNGs (default: build/scion_sweep_metric_plots)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = repo_root / args.build_dir
    out_dir = repo_root / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = discover_runs(build_dir, args.scenario)
    if not runs:
        print(f"No SCION sweep directories found in {build_dir} for scenario={args.scenario}.")
        return 1

    print(f"Found SCION sweep runs for scenario={args.scenario}:")
    for beacon_s, run_dir in sorted(runs.items()):
        print(f"  beacon={beacon_s:2d}s  {run_dir.name}")

    convergence_data: Dict[int, Optional[float]] = {}
    cp_overhead_data: Dict[int, Optional[float]] = {}
    packet_loss_data: Dict[int, Optional[float]] = {}

    for beacon_s, run_dir in sorted(runs.items()):
        print(f"  Computing metrics for {run_dir.name} ...")
        convergence_data[beacon_s] = compute_convergence_time(run_dir, args.scenario, repo_root)
        cp_overhead_data[beacon_s] = float(compute_cp_overhead(run_dir))
        packet_loss_data[beacon_s] = compute_packet_loss(run_dir)

    scenario_label = args.scenario.capitalize()
    print("\nPlotting ...")
    save_line_plot(
        convergence_data,
        ylabel="Mean convergence time (s)",
        title=f"SCION convergence time vs timer setting - {scenario_label}",
        out_path=out_dir / f"scion_{args.scenario}_convergence_time.png",
    )
    save_line_plot(
        cp_overhead_data,
        ylabel="Path change events",
        title=f"SCION CP overhead vs timer setting - {scenario_label}",
        out_path=out_dir / f"scion_{args.scenario}_cp_overhead.png",
    )
    save_line_plot(
        packet_loss_data,
        ylabel="Packet loss (%)",
        title=f"SCION packet loss vs timer setting - {scenario_label}",
        out_path=out_dir / f"scion_{args.scenario}_packet_loss.png",
    )

    print(f"\n{'Beacon(s)':>10}  {'Conv(s)':>10}  {'CP events':>10}  {'Loss%':>8}")
    print("-" * 46)
    for beacon_s in sorted(runs):
        conv = convergence_data.get(beacon_s)
        cp = cp_overhead_data.get(beacon_s)
        loss = packet_loss_data.get(beacon_s)
        conv_text = f"{conv:.2f}" if conv is not None else "n/a"
        cp_text = f"{cp:.0f}" if cp is not None else "n/a"
        loss_text = f"{loss:.2f}" if loss is not None else "n/a"
        print(f"{beacon_s:>10}  {conv_text:>10}  {cp_text:>10}  {loss_text:>8}")

    print(f"\nPlots written to {out_dir}")
    print("Note: raw SCION sweep outputs were present; only the derived per-run control-plane CSVs were missing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
