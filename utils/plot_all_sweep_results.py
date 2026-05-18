#!/usr/bin/env python3
"""Analyze sweep run directories and generate SCION/BGP comparison plots.

This script scans the build tree for sweep run directories created by the existing
multi-event sweep runners, computes per-run metrics from the raw CSV outputs, and
aggregates those runs into summary statistics per sweep point.

It is designed to work with the current directory layout in this repository:

  build/sweep_bgp_<mode>_<scenario>_mrai<M>_clk<C>_bcn<B>_<trace>/
  build/sweep_scion_<mode>_<scenario>_mrai<M>_clk<C>_bcn<B>_<trace>/

The script writes:
  - a per-run CSV with raw metric values
  - a per-group CSV with count/mean/p50/p95/p99/min/max/stdev
    - PNG line plots with SCION and BGP series on the same axes
    - optional PNG box plots showing per-series run distributions

The default plot uses the mean line and a translucent p50-to-p95 band for each
series. That gives a compact view of both the central tendency and the spread of
repeated runs for the same sweep point.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
import statistics
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import DefaultDict, Dict, List, Optional, Sequence, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


STARTUP_CUTOFF_S = 10.0
MIN_REPLY_RATE = 0.01

METRICS: Dict[str, str] = {
    "convergence_time_s": "Mean convergence time (s)",
    "cp_overhead": "Control-plane overhead (messages/s)",
    "packet_loss_percent": "Packet loss (%)",
    "rtt_mean_ms": "RTT mean (ms)",
    "rtt_p95_ms": "RTT p95 (ms)",
    "rtt_p99_ms": "RTT p99 (ms)",
}

PROTOCOL_COLORS = {
    "BGP": "#d62728",
    "SCION": "#1f77b4",
}

MODE_STYLES = {
    "baseline": {"marker": "o", "linestyle": "-"},
    "split": {"marker": "s", "linestyle": "--"},
    "direct": {"marker": "^", "linestyle": "-."},
    "dual": {"marker": "D", "linestyle": "-"},
    "single": {"marker": "v", "linestyle": "--"},
}

DENSITY_STYLES = {
    "dense": {"linestyle": "-", "marker": "o"},
    "sparse": {"linestyle": "--", "marker": "s"},
    "na": {"linestyle": "-", "marker": "o"},
}

BGP_RE = re.compile(
    r"^sweep_bgp_"
    r"(?:(?P<family>dual|single)_)?(?P<mode>baseline|preferred|split|direct)_"
    r"(?P<scenario>visible|hidden)_"
    r"mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_bcn(?P<bcn>\d+)"
    r"(?:_(?P<trace>.+))?$"
)

SCION_RE = re.compile(
    r"^sweep_scion_"
    r"(?P<mode>dual|single|direct)_"
    r"(?P<scenario>visible|hidden)_"
    r"mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_bcn(?P<bcn>\d+)"
    r"(?:_(?P<trace>.+))?$"
)

STOCH_BGP_RE = re.compile(
    r"^stochastic_bgp_"
    r"(?P<mode>dual|single|direct)_"
    r"(?P<scenario>visible|hidden)_"
    r"(?P<density>dense|sparse)_seed(?P<seed>\d+)"
    r"(?:_mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_probe(?P<probe>\d+)_bcn(?P<bcn>\d+))?"
    r"(?:_.+)?$"
)

STOCH_SCION_RE = re.compile(
    r"^stochastic_scion_"
    r"(?P<mode>dual|single|direct)_"
    r"(?P<scenario>visible|hidden)_"
    r"(?P<density>dense|sparse)_seed(?P<seed>\d+)"
    r"(?:_mrai(?P<mrai>\d+)_clk(?P<clk>\d+)_probe(?P<probe>\d+)_bcn(?P<bcn>\d+))?"
    r"(?:_.+)?$"
)


@dataclass(frozen=True)
class RunSample:
    """Per-run metrics derived from a single sweep output directory or flat file."""

    run_dir: Path  # Path or RunRef
    protocol: str
    mode: str
    scenario: str
    density: str
    mrai_s: float
    clk_s: float
    bcn_s: float
    trace_key: str
    convergence_time_s: Optional[float]
    cp_overhead: Optional[float]
    cp_overhead_source: str
    packet_loss_percent: Optional[float]
    rtt_mean_ms: Optional[float]
    rtt_p95_ms: Optional[float]
    rtt_p99_ms: Optional[float]


@dataclass(frozen=True)
class GroupStats:
    """Aggregated statistics for one protocol/mode/sweep point."""

    scenario: str
    protocol: str
    mode: str
    density: str
    x_axis: str
    x_value: float
    metric: str
    count: int
    mean: float
    p50: float
    p95: float
    p99: float
    min_v: float
    max_v: float
    stdev: float


@dataclass(frozen=True)
class ScionRunConfig:
    """Small subset of SCION config fields needed for retrospective analysis."""

    topology: Optional[Path]
    cp_summary_output: Optional[Path]
    simulation_duration_s: Optional[float]
    beacon_period_s: Optional[float]
    first_beaconing_s: float
    last_beaconing_s: Optional[float]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze sweep run directories and plot SCION/BGP comparisons."
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Build directory containing sweep run outputs (default: build)",
    )
    parser.add_argument(
        "--out-dir",
        default="build/sweep_analysis",
        help="Output directory for CSV summaries and plots (default: build/sweep_analysis)",
    )
    parser.add_argument(
        "--scenarios",
        default="visible,hidden",
        help="Comma-separated scenario filter (default: visible,hidden)",
    )
    parser.add_argument(
        "--protocols",
        default="bgp,scion",
        help="Comma-separated protocol filter (default: bgp,scion)",
    )
    parser.add_argument(
        "--modes",
        default="baseline,split,direct,dual,single",
        help="Comma-separated mode filter (default: baseline,split,direct,dual,single)",
    )
    parser.add_argument(
        "--x-axis",
        choices=["mrai", "clk", "bcn"],
        default="mrai",
        help="Which timer value to place on the x-axis (default: mrai)",
    )
    parser.add_argument(
        "--metrics",
        default="convergence_time_s,cp_overhead,packet_loss_percent",
        help="Comma-separated metrics to plot (default: convergence_time_s,cp_overhead,packet_loss_percent)",
    )
    parser.add_argument(
        "--plot-stat",
        choices=["mean", "p50", "p95", "p99"],
        default="mean",
        help="Which aggregate statistic to draw as the main line (default: mean)",
    )
    parser.add_argument(
        "--no-band",
        action="store_true",
        help="Disable the translucent p50-to-p95 band in plots",
    )
    parser.add_argument(
        "--plot-type",
        choices=["line", "box", "errorbar", "violin", "all"],
        default="all",
        help="Plot type to generate (default: all)",
    )
    parser.add_argument(
        "--max-runs",
        type=int,
        default=0,
        help="Limit the number of discovered run directories processed (0 = all)",
    )
    parser.add_argument(
        "--mrai-min",
        type=float,
        default=1.0,
        help="Minimum MRAI value to include (default: 1)",
    )
    parser.add_argument(
        "--mrai-max",
        type=float,
        default=10.0,
        help="Maximum MRAI value to include (default: 10)",
    )
    return parser.parse_args()


def parse_csv_rows(path: Path) -> List[Dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def percentile(values: Sequence[float], pct: float) -> float:
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


def sanitize_filename(value: str) -> str:
    allowed = []
    for ch in value:
        if ch.isalnum() or ch in "._-":
            allowed.append(ch)
        else:
            allowed.append("_")
    return "".join(allowed)


def parse_duration_seconds(raw_value: str) -> Optional[float]:
    value = raw_value.strip()
    if not value:
        return None
    for suffix in ("ms", "s"):
        if value.endswith(suffix):
            value = value[: -len(suffix)].strip()
            break
    try:
        return float(value)
    except ValueError:
        return None


def resolve_scion_config_path(run_name: str, build_dir: Path, repo_root: Path) -> Optional[Path]:
    candidates = [
        build_dir / "configs" / f"{run_name}.yaml",
        repo_root / "configs" / f"{run_name}.yaml",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def resolve_events_path(events_rel: Path, config_path: Path, build_dir: Path, repo_root: Path) -> Optional[Path]:
    if events_rel.is_absolute() and events_rel.exists():
        return events_rel

    candidates = [
        repo_root / events_rel,
        config_path.parent / events_rel,
        build_dir / events_rel,
        build_dir / "generated_events" / events_rel.name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def load_scion_run_config(run_dir: Path, repo_root: Path) -> Optional[ScionRunConfig]:
    build_dir = run_dir.parent if hasattr(run_dir, "parent") else run_dir.parent
    config_path = resolve_scion_config_path(run_dir.name, build_dir, repo_root)
    if config_path is None:
        return None
    if not config_path.exists():
        return None

    topology_rel: Optional[Path] = None
    cp_summary_rel: Optional[Path] = None
    simulation_duration_s: Optional[float] = None
    beacon_period_s: Optional[float] = None
    first_beaconing_s = 0.0
    last_beaconing_s: Optional[float] = None
    in_beacon_service = False

    for line in config_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        if stripped == "beacon_service:":
            in_beacon_service = True
            continue

        if not line.startswith("  ") and stripped.endswith(":"):
            in_beacon_service = False

        if stripped.startswith("topology:"):
            topology_rel = Path(stripped.split(":", 1)[1].strip())
        elif stripped.startswith("cp_summary_output:"):
            cp_summary_rel = Path(stripped.split(":", 1)[1].strip())
        elif stripped.startswith("simulation_duration:"):
            simulation_duration_s = parse_duration_seconds(stripped.split(":", 1)[1])
        elif in_beacon_service and stripped.startswith("period:"):
            beacon_period_s = parse_duration_seconds(stripped.split(":", 1)[1])
        elif in_beacon_service and stripped.startswith("first_beaconing:"):
            parsed = parse_duration_seconds(stripped.split(":", 1)[1])
            if parsed is not None:
                first_beaconing_s = parsed
        elif in_beacon_service and stripped.startswith("last_beaconing:"):
            last_beaconing_s = parse_duration_seconds(stripped.split(":", 1)[1])

    if last_beaconing_s is None:
        last_beaconing_s = simulation_duration_s

    return ScionRunConfig(
        topology=None if topology_rel is None else repo_root / topology_rel,
        cp_summary_output=None if cp_summary_rel is None else repo_root / cp_summary_rel,
        simulation_duration_s=simulation_duration_s,
        beacon_period_s=beacon_period_s,
        first_beaconing_s=first_beaconing_s,
        last_beaconing_s=last_beaconing_s,
    )


def load_scion_cp_summary_total(summary_csv: Path) -> Optional[int]:
    if not summary_csv.exists():
        return None

    rows = parse_csv_rows(summary_csv)
    if not rows:
        return None

    try:
        return int(rows[0].get("total_beacons_sent", "0"))
    except (TypeError, ValueError):
        return None


def count_topology_ases(topology_xml: Path) -> Optional[int]:
    if not topology_xml.exists():
        return None

    try:
        root = ET.fromstring(topology_xml.read_text(encoding="utf-8"))
    except ET.ParseError:
        return None

    return len(root.findall("./ases/as"))


def estimate_scion_originated_beacons(config: ScionRunConfig) -> Optional[int]:
    if config.beacon_period_s is None or config.beacon_period_s <= 0:
        return None
    if config.last_beaconing_s is None or config.last_beaconing_s < config.first_beaconing_s:
        return None
    if config.topology is None:
        return None

    as_count = count_topology_ases(config.topology)
    if as_count is None or as_count <= 0:
        return None

    periods = int(((config.last_beaconing_s - config.first_beaconing_s) / config.beacon_period_s) + 1.0 + 1e-9)
    if periods <= 0:
        return None
    return as_count * periods


def load_link_down_times(events_json: Path) -> List[float]:
    if not events_json.exists():
        return []

    data = json.loads(events_json.read_text(encoding="utf-8"))
    seen: set[float] = set()
    times: List[float] = []
    for event in data.get("events", []):
        if event.get("type") != "link_down":
            continue
        raw_time = str(event.get("time", "")).strip().removesuffix("s")
        try:
            time_s = float(raw_time)
        except ValueError:
            continue
        if time_s < STARTUP_CUTOFF_S or time_s in seen:
            continue
        seen.add(time_s)
        times.append(time_s)

    times.sort()
    return times


def derive_snapshot_change_times(snapshot_csv: Path) -> List[float]:
    if not snapshot_csv.exists():
        return []

    previous_valid_paths: Dict[Tuple[int, int], int] = {}
    change_times: List[float] = []
    for row in parse_csv_rows(snapshot_csv):
        try:
            time_s = float(row["time_s"])
            src_as = int(row["src_as"])
            dst_as = int(row["dst_as"])
            valid_paths = int(row["valid_paths"])
        except (KeyError, ValueError):
            continue

        key = (src_as, dst_as)
        prev = previous_valid_paths.get(key)
        previous_valid_paths[key] = valid_paths
        if prev is None or prev == valid_paths:
            continue
        change_times.append(time_s)

    change_times.sort()
    return change_times


def compute_scion_convergence_time(run_dir, repo_root: Path) -> Optional[float]:
    """Compute SCION convergence time from path snapshots and link events.
    
    Args:
        run_dir: Path or RunRef object representing the run
        repo_root: Path to repository root
    """
    # Handle both Path and RunRef objects
    run_name = run_dir.name if hasattr(run_dir, 'name') else run_dir.name
    build_dir = run_dir.parent if hasattr(run_dir, 'parent') else run_dir.parent
    
    # Try directory-based path snapshots (legacy runs)
    snapshot_csv = build_dir / f"{run_name}_path_snapshots.csv"
    if not snapshot_csv.exists():
        # Try flat-file timing-sweep format: build/stochastic_scion_*.txt.paths.csv
        flat_file_csv = build_dir / f"{run_name}.txt.paths.csv"
        if flat_file_csv.exists():
            snapshot_csv = flat_file_csv
    
    change_times = derive_snapshot_change_times(snapshot_csv)
    if not change_times:
        return None

    config_path = resolve_scion_config_path(run_name, build_dir, repo_root)
    if config_path is None:
        return None
    events_rel: Optional[Path] = None
    for line in config_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("events_file:"):
            events_rel = Path(stripped.split(":", 1)[1].strip())
            break

    if events_rel is None:
        return None

    events_path = resolve_events_path(events_rel, config_path, build_dir, repo_root)
    if events_path is None:
        return None

    link_down_times = load_link_down_times(events_path)
    if not link_down_times:
        return None

    convergence_times: List[float] = []
    for index, t_down in enumerate(link_down_times):
        t_next = link_down_times[index + 1] if index + 1 < len(link_down_times) else t_down + 300.0
        lo = bisect.bisect_left(change_times, t_down)
        hi = bisect.bisect_left(change_times, t_next)
        if lo >= hi:
            continue
        convergence_times.append(change_times[hi - 1] - t_down)

    if not convergence_times:
        return None
    return statistics.fmean(convergence_times)


def compute_scion_cp_overhead(run_dir: Path, repo_root: Path) -> Tuple[Optional[float], str]:
    """Return SCION control-plane overhead as messages/s and the source used.

    Prefer a per-run CP summary when available. The current multi-event sweep configs
    overwrite a scenario-shared summary file, so historical runs usually need a
    conservative fallback derived from the configured beacon schedule.
    """
    config = load_scion_run_config(run_dir, repo_root)
    if config is None:
        return None, "missing_config"

    duration_s = config.simulation_duration_s
    if duration_s is None and config.last_beaconing_s is not None:
        duration_s = config.last_beaconing_s
    if duration_s is None or duration_s <= 0.0:
        return None, "missing_duration"

    if config.cp_summary_output is not None and run_dir.name in config.cp_summary_output.name:
        total = load_scion_cp_summary_total(config.cp_summary_output)
        if total is not None:
            return float(total) / duration_s, "cp_summary_total_beacons_per_second"

    estimated = estimate_scion_originated_beacons(config)
    if estimated is not None:
        return float(estimated) / duration_s, "configured_origin_beacons_lower_bound_per_second"

    return None, "unavailable"


def compute_scion_packet_loss(run_dir: Path) -> Optional[float]:
    # Always compute loss from run-local probe artifacts only.
    # Mixing in build-level scion_probe_*.csv files causes cross-run contamination.
    run_name = run_dir.name if hasattr(run_dir, "name") else run_dir.name
    build_dir = run_dir.parent if hasattr(run_dir, "parent") else run_dir.parent
    is_flat_file = hasattr(run_dir, "is_flat_file") and run_dir.is_flat_file

    run_path = run_dir if isinstance(run_dir, Path) else build_dir / run_name
    probe_files = sorted(run_path.glob("scion_probe_*.csv"))

    if is_flat_file and not probe_files:
        return None

    total_reply = 0
    total_timeout = 0
    for probe_path in probe_files:
        rows = parse_csv_rows(probe_path)
        events = [(row.get("event") or "").strip() for row in rows]
        sent = sum(1 for event in events if event == "sent")
        reply = sum(1 for event in events if event == "reply")
        timeout = sum(1 for event in events if event == "timeout")
        if sent == 0 or reply / sent < MIN_REPLY_RATE:
            continue
        total_reply += reply
        total_timeout += timeout

    total_terminal = total_reply + total_timeout
    if total_terminal == 0:
        return None
    return 100.0 * total_timeout / total_terminal


def compute_bgp_convergence_time(run_dir) -> Optional[float]:
    """Compute BGP convergence time. Accepts Path or RunRef."""
    # Handle both Path and RunRef objects
    if hasattr(run_dir, 'parent'):
        build_dir = run_dir.parent
        run_name = run_dir.name
    else:
        build_dir = run_dir.parent
        run_name = run_dir.name
    
    # Try directory-based lookup first (legacy)
    link_events_path = build_dir / run_name / "link_events.csv"
    if not link_events_path.exists() and hasattr(run_dir, 'is_flat_file') and run_dir.is_flat_file:
        # For flat files, events might still be in the legacy empty directory
        # Actually, for timing-sweep, link events come from the same events files
        # as used by convergence computation, so if we can't find them, we return None
        pass
    
    if not link_events_path.exists():
        return None

    seen: set[float] = set()
    link_downs: List[float] = []
    for row in parse_csv_rows(link_events_path):
        if (row.get("event") or "").strip() != "link_down":
            continue
        try:
            time_s = float(row["time_s"])
        except (KeyError, ValueError):
            continue
        if time_s < STARTUP_CUTOFF_S or time_s in seen:
            continue
        seen.add(time_s)
        link_downs.append(time_s)

    link_downs.sort()
    if not link_downs:
        return None

    update_times: List[float] = []
    cp_files_found = False
    # For flat files, check parent dir too
    for check_dir in [build_dir / run_name, build_dir] if (hasattr(run_dir, 'is_flat_file') and run_dir.is_flat_file) else [build_dir / run_name]:
        if check_dir.exists():
            for cp_file in sorted(check_dir.glob("bgp_cp_as*.csv")):
                cp_files_found = True
                for row in parse_csv_rows(cp_file):
                    event = (row.get("event") or "").strip().upper()
                    if event in {"UPDATE", "NOTIFICATION"}:
                        try:
                            update_times.append(float(row["time_s"]))
                        except (KeyError, ValueError):
                            continue

    update_times.sort()
    if not update_times:
        return None

    convergence_values: List[float] = []
    for index, t_down in enumerate(link_downs):
        t_next = link_downs[index + 1] if index + 1 < len(link_downs) else t_down + 300.0
        lo = bisect.bisect_left(update_times, t_down)
        hi = bisect.bisect_left(update_times, t_next)
        if lo >= hi:
            continue
        convergence_values.append(update_times[hi - 1] - t_down)

    if not convergence_values:
        return None
    return statistics.fmean(convergence_values)


def compute_bgp_cp_overhead(run_dir) -> Tuple[Optional[float], str]:
    """Compute BGP control-plane overhead as messages/s. Accepts Path or RunRef."""
    total_messages = 0
    max_time_s = 0.0
    
    # Handle both Path and RunRef objects
    build_dir = run_dir.parent if hasattr(run_dir, 'parent') else run_dir.parent
    run_name = run_dir.name if hasattr(run_dir, 'name') else run_dir.name
    run_full_path = build_dir / run_name if not (hasattr(run_dir, 'is_flat_file') and run_dir.is_flat_file) else None

    # Check link events in run directory
    if run_full_path and (run_full_path / "link_events.csv").exists():
        for row in parse_csv_rows(run_full_path / "link_events.csv"):
            try:
                max_time_s = max(max_time_s, float(row.get("time_s", "0")))
            except ValueError:
                continue

    # BGP probe files are directory-based (not shared globally like SCION)
    probe_dirs = [run_full_path] if run_full_path else []
    for probe_dir in probe_dirs:
        if probe_dir and probe_dir.exists():
            for probe_path in sorted(probe_dir.glob("probe_*.csv")):
                for row in parse_csv_rows(probe_path):
                    try:
                        max_time_s = max(max_time_s, float(row.get("time_s", "0")))
                    except ValueError:
                        continue

    # BGP CP files
    cp_dirs = [run_full_path] if run_full_path else []
    for cp_dir in cp_dirs:
        if cp_dir and cp_dir.exists():
            for cp_file in sorted(cp_dir.glob("bgp_cp_as*.csv")):
                for row in parse_csv_rows(cp_file):
                    try:
                        max_time_s = max(max_time_s, float(row.get("time_s", "0")))
                    except ValueError:
                        pass
                    event = (row.get("event") or "").strip().upper()
                    if event in {"UPDATE", "NOTIFICATION", "KEEPALIVE", "OPEN"}:
                        total_messages += 1

    if max_time_s <= 0.0:
        return None, "missing_duration"
    return float(total_messages) / max_time_s, "bgp_cp_messages_per_second"


def compute_bgp_packet_loss(run_dir) -> Optional[float]:
    """Compute BGP packet loss. Accepts Path or RunRef."""
    total_sent = 0
    total_timeout = 0
    
    # Handle both Path and RunRef objects
    build_dir = run_dir.parent if hasattr(run_dir, 'parent') else run_dir.parent
    run_name = run_dir.name if hasattr(run_dir, 'name') else run_dir.name
    run_full_path = build_dir / run_name if not (hasattr(run_dir, 'is_flat_file') and run_dir.is_flat_file) else None
    
    probe_dirs = [run_full_path] if run_full_path else []
    for probe_dir in probe_dirs:
        if probe_dir and probe_dir.exists():
            for probe_path in sorted(probe_dir.glob("probe_*.csv")):
                rows = parse_csv_rows(probe_path)
                events = [(row.get("event") or "").strip() for row in rows]
                sent = sum(1 for event in events if event == "sent")
                reply = sum(1 for event in events if event == "reply")
                timeout = sum(1 for event in events if event == "timeout")
                if sent == 0 or reply / sent < MIN_REPLY_RATE:
                    continue
                total_sent += sent
                total_timeout += timeout

    if total_sent == 0:
        return None
    return 100.0 * total_timeout / total_sent


def compute_probe_rtt_stats(run_dir, glob_pattern: str) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    """Compute probe RTT statistics. Accepts Path or RunRef."""
    rtts_ms: List[float] = []
    total_sent = 0
    total_reply = 0
    
    # Handle both Path and RunRef objects
    build_dir = run_dir.parent if hasattr(run_dir, 'parent') else run_dir.parent
    run_name = run_dir.name if hasattr(run_dir, 'name') else run_dir.name
    is_flat_file = hasattr(run_dir, 'is_flat_file') and run_dir.is_flat_file
    
    # For directory-based runs
    if not is_flat_file:
        run_full_path = build_dir / run_name
        if run_full_path.exists():
            for probe_path in sorted(run_full_path.glob(glob_pattern)):
                rows = parse_csv_rows(probe_path)
                events = [(row.get("event") or "").strip() for row in rows]
                sent = sum(1 for event in events if event == "sent")
                reply = sum(1 for event in events if event == "reply")
                if sent == 0 or reply / sent < MIN_REPLY_RATE:
                    continue
                total_sent += sent
                total_reply += reply
                for row in rows:
                    if (row.get("event") or "").strip() != "reply":
                        continue
                    raw = (row.get("rtt_ms") or "").strip()
                    if not raw:
                        continue
                    try:
                        value = float(raw)
                    except ValueError:
                        continue
                    rtts_ms.append(value)
    else:
        # For flat-file timing-sweep runs, check build_dir
        for probe_path in sorted(build_dir.glob(glob_pattern)):
            rows = parse_csv_rows(probe_path)
            events = [(row.get("event") or "").strip() for row in rows]
            sent = sum(1 for event in events if event == "sent")
            reply = sum(1 for event in events if event == "reply")
            if sent == 0 or reply / sent < MIN_REPLY_RATE:
                continue
            total_sent += sent
            total_reply += reply
            for row in rows:
                if (row.get("event") or "").strip() != "reply":
                    continue
                raw = (row.get("rtt_ms") or "").strip()
                if not raw:
                    continue
                try:
                    value = float(raw)
                except ValueError:
                    continue
                rtts_ms.append(value)

    if total_sent == 0 or total_reply == 0 or not rtts_ms:
        return None, None, None

    return statistics.fmean(rtts_ms), percentile(rtts_ms, 95.0), percentile(rtts_ms, 99.0)


def compute_scion_rtt_stats(run_dir: Path) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    return compute_probe_rtt_stats(run_dir, "scion_probe_*.csv")


def compute_bgp_rtt_stats(run_dir: Path) -> Tuple[Optional[float], Optional[float], Optional[float]]:
    return compute_probe_rtt_stats(run_dir, "probe_*.csv")


def parse_run_dir(path: Path) -> Optional[Tuple[str, str, str, str, float, float, float, str]]:
    match = STOCH_BGP_RE.fullmatch(path.name)
    if match:
        density = match.group("density")
        seed = match.group("seed")
        mrai = float(match.group("mrai") or "10")
        clk = float(match.group("clk") or "50")
        bcn = float(match.group("bcn") or "50")
        return (
            "BGP",
            match.group("mode"),
            match.group("scenario"),
            density,
            mrai,
            clk,
            bcn,
            f"{density}_seed{seed}",
        )

    match = STOCH_SCION_RE.fullmatch(path.name)
    if match:
        density = match.group("density")
        seed = match.group("seed")
        mrai = float(match.group("mrai") or "10")
        clk = float(match.group("clk") or "50")
        bcn = float(match.group("bcn") or "50")
        return (
            "SCION",
            match.group("mode"),
            match.group("scenario"),
            density,
            mrai,
            clk,
            bcn,
            f"{density}_seed{seed}",
        )

    match = BGP_RE.fullmatch(path.name)
    if match:
        return (
            "BGP",
            match.group("mode"),
            match.group("scenario"),
            "na",
            float(match.group("mrai")),
            float(match.group("clk")),
            float(match.group("bcn")),
            match.group("trace") or path.name,
        )

    match = SCION_RE.fullmatch(path.name)
    if match:
        return (
            "SCION",
            match.group("mode"),
            match.group("scenario"),
            "na",
            float(match.group("mrai")),
            float(match.group("clk")),
            float(match.group("bcn")),
            match.group("trace") or path.name,
        )

    return None


class RunRef:
    """Reference to either a directory-based or flat-file run."""
    def __init__(self, name: str, build_dir: Path, is_flat_file: bool = False):
        self.name = name
        self.build_dir = build_dir
        self.is_flat_file = is_flat_file
        self._path = Path(name)  # For parse_run_dir compatibility
    
    @property
    def parent(self) -> Path:
        return self.build_dir
    
    def glob(self, pattern: str) -> List[Path]:
        """Glob for files; for flat files, search in parent dir."""
        if self.is_flat_file:
            return list(self.build_dir.glob(pattern))
        else:
            return list((self.build_dir / self.name).glob(pattern))
    
    def __truediv__(self, other):
        """Support / operator for path joining."""
        if self.is_flat_file:
            return self.build_dir / other
        else:
            return (self.build_dir / self.name) / other
    
    def __str__(self):
        return self.name
    
    def __repr__(self):
        return f"RunRef({self.name})"


def discover_run_dirs(build_dir: Path) -> List[RunRef]:
    """Discover both directory-based (legacy) and flat-file (timing-sweep) runs."""
    dirs: List[RunRef] = []
    
    # Discover traditional directory-based runs
    for path in sorted(build_dir.iterdir()):
        if path.is_dir() and parse_run_dir(path) is not None:
            dirs.append(RunRef(path.name, build_dir, is_flat_file=False))
    
    # Discover flat-file timing-sweep runs (*.txt files)
    flat_file_runs: set[str] = set()
    for txt_file in sorted(build_dir.glob("stochastic_*.txt")):
        pseudo_run_name = txt_file.name[:-4]  # Remove .txt
        if pseudo_run_name in flat_file_runs:
            continue
        flat_file_runs.add(pseudo_run_name)
        
        # Check if this parses as a valid STOCH_* run
        if parse_run_dir(Path(pseudo_run_name)) is not None:
            dirs.append(RunRef(pseudo_run_name, build_dir, is_flat_file=True))
    
    return dirs


def series_label(protocol: str, mode: str, density: str, include_mode: bool = True) -> str:
    parts = [protocol]
    if density and density != "na":
        parts.append(density)
    if include_mode:
        parts.append(mode)
    return " ".join(parts)


def plot_scope_label(scope_name: str) -> str:
    if scope_name == "single_compare":
        return "single SCION-vs-BGP"
    if scope_name == "dual_compare":
        return "dual SCION-vs-BGP"
    return scope_name


def plot_scope_suffix(scope_name: str) -> str:
    return f"__{sanitize_filename(scope_name)}"


def plot_type_enabled(selected: str, kind: str) -> bool:
    if selected == "all":
        return True
    return selected == kind


def load_run_samples(
    build_dir: Path,
    repo_root: Path,
    max_runs: int,
    protocols: set[str],
    scenarios: set[str],
    modes: set[str],
    mrai_min: float,
    mrai_max: float,
) -> List[RunSample]:
    def scenario_allowed(name: str) -> bool:
        if name in scenarios:
            return True
        base = name.split("_", 1)[0]
        return base in scenarios

    samples: List[RunSample] = []
    for run_dir in discover_run_dirs(build_dir):
        parsed = parse_run_dir(run_dir)
        if parsed is None:
            continue
        protocol, mode, scenario, density, mrai_s, clk_s, bcn_s, trace_key = parsed
        if protocol.upper() not in protocols:
            continue
        if not scenario_allowed(scenario):
            continue
        if mode not in modes:
            continue
        if mrai_s < mrai_min or mrai_s > mrai_max:
            continue

        if protocol == "BGP":
            convergence_time_s = compute_bgp_convergence_time(run_dir)
            cp_overhead, cp_overhead_source = compute_bgp_cp_overhead(run_dir)
            packet_loss_percent = compute_bgp_packet_loss(run_dir)
            rtt_mean_ms, rtt_p95_ms, rtt_p99_ms = compute_bgp_rtt_stats(run_dir)
        else:
            convergence_time_s = compute_scion_convergence_time(run_dir, repo_root)
            cp_overhead, cp_overhead_source = compute_scion_cp_overhead(run_dir, repo_root)
            packet_loss_percent = compute_scion_packet_loss(run_dir)
            rtt_mean_ms, rtt_p95_ms, rtt_p99_ms = compute_scion_rtt_stats(run_dir)

        samples.append(
            RunSample(
                run_dir=run_dir,
                protocol=protocol,
                mode=mode,
                scenario=scenario,
                density=density,
                mrai_s=mrai_s,
                clk_s=clk_s,
                bcn_s=bcn_s,
                trace_key=trace_key,
                convergence_time_s=convergence_time_s,
                cp_overhead=cp_overhead,
                cp_overhead_source=cp_overhead_source,
                packet_loss_percent=packet_loss_percent,
                rtt_mean_ms=rtt_mean_ms,
                rtt_p95_ms=rtt_p95_ms,
                rtt_p99_ms=rtt_p99_ms,
            )
        )
        if max_runs > 0 and len(samples) >= max_runs:
            break

    return samples


def metric_value(sample: RunSample, metric: str) -> Optional[float]:
    return getattr(sample, metric, None)


def aggregate_groups(samples: Sequence[RunSample], x_axis: str, metrics: Sequence[str]) -> List[GroupStats]:
    buckets: DefaultDict[Tuple[str, str, str, str, str, float, str], List[float]] = defaultdict(list)
    for sample in samples:
        x_value = getattr(sample, f"{x_axis}_s")
        for metric in metrics:
            value = metric_value(sample, metric)
            if value is None:
                continue
            buckets[(sample.scenario, sample.protocol, sample.mode, sample.density, x_axis, x_value, metric)].append(
                value
            )

    out: List[GroupStats] = []
    for (scenario, protocol, mode, density, axis, x_value, metric), values in sorted(buckets.items()):
        out.append(
            GroupStats(
                scenario=scenario,
                protocol=protocol,
                mode=mode,
                density=density,
                x_axis=axis,
                x_value=x_value,
                metric=metric,
                count=len(values),
                mean=statistics.fmean(values),
                p50=percentile(values, 50.0),
                p95=percentile(values, 95.0),
                p99=percentile(values, 99.0),
                min_v=min(values),
                max_v=max(values),
                stdev=statistics.pstdev(values) if len(values) > 1 else 0.0,
            )
        )
    return out


def write_run_csv(samples: Sequence[RunSample], out_path: Path, x_axis: str) -> None:
    with out_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "scenario",
                "protocol",
                "mode",
                "density",
                "series",
                x_axis,
                "run_dir",
                "trace_key",
                "convergence_time_s",
                "cp_overhead",
                "cp_overhead_source",
                "packet_loss_percent",
                "rtt_mean_ms",
                "rtt_p95_ms",
                "rtt_p99_ms",
            ]
        )
        for sample in samples:
            writer.writerow(
                [
                    sample.scenario,
                    sample.protocol,
                    sample.mode,
                    sample.density,
                    series_label(sample.protocol, sample.mode, sample.density),
                    getattr(sample, f"{x_axis}_s"),
                    str(sample.run_dir),
                    sample.trace_key,
                    "" if sample.convergence_time_s is None else f"{sample.convergence_time_s:.6f}",
                    "" if sample.cp_overhead is None else f"{sample.cp_overhead:.6f}",
                    sample.cp_overhead_source,
                    "" if sample.packet_loss_percent is None else f"{sample.packet_loss_percent:.6f}",
                    "" if sample.rtt_mean_ms is None else f"{sample.rtt_mean_ms:.6f}",
                    "" if sample.rtt_p95_ms is None else f"{sample.rtt_p95_ms:.6f}",
                    "" if sample.rtt_p99_ms is None else f"{sample.rtt_p99_ms:.6f}",
                ]
            )


def write_summary_csv(stats: Sequence[GroupStats], out_path: Path) -> None:
    with out_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "scenario",
                "protocol",
                "mode",
                "density",
                "series",
                "x_axis",
                "x_value",
                "metric",
                "count",
                "mean",
                "p50",
                "p95",
                "p99",
                "min",
                "max",
                "stdev",
            ]
        )
        for row in stats:
            writer.writerow(
                [
                    row.scenario,
                    row.protocol,
                    row.mode,
                    row.density,
                    series_label(row.protocol, row.mode, row.density),
                    row.x_axis,
                    f"{row.x_value:.6f}",
                    row.metric,
                    row.count,
                    f"{row.mean:.6f}",
                    f"{row.p50:.6f}",
                    f"{row.p95:.6f}",
                    f"{row.p99:.6f}",
                    f"{row.min_v:.6f}",
                    f"{row.max_v:.6f}",
                    f"{row.stdev:.6f}",
                ]
            )


def line_style(protocol: str, mode: str, density: str = "na") -> Tuple[str, str, str]:
    """Return (color, linestyle, marker) for a given protocol/mode/density."""
    color = PROTOCOL_COLORS.get(protocol, "#666666")
    # When mode is fixed within a scope (single/dual compare), differentiate by density.
    if density and density != "na":
        ds = DENSITY_STYLES.get(density, {"linestyle": "-", "marker": "o"})
        return color, ds["linestyle"], ds["marker"]
    style = MODE_STYLES.get(mode, {"marker": "o", "linestyle": "-"})
    return color, style["linestyle"], style["marker"]


def group_stats_by_scenario(
    stats: Sequence[GroupStats],
    include_mode_in_series: bool,
) -> Dict[str, Dict[str, Dict[str, List[GroupStats]]]]:
    out: Dict[str, Dict[str, Dict[str, List[GroupStats]]]] = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    for row in stats:
        series = series_label(row.protocol, row.mode, row.density, include_mode=include_mode_in_series)
        out[row.scenario][row.metric][series].append(row)
    for scenario in out.values():
        for metric in scenario.values():
            for rows in metric.values():
                rows.sort(key=lambda item: item.x_value)
    return out


def group_runs_for_boxplot(
    samples: Sequence[RunSample],
    metrics: Sequence[str],
    include_mode_in_series: bool,
) -> Dict[str, Dict[str, Dict[str, List[float]]]]:
    out: Dict[str, Dict[str, Dict[str, List[float]]]] = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    for sample in samples:
        series = series_label(sample.protocol, sample.mode, sample.density, include_mode=include_mode_in_series)
        for metric in metrics:
            value = metric_value(sample, metric)
            if value is None:
                continue
            out[sample.scenario][metric][series].append(value)
    return out


def make_plot(
    scenario: str,
    metric: str,
    ylabel: str,
    series_rows: Dict[str, List[GroupStats]],
    out_path: Path,
    line_stat: str,
    x_axis: str,
    show_band: bool,
    scope_label: str,
) -> bool:
    if not series_rows:
        return False

    fig, ax = plt.subplots(figsize=(8.5, 5))
    any_data = False

    for series, rows in sorted(series_rows.items()):
        if not rows:
            continue
        any_data = True
        first = rows[0]
        color, linestyle, marker = line_style(first.protocol, first.mode, first.density)

        xs = [row.x_value for row in rows]
        ys = [getattr(row, line_stat) for row in rows]
        ax.plot(
            xs,
            ys,
            marker=marker,
            linestyle=linestyle,
            linewidth=1.8,
            markersize=5,
            color=color,
            label=series,
        )

        if show_band:
            lows = [min(row.p50, row.p95) for row in rows]
            highs = [max(row.p50, row.p95) for row in rows]
            ax.fill_between(xs, lows, highs, color=color, alpha=0.14, linewidth=0)

    if not any_data:
        plt.close(fig)
        return False

    ax.set_xlabel(f"{x_axis.upper()} (s)")
    ax.set_ylabel(ylabel)
    ax.set_title(f"Scenario {scenario.capitalize()} ({scope_label}): {ylabel} vs {x_axis.upper()}")
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend(framealpha=0.85)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    return True


def make_errorbar_plot(
    scenario: str,
    ylabel: str,
    series_rows: Dict[str, List[GroupStats]],
    out_path: Path,
    line_stat: str,
    x_axis: str,
    scope_label: str,
) -> bool:
    if not series_rows:
        return False

    fig, ax = plt.subplots(figsize=(8.5, 5))
    any_data = False

    for series, rows in sorted(series_rows.items()):
        if not rows:
            continue
        any_data = True
        first = rows[0]
        color, linestyle, marker = line_style(first.protocol, first.mode, first.density)

        xs = [row.x_value for row in rows]
        centers = [getattr(row, line_stat) for row in rows]
        lower = [max(0.0, center - row.p50) for center, row in zip(centers, rows)]
        upper = [max(0.0, row.p95 - center) for center, row in zip(centers, rows)]
        ax.errorbar(
            xs,
            centers,
            yerr=[lower, upper],
            marker=marker,
            linestyle=linestyle,
            linewidth=1.6,
            markersize=5,
            capsize=4,
            color=color,
            label=series,
        )

    if not any_data:
        plt.close(fig)
        return False

    ax.set_xlabel(f"{x_axis.upper()} (s)")
    ax.set_ylabel(ylabel)
    ax.set_title(f"Scenario {scenario.capitalize()} ({scope_label}): {ylabel} uncertainty")
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend(framealpha=0.85)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    return True


def make_box_plot(
    scenario: str,
    ylabel: str,
    series_values: Dict[str, List[float]],
    out_path: Path,
    scope_label: str,
) -> bool:
    entries: List[Tuple[str, List[float], str]] = []
    for series, values in sorted(series_values.items()):
        if not values:
            continue
        protocol = series.split(" ", 1)[0]
        color = PROTOCOL_COLORS.get(protocol, "#666666")
        entries.append((series, values, color))

    if not entries:
        return False

    labels = [item[0] for item in entries]
    data = [item[1] for item in entries]
    colors = [item[2] for item in entries]

    fig, ax = plt.subplots(figsize=(max(8.5, 1.2 * len(labels)), 5.2))
    bp = ax.boxplot(
        data,
        tick_labels=labels,
        patch_artist=True,
        showfliers=True,
        medianprops={"color": "black", "linewidth": 1.4},
    )

    for patch, color in zip(bp["boxes"], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.35)
        patch.set_edgecolor(color)
        patch.set_linewidth(1.2)

    for whisker in bp["whiskers"]:
        whisker.set_color("#444444")
        whisker.set_linewidth(1.0)
    for cap in bp["caps"]:
        cap.set_color("#444444")
        cap.set_linewidth(1.0)

    ax.set_ylabel(ylabel)
    ax.set_title(f"Scenario {scenario.capitalize()} ({scope_label}): {ylabel} distribution by series")
    ax.grid(True, axis="y", linestyle="--", alpha=0.35)
    ax.tick_params(axis="x", rotation=25)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    return True


def make_violin_plot(
    scenario: str,
    ylabel: str,
    series_values: Dict[str, List[float]],
    out_path: Path,
    scope_label: str,
) -> bool:
    entries: List[Tuple[str, List[float], str]] = []
    for series, values in sorted(series_values.items()):
        if not values:
            continue
        protocol = series.split(" ", 1)[0]
        color = PROTOCOL_COLORS.get(protocol, "#666666")
        entries.append((series, values, color))

    if not entries:
        return False

    labels = [item[0] for item in entries]
    data = [item[1] for item in entries]
    colors = [item[2] for item in entries]

    fig, ax = plt.subplots(figsize=(max(8.5, 1.2 * len(labels)), 5.2))
    parts = ax.violinplot(data, showmeans=False, showmedians=True, showextrema=True)
    for body, color in zip(parts["bodies"], colors):
        body.set_facecolor(color)
        body.set_edgecolor(color)
        body.set_alpha(0.28)

    for key in ("cbars", "cmins", "cmaxes", "cmedians"):
        part = parts.get(key)
        if part is not None:
            part.set_color("#444444")
            part.set_linewidth(1.0)

    ax.set_xticks(range(1, len(labels) + 1))
    ax.set_xticklabels(labels, rotation=25)
    ax.set_ylabel(ylabel)
    ax.set_title(f"Scenario {scenario.capitalize()} ({scope_label}): {ylabel} violin distribution")
    ax.grid(True, axis="y", linestyle="--", alpha=0.35)
    fig.tight_layout()
    fig.savefig(out_path, dpi=160)
    plt.close(fig)
    return True


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = (repo_root / args.build_dir).resolve()
    out_dir = (repo_root / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    scenario_filter = {item.strip().lower() for item in args.scenarios.split(",") if item.strip()}
    protocol_filter = {item.strip().upper() for item in args.protocols.split(",") if item.strip()}
    mode_filter = {item.strip() for item in args.modes.split(",") if item.strip()}
    requested_metrics = [item.strip() for item in args.metrics.split(",") if item.strip()]
    invalid_metrics = [metric for metric in requested_metrics if metric not in METRICS]
    if invalid_metrics:
        raise SystemExit(f"Unknown metric(s): {', '.join(invalid_metrics)}")

    if not build_dir.exists():
        raise SystemExit(f"Build directory not found: {build_dir}")

    samples = load_run_samples(
        build_dir,
        repo_root,
        args.max_runs,
        protocol_filter,
        scenario_filter,
        mode_filter,
        args.mrai_min,
        args.mrai_max,
    )
    if not samples:
        raise SystemExit("No matching sweep run directories found")

    run_csv = out_dir / "run_metrics.csv"
    summary_csv = out_dir / "summary_stats.csv"
    write_run_csv(samples, run_csv, args.x_axis)

    stats = aggregate_groups(samples, args.x_axis, requested_metrics)
    if not stats:
        raise SystemExit("No metric values found after filtering")
    write_summary_csv(stats, summary_csv)

    written: List[Path] = []
    plot_scopes = [
        (
            "single_compare",
            [sample for sample in samples if sample.mode == "single"],
            False,
        ),
        (
            "dual_compare",
            [sample for sample in samples if sample.mode == "dual"],
            False,
        ),
    ]
    for scope_name, scope_samples, include_mode_in_series in plot_scopes:
        if not scope_samples:
            continue

        scope_stats = aggregate_groups(scope_samples, args.x_axis, requested_metrics)
        grouped = group_stats_by_scenario(scope_stats, include_mode_in_series)
        box_grouped = group_runs_for_boxplot(scope_samples, requested_metrics, include_mode_in_series)
        suffix = plot_scope_suffix(scope_name)
        scope_label = plot_scope_label(scope_name)

        for scenario in sorted(grouped.keys()):
            for metric in requested_metrics:
                series_rows = grouped[scenario].get(metric, {})
                series_values = box_grouped[scenario].get(metric, {})

                if plot_type_enabled(args.plot_type, "line"):
                    out_path = out_dir / f"scenario_{scenario}__{sanitize_filename(metric)}__vs_{args.x_axis}{suffix}.png"
                    ok = make_plot(
                        scenario=scenario,
                        metric=metric,
                        ylabel=METRICS[metric],
                        series_rows=series_rows,
                        out_path=out_path,
                        line_stat=args.plot_stat,
                        x_axis=args.x_axis,
                        show_band=not args.no_band,
                        scope_label=scope_label,
                    )
                    if ok:
                        written.append(out_path)

                if plot_type_enabled(args.plot_type, "errorbar"):
                    out_path = out_dir / f"scenario_{scenario}__{sanitize_filename(metric)}__errorbar__vs_{args.x_axis}{suffix}.png"
                    ok = make_errorbar_plot(
                        scenario=scenario,
                        ylabel=METRICS[metric],
                        series_rows=series_rows,
                        out_path=out_path,
                        line_stat=args.plot_stat,
                        x_axis=args.x_axis,
                        scope_label=scope_label,
                    )
                    if ok:
                        written.append(out_path)

                if plot_type_enabled(args.plot_type, "box"):
                    out_path = out_dir / f"scenario_{scenario}__{sanitize_filename(metric)}__box{suffix}.png"
                    ok = make_box_plot(
                        scenario=scenario,
                        ylabel=METRICS[metric],
                        series_values=series_values,
                        out_path=out_path,
                        scope_label=scope_label,
                    )
                    if ok:
                        written.append(out_path)

                if plot_type_enabled(args.plot_type, "violin"):
                    out_path = out_dir / f"scenario_{scenario}__{sanitize_filename(metric)}__violin{suffix}.png"
                    ok = make_violin_plot(
                        scenario=scenario,
                        ylabel=METRICS[metric],
                        series_values=series_values,
                        out_path=out_path,
                        scope_label=scope_label,
                    )
                    if ok:
                        written.append(out_path)

    print(f"Wrote: {run_csv}")
    print(f"Wrote: {summary_csv}")
    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())