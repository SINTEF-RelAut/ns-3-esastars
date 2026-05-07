#!/usr/bin/env python3
"""Run full BGP Scenario A with and without virtual IXP and compare metrics."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from bgp_scenario_runner import run_scenario


def write_summary(summary_path: Path, baseline_json: Path, ixp_json: Path) -> None:
    baseline_stats = {}
    ixp_stats = {}

    if baseline_json.exists():
        baseline_stats = json.loads(baseline_json.read_text()).get("scenario_stats", {})
    if ixp_json.exists():
        ixp_stats = json.loads(ixp_json.read_text()).get("scenario_stats", {})

    with summary_path.open("w") as f:
        f.write("# BGP Scenario A: Baseline vs Virtual IXP (full pipeline)\n\n")
        f.write("| Metric | Baseline (no IXP) | Virtual IXP |\n")
        f.write("|---|---:|---:|\n")
        for metric in ["count", "mean", "p50", "p95", "p99", "min", "max", "stdev"]:
            b = baseline_stats.get(metric, "N/A")
            i = ixp_stats.get(metric, "N/A")
            f.write(f"| {metric} | {b} | {i} |\n")


def main() -> int:
    p = argparse.ArgumentParser(description="Run full BGP Scenario A baseline and virtual IXP")
    p.add_argument("--output-dir", default="build", help="Base output directory")
    p.add_argument("--clock-interval", type=float, default=5.0, help="BGP clock interval in seconds")
    p.add_argument("--duration", type=float, default=800.0, help="Scenario duration in seconds")
    p.add_argument("--skip-baseline", action="store_true", help="Skip baseline run")
    p.add_argument("--virtual-ixp-port-count", type=int, default=2, help="Virtual IXP port count")
    p.add_argument("--virtual-ixp-rebalance", type=float, default=0.25,
                   help="Virtual IXP rebalance period in seconds")
    p.add_argument("--virtual-ixp-hold-down", type=float, default=0.0,
                   help="Virtual IXP hold-down in seconds")
    args = p.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    base_suffix = f"pcb{int(args.clock_interval)}_clk{int(args.clock_interval)}"
    ixp_suffix = f"{base_suffix}_vixp"

    baseline_dir = output_dir / f"bgp_scenario_a_{base_suffix}"
    ixp_dir = output_dir / f"bgp_scenario_a_{ixp_suffix}"

    if not args.skip_baseline:
        print("Running BGP baseline Scenario A (no IXP)...")
        run_scenario(
            "A",
            output_dir,
            clock_interval_s=args.clock_interval,
            variant_suffix=base_suffix,
            sim_time_s=args.duration,
            use_virtual_ixp=False,
        )

    print("Running BGP full virtual-IXP Scenario A variant...")
    run_scenario(
        "A",
        output_dir,
        clock_interval_s=args.clock_interval,
        variant_suffix=ixp_suffix,
        sim_time_s=args.duration,
        use_virtual_ixp=True,
        virtual_ixp_port_count=args.virtual_ixp_port_count,
        virtual_ixp_rebalance_s=args.virtual_ixp_rebalance,
        virtual_ixp_hold_down_s=args.virtual_ixp_hold_down,
    )

    if baseline_dir.exists():
        for fn in ["cp_summary.csv", "bgp_data_plane_metrics.json"]:
            src = baseline_dir / fn
            if src.exists():
                shutil.copy2(src, ixp_dir / f"baseline_{fn}")

    summary_path = ixp_dir / "comparison_summary.md"
    write_summary(summary_path, baseline_dir / "bgp_data_plane_metrics.json", ixp_dir / "bgp_data_plane_metrics.json")

    print(f"Wrote BGP baseline dir: {baseline_dir}")
    print(f"Wrote BGP IXP dir: {ixp_dir}")
    print(f"Wrote summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
