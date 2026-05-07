#!/usr/bin/env python3
"""Plot convergence sweep results from convergence_parameter_sweep.py output."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


DEFAULT_METRICS = [
    "recovery_delay_mean_s",
    "recovery_delay_max_s",
    "switched_not_recovered",
    "outage_duration_mean_s",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot convergence sweep CSV results")
    parser.add_argument("--csv", required=True, help="Input sweep CSV")
    parser.add_argument(
        "--out-dir",
        default="build/convergence_plots",
        help="Directory where PNG plots will be written",
    )
    parser.add_argument(
        "--x",
        default="beacon_period_s",
        help="Column to use as x-axis, e.g. beacon_period_s or expiration_period_s",
    )
    parser.add_argument(
        "--metrics",
        default=",".join(DEFAULT_METRICS),
        help="Comma-separated metrics to plot",
    )
    parser.add_argument(
        "--series-columns",
        default="pair_class",
        help="Comma-separated columns used to form one plotted series label",
    )
    parser.add_argument(
        "--where",
        default="",
        help="Optional comma-separated equality filters, e.g. 'policy=baseline,pair_class=core-core'",
    )
    parser.add_argument(
        "--title-prefix",
        default="Convergence Sweep",
        help="Title prefix for generated plots",
    )
    parser.add_argument(
        "--metric-titles",
        default="",
        help="Comma-separated metric=Title pairs to override per-metric title text, "
             "e.g. 'recovery_delay_mean_s=Recovery Delay'",
    )
    return parser.parse_args()


def parse_csv_list(raw: str) -> List[str]:
    return [item.strip() for item in raw.split(",") if item.strip()]


def parse_filters(raw: str) -> Dict[str, str]:
    filters: Dict[str, str] = {}
    for item in parse_csv_list(raw):
        key, value = item.split("=", 1)
        filters[key.strip()] = value.strip()
    return filters


def load_rows(path: Path) -> List[dict[str, str]]:
    with path.open("r", newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def row_matches_filters(row: dict[str, str], filters: Dict[str, str]) -> bool:
    for key, value in filters.items():
        if row.get(key, "") != value:
            return False
    return True


def parse_float(value: str) -> float | None:
    value = value.strip()
    if not value:
        return None
    return float(value)


def build_series_label(row: dict[str, str], series_columns: List[str]) -> str:
    parts = []
    for column in series_columns:
        parts.append(f"{column}={row.get(column, '')}")
    return ", ".join(parts)


def sanitize_filename(value: str) -> str:
    allowed = []
    for ch in value:
        if ch.isalnum() or ch in "._-":
            allowed.append(ch)
        else:
            allowed.append("_")
    return "".join(allowed)


def parse_metric_titles(raw: str) -> Dict[str, str]:
    titles: Dict[str, str] = {}
    for item in parse_csv_list(raw):
        key, value = item.split("=", 1)
        titles[key.strip()] = value.strip()
    return titles


def plot_metric(
    rows: List[dict[str, str]],
    metric: str,
    x_column: str,
    series_columns: List[str],
    out_dir: Path,
    title_prefix: str,
    metric_titles: Dict[str, str] | None = None,
) -> Path | None:
    grouped: Dict[str, List[Tuple[float, float]]] = {}
    for row in rows:
        x_value = parse_float(row.get(x_column, ""))
        y_value = parse_float(row.get(metric, ""))
        if x_value is None or y_value is None:
            continue
        label = build_series_label(row, series_columns)
        grouped.setdefault(label, []).append((x_value, y_value))

    if not grouped:
        return None

    plt.figure(figsize=(8, 5))
    for label, points in sorted(grouped.items()):
        points.sort(key=lambda item: item[0])
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        plt.plot(xs, ys, marker="o", linewidth=2, label=label)

    metric_label = (metric_titles or {}).get(metric, metric)
    plt.xlabel(x_column)
    plt.ylabel(metric_label)
    plt.title(f"{title_prefix}: {metric_label} vs {x_column}")
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()

    out_path = out_dir / f"{sanitize_filename(metric)}__vs__{sanitize_filename(x_column)}.png"
    plt.savefig(out_path, dpi=160)
    plt.close()
    return out_path


def main() -> None:
    args = parse_args()
    csv_path = Path(args.csv).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    metrics = parse_csv_list(args.metrics)
    series_columns = parse_csv_list(args.series_columns)
    filters = parse_filters(args.where)
    metric_titles = parse_metric_titles(args.metric_titles)

    rows = [row for row in load_rows(csv_path) if row_matches_filters(row, filters)]

    written: List[Path] = []
    for metric in metrics:
        out_path = plot_metric(rows, metric, args.x, series_columns, out_dir, args.title_prefix, metric_titles)
        if out_path is not None:
            written.append(out_path)

    for path in written:
        print(path)


if __name__ == "__main__":
    main()
