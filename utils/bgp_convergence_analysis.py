#!/usr/bin/env python3
"""Analyse BGP convergence from bgp-convergence-first simulation outputs.

Reads:
  <dir>/probe_<src>_<dst>.csv    -- UDP probe RTT log (sent/reply/timeout rows)
  <dir>/link_events.csv          -- link up/down schedule
  <dir>/bgp_cp_as<N>.csv        -- per-AS BGP control-plane messages (optional)

Computes per probe-pair per link-failure-event:
  * switchover delay  – time from link_down to first probe timeout
  * recovery delay    – time from link_up  to first probe reply after outage
  * outage duration   – time from first timeout to first post-outage reply
  * BGP detection delay – time from link_down to BGP session ESTABLISHED->IDLE

Outputs a summary table to stdout and optionally writes CSVs.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import statistics
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class LinkEvent:
    time_s: float
    event: str          # 'link_down' | 'link_up'
    as_a: int
    as_b: int


@dataclass
class ProbeEvent:
    time_s: float
    seq: int
    kind: str           # 'sent' | 'reply' | 'timeout'
    rtt_ms: Optional[float]


@dataclass
class CpEvent:
    time_s: float
    local_asn: int
    peer_asn: int
    event: str          # 'STATE_CHANGE' | 'OPEN' | 'UPDATE' | 'KEEPALIVE' | 'NOTIFICATION'
    detail: str         # e.g. 'ESTABLISHED->IDLE', 'announce', 'withdraw'


@dataclass
class EventMetrics:
    event_idx: int
    link_as_a: int
    link_as_b: int
    link_down_time: float
    link_up_time: Optional[float]
    first_timeout_time: Optional[float]     # switchover
    last_timeout_time: Optional[float]
    first_recovery_time: Optional[float]    # data-plane recovery
    switchover_delay_s: Optional[float]
    recovery_delay_s: Optional[float]       # relative to link_up
    outage_duration_s: Optional[float]
    bgp_detection_time: Optional[float]     # when BGP session ESTABLISHED->IDLE
    bgp_detection_delay_s: Optional[float]  # relative to link_down
    bgp_first_update_time: Optional[float]  # first UPDATE after failure detection
    status: str


# ---------------------------------------------------------------------------
# Pair-class helpers (same taxonomy as recovery_metrics.py)
# ---------------------------------------------------------------------------

def classify_pair(src_as: int, dst_as: int,
                  core_ases: Set[int], customer_ases: Set[int]) -> str:
    src_core = src_as in core_ases
    dst_core = dst_as in core_ases
    src_cust = src_as in customer_ases
    dst_cust = dst_as in customer_ases
    if src_core and dst_core:
        return "core-core"
    if src_cust and dst_cust:
        return "customer-customer"
    if (src_cust and dst_core) or (src_core and dst_cust):
        return "customer-core"
    return "other"


def parse_as_set(s: str) -> Set[int]:
    result: Set[int] = set()
    for item in s.split(","):
        item = item.strip()
        if item:
            result.add(int(item))
    return result


def infer_classes_from_topology(path: str) -> Tuple[Set[int], Set[int]]:
    core: Set[int] = set()
    customer: Set[int] = set()
    try:
        root = ET.parse(path).getroot()
        ases = root.find("ases")
        if ases is None:
            return core, customer
        for as_node in ases.findall("as"):
            as_id_raw = as_node.get("id", "")
            if not as_id_raw:
                continue
            as_id = int(as_id_raw)
            as_type = ""
            for prop in as_node.findall("property"):
                if (prop.get("name") or "").strip().lower() == "type":
                    as_type = (prop.get("value") or prop.text or "").strip().lower()
                    break
            if as_type == "core":
                core.add(as_id)
            else:
                customer.add(as_id)
    except Exception:
        pass
    return core, customer


def resolve_as_classes(args: argparse.Namespace) -> Tuple[Set[int], Set[int]]:
    core = parse_as_set(args.core_ases)
    customer = parse_as_set(args.customer_ases)
    if (not core or not customer) and args.topology:
        ic, icu = infer_classes_from_topology(args.topology)
        if not core:
            core = ic
        if not customer:
            customer = icu
    return core, customer


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def load_link_events(path: str) -> List[LinkEvent]:
    events: List[LinkEvent] = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            events.append(LinkEvent(
                time_s=float(row["time_s"]),
                event=row["event"].strip(),
                as_a=int(row["as_a"]),
                as_b=int(row["as_b"]),
            ))
    events.sort(key=lambda e: e.time_s)
    return events


def load_probe_events(path: str) -> List[ProbeEvent]:
    events: List[ProbeEvent] = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rtt = float(row["rtt_ms"]) if row["rtt_ms"].strip() else None
            events.append(ProbeEvent(
                time_s=float(row["time_s"]),
                seq=int(row["seq"]),
                kind=row["event"].strip(),
                rtt_ms=rtt,
            ))
    events.sort(key=lambda e: (e.time_s, e.seq))
    return events


def load_cp_events(path: str) -> List[CpEvent]:
    events: List[CpEvent] = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            events.append(CpEvent(
                time_s=float(row["time_s"]),
                local_asn=int(row["local_asn"]) if row["local_asn"].strip() else 0,
                peer_asn=int(row["peer_asn"]) if row["peer_asn"].strip() else 0,
                event=row["event"].strip(),
                detail=row["detail"].strip(),
            ))
    events.sort(key=lambda e: e.time_s)
    return events


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

def find_failure_events(link_events: List[LinkEvent]) -> List[Tuple[LinkEvent, Optional[LinkEvent]]]:
    """Pair each link_down with its subsequent link_up (same pair) if one exists."""
    pairs: List[Tuple[LinkEvent, Optional[LinkEvent]]] = []
    for i, ev in enumerate(link_events):
        if ev.event != "link_down":
            continue
        recovery: Optional[LinkEvent] = None
        for later in link_events[i + 1:]:
            if later.event == "link_up" and {later.as_a, later.as_b} == {ev.as_a, ev.as_b}:
                recovery = later
                break
        pairs.append((ev, recovery))
    return pairs


def analyse_probe_pair(
    probe_events: List[ProbeEvent],
    down_ev: LinkEvent,
    up_ev: Optional[LinkEvent],
    next_down_time: Optional[float] = None,
    grace_after_up: float = 300.0,
) -> EventMetrics:
    """Compute convergence metrics for one probe pair over one failure window."""

    t_down = down_ev.time_s
    t_up = up_ev.time_s if up_ev else None

    # Analysis window: from link_down to link_up + grace, capped at next failure.
    raw_window_end = (t_up + grace_after_up) if t_up else (t_down + grace_after_up)
    window_end = raw_window_end
    if next_down_time is not None:
        window_end = min(window_end, next_down_time - 0.001)

    # Collect timeouts and replies in the window
    timeouts_in_window = [
        e for e in probe_events
        if e.kind == "timeout" and e.time_s >= t_down and e.time_s <= window_end
    ]
    replies_in_window = [
        e for e in probe_events
        if e.kind == "reply" and e.time_s >= t_down and e.time_s <= window_end
    ]

    first_timeout = timeouts_in_window[0] if timeouts_in_window else None
    last_timeout = timeouts_in_window[-1] if timeouts_in_window else None

    # First reply AFTER the last timeout (data-plane recovery)
    first_recovery: Optional[ProbeEvent] = None
    if last_timeout:
        post_loss = [e for e in replies_in_window if e.time_s > last_timeout.time_s]
        first_recovery = post_loss[0] if post_loss else None

    switchover_delay = (first_timeout.time_s - t_down) if first_timeout else None
    outage_duration = (
        first_recovery.time_s - first_timeout.time_s
        if first_timeout and first_recovery else None
    )

    recovery_delay: Optional[float] = None
    if first_recovery and t_up is not None:
        recovery_delay = max(0.0, first_recovery.time_s - t_up)

    if not first_timeout:
        status = "no_loss"
    elif not first_recovery:
        status = "no_recovery"
    else:
        status = "switched_and_recovered"

    return EventMetrics(
        event_idx=0,
        link_as_a=down_ev.as_a,
        link_as_b=down_ev.as_b,
        link_down_time=t_down,
        link_up_time=t_up,
        first_timeout_time=first_timeout.time_s if first_timeout else None,
        last_timeout_time=last_timeout.time_s if last_timeout else None,
        first_recovery_time=first_recovery.time_s if first_recovery else None,
        switchover_delay_s=switchover_delay,
        recovery_delay_s=recovery_delay,
        outage_duration_s=outage_duration,
        bgp_detection_time=None,
        bgp_detection_delay_s=None,
        bgp_first_update_time=None,
        status=status,
    )


def enrich_with_cp(
    metrics: EventMetrics,
    all_cp: List[CpEvent],
    down_ev: LinkEvent,
) -> None:
    """Fill BGP control-plane timings into an EventMetrics object (in-place)."""
    t_down = down_ev.time_s
    window_start = t_down - 5.0   # small buffer for events right at t_down
    window_end = t_down + 300.0

    # BGP session drops: look for ESTABLISHED->IDLE where one of the ASNs is
    # on the failed link.  We check all per-AS CSV data (passed merged).
    failed_pair = {down_ev.as_a, down_ev.as_b}
    detection: Optional[float] = None
    first_update: Optional[float] = None

    for ev in all_cp:
        if ev.time_s < window_start or ev.time_s > window_end:
            continue
        if ev.event == "STATE_CHANGE" and ev.detail == "ESTABLISHED->IDLE":
            if {ev.local_asn, ev.peer_asn} == failed_pair:
                if detection is None or ev.time_s < detection:
                    detection = ev.time_s
        if ev.event == "UPDATE" and detection is not None and ev.time_s >= detection:
            if first_update is None:
                first_update = ev.time_s

    metrics.bgp_detection_time = detection
    metrics.bgp_detection_delay_s = (detection - t_down) if detection else None
    metrics.bgp_first_update_time = first_update


def count_cp_messages(all_cp: List[CpEvent], t_start: float, t_end: float) -> Dict[str, int]:
    counts: Dict[str, int] = {
        "OPEN": 0, "KEEPALIVE": 0,
        "UPDATE_announce": 0, "UPDATE_withdraw": 0, "UPDATE_other": 0,
        "NOTIFICATION": 0, "STATE_CHANGE": 0,
    }
    for ev in all_cp:
        if ev.time_s < t_start or ev.time_s > t_end:
            continue
        if ev.event == "KEEPALIVE":
            counts["KEEPALIVE"] += 1
        elif ev.event == "OPEN":
            counts["OPEN"] += 1
        elif ev.event == "NOTIFICATION":
            counts["NOTIFICATION"] += 1
        elif ev.event == "STATE_CHANGE":
            counts["STATE_CHANGE"] += 1
        elif ev.event == "UPDATE":
            if ev.detail == "announce":
                counts["UPDATE_announce"] += 1
            elif ev.detail == "withdraw":
                counts["UPDATE_withdraw"] += 1
            else:
                counts["UPDATE_other"] += 1
    return counts


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

def fmt(v: Optional[float], decimals: int = 3) -> str:
    return f"{v:.{decimals}f}" if v is not None else "N/A"


def compute_stats(vals: List[float]) -> Dict[str, float]:
    if not vals:
        return {
            "count": 0,
            "mean": 0.0,
            "p50": 0.0,
            "p95": 0.0,
            "p99": 0.0,
            "min": 0.0,
            "max": 0.0,
            "stdev": 0.0,
        }

    sorted_vals = sorted(vals)

    def percentile(p: float) -> float:
        if len(sorted_vals) == 1:
            return sorted_vals[0]
        idx = (len(sorted_vals) - 1) * p
        lo = int(idx)
        hi = min(lo + 1, len(sorted_vals) - 1)
        frac = idx - lo
        return sorted_vals[lo] * (1.0 - frac) + sorted_vals[hi] * frac

    return {
        "count": len(sorted_vals),
        "mean": statistics.mean(sorted_vals),
        "p50": percentile(0.50),
        "p95": percentile(0.95),
        "p99": percentile(0.99),
        "min": min(sorted_vals),
        "max": max(sorted_vals),
        "stdev": statistics.pstdev(sorted_vals) if len(sorted_vals) > 1 else 0.0,
    }


def print_section(title: str) -> None:
    print()
    print("=" * 70)
    print(f"  {title}")
    print("=" * 70)


def print_pair_table(
    pair: Tuple[int, int],
    failure_pairs: List[Tuple[LinkEvent, Optional[LinkEvent]]],
    probe_events: List[ProbeEvent],
    all_cp: List[CpEvent],
    pair_class: str = "?",
    grace_after_up: float = 300.0,
) -> List[EventMetrics]:
    src_as, dst_as = pair
    print(f"\nProbe pair AS{src_as} -> AS{dst_as}  [{pair_class}]")
    print(f"  {'Event':>5}  {'FailedLink':>12}  {'T_down':>8}  "
          f"{'Switchover':>12}  {'SW_delay':>10}  "
          f"{'Outage':>8}  {'Rec_delay':>10}  "
          f"{'BGP_detect':>12}  {'Status'}")
    print("  " + "-" * 106)

    results: List[EventMetrics] = []
    for idx, (down_ev, up_ev) in enumerate(failure_pairs):
        # Cap window at the next link_down to avoid cross-event contamination
        next_down: Optional[float] = None
        if idx + 1 < len(failure_pairs):
            next_down = failure_pairs[idx + 1][0].time_s
        m = analyse_probe_pair(probe_events, down_ev, up_ev,
                               next_down_time=next_down,
                               grace_after_up=grace_after_up)
        m.event_idx = idx + 1
        enrich_with_cp(m, all_cp, down_ev)
        results.append(m)

        link_str = f"AS{down_ev.as_a}-AS{down_ev.as_b}"
        sw_time = fmt(m.first_timeout_time)
        print(f"  {m.event_idx:>5}  {link_str:>12}  {fmt(m.link_down_time):>8}  "
              f"{sw_time:>12}  {fmt(m.switchover_delay_s):>10}  "
              f"{fmt(m.outage_duration_s):>8}  {fmt(m.recovery_delay_s):>10}  "
              f"{fmt(m.bgp_detection_delay_s):>12}  "
              f"{m.status}")

    return results


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Analyse BGP convergence from bgp-convergence-first outputs")
    p.add_argument("--dir", default="build/bgp_convergence_first",
                   help="Directory containing probe_*.csv, link_events.csv, bgp_cp_as*.csv")
    p.add_argument("--out-csv", default="",
                   help="If set, write per-pair per-event metrics to this CSV file")
    p.add_argument("--cp-summary-csv", default="",
                   help="If set, write control-plane message counts to this CSV file")
    p.add_argument("--out-json", default="",
                   help="If set, write SCION-compatible scenario metrics JSON")
    p.add_argument("--grace", type=float, default=300.0,
                   help="Seconds after link_up to keep searching for recovery (default 300)")
    p.add_argument("--core-ases", default="",
                   help="Comma-separated ASNs to treat as core, e.g. '101,102,103,104,105,106'")
    p.add_argument("--customer-ases", default="",
                   help="Comma-separated ASNs to treat as customer/non-core, e.g. '107,108'")
    p.add_argument("--topology", default="",
                   help="Optional topology XML to infer core/customer classes automatically")
    return p.parse_args()


def main() -> None:
    args = parse_args()
    data_dir = args.dir

    # --- Load link events ---
    link_events_path = os.path.join(data_dir, "link_events.csv")
    if not os.path.exists(link_events_path):
        print(f"ERROR: {link_events_path} not found.")
        raise SystemExit(1)
    link_events = load_link_events(link_events_path)
    failure_pairs = find_failure_events(link_events)

    # --- Resolve AS classes for pair labeling ---
    core_ases, customer_ases = resolve_as_classes(args)
    if not core_ases and not customer_ases:
        # Fall back to topology file in the workspace if it exists alongside the data
        default_topo = "topology/manual_link_fail_6as_topology.xml"
        if os.path.exists(default_topo):
            core_ases, customer_ases = infer_classes_from_topology(default_topo)
    if core_ases or customer_ases:
        print(f"\nAS classes: core={sorted(core_ases)}  customer={sorted(customer_ases)}")

    print_section("Link failure schedule")
    for down_ev, up_ev in failure_pairs:
        up_str = f"{up_ev.time_s:.1f}s" if up_ev else "no recovery"
        print(f"  AS{down_ev.as_a}-AS{down_ev.as_b}: down at {down_ev.time_s:.1f}s, up at {up_str}")

    # --- Load control-plane CSVs (merge all ASes) ---
    all_cp: List[CpEvent] = []
    cp_files = sorted(glob.glob(os.path.join(data_dir, "bgp_cp_as*.csv")))
    if cp_files:
        for cp_path in cp_files:
            all_cp.extend(load_cp_events(cp_path))
        all_cp.sort(key=lambda e: e.time_s)
        print(f"\nLoaded {len(all_cp)} control-plane events from {len(cp_files)} files")
    else:
        print("\nNo bgp_cp_as*.csv files found — BGP control-plane metrics unavailable")

    # --- Load probe CSVs ---
    probe_files = sorted(glob.glob(os.path.join(data_dir, "probe_*.csv")))
    if not probe_files:
        print(f"ERROR: No probe_*.csv files found in {data_dir}")
        raise SystemExit(1)

    # Parse probe pair identifiers
    pairs: List[Tuple[int, int]] = []
    pair_probes: Dict[Tuple[int, int], List[ProbeEvent]] = {}
    for pf in probe_files:
        basename = os.path.basename(pf)  # probe_101_106.csv
        parts = basename.replace(".csv", "").split("_")
        if len(parts) >= 3:
            src, dst = int(parts[1]), int(parts[2])
            pairs.append((src, dst))
            pair_probes[(src, dst)] = load_probe_events(pf)

    # --- Per-pair per-event analysis ---
    print_section("Data-plane convergence metrics (per probe pair)")
    print("  Columns: Event | FailedLink | T_down(s) | FirstTimeout(s) | SW_delay(s) |"
          " Outage(s) | Rec_delay_from_up(s) | BGP_detect_delay(s) | Status")

    all_results: List[Dict] = []
    for pair in pairs:
        src_as, dst_as = pair
        probe_evs = pair_probes[pair]
        pclass = classify_pair(src_as, dst_as, core_ases, customer_ases)
        results = print_pair_table(pair, failure_pairs, probe_evs, all_cp,
                                   pair_class=pclass,
                                   grace_after_up=args.grace)
        for m in results:
            all_results.append({
                "src_as": src_as,
                "dst_as": dst_as,
                "pair_class": pclass,
                "event_idx": m.event_idx,
                "failed_link": f"AS{m.link_as_a}-AS{m.link_as_b}",
                "link_down_time_s": m.link_down_time,
                "link_up_time_s": m.link_up_time,
                "first_timeout_time_s": m.first_timeout_time,
                "last_timeout_time_s": m.last_timeout_time,
                "first_recovery_time_s": m.first_recovery_time,
                "switchover_delay_s": m.switchover_delay_s,
                "recovery_delay_from_up_s": m.recovery_delay_s,
                "outage_duration_s": m.outage_duration_s,
                "bgp_detection_time_s": m.bgp_detection_time,
                "bgp_detection_delay_s": m.bgp_detection_delay_s,
                "bgp_first_update_time_s": m.bgp_first_update_time,
                "status": m.status,
            })

    # --- Aggregate statistics ---
    sw_delays = [r["switchover_delay_s"] for r in all_results if r["switchover_delay_s"] is not None]
    rec_delays = [r["recovery_delay_from_up_s"] for r in all_results if r["recovery_delay_from_up_s"] is not None]
    outages = [r["outage_duration_s"] for r in all_results if r["outage_duration_s"] is not None]
    bgp_det = [r["bgp_detection_delay_s"] for r in all_results if r["bgp_detection_delay_s"] is not None]

    def stats_line(label: str, vals: List[float]) -> None:
        if not vals:
            print(f"  {label}: N/A")
            return
        print(f"  {label}: "
              f"min={min(vals):.3f}s  "
              f"median={statistics.median(vals):.3f}s  "
              f"mean={statistics.mean(vals):.3f}s  "
              f"max={max(vals):.3f}s  "
              f"(n={len(vals)})")

    status_counts: Dict[str, int] = {}
    for r in all_results:
        status_counts[r["status"]] = status_counts.get(r["status"], 0) + 1

    print_section("Aggregate statistics")
    stats_line("Switchover delay       ", sw_delays)
    stats_line("Outage duration        ", outages)
    stats_line("Recovery delay (from up)", rec_delays)
    stats_line("BGP detection delay    ", bgp_det)
    print(f"\n  Status breakdown: {status_counts}")

    # --- Per-class aggregates ---
    all_classes = sorted(set(r["pair_class"] for r in all_results))
    if len(all_classes) > 1:
        print_section("Aggregate statistics by pair class")
        for pc in all_classes:
            class_rows = [r for r in all_results if r["pair_class"] == pc]
            sw = [r["switchover_delay_s"] for r in class_rows if r["switchover_delay_s"] is not None]
            out = [r["outage_duration_s"] for r in class_rows if r["outage_duration_s"] is not None]
            rec = [r["recovery_delay_from_up_s"] for r in class_rows if r["recovery_delay_from_up_s"] is not None]
            det = [r["bgp_detection_delay_s"] for r in class_rows if r["bgp_detection_delay_s"] is not None]
            sc = {s: sum(1 for r in class_rows if r["status"] == s) for s in set(r["status"] for r in class_rows)}
            print(f"\n  [{pc}]  n={len(class_rows)} observations")
            stats_line("  Switchover delay       ", sw)
            stats_line("  Outage duration        ", out)
            stats_line("  Recovery delay (from up)", rec)
            stats_line("  BGP detection delay    ", det)
            print(f"    Status: {sc}")

    # --- BGP control-plane summary ---
    if all_cp and failure_pairs:
        print_section("BGP control-plane messages per failure window")
        for down_ev, up_ev in failure_pairs:
            t_start = down_ev.time_s
            t_end = up_ev.time_s + args.grace if up_ev else down_ev.time_s + args.grace
            counts = count_cp_messages(all_cp, t_start, t_end)
            link_str = f"AS{down_ev.as_a}-AS{down_ev.as_b}"
            print(f"\n  Failure: {link_str} down at {down_ev.time_s:.1f}s "
                  f"(window {t_start:.0f}s–{t_end:.0f}s)")
            for k, v in counts.items():
                print(f"    {k:25s}: {v}")

        # Global totals (full simulation, no KEEPALIVE)
        print_section("BGP control-plane global totals (no KEEPALIVE)")
        global_counts = count_cp_messages(all_cp, 0.0, float("inf"))
        for k, v in global_counts.items():
            if k != "KEEPALIVE":
                print(f"  {k:25s}: {v}")

    # --- Write optional CSVs ---
    if args.out_csv and all_results:
        fieldnames = list(all_results[0].keys())
        with open(args.out_csv, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(all_results)
        print(f"\nWrote per-pair metrics to {args.out_csv}")

    if args.cp_summary_csv and all_cp and failure_pairs:
        rows = []
        for down_ev, up_ev in failure_pairs:
            t_start = down_ev.time_s
            t_end = up_ev.time_s + args.grace if up_ev else down_ev.time_s + args.grace
            counts = count_cp_messages(all_cp, t_start, t_end)
            row = {
                "failed_link": f"AS{down_ev.as_a}-AS{down_ev.as_b}",
                "link_down_time_s": down_ev.time_s,
                "link_up_time_s": up_ev.time_s if up_ev else "",
            }
            row.update(counts)
            rows.append(row)
        with open(args.cp_summary_csv, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print(f"Wrote control-plane summary to {args.cp_summary_csv}")

    if args.out_json:
        # Build per-failure recovery latency from failure time to first recovered reply.
        failure_events_json: List[Dict] = []
        all_recovery_latencies: List[float] = []

        for idx, (down_ev, _up_ev) in enumerate(failure_pairs, start=1):
            event_rows = [
                r for r in all_results
                if r["event_idx"] == idx and r["first_recovery_time_s"] is not None
            ]

            event_latencies: List[float] = []
            by_as_class: Dict[str, List[float]] = {}

            for r in event_rows:
                rec = r["first_recovery_time_s"]
                if rec is None:
                    continue
                lat = rec - down_ev.time_s
                if lat < 0:
                    continue
                event_latencies.append(lat)
                all_recovery_latencies.append(lat)

                pair_class = r.get("pair_class", "other")
                if pair_class == "core-core":
                    cls = "core"
                elif pair_class == "customer-customer":
                    cls = "non-core"
                elif pair_class == "customer-core":
                    cls = "mixed"
                else:
                    cls = "other"

                by_as_class.setdefault(cls, []).append(lat)

            failure_events_json.append({
                "failure_time_s": down_ev.time_s,
                "recovery_latency_stats": compute_stats(event_latencies),
                "by_as_class": by_as_class,
            })

        out_doc = {
            "failure_events": failure_events_json,
            "scenario_stats": compute_stats(all_recovery_latencies),
        }

        with open(args.out_json, "w") as f:
            json.dump(out_doc, f, indent=2)
        print(f"Wrote scenario metrics JSON to {args.out_json}")


if __name__ == "__main__":
    main()
