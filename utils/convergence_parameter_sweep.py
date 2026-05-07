#!/usr/bin/env python3
"""Run convergence sweeps over SCION simulation parameters.

This utility clones a base YAML config, overrides selected parameters such as
beacon interval and expiration period, runs the simulator, computes aggregate
recovery metrics per pair class, and stores one consolidated CSV for plotting.
"""

from __future__ import annotations

import argparse
import copy
import csv
import itertools
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List

import yaml


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sweep convergence parameters and collect aggregate metrics")
    parser.add_argument("--base-config", required=True, help="Base SCION YAML config")
    parser.add_argument(
        "--beacon-periods",
        required=True,
        help="Comma-separated beacon periods, e.g. '5s,10s,20s'",
    )
    parser.add_argument(
        "--expiration-periods",
        default="",
        help="Comma-separated expiration periods; defaults to the base config value",
    )
    parser.add_argument(
        "--snapshot-periods",
        default="",
        help="Comma-separated snapshot logger periods; defaults to beacon period for each run",
    )
    parser.add_argument(
        "--policies",
        default="",
        help="Comma-separated beaconing policies; defaults to the base config policy",
    )
    parser.add_argument(
        "--simulation-durations",
        default="",
        help="Comma-separated simulation durations; defaults to the base config value",
    )
    parser.add_argument(
        "--pairs",
        default="",
        help="Optional src:dst filter passed through to recovery_metrics.py",
    )
    parser.add_argument(
        "--event-interface-id",
        default="",
        help="Optional interface ID passed through to recovery_metrics.py",
    )
    parser.add_argument(
        "--per-event-windows",
        action="store_true",
        help="Compute aggregate metrics per failure/recovery event window",
    )
    parser.add_argument(
        "--output-csv",
        default="build/convergence_parameter_sweep.csv",
        help="Consolidated aggregate CSV output",
    )
    parser.add_argument(
        "--work-dir",
        default="build/convergence_parameter_sweep",
        help="Directory for generated configs, logs, and per-run CSVs",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip the initial ./waf build step",
    )
    parser.add_argument(
        "--keep-generated-configs",
        action="store_true",
        help="Keep generated YAML configs instead of deleting them after successful runs",
    )
    return parser.parse_args()


def parse_csv_list(raw: str) -> List[str]:
    return [item.strip() for item in raw.split(",") if item.strip()]


def parse_duration_seconds(value: str) -> float:
    match = re.match(r"^\s*([0-9]*\.?[0-9]+)\s*([a-zA-Z]+)?\s*$", value)
    if match is None:
        raise ValueError(f"invalid duration: {value!r}")

    magnitude = float(match.group(1))
    unit = (match.group(2) or "s").lower()
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
        raise ValueError(f"unsupported duration unit: {unit!r}")
    return magnitude * factor


def sanitize_token(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", value)


def load_yaml(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"expected mapping at top level of {path}")
    return data


def dump_yaml(path: Path, data: dict) -> None:
    with path.open("w", encoding="utf-8") as handle:
        yaml.safe_dump(data, handle, sort_keys=False)


def read_aggregate_rows(path: Path) -> List[dict[str, str]]:
    with path.open("r", newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def run_command(command: List[str], cwd: Path, stdout_path: Path, stderr_path: Path) -> None:
    with stdout_path.open("w", encoding="utf-8") as stdout_handle, stderr_path.open(
        "w", encoding="utf-8"
    ) as stderr_handle:
        completed = subprocess.run(command, cwd=str(cwd), stdout=stdout_handle, stderr=stderr_handle)
    if completed.returncode != 0:
        raise subprocess.CalledProcessError(completed.returncode, command)


def main() -> None:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    base_config_path = (repo_root / args.base_config).resolve() if not Path(args.base_config).is_absolute() else Path(args.base_config)
    work_dir = (repo_root / args.work_dir).resolve() if not Path(args.work_dir).is_absolute() else Path(args.work_dir)
    output_csv_path = (repo_root / args.output_csv).resolve() if not Path(args.output_csv).is_absolute() else Path(args.output_csv)

    ensure_dir(work_dir)
    ensure_dir(work_dir / "configs")
    ensure_dir(work_dir / "logs")
    ensure_dir(work_dir / "metrics")
    ensure_dir(output_csv_path.parent)

    base_config = load_yaml(base_config_path)
    topology = str(base_config.get("topology", ""))
    events_file = str(base_config.get("events_file", ""))

    if not topology or not events_file:
        raise ValueError("base config must define both 'topology' and 'events_file'")

    beacon_service = base_config.setdefault("beacon_service", {})
    snapshot_logger = base_config.setdefault("path_snapshot_logger", {})

    beacon_periods = parse_csv_list(args.beacon_periods)
    expiration_periods = parse_csv_list(args.expiration_periods) or [str(beacon_service.get("expiration_period", "120s"))]
    policies = parse_csv_list(args.policies) or [str(beacon_service.get("policy", "baseline"))]
    simulation_durations = parse_csv_list(args.simulation_durations) or [str(base_config.get("simulation_duration", beacon_service.get("last_beaconing", "300s")))]
    snapshot_periods_arg = parse_csv_list(args.snapshot_periods)

    if not args.skip_build:
        build_stdout = work_dir / "logs" / "build.stdout"
        build_stderr = work_dir / "logs" / "build.stderr"
        run_command(["./waf", "build"], repo_root, build_stdout, build_stderr)

    fieldnames = [
        "run_id",
        "event_index",
        "event_label",
        "event_interface_id",
        "failure_time_s",
        "recovery_time_s",
        "policy",
        "beacon_period",
        "beacon_period_s",
        "expiration_period",
        "expiration_period_s",
        "snapshot_period",
        "snapshot_period_s",
        "simulation_duration",
        "simulation_duration_s",
        "pair_class",
        "pair_count",
        "switched_and_recovered",
        "switched_and_recovered_best_path_only",
        "switched_not_recovered",
        "recovered_without_detected_switch",
        "no_baseline_before_failure",
        "no_detected_change",
        "recovery_delay_min_s",
        "recovery_delay_median_s",
        "recovery_delay_mean_s",
        "recovery_delay_max_s",
        "switchover_delay_min_s",
        "switchover_delay_median_s",
        "switchover_delay_mean_s",
        "switchover_delay_max_s",
        "outage_duration_min_s",
        "outage_duration_median_s",
        "outage_duration_mean_s",
        "outage_duration_max_s",
        "best_path_only_recovery_delay_min_s",
        "best_path_only_recovery_delay_median_s",
        "best_path_only_recovery_delay_mean_s",
        "best_path_only_recovery_delay_max_s",
        "best_path_only_outage_duration_min_s",
        "best_path_only_outage_duration_median_s",
        "best_path_only_outage_duration_mean_s",
        "best_path_only_outage_duration_max_s",
        "snapshot_csv",
        "aggregate_csv",
    ]

    rows_to_write: List[dict[str, str]] = []

    for policy, beacon_period, expiration_period, simulation_duration in itertools.product(
        policies, beacon_periods, expiration_periods, simulation_durations
    ):
        snapshot_periods = snapshot_periods_arg or [beacon_period]
        for snapshot_period in snapshot_periods:
            run_id = "__".join(
                [
                    f"policy_{sanitize_token(policy)}",
                    f"beacon_{sanitize_token(beacon_period)}",
                    f"expiry_{sanitize_token(expiration_period)}",
                    f"snapshot_{sanitize_token(snapshot_period)}",
                    f"duration_{sanitize_token(simulation_duration)}",
                ]
            )

            config = copy.deepcopy(base_config)
            config.setdefault("beacon_service", {})["policy"] = policy
            config["beacon_service"]["period"] = beacon_period
            config["beacon_service"]["expiration_period"] = expiration_period
            config["beacon_service"]["last_beaconing"] = simulation_duration
            config["simulation_duration"] = simulation_duration
            config.setdefault("path_snapshot_logger", {})["period"] = snapshot_period

            snapshot_csv = work_dir / "metrics" / f"{run_id}.paths.csv"
            aggregate_csv = work_dir / "metrics" / f"{run_id}.aggregate.csv"
            output_txt = work_dir / "metrics" / f"{run_id}.txt"

            config["output"] = str(output_txt)
            config["path_snapshot_logger"]["output"] = str(snapshot_csv)

            config_path = work_dir / "configs" / f"{run_id}.yaml"
            dump_yaml(config_path, config)

            run_stdout = work_dir / "logs" / f"{run_id}.stdout"
            run_stderr = work_dir / "logs" / f"{run_id}.stderr"
            metrics_stdout = work_dir / "logs" / f"{run_id}.metrics.stdout"
            metrics_stderr = work_dir / "logs" / f"{run_id}.metrics.stderr"

            try:
                run_command(
                    ["./waf", "--run", f"scion {config_path}"],
                    repo_root,
                    run_stdout,
                    run_stderr,
                )

                metrics_command = [
                    sys.executable,
                    str((repo_root / "utils" / "recovery_metrics.py").resolve()),
                    "--csv",
                    str(snapshot_csv),
                    "--events-json",
                    events_file,
                    "--aggregate-by-class",
                    "--topology",
                    topology,
                    "--aggregate-output",
                    str(aggregate_csv),
                ]
                if args.pairs:
                    metrics_command.extend(["--pairs", args.pairs])
                if args.event_interface_id:
                    metrics_command.extend(["--event-interface-id", args.event_interface_id])
                if args.per_event_windows:
                    metrics_command.append("--per-event-windows")

                run_command(metrics_command, repo_root, metrics_stdout, metrics_stderr)

                for aggregate_row in read_aggregate_rows(aggregate_csv):
                    rows_to_write.append(
                        {
                            "run_id": run_id,
                            "event_index": aggregate_row.get("event_index", "1"),
                            "event_label": aggregate_row.get("event_label", "event_1"),
                            "event_interface_id": aggregate_row.get("event_interface_id", ""),
                            "failure_time_s": aggregate_row.get("failure_time_s", ""),
                            "recovery_time_s": aggregate_row.get("recovery_time_s", ""),
                            "policy": policy,
                            "beacon_period": beacon_period,
                            "beacon_period_s": str(parse_duration_seconds(beacon_period)),
                            "expiration_period": expiration_period,
                            "expiration_period_s": str(parse_duration_seconds(expiration_period)),
                            "snapshot_period": snapshot_period,
                            "snapshot_period_s": str(parse_duration_seconds(snapshot_period)),
                            "simulation_duration": simulation_duration,
                            "simulation_duration_s": str(parse_duration_seconds(simulation_duration)),
                            "pair_class": aggregate_row["pair_class"],
                            "pair_count": aggregate_row["pair_count"],
                            "switched_and_recovered": aggregate_row["switched_and_recovered"],
                            "switched_and_recovered_best_path_only": aggregate_row[
                                "switched_and_recovered_best_path_only"
                            ],
                            "switched_not_recovered": aggregate_row["switched_not_recovered"],
                            "recovered_without_detected_switch": aggregate_row[
                                "recovered_without_detected_switch"
                            ],
                            "no_baseline_before_failure": aggregate_row[
                                "no_baseline_before_failure"
                            ],
                            "no_detected_change": aggregate_row["no_detected_change"],
                            "recovery_delay_min_s": aggregate_row["recovery_delay_min_s"],
                            "recovery_delay_median_s": aggregate_row["recovery_delay_median_s"],
                            "recovery_delay_mean_s": aggregate_row["recovery_delay_mean_s"],
                            "recovery_delay_max_s": aggregate_row["recovery_delay_max_s"],
                            "switchover_delay_min_s": aggregate_row["switchover_delay_min_s"],
                            "switchover_delay_median_s": aggregate_row["switchover_delay_median_s"],
                            "switchover_delay_mean_s": aggregate_row["switchover_delay_mean_s"],
                            "switchover_delay_max_s": aggregate_row["switchover_delay_max_s"],
                            "outage_duration_min_s": aggregate_row["outage_duration_min_s"],
                            "outage_duration_median_s": aggregate_row["outage_duration_median_s"],
                            "outage_duration_mean_s": aggregate_row["outage_duration_mean_s"],
                            "outage_duration_max_s": aggregate_row["outage_duration_max_s"],
                            "best_path_only_recovery_delay_min_s": aggregate_row[
                                "best_path_only_recovery_delay_min_s"
                            ],
                            "best_path_only_recovery_delay_median_s": aggregate_row[
                                "best_path_only_recovery_delay_median_s"
                            ],
                            "best_path_only_recovery_delay_mean_s": aggregate_row[
                                "best_path_only_recovery_delay_mean_s"
                            ],
                            "best_path_only_recovery_delay_max_s": aggregate_row[
                                "best_path_only_recovery_delay_max_s"
                            ],
                            "best_path_only_outage_duration_min_s": aggregate_row[
                                "best_path_only_outage_duration_min_s"
                            ],
                            "best_path_only_outage_duration_median_s": aggregate_row[
                                "best_path_only_outage_duration_median_s"
                            ],
                            "best_path_only_outage_duration_mean_s": aggregate_row[
                                "best_path_only_outage_duration_mean_s"
                            ],
                            "best_path_only_outage_duration_max_s": aggregate_row[
                                "best_path_only_outage_duration_max_s"
                            ],
                            "snapshot_csv": str(snapshot_csv),
                            "aggregate_csv": str(aggregate_csv),
                        }
                    )
            finally:
                if not args.keep_generated_configs and config_path.exists():
                    config_path.unlink()

    with output_csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows_to_write)

    print(output_csv_path)


if __name__ == "__main__":
    main()