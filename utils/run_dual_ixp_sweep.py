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
    args = parser.parse_args()

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
                        generate_scion_config(temp_template, out_cfg, pt.beacon_period_s, run_prefix)
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
