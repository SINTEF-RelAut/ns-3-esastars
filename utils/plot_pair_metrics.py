#!/usr/bin/env python3
"""Plot BGP-versus-SCION figures from the per-pair table written by analyze_timing_sweep.py.

Takes ``pair_metrics.csv`` and nothing else, so the figures can be produced on a machine that
does not hold the raw sweep output. One row per (run, probe pair), already warm-up excluded,
with one loss denominator for both protocols and path-gated recovery.

Four figures per path class, each a function of the SCION beacon period (paired one-to-one
with the BGP clock interval, since both drive periodic control-message emission):

  loss        steady-state packet loss, the headline resilience number
  recovery    median and p90 time for a path to come back after a handover
  no_impact   share of handovers the data plane never noticed, which is the cleanest
              discriminator between a handover hidden below IP and one exposed to routing
  health      unreachable pairs and censored recovery windows

The health figure is not decoration. A pair that never replies is reported as unreachable
rather than dropped, so a sweep whose data plane is broken shows up as a tall bar instead of
quietly biasing the other three panels.

Where a sweep covers several topology families (for example ``single`` for a handover hidden
below IP and ``dual_vis`` for one visible to BGP) each family is drawn as its own line style
on the same axes, so the two handover models can be read against each other directly.
"""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

# dataviz reference palette, categorical slots 1-2. Two series sit far apart on every
# colour-vision check, and identity is carried by a legend and direct labels as well as hue.
PROTOCOL_COLORS: Dict[str, str] = {"SCION": "#2a78d6", "BGP": "#eb6834"}
# Status palette, reserved for state rather than identity.
STATUS_CRITICAL = "#d03b3b"
STATUS_WARNING = "#fab219"

SURFACE = "#fcfcfb"
INK_PRIMARY = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#8a8880"
GRID = "#d9d7cf"

# Families are distinguished by line style so colour stays free to mean protocol.
FAMILY_STYLES = ["-", "--", "-.", ":"]


@dataclass(frozen=True)
class Row:
    protocol: str
    family: str
    seed: int
    beacon_s: float
    path_class: str
    status: str
    loss_pct: Optional[float]
    loss_pct_churn: Optional[float]
    recovery_median_s: Optional[float]
    recovery_p90_s: Optional[float]
    recovery_windows: int
    recovery_no_impact: int
    recovery_censored: int


def _f(value: str) -> Optional[float]:
    value = (value or "").strip()
    if not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def _i(value: str) -> int:
    return int(_f(value) or 0)


def load(csv_path: Path) -> List[Row]:
    """Read pair_metrics.csv, tolerating tables written before the family column existed."""
    rows: List[Row] = []
    with csv_path.open(newline="", encoding="utf-8") as handle:
        for raw in csv.DictReader(handle):
            rows.append(
                Row(
                    protocol=raw["protocol"].strip(),
                    family=(raw.get("family") or "single").strip(),
                    seed=_i(raw.get("seed", "0")),
                    beacon_s=_f(raw.get("beacon_s", "")) or 0.0,
                    path_class=raw["path_class"].strip(),
                    status=raw["status"].strip(),
                    loss_pct=_f(raw.get("loss_pct", "")),
                    loss_pct_churn=_f(raw.get("loss_pct_churn", "")),
                    recovery_median_s=_f(raw.get("recovery_median_s", "")),
                    recovery_p90_s=_f(raw.get("recovery_p90_s", "")),
                    recovery_windows=_i(raw.get("recovery_windows", "0")),
                    recovery_no_impact=_i(raw.get("recovery_no_impact", "0")),
                    recovery_censored=_i(raw.get("recovery_censored", "0")),
                )
            )
    return rows


def style_axes(ax: plt.Axes, title: str, subtitle: str, ylabel: str) -> None:
    """Shared chrome: recessive grid, no top/right spines, title block above the axes."""
    ax.set_facecolor(SURFACE)
    ax.grid(True, linestyle="--", linewidth=0.7, color=GRID, alpha=0.9)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK_SECONDARY, labelsize=9)
    ax.set_xlabel("SCION beacon period / BGP clock interval (s)", fontsize=10, color=INK_SECONDARY)
    ax.set_ylabel(ylabel, fontsize=10, color=INK_SECONDARY)
    ax.set_title(title, fontsize=13, fontweight="bold", color=INK_PRIMARY, loc="left", pad=34)
    ax.text(
        0.0, 1.02, subtitle, transform=ax.transAxes, fontsize=9,
        color=INK_SECONDARY, va="bottom", ha="left",
    )


def family_style(families: Sequence[str], family: str) -> str:
    return FAMILY_STYLES[families.index(family) % len(FAMILY_STYLES)]


def series_label(protocol: str, family: str, n_families: int) -> str:
    return protocol if n_families == 1 else f"{protocol} · {family}"


def finish(fig: plt.Figure, ax: plt.Axes, out_path: Path, note: Optional[str] = None) -> None:
    """Attach the legend, optional footnote, and write the file."""
    handles, labels = ax.get_legend_handles_labels()
    if handles:
        ax.legend(
            handles, labels, frameon=True, framealpha=0.95, edgecolor=GRID,
            fontsize=9, labelcolor=INK_SECONDARY, loc="upper left",
            bbox_to_anchor=(1.01, 1.0), borderaxespad=0.0,
        )
    if note:
        fig.text(0.01, 0.005, note, fontsize=8, color=INK_MUTED, ha="left", va="bottom")
    fig.tight_layout(rect=(0, 0.03 if note else 0, 0.99, 1))
    fig.savefig(out_path, dpi=170, facecolor=SURFACE)
    plt.close(fig)


def group_by_point(
    rows: Sequence[Row],
) -> Dict[Tuple[str, str, float], List[Row]]:
    """Bucket rows by (protocol, family, beacon period)."""
    out: Dict[Tuple[str, str, float], List[Row]] = defaultdict(list)
    for r in rows:
        out[(r.protocol, r.family, r.beacon_s)].append(r)
    return out


def plot_loss(rows: Sequence[Row], path_class: str, out_path: Path) -> bool:
    """Mean loss per sweep point, with a p10-p90 band across seeds."""
    subset = [r for r in rows if r.path_class == path_class and r.loss_pct is not None]
    if not subset:
        return False

    families = sorted({r.family for r in subset})
    buckets = group_by_point(subset)
    fig, ax = plt.subplots(figsize=(9.4, 5.0))
    fig.patch.set_facecolor(SURFACE)

    for protocol in sorted({r.protocol for r in subset}):
        for family in families:
            xs, means, los, his = [], [], [], []
            for beacon in sorted({r.beacon_s for r in subset}):
                vals = [r.loss_pct for r in buckets.get((protocol, family, beacon), [])]
                vals = [v for v in vals if v is not None]
                if not vals:
                    continue
                ordered = sorted(vals)
                xs.append(beacon)
                means.append(statistics.fmean(ordered))
                los.append(ordered[int(0.10 * (len(ordered) - 1))])
                his.append(ordered[int(0.90 * (len(ordered) - 1))])
            if not xs:
                continue
            color = PROTOCOL_COLORS.get(protocol, INK_MUTED)
            ax.fill_between(xs, los, his, color=color, alpha=0.13, linewidth=0)
            ax.plot(
                xs, means, color=color, linewidth=2.0, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.4,
                linestyle=family_style(families, family),
                label=series_label(protocol, family, len(families)),
            )
            ax.annotate(
                f"{means[-1]:.1f}%", (xs[-1], means[-1]), textcoords="offset points",
                xytext=(8, 0), fontsize=9, color=INK_SECONDARY, va="center",
            )

    style_axes(
        ax,
        f"Packet loss — {path_class}",
        "Mean across seeds, band spans p10-p90. Warm-up excluded;\n"
        "unreachable pairs count as 100%.",
        "steady-state packet loss (%)",
    )
    ax.set_ylim(bottom=0)
    finish(fig, ax, out_path)
    return True


def plot_recovery(rows: Sequence[Row], path_class: str, out_path: Path) -> bool:
    """Median recovery per sweep point, with the p90 drawn as a lighter companion line."""
    subset = [
        r for r in rows if r.path_class == path_class and r.recovery_median_s is not None
    ]
    if not subset:
        return False

    families = sorted({r.family for r in subset})
    buckets = group_by_point(subset)
    fig, ax = plt.subplots(figsize=(9.4, 5.0))
    fig.patch.set_facecolor(SURFACE)

    for protocol in sorted({r.protocol for r in subset}):
        for family in families:
            xs, meds, p90s = [], [], []
            for beacon in sorted({r.beacon_s for r in subset}):
                group = buckets.get((protocol, family, beacon), [])
                m = [r.recovery_median_s for r in group if r.recovery_median_s is not None]
                q = [r.recovery_p90_s for r in group if r.recovery_p90_s is not None]
                if not m:
                    continue
                xs.append(beacon)
                meds.append(statistics.median(m))
                p90s.append(statistics.median(q) if q else float("nan"))
            if not xs:
                continue
            color = PROTOCOL_COLORS.get(protocol, INK_MUTED)
            style = family_style(families, family)
            ax.plot(
                xs, meds, color=color, linewidth=2.0, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.4, linestyle=style,
                label=series_label(protocol, family, len(families)),
            )
            ax.plot(xs, p90s, color=color, linewidth=1.3, alpha=0.45, linestyle=style)
            ax.annotate(
                f"{meds[-1]:.1f}s", (xs[-1], meds[-1]), textcoords="offset points",
                xytext=(8, 0), fontsize=9, color=INK_SECONDARY, va="center",
            )

    style_axes(
        ax,
        f"Handover recovery time — {path_class}",
        "Solid: median across pairs. Faint: p90.\n"
        "Path-gated, data-plane measured, floored by the 1 s probe interval.",
        "time for the path to recover (s)",
    )
    ax.set_ylim(bottom=0)
    finish(
        fig, ax, out_path,
        "A value at the 1 s floor means 'recovered within one probe interval', not a measured duration.",
    )
    return True


def plot_no_impact(rows: Sequence[Row], path_class: str, out_path: Path) -> bool:
    """Share of handovers the data plane never noticed."""
    subset = [r for r in rows if r.path_class == path_class and r.recovery_windows > 0]
    if not subset:
        return False

    families = sorted({r.family for r in subset})
    buckets = group_by_point(subset)
    fig, ax = plt.subplots(figsize=(9.4, 5.0))
    fig.patch.set_facecolor(SURFACE)

    for protocol in sorted({r.protocol for r in subset}):
        for family in families:
            xs, fracs = [], []
            for beacon in sorted({r.beacon_s for r in subset}):
                group = buckets.get((protocol, family, beacon), [])
                windows = sum(r.recovery_windows for r in group)
                if not windows:
                    continue
                xs.append(beacon)
                fracs.append(100.0 * sum(r.recovery_no_impact for r in group) / windows)
            if not xs:
                continue
            color = PROTOCOL_COLORS.get(protocol, INK_MUTED)
            ax.plot(
                xs, fracs, color=color, linewidth=2.0, marker="o", markersize=6,
                markeredgecolor=SURFACE, markeredgewidth=1.4,
                linestyle=family_style(families, family),
                label=series_label(protocol, family, len(families)),
            )
            ax.annotate(
                f"{fracs[-1]:.0f}%", (xs[-1], fracs[-1]), textcoords="offset points",
                xytext=(8, 0), fontsize=9, color=INK_SECONDARY, va="center",
            )

    style_axes(
        ax,
        f"Handovers absorbed without loss — {path_class}",
        "Share of path-relevant link events after which no probe was lost.\n"
        "Higher means the handover was invisible to the data plane.",
        "handovers with no packet loss (%)",
    )
    ax.set_ylim(0, 105)
    finish(fig, ax, out_path)
    return True


def plot_health(rows: Sequence[Row], path_class: str, out_path: Path) -> bool:
    """Unreachable pairs and censored recovery windows — the trustworthiness panel."""
    subset = [r for r in rows if r.path_class == path_class]
    if not subset:
        return False

    families = sorted({r.family for r in subset})
    beacons = sorted({r.beacon_s for r in subset})
    protocols = sorted({r.protocol for r in subset})
    buckets = group_by_point(subset)

    fig, ax = plt.subplots(figsize=(9.4, 5.0))
    fig.patch.set_facecolor(SURFACE)

    combos = [(p, f) for p in protocols for f in families]
    width = 0.8 / max(1, len(combos))
    any_bar = False
    for slot, (protocol, family) in enumerate(combos):
        unreach, censored, xs = [], [], []
        for index, beacon in enumerate(beacons):
            group = buckets.get((protocol, family, beacon), [])
            if not group:
                continue
            xs.append(index + (slot - (len(combos) - 1) / 2) * width)
            unreach.append(100.0 * sum(1 for r in group if r.status == "unreachable") / len(group))
            windows = sum(r.recovery_windows for r in group)
            censored.append(
                100.0 * sum(r.recovery_censored for r in group) / windows if windows else 0.0
            )
        if not xs:
            continue
        any_bar = True
        label = series_label(protocol, family, len(families))
        ax.bar(
            xs, unreach, width=width * 0.92, color=STATUS_CRITICAL, alpha=0.85,
            edgecolor=SURFACE, linewidth=1.2,
            label="unreachable pairs" if slot == 0 else None,
        )
        ax.bar(
            xs, censored, width=width * 0.92, bottom=unreach, color=STATUS_WARNING,
            alpha=0.9, edgecolor=SURFACE, linewidth=1.2,
            label="censored recovery windows" if slot == 0 else None,
        )
        for x, u, c in zip(xs, unreach, censored):
            if u + c > 0.5:
                ax.annotate(
                    label, (x, u + c), textcoords="offset points", xytext=(0, 4),
                    fontsize=7, color=INK_SECONDARY, ha="center", rotation=90,
                )
            else:
                # Zero-height bars are invisible, so say so explicitly rather than leave
                # the reader unable to tell a clean series from a missing one.
                ax.annotate(
                    f"{label}: 0", (x, 0), textcoords="offset points", xytext=(0, 4),
                    fontsize=7, color=INK_MUTED, ha="center", rotation=90,
                )

    if not any_bar:
        plt.close(fig)
        return False

    ax.set_xticks(range(len(beacons)))
    ax.set_xticklabels([f"{b:.0f}" for b in beacons])
    style_axes(
        ax,
        f"Measurement health — {path_class}",
        "Both should be near zero. A tall red bar means paths never worked\n"
        "at all, which invalidates the other figures for that point.",
        "share of pairs / windows (%)",
    )
    ax.set_ylim(bottom=0)
    finish(fig, ax, out_path)
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pair_metrics", help="Path to pair_metrics.csv")
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Directory for the PNGs (default: alongside the CSV, in ./figures)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    csv_path = Path(args.pair_metrics).resolve()
    if not csv_path.is_file():
        raise SystemExit(f"Not a file: {csv_path}")

    out_dir = Path(args.out_dir).resolve() if args.out_dir else csv_path.parent / "figures"
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = load(csv_path)
    if not rows:
        raise SystemExit(f"No rows in {csv_path}")

    families = sorted({r.family for r in rows})
    print(f"{len(rows)} rows | families: {', '.join(families)} | "
          f"protocols: {', '.join(sorted({r.protocol for r in rows}))}")

    unreachable = sum(1 for r in rows if r.status == "unreachable")
    if unreachable:
        print(
            f"WARNING: {unreachable} of {len(rows)} pairs ({100 * unreachable / len(rows):.1f}%) "
            f"never replied. Check the health figure before reading the others."
        )

    written: List[Path] = []
    for path_class in sorted({r.path_class for r in rows}):
        slug = path_class.replace(" ", "_").replace("-", "_")
        for name, fn in (
            ("loss", plot_loss),
            ("recovery", plot_recovery),
            ("no_impact", plot_no_impact),
            ("health", plot_health),
        ):
            out_path = out_dir / f"{name}__{slug}.png"
            if fn(rows, path_class, out_path):
                written.append(out_path)

    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
