#!/usr/bin/env python3
"""Postprocess and plot BGP vs SCION scenario results.

Adds both data-plane and control-plane comparison metrics:
- recovery latency stats (mean/p50/p95/p99/max)
- control-plane total message volume
- propagation messages
- path exploration messages
- propagation-to-exploration ratio

Supports variant-tagged outputs from interval sweeps.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from statistics import fmean
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


DEFAULT_SCENARIOS = ["a", "b", "c"]
PAIR_RE = re.compile(r"(scion_)?probe_(\d+)_(\d+)\.csv$")


@dataclass
class ScenarioMetrics:
    scenario: str
    protocol: str
    count: int
    mean: float
    p50: float
    p95: float
    p99: float
    max_v: float
    cp_total: int
    cp_propagation: int
    cp_exploration: int
    cp_prop_to_explore_ratio: float
    source_file: str
    control_source_file: str


def _float(v: object, default: float = 0.0) -> float:
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _int(v: object, default: int = 0) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        return default


def _ratio(n: int, d: int) -> float:
    return float(n) / float(d) if d > 0 else 0.0


def _percentile(values: List[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]

    rank = (pct / 100.0) * (len(ordered) - 1)
    lo = int(rank)
    hi = min(lo + 1, len(ordered) - 1)
    frac = rank - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def _parse_pair_list(raw: str) -> Set[Tuple[int, int]]:
    pairs: Set[Tuple[int, int]] = set()
    for token in [t.strip() for t in raw.split(",") if t.strip()]:
        norm = token.replace(":", "-").replace("_", "-")
        parts = norm.split("-")
        if len(parts) != 2:
            raise ValueError(f"Invalid pair token '{token}'. Expected like 101-106")
        a = int(parts[0])
        b = int(parts[1])
        if a == b:
            continue
        pair = (a, b) if a < b else (b, a)
        pairs.add(pair)
    return pairs


def _discover_probe_files(base_dir: Path, scenario: str, suffix: str, protocol: str) -> Dict[Tuple[int, int], Path]:
    folders: List[Path]
    if protocol == "BGP":
        folders = [
            base_dir / f"bgp_ixp_dual_ixp_{scenario}{suffix}_dual_ixp_{scenario}",
            base_dir / f"bgp_scenario_{scenario}{suffix}",
        ]
        pattern = "probe_*_*.csv"
    else:
        folders = [
            base_dir / f"dual_sat_{scenario}",
            base_dir / f"scion_scenario_{scenario}{suffix}",
        ]
        pattern = "scion_probe_*_*.csv"

    out: Dict[Tuple[int, int], Path] = {}
    folder = next((f for f in folders if f.exists()), None)
    if folder is None:
        return out

    for p in sorted(folder.glob(pattern)):
        m = PAIR_RE.search(p.name)
        if not m:
            continue
        a = int(m.group(2))
        b = int(m.group(3))
        pair = (a, b) if a < b else (b, a)
        out[pair] = p
    return out


def _discover_first_failure_time(base_dir: Path, scenario: str, suffix: str, default: float = 50.0) -> float:
    candidates = [
        base_dir / f"bgp_ixp_dual_ixp_{scenario}{suffix}_dual_ixp_{scenario}" / "link_events.csv",
        base_dir / f"bgp_scenario_{scenario}{suffix}" / "link_events.csv",
    ]
    events = next((p for p in candidates if p.exists()), None)
    if events is None:
        return default

    with open(events, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if (row.get("event", "") or "").strip().lower() == "link_down":
                return _float(row.get("time_s", default), default)

    return default


def _pair_recovery_latency_from_probe_csv(path: Path, failure_time: float) -> Optional[float]:
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ts = _float(row.get("time_s", 0.0), 0.0)
            if ts < failure_time:
                continue
            event = (row.get("event", "") or row.get("status", "")).strip().lower()
            if event == "reply":
                return ts - failure_time
    return None


def _stats_from_latencies(latencies: List[float]) -> Dict[str, float]:
    if not latencies:
        return {"count": 0, "mean": 0.0, "p50": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0}
    return {
        "count": len(latencies),
        "mean": fmean(latencies),
        "p50": _percentile(latencies, 50.0),
        "p95": _percentile(latencies, 95.0),
        "p99": _percentile(latencies, 99.0),
        "max": max(latencies),
    }


def parse_scion_cp_summary(path: Path) -> Dict[str, int]:
    """Parse the compact CP summary CSV written by scion.cc (total_beacons_sent,propagation,exploration)."""
    if not path.exists():
        return {"total": 0, "propagation": 0, "exploration": 0}
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            return {
                "total": _int(row.get("total_beacons_sent", 0)),
                "propagation": _int(row.get("propagation", 0)),
                "exploration": _int(row.get("exploration", 0)),
            }
    return {"total": 0, "propagation": 0, "exploration": 0}


def parse_scion_control_events(path: Path) -> Dict[str, int]:
    total = 0
    beacon = 0
    signaling = 0
    exploration = 0

    if not path.exists():
        return {"total": 0, "propagation": 0, "exploration": 0}

    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            total += 1
            ev = (row.get("event_type", "") or "").strip().upper()
            if "BEACON" in ev:
                beacon += 1
            if ev in {"PS-SEND", "SEG-RX", "CACHE", "BEACON"}:
                signaling += 1
            if ev in {"PATH-UP", "PATH-DOWN", "PATH-CHANGE", "PS-SEND", "SEG-RX"}:
                exploration += 1

    # In snapshot-derived control logs, explicit BEACON rows may be unavailable.
    propagation = beacon if beacon > 0 else signaling
    if propagation == 0 and total > 0:
        propagation = total
    return {"total": total, "propagation": propagation, "exploration": exploration}


def parse_bgp_control_summary(path: Path) -> Dict[str, int]:
    if not path.exists():
        return {"total": 0, "propagation": 0, "exploration": 0}

    total = 0
    propagation = 0
    exploration = 0

    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            open_c = _int(row.get("OPEN", 0))
            keepalive = _int(row.get("KEEPALIVE", 0))
            upd_ann = _int(row.get("UPDATE_announce", 0))
            upd_wd = _int(row.get("UPDATE_withdraw", 0))
            upd_other = _int(row.get("UPDATE_other", 0))
            notif = _int(row.get("NOTIFICATION", 0))
            state = _int(row.get("STATE_CHANGE", 0))

            total += open_c + keepalive + upd_ann + upd_wd + upd_other + notif + state
            propagation += upd_ann + upd_wd + upd_other
            exploration += state

    return {"total": total, "propagation": propagation, "exploration": exploration}


def discover_scion_json(base_dir: Path, scenario: str, suffix: str) -> Optional[Path]:
    candidates = [
        base_dir / f"scion_scenario_{scenario}{suffix}" / "scion_data_plane_metrics.json",
        base_dir / f"scenario_{scenario}_data_plane_metrics{suffix}.json",
        base_dir / f"scenario_{scenario}{suffix}" / "data_plane_metrics.json",
        base_dir / f"scion_scenario_{scenario}{suffix}" / "data_plane_metrics.json",
    ]

    for c in candidates:
        if c.exists():
            return c

    shared = base_dir / "data_plane_metrics.json"
    if shared.exists() and suffix == "":
        return shared

    return None


def discover_scion_control_csv(base_dir: Path, scenario: str, suffix: str) -> Optional[Path]:
    candidates = [
        base_dir / f"scion_scenario_{scenario}{suffix}" / "scion_control_plane_events.csv",
        base_dir / f"scenario_{scenario}_control_plane_events{suffix}.csv",
        base_dir / f"scenario_{scenario}_control_plane_events.csv",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def discover_scion_cp_summary_csv(base_dir: Path, scenario: str, suffix: str) -> Optional[Path]:
    """Find the compact CP beacon-count summary written directly by scion.cc."""
    # Try the path encoded in the generated config name
    candidates = [
        base_dir / f"scenario_ixp_as110_dual_sat_{scenario.lower()}_cp_summary.csv",
        base_dir / f"scenario_ixp_as110_dual_sat_{scenario.lower()}{suffix}_cp_summary.csv",
        base_dir / f"scion_scenario_{scenario.lower()}{suffix}_cp_summary.csv",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def discover_bgp_json(base_dir: Path, scenario: str, suffix: str) -> Optional[Path]:
    candidates = [
        base_dir / f"bgp_ixp_dual_ixp_{scenario}{suffix}_dual_ixp_{scenario}" / "bgp_data_plane_metrics.json",
        base_dir / f"bgp_scenario_{scenario}{suffix}" / "bgp_data_plane_metrics.json",
    ]
    for p in candidates:
        if p.exists():
            return p
    return None


def discover_bgp_cp_summary(base_dir: Path, scenario: str, suffix: str) -> Optional[Path]:
    candidates = [
        base_dir / f"bgp_ixp_dual_ixp_{scenario}{suffix}_dual_ixp_{scenario}" / "cp_summary.csv",
        base_dir / f"bgp_scenario_{scenario}{suffix}" / "cp_summary.csv",
    ]
    for p in candidates:
        if p.exists():
            return p
    return None


def read_metrics_json(path: Path, scenario: str, protocol: str,
                      cp_total: int, cp_prop: int, cp_explore: int,
                      cp_src: str) -> Optional[ScenarioMetrics]:
    if not path.exists():
        return None

    with open(path) as f:
        doc = json.load(f)

    stats = doc.get("scenario_stats", {})
    return ScenarioMetrics(
        scenario=scenario.upper(),
        protocol=protocol,
        count=int(stats.get("count", 0) or 0),
        mean=_float(stats.get("mean", 0.0)),
        p50=_float(stats.get("p50", 0.0)),
        p95=_float(stats.get("p95", 0.0)),
        p99=_float(stats.get("p99", 0.0)),
        max_v=_float(stats.get("max", 0.0)),
        cp_total=cp_total,
        cp_propagation=cp_prop,
        cp_exploration=cp_explore,
        cp_prop_to_explore_ratio=_ratio(cp_prop, cp_explore),
        source_file=str(path),
        control_source_file=cp_src,
    )


def _metrics_from_probe_pairs(
    scenario: str,
    protocol: str,
    pair_to_probe: Dict[Tuple[int, int], Path],
    selected_pairs: Set[Tuple[int, int]],
    failure_time: float,
    cp: Dict[str, int],
    cp_src: str,
) -> ScenarioMetrics:
    latencies: List[float] = []
    for pair in sorted(selected_pairs):
        probe_path = pair_to_probe.get(pair)
        if not probe_path:
            continue
        latency = _pair_recovery_latency_from_probe_csv(probe_path, failure_time)
        if latency is not None:
            latencies.append(latency)

    stats = _stats_from_latencies(latencies)
    pair_str = ",".join(f"{a}-{b}" for (a, b) in sorted(selected_pairs))
    src = f"probe_pairs={pair_str};failure_t={failure_time:.3f}"
    return ScenarioMetrics(
        scenario=scenario.upper(),
        protocol=protocol,
        count=int(stats["count"]),
        mean=_float(stats["mean"]),
        p50=_float(stats["p50"]),
        p95=_float(stats["p95"]),
        p99=_float(stats["p99"]),
        max_v=_float(stats["max"]),
        cp_total=cp["total"],
        cp_propagation=cp["propagation"],
        cp_exploration=cp["exploration"],
        cp_prop_to_explore_ratio=_ratio(cp["propagation"], cp["exploration"]),
        source_file=src,
        control_source_file=cp_src,
    )


def collect_metrics(
    build_dir: Path,
    scenarios: List[str],
    variant_suffix: str,
    data_plane_mode: str,
    affected_pairs: Optional[Set[Tuple[int, int]]],
) -> List[ScenarioMetrics]:
    rows: List[ScenarioMetrics] = []

    for s in scenarios:
        bgp_cp = discover_bgp_cp_summary(build_dir, s, variant_suffix)
        scion_cp = discover_scion_control_csv(build_dir, s, variant_suffix)
        bgp_cp_metrics = parse_bgp_control_summary(bgp_cp) if bgp_cp else {"total": 0, "propagation": 0, "exploration": 0}
        scion_cp_metrics = parse_scion_control_events(scion_cp) if scion_cp else {"total": 0, "propagation": 0, "exploration": 0}
        # Prefer the compact CP summary (direct beacon counts from scion.cc) when available
        scion_cp_summary = discover_scion_cp_summary_csv(build_dir, s, variant_suffix)
        if scion_cp_summary:
            scion_cp_metrics = parse_scion_cp_summary(scion_cp_summary)
            scion_cp = scion_cp_summary

        if data_plane_mode == "probes":
            bgp_probes = _discover_probe_files(build_dir, s, variant_suffix, "BGP")
            scion_probes = _discover_probe_files(build_dir, s, variant_suffix, "SCION")

            common_pairs = set(bgp_probes.keys()) & set(scion_probes.keys())
            selected_pairs = common_pairs
            if affected_pairs is not None:
                selected_pairs = selected_pairs & affected_pairs

            failure_time = _discover_first_failure_time(build_dir, s, variant_suffix)

            rows.append(_metrics_from_probe_pairs(
                s,
                "BGP",
                bgp_probes,
                selected_pairs,
                failure_time,
                bgp_cp_metrics,
                str(bgp_cp) if bgp_cp else "",
            ))
            rows.append(_metrics_from_probe_pairs(
                s,
                "SCION",
                scion_probes,
                selected_pairs,
                failure_time,
                scion_cp_metrics,
                str(scion_cp) if scion_cp else "",
            ))
        else:
            bgp_json = discover_bgp_json(build_dir, s, variant_suffix)
            if bgp_json:
                m = read_metrics_json(
                    bgp_json,
                    s,
                    "BGP",
                    bgp_cp_metrics["total"],
                    bgp_cp_metrics["propagation"],
                    bgp_cp_metrics["exploration"],
                    str(bgp_cp) if bgp_cp else "",
                )
                if m:
                    rows.append(m)

            scion_json = discover_scion_json(build_dir, s, variant_suffix)
            if scion_json:
                m = read_metrics_json(
                    scion_json,
                    s,
                    "SCION",
                    scion_cp_metrics["total"],
                    scion_cp_metrics["propagation"],
                    scion_cp_metrics["exploration"],
                    str(scion_cp) if scion_cp else "",
                )
                if m:
                    rows.append(m)

    return rows


def write_combined_csv(rows: List[ScenarioMetrics], out_csv: Path) -> None:
    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "scenario", "protocol", "count", "mean_s", "p50_s", "p95_s", "p99_s", "max_s",
            "cp_total", "cp_propagation", "cp_exploration", "cp_prop_to_explore_ratio",
            "source_file", "control_source_file",
        ])
        for r in rows:
            w.writerow([
                r.scenario, r.protocol, r.count,
                f"{r.mean:.6f}", f"{r.p50:.6f}", f"{r.p95:.6f}", f"{r.p99:.6f}", f"{r.max_v:.6f}",
                r.cp_total, r.cp_propagation, r.cp_exploration, f"{r.cp_prop_to_explore_ratio:.6f}",
                r.source_file, r.control_source_file,
            ])


def write_markdown_summary(rows: List[ScenarioMetrics], out_md: Path) -> None:
    by_key: Dict[tuple, ScenarioMetrics] = {(r.scenario, r.protocol): r for r in rows}

    with open(out_md, "w") as f:
        f.write("# BGP vs SCION Postprocessed Comparison\n\n")
        f.write("## Data Plane\n\n")
        f.write("| Scenario | Metric | BGP (s) | SCION (s) | Delta (BGP-SCION) |\n")
        f.write("|---|---:|---:|---:|---:|\n")

        scenarios = sorted(set(r.scenario for r in rows))
        for s in scenarios:
            b = by_key.get((s, "BGP"))
            c = by_key.get((s, "SCION"))
            if not b or not c:
                continue
            for metric in ["mean", "p50", "p95", "p99", "max_v"]:
                bval = getattr(b, metric)
                cval = getattr(c, metric)
                name = metric.replace("_v", "").upper()
                f.write(f"| {s} | {name} | {bval:.6f} | {cval:.6f} | {bval - cval:.6f} |\n")

        f.write("\n## Control Plane Volumes\n\n")
        f.write("| Scenario | Metric | BGP | SCION | Delta (BGP-SCION) |\n")
        f.write("|---|---:|---:|---:|---:|\n")
        for s in scenarios:
            b = by_key.get((s, "BGP"))
            c = by_key.get((s, "SCION"))
            if not b or not c:
                continue
            pairs = [
                ("CP_TOTAL", b.cp_total, c.cp_total),
                ("CP_PROPAGATION", b.cp_propagation, c.cp_propagation),
                ("CP_EXPLORATION", b.cp_exploration, c.cp_exploration),
                ("CP_PROP/EXP_RATIO", b.cp_prop_to_explore_ratio, c.cp_prop_to_explore_ratio),
            ]
            for label, bv, cv in pairs:
                if isinstance(bv, float):
                    f.write(f"| {s} | {label} | {bv:.6f} | {cv:.6f} | {bv - cv:.6f} |\n")
                else:
                    f.write(f"| {s} | {label} | {bv} | {cv} | {bv - cv} |\n")

        f.write("\n## Sources\n\n")
        for r in rows:
            f.write(f"- {r.protocol} scenario {r.scenario}: data={r.source_file} control={r.control_source_file}\n")


def plot_metric(rows: List[ScenarioMetrics], metric: str, ylabel: str, out_png: Path) -> None:
    scenarios = sorted(set(r.scenario for r in rows))
    by_key = {(r.scenario, r.protocol): r for r in rows}

    bgp_vals = [getattr(by_key[(s, "BGP")], metric) if (s, "BGP") in by_key else 0.0 for s in scenarios]
    scion_vals = [getattr(by_key[(s, "SCION")], metric) if (s, "SCION") in by_key else 0.0 for s in scenarios]

    x = list(range(len(scenarios)))
    w = 0.36

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar([i - w / 2 for i in x], bgp_vals, width=w, label="BGP", color="#d62728")
    ax.bar([i + w / 2 for i in x], scion_vals, width=w, label="SCION", color="#1f77b4")
    ax.set_xticks(x)
    ax.set_xticklabels(scenarios)
    ax.set_xlabel("Scenario")
    ax.set_ylabel(ylabel)
    ax.set_title(f"BGP vs SCION {ylabel} by Scenario")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_png, dpi=160)
    plt.close(fig)


def main() -> None:
    p = argparse.ArgumentParser(description="Postprocess and plot BGP+SCION results together")
    p.add_argument("--build-dir", default="build", help="Directory containing scenario outputs")
    p.add_argument("--out-dir", default="build/protocol_comparison",
                   help="Output directory for combined tables and plots")
    p.add_argument("--scenarios", default="a,b,c", help="Comma-separated scenarios")
    p.add_argument("--variant-suffix", default="",
                   help="Optional variant suffix used in folder names, e.g. pcb10_clk1")
    p.add_argument(
        "--data-plane-mode",
        choices=["json", "probes"],
        default="probes",
        help="Use scenario JSON summaries or recompute from probe CSVs",
    )
    p.add_argument(
        "--affected-pairs",
        default="",
        help="Comma-separated AS pairs to include (e.g. 101-106,107-108). If empty, uses all common probe pairs.",
    )
    args = p.parse_args()

    build_dir = Path(args.build_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    suffix = f"_{args.variant_suffix}" if args.variant_suffix else ""
    scenarios = [s.strip().lower() for s in args.scenarios.split(",") if s.strip()]
    if not scenarios:
        scenarios = DEFAULT_SCENARIOS

    affected_pairs = _parse_pair_list(args.affected_pairs) if args.affected_pairs else None
    rows = collect_metrics(build_dir, scenarios, suffix, args.data_plane_mode, affected_pairs)
    if not rows:
        raise SystemExit("No BGP/SCION metrics JSON files found.")

    out_csv = out_dir / "combined_metrics.csv"
    out_md = out_dir / "comparison_summary.md"
    write_combined_csv(rows, out_csv)
    write_markdown_summary(rows, out_md)

    plot_metric(rows, "p50", "Recovery p50 (s)", out_dir / "recovery_p50_comparison.png")
    plot_metric(rows, "p99", "Recovery p99 (s)", out_dir / "recovery_p99_comparison.png")
    plot_metric(rows, "mean", "Recovery mean (s)", out_dir / "recovery_mean_comparison.png")
    plot_metric(rows, "cp_total", "Control-plane total messages", out_dir / "cp_total_comparison.png")
    plot_metric(
        rows,
        "cp_prop_to_explore_ratio",
        "Propagation/Exploration ratio",
        out_dir / "cp_prop_to_explore_ratio.png",
    )

    print(f"Wrote: {out_csv}")
    print(f"Wrote: {out_md}")
    print(f"Wrote: {out_dir / 'recovery_p50_comparison.png'}")
    print(f"Wrote: {out_dir / 'recovery_p99_comparison.png'}")
    print(f"Wrote: {out_dir / 'recovery_mean_comparison.png'}")
    print(f"Wrote: {out_dir / 'cp_total_comparison.png'}")
    print(f"Wrote: {out_dir / 'cp_prop_to_explore_ratio.png'}")


if __name__ == "__main__":
    main()
