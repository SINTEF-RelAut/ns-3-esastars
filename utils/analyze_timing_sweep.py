#!/usr/bin/env python3
"""Analyse a stochastic timing sweep with the warm-up window excluded.

This is a focused replacement for the loss metrics in ``plot_all_sweep_results.py``,
written because that script has three properties that make its packet-loss numbers
unusable for a BGP-versus-SCION comparison:

* ``MIN_REPLY_RATE`` silently *drops* any probe file whose reply rate is under 1%, so a
  fully blackholed path disappears from the aggregate instead of counting as 100% loss.
* No warm-up is excluded from loss. Probes start at t=10s, but SCION needs roughly
  ``beacon_period + 5s`` to install its first paths, so at beacon_period=40s about 30s of
  convergence is charged as packet loss while BGP, which converges by t=10.2s at every
  clock interval, is charged almost none. The penalty changes at every sweep point, which
  biases the timing curve itself.
* The two protocols use different denominators: BGP ``timeout/sent`` and SCION
  ``timeout/(reply+timeout)``.

Here every metric is computed after the warm-up, with one denominator for both protocols,
and unreachable paths are reported as unreachable rather than filtered away.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

# Share the recovery definition rather than restating it, so the sweep aggregate and the
# per-run tool can never drift apart on what "recovery" means.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_handover_recovery import load_link_downs, recovery_for_pair  # noqa: E402

GROUND_ASES = {"101", "106", "107", "108"}
SATELLITE_ASES = {"102", "103", "104", "105"}

RUN_RE = re.compile(
    r"^stochastic_(?P<protocol>bgp|scion)_(?P<family>\w+?)_(?P<scenario>visible|hidden)_"
    r"(?P<density>dense|sparse)_seed(?P<seed>\d+)_mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_"
    r"probe(?P<probe>\d+)_bcn(?P<bcn>\d+)"
)


@dataclass(frozen=True)
class PairResult:
    """Outcome of one probe pair in one run, after warm-up exclusion."""

    protocol: str
    family: str
    # The virtual-IXP-fabric axis (visible = plain link events, hidden = VirtualIxpFabric).
    # RUN_RE has always captured it but nothing carried it through, so hidden and visible rows
    # were silently pooled in every aggregate and CSV.
    scenario: str
    seed: int
    mrai_s: int
    beacon_s: int
    src_as: str
    dst_as: str
    path_class: str
    status: str  # ok | unreachable | no_data
    reply: int
    timeout: int
    reply_pre_churn: int
    timeout_pre_churn: int
    reply_churn: int
    timeout_churn: int
    first_reply_s: Optional[float]
    rtt_p50_ms: Optional[float]
    # Path-gated recovery: windows are opened and closed only by link events that can
    # affect this pair's path. Gating globally, as the legacy convergence metrics did,
    # censors most windows before the path recovers and biases the survivors fast.
    recovery_n: int
    recovery_no_impact: int
    recovery_censored: int
    recovery_median_s: Optional[float]
    recovery_p90_s: Optional[float]


SPLIT_AS_RE = re.compile(r"^(10[2-5])([01])$")


def normalise_as(asn: str) -> Tuple[str, Optional[str]]:
    """Map a split ASN to its base AS and exchange location; leave any other ASN alone.

    In the split-edge topologies each constellation is two ASes, ``10X0`` homed at exchange A
    and ``10X1`` at exchange B. Without this, every split ASN fails the three-digit membership
    tests below and the whole run collapses into "ground-sat direct".

    Ground ASes deliberately get no location even though they attach to both halves: a location
    label on them would be a fiction, and returning None keeps every pre-split run classifying
    exactly as it did before.
    """
    match = SPLIT_AS_RE.match(asn)
    if match:
        return match.group(1), ("A" if match.group(2) == "0" else "B")
    return asn, None


def classify(src_as: str, dst_as: str) -> str:
    """Label a probe pair by the kind of path it exercises."""
    src_base, src_loc = normalise_as(src_as)
    dst_base, dst_loc = normalise_as(dst_as)

    if src_base in GROUND_ASES and dst_base in GROUND_ASES:
        return "ground-ground via IXP"
    if src_base in SATELLITE_ASES and dst_base in SATELLITE_ASES:
        if src_loc is not None and dst_loc is not None and src_loc != dst_loc:
            # Both halves of one constellation: the two ASes are directly adjacent over the
            # 20ms link between exchange locations, and that link never churns. Kept apart
            # from the diagonal pairs below because the two are not the same measurement —
            # pooling them hides that this path is completely unaffected by handovers and
            # makes the pooled class look like the link itself is unreliable.
            if src_base == dst_base:
                return "sat-sat cross-location, direct"
            # Different constellations at different exchanges: the path crosses the 20ms link
            # AND an exchange, so it is exposed to handovers like any other via-IXP path, but
            # over more AS hops. This is where the path stretch shows up, in latency and in
            # how long re-beaconing takes to rebuild the longer path.
            return "sat-sat cross-location via IXP"
        return "sat-sat via IXP"
    return "ground-sat direct"


def events_stem(match: "re.Match") -> str:
    """Events-JSON filename for a run, which depends on its topology family.

    dual_vis shares the dual trace: its satellite-side interface IDs are the same X0001 and
    X0003 pair. splitsame and splitvis share the split trace, which toggles within each
    exchange location. direct has separate per-protocol traces, so it is not resolvable here.
    """
    family = match.group("family")
    suffix = {
        "dual": "dual",
        "dual_vis": "dual",
        "single": "single",
        "splitsame": "split",
        "splitvis": "split",
    }.get(family)
    if suffix is None:
        return ""
    return (
        f"{match.group('density')}_seed{int(match.group('seed')):02d}_"
        f"mrai{match.group('mrai')}_clk{match.group('clk')}_"
        f"probe{match.group('probe')}_bcn{match.group('bcn')}_{suffix}.json"
    )


def events_json_for(run_dir: Path, generated_events: Path) -> Optional[Path]:
    """Return the shared events JSON for a run, used when it writes no link_events.csv."""
    match = RUN_RE.match(run_dir.name)
    if match is None:
        return None
    stem = events_stem(match)
    if not stem:
        return None
    candidate = generated_events / stem
    return candidate if candidate.exists() else None


def first_link_down(
    run_dir: Path, generated_events: Path, warmup_s: float = 0.0
) -> Optional[float]:
    """Return the first link_down time for a run, or None when it has no churn.

    BGP writes the events it actually applied to ``link_events.csv``; SCION does not, so
    its run falls back to the shared events JSON named by the run's timing point.

    Events before the warm-up are ignored. The BGP scenario takes each satellite's standby
    link down at t=0.1s to establish the initial active/standby state, and that is setup
    rather than churn: counting it would mark the whole run as churned and leave no
    pre-churn window at all.
    """
    link_events = run_dir / "link_events.csv"
    if link_events.exists():
        times = [
            float(row["time_s"])
            for row in csv.DictReader(link_events.open())
            if (row.get("event") or "").strip() == "link_down"
            and float(row["time_s"]) >= warmup_s
        ]
        if times:
            return min(times)

    match = RUN_RE.match(run_dir.name)
    if match is None:
        return None
    stem = events_stem(match)
    if not stem:
        return None
    events_path = generated_events / stem
    if not events_path.exists():
        return None
    data = json.loads(events_path.read_text())
    times = [
        float(str(event["time"]).rstrip("s"))
        for event in data.get("events", [])
        if event.get("type") == "link_down"
        and float(str(event["time"]).rstrip("s")) >= warmup_s
    ]
    return min(times) if times else None


def read_pair(
    probe_csv: Path,
    warmup_s: float,
    churn_start_s: Optional[float],
) -> Tuple[int, int, int, int, int, int, Optional[float], Optional[float]]:
    """Count terminal probe outcomes after warm-up, split by churn phase.

    Returns:
        reply, timeout, reply_pre_churn, timeout_pre_churn, reply_churn, timeout_churn,
        first_reply_s, rtt_p50_ms.
    """
    reply = timeout = 0
    reply_pre = timeout_pre = 0
    reply_churn = timeout_churn = 0
    first_reply: Optional[float] = None
    rtts: List[float] = []

    for row in csv.DictReader(probe_csv.open()):
        event = (row.get("event") or "").strip()
        if event not in ("reply", "timeout"):
            continue
        try:
            time_s = float(row["time_s"])
        except (KeyError, ValueError):
            continue

        if event == "reply" and (first_reply is None or time_s < first_reply):
            first_reply = time_s
        if time_s < warmup_s:
            continue

        in_churn = churn_start_s is not None and time_s >= churn_start_s
        if event == "reply":
            reply += 1
            if in_churn:
                reply_churn += 1
            else:
                reply_pre += 1
            raw = (row.get("rtt_ms") or "").strip()
            if raw:
                try:
                    rtts.append(float(raw))
                except ValueError:
                    pass
        else:
            timeout += 1
            if in_churn:
                timeout_churn += 1
            else:
                timeout_pre += 1

    rtt_p50 = statistics.median(rtts) if rtts else None
    return (
        reply,
        timeout,
        reply_pre,
        timeout_pre,
        reply_churn,
        timeout_churn,
        first_reply,
        rtt_p50,
    )


def collect(sweep_dir: Path, warmup_s: float) -> List[PairResult]:
    """Walk every run directory in the sweep and produce one record per probe pair."""
    generated_events = sweep_dir / "generated_events"
    results: List[PairResult] = []

    for run_dir in sorted(p for p in sweep_dir.iterdir() if p.is_dir()):
        match = RUN_RE.match(run_dir.name)
        if match is None:
            continue

        protocol = match.group("protocol").upper()
        pattern = "scion_probe_*.csv" if protocol == "SCION" else "probe_*.csv"
        probe_files = sorted(run_dir.glob(pattern))
        if not probe_files:
            continue

        churn_start = first_link_down(run_dir, generated_events, warmup_s)
        events_json = events_json_for(run_dir, generated_events)
        try:
            link_downs = load_link_downs(run_dir, events_json)
        except SystemExit:
            link_downs = []

        for probe_csv in probe_files:
            pair = re.search(r"probe_(\d+)_(\d+)\.csv$", probe_csv.name)
            if pair is None:
                continue
            src_as, dst_as = pair.group(1), pair.group(2)
            (
                reply,
                timeout,
                reply_pre,
                timeout_pre,
                reply_churn,
                timeout_churn,
                first_reply,
                rtt_p50,
            ) = read_pair(probe_csv, warmup_s, churn_start)

            rec = (
                recovery_for_pair(probe_csv, src_as, dst_as, link_downs, warmup_s)
                if link_downs
                else None
            )

            if reply + timeout == 0:
                status = "no_data"
            elif reply == 0:
                status = "unreachable"
            else:
                status = "ok"

            results.append(
                PairResult(
                    protocol=protocol,
                    family=match.group("family"),
                    scenario=match.group("scenario"),
                    seed=int(match.group("seed")),
                    mrai_s=int(match.group("mrai")),
                    beacon_s=int(match.group("bcn")),
                    src_as=src_as,
                    dst_as=dst_as,
                    path_class=classify(src_as, dst_as),
                    status=status,
                    reply=reply,
                    timeout=timeout,
                    reply_pre_churn=reply_pre,
                    timeout_pre_churn=timeout_pre,
                    reply_churn=reply_churn,
                    timeout_churn=timeout_churn,
                    first_reply_s=first_reply,
                    rtt_p50_ms=rtt_p50,
                    recovery_n=0 if rec is None else len(rec.recovered),
                    recovery_no_impact=0 if rec is None else rec.no_impact,
                    recovery_censored=0 if rec is None else rec.censored,
                    recovery_median_s=None if rec is None else rec.median_s,
                    recovery_p90_s=None if rec is None else rec.p90_s,
                )
            )

    return results


def loss_pct(reply: int, timeout: int) -> Optional[float]:
    """Loss over terminal outcomes only, the one denominator used for both protocols."""
    total = reply + timeout
    return 100.0 * timeout / total if total else None


def fmt(value: Optional[float], width: int = 8, suffix: str = "%") -> str:
    return f"{'n/a':>{width}}" if value is None else f"{value:>{width}.2f}{suffix}"


def report(results: Sequence[PairResult], warmup_s: float) -> None:
    """Print per-timing-point aggregates, broken out by path class and churn phase."""
    print(f"\nWarm-up excluded: all statistics computed for t >= {warmup_s:.0f}s")
    print("Denominator: reply + timeout (terminal outcomes), identical for both protocols")

    # Grouped on scenario as well as family: the two are orthogonal axes (scenario selects
    # whether the virtual IXP fabric absorbs the link events), and pooling them averages two
    # different experiments into one row.
    classes = sorted({(r.family, r.scenario, r.path_class) for r in results})
    for family, scenario, path_class in classes:
        print(f"\n=== [{family}/{scenario}] {path_class} ===")
        print(
            f"{'mrai/bcn':>9} {'protocol':<7} {'pre-churn':>10} {'churn':>9} {'overall':>9} "
            f"{'unreach':>8} {'pairs':>6} {'rtt p50':>9} {'recov p50':>10} {'recov p90':>10} "
            f"{'no-impact':>10} {'censored':>9}"
        )
        points = sorted(
            {
                (r.mrai_s, r.beacon_s)
                for r in results
                if r.family == family and r.scenario == scenario
            }
        )
        for mrai_s, beacon_s in points:
            for protocol in ("BGP", "SCION"):
                subset = [
                    r
                    for r in results
                    if r.path_class == path_class
                    and r.family == family
                    and r.scenario == scenario
                    and r.mrai_s == mrai_s
                    and r.beacon_s == beacon_s
                    and r.protocol == protocol
                ]
                if not subset:
                    continue
                pre = loss_pct(
                    sum(r.reply_pre_churn for r in subset),
                    sum(r.timeout_pre_churn for r in subset),
                )
                churn = loss_pct(
                    sum(r.reply_churn for r in subset), sum(r.timeout_churn for r in subset)
                )
                overall = loss_pct(
                    sum(r.reply for r in subset), sum(r.timeout for r in subset)
                )
                unreachable = sum(1 for r in subset if r.status == "unreachable")
                rtts = [r.rtt_p50_ms for r in subset if r.rtt_p50_ms is not None]
                rtt = statistics.median(rtts) if rtts else None

                # Pool the per-pair medians rather than the raw samples: each pair
                # contributes once, so a pair with many events cannot dominate the class.
                meds = [r.recovery_median_s for r in subset if r.recovery_median_s is not None]
                p90s = [r.recovery_p90_s for r in subset if r.recovery_p90_s is not None]
                rec_med = statistics.median(meds) if meds else None
                rec_p90 = statistics.median(p90s) if p90s else None
                windows = sum(
                    r.recovery_n + r.recovery_no_impact + r.recovery_censored for r in subset
                )
                ni_pct = (
                    100.0 * sum(r.recovery_no_impact for r in subset) / windows if windows else None
                )
                cen_pct = (
                    100.0 * sum(r.recovery_censored for r in subset) / windows if windows else None
                )
                print(
                    f"{str(mrai_s) + '/' + str(beacon_s):>9} {protocol:<7} {fmt(pre)} "
                    f"{fmt(churn, 8)} {fmt(overall, 8)} {unreachable:>8} {len(subset):>6} "
                    f"{fmt(rtt, 8, '')} {fmt(rec_med, 9, 's')} {fmt(rec_p90, 9, 's')} "
                    f"{fmt(ni_pct, 9)} {fmt(cen_pct, 8)}"
                )


def write_csv(results: Sequence[PairResult], out_path: Path) -> None:
    """Write the per-pair table so nothing is hidden behind an aggregate."""
    with out_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "protocol",
                "family",
                "scenario",
                "seed",
                "mrai_s",
                "beacon_s",
                "src_as",
                "dst_as",
                "path_class",
                "status",
                "reply",
                "timeout",
                "loss_pct",
                "reply_pre_churn",
                "timeout_pre_churn",
                "loss_pct_pre_churn",
                "reply_churn",
                "timeout_churn",
                "loss_pct_churn",
                "first_reply_s",
                "rtt_p50_ms",
                "recovery_windows",
                "recovery_n",
                "recovery_no_impact",
                "recovery_censored",
                "recovery_median_s",
                "recovery_p90_s",
            ]
        )
        for r in results:
            writer.writerow(
                [
                    r.protocol,
                    r.family,
                    r.scenario,
                    r.seed,
                    r.mrai_s,
                    r.beacon_s,
                    r.src_as,
                    r.dst_as,
                    r.path_class,
                    r.status,
                    r.reply,
                    r.timeout,
                    "" if loss_pct(r.reply, r.timeout) is None else f"{loss_pct(r.reply, r.timeout):.6f}",
                    r.reply_pre_churn,
                    r.timeout_pre_churn,
                    ""
                    if loss_pct(r.reply_pre_churn, r.timeout_pre_churn) is None
                    else f"{loss_pct(r.reply_pre_churn, r.timeout_pre_churn):.6f}",
                    r.reply_churn,
                    r.timeout_churn,
                    ""
                    if loss_pct(r.reply_churn, r.timeout_churn) is None
                    else f"{loss_pct(r.reply_churn, r.timeout_churn):.6f}",
                    "" if r.first_reply_s is None else f"{r.first_reply_s:.6f}",
                    "" if r.rtt_p50_ms is None else f"{r.rtt_p50_ms:.6f}",
                    r.recovery_n + r.recovery_no_impact + r.recovery_censored,
                    r.recovery_n,
                    r.recovery_no_impact,
                    r.recovery_censored,
                    "" if r.recovery_median_s is None else f"{r.recovery_median_s:.6f}",
                    "" if r.recovery_p90_s is None else f"{r.recovery_p90_s:.6f}",
                ]
            )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sweep_dir", help="Sweep output directory containing the run directories")
    parser.add_argument(
        "--warmup",
        type=float,
        default=60.0,
        help="Warm-up in seconds to exclude from every statistic (default: 60)",
    )
    parser.add_argument(
        "--out-csv",
        default=None,
        help="Optional path for the per-pair CSV (default: <sweep_dir>/pair_metrics.csv)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    sweep_dir = Path(args.sweep_dir).resolve()
    if not sweep_dir.is_dir():
        raise SystemExit(f"Not a directory: {sweep_dir}")

    results = collect(sweep_dir, args.warmup)
    if not results:
        raise SystemExit(f"No probe data found under {sweep_dir}")

    report(results, args.warmup)
    out_csv = Path(args.out_csv) if args.out_csv else sweep_dir / "pair_metrics.csv"
    write_csv(results, out_csv)
    print(f"\nPer-pair table: {out_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
