#!/usr/bin/env python3
"""Compute path switchover and recovery metrics from SCION path snapshot CSV.

Input CSV format (from LogPathSnapshotCsv):
time_s,src_as,dst_as,valid_paths,best_latency,best_bw,best_path,sampled_paths
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass
class Snapshot:
    t: float
    src: int
    dst: int
    valid_paths: int
    best_path: str


@dataclass
class Metrics:
    src_as: int
    dst_as: int
    baseline_time: Optional[float]
    baseline_valid_paths: Optional[int]
    baseline_best_path: str
    switchover_time: Optional[float]
    switchover_delay_s: Optional[float]
    recovery_time: Optional[float]
    recovery_delay_from_up_s: Optional[float]
    outage_duration_s: Optional[float]
    status: str
    best_path_only_status: str
    best_path_only_switchover_delay_s: Optional[float]
    best_path_only_recovery_delay_from_up_s: Optional[float]
    best_path_only_outage_duration_s: Optional[float]


@dataclass
class TimedEvent:
    time_s: float
    event_type: str
    args: Tuple[str, ...]
    interface_id: Optional[str]


@dataclass
class AggregateSummary:
    pair_class: str
    pair_count: int
    switched_and_recovered: int
    switched_and_recovered_best_path_only: int
    switched_not_recovered: int
    recovered_without_detected_switch: int
    no_baseline_before_failure: int
    no_detected_change: int
    recovery_delay_min_s: Optional[float]
    recovery_delay_median_s: Optional[float]
    recovery_delay_mean_s: Optional[float]
    recovery_delay_max_s: Optional[float]
    switchover_delay_min_s: Optional[float]
    switchover_delay_median_s: Optional[float]
    switchover_delay_mean_s: Optional[float]
    switchover_delay_max_s: Optional[float]
    outage_duration_min_s: Optional[float]
    outage_duration_median_s: Optional[float]
    outage_duration_mean_s: Optional[float]
    outage_duration_max_s: Optional[float]
    best_path_only_recovery_delay_min_s: Optional[float]
    best_path_only_recovery_delay_median_s: Optional[float]
    best_path_only_recovery_delay_mean_s: Optional[float]
    best_path_only_recovery_delay_max_s: Optional[float]
    best_path_only_outage_duration_min_s: Optional[float]
    best_path_only_outage_duration_median_s: Optional[float]
    best_path_only_outage_duration_mean_s: Optional[float]
    best_path_only_outage_duration_max_s: Optional[float]


@dataclass
class EventWindow:
    event_index: int
    event_label: str
    event_interface_id: str
    failure_time_s: float
    recovery_time_s: float
    analysis_end_time_s: Optional[float]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Compute path recovery metrics from path snapshot CSV")
    p.add_argument("--csv", required=True, help="Path snapshot CSV file")
    p.add_argument("--failure-time", type=float, default=None, help="Link-down event time in seconds")
    p.add_argument("--recovery-time", type=float, default=None, help="Link-up event time in seconds")
    p.add_argument(
        "--events-json",
        default="",
        help="Optional events JSON file used to auto-detect failure/recovery times",
    )
    p.add_argument(
        "--failure-event",
        default="link_down",
        help="Event type name to use as failure marker when --events-json is set",
    )
    p.add_argument(
        "--recovery-event",
        default="link_up",
        help="Event type name to use as recovery marker when --events-json is set",
    )
    p.add_argument(
        "--event-interface-id",
        default="",
        help="Optional interface ID to select matching failure/recovery events from JSON",
    )
    p.add_argument(
        "--per-event-windows",
        action="store_true",
        help="When --events-json is set, compute metrics separately for each detected failure/recovery window",
    )
    p.add_argument(
        "--pairs",
        default="",
        help="Optional comma-separated src:dst filter, e.g. '101:104,102:106'",
    )
    p.add_argument(
        "--aggregate-by-class",
        action="store_true",
        help="Also print aggregate recovery stats by pair class",
    )
    p.add_argument(
        "--core-ases",
        default="",
        help="Comma-separated AS numbers treated as core for pair classing, e.g. '101,102,103'",
    )
    p.add_argument(
        "--customer-ases",
        default="",
        help="Comma-separated AS numbers treated as customer for pair classing, e.g. '107,108'",
    )
    p.add_argument(
        "--aggregate-output",
        default="",
        help="Optional output CSV for aggregate class summaries",
    )
    p.add_argument(
        "--topology",
        default="",
        help="Optional topology XML for automatic AS class inference when core/customer lists are not provided",
    )
    p.add_argument("--output", default="", help="Optional output CSV for computed metrics")
    return p.parse_args()


def parse_as_set(as_arg: str) -> set[int]:
    values: set[int] = set()
    for item in as_arg.split(","):
        item = item.strip()
        if item:
            values.add(int(item))
    return values


def infer_as_classes_from_topology(topology_path: str) -> Tuple[set[int], set[int]]:
    core_ases: set[int] = set()
    customer_ases: set[int] = set()

    root = ET.parse(topology_path).getroot()
    ases = root.find("ases")
    if ases is None:
        return core_ases, customer_ases

    for as_node in ases.findall("as"):
        as_id_raw = as_node.get("id")
        if not as_id_raw:
            continue

        try:
            as_id = int(as_id_raw)
        except ValueError:
            continue

        as_type = ""
        for prop in as_node.findall("property"):
            if (prop.get("name") or "").strip().lower() != "type":
                continue
            as_type = (prop.get("value") or (prop.text or "")).strip().lower()
            break

        if as_type == "core":
            core_ases.add(as_id)
        else:
            # Treat any non-core or unknown type as customer/non-core class.
            customer_ases.add(as_id)

    return core_ases, customer_ases


def resolve_as_classes(args: argparse.Namespace) -> Tuple[set[int], set[int]]:
    core_ases = parse_as_set(args.core_ases)
    customer_ases = parse_as_set(args.customer_ases)

    if (not core_ases or not customer_ases) and args.topology:
        inferred_core, inferred_customer = infer_as_classes_from_topology(args.topology)
        if not core_ases:
            core_ases = inferred_core
        if not customer_ases:
            customer_ases = inferred_customer

    return core_ases, customer_ases


def classify_pair(src_as: int, dst_as: int, core_ases: set[int], customer_ases: set[int]) -> str:
    src_is_core = src_as in core_ases
    dst_is_core = dst_as in core_ases
    src_is_customer = src_as in customer_ases
    dst_is_customer = dst_as in customer_ases

    if src_is_core and dst_is_core:
        return "core-core"
    if src_is_customer and dst_is_customer:
        return "customer-customer"
    if (src_is_customer and dst_is_core) or (src_is_core and dst_is_customer):
        return "customer-core"
    return "other"


def summarize_optional(values: List[Optional[float]]) -> Tuple[Optional[float], Optional[float], Optional[float], Optional[float]]:
    finite = [v for v in values if v is not None]
    if not finite:
        return (None, None, None, None)
    return (
        min(finite),
        statistics.median(finite),
        statistics.mean(finite),
        max(finite),
    )


def build_aggregate_summaries(
    rows: List[Metrics], core_ases: set[int], customer_ases: set[int]
) -> List[AggregateSummary]:
    grouped: Dict[str, List[Metrics]] = {}
    for row in rows:
        pair_class = classify_pair(row.src_as, row.dst_as, core_ases, customer_ases)
        grouped.setdefault(pair_class, []).append(row)

    summaries: List[AggregateSummary] = []
    for pair_class in sorted(grouped.keys()):
        members = grouped[pair_class]
        recovery_stats = summarize_optional([m.recovery_delay_from_up_s for m in members])
        switchover_stats = summarize_optional([m.switchover_delay_s for m in members])
        outage_stats = summarize_optional([m.outage_duration_s for m in members])
        best_path_only_recovery_stats = summarize_optional(
            [
                m.best_path_only_recovery_delay_from_up_s
                if m.best_path_only_status == "switched_and_recovered"
                else None
                for m in members
            ]
        )
        best_path_only_outage_stats = summarize_optional(
            [
                m.best_path_only_outage_duration_s
                if m.best_path_only_status == "switched_and_recovered"
                else None
                for m in members
            ]
        )

        summaries.append(
            AggregateSummary(
                pair_class=pair_class,
                pair_count=len(members),
                switched_and_recovered=sum(1 for m in members if m.status == "switched_and_recovered"),
                switched_and_recovered_best_path_only=sum(
                    1 for m in members if m.best_path_only_status == "switched_and_recovered"
                ),
                switched_not_recovered=sum(1 for m in members if m.status == "switched_not_recovered"),
                recovered_without_detected_switch=sum(
                    1 for m in members if m.status == "recovered_without_detected_switch"
                ),
                no_baseline_before_failure=sum(
                    1 for m in members if m.status == "no_baseline_before_failure"
                ),
                no_detected_change=sum(1 for m in members if m.status == "no_detected_change"),
                recovery_delay_min_s=recovery_stats[0],
                recovery_delay_median_s=recovery_stats[1],
                recovery_delay_mean_s=recovery_stats[2],
                recovery_delay_max_s=recovery_stats[3],
                switchover_delay_min_s=switchover_stats[0],
                switchover_delay_median_s=switchover_stats[1],
                switchover_delay_mean_s=switchover_stats[2],
                switchover_delay_max_s=switchover_stats[3],
                outage_duration_min_s=outage_stats[0],
                outage_duration_median_s=outage_stats[1],
                outage_duration_mean_s=outage_stats[2],
                outage_duration_max_s=outage_stats[3],
                best_path_only_recovery_delay_min_s=best_path_only_recovery_stats[0],
                best_path_only_recovery_delay_median_s=best_path_only_recovery_stats[1],
                best_path_only_recovery_delay_mean_s=best_path_only_recovery_stats[2],
                best_path_only_recovery_delay_max_s=best_path_only_recovery_stats[3],
                best_path_only_outage_duration_min_s=best_path_only_outage_stats[0],
                best_path_only_outage_duration_median_s=best_path_only_outage_stats[1],
                best_path_only_outage_duration_mean_s=best_path_only_outage_stats[2],
                best_path_only_outage_duration_max_s=best_path_only_outage_stats[3],
            )
        )

    return summaries


def write_aggregate_csv(path: str, rows: List[AggregateSummary]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "pair_class",
                "pair_count",
                "switched_and_recovered",
                "switched_and_recovered_best_path_only",
                "switched_not_recovered",
                "recovered_without_detected_switch",
                "no_baseline_before_failure",
                "no_detected_change",
                "recovery_delay_min_s",
                "recovery_delay_median_s",
                "recovery_delay_mean_s",
                "recovery_delay_max_s",
                "switchover_delay_min_s",
                "switchover_delay_median_s",
                "switchover_delay_mean_s",
                "switchover_delay_max_s",
                "outage_duration_min_s",
                "outage_duration_median_s",
                "outage_duration_mean_s",
                "outage_duration_max_s",
                "best_path_only_recovery_delay_min_s",
                "best_path_only_recovery_delay_median_s",
                "best_path_only_recovery_delay_mean_s",
                "best_path_only_recovery_delay_max_s",
                "best_path_only_outage_duration_min_s",
                "best_path_only_outage_duration_median_s",
                "best_path_only_outage_duration_mean_s",
                "best_path_only_outage_duration_max_s",
            ]
        )
        for m in rows:
            w.writerow(
                [
                    m.pair_class,
                    m.pair_count,
                    m.switched_and_recovered,
                    m.switched_and_recovered_best_path_only,
                    m.switched_not_recovered,
                    m.recovered_without_detected_switch,
                    m.no_baseline_before_failure,
                    m.no_detected_change,
                    m.recovery_delay_min_s,
                    m.recovery_delay_median_s,
                    m.recovery_delay_mean_s,
                    m.recovery_delay_max_s,
                    m.switchover_delay_min_s,
                    m.switchover_delay_median_s,
                    m.switchover_delay_mean_s,
                    m.switchover_delay_max_s,
                    m.outage_duration_min_s,
                    m.outage_duration_median_s,
                    m.outage_duration_mean_s,
                    m.outage_duration_max_s,
                    m.best_path_only_recovery_delay_min_s,
                    m.best_path_only_recovery_delay_median_s,
                    m.best_path_only_recovery_delay_mean_s,
                    m.best_path_only_recovery_delay_max_s,
                    m.best_path_only_outage_duration_min_s,
                    m.best_path_only_outage_duration_median_s,
                    m.best_path_only_outage_duration_mean_s,
                    m.best_path_only_outage_duration_max_s,
                ]
            )


def write_aggregate_csv_with_events(
    path: str, rows: List[Tuple[EventWindow, AggregateSummary]], include_event_columns: bool
) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        header: List[str] = []
        if include_event_columns:
            header.extend(
                [
                    "event_index",
                    "event_label",
                    "event_interface_id",
                    "failure_time_s",
                    "recovery_time_s",
                ]
            )
        header.extend(
            [
                "pair_class",
                "pair_count",
                "switched_and_recovered",
                "switched_and_recovered_best_path_only",
                "switched_not_recovered",
                "recovered_without_detected_switch",
                "no_baseline_before_failure",
                "no_detected_change",
                "recovery_delay_min_s",
                "recovery_delay_median_s",
                "recovery_delay_mean_s",
                "recovery_delay_max_s",
                "switchover_delay_min_s",
                "switchover_delay_median_s",
                "switchover_delay_mean_s",
                "switchover_delay_max_s",
                "outage_duration_min_s",
                "outage_duration_median_s",
                "outage_duration_mean_s",
                "outage_duration_max_s",
                "best_path_only_recovery_delay_min_s",
                "best_path_only_recovery_delay_median_s",
                "best_path_only_recovery_delay_mean_s",
                "best_path_only_recovery_delay_max_s",
                "best_path_only_outage_duration_min_s",
                "best_path_only_outage_duration_median_s",
                "best_path_only_outage_duration_mean_s",
                "best_path_only_outage_duration_max_s",
            ]
        )
        w.writerow(header)

        for event_window, m in rows:
            out: List[object] = []
            if include_event_columns:
                out.extend(
                    [
                        event_window.event_index,
                        event_window.event_label,
                        event_window.event_interface_id,
                        event_window.failure_time_s,
                        event_window.recovery_time_s,
                    ]
                )
            out.extend(
                [
                    m.pair_class,
                    m.pair_count,
                    m.switched_and_recovered,
                    m.switched_and_recovered_best_path_only,
                    m.switched_not_recovered,
                    m.recovered_without_detected_switch,
                    m.no_baseline_before_failure,
                    m.no_detected_change,
                    m.recovery_delay_min_s,
                    m.recovery_delay_median_s,
                    m.recovery_delay_mean_s,
                    m.recovery_delay_max_s,
                    m.switchover_delay_min_s,
                    m.switchover_delay_median_s,
                    m.switchover_delay_mean_s,
                    m.switchover_delay_max_s,
                    m.outage_duration_min_s,
                    m.outage_duration_median_s,
                    m.outage_duration_mean_s,
                    m.outage_duration_max_s,
                    m.best_path_only_recovery_delay_min_s,
                    m.best_path_only_recovery_delay_median_s,
                    m.best_path_only_recovery_delay_mean_s,
                    m.best_path_only_recovery_delay_max_s,
                    m.best_path_only_outage_duration_min_s,
                    m.best_path_only_outage_duration_median_s,
                    m.best_path_only_outage_duration_mean_s,
                    m.best_path_only_outage_duration_max_s,
                ]
            )
            w.writerow(out)


def parse_duration_seconds(value: object) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        raise ValueError(f"unsupported time value type: {type(value).__name__}")

    m = re.match(r"^\s*([0-9]*\.?[0-9]+)\s*([a-zA-Z]+)?\s*$", value)
    if m is None:
        raise ValueError(f"invalid time value: {value!r}")

    magnitude = float(m.group(1))
    unit = (m.group(2) or "s").lower()
    factor = {
        "ns": 1e-9,
        "us": 1e-6,
        "ms": 1e-3,
        "s": 1.0,
        "m": 60.0,
        "min": 60.0,
        "h": 3600.0,
    }.get(unit)
    if factor is None:
        raise ValueError(f"unsupported time unit: {unit!r}")
    return magnitude * factor


def extract_interface_id(event_args: object) -> Optional[str]:
    if not isinstance(event_args, list) or len(event_args) < 3:
        return None
    return str(event_args[2]).strip()


def load_timed_events(events_json_path: str, event_type: str) -> List[TimedEvent]:
    with open(events_json_path, "r", encoding="utf-8") as f:
        payload = json.load(f)

    events = payload.get("events", [])
    timed: List[TimedEvent] = []
    for event in events:
        if event.get("type") != event_type:
            continue
        event_args_raw = event.get("args", [])
        if isinstance(event_args_raw, list):
            event_args = tuple(str(x) for x in event_args_raw)
        else:
            event_args = tuple()

        timed.append(
            TimedEvent(
                time_s=parse_duration_seconds(event.get("time", "")),
                event_type=event_type,
                args=event_args,
                interface_id=extract_interface_id(event_args_raw),
            )
        )

    timed.sort(key=lambda e: e.time_s)
    return timed


def choose_interface_id(
    failure_events: List[TimedEvent], recovery_events: List[TimedEvent], requested_interface_id: str
) -> Optional[str]:
    if requested_interface_id.strip():
        return requested_interface_id.strip()

    failure_ifids = [e.interface_id for e in failure_events if e.interface_id]
    recovery_ifids = {e.interface_id for e in recovery_events if e.interface_id}

    # Prefer the interface ID of the first failure event that also appears in recovery events.
    for ifid in failure_ifids:
        if ifid in recovery_ifids:
            return ifid

    # Otherwise, fall back to the first failure interface if present.
    return failure_ifids[0] if failure_ifids else None


def resolve_event_windows(args: argparse.Namespace) -> List[EventWindow]:
    if args.events_json and args.per_event_windows:
        failure_events = load_timed_events(args.events_json, args.failure_event)
        recovery_events = load_timed_events(args.events_json, args.recovery_event)

        if args.event_interface_id.strip():
            selected_if = args.event_interface_id.strip()
            failure_events = [e for e in failure_events if e.interface_id == selected_if]
            recovery_events = [e for e in recovery_events if e.interface_id == selected_if]

        failure_times = sorted({e.time_s for e in failure_events})
        recovery_times = sorted({e.time_s for e in recovery_events})
        windows: List[EventWindow] = []

        for idx, failure_time in enumerate(failure_times, start=1):
            recovery_after_failure = [t for t in recovery_times if t > failure_time]
            if recovery_after_failure:
                recovery_time = recovery_after_failure[0]
            elif recovery_times:
                recovery_time = recovery_times[-1]
            else:
                recovery_time = failure_time + 100.0

            analysis_end_time: Optional[float] = None
            if idx < len(failure_times):
                analysis_end_time = failure_times[idx]

            windows.append(
                EventWindow(
                    event_index=idx,
                    event_label=f"event_{idx}",
                    event_interface_id=args.event_interface_id.strip(),
                    failure_time_s=float(failure_time),
                    recovery_time_s=float(recovery_time),
                    analysis_end_time_s=analysis_end_time,
                )
            )

        if windows:
            return windows

    failure_time, recovery_time = resolve_failure_recovery_times(args)
    return [
        EventWindow(
            event_index=1,
            event_label="event_1",
            event_interface_id=args.event_interface_id.strip(),
            failure_time_s=float(failure_time),
            recovery_time_s=float(recovery_time),
            analysis_end_time_s=None,
        )
    ]


def resolve_failure_recovery_times(args: argparse.Namespace) -> Tuple[float, float]:
    default_failure = 120.0
    default_recovery = 220.0

    failure_time = args.failure_time
    recovery_time = args.recovery_time

    if args.events_json:
        failure_events = load_timed_events(args.events_json, args.failure_event)
        recovery_events = load_timed_events(args.events_json, args.recovery_event)
        selected_ifid = choose_interface_id(failure_events, recovery_events, args.event_interface_id)

        if selected_ifid is not None:
            failure_candidates = [e.time_s for e in failure_events if e.interface_id == selected_ifid]
            recovery_candidates = [e.time_s for e in recovery_events if e.interface_id == selected_ifid]
        else:
            failure_candidates = [e.time_s for e in failure_events]
            recovery_candidates = [e.time_s for e in recovery_events]

        if failure_time is None:
            failure_time = failure_candidates[0] if failure_candidates else default_failure

        if recovery_time is None:
            recovery_after_failure = [t for t in recovery_candidates if t > failure_time]
            if recovery_after_failure:
                recovery_time = recovery_after_failure[0]
            elif recovery_candidates:
                recovery_time = recovery_candidates[-1]
            else:
                recovery_time = default_recovery
    else:
        if failure_time is None:
            failure_time = default_failure
        if recovery_time is None:
            recovery_time = default_recovery

    return float(failure_time), float(recovery_time)


def parse_pairs(pairs_arg: str) -> Optional[set[Tuple[int, int]]]:
    if not pairs_arg.strip():
        return None
    pairs: set[Tuple[int, int]] = set()
    for item in pairs_arg.split(","):
        item = item.strip()
        if not item:
            continue
        src_str, dst_str = item.split(":", 1)
        pairs.add((int(src_str), int(dst_str)))
    return pairs


def load_snapshots(csv_path: str) -> Dict[Tuple[int, int], List[Snapshot]]:
    per_pair: Dict[Tuple[int, int], List[Snapshot]] = {}
    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            t = float(row["time_s"])
            src = int(row["src_as"])
            dst = int(row["dst_as"])
            valid_paths = int(row["valid_paths"])
            best_path = row["best_path"]
            snap = Snapshot(t=t, src=src, dst=dst, valid_paths=valid_paths, best_path=best_path)
            per_pair.setdefault((src, dst), []).append(snap)

    for snaps in per_pair.values():
        snaps.sort(key=lambda s: s.t)
    return per_pair


def last_before(snaps: Iterable[Snapshot], t: float) -> Optional[Snapshot]:
    candidate = None
    for s in snaps:
        if s.t < t:
            candidate = s
        else:
            break
    return candidate


def first_at_or_after(snaps: Iterable[Snapshot], t: float) -> Optional[Snapshot]:
    for s in snaps:
        if s.t >= t:
            return s
    return None


def compute_metrics_for_pair(
    snaps: List[Snapshot],
    failure_time: float,
    recovery_time: float,
    analysis_end_time: Optional[float] = None,
) -> Metrics:
    src = snaps[0].src
    dst = snaps[0].dst

    baseline = last_before(snaps, failure_time)
    if baseline is None:
        return Metrics(
            src_as=src,
            dst_as=dst,
            baseline_time=None,
            baseline_valid_paths=None,
            baseline_best_path="",
            switchover_time=None,
            switchover_delay_s=None,
            recovery_time=None,
            recovery_delay_from_up_s=None,
            outage_duration_s=None,
            status="no_baseline_before_failure",
            best_path_only_status="no_baseline_before_failure",
            best_path_only_switchover_delay_s=None,
            best_path_only_recovery_delay_from_up_s=None,
            best_path_only_outage_duration_s=None,
        )

    baseline_path = baseline.best_path
    baseline_valid = baseline.valid_paths

    switchover: Optional[Snapshot] = None
    for s in snaps:
        if s.t < failure_time:
            continue
        if analysis_end_time is not None and s.t >= analysis_end_time:
            break
        if s.best_path != baseline_path or s.valid_paths < baseline_valid:
            switchover = s
            break

    recovery: Optional[Snapshot] = None
    for s in snaps:
        if s.t < recovery_time:
            continue
        if analysis_end_time is not None and s.t >= analysis_end_time:
            break
        if s.best_path == baseline_path and s.valid_paths >= baseline_valid:
            recovery = s
            break

    sw_time = switchover.t if switchover else None
    rec_time = recovery.t if recovery else None

    sw_delay = (sw_time - failure_time) if sw_time is not None else None
    rec_delay_from_up = (rec_time - recovery_time) if rec_time is not None else None
    outage_duration = (rec_time - (sw_time if sw_time is not None else failure_time)) if rec_time is not None else None

    if switchover is None and recovery is None:
        status = "no_detected_change"
    elif switchover is not None and recovery is None:
        status = "switched_not_recovered"
    elif switchover is None and recovery is not None:
        status = "recovered_without_detected_switch"
    else:
        status = "switched_and_recovered"

    # Best-path-only status ignores valid-path count changes and only tracks
    # whether the best path changed and then returned to baseline.
    switchover_best_path_only: Optional[Snapshot] = None
    for s in snaps:
        if s.t < failure_time:
            continue
        if analysis_end_time is not None and s.t >= analysis_end_time:
            break
        if s.best_path != baseline_path:
            switchover_best_path_only = s
            break

    recovery_best_path_only: Optional[Snapshot] = None
    for s in snaps:
        if s.t < recovery_time:
            continue
        if analysis_end_time is not None and s.t >= analysis_end_time:
            break
        if s.best_path == baseline_path:
            recovery_best_path_only = s
            break

    if switchover_best_path_only is None and recovery_best_path_only is None:
        best_path_only_status = "no_detected_change"
    elif switchover_best_path_only is not None and recovery_best_path_only is None:
        best_path_only_status = "switched_not_recovered"
    elif switchover_best_path_only is None and recovery_best_path_only is not None:
        best_path_only_status = "recovered_without_detected_switch"
    else:
        best_path_only_status = "switched_and_recovered"

    sw_best_time = switchover_best_path_only.t if switchover_best_path_only else None
    rec_best_time = recovery_best_path_only.t if recovery_best_path_only else None
    sw_best_delay = (sw_best_time - failure_time) if sw_best_time is not None else None
    rec_best_delay_from_up = (rec_best_time - recovery_time) if rec_best_time is not None else None
    best_path_only_outage_duration = (
        rec_best_time - (sw_best_time if sw_best_time is not None else failure_time)
        if rec_best_time is not None
        else None
    )

    return Metrics(
        src_as=src,
        dst_as=dst,
        baseline_time=baseline.t,
        baseline_valid_paths=baseline_valid,
        baseline_best_path=baseline_path,
        switchover_time=sw_time,
        switchover_delay_s=sw_delay,
        recovery_time=rec_time,
        recovery_delay_from_up_s=rec_delay_from_up,
        outage_duration_s=outage_duration,
        status=status,
        best_path_only_status=best_path_only_status,
        best_path_only_switchover_delay_s=sw_best_delay,
        best_path_only_recovery_delay_from_up_s=rec_best_delay_from_up,
        best_path_only_outage_duration_s=best_path_only_outage_duration,
    )


def write_output_csv(path: str, rows: List[Metrics]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "src_as",
                "dst_as",
                "baseline_time",
                "baseline_valid_paths",
                "baseline_best_path",
                "switchover_time",
                "switchover_delay_s",
                "recovery_time",
                "recovery_delay_from_up_s",
                "outage_duration_s",
                "status",
                "best_path_only_status",
                "best_path_only_switchover_delay_s",
                "best_path_only_recovery_delay_from_up_s",
                "best_path_only_outage_duration_s",
            ]
        )
        for m in rows:
            w.writerow(
                [
                    m.src_as,
                    m.dst_as,
                    m.baseline_time,
                    m.baseline_valid_paths,
                    m.baseline_best_path,
                    m.switchover_time,
                    m.switchover_delay_s,
                    m.recovery_time,
                    m.recovery_delay_from_up_s,
                    m.outage_duration_s,
                    m.status,
                    m.best_path_only_status,
                    m.best_path_only_switchover_delay_s,
                    m.best_path_only_recovery_delay_from_up_s,
                    m.best_path_only_outage_duration_s,
                ]
            )


def main() -> None:
    args = parse_args()
    pair_filter = parse_pairs(args.pairs)
    event_windows = resolve_event_windows(args)
    core_ases, customer_ases = resolve_as_classes(args)

    per_pair = load_snapshots(args.csv)
    keys = sorted(per_pair.keys())
    if pair_filter is not None:
        keys = [k for k in keys if k in pair_filter]

    results_per_event: List[Tuple[EventWindow, Metrics]] = []
    for event_window in event_windows:
        for key in keys:
            metrics = compute_metrics_for_pair(
                per_pair[key],
                event_window.failure_time_s,
                event_window.recovery_time_s,
                event_window.analysis_end_time_s,
            )
            results_per_event.append((event_window, metrics))

    include_event_columns = len(event_windows) > 1 or args.per_event_windows

    if args.output:
        with open(args.output, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            header: List[str] = []
            if include_event_columns:
                header.extend(
                    [
                        "event_index",
                        "event_label",
                        "event_interface_id",
                        "failure_time_s",
                        "recovery_time_s",
                    ]
                )
            header.extend(
                [
                    "src_as",
                    "dst_as",
                    "baseline_time",
                    "baseline_valid_paths",
                    "baseline_best_path",
                    "switchover_time",
                    "switchover_delay_s",
                    "recovery_time",
                    "recovery_delay_from_up_s",
                    "outage_duration_s",
                    "status",
                    "best_path_only_status",
                    "best_path_only_switchover_delay_s",
                    "best_path_only_recovery_delay_from_up_s",
                    "best_path_only_outage_duration_s",
                ]
            )
            w.writerow(header)

            for event_window, m in results_per_event:
                row: List[object] = []
                if include_event_columns:
                    row.extend(
                        [
                            event_window.event_index,
                            event_window.event_label,
                            event_window.event_interface_id,
                            event_window.failure_time_s,
                            event_window.recovery_time_s,
                        ]
                    )
                row.extend(
                    [
                        m.src_as,
                        m.dst_as,
                        m.baseline_time,
                        m.baseline_valid_paths,
                        m.baseline_best_path,
                        m.switchover_time,
                        m.switchover_delay_s,
                        m.recovery_time,
                        m.recovery_delay_from_up_s,
                        m.outage_duration_s,
                        m.status,
                        m.best_path_only_status,
                        m.best_path_only_switchover_delay_s,
                        m.best_path_only_recovery_delay_from_up_s,
                        m.best_path_only_outage_duration_s,
                    ]
                )
                w.writerow(row)

    if include_event_columns:
        print(
            "event_index,event_label,event_interface_id,failure_time_s,recovery_time_s,"
            "src_as,dst_as,status,switchover_time,recovery_time,switchover_delay_s,"
            "recovery_delay_from_up_s,outage_duration_s"
        )
    else:
        print("src_as,dst_as,status,switchover_time,recovery_time,switchover_delay_s,recovery_delay_from_up_s,outage_duration_s")

    for event_window, m in results_per_event:
        if include_event_columns:
            print(
                f"{event_window.event_index},{event_window.event_label},"
                f"{event_window.event_interface_id},{event_window.failure_time_s},"
                f"{event_window.recovery_time_s},{m.src_as},{m.dst_as},{m.status},"
                f"{m.switchover_time},{m.recovery_time},{m.switchover_delay_s},"
                f"{m.recovery_delay_from_up_s},{m.outage_duration_s}"
            )
        else:
            print(
                f"{m.src_as},{m.dst_as},{m.status},{m.switchover_time},{m.recovery_time},"
                f"{m.switchover_delay_s},{m.recovery_delay_from_up_s},{m.outage_duration_s}"
            )

    if args.aggregate_by_class:
        aggregate_rows_with_events: List[Tuple[EventWindow, AggregateSummary]] = []
        for event_window in event_windows:
            event_metrics = [
                m for ew, m in results_per_event if ew.event_index == event_window.event_index
            ]
            aggregate = build_aggregate_summaries(event_metrics, core_ases, customer_ases)
            for summary in aggregate:
                aggregate_rows_with_events.append((event_window, summary))

        if args.aggregate_output:
            write_aggregate_csv_with_events(
                args.aggregate_output, aggregate_rows_with_events, include_event_columns
            )

        print()
        if include_event_columns:
            print(
                "event_index,event_label,event_interface_id,failure_time_s,recovery_time_s,"
                "pair_class,pair_count,switched_and_recovered,switched_not_recovered,"
                "switched_and_recovered_best_path_only,"
                "recovered_without_detected_switch,no_baseline_before_failure,"
                "no_detected_change,recovery_delay_min_s,recovery_delay_median_s,"
                "recovery_delay_mean_s,recovery_delay_max_s,switchover_delay_min_s,"
                "switchover_delay_median_s,switchover_delay_mean_s,switchover_delay_max_s,"
                "outage_duration_min_s,outage_duration_median_s,outage_duration_mean_s,"
                "outage_duration_max_s,best_path_only_recovery_delay_min_s,"
                "best_path_only_recovery_delay_median_s,best_path_only_recovery_delay_mean_s,"
                "best_path_only_recovery_delay_max_s,best_path_only_outage_duration_min_s,"
                "best_path_only_outage_duration_median_s,best_path_only_outage_duration_mean_s,"
                "best_path_only_outage_duration_max_s"
            )
        else:
            print(
                "pair_class,pair_count,switched_and_recovered,switched_not_recovered,"
                "switched_and_recovered_best_path_only,"
                "recovered_without_detected_switch,no_baseline_before_failure,"
                "no_detected_change,recovery_delay_min_s,recovery_delay_median_s,"
                "recovery_delay_mean_s,recovery_delay_max_s,switchover_delay_min_s,"
                "switchover_delay_median_s,switchover_delay_mean_s,switchover_delay_max_s,"
                "outage_duration_min_s,outage_duration_median_s,outage_duration_mean_s,"
                "outage_duration_max_s,best_path_only_recovery_delay_min_s,"
                "best_path_only_recovery_delay_median_s,best_path_only_recovery_delay_mean_s,"
                "best_path_only_recovery_delay_max_s,best_path_only_outage_duration_min_s,"
                "best_path_only_outage_duration_median_s,best_path_only_outage_duration_mean_s,"
                "best_path_only_outage_duration_max_s"
            )

        for event_window, a in aggregate_rows_with_events:
            if include_event_columns:
                print(
                    f"{event_window.event_index},{event_window.event_label},"
                    f"{event_window.event_interface_id},{event_window.failure_time_s},"
                    f"{event_window.recovery_time_s},{a.pair_class},{a.pair_count},"
                    f"{a.switched_and_recovered},{a.switched_not_recovered},"
                    f"{a.switched_and_recovered_best_path_only},"
                    f"{a.recovered_without_detected_switch},{a.no_baseline_before_failure},"
                    f"{a.no_detected_change},{a.recovery_delay_min_s},"
                    f"{a.recovery_delay_median_s},{a.recovery_delay_mean_s},"
                    f"{a.recovery_delay_max_s},{a.switchover_delay_min_s},"
                    f"{a.switchover_delay_median_s},{a.switchover_delay_mean_s},"
                    f"{a.switchover_delay_max_s},{a.outage_duration_min_s},"
                    f"{a.outage_duration_median_s},{a.outage_duration_mean_s},"
                    f"{a.outage_duration_max_s},{a.best_path_only_recovery_delay_min_s},"
                    f"{a.best_path_only_recovery_delay_median_s},"
                    f"{a.best_path_only_recovery_delay_mean_s},"
                    f"{a.best_path_only_recovery_delay_max_s},"
                    f"{a.best_path_only_outage_duration_min_s},"
                    f"{a.best_path_only_outage_duration_median_s},"
                    f"{a.best_path_only_outage_duration_mean_s},"
                    f"{a.best_path_only_outage_duration_max_s}"
                )
            else:
                print(
                    f"{a.pair_class},{a.pair_count},{a.switched_and_recovered},"
                    f"{a.switched_not_recovered},{a.switched_and_recovered_best_path_only},"
                    f"{a.recovered_without_detected_switch},"
                    f"{a.no_baseline_before_failure},{a.no_detected_change},"
                    f"{a.recovery_delay_min_s},{a.recovery_delay_median_s},"
                    f"{a.recovery_delay_mean_s},{a.recovery_delay_max_s},"
                    f"{a.switchover_delay_min_s},{a.switchover_delay_median_s},"
                    f"{a.switchover_delay_mean_s},{a.switchover_delay_max_s},"
                    f"{a.outage_duration_min_s},{a.outage_duration_median_s},"
                    f"{a.outage_duration_mean_s},{a.outage_duration_max_s},"
                    f"{a.best_path_only_recovery_delay_min_s},"
                    f"{a.best_path_only_recovery_delay_median_s},"
                    f"{a.best_path_only_recovery_delay_mean_s},"
                    f"{a.best_path_only_recovery_delay_max_s},"
                    f"{a.best_path_only_outage_duration_min_s},"
                    f"{a.best_path_only_outage_duration_median_s},"
                    f"{a.best_path_only_outage_duration_mean_s},"
                    f"{a.best_path_only_outage_duration_max_s}"
                )


if __name__ == "__main__":
    main()
