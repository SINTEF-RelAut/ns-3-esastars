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
    return run_scenario_with_latency(
        name=name,
        output_dir=output_dir,
        clock_interval_s=clock_interval_s,
        stochastic_latency_model=True,
        stochastic_r_eff_m=1_400_000.0,
        stochastic_alpha=1.3,
        stochastic_mu_link_s=5.5e-3,
        stochastic_sigma_link_s=1.2e-3,
        stochastic_delta_s=0.4e-3,
        stochastic_beta=0.08,
        stochastic_omega_rad_s=2.0 * 3.141592653589793 / 1500.0,
        stochastic_r_l_m=600_000.0,
        stochastic_r_m_m=6_000_000.0,
        stochastic_p=6,
        stochastic_n_p=12,
    )


def run_scenario_with_latency(
    name: str,
    output_dir: Path,
    clock_interval_s: float,
    stochastic_latency_model: bool,
    stochastic_r_eff_m: float,
    stochastic_alpha: float,
    stochastic_mu_link_s: float,
    stochastic_sigma_link_s: float,
    stochastic_delta_s: float,
    stochastic_beta: float,
    stochastic_omega_rad_s: float,
    stochastic_r_l_m: float,
    stochastic_r_m_m: float,
    stochastic_p: int,
    stochastic_n_p: int,
) -> Path:
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
                f"--stochasticLatencyModel={1 if stochastic_latency_model else 0} "
                f"--stochasticR_eff_m={stochastic_r_eff_m} "
                f"--stochasticAlpha={stochastic_alpha} "
                f"--stochasticMuLink_s={stochastic_mu_link_s} "
                f"--stochasticSigmaLink_s={stochastic_sigma_link_s} "
                f"--stochasticDelta_s={stochastic_delta_s} "
                f"--stochasticBeta={stochastic_beta} "
                f"--stochasticOmega_rad_s={stochastic_omega_rad_s} "
                f"--stochasticR_L_m={stochastic_r_l_m} "
                f"--stochasticR_M_m={stochastic_r_m_m} "
                f"--stochasticP={stochastic_p} "
                f"--stochasticN_p={stochastic_n_p} "
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
    parser.add_argument("--stochastic-latency-model", dest="stochastic_latency_model", action="store_true", default=True)
    parser.add_argument("--no-stochastic-latency-model", dest="stochastic_latency_model", action="store_false")
    parser.add_argument("--stochastic-r-eff-m", type=float, default=1_400_000.0)
    parser.add_argument("--stochastic-alpha", type=float, default=1.3)
    parser.add_argument("--stochastic-mu-link-s", type=float, default=5.5e-3)
    parser.add_argument("--stochastic-sigma-link-s", type=float, default=1.2e-3)
    parser.add_argument("--stochastic-delta-s", type=float, default=0.4e-3)
    parser.add_argument("--stochastic-beta", type=float, default=0.08)
    parser.add_argument("--stochastic-omega-rad-s", type=float, default=2.0 * 3.141592653589793 / 1500.0)
    parser.add_argument("--stochastic-r-l-m", type=float, default=600_000.0)
    parser.add_argument("--stochastic-r-m-m", type=float, default=6_000_000.0)
    parser.add_argument("--stochastic-p", type=int, default=6)
    parser.add_argument("--stochastic-n-p", type=int, default=12)
    args = parser.parse_args()

    selected = [s.strip().lower() for s in args.scenarios.split(",") if s.strip()]
    unknown = [s for s in selected if s not in SCENARIOS]
    if unknown:
        raise SystemExit(f"Unknown scenarios: {', '.join(unknown)}")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    for scenario in selected:
        run_dir = run_scenario_with_latency(
            name=scenario,
            output_dir=output_dir,
            clock_interval_s=args.clock_interval,
            stochastic_latency_model=args.stochastic_latency_model,
            stochastic_r_eff_m=args.stochastic_r_eff_m,
            stochastic_alpha=args.stochastic_alpha,
            stochastic_mu_link_s=args.stochastic_mu_link_s,
            stochastic_sigma_link_s=args.stochastic_sigma_link_s,
            stochastic_delta_s=args.stochastic_delta_s,
            stochastic_beta=args.stochastic_beta,
            stochastic_omega_rad_s=args.stochastic_omega_rad_s,
            stochastic_r_l_m=args.stochastic_r_l_m,
            stochastic_r_m_m=args.stochastic_r_m_m,
            stochastic_p=args.stochastic_p,
            stochastic_n_p=args.stochastic_n_p,
        )
        print(run_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())