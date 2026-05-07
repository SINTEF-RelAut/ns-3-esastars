#!/usr/bin/env python3
"""Generate direct-link switch events from gateway up/down traces.

Each source gateway event is mapped 1-to-1 to a single primary↔backup switch
on one specific direct peer link between AS102-105.  There is NO fan-out: one
source event → one link_down + one link_up.  Different gateways drive different
independent link pairs, so events across gateways are uncorrelated.

Default gateway-to-link assignment:
  gateway 1  →  AS102-AS103 loc-A   (if_id_a primary=1020100, backup=1020101)
  gateway 2  →  AS103-AS105 loc-A   (if_id_a primary=1030120, backup=1030121)
  gateway 3  →  AS104-AS105 loc-B   (if_id_a primary=1040220, backup=1040221)
  gateway 4  →  AS102-AS104 loc-B   (if_id_a primary=1020210, backup=1020211)

if_id encoding used in bgp-ixp-scenarios.cc --directLinks:
  - loc A: hundreds digit of last 3 digits = 1  (e.g. 1020100)
  - loc B: hundreds digit of last 3 digits = 2  (e.g. 1020200)
  - primary: units digit = 0
  - backup:  units digit = 1
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


# Each value: (primary_if_id, backup_if_id, human description)
DEFAULT_GATEWAY_LINK_MAP: Dict[int, Tuple[int, int, str]] = {
    1: (1020100, 1020101, "AS102-AS103 loc-A"),
    2: (1030120, 1030121, "AS103-AS105 loc-A"),
    3: (1040220, 1040221, "AS104-AS105 loc-B"),
    4: (1020210, 1020211, "AS102-AS104 loc-B"),
}


@dataclass
class SourceEvent:
    time_s: float
    event_type: str   # "link_up" or "link_down"
    gateway_id: int
    source_ifid: int


def parse_time_s(raw: str) -> float:
    v = raw.strip()
    if v.endswith("s"):
        v = v[:-1]
    return float(v)


def load_source_events(path: Path) -> List[SourceEvent]:
    """Load source gateway events from the 30_links JSON format."""
    doc = json.loads(path.read_text())
    out: List[SourceEvent] = []
    for item in doc.get("events", []):
        etype = str(item.get("type", "")).strip().lower()
        if etype not in {"link_up", "link_down"}:
            continue
        args = item.get("args", [])
        if not isinstance(args, list) or len(args) < 3:
            continue
        out.append(
            SourceEvent(
                time_s=parse_time_s(str(item.get("time", "0s"))),
                event_type=etype,
                gateway_id=int(str(args[1])),
                source_ifid=int(str(args[2])),
            )
        )
    return out


def build_direct_link_events(
    source_events: List[SourceEvent],
    gateway_link_map: Dict[int, Tuple[int, int, str]],
    switch_delta_s: float,
) -> Tuple[List[dict], Dict[int, int]]:
    """Generate one primary↔backup switch per source event, per gateway.

    Returns:
        (generated_events, final_active_if_per_gateway)
    """
    # Initial active state: primary is up for every managed pair.
    active_if: Dict[int, int] = {gw: v[0] for gw, v in gateway_link_map.items()}

    generated: List[dict] = []
    # Deduplicate: only one toggle per (time_s, gateway_id).
    seen: set = set()

    for src in sorted(source_events, key=lambda e: (e.time_s, e.gateway_id)):
        dedup_key = (src.time_s, src.gateway_id)
        if dedup_key in seen:
            continue
        seen.add(dedup_key)

        entry = gateway_link_map.get(src.gateway_id)
        if entry is None:
            continue

        primary_if, backup_if, link_desc = entry
        current = active_if[src.gateway_id]
        standby = backup_if if current == primary_if else primary_if

        generated.append({
            "time": f"{src.time_s:.3f}s",
            "type": "link_down",
            "args": ["1", str(src.gateway_id), str(current)],
            "description": (
                f"DirectLink: gateway={src.gateway_id} {src.event_type} → "
                f"{link_desc}: down {current} (was active)"
            ),
        })
        generated.append({
            "time": f"{src.time_s + switch_delta_s:.3f}s",
            "type": "link_up",
            "args": ["1", str(src.gateway_id), str(standby)],
            "description": (
                f"DirectLink: gateway={src.gateway_id} {src.event_type} → "
                f"{link_desc}: up {standby} (new active)"
            ),
        })
        active_if[src.gateway_id] = standby

    return generated, active_if


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate direct-link switch events from gateway up/down traces (no fan-out)"
    )
    parser.add_argument(
        "--input-events",
        default="user_defined_events/30_links.json",
        help="Source gateway event JSON (default: user_defined_events/30_links.json)",
    )
    parser.add_argument(
        "--output",
        default="user_defined_events/scenario_direct_link_gateway_switch_events.json",
        help="Output JSON path for generated direct-link switch events",
    )
    parser.add_argument(
        "--switch-delta",
        type=float,
        default=0.001,
        help="Delay in seconds between link_down and link_up for each switch (default: 0.001)",
    )
    args = parser.parse_args()

    input_path = Path(args.input_events)
    output_path = Path(args.output)

    source_events = load_source_events(input_path)
    generated, final_active = build_direct_link_events(
        source_events, DEFAULT_GATEWAY_LINK_MAP, args.switch_delta
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    doc = {
        "description": (
            "Direct-link switch events (no fan-out): each source gateway event toggles "
            "exactly one primary/backup direct ISL between AS102-105."
        ),
        "source_file": input_path.as_posix(),
        "gateway_link_map": {
            str(gw): {"primary": v[0], "backup": v[1], "description": v[2]}
            for gw, v in DEFAULT_GATEWAY_LINK_MAP.items()
        },
        "switch_delta_s": args.switch_delta,
        "event_count_source": len(source_events),
        "event_count_generated": len(generated),
        "final_active_if_per_gateway": {str(k): v for k, v in final_active.items()},
        "events": generated,
    }
    output_path.write_text(json.dumps(doc, indent=2) + "\n")

    print(f"Source events:    {len(source_events)}")
    print(f"Generated events: {len(generated)}")
    print(f"Output:           {output_path}")
    print("Final active interface per gateway:")
    for gw, ifid in sorted(final_active.items()):
        desc = DEFAULT_GATEWAY_LINK_MAP[gw][2]
        print(f"  gateway {gw} ({desc}): {ifid}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
