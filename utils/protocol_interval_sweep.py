#!/usr/bin/env python3
"""Run PCB/clock interval sweeps for SCION and BGP and postprocess jointly.

For each interval value x:
- SCION: run selected scenario config with beacon_service.period = x seconds
- BGP:   run selected scenario with --clock-interval = x seconds

Outputs are stored in variant-tagged directories and then postprocessed with
utils/compare_bgp_scion_results.py using --variant-suffix.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import time
from pathlib import Path
from typing import Dict, List


def run_cmd(cmd: List[str]) -> None:
    result = subprocess.run(cmd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def parse_intervals(s: str) -> List[float]:
    vals: List[float] = []
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        vals.append(float(part))
    return vals


def scenario_config_path(scenario: str) -> Path:
    m = {
        "A": Path("configs/scenario_a_core_link_failure.yaml"),
        "B": Path("configs/scenario_b_dual_link_failure.yaml"),
        "C": Path("configs/scenario_c_recovery_variance.yaml"),
    }
    return m[scenario]


def scenario_failure_times(scenario: str) -> List[float]:
    m = {
        "A": [50.0],
        "B": [50.0],
        "C": [30.0, 320.0, 610.0],
    }
    return m[scenario]


def extract_probe_pairs_from_config_text(cfg_text: str) -> List[tuple]:
    pairs: List[tuple] = []
    src = None
    dst = None
    for line in cfg_text.splitlines():
        m_src = re.match(r"\s*(?:-\s*)?src_as:\s*(\d+)", line)
        if m_src:
            src = int(m_src.group(1))
            continue
        m_dst = re.match(r"\s*(?:-\s*)?dst_as:\s*(\d+)", line)
        if m_dst:
            dst = int(m_dst.group(1))
            if src is not None:
                pairs.append((src, dst))
                src = None
                dst = None
    return pairs


def build_scion_variant_config(
    base_cfg: Path,
    out_cfg: Path,
    run_dir: Path,
    pcb_interval_s: float,
    scenario: str,
    target_duration_s: int = 800,
) -> List[tuple]:
    txt = base_cfg.read_text()

    pairs = extract_probe_pairs_from_config_text(txt)

    txt = re.sub(r"(?m)^\s*period:\s*\d+s\s*$", f"  period: {int(pcb_interval_s)}s", txt, count=1)

    txt = re.sub(r"(?m)^output:\s*build/.*$", f"output: {run_dir}/scion_output.txt", txt, count=1)
    txt = re.sub(r"(?m)^\s*log_file:\s*build/.*$", f"  log_file: {run_dir}/scion_control_plane.log", txt)
    txt = re.sub(r"(?m)^\s*output:\s*build/.*path_snapshots\.csv\s*$", f"  output: {run_dir}/scion_path_snapshots.csv", txt)

    probe_start_s = 10
    probe_interval_s = 1
    probe_end_guard_s = 10
    # Last probe send should be strictly before (simulation_duration - probe_end_guard_s).
    probe_count = int((target_duration_s - probe_end_guard_s - probe_start_s) / probe_interval_s)
    if probe_count < 1:
        probe_count = 1

    txt = re.sub(
        r"(?m)^simulation_duration:\s*\d+s\s*$",
        f"simulation_duration: {target_duration_s}s",
        txt,
        count=1,
    )
    txt = re.sub(
        r"(?m)^\s*last_beaconing:\s*\d+s\s*$",
        f"  last_beaconing: {target_duration_s}s",
        txt,
        count=1,
    )
    txt = re.sub(
        r"(?m)^\s*count:\s*\d+\s*$",
        f"  count: {probe_count}",
        txt,
        count=1,
    )

    out_cfg.parent.mkdir(parents=True, exist_ok=True)
    out_cfg.write_text(txt)
    return pairs


def run_scion_variant(scenario: str, pcb_interval_s: float, build_dir: Path,
                      target_duration_s: int = 800) -> None:
    variant_suffix = f"pcb{int(pcb_interval_s)}_clk{int(pcb_interval_s)}"
    run_dir = build_dir / f"scion_scenario_{scenario.lower()}_{variant_suffix}"
    run_dir.mkdir(parents=True, exist_ok=True)

    base_cfg = scenario_config_path(scenario)
    cfg_path = run_dir / f"scenario_{scenario.lower()}_{variant_suffix}.yaml"
    pairs = build_scion_variant_config(base_cfg, cfg_path, run_dir, pcb_interval_s, scenario,
                                       target_duration_s=target_duration_s)

    run_start = time.time()

    run_cmd(["./waf", "--run", f"scion {cfg_path}"])

    # Copy expected probe CSVs for this scenario into run_dir and aggregate metrics.
    probe_files: List[str] = []
    for src, dst in pairs:
        p = Path("build") / f"scion_probe_{src}_{dst}.csv"
        if p.exists():
            dst_path = run_dir / p.name
            shutil.copy2(p, dst_path)
            probe_files.append(str(dst_path))

    # Fallback: collect recently updated probe files if pair extraction or naming missed some.
    if not probe_files:
        for p_str in sorted(glob.glob("build/scion_probe_*.csv")):
            p = Path(p_str)
            try:
                if p.stat().st_mtime >= run_start - 2.0:
                    dst_path = run_dir / p.name
                    shutil.copy2(p, dst_path)
                    probe_files.append(str(dst_path))
            except OSError:
                continue

    if not probe_files:
        raise RuntimeError(f"No SCION probe files found for scenario {scenario} variant {variant_suffix}")

    # Build scenario metrics JSON in SCION-compatible shape.
    analyzer_py = (
        "import sys;"
        "sys.path.insert(0, 'utils');"
        "from data_plane_analyzer import DataPlaneAnalyzer;"
        "import json;"
        "a=DataPlaneAnalyzer();"
        f"[a.load_probe_csv(p) for p in {probe_files!r}];"
        f"a.export_metrics_json('{str(run_dir / 'scion_data_plane_metrics.json')}', {scenario_failure_times(scenario)!r}, "
        "{101:'core',102:'core',103:'core',104:'core',105:'non-core',106:'non-core',107:'non-core',108:'non-core'});"
        "print('wrote scion metrics json')"
    )
    run_cmd(["python3", "-c", analyzer_py])

    # Generate control-plane events CSV from snapshots fallback.
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


def run_bgp_variant(scenario: str, clock_interval_s: float, build_dir: Path,
                    target_duration_s: int = 800) -> None:
    suffix = f"pcb{int(clock_interval_s)}_clk{int(clock_interval_s)}"
    run_cmd([
        "python3",
        "utils/bgp_scenario_runner.py",
        "--scenarios",
        scenario,
        "--sim-time",
        str(target_duration_s),
        "--output-dir",
        str(build_dir),
        "--clock-interval",
        str(clock_interval_s),
        "--variant-suffix",
        suffix,
    ])


def main() -> None:
    p = argparse.ArgumentParser(description="Sweep SCION PCB interval vs BGP clock interval")
    p.add_argument("--scenarios", default="A", help="Comma-separated scenarios to sweep")
    p.add_argument("--intervals", default="5,10,20", help="Comma-separated interval values (seconds)")
    p.add_argument("--build-dir", default="build", help="Build/output base directory")
    p.add_argument("--skip-scion", action="store_true", help="Skip SCION runs")
    p.add_argument("--skip-bgp", action="store_true", help="Skip BGP runs")
    p.add_argument("--dry-run", action="store_true", help="Print planned runs without executing")
    p.add_argument("--duration", type=int, default=800,
                   help="Simulation duration in seconds (default 800)")
    args = p.parse_args()

    scenarios = [s.strip().upper() for s in args.scenarios.split(",") if s.strip()]
    intervals = parse_intervals(args.intervals)
    build_dir = Path(args.build_dir)

    for s in scenarios:
        if s not in {"A", "B", "C"}:
            raise ValueError(f"Unknown scenario: {s}")

    for interval in intervals:
        suffix = f"pcb{int(interval)}_clk{int(interval)}"

        for scenario in scenarios:
            print(f"\\n=== Sweep scenario {scenario}, interval={interval}s, suffix={suffix} ===")

            if args.dry_run:
                continue

            if not args.skip_scion:
                run_scion_variant(scenario, interval, build_dir,
                                  target_duration_s=args.duration)
            if not args.skip_bgp:
                run_bgp_variant(scenario, interval, build_dir,
                                target_duration_s=args.duration)

        if args.dry_run:
            continue

        # Postprocess once per interval so the combined outputs include all requested scenarios.
        out_dir = build_dir / "protocol_comparison" / suffix
        run_cmd([
            "python3",
            "utils/compare_bgp_scion_results.py",
            "--build-dir",
            str(build_dir),
            "--out-dir",
            str(out_dir),
            "--scenarios",
            ",".join(s.lower() for s in scenarios),
            "--variant-suffix",
            suffix,
        ])


if __name__ == "__main__":
    main()
