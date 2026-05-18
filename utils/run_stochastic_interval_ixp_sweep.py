#!/usr/bin/env python3
"""Run fixed-parameter IXP sweeps using stochastic gateway-switch event traces.

This script generates one shared stochastic gateway trace per density/seed pair,
derives family-specific event files from that shared trace, and then runs the
corresponding BGP and SCION simulations so all protocols reuse the same event
timelines.

Sweep design:
  - Densities:
      * dense:  Uniform inter-switch interval in [50, 200] seconds
      * sparse: Uniform inter-switch interval in [150, 600] seconds
  - Seeds: 10 independent runs by default
  - Timers: MRAI=10s, BGP clock=50s, SCION beacon period=50s
  - Simulation time: 5000s
  - Families: dual, single, direct
  - Scenarios: hidden, visible (BGP direct hidden is not supported upstream and is skipped)

The shared source trace models four independent managed link pairs (gateways
1..4).  Each pair starts with the primary link logically active and toggles to
the backup, then back again, at independently sampled uniform intervals.
"""

from __future__ import annotations

import argparse
import random
import re
import shlex
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

from run_multi_event_ixp_sweep import (
    GATEWAY_TO_AS,
    SCION_TEMPLATE_PATHS,
    SWITCH_DELTA_S,
    SourceEvent,
    build_direct_events_bgp,
    build_direct_events_scion,
    build_dual_ixp_events,
    build_single_ixp_events,
    generate_scion_config,
    run_cmd,
    sanitize_stem,
    write_json,
)


SIM_TIME_S = 5000
MRAI_S = 10
BGP_CLOCK_S = 50
SCION_BEACON_S = 50
DEFAULT_SEEDS = list(range(1, 11))


@dataclass(frozen=True)
class DensitySpec:
    name: str
    min_interval_s: float
    max_interval_s: float


DENSITIES: Dict[str, DensitySpec] = {
    "dense": DensitySpec(name="dense", min_interval_s=50.0, max_interval_s=200.0),
    "sparse": DensitySpec(name="sparse", min_interval_s=150.0, max_interval_s=600.0),
}


def format_time_s(value: float) -> str:
    return f"{value:.3f}s"


def build_stochastic_source_events(
    density: DensitySpec,
    seed: int,
    sim_time_s: float,
) -> Tuple[List[SourceEvent], Dict[str, object]]:
    """Generate a shared gateway trace for one density/seed pair.

    Each gateway drives one independent primary/backup pair and alternates
    between the two states at uniform inter-event intervals.
    """
    rng = random.Random(seed)
    events: List[SourceEvent] = []
    per_gateway_counts: Dict[str, int] = {}

    for gateway_id in sorted(GATEWAY_TO_AS):
        current_time = rng.uniform(density.min_interval_s, density.max_interval_s)
        event_index = 0
        while current_time < sim_time_s:
            event_type = "link_down" if event_index % 2 == 0 else "link_up"
            source_ifid = gateway_id * 1000 + event_index
            events.append(
                SourceEvent(
                    time_s=current_time,
                    event_type=event_type,
                    gateway_id=gateway_id,
                    source_ifid=str(source_ifid),
                )
            )
            event_index += 1
            current_time += rng.uniform(density.min_interval_s, density.max_interval_s)

        per_gateway_counts[str(gateway_id)] = event_index

    events.sort(key=lambda e: (e.time_s, e.gateway_id, e.source_ifid))
    metadata = {
        "density": density.name,
        "seed": seed,
        "sim_time_s": sim_time_s,
        "interval_uniform_min_s": density.min_interval_s,
        "interval_uniform_max_s": density.max_interval_s,
        "gateways": sorted(GATEWAY_TO_AS),
        "events_per_gateway": per_gateway_counts,
    }
    return events, metadata


def build_source_event_document(
    density: DensitySpec,
    seed: int,
    sim_time_s: float,
    source_events: Iterable[SourceEvent],
    metadata: Dict[str, object],
) -> Dict[str, object]:
    return {
        "description": "Stochastic source gateway events for comparable IXP sweeps",
        "generator": "run_stochastic_interval_ixp_sweep.py",
        "density": density.name,
        "seed": seed,
        "simulation_time_s": sim_time_s,
        "interval_distribution": {
            "type": "uniform",
            "min_s": density.min_interval_s,
            "max_s": density.max_interval_s,
        },
        "notes": [
            "All primary links are assumed active initially.",
            "Family-specific event files are derived from this shared source trace.",
            "The source event type is informational; family converters implement the actual toggle pair.",
        ],
        "metadata": metadata,
        "events": [
            {
                "time": format_time_s(event.time_s),
                "type": event.event_type,
                "args": ["1", str(event.gateway_id), str(event.source_ifid)],
                "description": (
                    f"Stochastic gateway event: gateway={event.gateway_id} {event.event_type} "
                    f"seed={seed} density={density.name}"
                ),
            }
            for event in source_events
        ],
    }


def finalize_scion_config_timing(output_path: Path, sim_time_s: int) -> None:
    """Rewrite generated SCION config to the requested simulation length.

    The base template defaults to 10000s.  This sweep fixes simulation duration,
    last beaconing time, and probing count to match the requested run length.
    """
    text = output_path.read_text(encoding="utf-8")
    probe_count = max(0, sim_time_s - 10)

    text = re.sub(r"^(simulation_duration:\s*)\S+", rf"\g<1>{sim_time_s}s", text, flags=re.MULTILINE)
    text = re.sub(r"^(\s*last_beaconing:\s*)\S+", rf"\g<1>{sim_time_s}s", text, flags=re.MULTILINE)
    text = re.sub(r"^(\s*count:\s*)\d+", rf"\g<1>{probe_count}", text, flags=re.MULTILINE)

    output_path.write_text(text, encoding="utf-8")


def ensure_templates_exist(repo_root: Path, families: Iterable[str], scenarios: Iterable[str]) -> List[Path]:
    missing: List[Path] = []
    for scenario in scenarios:
        for family in families:
            candidate = repo_root / SCION_TEMPLATE_PATHS[scenario][family]
            if not candidate.exists():
                missing.append(candidate)
    return missing


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run stochastic dense/sparse comparable sweeps across BGP and SCION IXP families."
    )
    parser.add_argument(
        "--densities",
        nargs="+",
        choices=sorted(DENSITIES),
        default=["dense", "sparse"],
        help="Which density sweeps to run (default: dense sparse)",
    )
    parser.add_argument(
        "--seed-start",
        type=int,
        default=1,
        help="First seed index (default: 1)",
    )
    parser.add_argument(
        "--seed-count",
        type=int,
        default=10,
        help="Number of seeds/runs per density (default: 10)",
    )
    parser.add_argument(
        "--families",
        nargs="+",
        choices=["dual", "single", "direct"],
        default=["dual", "single", "direct"],
        help="Topology families to run (default: dual single direct)",
    )
    parser.add_argument(
        "--protocols",
        nargs="+",
        choices=["bgp", "scion"],
        default=["bgp", "scion"],
        help="Protocols to run (default: bgp scion)",
    )
    parser.add_argument(
        "--scenarios",
        nargs="+",
        choices=["hidden", "visible"],
        default=["hidden", "visible"],
        help="Scenarios to run (default: hidden visible)",
    )
    parser.add_argument(
        "--sim-time",
        type=int,
        default=SIM_TIME_S,
        help=f"Simulation time in seconds (default: {SIM_TIME_S})",
    )
    parser.add_argument("--skip-build", action="store_true", help="Skip ./waf build")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing")
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Abort on first failed command (default: continue and summarize)",
    )
    parser.add_argument(
        "--stochastic-latency-model",
        dest="stochastic_latency_model",
        action="store_true",
        default=True,
        help="Enable stochastic path latency model in the simulations",
    )
    parser.add_argument(
        "--no-stochastic-latency-model",
        dest="stochastic_latency_model",
        action="store_false",
        help="Disable stochastic path latency model in the simulations",
    )
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

    if args.seed_count <= 0:
        print("seed-count must be positive")
        return 1

    repo_root = Path(__file__).resolve().parent.parent
    seeds = [args.seed_start + offset for offset in range(args.seed_count)]
    densities = [DENSITIES[name] for name in args.densities]
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

    missing_templates = ensure_templates_exist(repo_root, args.families, args.scenarios)
    if missing_templates:
        print("Missing SCION templates:")
        for path in missing_templates:
            print(f"  - {path}")
        return 1

    if not args.skip_build:
        print("Building...")
        build_rc = run_cmd(["./waf", "build"], dry_run=args.dry_run)
        if build_rc != 0:
            print(f"Build failed with exit code {build_rc}")
            return build_rc
        print()

    generated_dir = repo_root / "build" / "generated_events" / "stochastic_interval_sweep"
    command_failures: List[Tuple[str, int, str]] = []
    command_attempts = 0
    total_runs = 0

    def run_or_record(label: str, cmd: List[str]) -> None:
        nonlocal command_attempts
        command_attempts += 1
        rc = run_cmd(cmd, dry_run=args.dry_run)
        if rc != 0:
            command_failures.append((label, rc, " ".join(str(part) for part in cmd)))
            print(f"  !! FAILED ({label}) exit={rc}")
            if args.fail_fast:
                raise RuntimeError(f"Command failed (exit {rc}): {' '.join(str(part) for part in cmd)}")

    for density in densities:
        print(f"\n=== Density {density.name} [{density.min_interval_s:.0f}, {density.max_interval_s:.0f}] ===")
        for seed in seeds:
            run_key = sanitize_stem(f"{density.name}_seed{seed:02d}")
            print(f"\n  Seed {seed} -> {run_key}")

            source_events, metadata = build_stochastic_source_events(density, seed, args.sim_time)
            source_path = generated_dir / f"{run_key}_source.json"
            dual_path = generated_dir / f"{run_key}_dual.json"
            single_path = generated_dir / f"{run_key}_single.json"
            direct_bgp_path = generated_dir / f"{run_key}_direct_bgp.json"
            direct_scion_path = generated_dir / f"{run_key}_direct_scion.json"

            if not args.dry_run:
                write_json(
                    source_path,
                    build_source_event_document(density, seed, args.sim_time, source_events, metadata),
                )
                write_json(dual_path, build_dual_ixp_events(source_path, source_events))
                write_json(single_path, build_single_ixp_events(source_path, source_events))
                write_json(direct_bgp_path, build_direct_events_bgp(source_path, source_events))
                write_json(direct_scion_path, build_direct_events_scion(source_path, source_events))

            event_paths = {
                "dual": dual_path,
                "single": single_path,
                "direct_bgp": direct_bgp_path,
                "direct_scion": direct_scion_path,
            }

            for scenario in args.scenarios:
                print(f"    Scenario: {scenario}")
                for family in args.families:
                    event_path_bgp = event_paths["direct_bgp"] if family == "direct" else event_paths[family]
                    event_path_scion = event_paths["direct_scion"] if family == "direct" else event_paths[family]
                    event_rel_bgp = str(event_path_bgp.relative_to(repo_root))
                    event_rel_scion = str(event_path_scion.relative_to(repo_root))

                    if "bgp" in args.protocols:
                        if family == "direct" and scenario == "hidden":
                            print("      BGP direct: skipped (hidden scenario unsupported by bgp-ixp-scenarios)")
                        else:
                            bgp_flags = [
                                f"--scenario={scenario}",
                                f"--simTime={args.sim_time}",
                                f"--clockInterval={BGP_CLOCK_S}",
                                f"--mrai={MRAI_S}",
                                f"--eventFile={event_rel_bgp}",
                                f"--outDir=build/stochastic_bgp_{family}_{scenario}_{run_key}",
                                f"--stochasticLatencyModel={1 if args.stochastic_latency_model else 0}",
                                f"--stochasticR_eff_m={args.stochastic_r_eff_m}",
                                f"--stochasticAlpha={args.stochastic_alpha}",
                                f"--stochasticMuLink_s={args.stochastic_mu_link_s}",
                                f"--stochasticSigmaLink_s={args.stochastic_sigma_link_s}",
                                f"--stochasticDelta_s={args.stochastic_delta_s}",
                                f"--stochasticBeta={args.stochastic_beta}",
                                f"--stochasticOmega_rad_s={args.stochastic_omega_rad_s}",
                                f"--stochasticR_L_m={args.stochastic_r_l_m}",
                                f"--stochasticR_M_m={args.stochastic_r_m_m}",
                                f"--stochasticP={args.stochastic_p}",
                                f"--stochasticN_p={args.stochastic_n_p}",
                            ]
                            if family == "dual":
                                bgp_flags.append("--dualIxp=1")
                                bgp_flags.append("--splitEdgeAs=0")
                            elif family == "single":
                                bgp_flags.append("--dualIxp=0")
                                bgp_flags.append("--splitEdgeAs=0")
                            else:
                                bgp_flags.append("--directLinks=1")

                            run_or_record(
                                f"bgp/{family}/{scenario}/{run_key}",
                                [str(repo_root / "build/scratch/bgp-ixp-scenarios")] + bgp_flags,
                            )
                            total_runs += 1

                    if "scion" in args.protocols:
                        template = repo_root / SCION_TEMPLATE_PATHS[scenario][family]
                        run_prefix = f"stochastic_scion_{family}_{scenario}_{run_key}"
                        out_cfg = repo_root / "configs" / f"{run_prefix}.yaml"
                        if not args.dry_run:
                            generate_scion_config(
                                template_path=template,
                                output_path=out_cfg,
                                beacon_period_s=SCION_BEACON_S,
                                run_prefix=run_prefix,
                                events_file_rel=event_rel_scion,
                                stochastic_latency=stochastic_latency,
                            )
                            finalize_scion_config_timing(out_cfg, args.sim_time)
                            (repo_root / "build" / run_prefix).mkdir(parents=True, exist_ok=True)

                        run_or_record(
                            f"scion/{family}/{scenario}/{run_key}",
                            [
                                str(repo_root / "build/src/SCION/ns3.30.1-scion-debug"),
                                str(out_cfg.relative_to(repo_root)),
                            ],
                        )
                        total_runs += 1

    print(f"\nSweep complete. Runs attempted: {total_runs}")
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