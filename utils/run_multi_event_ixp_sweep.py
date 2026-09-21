#!/usr/bin/env python3
"""Run large BGP/SCION sweeps across many filtered event traces.

This script expands each source event trace in user_defined_events/filtered into
scenario-specific event files for:
  - dual IXP switch model
  - single IXP switch model
  - direct-link switch model

Then, for each selected timer sweep point, it runs BGP and/or SCION for hidden /
visible scenarios and for all enabled topology families.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


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

SCION_TEMPLATE_PATHS: Dict[str, Dict[str, Path]] = {
    "hidden": {
        "dual": Path("configs/scenario_ixp_dual_sat_hidden_gateway_switch_generated.yaml"),
        "single": Path("configs/scenario_ixp_single_sat_hidden_gateway_switch_generated.yaml"),
        # No direct entry. The hidden axis routes link events through the VirtualIxpFabric, and
        # without an exchange satellite there is no fabric to hide behind. The config that used
        # to sit here was a copy of the dual-satellite one carrying a virtual_ixp block of
        # sixteen interface IDs absent from the direct topology, which ResolveScionIfId's
        # `if_id % 10000` fallback silently remapped onto unrelated links.
    },
    "visible": {
        "dual": Path("configs/scenario_ixp_dual_sat_visible_gateway_switch_generated.yaml"),
        # Dual IXP with one link per satellite-IXP AS pair: the visible-handover family. A
        # handover moves between two different IXP ASes, so it is exposed to the control
        # plane, unlike the single-IXP family where the switch happens below IP.
        "dual_vis": Path("configs/scenario_ixp_dual_sat_visible_single_link_generated.yaml"),
        "single": Path("configs/scenario_ixp_single_sat_visible_gateway_switch_generated.yaml"),
        # Direct peering, no exchange satellite. All three direct families share ONE SCION
        # config, because the contrast between directsame and directvis is an IP-level one
        # (shared /30 versus a /30 per cable) that SCION does not model: same topology, same
        # interface-level handover, so two configs would mean two byte-identical simulations.
        # The config's trailer records this in full.
        "direct": Path("configs/scenario_ixp_direct_generated.yaml"),
        "directsame": Path("configs/scenario_ixp_direct_generated.yaml"),
        "directvis": Path("configs/scenario_ixp_direct_generated.yaml"),
        # Two distant IXP satellites with NO link between them. Each constellation is split in
        # two, one half homed at each exchange, joined by a 20ms internal link that is the only
        # path between locations and supplies the path stretch. The handover stays within a
        # location; the pair differs only in where the backup cable lands, and therefore only in
        # whether the handover is hidden below IP or visible to the control plane.
        "splitsame": Path("configs/scenario_ixp_split_samefabric_generated.yaml"),
        "splitvis": Path("configs/scenario_ixp_split_splitfabric_generated.yaml"),
    },
}

# Families whose BGP and SCION sides describe different graphs. Any loss, recovery or latency
# number produced from one of these compares two different experiments, so the sweep runners
# refuse them outright. They stay in the argparse choices so the refusal can name the problem
# and the replacement, rather than argparse reporting a bare "invalid choice".
MISMATCHED_FAMILIES: Dict[str, str] = {
    "dual": (
        "BGP builds the split-edge topology (28 links, ASNs 1020/1021/...) while the SCION "
        "template loads topology/ixp_as110_dual_satellites_topology.xml (20 links, ASNs "
        "102-105), so the two protocols never ran the same graph. Use splitsame (handover "
        "hidden below IP) and splitvis (handover visible to the control plane) instead: they "
        "are the two-IXP families that replaced it, and both are topology-matched by "
        "construction because their XMLs are generated from the BGP link table."
    ),
}


def reject_mismatched_families(families: Iterable[str]) -> None:
    """Abort if any requested family's BGP and SCION sides are not the same topology.

    Args:
        families: Family names requested on the command line.

    Raises:
        SystemExit: If a requested family is listed in ``MISMATCHED_FAMILIES``.
    """
    for family in families:
        reason = MISMATCHED_FAMILIES.get(family)
        if reason is not None:
            raise SystemExit(f"family '{family}' does not produce a valid comparison.\n{reason}")


# Source gateway IDs map to edge AS numbers in IXP scenarios.
GATEWAY_TO_AS: Dict[int, int] = {
    1: 102,
    2: 103,
    3: 104,
    4: 105,
}

# Direct-link mapping: gateway -> (primary_if, backup_if, description)
#
# Four gateway streams, deliberately, and not one per mesh adjacency. The source trace carries
# exactly four (GATEWAY_TO_AS), and in the IXP families each one churns a single satellite-IXP
# adjacency. Mapping the same four streams onto four of the direct mesh's six adjacencies keeps
# the perturbation load identical across families, which is the whole point of the comparison.
# Driving all six would give the direct family half again as much churn as the IXP families and
# make the loss and convergence figures incomparable. The four chosen pairs touch each of
# AS102-105 exactly twice, so no satellite is over- or under-exercised.
#
# What does differ, unavoidably, is the fraction of a satellite's connectivity under churn: in
# the IXP families a satellite has one adjacency and it churns, whereas here it has five of
# which two churn. That is inherent to comparing a mesh against a hub and belongs in the
# results, not in the event generator.
GATEWAY_TO_DIRECT: Dict[int, Tuple[int, int, str]] = {
    1: (1020100, 1020101, "AS102-AS103"),
    2: (1030120, 1030121, "AS103-AS105"),
    3: (1040120, 1040121, "AS104-AS105"),
    4: (1020110, 1020111, "AS102-AS104"),
}

# Every standby cable in the direct mesh, on the side of the AS that owns the interface ID:
# six pairs, units digit 1 marking the standby. This mirrors what the BGP program derives from
# its own link table for --backupLinksDownAtStart, and it must stay in step with
# BuildTopologyLinksDirect() in scratch/bgp-ixp-scenarios.cc.
DIRECT_STANDBY_IF_IDS: Tuple[int, ...] = (
    1020101, 1020111, 1020121, 1030111, 1030121, 1040121,
)

SWITCH_DELTA_S = 0.001

# When the split-mode trace downs each half's standby cable to establish the initial state.
# Matches the time the BGP program uses for its own backup-down-at-start schedule, and is well
# before BGP starts at t=1s or probing at t=10s.
STANDBY_DOWN_TIME_S = 0.1
SIM_TIME_S = 10010
EXPIRATION_RATIO = 135  # expiration_period = beacon_period * this ratio


@dataclass
class SourceEvent:
    time_s: float
    event_type: str
    gateway_id: int
    source_ifid: str


def parse_time_s(raw: str) -> Optional[float]:
    value = str(raw).strip()
    if value.endswith("s"):
        value = value[:-1]
    try:
        return float(value)
    except ValueError:
        return None


def sanitize_stem(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name)


def run_cmd(cmd: List[str], dry_run: bool = False) -> int:
    print(f"  $ {' '.join(shlex.quote(str(c)) for c in cmd)}")
    if dry_run:
        return 0
    env = os.environ.copy()
    build_lib = str(Path(__file__).resolve().parent.parent / "build" / "lib")
    current_ld = env.get("LD_LIBRARY_PATH", "")
    env["LD_LIBRARY_PATH"] = build_lib if not current_ld else f"{build_lib}:{current_ld}"
    result = subprocess.run(cmd, text=True, env=env)
    return result.returncode


def parse_source_events(path: Path) -> List[SourceEvent]:
    data = json.loads(path.read_text(encoding="utf-8"))
    events = data.get("events", [])
    parsed: List[SourceEvent] = []
    for raw in events:
        if raw.get("type") not in {"link_up", "link_down"}:
            continue
        args = raw.get("args", [])
        if len(args) < 3:
            continue
        time_s = parse_time_s(raw.get("time", ""))
        if time_s is None:
            continue
        try:
            gateway_id = int(str(args[1]))
        except ValueError:
            continue
        if gateway_id not in GATEWAY_TO_AS:
            continue
        parsed.append(
            SourceEvent(
                time_s=time_s,
                event_type=str(raw.get("type")),
                gateway_id=gateway_id,
                source_ifid=str(args[2]),
            )
        )
    parsed.sort(key=lambda e: e.time_s)
    return parsed


def _format_time_s(time_s: float) -> str:
    return f"{time_s:.3f}s"


def _toggle_events(
    source_events: Iterable[SourceEvent],
    primary_by_gateway: Dict[int, int],
    backup_by_gateway: Dict[int, int],
    arg2_by_gateway: Dict[int, str],
    description_prefix: str,
) -> Tuple[List[Dict[str, object]], Dict[str, int]]:
    active_if: Dict[int, int] = dict(primary_by_gateway)
    out: List[Dict[str, object]] = []
    for src in source_events:
        gateway = src.gateway_id
        current_if = active_if[gateway]
        next_if = backup_by_gateway[gateway] if current_if == primary_by_gateway[gateway] else primary_by_gateway[gateway]

        out.append(
            {
                "time": _format_time_s(src.time_s),
                "type": "link_down",
                "args": ["1", arg2_by_gateway[gateway], str(current_if)],
                "description": (
                    f"{description_prefix}: gateway={gateway} {src.event_type} "
                    f"(src_if={src.source_ifid}) -> down {current_if}"
                ),
            }
        )
        out.append(
            {
                "time": _format_time_s(src.time_s + SWITCH_DELTA_S),
                "type": "link_up",
                "args": ["1", arg2_by_gateway[gateway], str(next_if)],
                "description": (
                    f"{description_prefix}: gateway={gateway} {src.event_type} "
                    f"(src_if={src.source_ifid}) -> up {next_if}"
                ),
            }
        )
        active_if[gateway] = next_if

    return out, {str(k): v for k, v in active_if.items()}


def build_single_ixp_events(source_path: Path, source_events: List[SourceEvent]) -> Dict[str, object]:
    primary = {g: asn * 10000 + 1 for g, asn in GATEWAY_TO_AS.items()}
    backup = {g: asn * 10000 + 2 for g, asn in GATEWAY_TO_AS.items()}
    arg2 = {g: str(GATEWAY_TO_AS[g]) for g in GATEWAY_TO_AS}

    events, final_active = _toggle_events(
        source_events,
        primary_by_gateway=primary,
        backup_by_gateway=backup,
        arg2_by_gateway=arg2,
        description_prefix="Single-IXP switch",
    )

    return {
        "description": "Generated IXP switch events (single mode) from gateway trace",
        "source_file": str(source_path),
        "gateway_to_satellite_map": {str(k): v for k, v in GATEWAY_TO_AS.items()},
        "ixp_mode": "single",
        "switch_delta_s": SWITCH_DELTA_S,
        "event_count_source": len(source_events),
        "event_count_generated": len(events),
        "final_active_if_per_gateway": final_active,
        "events": events,
    }


def build_split_ixp_events(
    source_path: Path,
    source_events_loc_a: List[SourceEvent],
    source_events_loc_b: List[SourceEvent],
) -> Dict[str, object]:
    """Build a within-location handover trace for the split-edge two-IXP topology.

    Each constellation is split into two ASes, one homed at each exchange satellite, and each
    half owns a main and a backup cable to its own exchange. A handover toggles between those
    two cables and never crosses to the other exchange, so this differs from
    ``build_dual_ixp_events``, which toggles X0001 against X0003 and therefore moves the
    satellite between locations.

    The two locations get independent source traces. In lockstep both halves of a constellation
    would hit the break-before-make gap at the same instant, detaching that constellation from
    both exchanges simultaneously — a partition rather than a handover — and the relative phase
    of the two locations would never vary across seeds.

    Note the interface IDs derive from the *base* AS while ownership is by *split* ASN: AS1020
    owns 1020001 and 1020002, AS1021 owns 1020003 and 1020004. ``args[1]`` must name the split
    ASN, because that is the AS that resolves the interface.

    Args:
        source_path: Path of the source gateway trace, recorded for provenance.
        source_events_loc_a: Gateway trace driving the location-A (``*0``) halves.
        source_events_loc_b: Gateway trace driving the location-B (``*1``) halves.

    Returns:
        An events document ready to serialise as the run's events JSON.
    """
    loc_a_events, loc_a_final = _toggle_events(
        source_events_loc_a,
        primary_by_gateway={g: asn * 10000 + 1 for g, asn in GATEWAY_TO_AS.items()},
        backup_by_gateway={g: asn * 10000 + 2 for g, asn in GATEWAY_TO_AS.items()},
        arg2_by_gateway={g: str(asn * 10) for g, asn in GATEWAY_TO_AS.items()},
        description_prefix="Split-IXP loc-A switch",
    )
    loc_b_events, loc_b_final = _toggle_events(
        source_events_loc_b,
        primary_by_gateway={g: asn * 10000 + 3 for g, asn in GATEWAY_TO_AS.items()},
        backup_by_gateway={g: asn * 10000 + 4 for g, asn in GATEWAY_TO_AS.items()},
        arg2_by_gateway={g: str(asn * 10 + 1) for g, asn in GATEWAY_TO_AS.items()},
        description_prefix="Split-IXP loc-B switch",
    )

    # Establish the initial state explicitly, in the trace itself.
    #
    # The BGP program holds each half's backup cable down from t=0.1s so exactly one cable per
    # adjacency is live. SCION has no equivalent: its topology XML brings every link up. Without
    # these events the two protocols start from different states and, worse, SCION never sees a
    # handover at all — a link_down on the main cable would merely remove one of two live cables
    # and the paired link_up on the backup would be a no-op. Emitting the downs here makes the
    # starting state part of the shared trace rather than a per-protocol default. They are
    # harmless to BGP, which has already downed the same cables by the time these fire.
    standby_events = [
        {
            "time": _format_time_s(STANDBY_DOWN_TIME_S),
            "type": "link_down",
            "args": ["1", str(asn * 10 + half), str(asn * 10000 + slot)],
            "description": (
                f"Split-IXP initial state: standby cable {asn * 10000 + slot} down so one "
                f"cable per adjacency is live"
            ),
        }
        for asn in GATEWAY_TO_AS.values()
        for half, slot in ((0, 2), (1, 4))
    ]

    events = sorted(
        standby_events + loc_a_events + loc_b_events,
        key=lambda e: (float(str(e["time"]).rstrip("s")), e["args"][1], e["args"][2]),
    )

    return {
        "description": "Generated IXP switch events (split mode) from two gateway traces",
        "source_file": str(source_path),
        "gateway_to_satellite_map": {str(k): v for k, v in GATEWAY_TO_AS.items()},
        "ixp_mode": "split",
        "switch_delta_s": SWITCH_DELTA_S,
        "event_count_source": len(source_events_loc_a) + len(source_events_loc_b),
        "event_count_generated": len(events),
        "final_active_if_per_gateway_loc_a": loc_a_final,
        "final_active_if_per_gateway_loc_b": loc_b_final,
        "events": events,
    }


def build_dual_ixp_events(source_path: Path, source_events: List[SourceEvent]) -> Dict[str, object]:
    primary = {g: asn * 10000 + 1 for g, asn in GATEWAY_TO_AS.items()}
    backup = {g: asn * 10000 + 3 for g, asn in GATEWAY_TO_AS.items()}
    arg2 = {g: str(GATEWAY_TO_AS[g]) for g in GATEWAY_TO_AS}

    events, final_active = _toggle_events(
        source_events,
        primary_by_gateway=primary,
        backup_by_gateway=backup,
        arg2_by_gateway=arg2,
        description_prefix="Dual-IXP switch",
    )

    return {
        "description": "Generated IXP switch events (dual mode) from gateway trace",
        "source_file": str(source_path),
        "gateway_to_satellite_map": {str(k): v for k, v in GATEWAY_TO_AS.items()},
        "ixp_mode": "dual",
        "switch_delta_s": SWITCH_DELTA_S,
        "event_count_source": len(source_events),
        "event_count_generated": len(events),
        "final_active_if_per_gateway": final_active,
        "events": events,
    }


def build_direct_events_bgp(source_path: Path, source_events: List[SourceEvent]) -> Dict[str, object]:
    primary = {g: v[0] for g, v in GATEWAY_TO_DIRECT.items()}
    backup = {g: v[1] for g, v in GATEWAY_TO_DIRECT.items()}
    arg2 = {g: str(g) for g in GATEWAY_TO_DIRECT}

    events, final_active = _toggle_events(
        source_events,
        primary_by_gateway=primary,
        backup_by_gateway=backup,
        arg2_by_gateway=arg2,
        description_prefix="Direct-link switch",
    )

    return {
        "description": "Generated direct-link switch events for BGP (gateway-index addressing)",
        "source_file": str(source_path),
        "gateway_link_map": {
            str(k): {
                "primary": v[0],
                "backup": v[1],
                "description": v[2],
            }
            for k, v in GATEWAY_TO_DIRECT.items()
        },
        "switch_delta_s": SWITCH_DELTA_S,
        "event_count_source": len(source_events),
        "event_count_generated": len(events),
        "final_active_if_per_gateway": final_active,
        "events": events,
    }


def build_direct_events_scion(source_path: Path, source_events: List[SourceEvent]) -> Dict[str, object]:
    """SCION direct-link events use real AS numbers in args[1].

    UserDefinedEvents::LinkDown/LinkUp resolves AS by real AS number. Using gateway
    IDs (1..4) causes event drops with "AS not found" warnings.

    The trace also has to establish the initial state, exactly as ``build_split_ixp_events``
    does and for the same reason. The BGP program holds every standby cable down from t=0.1s so
    one cable per adjacency is live; SCION has no equivalent, because its topology XML brings
    every link up. Without the standby-down events below the two protocols start from different
    states, and worse, SCION never observes a handover at all: a ``link_down`` on the main cable
    merely removes one of two live interfaces, leaves ``interfaces_per_neighbor_as`` non-empty so
    the AS-level graph is unchanged, and makes the paired ``link_up`` on the standby a no-op.
    That is why the direct family currently shows no handover impact in SCION.
    """
    primary = {g: v[0] for g, v in GATEWAY_TO_DIRECT.items()}
    backup = {g: v[1] for g, v in GATEWAY_TO_DIRECT.items()}
    # Use the AS encoded in IF-ID prefix (e.g., 1020100 -> AS102).
    arg2 = {g: str(v[0] // 10000) for g, v in GATEWAY_TO_DIRECT.items()}

    toggles, final_active = _toggle_events(
        source_events,
        primary_by_gateway=primary,
        backup_by_gateway=backup,
        arg2_by_gateway=arg2,
        description_prefix="Direct-link switch",
    )

    if_to_gateway: Dict[int, int] = {}
    for gateway, (gw_primary, gw_backup, _desc) in GATEWAY_TO_DIRECT.items():
        if_to_gateway[gw_primary] = gateway
        if_to_gateway[gw_backup] = gateway

    # Which cable is live on each churning adjacency by the time the preamble fires. A source
    # event before STANDBY_DOWN_TIME_S has already handed that adjacency over, so downing its
    # nominal standby would take down the cable the toggle just brought up and leave the
    # adjacency with NO live cable until its next event -- a partition rather than a handover.
    # Down the other cable instead. With a warm-up, as every sweep runner uses, no toggle
    # precedes the preamble and this reduces to downing the nominal standby.
    active_at_preamble = {g: v[0] for g, v in GATEWAY_TO_DIRECT.items()}
    for toggle in toggles:  # already in time order
        if float(str(toggle["time"]).rstrip("s")) > STANDBY_DOWN_TIME_S:
            break
        if toggle["type"] != "link_up":
            continue
        toggled_if = int(str(toggle["args"][2]))
        gateway = if_to_gateway.get(toggled_if)
        if gateway is not None:
            active_at_preamble[gateway] = toggled_if

    # Every standby goes down, not just those of the four churning adjacencies, because the BGP
    # program downs every standby it derives from its link table. Downing only the churning ones
    # would leave the other eight adjacencies double-cabled in SCION and single-cabled in BGP.
    standby_events: List[Dict[str, object]] = []
    for if_id in DIRECT_STANDBY_IF_IDS:
        gateway = if_to_gateway.get(if_id)
        if gateway is None:
            down_if = if_id  # adjacency never churns; its nominal standby is the idle cable
        else:
            gw_primary, gw_backup, _desc = GATEWAY_TO_DIRECT[gateway]
            down_if = gw_backup if active_at_preamble[gateway] == gw_primary else gw_primary
        standby_events.append(
            {
                "time": _format_time_s(STANDBY_DOWN_TIME_S),
                "type": "link_down",
                "args": ["1", str(down_if // 10000), str(down_if)],
                "description": (
                    f"DirectLink initial state: standby cable {down_if} down so one cable per "
                    f"adjacency is live"
                ),
            }
        )

    # BGP schedules its backup-down unconditionally at 0.1s, so on a trace with events that
    # early the two protocols start from different states despite the correction above. Every
    # sweep runner warms up for 60s, so this is a warning about hand-written traces rather than
    # about the sweeps.
    early = [e.time_s for e in source_events if e.time_s < STANDBY_DOWN_TIME_S]
    if early:
        print(
            f"  WARNING: {len(early)} source event(s) at t < {STANDBY_DOWN_TIME_S}s in "
            f"{source_path.name}; SCION compensates but BGP's backup-down does not"
        )

    events = sorted(
        standby_events + toggles,
        key=lambda e: (float(str(e["time"]).rstrip("s")), e["args"][1], e["args"][2]),
    )

    return {
        "description": "Generated direct-link switch events for SCION (real-AS addressing)",
        "source_file": str(source_path),
        "gateway_link_map": {
            str(k): {
                "primary": v[0],
                "backup": v[1],
                "description": v[2],
            }
            for k, v in GATEWAY_TO_DIRECT.items()
        },
        "switch_delta_s": SWITCH_DELTA_S,
        "event_count_source": len(source_events),
        "event_count_generated": len(events),
        "final_active_if_per_gateway": final_active,
        "events": events,
    }


def write_json(path: Path, data: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def replace_top_level_block(text: str, key: str, replacement_block: str) -> str:
    """Replace a top-level YAML mapping block while preserving following sections."""
    lines = text.splitlines(keepends=True)
    out: List[str] = []
    i = 0
    replaced = False
    key_prefix = f"{key}:"

    while i < len(lines):
        line = lines[i]
        if not replaced and line.startswith(key_prefix):
            out.append(replacement_block)
            replaced = True
            i += 1
            while i < len(lines):
                curr = lines[i]
                if curr.startswith("  ") or curr.strip() == "":
                    i += 1
                    continue
                break
            continue

        out.append(line)
        i += 1

    if replaced:
        return "".join(out)

    suffix = "" if text.endswith("\n") else "\n"
    return text + suffix + replacement_block


def generate_scion_config(
    template_path: Path,
    output_path: Path,
    beacon_period_s: int,
    run_prefix: str,
    events_file_rel: str,
    stochastic_latency: Dict[str, object],
) -> None:
    text = template_path.read_text(encoding="utf-8")
    expiration_s = beacon_period_s * EXPIRATION_RATIO

    # Beacon timers.
    text = re.sub(r"(  period:\s*)\S+", rf"\g<1>{beacon_period_s}s", text, count=1)
    text = re.sub(r"(  expiration_period:\s*)\S+", rf"\g<1>{expiration_s}s", text, count=1)

    # Event file.
    text = re.sub(r"^(events_file:\s*)\S+", rf"\g<1>{events_file_rel}", text, flags=re.MULTILINE)

    # Output files.
    text = re.sub(r"^(output:\s*)build/\S+", rf"\g<1>build/{run_prefix}.txt", text, flags=re.MULTILINE)
    text = re.sub(
        r"^(cp_summary_output:\s*)build/\S+",
        rf"\g<1>build/{run_prefix}_cp_summary.csv",
        text,
        flags=re.MULTILINE,
    )
    text = re.sub(r"(log_file:\s*)build/\S+", rf"\g<1>build/{run_prefix}_control_plane.log", text)
    text = re.sub(
        r"(  output:\s*)build/\S+_path_snapshots\.csv",
        rf"\g<1>build/{run_prefix}_path_snapshots.csv",
        text,
    )
    text = re.sub(
        r"(      output:\s*)build/(?:[^\n]*/)?(scion_probe_\S+\.csv)",
        rf"\g<1>build/{run_prefix}/\g<2>",
        text,
    )

    # Some templates (notably single-mode) omit cp/probe outputs; inject deterministic paths.
    if not re.search(r"^cp_summary_output:\s*\S+", text, flags=re.MULTILINE):
        suffix = "" if text.endswith("\n") else "\n"
        text = text + f"{suffix}cp_summary_output: build/{run_prefix}_cp_summary.csv\n"

    pair_output_pattern = re.compile(
        r"(?m)(^(\s*)-\s*src_as:\s*(\d+)\s*\n"
        r"\2\s+dst_as:\s*(\d+)\s*\n"
        r"\2\s+src_host:\s*\d+\s*\n"
        r"\2\s+dst_host:\s*\d+\s*\n)(?!\2\s+output:)")

    def _add_pair_output(match: re.Match[str]) -> str:
        block = match.group(1)
        indent = match.group(2)
        src_as = match.group(3)
        dst_as = match.group(4)
        output_line = f"{indent}  output: build/{run_prefix}/scion_probe_{src_as}_{dst_as}.csv\n"
        return block + output_line

    text = pair_output_pattern.sub(_add_pair_output, text)

    # Stochastic latency block.
    stochastic_block = (
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
        f"  N_p: {stochastic_latency['N_p']}\n"
    )
    text = replace_top_level_block(text, "stochastic_latency_model", stochastic_block)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(text, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run multi-event sweeps across dual/single/direct IXP scenario families."
    )
    parser.add_argument(
        "--event-dir",
        default="user_defined_events/filtered",
        help="Directory with source filtered event JSON files (default: user_defined_events/filtered)",
    )
    parser.add_argument(
        "--event-glob",
        default="*.json",
        help="Glob for source event files inside --event-dir (default: *.json)",
    )
    parser.add_argument(
        "--max-events",
        type=int,
        default=0,
        help="Limit number of source event files (0 = all)",
    )
    parser.add_argument(
        "--scenarios",
        nargs="+",
        choices=SCENARIOS,
        default=SCENARIOS,
        help="Scenarios to run (default: hidden visible)",
    )
    parser.add_argument(
        "--families",
        nargs="+",
        choices=["dual", "single", "direct"],
        default=["single", "direct"],
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
        "--bgp-modes",
        nargs="+",
        choices=["baseline", "split"],
        default=["baseline", "split"],
        help="BGP modes for IXP families (default: baseline split). Split is dual-only.",
    )
    parser.add_argument(
        "--sweep-points",
        nargs="+",
        type=int,
        choices=[1, 2, 3, 4],
        default=[1, 2, 3, 4],
        help="Which timer sweep points to run (default: 1 2 3 4)",
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
        help="Abort on first failed run (default: continue and summarize failures)",
    )
    parser.add_argument("--stochastic-latency-model", dest="stochastic_latency_model", action="store_true", default=True, help="Enable stochastic IXP latency model")
    parser.add_argument("--no-stochastic-latency-model", dest="stochastic_latency_model", action="store_false", help="Disable stochastic IXP latency model")
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
    reject_mismatched_families(args.families)

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
    event_dir = repo_root / args.event_dir
    source_files = sorted(event_dir.glob(args.event_glob))
    if args.max_events > 0:
        source_files = source_files[: args.max_events]
    if not source_files:
        print(f"No source event files found in {event_dir} matching {args.event_glob}")
        return 1

    # Validate templates needed by requested scenario/family combinations.
    missing_templates: List[Path] = []
    for scenario in args.scenarios:
        for family in args.families:
            p = repo_root / SCION_TEMPLATE_PATHS[scenario][family]
            if not p.exists():
                missing_templates.append(p)
    if missing_templates:
        print("Missing SCION templates:")
        for p in missing_templates:
            print(f"  - {p}")
        return 1

    if not args.skip_build:
        print("Building...")
        build_rc = run_cmd(["./waf", "build"], dry_run=args.dry_run)
        if build_rc != 0:
            print(f"Build failed with exit code {build_rc}")
            return build_rc
        print()

    selected_points = [SWEEP_POINTS[i - 1] for i in args.sweep_points]
    generated_dir = repo_root / "build" / "generated_events" / "filtered_sweep"

    # Pre-generate family-specific event files from each source trace.
    generated_events: Dict[str, Dict[str, Path]] = {}
    for src in source_files:
        src_events = parse_source_events(src)
        if not src_events:
            print(f"Skipping {src.name}: no valid gateway link_up/link_down events")
            continue

        stem = sanitize_stem(src.stem)
        generated_events[stem] = {}

        dual_path = generated_dir / f"{stem}_dual.json"
        single_path = generated_dir / f"{stem}_single.json"
        direct_path_bgp = generated_dir / f"{stem}_direct_bgp.json"
        direct_path_scion = generated_dir / f"{stem}_direct_scion.json"

        write_json(dual_path, build_dual_ixp_events(src, src_events))
        write_json(single_path, build_single_ixp_events(src, src_events))
        write_json(direct_path_bgp, build_direct_events_bgp(src, src_events))
        write_json(direct_path_scion, build_direct_events_scion(src, src_events))

        generated_events[stem]["dual"] = dual_path
        generated_events[stem]["single"] = single_path
        generated_events[stem]["direct_bgp"] = direct_path_bgp
        generated_events[stem]["direct_scion"] = direct_path_scion

    if not generated_events:
        print("No usable event files after parsing; nothing to run.")
        return 1

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

    total_runs = 0

    for pt in selected_points:
        print(f"\n=== Sweep point {pt.label} ===")
        for event_key, family_paths in sorted(generated_events.items()):
            print(f"\n  Event trace: {event_key}")
            for scenario in args.scenarios:
                print(f"    Scenario: {scenario}")
                for family in args.families:
                    event_path_bgp = family_paths["direct_bgp"] if family == "direct" else family_paths[family]
                    event_path_scion = family_paths["direct_scion"] if family == "direct" else family_paths[family]
                    event_rel_bgp = str(event_path_bgp.relative_to(repo_root))
                    event_rel_scion = str(event_path_scion.relative_to(repo_root))

                    # BGP section
                    if "bgp" in args.protocols:
                        if family == "direct":
                            if scenario != "visible":
                                print("      BGP direct: skipped (hidden not supported)")
                            else:
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
                                    f" --eventFile={event_rel_bgp}"
                                    f" --outDir=build/sweep_bgp_direct_{scenario}_{pt.label}_{event_key}"
                                )
                                run_or_record(
                                    f"bgp/direct/{scenario}/{pt.label}/{event_key}",
                                    [str(repo_root / "build/scratch/bgp-ixp-scenarios")] + shlex.split(run_args),
                                )
                                total_runs += 1
                    else:
                        for bgp_mode in args.bgp_modes:
                                if family == "single" and bgp_mode == "split":
                                    print("      BGP split: skipped for single IXP")
                                    continue
                                split_flag = "1" if bgp_mode == "split" else "0"
                                dual_flag = "1" if family == "dual" else "0"
                                run_args = (
                                    f"bgp-ixp-scenarios"
                                    f" --dualIxp={dual_flag}"
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
                                    f" --eventFile={event_rel_bgp}"
                                    f" --outDir=build/sweep_bgp_{family}_{bgp_mode}_{scenario}_{pt.label}_{event_key}"
                                )
                                run_or_record(
                                    f"bgp/{family}/{bgp_mode}/{scenario}/{pt.label}/{event_key}",
                                    [str(repo_root / "build/scratch/bgp-ixp-scenarios")] + shlex.split(run_args),
                                )
                                total_runs += 1

                    # SCION section
                    if "scion" in args.protocols:
                        template = repo_root / SCION_TEMPLATE_PATHS[scenario][family]
                        run_prefix = f"sweep_scion_{family}_{scenario}_{pt.label}_{event_key}"
                        out_cfg = repo_root / "configs" / f"{run_prefix}.yaml"

                        if not args.dry_run:
                            generate_scion_config(
                                template_path=template,
                                output_path=out_cfg,
                                beacon_period_s=pt.beacon_period_s,
                                run_prefix=run_prefix,
                                events_file_rel=event_rel_scion,
                                stochastic_latency=stochastic_latency,
                            )
                            (repo_root / "build" / run_prefix).mkdir(parents=True, exist_ok=True)

                        run_or_record(
                            f"scion/{family}/{scenario}/{pt.label}/{event_key}",
                            [str(repo_root / "build/src/SCION/ns3.30.1-scion-debug"), str(out_cfg.relative_to(repo_root))],
                        )
                        total_runs += 1

    print("\nSweep complete")
    print(f"Source traces used: {len(generated_events)}")
    print(f"Sweep points: {len(selected_points)}")
    print(f"Commands attempted: {command_attempts}")
    print(f"Simulation runs launched: {total_runs}")
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
