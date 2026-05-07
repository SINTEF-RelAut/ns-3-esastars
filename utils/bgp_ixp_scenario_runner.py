#!/usr/bin/env python3
"""Run and analyze the BGP IXP hidden/visible failover scenarios."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


SCENARIOS = {
    "hidden": {
        "out_dir": "bgp_ixp_anycast_hidden_failover",
        "title": "BGP IXP Scenario 1: anycast-like hidden failover",
        "sim_time": 260.0,
    },
    "visible": {
        "out_dir": "bgp_ixp_as110_visible_failover",
        "title": "BGP IXP Scenario 2: visible AS110 failover",
        "sim_time": 260.0,
    },
}


def run_cmd(cmd: list[str]) -> None:
    result = subprocess.run(cmd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def write_summary(summary_path: Path, title: str, metrics_json: Path, cp_summary_csv: Path) -> None:
    scenario_stats = {}
    packet_loss = {}
    if metrics_json.exists():
        with metrics_json.open() as f:
            doc = json.load(f)
        scenario_stats = doc.get("scenario_stats", {})
        packet_loss = doc.get("scenario_packet_loss", {})

    cp_rows: list[dict[str, str]] = []
    if cp_summary_csv.exists():
        with cp_summary_csv.open(newline="") as f:
            cp_rows = list(csv.DictReader(f))

    with summary_path.open("w") as f:
        f.write(f"# {title}\n\n")
        f.write("## Data Plane\n\n")
        f.write("| Metric | Value |\n")
        f.write("|---|---:|\n")
        for metric in ["count", "mean", "p50", "p95", "p99", "min", "max", "stdev"]:
            f.write(f"| {metric} | {scenario_stats.get(metric, 'N/A')} |\n")
        f.write(f"| packet_loss_percent | {packet_loss.get('loss_percent', 'N/A')} |\n")

        f.write("\n## Control Plane Windows\n\n")
        if not cp_rows:
            f.write("No control-plane summary rows were generated.\n")
            return

        f.write("| failed_link | link_down_time_s | link_up_time_s | OPEN | UPDATE_announce | UPDATE_withdraw | STATE_CHANGE |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|\n")
        for row in cp_rows:
            f.write(
                f"| {row.get('failed_link', 'N/A')} | {row.get('link_down_time_s', 'N/A')} | "
                f"{row.get('link_up_time_s', 'N/A')} | {row.get('OPEN', '0')} | "
                f"{row.get('UPDATE_announce', '0')} | {row.get('UPDATE_withdraw', '0')} | "
                f"{row.get('STATE_CHANGE', '0')} |\n"
            )


def run_scenario(name: str, output_dir: Path, clock_interval_s: float) -> Path:
    config = SCENARIOS[name]
    run_dir = output_dir / config["out_dir"]
    run_dir.mkdir(parents=True, exist_ok=True)

    run_cmd(
        [
            "./waf",
            "--run",
            (
                "bgp-ixp-scenarios "
                f"--scenario={name} "
                f"--outDir={run_dir} "
                f"--clockInterval={clock_interval_s} "
                f"--simTime={config['sim_time']}"
            ),
        ]
    )

    metrics_csv = run_dir / "metrics.csv"
    cp_summary_csv = run_dir / "cp_summary.csv"
    metrics_json = run_dir / "bgp_data_plane_metrics.json"

    run_cmd(
        [
            "python3",
            "utils/bgp_convergence_analysis.py",
            "--dir",
            str(run_dir),
            "--out-csv",
            str(metrics_csv),
            "--cp-summary-csv",
            str(cp_summary_csv),
            "--out-json",
            str(metrics_json),
            "--core-ases",
            "101,102,103,104,105,106,107,108,110",
        ]
    )

    write_summary(run_dir / "summary.md", config["title"], metrics_json, cp_summary_csv)
    return run_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="Run BGP IXP hidden/visible scenarios")
    parser.add_argument(
        "--scenarios",
        default="hidden,visible",
        help="Comma-separated scenario set: hidden, visible",
    )
    parser.add_argument("--output-dir", default="build", help="Base output directory")
    parser.add_argument(
        "--clock-interval",
        type=float,
        default=1.0,
        help="BGP clock interval in seconds",
    )
    args = parser.parse_args()

    selected = [s.strip().lower() for s in args.scenarios.split(",") if s.strip()]
    unknown = [s for s in selected if s not in SCENARIOS]
    if unknown:
        raise SystemExit(f"Unknown scenarios: {', '.join(unknown)}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    for scenario in selected:
        run_dir = run_scenario(scenario, output_dir, args.clock_interval)
        print(run_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())