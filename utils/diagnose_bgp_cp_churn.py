#!/usr/bin/env python3
"""Diagnose BGP control-plane churn from bgp_cp_as*.csv logs.

This script is designed for very large CSVs. It streams rows once and aggregates:
- Per-time-bin event counts (UPDATE/KEEPALIVE/STATE_CHANGE/...)
- Per-peer update counts by time bin
- Per-file high-level churn indicators

Typical usage:
  python3 utils/diagnose_bgp_cp_churn.py \
    --inputs build/bgp_ixp_dual_ixp_visible_10k_dual_ixp_visible/bgp_cp_as121.csv \
             build/bgp_ixp_dual_ixp_visible_10k_dual_ixp_visible/bgp_cp_as104.csv \
    --bin-seconds 10 \
    --out-prefix build/bgp_churn_diag/visible
"""

from __future__ import annotations

import argparse
import csv
import math
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass
class FileSummary:
    path: Path
    rows: int
    t_min: float
    t_max: float
    duration_s: float
    updates: int
    withdraws: int
    announces: int
    keepalives: int
    state_changes: int
    notifications: int
    opens: int
    update_rate_s: float
    max_bin_updates: int
    max_bin_start_s: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Diagnose potential BGP churn/oscillation from control-plane logs"
    )
    parser.add_argument(
        "--inputs",
        nargs="+",
        required=True,
        help="One or more bgp_cp_as*.csv files",
    )
    parser.add_argument(
        "--bin-seconds",
        type=float,
        default=10.0,
        help="Time bin size for aggregation in seconds (default: 10)",
    )
    parser.add_argument(
        "--out-prefix",
        type=Path,
        default=Path("build/bgp_churn_diag/churn"),
        help="Output prefix for generated CSVs (default: build/bgp_churn_diag/churn)",
    )
    parser.add_argument(
        "--top-bins",
        type=int,
        default=8,
        help="Number of hottest bins to print per input file (default: 8)",
    )
    parser.add_argument(
        "--top-peers",
        type=int,
        default=6,
        help="Number of peers to print by update volume per input file (default: 6)",
    )
    return parser.parse_args()


def _safe_float(raw: Optional[str], default: float = 0.0) -> float:
    try:
        return float(raw) if raw is not None and raw != "" else default
    except ValueError:
        return default


def _bin_index(time_s: float, bin_seconds: float) -> int:
    return int(math.floor(time_s / bin_seconds))


def analyze_file(
    path: Path,
    bin_seconds: float,
) -> Tuple[FileSummary, Dict[int, Counter], Dict[Tuple[int, int], Counter], Counter]:
    """Analyze one CP CSV.

    Returns:
      - FileSummary
      - bin_event_counts: bin_idx -> Counter(event/detail categories)
      - peer_bin_updates: (bin_idx, peer_asn) -> Counter(update detail categories)
      - peer_updates_total: peer_asn -> total UPDATE count
    """
    bin_event_counts: Dict[int, Counter] = defaultdict(Counter)
    peer_bin_updates: Dict[Tuple[int, int], Counter] = defaultdict(Counter)
    peer_updates_total: Counter = Counter()

    rows = 0
    t_min: Optional[float] = None
    t_max: Optional[float] = None

    updates = 0
    withdraws = 0
    announces = 0
    keepalives = 0
    state_changes = 0
    notifications = 0
    opens = 0

    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows += 1
            time_s = _safe_float(row.get("time_s"), 0.0)
            event = (row.get("event") or "").strip().upper()
            detail = (row.get("detail") or "").strip().lower()
            peer_asn = int(_safe_float(row.get("peer_asn"), 0.0))

            if t_min is None or time_s < t_min:
                t_min = time_s
            if t_max is None or time_s > t_max:
                t_max = time_s

            b = _bin_index(time_s, bin_seconds)
            bin_event_counts[b]["total"] += 1
            bin_event_counts[b][event] += 1

            if event == "UPDATE":
                updates += 1
                peer_updates_total[peer_asn] += 1
                peer_bin_updates[(b, peer_asn)]["updates"] += 1
                if detail == "withdraw":
                    withdraws += 1
                    bin_event_counts[b]["UPDATE_WITHDRAW"] += 1
                    peer_bin_updates[(b, peer_asn)]["withdraw"] += 1
                elif detail == "announce":
                    announces += 1
                    bin_event_counts[b]["UPDATE_ANNOUNCE"] += 1
                    peer_bin_updates[(b, peer_asn)]["announce"] += 1
                else:
                    bin_event_counts[b]["UPDATE_OTHER"] += 1
                    peer_bin_updates[(b, peer_asn)]["other"] += 1
            elif event == "KEEPALIVE":
                keepalives += 1
            elif event == "STATE_CHANGE":
                state_changes += 1
            elif event == "NOTIFICATION":
                notifications += 1
            elif event == "OPEN":
                opens += 1

    t_min_v = t_min if t_min is not None else 0.0
    t_max_v = t_max if t_max is not None else 0.0
    duration = max(0.0, t_max_v - t_min_v)
    update_rate = (updates / duration) if duration > 0 else 0.0

    max_bin_updates = 0
    max_bin_start_s = 0.0
    for b, c in bin_event_counts.items():
        u = c.get("UPDATE", 0)
        if u > max_bin_updates:
            max_bin_updates = u
            max_bin_start_s = b * bin_seconds

    summary = FileSummary(
        path=path,
        rows=rows,
        t_min=t_min_v,
        t_max=t_max_v,
        duration_s=duration,
        updates=updates,
        withdraws=withdraws,
        announces=announces,
        keepalives=keepalives,
        state_changes=state_changes,
        notifications=notifications,
        opens=opens,
        update_rate_s=update_rate,
        max_bin_updates=max_bin_updates,
        max_bin_start_s=max_bin_start_s,
    )
    return summary, bin_event_counts, peer_bin_updates, peer_updates_total


def write_summary_csv(path: Path, summaries: Iterable[FileSummary]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "file",
                "rows",
                "time_min_s",
                "time_max_s",
                "duration_s",
                "updates",
                "withdraws",
                "announces",
                "withdraw_ratio",
                "announce_ratio",
                "keepalives",
                "state_changes",
                "notifications",
                "opens",
                "update_rate_per_s",
                "max_bin_updates",
                "max_bin_start_s",
            ]
        )
        for s in summaries:
            w.writerow(
                [
                    str(s.path),
                    s.rows,
                    f"{s.t_min:.6f}",
                    f"{s.t_max:.6f}",
                    f"{s.duration_s:.6f}",
                    s.updates,
                    s.withdraws,
                    s.announces,
                    f"{(s.withdraws / s.updates) if s.updates else 0.0:.6f}",
                    f"{(s.announces / s.updates) if s.updates else 0.0:.6f}",
                    s.keepalives,
                    s.state_changes,
                    s.notifications,
                    s.opens,
                    f"{s.update_rate_s:.6f}",
                    s.max_bin_updates,
                    f"{s.max_bin_start_s:.6f}",
                ]
            )


def write_bin_csv(path: Path, rows: List[List[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "file",
                "bin_idx",
                "bin_start_s",
                "total",
                "UPDATE",
                "UPDATE_WITHDRAW",
                "UPDATE_ANNOUNCE",
                "UPDATE_OTHER",
                "KEEPALIVE",
                "STATE_CHANGE",
                "OPEN",
                "NOTIFICATION",
            ]
        )
        w.writerows(rows)


def write_peer_bin_csv(path: Path, rows: List[List[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "peer_asn", "bin_idx", "bin_start_s", "updates", "withdraw", "announce", "other"])
        w.writerows(rows)


def main() -> None:
    args = parse_args()
    if args.bin_seconds <= 0:
        raise ValueError("--bin-seconds must be > 0")

    inputs = [Path(p) for p in args.inputs]
    for p in inputs:
        if not p.exists():
            raise FileNotFoundError(f"Input file not found: {p}")

    summaries: List[FileSummary] = []
    bin_rows: List[List[object]] = []
    peer_bin_rows: List[List[object]] = []

    for path in inputs:
        summary, bin_counts, peer_bin_updates, peer_updates_total = analyze_file(path, args.bin_seconds)
        summaries.append(summary)

        print(f"\n== {path} ==")
        print(
            "rows={rows} duration={dur:.3f}s updates={upd} withdraw={wd} announce={ann} update_rate={rate:.2f}/s".format(
                rows=summary.rows,
                dur=summary.duration_s,
                upd=summary.updates,
                wd=summary.withdraws,
                ann=summary.announces,
                rate=summary.update_rate_s,
            )
        )

        hottest = sorted(
            bin_counts.items(), key=lambda kv: kv[1].get("UPDATE", 0), reverse=True
        )[: args.top_bins]
        print("Top bins by UPDATE count:")
        for b, c in hottest:
            print(
                "  t=[{s:.0f},{e:.0f}) UPDATE={u} withdraw={w} announce={a} total={t}".format(
                    s=b * args.bin_seconds,
                    e=(b + 1) * args.bin_seconds,
                    u=c.get("UPDATE", 0),
                    w=c.get("UPDATE_WITHDRAW", 0),
                    a=c.get("UPDATE_ANNOUNCE", 0),
                    t=c.get("total", 0),
                )
            )

        print("Top peers by UPDATE volume:")
        for peer_asn, cnt in peer_updates_total.most_common(args.top_peers):
            print(f"  peer {peer_asn}: {cnt}")

        for b in sorted(bin_counts.keys()):
            c = bin_counts[b]
            bin_rows.append(
                [
                    str(path),
                    b,
                    f"{b * args.bin_seconds:.6f}",
                    c.get("total", 0),
                    c.get("UPDATE", 0),
                    c.get("UPDATE_WITHDRAW", 0),
                    c.get("UPDATE_ANNOUNCE", 0),
                    c.get("UPDATE_OTHER", 0),
                    c.get("KEEPALIVE", 0),
                    c.get("STATE_CHANGE", 0),
                    c.get("OPEN", 0),
                    c.get("NOTIFICATION", 0),
                ]
            )

        for (b, peer_asn), c in sorted(peer_bin_updates.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            peer_bin_rows.append(
                [
                    str(path),
                    peer_asn,
                    b,
                    f"{b * args.bin_seconds:.6f}",
                    c.get("updates", 0),
                    c.get("withdraw", 0),
                    c.get("announce", 0),
                    c.get("other", 0),
                ]
            )

    summary_csv = args.out_prefix.with_name(args.out_prefix.name + "_summary.csv")
    bins_csv = args.out_prefix.with_name(args.out_prefix.name + "_bins.csv")
    peer_bins_csv = args.out_prefix.with_name(args.out_prefix.name + "_peer_bins.csv")

    write_summary_csv(summary_csv, summaries)
    write_bin_csv(bins_csv, bin_rows)
    write_peer_bin_csv(peer_bins_csv, peer_bin_rows)

    print("\nWrote:")
    print(f"  {summary_csv}")
    print(f"  {bins_csv}")
    print(f"  {peer_bins_csv}")


if __name__ == "__main__":
    main()
