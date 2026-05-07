#!/usr/bin/env python3
"""Build control-plane-only convergence tables from sweep outputs.

This script reads BGP and SCION sweep run folders and emits:
- a CSV metric table
- a Markdown report containing metric definitions and the table

It also computes a SCION-specific best-path return metric:
mean elapsed time from link recovery to the first snapshot where the pair's
best path returns to its pre-failure baseline path, considering only
pair-event observations that both switched best path after failure and later
returned.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from dataclasses import dataclass
from pathlib import Path
from statistics import mean
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML is required (pip install pyyaml)") from exc


@dataclass
class Window:
    failure_time_s: float
    recovery_time_s: float
    end_time_s: Optional[float]


@dataclass
class Snapshot:
    time_s: float
    src_as: int
    dst_as: int
    best_path: str


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build control-plane-only convergence table from sweep outputs")
    p.add_argument("--build-dir", default="build", help="Sweep build directory")
    p.add_argument("--scenarios", default="A,B,C", help="Comma-separated scenarios")
    p.add_argument("--intervals", default="5,10,20,40,60", help="Comma-separated interval values")
    p.add_argument("--quiet-period-s", type=float, default=5.0, help="Quiet window for quiet-convergence")
    p.add_argument(
        "--pairs",
        default="",
        help="Optional pair filter, comma-separated AS pairs like '101-106,107-108'",
    )
    p.add_argument(
        "--include-keepalive",
        action="store_true",
        help="Include BGP KEEPALIVE rows in BGP control-plane event totals and convergence timing",
    )
    p.add_argument(
        "--out-csv",
        default="build/protocol_comparison/control_plane_only_metrics.csv",
        help="Output CSV path",
    )
    p.add_argument(
        "--out-md",
        default="build/protocol_comparison/control_plane_only_metrics.md",
        help="Output Markdown report path",
    )
    return p.parse_args()


def parse_list(raw: str) -> List[str]:
    return [x.strip() for x in raw.split(",") if x.strip()]


def parse_intervals(raw: str) -> List[int]:
    out: List[int] = []
    for token in parse_list(raw):
        out.append(int(float(token)))
    return out


def parse_pair_filter(raw: str) -> Optional[set[Tuple[int, int]]]:
    if not raw.strip():
        return None
    pairs: set[Tuple[int, int]] = set()
    for token in parse_list(raw):
        norm = token.replace(":", "-").replace("_", "-")
        parts = norm.split("-")
        if len(parts) != 2:
            raise ValueError(f"Invalid pair token: {token}")
        a = int(parts[0])
        b = int(parts[1])
        pair = (a, b) if a < b else (b, a)
        pairs.add(pair)
    return pairs


def parse_duration_seconds(value: object) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        raise ValueError(f"Unsupported duration value: {value!r}")
    m = re.match(r"^\s*([0-9]*\.?[0-9]+)\s*([A-Za-z]+)?\s*$", value)
    if m is None:
        raise ValueError(f"Invalid duration: {value!r}")
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
        raise ValueError(f"Unsupported unit: {unit}")
    return magnitude * factor


def load_events_json_windows(events_json: Path) -> List[Window]:
    payload = json.loads(events_json.read_text())
    events = payload.get("events", [])
    downs: List[Tuple[float, Optional[str]]] = []
    ups: List[Tuple[float, Optional[str]]] = []

    for ev in events:
        ev_type = str(ev.get("type", "")).strip().lower()
        t = parse_duration_seconds(ev.get("time", "0s"))
        args = ev.get("args", [])
        ifid = None
        if isinstance(args, list) and len(args) >= 3:
            ifid = str(args[2]).strip()
        if ev_type == "link_down":
            downs.append((t, ifid))
        elif ev_type == "link_up":
            ups.append((t, ifid))

    downs.sort(key=lambda x: x[0])
    ups.sort(key=lambda x: x[0])

    windows: List[Window] = []
    for i, (tdown, ifid) in enumerate(downs):
        matching_ups = [u for u in ups if u[0] > tdown and (ifid is None or u[1] == ifid)]
        if matching_ups:
            tup = matching_ups[0][0]
        else:
            tup = tdown

        next_down = downs[i + 1][0] if i + 1 < len(downs) else None
        windows.append(Window(failure_time_s=tdown, recovery_time_s=tup, end_time_s=next_down))

    return windows


def load_bgp_windows_from_link_events(link_events_csv: Path) -> List[Window]:
    rows: List[dict] = []
    with link_events_csv.open("r", newline="") as f:
        rows = list(csv.DictReader(f))

    downs: List[Tuple[float, Tuple[int, int]]] = []
    ups: List[Tuple[float, Tuple[int, int]]] = []
    for r in rows:
        kind = (r.get("event", "") or "").strip().lower()
        t = float(r.get("time_s", "0") or 0)
        a = int(r.get("as_a", "0") or 0)
        b = int(r.get("as_b", "0") or 0)
        pair = (a, b) if a < b else (b, a)
        if kind == "link_down":
            downs.append((t, pair))
        elif kind == "link_up":
            ups.append((t, pair))

    downs.sort(key=lambda x: x[0])
    ups.sort(key=lambda x: x[0])

    windows: List[Window] = []
    for i, (tdown, pair) in enumerate(downs):
        matching_ups = [u for u in ups if u[0] > tdown and u[1] == pair]
        tup = matching_ups[0][0] if matching_ups else tdown
        next_down = downs[i + 1][0] if i + 1 < len(downs) else None
        windows.append(Window(failure_time_s=tdown, recovery_time_s=tup, end_time_s=next_down))
    return windows


def in_window(t: float, w: Window) -> bool:
    if t < w.failure_time_s:
        return False
    if w.end_time_s is None:
        return True
    return t < w.end_time_s


def quiet_convergence_delay(event_times: List[float], w: Window, quiet_s: float, fallback_end: float) -> Optional[float]:
    # Earliest time t >= failure such that no events occur in [t, t+quiet_s].
    # Approximation via event gaps.
    if quiet_s <= 0:
        return 0.0

    times = sorted(t for t in event_times if in_window(t, w))
    if not times:
        return 0.0

    prev = w.failure_time_s
    for t in times:
        if t - prev >= quiet_s:
            return max(0.0, prev - w.failure_time_s)
        prev = t

    end_t = w.end_time_s if w.end_time_s is not None else fallback_end
    if end_t - prev >= quiet_s:
        return max(0.0, prev - w.failure_time_s)
    return None


def mean_or_none(vals: Sequence[float]) -> Optional[float]:
    return mean(vals) if vals else None


def load_scion_cp_rows(path: Path) -> List[Tuple[float, str]]:
    out: List[Tuple[float, str]] = []
    with path.open("r", newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            try:
                t = float((row.get("timestamp_s", "") or "").strip())
            except ValueError:
                continue
            et = (row.get("event_type", "") or "").strip().upper()
            out.append((t, et))
    return out


def scion_cp_metrics(cp_rows: List[Tuple[float, str]], windows: List[Window], quiet_s: float, sim_end_s: float) -> Dict[str, Optional[float]]:
    prop_types = {"BEACON", "PS-SEND", "SEG-RX", "CACHE"}
    explore_types = {"PATH-UP", "PATH-DOWN", "PATH-CHANGE", "PS-SEND", "SEG-RX"}

    total = 0
    propagation = 0
    exploration = 0

    detect_delays: List[float] = []
    event_conv_delays: List[float] = []
    quiet_conv_delays: List[float] = []

    all_times = [t for t, _ in cp_rows]

    for w in windows:
        wr = [(t, e) for t, e in cp_rows if in_window(t, w)]
        total += len(wr)
        propagation += sum(1 for _, e in wr if e in prop_types)
        exploration += sum(1 for _, e in wr if e in explore_types)

        if wr:
            detect_delays.append(wr[0][0] - w.failure_time_s)
            event_conv_delays.append(wr[-1][0] - w.failure_time_s)

        q = quiet_convergence_delay([t for t, _ in wr], w, quiet_s, sim_end_s)
        if q is not None:
            quiet_conv_delays.append(q)

    return {
        "cp_total": float(total),
        "cp_propagation": float(propagation),
        "cp_exploration": float(exploration),
        "cp_prop_to_explore_ratio": (float(propagation) / float(exploration)) if exploration > 0 else None,
        "cp_detection_delay_mean_s": mean_or_none(detect_delays),
        "cp_event_convergence_mean_s": mean_or_none(event_conv_delays),
        "cp_quiet_convergence_mean_s": mean_or_none(quiet_conv_delays),
    }


def load_bgp_cp_rows(run_dir: Path, include_keepalive: bool) -> List[Tuple[float, str, str]]:
    out: List[Tuple[float, str, str]] = []
    for p in sorted(run_dir.glob("bgp_cp_as*.csv")):
        with p.open("r", newline="") as f:
            r = csv.DictReader(f)
            for row in r:
                try:
                    t = float((row.get("time_s", "") or "").strip())
                except ValueError:
                    continue
                ev = (row.get("event", "") or "").strip().upper()
                detail = (row.get("detail", "") or "").strip()
                if ev == "KEEPALIVE" and not include_keepalive:
                    continue
                out.append((t, ev, detail))
    out.sort(key=lambda x: x[0])
    return out


def bgp_cp_metrics(cp_rows: List[Tuple[float, str, str]], windows: List[Window], quiet_s: float, sim_end_s: float) -> Dict[str, Optional[float]]:
    total = 0
    propagation = 0
    exploration = 0

    detect_delays: List[float] = []
    event_conv_delays: List[float] = []
    quiet_conv_delays: List[float] = []

    for w in windows:
        wr = [(t, ev, d) for t, ev, d in cp_rows if in_window(t, w)]
        total += len(wr)
        propagation += sum(1 for _, ev, _ in wr if ev == "UPDATE")
        exploration += sum(1 for _, ev, _ in wr if ev == "STATE_CHANGE")

        det = next((t for t, ev, d in wr if ev == "STATE_CHANGE" and d == "ESTABLISHED->IDLE"), None)
        if det is not None:
            detect_delays.append(det - w.failure_time_s)
        elif wr:
            detect_delays.append(wr[0][0] - w.failure_time_s)

        if wr:
            event_conv_delays.append(wr[-1][0] - w.failure_time_s)

        q = quiet_convergence_delay([t for t, _, _ in wr], w, quiet_s, sim_end_s)
        if q is not None:
            quiet_conv_delays.append(q)

    return {
        "cp_total": float(total),
        "cp_propagation": float(propagation),
        "cp_exploration": float(exploration),
        "cp_prop_to_explore_ratio": (float(propagation) / float(exploration)) if exploration > 0 else None,
        "cp_detection_delay_mean_s": mean_or_none(detect_delays),
        "cp_event_convergence_mean_s": mean_or_none(event_conv_delays),
        "cp_quiet_convergence_mean_s": mean_or_none(quiet_conv_delays),
    }


def load_snapshots(path: Path) -> Dict[Tuple[int, int], List[Snapshot]]:
    out: Dict[Tuple[int, int], List[Snapshot]] = {}
    with path.open("r", newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            try:
                t = float(row["time_s"])
                src = int(row["src_as"])
                dst = int(row["dst_as"])
            except (KeyError, ValueError):
                continue
            bp = (row.get("best_path", "") or "").strip()
            key = (src, dst) if src < dst else (dst, src)
            out.setdefault(key, []).append(Snapshot(time_s=t, src_as=src, dst_as=dst, best_path=bp))
    for snaps in out.values():
        snaps.sort(key=lambda s: s.time_s)
    return out


def baseline_before(snaps: Sequence[Snapshot], t: float) -> Optional[Snapshot]:
    cand = None
    for s in snaps:
        if s.time_s < t:
            cand = s
        else:
            break
    return cand


def scion_best_path_return_mean(
    snapshots: Dict[Tuple[int, int], List[Snapshot]],
    windows: Sequence[Window],
    pair_filter: Optional[set[Tuple[int, int]]],
    sim_end_s: float,
) -> Tuple[Optional[float], int]:
    latencies: List[float] = []

    pairs = sorted(snapshots.keys())
    if pair_filter is not None:
        pairs = [p for p in pairs if p in pair_filter]

    for pair in pairs:
        snaps = snapshots[pair]
        for w in windows:
            end_t = w.end_time_s if w.end_time_s is not None else sim_end_s
            base = baseline_before(snaps, w.failure_time_s)
            if base is None or not base.best_path:
                continue

            switched = None
            for s in snaps:
                if s.time_s < w.failure_time_s:
                    continue
                if s.time_s >= end_t:
                    break
                if s.best_path and s.best_path != base.best_path:
                    switched = s
                    break
            if switched is None:
                continue

            returned = None
            for s in snaps:
                if s.time_s < w.recovery_time_s:
                    continue
                if s.time_s >= end_t:
                    break
                if s.best_path == base.best_path:
                    returned = s
                    break
            if returned is None:
                continue

            latencies.append(returned.time_s - w.recovery_time_s)

    return (mean(latencies) if latencies else None, len(latencies))


def fmt(v: Optional[float], d: int = 6) -> str:
    if v is None:
        return ""
    return f"{v:.{d}f}"


def build_markdown(rows: List[dict], out_csv: Path) -> str:
    def cell_float(raw: str) -> str:
        if not raw:
            return "n/a"
        try:
            return f"{float(raw):.6f}"
        except ValueError:
            return "n/a"

    def cell_text(raw: str) -> str:
        return raw if raw else "n/a"

    lines: List[str] = []
    lines.append("# Control-Plane-Only Convergence Metric Table")
    lines.append("")
    lines.append("## Metric Definitions")
    lines.append("")
    lines.append("| Column | Definition |")
    lines.append("|---|---|")
    lines.append("| scenario | Scenario identifier (A/B/C) |")
    lines.append("| interval_s | Sweep interval (PCB=clock) in seconds |")
    lines.append("| protocol | BGP or SCION |")
    lines.append("| cp_total | Total control-plane events/messages in failure windows |")
    lines.append("| cp_propagation | Propagation events (BGP UPDATE; SCION BEACON/PS-SEND/SEG-RX/CACHE) |")
    lines.append("| cp_exploration | Exploration/state-change events (BGP STATE_CHANGE; SCION PATH-*/PS-SEND/SEG-RX) |")
    lines.append("| cp_prop_to_explore_ratio | cp_propagation / cp_exploration |")
    lines.append("| cp_detection_delay_mean_s | Mean time from failure to first control-plane detection event per window |")
    lines.append("| cp_event_convergence_mean_s | Mean time from failure to last control-plane event per window |")
    lines.append("| cp_quiet_convergence_mean_s | Mean time from failure to quiet-period convergence per window |")
    lines.append("| best_path_return_mean_s | SCION-only: mean elapsed time between link restoration and best-path return to the pre-failure baseline path, computed only for pair-event observations that switched best path after failure and later returned |")
    lines.append("| best_path_return_count | Number of qualifying pair-event observations used for best_path_return_mean_s |")
    lines.append("")
    lines.append("## Table")
    lines.append("")
    lines.append(f"CSV source: `{out_csv}`")
    lines.append("")
    lines.append("| scenario | interval_s | protocol | cp_total | cp_propagation | cp_exploration | cp_prop_to_explore_ratio | cp_detection_delay_mean_s | cp_event_convergence_mean_s | cp_quiet_convergence_mean_s | best_path_return_mean_s | best_path_return_count |")
    lines.append("|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    for r in rows:
        lines.append(
            "| "
            + f"{r['scenario']} | {r['interval_s']} | {r['protocol']} | {r['cp_total']} | {r['cp_propagation']} | {r['cp_exploration']} | "
            + f"{cell_float(r['cp_prop_to_explore_ratio'])} | {cell_float(r['cp_detection_delay_mean_s'])} | "
            + f"{cell_float(r['cp_event_convergence_mean_s'])} | {cell_float(r['cp_quiet_convergence_mean_s'])} | "
            + f"{cell_float(r['best_path_return_mean_s'])} | {cell_text(r['best_path_return_count'])} |"
        )
    lines.append("")
    return "\n".join(lines) + "\n"


def main() -> None:
    args = parse_args()
    build_dir = Path(args.build_dir)
    scenarios = [s.upper() for s in parse_list(args.scenarios)]
    intervals = parse_intervals(args.intervals)
    pair_filter = parse_pair_filter(args.pairs)

    out_csv = Path(args.out_csv)
    out_md = Path(args.out_md)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    out_md.parent.mkdir(parents=True, exist_ok=True)

    rows: List[dict] = []

    for scenario in scenarios:
        s_low = scenario.lower()
        for interval in intervals:
            suffix = f"pcb{interval}_clk{interval}"
            scion_dir = build_dir / f"scion_scenario_{s_low}_{suffix}"
            bgp_dir = build_dir / f"bgp_scenario_{s_low}_{suffix}"

            # SCION
            scion_cfg = scion_dir / f"scenario_{s_low}_{suffix}.yaml"
            scion_cp = scion_dir / "scion_control_plane_events.csv"
            scion_snap = scion_dir / "scion_path_snapshots.csv"
            if scion_cfg.exists() and scion_cp.exists() and scion_snap.exists():
                cfg = yaml.safe_load(scion_cfg.read_text()) or {}
                sim_end_s = parse_duration_seconds(cfg.get("simulation_duration", "0s"))
                events_file = cfg.get("events_file", "")
                events_json = (Path(events_file) if Path(events_file).is_absolute() else Path.cwd() / str(events_file))
                if events_json.exists():
                    windows = load_events_json_windows(events_json)
                else:
                    windows = [Window(50.0, 250.0, None)]

                cp_rows = load_scion_cp_rows(scion_cp)
                cp_metrics = scion_cp_metrics(cp_rows, windows, args.quiet_period_s, sim_end_s)

                snaps = load_snapshots(scion_snap)
                best_mean, best_count = scion_best_path_return_mean(snaps, windows, pair_filter, sim_end_s)

                rows.append(
                    {
                        "scenario": scenario,
                        "interval_s": str(interval),
                        "protocol": "SCION",
                        "cp_total": str(int(cp_metrics["cp_total"] or 0)),
                        "cp_propagation": str(int(cp_metrics["cp_propagation"] or 0)),
                        "cp_exploration": str(int(cp_metrics["cp_exploration"] or 0)),
                        "cp_prop_to_explore_ratio": fmt(cp_metrics["cp_prop_to_explore_ratio"]),
                        "cp_detection_delay_mean_s": fmt(cp_metrics["cp_detection_delay_mean_s"]),
                        "cp_event_convergence_mean_s": fmt(cp_metrics["cp_event_convergence_mean_s"]),
                        "cp_quiet_convergence_mean_s": fmt(cp_metrics["cp_quiet_convergence_mean_s"]),
                        "best_path_return_mean_s": fmt(best_mean),
                        "best_path_return_count": str(best_count),
                    }
                )

            # BGP
            bgp_link = bgp_dir / "link_events.csv"
            if bgp_dir.exists() and bgp_link.exists():
                windows = load_bgp_windows_from_link_events(bgp_link)
                # infer a simulation end fallback from latest cp event or latest window endpoint
                cp_rows = load_bgp_cp_rows(bgp_dir, args.include_keepalive)
                latest_cp = cp_rows[-1][0] if cp_rows else 0.0
                latest_window = max((w.end_time_s or w.recovery_time_s for w in windows), default=0.0)
                sim_end_s = max(latest_cp, latest_window)
                cp_metrics = bgp_cp_metrics(cp_rows, windows, args.quiet_period_s, sim_end_s)

                rows.append(
                    {
                        "scenario": scenario,
                        "interval_s": str(interval),
                        "protocol": "BGP",
                        "cp_total": str(int(cp_metrics["cp_total"] or 0)),
                        "cp_propagation": str(int(cp_metrics["cp_propagation"] or 0)),
                        "cp_exploration": str(int(cp_metrics["cp_exploration"] or 0)),
                        "cp_prop_to_explore_ratio": fmt(cp_metrics["cp_prop_to_explore_ratio"]),
                        "cp_detection_delay_mean_s": fmt(cp_metrics["cp_detection_delay_mean_s"]),
                        "cp_event_convergence_mean_s": fmt(cp_metrics["cp_event_convergence_mean_s"]),
                        "cp_quiet_convergence_mean_s": fmt(cp_metrics["cp_quiet_convergence_mean_s"]),
                        "best_path_return_mean_s": "",
                        "best_path_return_count": "",
                    }
                )

    rows.sort(key=lambda r: (r["scenario"], int(r["interval_s"]), r["protocol"]))

    with out_csv.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "scenario",
                "interval_s",
                "protocol",
                "cp_total",
                "cp_propagation",
                "cp_exploration",
                "cp_prop_to_explore_ratio",
                "cp_detection_delay_mean_s",
                "cp_event_convergence_mean_s",
                "cp_quiet_convergence_mean_s",
                "best_path_return_mean_s",
                "best_path_return_count",
            ],
        )
        w.writeheader()
        w.writerows(rows)

    out_md.write_text(build_markdown(rows, out_csv), encoding="utf-8")

    print(out_csv)
    print(out_md)


if __name__ == "__main__":
    main()
