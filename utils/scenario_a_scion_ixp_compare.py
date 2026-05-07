#!/usr/bin/env python3
"""Run full SCION Scenario A with and without virtual IXP and compare metrics."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from protocol_interval_sweep import (
    build_scion_variant_config,
    run_cmd,
    scenario_config_path,
    scenario_failure_times,
)


def run_scion_variant_ixp(interval_s: float, build_dir: Path, target_duration_s: int = 800) -> Path:
    variant_suffix = f"pcb{int(interval_s)}_clk{int(interval_s)}_vixp"
    run_dir = build_dir / f"scion_scenario_a_{variant_suffix}"
    run_dir.mkdir(parents=True, exist_ok=True)

    base_cfg = scenario_config_path("A")
    cfg_path = run_dir / f"scenario_a_{variant_suffix}.yaml"
    pairs = build_scion_variant_config(
        base_cfg,
        cfg_path,
        run_dir,
        interval_s,
        "A",
        target_duration_s=target_duration_s,
    )

    cfg_txt = cfg_path.read_text()
    cfg_txt += (
        "\nvirtual_ixp:\n"
        "  enabled: true\n"
        "  port_count: 2\n"
        "  rebalance_period: 250ms\n"
        "  hold_down: 0s\n"
        "  links:\n"
        "    - real_as: 101\n"
        "      if_id: 1010000\n"
        "      quality: 1.0\n"
    )
    cfg_path.write_text(cfg_txt)

    run_cmd(["./waf", "--run", f"scion {cfg_path}"])

    probe_files = []
    for src, dst in pairs:
        p = Path("build") / f"scion_probe_{src}_{dst}.csv"
        if p.exists():
            dst_path = run_dir / p.name
            shutil.copy2(p, dst_path)
            probe_files.append(str(dst_path))

    if not probe_files:
        raise RuntimeError("No SCION probe files found for virtual IXP variant")

    analyzer_py = (
        "import sys;"
        "sys.path.insert(0, 'utils');"
        "from data_plane_analyzer import DataPlaneAnalyzer;"
        "a=DataPlaneAnalyzer();"
        f"[a.load_probe_csv(p) for p in {probe_files!r}];"
        f"a.export_metrics_json('{str(run_dir / 'scion_data_plane_metrics.json')}',"
        f" {scenario_failure_times('A')!r},"
        " {101:'core',102:'core',103:'core',104:'core',105:'non-core',106:'non-core',107:'non-core',108:'non-core'},"
        " align_with_bgp=True);"
    )
    run_cmd(["python3", "-c", analyzer_py])

    snapshots = run_dir / "scion_path_snapshots.csv"
    if snapshots.exists():
        run_cmd([
            "python3",
            "utils/control_plane_log_parser.py",
            "--snapshots",
            str(snapshots),
        ])
        default_csv = Path("build/control_plane_events.csv")
        if default_csv.exists():
            shutil.copy2(default_csv, run_dir / "scion_control_plane_events.csv")

    return run_dir


def write_summary(summary_path: Path, baseline_json: Path, ixp_json: Path) -> None:
    baseline_stats = {}
    ixp_stats = {}

    if baseline_json.exists():
        baseline_stats = json.loads(baseline_json.read_text()).get("scenario_stats", {})
    if ixp_json.exists():
        ixp_stats = json.loads(ixp_json.read_text()).get("scenario_stats", {})

    with summary_path.open("w") as f:
        f.write("# SCION Scenario A: Baseline vs Virtual IXP (full pipeline)\n\n")
        f.write("| Metric | Baseline (no IXP) | Virtual IXP |\n")
        f.write("|---|---:|---:|\n")
        for metric in ["count", "mean", "p50", "p95", "p99", "min", "max", "stdev"]:
            b = baseline_stats.get(metric, "N/A")
            i = ixp_stats.get(metric, "N/A")
            f.write(f"| {metric} | {b} | {i} |\n")


def main() -> int:
    p = argparse.ArgumentParser(description="Run full SCION Scenario A baseline and virtual IXP")
    p.add_argument("--output-dir", default="build", help="Base output directory")
    p.add_argument("--interval", type=int, default=5, help="SCION beacon period in seconds")
    p.add_argument("--duration", type=int, default=800, help="Scenario duration in seconds")
    p.add_argument("--skip-baseline", action="store_true", help="Skip baseline run")
    args = p.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    suffix = f"pcb{args.interval}_clk{args.interval}"
    baseline_dir = output_dir / f"scion_scenario_a_{suffix}"

    if not args.skip_baseline:
        from protocol_interval_sweep import run_scion_variant

        print("Running SCION baseline Scenario A (no IXP)...")
        run_scion_variant("A", float(args.interval), output_dir, target_duration_s=args.duration)

    print("Running SCION full virtual-IXP Scenario A variant...")
    ixp_dir = run_scion_variant_ixp(float(args.interval), output_dir, target_duration_s=args.duration)

    if baseline_dir.exists():
        for fn in ["scion_control_plane_events.csv", "scion_data_plane_metrics.json"]:
            src = baseline_dir / fn
            if src.exists():
                shutil.copy2(src, ixp_dir / f"baseline_{fn}")

    summary_path = ixp_dir / "comparison_summary.md"
    write_summary(summary_path, baseline_dir / "scion_data_plane_metrics.json", ixp_dir / "scion_data_plane_metrics.json")

    print(f"Wrote SCION baseline dir: {baseline_dir}")
    print(f"Wrote SCION IXP dir: {ixp_dir}")
    print(f"Wrote summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
