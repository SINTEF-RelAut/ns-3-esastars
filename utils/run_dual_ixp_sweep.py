#!/usr/bin/env python3
"""Sweep BGP and SCION dual-IXP scenarios over a set of time-constant configurations.

Sweep configurations (mrai_s, bgp_clock_s, beacon_period_s):
  1. MRAI=1s,  BGP clock=5s,  beacon=5s
  2. MRAI=2s,  BGP clock=10s, beacon=10s
  3. MRAI=4s,  BGP clock=20s, beacon=20s
  4. MRAI=6s,  BGP clock=30s, beacon=30s

For each configuration and each scenario (hidden, visible):
  - Runs BGP baseline dual-IXP
  - Runs BGP split-AS dual-IXP
  - Generates a SCION config with the appropriate beacon period and runs it

Args:
    See argparse help below.
"""
from __future__ import annotations

import argparse
import re
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple


DEFAULT_STOCHASTIC_LATENCY = {
    "enabled": True,
    "R_eff_m": 1_400_000.0,
    "alpha": 1.3,
    "mu_link_s": 5.5e-3,
    "sigma_link_s": 1.2e-3,
    "delta_s": 0.4e-3,
    "beta": 0.08,
    "omega_rad_s": 2.0 * 3.141592653589793 / 1500.0,
    "R_L_m": 600_000.0,
    "R_M_m": 6_000_000.0,
    "P": 6,
    "N_p": 12,
}


@dataclass
class SweepPoint:
    """A single sweep configuration."""

    mrai_s: int
    bgp_clock_s: int
    beacon_period_s: int

    @property
    def label(self) -> str:
        return f"mrai{self.mrai_s}_clk{self.bgp_clock_s}_bcn{self.beacon_period_s}"


SWEEP_POINTS: List[SweepPoint] = [
    SweepPoint(mrai_s=1, bgp_clock_s=5, beacon_period_s=5),
    SweepPoint(mrai_s=2, bgp_clock_s=10, beacon_period_s=10),
    SweepPoint(mrai_s=4, bgp_clock_s=20, beacon_period_s=20),
    SweepPoint(mrai_s=6, bgp_clock_s=30, beacon_period_s=30),
]

SCENARIOS: List[str] = ["hidden", "visible"]

SCION_TEMPLATE_PATHS = {
    "hidden": {
        "ixp": Path("configs/scenario_ixp_dual_sat_hidden_gateway_switch_generated.yaml"),
        "direct": Path("configs/scenario_ixp_direct_hidden_gateway_switch_generated.yaml"),
    },
    "visible": {
        "ixp": Path("configs/scenario_ixp_dual_sat_visible_gateway_switch_generated.yaml"),
        "direct": Path("configs/scenario_ixp_direct_visible_gateway_switch_generated.yaml"),
    },
}

EVENT_FILE = "user_defined_events/scenario_ixp_gateway_switch_30links_dual_generated_events.json"
DIRECT_EVENT_FILE = "user_defined_events/scenario_direct_link_gateway_switch_events.json"
SIM_TIME_S = 10010
EXPIRATION_RATIO = 135  # expiration_period = beacon_period * this ratio


def run_cmd(cmd: List[str], dry_run: bool = False) -> int:
    """Print and optionally execute a shell command.

    Returns the process exit code. In dry-run mode, always returns 0.
    """
    print(f"  $ {' '.join(shlex.quote(str(c)) for c in cmd)}")
    if dry_run:
        return 0

    result = subprocess.run(cmd, text=True)
    return result.returncode


def generate_scion_config(
    template_path: Path,
    output_path: Path,
    beacon_period_s: int,
    run_prefix: str,
    stochastic_latency: dict[str, object],
) -> None:
    """Write a SCION config derived from template with beacon_period substituted.

    Also rewrites:
    - output: and log_file: to use run_prefix
    - path_snapshot_logger.output: to use run_prefix
    - All scion_probe output paths inside data_plane_probing.pairs to use run_prefix as the
      build sub-directory.
    """
    text = template_path.read_text()

    expiration_s = beacon_period_s * EXPIRATION_RATIO

    # Beacon period
    text = re.sub(r"(  period:\s*)\S+", rf"\g<1>{beacon_period_s}s", text, count=1)
    # Expiration period (only under beacon_service, first occurrence after "beacon_service:")
    text = re.sub(r"(  expiration_period:\s*)\S+", rf"\g<1>{expiration_s}s", text, count=1)

    # Top-level output: line (scenario output text file)
    text = re.sub(
        r"^(output:\s*)build/\S+",
        rf"\g<1>build/{run_prefix}.txt",
        text,
        flags=re.MULTILINE,
    )

    # control_plane_logging log_file
    text = re.sub(
        r"(log_file:\s*)build/\S+",
        rf"\g<1>build/{run_prefix}_control_plane.log",
        text,
    )

    # path_snapshot_logger output
    text = re.sub(
        r"(path_snapshot_logger:\n(?:.*\n)*?  output:\s*)build/\S+",
        lambda m: m.group(0).replace(
            m.group(0).split("output:")[-1].strip(),
            f"build/{run_prefix}_path_snapshots.csv",
        ),
        text,
    )
    # Simpler approach for snapshot output since the regex above can be tricky
    text = re.sub(
        r"(  output:\s*)build/\S+_path_snapshots\.csv",
        rf"\g<1>build/{run_prefix}_path_snapshots.csv",
        text,
    )

    # Probe pair output paths: replace build/<sub-dir>/scion_probe_*.csv
    text = re.sub(
        r"(      output:\s*)build/[^/\n]+/(scion_probe_\S+\.csv)",
        rf"\g<1>build/{run_prefix}/\g<2>",
        text,
    )

    text = re.sub(
        r"(?ms)^stochastic_latency_model:\n(?:  .*\n)*?(?=^[^ \n]|\Z)",
        (
            "stochastic_latency_model:\n"
            f"  enabled: {'true' if stochastic_latency['enabled'] else 'false'}\n"
            f"  R_eff_m: {stochastic_latency['R_eff_m']}\n"
            f"  alpha: {stochastic_latency['alpha']}\n"
            f"  mu_link_s: {stochastic_latency['mu_link_s']}\n"
            f"  sigma_link_s: {stochastic_latency['sigma_link_s']}\n"
            f"  delta_s: {stochastic_latency['delta_s']}\n"
            f"  beta: {stochastic_latency['beta']}\n"
            f"  omega_rad_s: {stochastic_latency['omega_rad_s']}\n"
            f"  R_L_m: {stochastic_latency['R_L_m']}\n"
            f"  R_M_m: {stochastic_latency['R_M_m']}\n"
            f"  P: {stochastic_latency['P']}\n"
            f"  N_p: {stochastic_latency['N_p']}\n\n"
        ),
        text,
        count=1,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(text)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run BGP and SCION dual-IXP parameter sweep."
    )
    parser.add_argument(
        "--scenarios",
        nargs="+",
        choices=SCENARIOS,
        default=SCENARIOS,
        help="Scenarios to run (default: hidden visible)",
    )
    parser.add_argument(
        "--sweep-points",
        nargs="+",
        type=int,
        choices=[1, 2, 3, 4],
        default=[1, 2, 3, 4],
        help="Which sweep points to run (1-4, default: all)",
    )
    parser.add_argument(
        "--protocols",
        nargs="+",
        choices=["bgp", "scion"],
        default=["bgp", "scion"],
        help="Protocols to run (default: bgp scion)",
    )
    parser.add_argument(
        "--bgp-modes",
        nargs="+",
        choices=["baseline", "split", "direct"],
        default=["baseline", "split"],
        help="BGP topology modes (default: baseline split); 'direct' uses direct peer links (visible only)",
    )
    parser.add_argument(
        "--scion-modes",
        nargs="+",
        choices=["ixp", "direct"],
        default=["ixp"],
        help="SCION topology modes (default: ixp); 'direct' uses direct peer links (visible only)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip ./waf build",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print commands without executing",
    )
    parser.add_argument(
        "--sim-time",
        type=int,
        default=SIM_TIME_S,
        help=f"Simulation time in seconds (default: {SIM_TIME_S})",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Abort sweep on first failed run (default: continue and report failures)",
    )
    parser.add_argument("--stochastic-latency-model", dest="stochastic_latency_model", action="store_true", default=True)
    parser.add_argument("--no-stochastic-latency-model", dest="stochastic_latency_model", action="store_false")
    parser.add_argument("--stochastic-r-eff-m", type=float, default=DEFAULT_STOCHASTIC_LATENCY["R_eff_m"])
    parser.add_argument("--stochastic-alpha", type=float, default=DEFAULT_STOCHASTIC_LATENCY["alpha"])
    parser.add_argument("--stochastic-mu-link-s", type=float, default=DEFAULT_STOCHASTIC_LATENCY["mu_link_s"])
    parser.add_argument("--stochastic-sigma-link-s", type=float, default=DEFAULT_STOCHASTIC_LATENCY["sigma_link_s"])
    parser.add_argument("--stochastic-delta-s", type=float, default=DEFAULT_STOCHASTIC_LATENCY["delta_s"])
    parser.add_argument("--stochastic-beta", type=float, default=DEFAULT_STOCHASTIC_LATENCY["beta"])
    parser.add_argument("--stochastic-omega-rad-s", type=float, default=DEFAULT_STOCHASTIC_LATENCY["omega_rad_s"])
    parser.add_argument("--stochastic-r-l-m", type=float, default=DEFAULT_STOCHASTIC_LATENCY["R_L_m"])
    parser.add_argument("--stochastic-r-m-m", type=float, default=DEFAULT_STOCHASTIC_LATENCY["R_M_m"])
    parser.add_argument("--stochastic-p", type=int, default=DEFAULT_STOCHASTIC_LATENCY["P"])
    parser.add_argument("--stochastic-n-p", type=int, default=DEFAULT_STOCHASTIC_LATENCY["N_p"])
    args = parser.parse_args()

    stochastic_latency = {
        "enabled": args.stochastic_latency_model,
        "R_eff_m": args.stochastic_r_eff_m,
        "alpha": args.stochastic_alpha,
        "mu_link_s": args.stochastic_mu_link_s,
        "sigma_link_s": args.stochastic_sigma_link_s,
        "delta_s": args.stochastic_delta_s,
        "beta": args.stochastic_beta,
        "omega_rad_s": args.stochastic_omega_rad_s,
        "R_L_m": args.stochastic_r_l_m,
        "R_M_m": args.stochastic_r_m_m,
        "P": args.stochastic_p,
        "N_p": args.stochastic_n_p,
    }

    repo_root = Path(__file__).resolve().parent.parent

    selected_points = [SWEEP_POINTS[i - 1] for i in args.sweep_points]

    if not args.skip_build:
        print("Building...")
        build_rc = run_cmd(["./waf", "build"], dry_run=args.dry_run)
        if build_rc != 0:
            print(f"Build failed with exit code {build_rc}")
            return build_rc
        print()

    total = len(selected_points) * len(args.scenarios) * len(args.protocols)
    done = 0
    command_attempts = 0
    command_failures: List[Tuple[str, int, str]] = []

    def run_or_record(label: str, cmd: List[str]) -> None:
        nonlocal command_attempts
        command_attempts += 1
        rc = run_cmd(cmd, dry_run=args.dry_run)
        if rc != 0:
            command_failures.append((label, rc, " ".join(cmd)))
            print(f"  !! FAILED ({label}) exit={rc}")
            if args.fail_fast:
                raise RuntimeError(f"Command failed (exit {rc}): {' '.join(cmd)}")

    for pt in selected_points:
        for scenario in args.scenarios:
            print(f"\n[{pt.label}] scenario={scenario}")

            # ---- BGP ----
            if "bgp" in args.protocols:
                for mode in args.bgp_modes:
                    # Direct-link mode only supports the visible scenario.
                    if mode == "direct" and scenario != "visible":
                        print(f"  BGP direct: skipped (only supported for visible scenario)")
                        continue

                    if mode == "direct":
                        run_args = (
                            f"bgp-ixp-scenarios"
                            f" --directLinks=1"
                            f" --scenario={scenario}"
                            f" --simTime={args.sim_time}"
                            f" --clockInterval={pt.bgp_clock_s}"
                            f" --mrai={pt.mrai_s}"
                            f" --stochasticLatencyModel={1 if args.stochastic_latency_model else 0}"
                            f" --stochasticR_eff_m={args.stochastic_r_eff_m}"
                            f" --stochasticAlpha={args.stochastic_alpha}"
                            f" --stochasticMuLink_s={args.stochastic_mu_link_s}"
                            f" --stochasticSigmaLink_s={args.stochastic_sigma_link_s}"
                            f" --stochasticDelta_s={args.stochastic_delta_s}"
                            f" --stochasticBeta={args.stochastic_beta}"
                            f" --stochasticOmega_rad_s={args.stochastic_omega_rad_s}"
                            f" --stochasticR_L_m={args.stochastic_r_l_m}"
                            f" --stochasticR_M_m={args.stochastic_r_m_m}"
                            f" --stochasticP={args.stochastic_p}"
                            f" --stochasticN_p={args.stochastic_n_p}"
                            f" --eventFile={DIRECT_EVENT_FILE}"
                            f" --outDir=build/sweep_bgp_{mode}_{scenario}_{pt.label}"
                        )
                    else:
                        split_flag = "1" if mode == "split" else "0"
                        run_args = (
                            f"bgp-ixp-scenarios"
                            f" --dualIxp=1"
                            f" --splitEdgeAs={split_flag}"
                            f" --scenario={scenario}"
                            f" --simTime={args.sim_time}"
                            f" --clockInterval={pt.bgp_clock_s}"
                            f" --mrai={pt.mrai_s}"
                            f" --stochasticLatencyModel={1 if args.stochastic_latency_model else 0}"
                            f" --stochasticR_eff_m={args.stochastic_r_eff_m}"
                            f" --stochasticAlpha={args.stochastic_alpha}"
                            f" --stochasticMuLink_s={args.stochastic_mu_link_s}"
                            f" --stochasticSigmaLink_s={args.stochastic_sigma_link_s}"
                            f" --stochasticDelta_s={args.stochastic_delta_s}"
                            f" --stochasticBeta={args.stochastic_beta}"
                            f" --stochasticOmega_rad_s={args.stochastic_omega_rad_s}"
                            f" --stochasticR_L_m={args.stochastic_r_l_m}"
                            f" --stochasticR_M_m={args.stochastic_r_m_m}"
                            f" --stochasticP={args.stochastic_p}"
                            f" --stochasticN_p={args.stochastic_n_p}"
                            f" --eventFile={EVENT_FILE}"
                            f" --outDir=build/sweep_bgp_{mode}_{scenario}_{pt.label}"
                        )

                    cmd = [str(repo_root / "build/scratch/bgp-ixp-scenarios")] + shlex.split(run_args)
                    print(f"  BGP {mode}:")
                    run_or_record(f"bgp/{scenario}/{mode}/{pt.label}", cmd)

            # ---- SCION ----
            if "scion" in args.protocols:
                for scion_mode in args.scion_modes:
                    # Direct-link mode only supports the visible scenario.
                    if scion_mode == "direct" and scenario != "visible":
                        print(f"  SCION {scion_mode}: skipped (only supported for visible scenario)")
                        continue

                    template = SCION_TEMPLATE_PATHS[scenario][scion_mode]
                    event_file = DIRECT_EVENT_FILE if scion_mode == "direct" else EVENT_FILE
                    run_prefix = f"sweep_scion_{scion_mode}_{scenario}_{pt.label}"
                    out_cfg = Path(f"configs/sweep_scion_{scion_mode}_{scenario}_{pt.label}.yaml")

                    print(f"  SCION {scion_mode} (beacon={pt.beacon_period_s}s): generating {out_cfg}")
                    if not args.dry_run:
                        # For direct mode, we need to replace the event file in the config
                        text = template.read_text()
                        if scion_mode == "direct":
                            text = re.sub(
                                r"(events_file:\s*)\S+",
                                rf"\g<1>{event_file}",
                                text,
                            )
                        # Write the modified template to a temporary version
                        temp_template = Path(f"{template}.tmp")
                        temp_template.write_text(text)
                        generate_scion_config(
                            temp_template,
                            out_cfg,
                            pt.beacon_period_s,
                            run_prefix,
                            stochastic_latency,
                        )
                        temp_template.unlink()  # Clean up temp file
                        # Ensure probe output directory exists
                        Path(f"build/{run_prefix}").mkdir(parents=True, exist_ok=True)

                    run_or_record(
                        f"scion/{scion_mode}/{scenario}/{pt.label}",
                        [str(repo_root / "build/src/SCION/ns3.30.1-scion-debug"), str(out_cfg)],
                    )

            done += 1

    print(f"\nSweep complete: {done} scenario/protocol combinations across {len(selected_points)} sweep points.")
    print(f"Commands attempted: {command_attempts}")
    print(f"Commands failed: {len(command_failures)}")

    if command_failures:
        print("\nFailed runs:")
        for label, rc, cmd in command_failures:
            print(f"  - {label} (exit {rc})")
            print(f"    {cmd}")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
