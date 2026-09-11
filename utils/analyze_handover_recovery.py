#!/usr/bin/env python3
"""Measure per-handover recovery time, gating each event on the path it can affect.

A naive recovery metric closes each measurement window at the *next* link event anywhere in
the topology. With four satellites handing over independently, events land far more often
globally than on any one path, so most windows are cut short before the path has recovered
and are discarded as censored. The surviving sample is biased towards fast recoveries.

Gating fixes that: an event is only allowed to open or close a window for a probe pair whose
path could actually traverse the affected link. For a pair (src, dst) in the IXP topologies
the relevant events are those touching src's or dst's own attachment, since every other link
belongs to a satellite the path does not cross.

Recovery is measured on the data plane rather than from control-plane logs, so it means the
same thing for both protocols: the time from the link going down until probes flow again.

Recovery is derived from outages on the probe stream rather than by scanning rows inside a
window, because two timing details make the naive versions wrong:

* A lost probe's ``timeout`` row is written only when the probe timeout expires, 2s after
  the probe was sent. In row order it therefore appears *after* the reply to a later probe
  that already succeeded, so scanning row timestamps misses the real recovery and can never
  report anything below the probe timeout however fast the path came back.
* A probe sent shortly *before* an event can still be lost to it, because the event lands
  while the probe is in flight. Placing that loss at its send time pushes it before the
  event, which makes the event look harmless and strands the loss in the previous window.

So: outages are maximal runs of consecutive lost probes, located by send time; each is
attributed to the latest relevant event that could have caused it, meaning one falling no
later than the lost probe's timeout expiry; and recovery is the send time of the first probe
that replied afterwards, minus the event time. The floor is then the probe interval, and the
value is an upper bound on the true recovery with a resolution of one interval.

Every window gets one of three explicit outcomes and none are silently dropped:

  recovered  the path lost packets and then came back; the value is the gap in seconds
  no_impact  no probe was lost after the event, so the path never noticed it
  censored   the path was still down when the next relevant event arrived
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
import statistics
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple


@dataclass
class PairRecovery:
    """Recovery outcomes for one probe pair across all events affecting its path."""

    src_as: str
    dst_as: str
    relevant_events: int
    recovered: List[float] = field(default_factory=list)
    no_impact: int = 0
    censored: int = 0
    # Outages with no preceding relevant event to attribute them to.
    unattributed: int = 0

    @property
    def median_s(self) -> Optional[float]:
        return statistics.median(self.recovered) if self.recovered else None

    @property
    def p90_s(self) -> Optional[float]:
        if not self.recovered:
            return None
        ordered = sorted(self.recovered)
        return ordered[min(len(ordered) - 1, int(0.9 * len(ordered)))]


def load_link_downs(run_dir: Path, events_json: Optional[Path] = None) -> List[Tuple[float, Set[str]]]:
    """Read link_down events as (time, set of ASes whose attachment changed).

    BGP runs record the events they applied in ``link_events.csv`` with both endpoints, so an
    event matches a path whether the probe pair names the satellite or the IXP side. SCION
    runs write no such file, so their events come from the shared events JSON, where each
    event carries ``args = [isd, as, if_id]`` and the AS is taken from ``args[1]``. Using the
    same trace for both protocols is what makes the resulting numbers comparable.
    """
    path = run_dir / "link_events.csv"
    out: List[Tuple[float, Set[str]]] = []

    if path.exists():
        for row in csv.DictReader(path.open()):
            if (row.get("event") or "").strip() != "link_down":
                continue
            try:
                time_s = float(row["time_s"])
            except (KeyError, ValueError):
                continue
            out.append(
                (time_s, {str(row.get("as_a", "")).strip(), str(row.get("as_b", "")).strip()})
            )
    elif events_json is not None and events_json.exists():
        for event in json.loads(events_json.read_text()).get("events", []):
            if event.get("type") != "link_down":
                continue
            try:
                time_s = float(str(event.get("time", "")).strip().rstrip("s"))
            except ValueError:
                continue
            args = event.get("args", [])
            if len(args) < 2:
                continue
            out.append((time_s, {str(args[1]).strip()}))
    else:
        raise SystemExit(
            f"no link_events.csv in {run_dir} and no --events-json given (SCION runs need it)"
        )

    out.sort(key=lambda item: item[0])
    return out


def relevant_times(
    link_downs: Sequence[Tuple[float, Set[str]]],
    endpoints: Set[str],
) -> List[float]:
    """Times of link_down events that can affect a path between the given endpoints."""
    return [t for t, ases in link_downs if ases & endpoints]


def recovery_for_pair(
    probe_csv: Path,
    src_as: str,
    dst_as: str,
    link_downs: Sequence[Tuple[float, Set[str]]],
    warmup_s: float,
) -> PairRecovery:
    """Compute path-gated recovery outcomes for one probe pair."""
    endpoints = {src_as, dst_as}
    downs = [t for t in relevant_times(link_downs, endpoints) if t >= warmup_s]
    result = PairRecovery(src_as=src_as, dst_as=dst_as, relevant_events=len(downs))
    if not downs:
        return result

    sent_at: Dict[str, float] = {}
    raw: List[Tuple[str, float, str]] = []
    for row in csv.DictReader(probe_csv.open()):
        event = (row.get("event") or "").strip()
        seq = (row.get("seq") or "").strip()
        try:
            time_s = float(row["time_s"])
        except (KeyError, ValueError):
            continue
        if event == "sent":
            sent_at[seq] = time_s
        elif event in ("reply", "timeout"):
            raw.append((seq, time_s, event))

    # Probe timeout, measured from the data rather than assumed, so attribution of an
    # in-flight loss uses this run's actual value.
    gaps = [t - sent_at[s] for s, t, e in raw if e == "timeout" and s in sent_at]
    probe_timeout = statistics.median(gaps) if gaps else 0.0

    # A probe with no sent row was never emitted (SCION does this when its path cache is
    # empty); fall back to the recorded row time.
    probes = sorted(
        ((sent_at.get(s, t), e) for s, t, e in raw), key=lambda item: item[0]
    )
    if not probes:
        return result

    # Maximal runs of consecutive losses, each with the send time of the probe that
    # recovered after it.
    outages: List[Tuple[float, Optional[float]]] = []
    index = 0
    while index < len(probes):
        if probes[index][1] != "timeout":
            index += 1
            continue
        start = probes[index][0]
        while index < len(probes) and probes[index][1] == "timeout":
            index += 1
        recovered_at = probes[index][0] if index < len(probes) else None
        outages.append((start, recovered_at))

    attributed: Dict[int, Tuple[float, Optional[float]]] = {}
    for start, recovered_at in outages:
        # The causing event is the latest relevant one that could still have affected this
        # probe: anything up to its timeout expiry, which covers a loss while in flight.
        cutoff = start + probe_timeout
        candidates = [i for i, t in enumerate(downs) if t <= cutoff]
        if not candidates:
            result.unattributed += 1
            continue
        idx = candidates[-1]
        if idx in attributed:
            continue
        attributed[idx] = (start, recovered_at)

    for idx, t_down in enumerate(downs):
        if idx not in attributed:
            result.no_impact += 1
            continue
        _, recovered_at = attributed[idx]
        if recovered_at is None:
            result.censored += 1
        else:
            result.recovered.append(max(0.0, recovered_at - t_down))

    return result


def analyse(
    run_dir: Path, warmup_s: float, probe_glob: str, events_json: Optional[Path] = None
) -> List[PairRecovery]:
    link_downs = load_link_downs(run_dir, events_json)
    results: List[PairRecovery] = []
    for probe_csv in sorted(run_dir.glob(probe_glob)):
        pair = re.search(r"probe_(\d+)_(\d+)\.csv$", probe_csv.name)
        if pair is None:
            continue
        results.append(
            recovery_for_pair(probe_csv, pair.group(1), pair.group(2), link_downs, warmup_s)
        )
    return results


def report(run_dir: Path, results: Sequence[PairRecovery], total_events: int) -> None:
    print(f"\n{run_dir.name}")
    print(f"  link_down events in run: {total_events}")
    print(
        f"  {'pair':<12} {'relevant':>9} {'recovered':>10} {'no_impact':>10} "
        f"{'censored':>9} {'median':>8} {'p90':>8}"
    )
    all_recovered: List[float] = []
    tot_rel = tot_rec = tot_ni = tot_cen = 0
    for r in results:
        all_recovered.extend(r.recovered)
        tot_rel += r.relevant_events
        tot_rec += len(r.recovered)
        tot_ni += r.no_impact
        tot_cen += r.censored
        med = f"{r.median_s:.1f}s" if r.median_s is not None else "n/a"
        p90 = f"{r.p90_s:.1f}s" if r.p90_s is not None else "n/a"
        print(
            f"  {r.src_as + '-' + r.dst_as:<12} {r.relevant_events:>9} {len(r.recovered):>10} "
            f"{r.no_impact:>10} {r.censored:>9} {med:>8} {p90:>8}"
        )

    if all_recovered:
        ordered = sorted(all_recovered)
        med = statistics.median(ordered)
        p90 = ordered[min(len(ordered) - 1, int(0.9 * len(ordered)))]
        cen_pct = 100.0 * tot_cen / max(1, tot_rel)
        print(
            f"  {'ALL':<12} {tot_rel:>9} {tot_rec:>10} {tot_ni:>10} {tot_cen:>9} "
            f"{med:>7.1f}s {p90:>7.1f}s   censored={cen_pct:.1f}%"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("run_dirs", nargs="+", help="Run directories containing link_events.csv")
    parser.add_argument(
        "--warmup", type=float, default=60.0, help="Ignore events before this time (default: 60)"
    )
    parser.add_argument(
        "--events-json",
        default=None,
        help="Events JSON for runs without link_events.csv (SCION). The AS is args[1].",
    )
    parser.add_argument(
        "--probe-glob",
        default="probe_*.csv",
        help="Probe CSV glob; use 'scion_probe_*.csv' for SCION runs",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    for raw in args.run_dirs:
        run_dir = Path(raw).resolve()
        if not run_dir.is_dir():
            print(f"skipping {run_dir}: not a directory")
            continue
        events_json = Path(args.events_json) if args.events_json else None
        results = analyse(run_dir, args.warmup, args.probe_glob, events_json)
        if not results:
            print(f"skipping {run_dir}: no probe files matching {args.probe_glob}")
            continue
        report(run_dir, results, len(load_link_downs(run_dir, events_json)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
