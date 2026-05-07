#!/usr/bin/env python3
"""Correlate BGP control-plane churn hotspots with scheduled link events.

Inputs:
- bins CSV from utils/diagnose_bgp_cp_churn.py (*_bins.csv)
- link_events.csv from a scenario output directory

Outputs:
- hotspot summary CSV (top bins by UPDATE count with nearest link events)
- optional console summary
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


AS_RE = re.compile(r"bgp_cp_as(\d+)\.csv$")


@dataclass
class LinkEvent:
    time_s: float
    event: str
    as_a: int
    as_b: int
    if_id: int


@dataclass
class BinRow:
    file: str
    bin_idx: int
    bin_start_s: float
    update: int
    update_withdraw: int
    update_announce: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Correlate churn spikes with link events")
    parser.add_argument("--bins-csv", type=Path, required=True, help="*_bins.csv from diagnose_bgp_cp_churn.py")
    parser.add_argument("--link-events", type=Path, required=True, help="link_events.csv for same scenario")
    parser.add_argument("--out-csv", type=Path, required=True, help="Output CSV path")
    parser.add_argument("--top-bins", type=int, default=20, help="Number of hottest bins per file to report")
    parser.add_argument("--before-window", type=float, default=180.0, help="Seconds before bin start to scan")
    parser.add_argument("--after-window", type=float, default=30.0, help="Seconds after bin start to scan")
    parser.add_argument("--min-updates", type=int, default=1, help="Ignore bins below this UPDATE count")
    return parser.parse_args()


def _to_int(raw: str, default: int = 0) -> int:
    try:
        return int(raw)
    except Exception:
        return default


def _to_float(raw: str, default: float = 0.0) -> float:
    try:
        return float(raw)
    except Exception:
        return default


def parse_local_as(file_path: str) -> Optional[int]:
    m = AS_RE.search(Path(file_path).name)
    if not m:
        return None
    return int(m.group(1))


def read_link_events(path: Path) -> List[LinkEvent]:
    out: List[LinkEvent] = []
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            out.append(
                LinkEvent(
                    time_s=_to_float(row.get("time_s", "0")),
                    event=(row.get("event", "") or "").strip(),
                    as_a=_to_int(row.get("as_a", "0")),
                    as_b=_to_int(row.get("as_b", "0")),
                    if_id=_to_int(row.get("if_id", "0")),
                )
            )
    return out


def read_bins(path: Path) -> Dict[str, List[BinRow]]:
    grouped: Dict[str, List[BinRow]] = {}
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            file_key = row.get("file", "")
            grouped.setdefault(file_key, []).append(
                BinRow(
                    file=file_key,
                    bin_idx=_to_int(row.get("bin_idx", "0")),
                    bin_start_s=_to_float(row.get("bin_start_s", "0")),
                    update=_to_int(row.get("UPDATE", "0")),
                    update_withdraw=_to_int(row.get("UPDATE_WITHDRAW", "0")),
                    update_announce=_to_int(row.get("UPDATE_ANNOUNCE", "0")),
                )
            )
    return grouped


def nearest_event(bin_start: float, events: List[LinkEvent]) -> Optional[LinkEvent]:
    if not events:
        return None
    return min(events, key=lambda e: abs(e.time_s - bin_start))


def events_in_window(bin_start: float, events: List[LinkEvent], before: float, after: float) -> List[LinkEvent]:
    t0 = bin_start - before
    t1 = bin_start + after
    return [e for e in events if t0 <= e.time_s <= t1]


def main() -> None:
    args = parse_args()
    if args.top_bins <= 0:
        raise ValueError("--top-bins must be > 0")
    if args.before_window < 0 or args.after_window < 0:
        raise ValueError("--before-window and --after-window must be >= 0")

    bins_by_file = read_bins(args.bins_csv)
    links = read_link_events(args.link_events)

    args.out_csv.parent.mkdir(parents=True, exist_ok=True)

    rows_out: List[List[object]] = []

    for file_key, rows in bins_by_file.items():
        local_as = parse_local_as(file_key)
        filtered = [r for r in rows if r.update >= args.min_updates]
        filtered.sort(key=lambda r: r.update, reverse=True)
        top = filtered[: args.top_bins]

        for r in top:
            win = events_in_window(r.bin_start_s, links, args.before_window, args.after_window)
            near = nearest_event(r.bin_start_s, links)

            win_down = sum(1 for e in win if e.event == "link_down")
            win_up = sum(1 for e in win if e.event == "link_up")
            win_local = 0
            win_remote = 0
            if local_as is not None:
                for e in win:
                    if e.as_a == local_as or e.as_b == local_as:
                        win_local += 1
                    else:
                        win_remote += 1

            rows_out.append(
                [
                    file_key,
                    local_as if local_as is not None else "",
                    r.bin_idx,
                    f"{r.bin_start_s:.6f}",
                    r.update,
                    r.update_withdraw,
                    r.update_announce,
                    f"{(r.update_withdraw / r.update) if r.update else 0.0:.6f}",
                    near.event if near else "",
                    f"{near.time_s:.6f}" if near else "",
                    f"{(near.time_s - r.bin_start_s):.6f}" if near else "",
                    near.as_a if near else "",
                    near.as_b if near else "",
                    near.if_id if near else "",
                    len(win),
                    win_down,
                    win_up,
                    win_local,
                    win_remote,
                ]
            )

    with args.out_csv.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "file",
                "local_as",
                "bin_idx",
                "bin_start_s",
                "updates",
                "withdraws",
                "announces",
                "withdraw_ratio",
                "nearest_event",
                "nearest_event_time_s",
                "nearest_event_delta_s",
                "nearest_as_a",
                "nearest_as_b",
                "nearest_if_id",
                "window_event_count",
                "window_link_down_count",
                "window_link_up_count",
                "window_events_involving_local_as",
                "window_events_not_involving_local_as",
            ]
        )
        writer.writerows(rows_out)

    print(f"Wrote: {args.out_csv}")
    print(f"Rows: {len(rows_out)}")


if __name__ == "__main__":
    main()
