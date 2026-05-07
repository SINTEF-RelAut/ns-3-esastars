#!/usr/bin/env python3
"""Quality checks for SCION path usage and path-request forwarding.

This script inspects SCION run artifacts and highlights cases where:
1) valid paths exist in snapshots but probes still fail, and
2) available snapshot paths are never selected by probes.

It can also parse AS107 debug logs to verify that path requests are forwarded
and answered (PS-REQ -> PS-SEND -> SEG-RX).
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple


@dataclass
class PairProbeStats:
    pair: str
    sent: int
    reply: int
    timeout: int
    unique_sent_paths: Set[str]


@dataclass
class PairSnapshotStats:
    pair: str
    samples: int
    valid_samples: int
    max_valid_paths: int
    distinct_best_paths: Set[str]


def read_probe_stats(run_dir: Path) -> Dict[str, PairProbeStats]:
    out: Dict[str, PairProbeStats] = {}
    for probe_csv in sorted(run_dir.glob("scion_probe_*.csv")):
        pair = probe_csv.stem.replace("scion_probe_", "")
        sent = reply = timeout = 0
        unique_sent_paths: Set[str] = set()
        with probe_csv.open() as f:
            for row in csv.DictReader(f):
                ev = row.get("event", "")
                if ev == "sent":
                    sent += 1
                    path_used = row.get("path_used", "")
                    if path_used:
                        unique_sent_paths.add(path_used)
                elif ev == "reply":
                    reply += 1
                elif ev == "timeout":
                    timeout += 1
        out[pair] = PairProbeStats(pair, sent, reply, timeout, unique_sent_paths)
    return out


def read_snapshot_stats(run_dir: Path) -> Dict[str, PairSnapshotStats]:
    snap_csv = run_dir / "scion_path_snapshots.csv"
    out: Dict[str, PairSnapshotStats] = {}
    if not snap_csv.exists():
        return out

    by_pair: Dict[str, List[dict]] = {}
    with snap_csv.open() as f:
        for row in csv.DictReader(f):
            pair = f"{row['src_as']}_{row['dst_as']}"
            by_pair.setdefault(pair, []).append(row)

    for pair, rows in by_pair.items():
        valid_values = [int(r.get("valid_paths", "0") or 0) for r in rows]
        best_paths = {r.get("best_path", "") for r in rows if r.get("best_path", "")}
        out[pair] = PairSnapshotStats(
            pair=pair,
            samples=len(rows),
            valid_samples=sum(1 for v in valid_values if v > 0),
            max_valid_paths=max(valid_values) if valid_values else 0,
            distinct_best_paths=best_paths,
        )

    return out


def summarize_run(run_dir: Path) -> Tuple[List[str], List[str], List[str]]:
    probe = read_probe_stats(run_dir)
    snaps = read_snapshot_stats(run_dir)

    header = [
        f"Run: {run_dir}",
        "pair,sent,reply,timeout,max_valid_paths,distinct_snapshot_paths,distinct_sent_paths,unused_snapshot_paths",
    ]
    rows: List[str] = []
    findings: List[str] = []

    zero_reply_pairs = 0
    localized_ok_pairs = 0

    for pair in sorted(probe.keys()):
        p = probe[pair]
        s = snaps.get(pair, PairSnapshotStats(pair, 0, 0, 0, set()))

        unused = s.distinct_best_paths - p.unique_sent_paths
        rows.append(
            ",".join(
                [
                    pair,
                    str(p.sent),
                    str(p.reply),
                    str(p.timeout),
                    str(s.max_valid_paths),
                    str(len(s.distinct_best_paths)),
                    str(len(p.unique_sent_paths)),
                    str(len(unused)),
                ]
            )
        )

        has_valid_paths = s.max_valid_paths > 0
        stuck_on_one_path = len(p.unique_sent_paths) <= 1
        snapshot_has_alternatives = len(s.distinct_best_paths) >= 2

        if p.reply == 0:
            zero_reply_pairs += 1

        if p.reply > 0 and has_valid_paths:
            localized_ok_pairs += 1

        if p.reply == 0 and has_valid_paths:
            findings.append(
                f"WARN {pair}: zero replies despite max_valid_paths={s.max_valid_paths}."
            )
        if snapshot_has_alternatives and stuck_on_one_path and p.timeout > 0:
            findings.append(
                f"WARN {pair}: snapshot has {len(s.distinct_best_paths)} best-path variants but sender used {len(p.unique_sent_paths)} path(s)."
            )

    findings.append(
        f"INFO run_health: pairs={len(probe)}, zero_reply_pairs={zero_reply_pairs}, healthy_pairs_with_replies={localized_ok_pairs}"
    )

    return header, rows, findings


def check_debug_forwarding(debug_log: Path, dst_alias: int) -> List[str]:
    if not debug_log.exists():
        return [f"INFO debug_log_missing: {debug_log}"]

    req = send = seg = 0
    req_pat = re.compile(r"\[AS107-PS-REQ\].*type=DOWN.*dstIA=%d\b" % dst_alias)
    send_pat = re.compile(r"\[AS107-PS-SEND\].*type=DOWN.*dstIA=%d\b" % dst_alias)
    seg_pat = re.compile(r"\[AS107-SEG-RX\].*segType=DOWN.*dstIA=%d\b" % dst_alias)

    with debug_log.open(errors="ignore") as f:
        for line in f:
            if req_pat.search(line):
                req += 1
            if send_pat.search(line):
                send += 1
            if seg_pat.search(line):
                seg += 1

    result = [
        f"INFO forwarding dst_alias={dst_alias}: req={req}, send={send}, seg_rx={seg}",
    ]
    if req == 0:
        result.append("WARN forwarding: no DOWN requests observed for destination alias")
    elif send == 0 or seg == 0:
        result.append("WARN forwarding: requests observed but no sends/segment-receive evidence")
    else:
        result.append("OK forwarding: requests are forwarded and segments are received")

    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="SCION path-usage quality checks")
    parser.add_argument(
        "--run-dir",
        action="append",
        required=True,
        help="Run directory (e.g., build/scion_scenario_a_pcb10_clk10)",
    )
    parser.add_argument(
        "--debug-log",
        action="append",
        default=[],
        help="Optional debug log for forwarding checks",
    )
    parser.add_argument(
        "--dst-alias",
        type=int,
        default=7,
        help="Destination alias ASN used in AS107 debug markers (default: 7 for real AS108)",
    )
    args = parser.parse_args()

    all_findings: List[str] = []

    for run_dir_str in args.run_dir:
        run_dir = Path(run_dir_str)
        header, rows, findings = summarize_run(run_dir)
        print("\n".join(header))
        print("\n".join(rows))
        print("Findings:")
        for f in findings:
            print(f"- {f}")
        print()
        all_findings.extend(findings)

    for dbg in args.debug_log:
        print(f"Debug forwarding check: {dbg}")
        for line in check_debug_forwarding(Path(dbg), args.dst_alias):
            print(f"- {line}")
        print()

    has_warn = any(x.startswith("WARN") for x in all_findings)
    return 0 if not has_warn else 2


if __name__ == "__main__":
    raise SystemExit(main())
