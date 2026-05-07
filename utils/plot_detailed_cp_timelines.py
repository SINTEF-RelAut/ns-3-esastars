#!/usr/bin/env python3
"""Generate detailed control-plane timelines for BGP and SCION.

Creates separate plots for:
1) Single AS view (event types over time)
2) All ASes view (AS over time, colored by event type)
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Set, Tuple

import matplotlib
import yaml

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Plot detailed CP timelines for BGP and SCION")
    p.add_argument("--base-dir", default="build", help="Base build directory")
    p.add_argument("--scenario", default="A", help="Scenario letter (A/B/C)")
    p.add_argument("--interval", type=int, default=20, help="Interval value for pcb/clk suffix")
    p.add_argument("--single-as", type=int, default=101, help="AS number for single-AS plots")
    p.add_argument(
        "--out-dir",
        default="build/protocol_comparison/timeline_plots",
        help="Output directory",
    )
    p.add_argument(
        "--scion-include-path-events",
        action="store_true",
        help="Include PATH-UP/PATH-DOWN/PATH-CHANGE in SCION plots (disabled by default)",
    )
    p.add_argument(
        "--scion-beacon-only",
        action="store_true",
        help="Only plot SCION BEACON events (ignore other SCION control-plane event types)",
    )
    return p.parse_args()


def _bgp_event_type(event: str, detail: str) -> str:
    ev = (event or "").strip().upper()
    dt = (detail or "").strip().lower()
    if ev == "UPDATE":
        if "announce" in dt:
            return "UPDATE_ANNOUNCE"
        if "withdraw" in dt:
            return "UPDATE_WITHDRAW"
        return "UPDATE_OTHER"
    if ev == "STATE_CHANGE":
        return "STATE_CHANGE"
    if ev == "OPEN":
        return "OPEN"
    if ev == "KEEPALIVE":
        return "KEEPALIVE"
    if ev == "NOTIFICATION":
        return "NOTIFICATION"
    return ev if ev else "UNKNOWN"


def load_bgp_events(run_dir: Path) -> List[Tuple[float, int, str]]:
    rows: List[Tuple[float, int, str]] = []
    for p in sorted(run_dir.glob("bgp_cp_as*.csv")):
        with p.open("r", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                try:
                    t = float((row.get("time_s", "") or "").strip())
                    asn = int((row.get("local_asn", "") or "").strip())
                except (TypeError, ValueError):
                    continue
                et = _bgp_event_type(row.get("event", ""), row.get("detail", ""))
                rows.append((t, asn, et))
    return rows


def load_scion_events(run_dir: Path) -> List[Tuple[float, int, str]]:
    rows: List[Tuple[float, int, str]] = []
    path = run_dir / "scion_control_plane_events.csv"
    with path.open("r", newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            try:
                t = float((row.get("timestamp_s", "") or "").strip())
                asn = int((row.get("source_as", "") or "").strip())
            except (TypeError, ValueError):
                continue
            et = (row.get("event_type", "") or "").strip().upper() or "UNKNOWN"
            rows.append((t, asn, et))
    return rows


def _parse_time_seconds(raw: str) -> float:
    s = (raw or "").strip().lower()
    if not s:
        return 0.0
    units = [
        ("ns", 1e-9),
        ("us", 1e-6),
        ("ms", 1e-3),
        ("s", 1.0),
    ]
    for unit, factor in units:
        if s.endswith(unit):
            return float(s[:-len(unit)].strip()) * factor
    return float(s)


def _discover_scion_ases(run_dir: Path, existing_rows: List[Tuple[float, int, str]]) -> Set[int]:
    ases: Set[int] = {a for _, a, _ in existing_rows if a > 0}

    snapshots = run_dir / "scion_path_snapshots.csv"
    if snapshots.exists():
        with snapshots.open("r", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                for key in ("src_as", "dst_as"):
                    try:
                        ases.add(int((row.get(key, "") or "").strip()))
                    except (TypeError, ValueError):
                        continue

    cfg_files = list(run_dir.glob("scenario_*_pcb*_clk*.yaml"))
    if cfg_files:
        with cfg_files[0].open("r") as f:
            cfg = yaml.safe_load(f) or {}
        for pair in (((cfg.get("data_plane_probing") or {}).get("pairs")) or []):
            try:
                ases.add(int(pair.get("src_as")))
                ases.add(int(pair.get("dst_as")))
            except (TypeError, ValueError):
                continue

    return {a for a in ases if a > 0}


def _synthesize_beacon_events(run_dir: Path, existing_rows: List[Tuple[float, int, str]]) -> List[Tuple[float, int, str]]:
    cfg_files = list(run_dir.glob("scenario_*_pcb*_clk*.yaml"))
    if not cfg_files:
        return []

    with cfg_files[0].open("r") as f:
        cfg = yaml.safe_load(f) or {}

    bs = cfg.get("beacon_service") or {}
    period_s = _parse_time_seconds(str(bs.get("period", "")))
    first_s = _parse_time_seconds(str(bs.get("first_beaconing", "0s")))
    last_s = _parse_time_seconds(str(bs.get("last_beaconing", cfg.get("simulation_duration", "0s"))))

    if period_s <= 0 or last_s < first_s:
        return []

    ases = sorted(_discover_scion_ases(run_dir, existing_rows))
    if not ases:
        return []

    beacons: List[Tuple[float, int, str]] = []
    t = first_s
    # Add a tiny epsilon to include the boundary if t lands exactly on last_s.
    while t <= last_s + 1e-12:
        for asn in ases:
            beacons.append((round(t, 9), asn, "BEACON"))
        t += period_s
    return beacons


def prepare_scion_rows(
    run_dir: Path,
    include_path_events: bool,
    beacon_only: bool,
) -> Tuple[List[Tuple[float, int, str]], List[Tuple[float, int, str]]]:
    """Return (protocol_rows, path_state_rows) separately for dual rendering."""
    raw_rows = load_scion_events(run_dir)

    # Separate protocol-message style SCION events from path-state events.
    path_types = {"PATH-UP", "PATH-DOWN", "PATH-CHANGE"}
    protocol_rows = [(t, a, et) for t, a, et in raw_rows if et not in path_types]
    path_state_rows = [(t, a, et) for t, a, et in raw_rows if et in path_types] if include_path_events else []

    has_beacon = any(et == "BEACON" for _, _, et in protocol_rows)
    if not has_beacon:
        protocol_rows.extend(_synthesize_beacon_events(run_dir, raw_rows))

    if beacon_only:
        protocol_rows = [(t, a, et) for t, a, et in protocol_rows if et == "BEACON"]

    protocol_rows.sort(key=lambda x: (x[0], x[1], x[2]))
    path_state_rows.sort(key=lambda x: (x[0], x[1], x[2]))
    return protocol_rows, path_state_rows


def build_color_map(event_types: Iterable[str]) -> Dict[str, str]:
    palette = [
        "#1f77b4",
        "#d62728",
        "#2ca02c",
        "#ff7f0e",
        "#9467bd",
        "#8c564b",
        "#e377c2",
        "#7f7f7f",
        "#bcbd22",
        "#17becf",
    ]
    uniq = sorted(set(event_types))
    return {e: palette[i % len(palette)] for i, e in enumerate(uniq)}


def plot_single_as(
    rows: List[Tuple[float, int, str]],
    path_rows: List[Tuple[float, int, str]],
    asn: int,
    title: str,
    out_path: Path,
) -> bool:
    filt = [(t, et) for t, a, et in rows if a == asn]
    filt_path = [(t, et) for t, a, et in path_rows if a == asn]

    if not filt and not filt_path:
        return False

    all_types = sorted({et for _, et in filt} | {et for _, et in filt_path})
    y_idx = {et: i for i, et in enumerate(all_types)}
    colors = build_color_map(all_types)

    fig, ax = plt.subplots(figsize=(12, 5))

    # Plot path-state events as short vertical line markers on their message rows.
    path_types = {"PATH-UP", "PATH-DOWN", "PATH-CHANGE"}
    for et in (et for et in all_types if et in path_types and any(e == et for _, e in filt_path)):
        times = [t for t, e in filt_path if e == et]
        for t in times:
            y = y_idx[et]
            ax.vlines(t, y - 0.32, y + 0.32, color=colors[et], linewidth=2.5, alpha=0.8, zorder=3)

    # Plot protocol messages as dots.
    for et in (et for et in all_types if et not in path_types):
        ts = [t for t, e in filt if e == et]
        ys = [y_idx[et]] * len(ts)
        ax.scatter(ts, ys, s=20, color=colors[et], alpha=0.85, label=et, zorder=5)

    # Redraw path-state events as lines (after dots) with legend entry.
    for et in (et for et in all_types if et in path_types and any(e == et for _, e in filt_path)):
        ax.plot([], [], color=colors[et], linewidth=2.5, label=et, alpha=0.7)

    ax.set_yticks(list(y_idx.values()))
    ax.set_yticklabels(all_types)
    ax.set_xlabel("Simulation time (s)")
    ax.set_ylabel("Message type")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right", fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)
    return True


def plot_all_as(
    rows: List[Tuple[float, int, str]],
    path_rows: List[Tuple[float, int, str]],
    title: str,
    out_path: Path,
) -> bool:
    if not rows and not path_rows:
        return False

    ases = sorted({a for _, a, _ in rows} | {a for _, a, _ in path_rows})
    all_types = sorted({et for _, _, et in rows} | {et for _, _, et in path_rows})
    colors = build_color_map(all_types)

    fig, ax = plt.subplots(figsize=(12, 6))

    # Plot path-state events as lines.
    path_types = {"PATH-UP", "PATH-DOWN", "PATH-CHANGE"}
    if path_rows:
        for et in (et for et in all_types if et in path_types):
            data = [(t, a) for t, a, e in path_rows if e == et]
            if data:
                times, ys = zip(*data)
                ax.scatter(times, ys, s=0, alpha=0)  # Invisible scatter to track bounds.
                for t, y in data:
                    ax.vlines(t, y - 0.3, y + 0.3, color=colors[et], linewidth=2.5, alpha=0.7)

    # Plot protocol messages as dots.
    for et in (et for et in all_types if et not in path_types or not path_rows):
        data = [(t, a) for t, a, e in rows if e == et]
        if data:
            ts, ys = zip(*data)
            ax.scatter(ts, ys, s=12, color=colors[et], alpha=0.8, label=et, zorder=5)

    # Add legend entries for path-state types even if empty.
    for et in (et for et in all_types if et in path_types):
        ax.plot([], [], color=colors[et], linewidth=2.5, label=et, alpha=0.7)

    ax.set_yticks(ases)
    ax.set_xlabel("Simulation time (s)")
    ax.set_ylabel("AS")
    ax.set_title(title)
    ax.grid(True, alpha=0.3)

    legend_handles = [
        Line2D([0], [0], marker="o", color="w", markerfacecolor=colors[et], markersize=6, label=et)
        if et not in path_types
        else Line2D([0], [0], color=colors[et], linewidth=2.5, label=et, alpha=0.7)
        for et in all_types
    ]
    ax.legend(handles=legend_handles, loc="upper right", fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)
    return True


def main() -> None:
    args = parse_args()
    scenario = args.scenario.upper()
    suffix = f"pcb{args.interval}_clk{args.interval}"

    base = Path(args.base_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    bgp_dir = base / f"bgp_scenario_{scenario.lower()}_{suffix}"
    scion_dir = base / f"scion_scenario_{scenario.lower()}_{suffix}"

    if not bgp_dir.exists():
        raise SystemExit(f"Missing BGP dir: {bgp_dir}")
    if not scion_dir.exists():
        raise SystemExit(f"Missing SCION dir: {scion_dir}")

    bgp_rows = load_bgp_events(bgp_dir)
    bgp_path_rows: List[Tuple[float, int, str]] = []  # BGP has no path-state events in this context.

    scion_rows, scion_path_rows = prepare_scion_rows(
        scion_dir,
        include_path_events=True,  # Always load path events for visualizations.
        beacon_only=args.scion_beacon_only,
    )

    written: List[Path] = []

    p = out_dir / f"bgp_scenario_{scenario.lower()}_{suffix}_as{args.single_as}_timeline.png"
    if plot_single_as(bgp_rows, bgp_path_rows, args.single_as, f"BGP Scenario {scenario} {suffix}: AS{args.single_as} message timeline", p):
        written.append(p)

    p = out_dir / f"bgp_scenario_{scenario.lower()}_{suffix}_all_as_timeline.png"
    if plot_all_as(bgp_rows, bgp_path_rows, f"BGP Scenario {scenario} {suffix}: all-AS message timeline", p):
        written.append(p)

    p = out_dir / f"scion_scenario_{scenario.lower()}_{suffix}_as{args.single_as}_timeline.png"
    if plot_single_as(scion_rows, scion_path_rows, args.single_as, f"SCION Scenario {scenario} {suffix}: AS{args.single_as} message timeline", p):
        written.append(p)

    p = out_dir / f"scion_scenario_{scenario.lower()}_{suffix}_all_as_timeline.png"
    if plot_all_as(scion_rows, scion_path_rows, f"SCION Scenario {scenario} {suffix}: all-AS message timeline", p):
        written.append(p)

    if not written:
        raise SystemExit("No plots generated")

    for path in written:
        print(path)


if __name__ == "__main__":
    main()
