#!/usr/bin/env python3
"""Generate and run IXP switch-event scenarios from gateway up/down traces.

This script converts each link_up/link_down event in a gateway event trace into a
"switch active IXP link" action for mapped satellite ASes:
  1) bring currently active IXP interface down
  2) bring alternate IXP interface up

For now this focuses on IXP scenarios only (visible and hidden AS110 configs).
Direct-link scenarios are intentionally excluded.
"""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


SCENARIO_CONFIGS: Dict[str, Dict[str, Path]] = {
    "single": {
        "visible": Path("configs/scenario_ixp_as110_visible_failover.yaml"),
        "hidden": Path("configs/scenario_ixp_anycast_hidden_failover.yaml"),
    },
    "dual": {
        "visible": Path("configs/scenario_ixp_as110_dual_sat_visible.yaml"),
        "hidden": Path("configs/scenario_ixp_as110_dual_sat_hidden.yaml"),
    },
}


@dataclass
class SwitchEvent:
    time_s: float
    event_type: str
    gateway_asn: int
    source_ifid: int


@dataclass
class LinkPair:
    primary_ifid: int
    backup_ifid: int


def run_cmd(cmd: List[str]) -> None:
    print(f"$ {' '.join(shlex.quote(part) for part in cmd)}")
    result = subprocess.run(cmd, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}")


def parse_time_s(raw: str) -> float:
    value = raw.strip()
    if value.endswith("s"):
        value = value[:-1]
    return float(value)


def parse_gateway_map(raw: str) -> Dict[int, int]:
    mapping: Dict[int, int] = {}
    for item in raw.split(","):
        item = item.strip()
        if not item:
            continue
        gateway_raw, asn_raw = item.split(":", 1)
        mapping[int(gateway_raw)] = int(asn_raw)
    if not mapping:
        raise ValueError("Gateway mapping is empty")
    return mapping


def default_link_pair_for_as(sat_asn: int, ixp_satellite: str = "single") -> LinkPair:
    """Generate link pairs for satellite AS.
    
    single: links to one IXP satellite (AS110)
      - AS102: primary=1020001, backup=1020002
    dual-a: links to first IXP satellite (AS120, primary orbit)
      - AS102: primary=1020001, backup=1020002
    dual-b: links to second IXP satellite (AS121, backup orbit)
      - AS102: primary=1020003, backup=1020004
    """
    if ixp_satellite == "dual-a":
        # Links to AS120 (logical 110A)
        return LinkPair(primary_ifid=sat_asn * 10000 + 1, backup_ifid=sat_asn * 10000 + 2)
    elif ixp_satellite == "dual-b":
        # Links to AS121 (logical 110B)
        return LinkPair(primary_ifid=sat_asn * 10000 + 3, backup_ifid=sat_asn * 10000 + 4)
    else:  # single
        return LinkPair(primary_ifid=sat_asn * 10000 + 1, backup_ifid=sat_asn * 10000 + 2)


def load_source_events(path: Path) -> List[SwitchEvent]:
    doc = json.loads(path.read_text())
    events = doc.get("events", [])
    out: List[SwitchEvent] = []
    for item in events:
        event_type = str(item.get("type", "")).strip().lower()
        if event_type not in {"link_up", "link_down"}:
            continue
        args = item.get("args", [])
        if not isinstance(args, list) or len(args) < 3:
            continue
        out.append(
            SwitchEvent(
                time_s=parse_time_s(str(item.get("time", "0s"))),
                event_type=event_type,
                gateway_asn=int(str(args[1])),
                source_ifid=int(str(args[2])),
            )
        )
    return out


def build_ixp_switch_events(
    source_events: List[SwitchEvent],
    gateway_to_sat_as: Dict[int, int],
    switch_delta_s: float,
    dual_ixp: bool = False,
) -> Tuple[List[dict], Dict[int, int]]:
    """Generate IXP switch events.
    
    single-IXP mode: toggles between two links to the same IXP satellite.
    dual-IXP mode: toggles between two IXP satellites (120 and 121).
    """
    active_if_by_as: Dict[int, int] = {}
    active_ixp_by_as: Dict[int, str] = {}
    
    for sat_asn in set(gateway_to_sat_as.values()):
        if dual_ixp:
            active_ixp_by_as[sat_asn] = "dual-a"
            active_if_by_as[sat_asn] = default_link_pair_for_as(sat_asn, "dual-a").primary_ifid
        else:
            active_if_by_as[sat_asn] = default_link_pair_for_as(sat_asn, "single").primary_ifid

    generated: List[dict] = []
    ordered = sorted(enumerate(source_events), key=lambda it: (it[1].time_s, it[0]))
    # Deduplication: only one toggle per satellite AS per timestamp.
    # Multiple source links can share the same gateway → same sat_asn.
    seen_toggle: set = set()

    for _, src in ordered:
        sat_asn = gateway_to_sat_as.get(src.gateway_asn)
        if sat_asn is None:
            continue

        # Skip if this satellite AS already has a toggle scheduled at this timestamp
        dedup_key = (src.time_s, sat_asn)
        if dedup_key in seen_toggle:
            continue
        seen_toggle.add(dedup_key)

        if dual_ixp:
            # Toggle between IXP satellites A and B
            current_ixp = active_ixp_by_as[sat_asn]
            next_ixp = "dual-b" if current_ixp == "dual-a" else "dual-a"
            
            # Down current IXP links
            current_pair = default_link_pair_for_as(sat_asn, current_ixp)
            current_if = current_pair.primary_ifid
            
            # Up next IXP links
            next_pair = default_link_pair_for_as(sat_asn, next_ixp)
            next_if = next_pair.primary_ifid
            
            ixp_a_str = "120" if current_ixp == "dual-a" else "121"
            ixp_b_str = "121" if next_ixp == "dual-b" else "120"
            
            generated.append(
                {
                    "time": f"{src.time_s:.3f}s",
                    "type": "link_down",
                    "args": ["1", str(sat_asn), str(current_if)],
                    "description": (
                        f"Dual-IXP: gateway={src.gateway_asn} {src.event_type} → "
                        f"switch from IXP-sat-{ixp_a_str} to IXP-sat-{ixp_b_str}: down {current_if}"
                    ),
                }
            )
            generated.append(
                {
                    "time": f"{src.time_s + switch_delta_s:.3f}s",
                    "type": "link_up",
                    "args": ["1", str(sat_asn), str(next_if)],
                    "description": (
                        f"Dual-IXP: gateway={src.gateway_asn} {src.event_type} → "
                        f"switch from IXP-sat-{ixp_a_str} to IXP-sat-{ixp_b_str}: up {next_if}"
                    ),
                }
            )
            active_ixp_by_as[sat_asn] = next_ixp
        else:
            # Single-IXP: toggle between two links to same IXP
            pair = default_link_pair_for_as(sat_asn, "single")
            current = active_if_by_as[sat_asn]
            alternate = pair.backup_ifid if current == pair.primary_ifid else pair.primary_ifid

            generated.append(
                {
                    "time": f"{src.time_s:.3f}s",
                    "type": "link_down",
                    "args": ["1", str(sat_asn), str(current)],
                    "description": (
                        f"Switch trigger from gateway={src.gateway_asn} {src.event_type} "
                        f"(src_if={src.source_ifid}): down current IXP link {current}"
                    ),
                }
            )
            generated.append(
                {
                    "time": f"{src.time_s + switch_delta_s:.3f}s",
                    "type": "link_up",
                    "args": ["1", str(sat_asn), str(alternate)],
                    "description": (
                        f"Switch trigger from gateway={src.gateway_asn} {src.event_type} "
                        f"(src_if={src.source_ifid}): up alternate IXP link {alternate}"
                    ),
                }
            )
            active_if_by_as[sat_asn] = alternate

    return generated, active_if_by_as


def compute_duration_s(events: List[dict], guard_s: float, max_sim_time_s: float = 0.0) -> float:
    if not events:
        return 260.0
    last = 0.0
    for ev in events:
        t = parse_time_s(str(ev.get("time", "0s")))
        if t > last:
            last = t
    duration = last + guard_s
    if max_sim_time_s > 0.0:
        duration = min(duration, max_sim_time_s)
    return duration


def _adaptive_snapshot_period(sim_duration_s: float) -> str:
    """Return a snapshot period appropriate for the simulation duration.

    For long simulations (>1000s) the default 1s period generates millions of
    CSV rows and is the dominant runtime cost.  We scale automatically:
      <=  260s  ->  1s    (original short scenarios)
      <= 1000s  ->  10s
      <= 5000s  ->  60s
      >  5000s  -> 300s
    """
    if sim_duration_s <= 260:
        return "1s"
    if sim_duration_s <= 1000:
        return "10s"
    if sim_duration_s <= 5000:
        return "60s"
    return "300s"


def render_config_for_events(
    template_path: Path,
    out_config_path: Path,
    generated_events_path: Path,
    run_prefix: str,
    sim_duration_s: float,
    verbose_cp_logging: bool = False,
) -> None:
    text = template_path.read_text()
    lines = text.splitlines()

    in_data_plane = False
    in_cp_logging = False
    out_lines: List[str] = []

    probe_start_s = 10.0
    probe_period_s = 1.0
    probe_count = max(1, int((sim_duration_s - probe_start_s) / probe_period_s))
    snapshot_period = _adaptive_snapshot_period(sim_duration_s)
    if sim_duration_s > 260:
        print(f"  [perf] snapshot period set to {snapshot_period} (sim={sim_duration_s:.0f}s)")

    for line in lines:
        stripped = line.strip()

        if stripped.startswith("data_plane_probing:"):
            in_data_plane = True
            in_cp_logging = False
            out_lines.append(line)
            continue

        if stripped.startswith("control_plane_logging:"):
            in_cp_logging = True
            in_data_plane = False
            out_lines.append(line)
            continue

        if (in_data_plane or in_cp_logging) and line and not line[0].isspace():
            in_data_plane = False
            in_cp_logging = False

        if in_cp_logging and not verbose_cp_logging:
            # Suppress high-volume CP log flags for long runs
            if stripped.startswith("beacon_events:"):
                indent = line[: len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}beacon_events: false")
                continue
            if stripped.startswith("path_server_updates:"):
                indent = line[: len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}path_server_updates: false")
                continue
            if stripped.startswith("host_cache_updates:"):
                indent = line[: len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}host_cache_updates: false")
                continue

        if stripped.startswith("events_file:"):
            out_lines.append(f"events_file: {generated_events_path.as_posix()}")
            continue

        if stripped.startswith("simulation_duration:"):
            out_lines.append(f"simulation_duration: {sim_duration_s:.3f}s")
            continue

        if stripped.startswith("output:") and line.startswith("output:"):
            out_lines.append(f"output: build/{run_prefix}.txt")
            continue

        if stripped.startswith("last_beaconing:"):
            indent = line[: len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}last_beaconing: {sim_duration_s:.3f}s")
            continue

        if stripped.startswith("log_file:"):
            indent = line[: len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}log_file: build/{run_prefix}_control_plane.log")
            continue

            if stripped.startswith("cp_summary_output:"):
                out_lines.append(f"cp_summary_output: build/{run_prefix}_cp_summary.csv")
                continue

        if stripped.startswith("output:") and line.startswith("  output:"):
            indent = line[: len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}output: build/{run_prefix}_path_snapshots.csv")
            continue

        if stripped.startswith("period:") and "path_snapshot_logger" in "".join(out_lines[-10:]):
            # Only rewrite the path_snapshot_logger period, not the beacon period
            indent = line[: len(line) - len(line.lstrip())]
            if indent.startswith("  "):  # nested under path_snapshot_logger
                out_lines.append(f"{indent}period: {snapshot_period}")
                continue

        if in_data_plane and stripped.startswith("count:"):
            indent = line[: len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}count: {probe_count}")
            continue

        out_lines.append(line)

    out_config_path.write_text("\n".join(out_lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Generate switch-style IXP event schedules from gateway up/down traces and run IXP scenarios"
        )
    )
    parser.add_argument(
        "--input-events",
        default="user_defined_events/gateway_links_filtered_uncorrelated.json",
        help="Source gateway event JSON",
    )
    parser.add_argument(
        "--generated-events",
        default="user_defined_events/scenario_ixp_gateway_switch_generated_events.json",
        help="Output JSON path for generated IXP switch events",
    )
    parser.add_argument(
        "--gateway-map",
        default="1:102,2:103,3:104,4:105",
        help="Gateway-to-satellite-AS mapping (comma-separated gateway:sat_as)",
    )
    parser.add_argument(
        "--switch-delta",
        type=float,
        default=0.001,
        help="Delay in seconds between generated link_down and link_up for each switch",
    )
    parser.add_argument(
        "--duration-guard",
        type=float,
        default=30.0,
        help="Extra seconds added to simulation duration after last generated event",
    )
    parser.add_argument(
        "--max-sim-time",
        type=float,
        default=0.0,
        help="Cap simulation duration (seconds). 0 = no cap (default)",
    )
    parser.add_argument(
        "--verbose-cp-logging",
        action="store_true",
        help="Enable high-volume beacon_events/path_server_updates in CP log (slow for long sims)",
    )
    parser.add_argument(
        "--profile",
        action="store_true",
        help="Run with gprof instrumentation: builds with -pg and writes gmon.out",
    )
    parser.add_argument(
        "--dual-ixp",
        action="store_true",
        help="Use dual-IXP-satellite scenarios (AS120 and AS121) instead of single (AS110)",
    )
    parser.add_argument(
        "--scenarios",
        nargs="+",
        choices=["visible", "hidden"],
        default=["visible", "hidden"],
        help="IXP scenarios to run (direct-link scenarios intentionally excluded)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip ./waf build before runs",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only generate files and print the run commands",
    )
    args = parser.parse_args()

    input_events = Path(args.input_events)
    generated_events = Path(args.generated_events)
    ixp_mode = "dual" if args.dual_ixp else "single"

    gateway_map = parse_gateway_map(args.gateway_map)
    source_events = load_source_events(input_events)
    generated, final_active = build_ixp_switch_events(source_events, gateway_map, args.switch_delta, args.dual_ixp)

    generated_events.parent.mkdir(parents=True, exist_ok=True)
    generated_doc = {
        "description": (
            f"Generated IXP switch events ({ixp_mode} mode) from gateway link up/down trace; "
            f"each source event toggles active satellite<->IXP link"
        ),
        "source_file": input_events.as_posix(),
        "gateway_to_satellite_map": gateway_map,
        "ixp_mode": ixp_mode,
        "event_count_source": len(source_events),
        "event_count_generated": len(generated),
        "events": generated,
    }
    generated_events.write_text(json.dumps(generated_doc, indent=2) + "\n")

    sim_duration_s = compute_duration_s(generated, args.duration_guard, args.max_sim_time)
    generated_config_paths: List[Path] = []

    for scenario in args.scenarios:
        template = SCENARIO_CONFIGS[ixp_mode][scenario]
        out_cfg = Path("configs") / f"scenario_ixp_{ixp_mode}_sat_{scenario}_gateway_switch_generated.yaml"
        run_prefix = f"scenario_ixp_{ixp_mode}_sat_{scenario}_gateway_switch_generated"
        render_config_for_events(
            template,
            out_cfg,
            generated_events,
            run_prefix,
            sim_duration_s,
            verbose_cp_logging=args.verbose_cp_logging,
        )
        generated_config_paths.append(out_cfg)

    print(f"Wrote generated events: {generated_events}")
    print(f"IXP mode: {ixp_mode}")
    print(f"Source events: {len(source_events)}  Generated events: {len(generated)}")
    print(f"Simulation duration set to: {sim_duration_s:.3f}s")
    print(f"Final active if_id per satellite AS: {final_active}")
    print("Generated configs:")
    for cfg in generated_config_paths:
        print(f"  - {cfg}")

    if args.dry_run:
        print("Dry-run requested; skipping build and scenario execution")
        for cfg in generated_config_paths:
            print(f"  ./waf --run \"scion {cfg.as_posix()}\"")
        return 0

    if args.profile:
        print("Building with gprof instrumentation (-pg)...")
        if not args.skip_build:
            run_cmd(["./waf", "build", "--cxxflags=-pg", "--ldflags=-pg"])
        for cfg in generated_config_paths:
            run_cmd(["./waf", "--run", f"scion {cfg.as_posix()}"])
            gmon = Path("gmon.out")
            if gmon.exists():
                gprof_out = Path("build") / f"{cfg.stem}_gprof.txt"
                try:
                    result = subprocess.run(
                        ["gprof", "build/scratch/scion", "gmon.out"],
                        capture_output=True,
                        text=True,
                    )
                    gprof_out.write_text(result.stdout)
                    print(f"  gprof output: {gprof_out}")
                except FileNotFoundError:
                    print("  gprof not found; gmon.out written, run: gprof build/scratch/scion gmon.out")
        return 0

    if not args.skip_build:
        run_cmd(["./waf", "build"])

    for cfg in generated_config_paths:
        run_cmd(["./waf", "--run", f"scion {cfg.as_posix()}"])

    print(f"PASS: Generated IXP gateway-switch scenarios ({ixp_mode} mode) completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
