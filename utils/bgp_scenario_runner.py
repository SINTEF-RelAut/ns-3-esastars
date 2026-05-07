#!/usr/bin/env python3
"""Run BGP scenarios A/B/C and export SCION-compatible metrics outputs.

This runner executes `bgp-convergence-first` with scenario-specific settings,
then runs `bgp_convergence_analysis.py` to produce:
- metrics.csv
- cp_summary.csv
- bgp_data_plane_metrics.json (SCION-compatible shape)
- scenario_<x>_bgp_summary.md
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import subprocess
from pathlib import Path
from typing import Dict, List


SCENARIOS = {
    "A": {
        "name": "Core Link Failure (AS101-AS102)",
        "failure_times": [50.0],
    },
    "B": {
        "name": "Dual Core Link Failure",
        "failure_times": [50.0, 50.0],
    },
    "C": {
        "name": "Recovery Latency Variance",
        "failure_times": [30.0, 320.0, 610.0],
    },
}

_PROBE_START_S = 10.0
_PROBE_END_GUARD_S = 10.0
_PROBE_INTERVAL_S = 1.0


def expected_probe_count(sim_time_s: float) -> int:
    last_send = sim_time_s - _PROBE_END_GUARD_S
    return max(1, int((last_send - _PROBE_START_S) / _PROBE_INTERVAL_S))


def run_cmd(cmd: List[str]) -> None:
    result = subprocess.run(cmd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def outputs_look_complete(out_dir: Path, expected_probe_count: int) -> bool:
    link_events = out_dir / "link_events.csv"
    if not link_events.exists():
        return False

    probe_files = sorted(glob.glob(str(out_dir / "probe_*.csv")))
    if not probe_files:
        return False

    # Quick completion check: first probe file should contain last sequence index.
    target_seq = expected_probe_count - 1
    probe_path = probe_files[0]
    last_seq = None
    with open(probe_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                last_seq = int(row["seq"])
            except (KeyError, ValueError):
                continue

    return last_seq is not None and last_seq >= target_seq


def run_bgp_binary(scenario: str, out_dir: Path, probe_count: int,
                   clock_interval_s: float, sim_time_s: float = 800.0,
                   use_virtual_ixp: bool = False,
                   virtual_ixp_port_count: int = 2,
                   virtual_ixp_rebalance_s: float = 0.25,
                   virtual_ixp_hold_down_s: float = 0.0) -> None:
    """Run bgp-convergence-first directly (more stable than waf --run for these scenarios)."""
    binary = Path("build/src/bgp/examples/ns3.30.1-bgp-convergence-first-debug")
    if not binary.exists():
        # Ensure binary exists first.
        run_cmd(["./waf"])

    env = os.environ.copy()
    build_lib = str(Path("build/lib"))
    current_ld = env.get("LD_LIBRARY_PATH", "")
    env["LD_LIBRARY_PATH"] = build_lib if not current_ld else f"{build_lib}:{current_ld}"

    cmd = [
        str(binary),
        f"--scenario={scenario}",
        f"--outDir={out_dir}",
        f"--clockInterval={clock_interval_s}",
        f"--simTime={sim_time_s}",
        f"--virtualIxp={1 if use_virtual_ixp else 0}",
        f"--virtualIxpPortCount={virtual_ixp_port_count}",
        f"--virtualIxpRebalance={virtual_ixp_rebalance_s}",
        f"--virtualIxpHoldDown={virtual_ixp_hold_down_s}",
    ]
    result = subprocess.run(cmd, text=True, env=env)
    if result.returncode == 0 and outputs_look_complete(out_dir, probe_count):
        return

    # Some scenarios are flaky outside debugger; retry once under gdb.
    gdb_cmd = [
        "gdb",
        "-batch",
        "-ex",
        "run",
        "-ex",
        "bt",
        "--args",
        str(binary),
        f"--scenario={scenario}",
        f"--outDir={out_dir}",
        f"--clockInterval={clock_interval_s}",
        f"--simTime={sim_time_s}",
        f"--virtualIxp={1 if use_virtual_ixp else 0}",
        f"--virtualIxpPortCount={virtual_ixp_port_count}",
        f"--virtualIxpRebalance={virtual_ixp_rebalance_s}",
        f"--virtualIxpHoldDown={virtual_ixp_hold_down_s}",
    ]
    gdb_result = subprocess.run(gdb_cmd, text=True, env=env, capture_output=True)
    if outputs_look_complete(out_dir, probe_count):
        print("Simulation completed via gdb fallback after non-zero direct run")
        return

    raise RuntimeError(
        f"Command failed ({result.returncode}): {' '.join(cmd)}\n"
        f"gdb fallback exit={gdb_result.returncode}\n"
        f"gdb stderr:\n{gdb_result.stderr[-1000:]}"
    )


def write_summary_md(scenario: str, scenario_name: str, metrics_json: Path, out_md: Path) -> None:
    stats = {}
    if metrics_json.exists():
        with open(metrics_json) as f:
            doc = json.load(f)
        stats = doc.get("scenario_stats", {})

    with open(out_md, "w") as f:
        f.write(f"# BGP Scenario Summary: {scenario_name} (Scenario {scenario})\n\n")
        f.write("| Metric | BGP |\n")
        f.write("|--------|-----|\n")
        f.write(f"| Recovery Latency (mean) | {stats.get('mean', 'N/A')} s |\n")
        f.write(f"| Recovery Latency (p50) | {stats.get('p50', 'N/A')} s |\n")
        f.write(f"| Recovery Latency (p95) | {stats.get('p95', 'N/A')} s |\n")
        f.write(f"| Recovery Latency (p99) | {stats.get('p99', 'N/A')} s |\n")
        f.write(f"| Recovery Latency (max) | {stats.get('max', 'N/A')} s |\n")
        f.write(f"| Samples | {stats.get('count', 'N/A')} |\n")


def run_scenario(scenario: str, base_output: Path,
                 clock_interval_s: float = 1.0,
                 variant_suffix: str = "",
                 sim_time_s: float = 800.0,
                 use_virtual_ixp: bool = False,
                 virtual_ixp_port_count: int = 2,
                 virtual_ixp_rebalance_s: float = 0.25,
                 virtual_ixp_hold_down_s: float = 0.0) -> None:
    scenario = scenario.upper().strip()
    if scenario not in SCENARIOS:
        raise ValueError(f"Unknown scenario '{scenario}', expected one of {sorted(SCENARIOS.keys())}")

    suffix = f"_{variant_suffix}" if variant_suffix else ""
    out_dir = base_output / f"bgp_scenario_{scenario.lower()}{suffix}"
    out_dir.mkdir(parents=True, exist_ok=True)

    probe_count = expected_probe_count(sim_time_s)

    print(f"\n=== Running BGP scenario {scenario}: {SCENARIOS[scenario]['name']} ===")
    run_bgp_binary(
        scenario,
        out_dir,
        probe_count,
        clock_interval_s,
        sim_time_s,
        use_virtual_ixp=use_virtual_ixp,
        virtual_ixp_port_count=virtual_ixp_port_count,
        virtual_ixp_rebalance_s=virtual_ixp_rebalance_s,
        virtual_ixp_hold_down_s=virtual_ixp_hold_down_s,
    )

    metrics_csv = out_dir / "metrics.csv"
    cp_summary_csv = out_dir / "cp_summary.csv"
    metrics_json = out_dir / "bgp_data_plane_metrics.json"

    analysis_cmd = [
        "python3",
        "utils/bgp_convergence_analysis.py",
        "--dir",
        str(out_dir),
        "--out-csv",
        str(metrics_csv),
        "--cp-summary-csv",
        str(cp_summary_csv),
        "--out-json",
        str(metrics_json),
        "--core-ases",
        "101,102,103,104",
        "--customer-ases",
        "105,106,107,108",
    ]
    run_cmd(analysis_cmd)

    summary_md = out_dir / f"scenario_{scenario.lower()}_bgp_summary.md"
    write_summary_md(scenario, SCENARIOS[scenario]["name"], metrics_json, summary_md)
    if variant_suffix:
        with open(summary_md, "a") as f:
            f.write(f"\n| Clock Interval | {clock_interval_s} s |\n")
    print(f"Wrote BGP summary: {summary_md}")


def main() -> None:
    p = argparse.ArgumentParser(description="Run BGP scenarios and export SCION-compatible metrics")
    p.add_argument("--scenarios", default="A,B,C", help="Comma-separated scenarios to run")
    p.add_argument("--output-dir", default="build", help="Base output directory")
    p.add_argument("--clock-interval", type=float, default=1.0,
                   help="BGP ClockInterval (seconds), default 1.0")
    p.add_argument("--variant-suffix", default="",
                   help="Optional suffix appended to output directory names")
    p.add_argument("--sim-time", type=float, default=800.0,
                   help="Simulation duration in seconds (default 800)")
    p.add_argument("--virtual-ixp", action="store_true",
                   help="Enable virtual IXP mediation in bgp-convergence-first")
    p.add_argument("--virtual-ixp-port-count", type=int, default=2,
                   help="Virtual IXP port count (default 2)")
    p.add_argument("--virtual-ixp-rebalance", type=float, default=0.25,
                   help="Virtual IXP rebalance period in seconds (default 0.25)")
    p.add_argument("--virtual-ixp-hold-down", type=float, default=0.0,
                   help="Virtual IXP hold-down in seconds (default 0.0)")
    args = p.parse_args()

    base_output = Path(args.output_dir)
    base_output.mkdir(parents=True, exist_ok=True)

    scenarios = [s.strip().upper() for s in args.scenarios.split(",") if s.strip()]
    for scenario in scenarios:
        run_scenario(
            scenario,
            base_output,
            clock_interval_s=args.clock_interval,
            variant_suffix=args.variant_suffix,
            sim_time_s=args.sim_time,
            use_virtual_ixp=args.virtual_ixp,
            virtual_ixp_port_count=args.virtual_ixp_port_count,
            virtual_ixp_rebalance_s=args.virtual_ixp_rebalance,
            virtual_ixp_hold_down_s=args.virtual_ixp_hold_down,
        )

    print("\nBGP scenario run complete.")


if __name__ == "__main__":
    main()
