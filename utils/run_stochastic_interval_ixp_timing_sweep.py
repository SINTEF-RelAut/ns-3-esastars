#!/usr/bin/env python3
"""Run stochastic IXP sweeps across MRAI/clock/probe timing points.

This script is a timing-sweep variant of run_stochastic_interval_ixp_sweep.py.
It keeps the same stochastic event generation model, scenarios, families, and
protocols, while sweeping these timer points:
  - MRAI: 1, 2, 4, 6, 8 seconds
  - BGP clock interval: 5, 10, 20, 30, 40 seconds
    - SCION probe interval: 1 second (fixed)

All protocols/families for a given (density, seed) reuse the same shared source
event timeline to preserve comparability.
"""

from __future__ import annotations

import argparse
import random
import re
from datetime import date
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

from run_multi_event_ixp_sweep import (
    GATEWAY_TO_AS,
    SCION_TEMPLATE_PATHS,
    SWITCH_DELTA_S,
    SourceEvent,
    build_direct_events_bgp,
    build_direct_events_scion,
    build_dual_ixp_events,
    build_single_ixp_events,
    reject_mismatched_families,
    build_split_ixp_events,
    generate_scion_config,
    run_cmd,
    sanitize_stem,
    write_json,
)


SIM_TIME_S = 5000
# Measured slowest cold-start convergence across the sweep is SCION at beacon_period=40s,
# whose first probe reply lands at t=40.3s; BGP converges by t=10.2s at every clock
# interval. 60s therefore clears the worst case with margin, at both ends of the grid.
WARMUP_S = 60.0
DEFAULT_SEEDS = list(range(1, 11))
# Seed offset for the split families' second exchange location. Large enough to stay disjoint
# from any seed the CLI accepts, so location B draws an independent churn stream while location
# A keeps the exact trace the other families see at this seed.
LOCATION_B_SEED_OFFSET = 10_000


@dataclass(frozen=True)
class DensitySpec:
    name: str
    min_interval_s: float
    max_interval_s: float


@dataclass(frozen=True)
class TimingPoint:
    mrai_s: int
    bgp_clock_s: int
    probe_period_s: int
    beacon_period_s: int

    @property
    def label(self) -> str:
        return (
            f"mrai{self.mrai_s}_clk{self.bgp_clock_s}_"
            f"probe{self.probe_period_s}_bcn{self.beacon_period_s}"
        )


DENSITIES: Dict[str, DensitySpec] = {
    "dense": DensitySpec(name="dense", min_interval_s=50.0, max_interval_s=200.0),
    "sparse": DensitySpec(name="sparse", min_interval_s=150.0, max_interval_s=600.0),
}

TIMING_POINTS: List[TimingPoint] = [
    TimingPoint(mrai_s=1, bgp_clock_s=5, probe_period_s=1, beacon_period_s=5),
    TimingPoint(mrai_s=2, bgp_clock_s=10, probe_period_s=1, beacon_period_s=10),
    TimingPoint(mrai_s=4, bgp_clock_s=20, probe_period_s=1, beacon_period_s=20),
    TimingPoint(mrai_s=6, bgp_clock_s=30, probe_period_s=1, beacon_period_s=30),
    TimingPoint(mrai_s=8, bgp_clock_s=40, probe_period_s=1, beacon_period_s=40),
]


def format_time_s(value: float) -> str:
    return f"{value:.3f}s"


def build_stochastic_source_events(
    density: DensitySpec,
    seed: int,
    sim_time_s: float,
    warmup_s: float = 0.0,
) -> Tuple[List[SourceEvent], Dict[str, object]]:
    """Generate a shared gateway trace for one density/seed pair.

    Each gateway drives one independent primary/backup pair and alternates
    between the two states at uniform inter-event intervals.

    Args:
        density: Inter-event interval distribution to draw from.
        seed: RNG seed, shared by every protocol so all runs see one trace.
        sim_time_s: Total simulation duration in seconds.
        warmup_s: Quiet period at the start of the run during which no link event
            fires, so both control planes reach steady state before any churn. Without
            it the first event can land at ``density.min_interval_s`` (50s when dense),
            which is before SCION has finished its initial beaconing at the longer
            beacon periods, and the resulting convergence loss is indistinguishable
            from churn loss.

    Returns:
        The event list and a metadata dictionary describing how it was generated.
    """
    rng = random.Random(seed)
    events: List[SourceEvent] = []
    per_gateway_counts: Dict[str, int] = {}

    for gateway_id in sorted(GATEWAY_TO_AS):
        current_time = warmup_s + rng.uniform(density.min_interval_s, density.max_interval_s)
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
        "warmup_s": warmup_s,
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
        "description": "Stochastic source gateway events for comparable IXP timing sweeps",
        "generator": "run_stochastic_interval_ixp_timing_sweep.py",
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


def finalize_scion_config_timing(
    output_path: Path,
    sim_time_s: int,
    probe_period_s: int,
    snapshot_period_s: Optional[int] = None,
    warm_path_caches: bool = False,
) -> None:
    """Rewrite generated SCION config to requested simulation/probing timing.

    The SCION convergence metric in utils/plot_all_sweep_results.py is derived from
    path-snapshot change times, so its resolution is bounded by
    path_snapshot_logger.period. The templates ship with 300s, which is far coarser
    than the convergence times of interest (~10-30s) and leaves the metric insensitive
    to the beacon period being swept. Default the snapshot period to the probe period
    so snapshots resolve convergence at the same granularity as data-plane probing.
    """
    if snapshot_period_s is None:
        snapshot_period_s = probe_period_s

    text = output_path.read_text(encoding="utf-8")

    # path_snapshot_logger.discover_all_pairs schedules WarmPathRequestsForSnapshot, which asks
    # every AS's host 2 for paths to every other AS once per snapshot tick. It contributes
    # nothing to the snapshot CSV -- LogPathSnapshotCsv reads the beacon store, not host caches
    # -- and it is ~98% of SCION runtime: on one reference run, turning it off took a 400s
    # simulation from 519.5s to 10.4s. Default it off for that reason alone.
    #
    # It does NOT measurably bias the result, despite the plausible worry that refreshing host
    # caches would mask the cache-miss loss a handover causes. A single-run comparison suggested
    # a large effect (2.655% loss with warming, 4.374% without) but that did not survive the
    # sweep: across build/2026-09-12 (warming on, seeds 10-19) and build/2026-09-16 (off, seeds
    # 200-239), on identical SCION topologies, the single and dual_vis families differ by
    # between -1.0 and +0.45 percentage points with no consistent sign. Treat the earlier
    # single-run figure as noise, and results taken with warming on as still valid.
    text = re.sub(
        r"^(\s*discover_all_pairs:\s*)\S+",
        rf"\g<1>{'true' if warm_path_caches else 'false'}",
        text,
        flags=re.MULTILINE,
    )

    if sim_time_s <= 10:
        probe_count = 1
    else:
        # Probing starts at 10s in templates, so align count with that schedule.
        probe_count = int((sim_time_s - 10) // probe_period_s) + 1

    text = re.sub(r"^(simulation_duration:\s*)\S+", rf"\g<1>{sim_time_s}s", text, flags=re.MULTILINE)
    text = re.sub(r"^(\s*last_beaconing:\s*)\S+", rf"\g<1>{sim_time_s}s", text, flags=re.MULTILINE)

    lines = text.splitlines()
    in_data_plane_probing = False
    in_path_snapshot_logger = False
    for idx, line in enumerate(lines):
        if re.match(r"^\S", line):
            in_data_plane_probing = False
            in_path_snapshot_logger = False

        if line.startswith("data_plane_probing:"):
            in_data_plane_probing = True
            continue

        if line.startswith("path_snapshot_logger:"):
            in_path_snapshot_logger = True
            continue

        if in_path_snapshot_logger and re.match(r"^\s*period:\s*", line):
            lines[idx] = re.sub(r"^(\s*period:\s*)\S+", rf"\g<1>{snapshot_period_s}s", line)
            continue

        if in_data_plane_probing and re.match(r"^\s*period:\s*", line):
            lines[idx] = re.sub(r"^(\s*period:\s*)\S+", rf"\g<1>{probe_period_s}s", line)
            continue

        if in_data_plane_probing and re.match(r"^\s*count:\s*", line):
            lines[idx] = re.sub(r"^(\s*count:\s*)\d+", rf"\g<1>{probe_count}", line)

    text = "\n".join(lines) + "\n"

    output_path.write_text(text, encoding="utf-8")


def has_template(scenario: str, family: str) -> bool:
    """Whether this family is defined for this scenario at all."""
    return SCION_TEMPLATE_PATHS.get(scenario, {}).get(family) is not None


def ensure_templates_exist(repo_root: Path, families: Iterable[str], scenarios: Iterable[str]) -> List[Path]:
    """Check every requested (scenario, family) that exists has its SCION template on disk.

    Not every family has a template for every scenario. dual_vis, splitsame, splitvis and the
    three direct families exist only for the visible scenario, because the hidden scenario
    routes link events through the virtual IXP fabric -- which is either the opposite of what
    those families are built to expose or, for direct peering, a fabric that is not there at
    all. Asking for one of them under hidden used to raise a bare KeyError from inside this
    function, well before any run started and with nothing naming the culprit.

    Such a combination is now skipped rather than fatal, and the run loop prints why. It has to
    be: --scenarios defaults to "hidden visible", so aborting meant none of these families could
    be used without also overriding --scenarios. It stays fatal when a family is unknown for
    EVERY requested scenario, which is what still catches a typo.
    """
    missing: List[Path] = []
    for family in families:
        if not any(has_template(scenario, family) for scenario in scenarios):
            available = sorted(
                {f for scenario in scenarios for f in SCION_TEMPLATE_PATHS.get(scenario, {})}
            )
            raise SystemExit(
                f"family '{family}' has no SCION template for any requested scenario "
                f"({', '.join(scenarios)}). Families available: {', '.join(available)}."
            )
    for scenario in scenarios:
        for family in families:
            template = SCION_TEMPLATE_PATHS.get(scenario, {}).get(family)
            if template is None:
                continue
            candidate = repo_root / template
            if not candidate.exists():
                missing.append(candidate)
    return missing


def parse_requested_mrai(raw_values: List[int]) -> List[TimingPoint]:
    mapping = {point.mrai_s: point for point in TIMING_POINTS}
    selected: List[TimingPoint] = []
    for value in raw_values:
        if value not in mapping:
            valid = ", ".join(str(k) for k in sorted(mapping))
            raise ValueError(f"Unsupported MRAI value {value}. Valid values: {valid}")
        selected.append(mapping[value])
    return selected


def main() -> int:
    default_output_subdir = f"{date.today().isoformat()}/stochastic_interval_ixp_timing_sweep"

    parser = argparse.ArgumentParser(
        description=(
            "Run stochastic dense/sparse comparable sweeps across BGP and SCION "
            "IXP families for multiple timing points."
        )
    )
    parser.add_argument(
        "--densities",
        nargs="+",
        choices=sorted(DENSITIES),
        default=["dense", "sparse"],
        help="Which density sweeps to run (default: dense sparse)",
    )
    parser.add_argument(
        "--mrai-values",
        nargs="+",
        type=int,
        default=[1, 2, 4, 6, 8],
        help="MRAI values to run (default: 1 2 4 6 8)",
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
        choices=[
            "dual",
            "dual_vis",
            "single",
            "direct",
            "directsame",
            "directvis",
            "splitsame",
            "splitvis",
        ],
        default=["single", "direct"],
        help=(
            "Topology families to run (default: single direct). 'dual' is still accepted by the "
            "parser but refused at startup: its BGP and SCION sides are different graphs. Use "
            "splitsame and splitvis instead. directsame and directvis "
            "are the direct-peering pair: identical topology, differing only in whether the two "
            "cables of an adjacency share a /30 (handover hidden below IP, the BGP session "
            "survives) or get one each (handover changes the next-hop, the session re-forms). "
            "They share one SCION config, since SCION does not model the distinction."
        ),
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
    parser.add_argument(
        "--snapshot-period",
        type=int,
        default=1,
        help=(
            "path_snapshot_logger period in seconds. This is the time resolution of the SCION "
            "control-plane record, so a longer period only coarsens when you can say an AS "
            "learned or lost a path. With --warm-path-caches off it is cheap, so there is "
            "little reason to raise it. (default: %(default)s)"
        ),
    )
    parser.add_argument(
        "--warm-path-caches",
        action="store_true",
        help=(
            "Re-enable path_snapshot_logger.discover_all_pairs, which asks every AS's host for "
            "paths to every other AS once per snapshot tick. It adds nothing to the snapshot "
            "CSV and costs ~98%% of SCION runtime, which is why it is off by default. It does "
            "not measurably change measured loss: across two sweeps on identical topologies it "
            "moved single and dual_vis by under 1 percentage point with no consistent sign, so "
            "sweeps taken with it on remain valid. Pass it only to reproduce those exactly."
        ),
    )
    parser.add_argument(
        "--warmup",
        type=float,
        default=WARMUP_S,
        help=(
            "Quiet warm-up in seconds before the first link event, so both control planes "
            "converge before churn starts. Statistics should also exclude this window. "
            f"(default: {WARMUP_S})"
        ),
    )
    parser.add_argument(
        "--output-subdir",
        default=default_output_subdir,
        help=(
            "Subdirectory under build/ for generated events, configs, and run outputs "
            f"(default: {default_output_subdir})"
        ),
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

    try:
        timing_points = parse_requested_mrai(args.mrai_values)
    except ValueError as exc:
        print(str(exc))
        return 1

    repo_root = Path(__file__).resolve().parent.parent
    output_subdir = Path(args.output_subdir)
    if output_subdir.parts and output_subdir.parts[0] == "build":
        output_subdir = Path(*output_subdir.parts[1:])
    output_root = repo_root / "build" / output_subdir
    generated_dir = output_root / "generated_events"
    config_dir = output_root / "configs"

    print(f"Output root: {output_root}")

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

    reject_mismatched_families(args.families)

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

    for timing in timing_points:
        print(
            f"\n=== Timing {timing.label}: mrai={timing.mrai_s}s, "
            f"clock={timing.bgp_clock_s}s, probe={timing.probe_period_s}s ==="
        )
        for density in densities:
            print(f"\n  Density {density.name} [{density.min_interval_s:.0f}, {density.max_interval_s:.0f}]")
            for seed in seeds:
                run_key = sanitize_stem(f"{density.name}_seed{seed:02d}_{timing.label}")
                print(f"    Seed {seed} -> {run_key}")

                source_events, metadata = build_stochastic_source_events(
                    density, seed, args.sim_time, args.warmup
                )
                source_path = generated_dir / f"{run_key}_source.json"
                dual_path = generated_dir / f"{run_key}_dual.json"
                single_path = generated_dir / f"{run_key}_single.json"
                direct_bgp_path = generated_dir / f"{run_key}_direct_bgp.json"
                direct_scion_path = generated_dir / f"{run_key}_direct_scion.json"
                split_path = generated_dir / f"{run_key}_split.json"

                # The split families churn each exchange location independently. Reusing the
                # same source trace for both would put the two halves of a constellation in
                # lockstep, so they would hit the break-before-make gap at the same instant and
                # be detached from both exchanges at once — a partition, not a handover. The
                # offset seed keeps location A's churn bit-identical to the other families at
                # this seed, so the comparison to them still holds.
                source_events_loc_b, metadata_loc_b = build_stochastic_source_events(
                    density, seed + LOCATION_B_SEED_OFFSET, args.sim_time, args.warmup
                )

                if not args.dry_run:
                    write_json(
                        source_path,
                        build_source_event_document(density, seed, args.sim_time, source_events, metadata),
                    )
                    write_json(dual_path, build_dual_ixp_events(source_path, source_events))
                    write_json(single_path, build_single_ixp_events(source_path, source_events))
                    write_json(direct_bgp_path, build_direct_events_bgp(source_path, source_events))
                    write_json(direct_scion_path, build_direct_events_scion(source_path, source_events))
                    write_json(
                        split_path,
                        build_split_ixp_events(source_path, source_events, source_events_loc_b),
                    )

                event_paths = {
                    "dual": dual_path,
                    "single": single_path,
                    "direct_bgp": direct_bgp_path,
                    "direct_scion": direct_scion_path,
                    "split": split_path,
                }

                for scenario in args.scenarios:
                    print(f"      Scenario: {scenario}")
                    for family in args.families:
                        # dual_vis reuses the dual trace: its satellite-side interface IDs
                        # are exactly the X0001/X0003 pair that build_dual_ixp_events emits.
                        # splitsame and splitvis share the split trace, which toggles within
                        # each location (X0001/X0002 and X0003/X0004) and names the split ASN
                        # that owns the interface.
                        event_family = {
                            "dual_vis": "dual",
                            "splitsame": "split",
                            "splitvis": "split",
                        }.get(family, family)
                        if not has_template(scenario, family):
                            # Defined only for the other scenario. Skipping rather than
                            # aborting is what lets these families run under the default
                            # --scenarios of "hidden visible". For the direct families this
                            # also matches bgp-ixp-scenarios, which aborts on --directLinks
                            # together with --scenario=hidden.
                            print(
                                f"        {family}: skipped, not defined for scenario "
                                f"'{scenario}'"
                            )
                            continue
                        # The direct families share one trace per protocol, split by protocol
                        # rather than by family: BGP resolves a direct event by interface ID and
                        # takes a gateway index in args[1], while SCION resolves the AS by real
                        # AS number. Since directsame and directvis differ only in subnetting,
                        # the same trace drives both.
                        is_direct = family in ("direct", "directsame", "directvis")
                        event_path_bgp = (
                            event_paths["direct_bgp"] if is_direct else event_paths[event_family]
                        )
                        event_path_scion = (
                            event_paths["direct_scion"] if is_direct else event_paths[event_family]
                        )
                        event_rel_bgp = str(event_path_bgp.relative_to(repo_root))
                        event_rel_scion = str(event_path_scion.relative_to(repo_root))
                        output_suffix = f"{density.name}_seed{seed:02d}_{timing.label}"

                        if "bgp" in args.protocols:
                            if is_direct and scenario == "hidden":
                                # Defensive only: the has_template guard above has already
                                # skipped this combination. Kept because bgp-ixp-scenarios
                                # aborts outright on --directLinks with --scenario=hidden, and
                                # a skip here is a better failure than that abort if the
                                # template table ever grows a hidden direct entry again.
                                print(f"        BGP {family}: skipped (no hidden counterpart)")
                            else:
                                bgp_run_dir = output_subdir / f"stochastic_bgp_{family}_{scenario}_{output_suffix}"
                                bgp_flags = [
                                    f"--scenario={scenario}",
                                    f"--simTime={args.sim_time}",
                                    f"--clockInterval={timing.bgp_clock_s}",
                                    # Tie the reconnect backoff to the FSM clock so it moves
                                    # with the existing timing axis instead of adding a
                                    # dimension. It dominates handover recovery: at the
                                    # libbgp default of 45s a visible handover took a 37.3s
                                    # median to recover, against 14.0s at 5s.
                                    f"--errorHold={timing.bgp_clock_s}",
                                    f"--mrai={timing.mrai_s}",
                                    f"--eventFile={event_rel_bgp}",
                                    f"--outDir=build/{bgp_run_dir.as_posix()}",
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
                                    bgp_flags.append("--splitEdgeAs=1")
                                elif family == "dual_vis":
                                    bgp_flags.append("--dualIxp=1")
                                    bgp_flags.append("--singleLinkPerIxpPair=1")
                                elif family in ("splitsame", "splitvis"):
                                    # Two distant exchanges, no link between them. The pair
                                    # differs only in where each half's backup cable lands:
                                    # same fabric AS (handover hidden below IP, one session
                                    # survives it) or the location's second fabric AS
                                    # (handover changes peer ASN and is visible to BGP).
                                    bgp_flags.append("--dualIxp=1")
                                    bgp_flags.append("--splitEdgeAs=1")
                                    if family == "splitvis":
                                        bgp_flags.append("--splitFabricPerIxp=1")
                                    # Probe each AS at a loopback rather than at one of its
                                    # link addresses. With per-link targets the measurement
                                    # depends on the /30 of a particular cable, and for the
                                    # cross-location pairs that cable is a held-down standby,
                                    # so three of the four report 100% loss with no churn at
                                    # all. Only the split families get this, so the other
                                    # families' published numbers are unaffected.
                                    bgp_flags.append("--loopbackProbeTargets=1")
                                elif family == "single":
                                    bgp_flags.append("--dualIxp=0")
                                    bgp_flags.append("--splitEdgeAs=0")
                                elif family in ("directsame", "directvis"):
                                    # Direct peering, no exchange satellite. The pair differs
                                    # only in subnetting. directsame shares one /30 across
                                    # both cables of an adjacency, so a handover swaps cables
                                    # under an unchanged next-hop and the session survives it
                                    # below IP. directvis gives each cable its own /30, so the
                                    # next-hop changes and the session must go down and
                                    # re-form -- break-before-make above IP, which is what
                                    # keeps libbgp's per-router-ID RIB scoping safe: two
                                    # sessions to one peer node share a scope and would
                                    # collide.
                                    #
                                    # Per-cable /30s make a link-address probe target depend
                                    # on one particular cable, and for a held-down standby
                                    # that reads as 100% loss with no churn at all -- the same
                                    # artefact the split families hit. Both get loopback
                                    # targets so the two are measured the same way.
                                    bgp_flags.append("--directLinks=1")
                                    bgp_flags.append("--loopbackProbeTargets=1")
                                    bgp_flags.append(
                                        "--sharedPairSubnet=1"
                                        if family == "directsame"
                                        else "--sharedPairSubnet=0"
                                    )
                                else:
                                    # Plain `direct`, left as it was so earlier runs stay
                                    # reproducible: per-cable /30s, link-address probe targets.
                                    bgp_flags.append("--directLinks=1")
                                    bgp_flags.append("--sharedPairSubnet=0")

                                run_or_record(
                                    f"bgp/{family}/{scenario}/{output_suffix}",
                                    [str(repo_root / "build/scratch/bgp-ixp-scenarios")] + bgp_flags,
                                )
                                total_runs += 1

                        if "scion" in args.protocols:
                            template = repo_root / SCION_TEMPLATE_PATHS[scenario][family]
                            run_name = f"stochastic_scion_{family}_{scenario}_{output_suffix}"
                            run_prefix = (output_subdir / run_name).as_posix()
                            out_cfg = config_dir / f"{run_name}.yaml"
                            if not args.dry_run:
                                generate_scion_config(
                                    template_path=template,
                                    output_path=out_cfg,
                                    beacon_period_s=timing.beacon_period_s,
                                    run_prefix=run_prefix,
                                    events_file_rel=event_rel_scion,
                                    stochastic_latency=stochastic_latency,
                                )
                                finalize_scion_config_timing(
                                    out_cfg,
                                    args.sim_time,
                                    timing.probe_period_s,
                                    args.snapshot_period,
                                    args.warm_path_caches,
                                )
                                (output_root / run_name).mkdir(parents=True, exist_ok=True)

                            run_or_record(
                                f"scion/{family}/{scenario}/{output_suffix}",
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
