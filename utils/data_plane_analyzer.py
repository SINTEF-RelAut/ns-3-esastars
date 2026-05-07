#!/usr/bin/env python3
"""
Data Plane Analysis: Packet Loss, Recovery Latency, and Convergence Metrics

Analyzes probe CSV outputs to measure:
- Outage time and packet loss
- Path recovery latency
- AS class impact on recovery times
"""

import csv
import json
import io
from collections import defaultdict, namedtuple
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional
import numpy as np
from datetime import datetime


@dataclass
class ProbeResult:
    """Single probe result."""
    timestamp_s: float
    src_as: int
    dst_as: int
    seq: int
    status: str  # 'sent', 'reply', 'timeout'
    latency_s: Optional[float]
    path: Optional[str]
    
    @property
    def success(self) -> bool:
        return self.status == 'reply'


class DataPlaneAnalyzer:
    """Analyze data plane convergence and recovery."""
    
    def __init__(self):
        self.probes: List[ProbeResult] = []
        self.failure_times: List[float] = []  # When failures occur
        self.recovery_windows: List[Tuple[float, float]] = []  # (failure_time, recovery_time)
    
    def load_probe_csv(self, filepath: str):
        """Load probe results from CSV (scion_probe_*.csv format)."""
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            sanitized_lines = (line.replace('\x00', '') for line in f)
            reader = csv.reader(sanitized_lines)
            next(reader)  # skip header
            
            for row in reader:
                if len(row) < 5:
                    continue
                
                try:
                    timestamp_s = float(row[0])
                    src_as = int(row[1])
                    dst_as = int(row[2])
                    seq = int(row[3])
                    status = row[4].strip()
                    latency_s = float(row[5]) if row[5] else None
                    path = row[6] if len(row) > 6 else None
                    
                    probe = ProbeResult(
                        timestamp_s=timestamp_s,
                        src_as=src_as,
                        dst_as=dst_as,
                        seq=seq,
                        status=status,
                        latency_s=latency_s,
                        path=path
                    )
                    self.probes.append(probe)
                except (ValueError, IndexError):
                    continue
        
        self.probes.sort(key=lambda p: p.timestamp_s)
    
    def set_failure_times(self, failures: List[float]):
        """Set timestamps of failure events for analysis."""
        self.failure_times = sorted(failures)

    def _terminal_outcomes_for_pair(self, src_as: int, dst_as: int,
                                    start_time_s: float,
                                    end_time_s: Optional[float] = None) -> Dict[int, str]:
        """Return per-seq terminal outcome within a time window.

        Outcomes only include 'reply' and 'timeout'. A reply wins over timeout if both
        are present for the same sequence.
        """
        outcomes: Dict[int, str] = {}
        for probe in self.probes:
            if probe.src_as != src_as or probe.dst_as != dst_as:
                continue
            if probe.timestamp_s < start_time_s:
                continue
            if end_time_s is not None and probe.timestamp_s > end_time_s:
                continue

            if probe.status == "reply":
                outcomes[probe.seq] = "reply"
            elif probe.status == "timeout":
                if outcomes.get(probe.seq) != "reply":
                    outcomes[probe.seq] = "timeout"
        return outcomes

    def _max_probe_time(self) -> float:
        if not self.probes:
            return 0.0
        return max(p.timestamp_s for p in self.probes)

    def _failure_window(self, failure_times: List[float], idx: int) -> Tuple[float, float]:
        start = failure_times[idx]
        if idx + 1 < len(failure_times):
            end = max(start, failure_times[idx + 1] - 0.001)
        else:
            end = max(start, self._max_probe_time())
        return (start, end)
    
    def calculate_outage_time(self, src_as: int, dst_as: int, 
                             failure_time_s: float) -> Tuple[float, Dict]:
        """
        Calculate outage duration for specific source-destination pair.
        
        Returns:
            (outage_duration_s, metrics_dict)
        """
        pair_probes = [p for p in self.probes 
                      if p.src_as == src_as and p.dst_as == dst_as 
                      and p.timestamp_s >= failure_time_s]
        
        if not pair_probes:
            return 0, {"error": "No probes for this pair after failure"}
        
        # Find first successful probe after failure
        first_success = None
        for probe in pair_probes:
            if probe.success:
                first_success = probe
                break
        
        if first_success is None:
            return float('inf'), {
                "status": "no_recovery",
                "total_probes": len(pair_probes),
                "failed_probes": sum(1 for p in pair_probes if not p.success)
            }
        
        outage_duration = first_success.timestamp_s - failure_time_s
        
        return outage_duration, {
            "status": "recovered",
            "recovery_time_s": outage_duration,
            "first_success_seq": first_success.seq,
            "total_probes_before_recovery": len([p for p in pair_probes if p.timestamp_s <= first_success.timestamp_s])
        }

    def calculate_recovery_time_bgp_aligned(self, src_as: int, dst_as: int,
                                            failure_time_s: float) -> Tuple[float, Dict]:
        """BGP-aligned semantics: only count switched-and-recovered pairs.

        - no_loss: no timeout observed after failure
        - no_recovery: timeout(s) observed but no subsequent reply
        - switched_and_recovered: timeout(s) observed and later reply observed
        """
        pair_probes = [
            p for p in self.probes
            if p.src_as == src_as and p.dst_as == dst_as and p.timestamp_s >= failure_time_s
        ]

        if not pair_probes:
            return 0, {"error": "No probes for this pair after failure"}

        timeouts = [p for p in pair_probes if p.status == 'timeout']
        if not timeouts:
            return 0, {
                "status": "no_loss",
                "total_probes": len(pair_probes),
            }

        last_timeout = timeouts[-1]
        first_recovery = None
        for probe in pair_probes:
            if probe.success and probe.timestamp_s > last_timeout.timestamp_s:
                first_recovery = probe
                break

        if first_recovery is None:
            return float('inf'), {
                "status": "no_recovery",
                "total_probes": len(pair_probes),
                "failed_probes": sum(1 for p in pair_probes if not p.success),
            }

        recovery_time = first_recovery.timestamp_s - failure_time_s
        return recovery_time, {
            "status": "switched_and_recovered",
            "recovery_time_s": recovery_time,
            "first_recovery_seq": first_recovery.seq,
            "last_timeout_seq": last_timeout.seq,
        }
    
    def calculate_packet_loss(self, src_as: int, dst_as: int,
                             start_time_s: float, end_time_s: float) -> Dict:
        """Calculate packet loss percentage for a time window."""
        outcomes = self._terminal_outcomes_for_pair(src_as, dst_as, start_time_s, end_time_s)
        replies = sum(1 for v in outcomes.values() if v == "reply")
        timeouts = sum(1 for v in outcomes.values() if v == "timeout")
        total = replies + timeouts

        if total == 0:
            return {
                "loss_percent": None,
                "total_probes": 0,
                "successful": 0,
                "failed": 0,
            }

        loss_percent = (timeouts / total) * 100.0

        return {
            "loss_percent": loss_percent,
            "total_probes": total,
            "successful": replies,
            "failed": timeouts,
        }

    def packet_loss_stats(self, start_time_s: float, end_time_s: float) -> Dict:
        """Aggregate packet loss over all probe pairs in a time window."""
        pairs = sorted(set((p.src_as, p.dst_as) for p in self.probes))

        total_probes = 0
        total_success = 0
        total_failed = 0
        pair_losses: List[float] = []
        by_pair: Dict[str, Dict] = {}

        for src_as, dst_as in pairs:
            stats = self.calculate_packet_loss(src_as, dst_as, start_time_s, end_time_s)
            if stats.get("total_probes", 0) == 0:
                continue
            pair_id = f"{src_as}-{dst_as}"
            by_pair[pair_id] = stats

            total_probes += int(stats["total_probes"])
            total_success += int(stats["successful"])
            total_failed += int(stats["failed"])
            pair_losses.append(float(stats["loss_percent"]))

        if total_probes == 0:
            return {
                "window_start_s": start_time_s,
                "window_end_s": end_time_s,
                "loss_percent": None,
                "total_probes": 0,
                "successful": 0,
                "failed": 0,
                "pairs_count": 0,
                "pairs_with_loss": 0,
                "by_pair": {},
            }

        return {
            "window_start_s": start_time_s,
            "window_end_s": end_time_s,
            "loss_percent": (total_failed / total_probes) * 100.0,
            "total_probes": total_probes,
            "successful": total_success,
            "failed": total_failed,
            "pairs_count": len(pair_losses),
            "pairs_with_loss": sum(1 for x in pair_losses if x > 0.0),
            "pair_loss_percent_mean": float(np.mean(pair_losses)) if pair_losses else 0.0,
            "pair_loss_percent_p95": float(np.percentile(pair_losses, 95)) if pair_losses else 0.0,
            "by_pair": by_pair,
        }
    
    def convergence_by_as_class(self, failure_time_s: float,
                               as_class_map: Dict[int, str],
                               align_with_bgp: bool = False) -> Dict[str, List[float]]:
        """
        Group recovery latencies by AS class (core vs. non-core).
        
        Args:
            failure_time_s: When the failure occurred
            as_class_map: {as_id: 'core' or 'non-core'}
        
        Returns:
            {'core': [latencies...], 'non-core': [latencies...]}
        """
        latencies_by_class = defaultdict(list)
        
        # Get all unique source-destination pairs
        pairs = set((p.src_as, p.dst_as) for p in self.probes)
        
        for src_as, dst_as in sorted(pairs):
            if src_as >= dst_as:  # skip self and reverse pairs
                continue
            
            if align_with_bgp:
                outage_time, metrics = self.calculate_recovery_time_bgp_aligned(
                    src_as, dst_as, failure_time_s)
                include = metrics.get("status") == "switched_and_recovered"
            else:
                outage_time, metrics = self.calculate_outage_time(src_as, dst_as, failure_time_s)
                include = metrics.get("status") == "recovered"

            if include:
                src_class = as_class_map.get(src_as, "unknown")
                dst_class = as_class_map.get(dst_as, "unknown")
                
                # Classify by source AS (could also do by destination)
                latencies_by_class[src_class].append(outage_time)
        
        return {k: v for k, v in latencies_by_class.items() if v}
    
    def recovery_latency_stats(self, failure_time_s: float,
                               align_with_bgp: bool = False) -> Dict:
        """
        Calculate statistics on recovery latencies across all pairs.
        
        Returns:
            {'p50': ..., 'p95': ..., 'p99': ..., 'mean': ..., 'max': ...}
        """
        latencies = []
        
        pairs = set((p.src_as, p.dst_as) for p in self.probes)
        for src_as, dst_as in pairs:
            if align_with_bgp:
                outage_time, metrics = self.calculate_recovery_time_bgp_aligned(
                    src_as, dst_as, failure_time_s)
                include = metrics.get("status") == "switched_and_recovered"
            else:
                outage_time, metrics = self.calculate_outage_time(src_as, dst_as, failure_time_s)
                include = metrics.get("status") == "recovered"

            if include:
                latencies.append(outage_time)
        
        if not latencies:
            return {}
        
        return {
            "count": len(latencies),
            "mean": float(np.mean(latencies)),
            "p50": float(np.percentile(latencies, 50)),
            "p95": float(np.percentile(latencies, 95)),
            "p99": float(np.percentile(latencies, 99)),
            "min": float(np.min(latencies)),
            "max": float(np.max(latencies)),
            "stdev": float(np.std(latencies))
        }
    
    def export_metrics_json(self, output_path: str, failure_times: List[float],
                           as_class_map: Dict[int, str],
                           align_with_bgp: bool = False):
        """Export all metrics to JSON for visualization."""
        scenario_start = failure_times[0] if failure_times else 0.0
        scenario_end = self._max_probe_time()
        metrics = {
            "failure_events": [],
            "scenario_stats": self.recovery_latency_stats(
                failure_times[0] if failure_times else 0,
                align_with_bgp=align_with_bgp,
            ),
            "scenario_packet_loss": self.packet_loss_stats(scenario_start, scenario_end),
        }

        for idx, failure_time in enumerate(failure_times):
            win_start, win_end = self._failure_window(failure_times, idx)
            event_metrics = {
                "failure_time_s": failure_time,
                "recovery_latency_stats": self.recovery_latency_stats(
                    failure_time,
                    align_with_bgp=align_with_bgp,
                ),
                "by_as_class": self.convergence_by_as_class(
                    failure_time,
                    as_class_map,
                    align_with_bgp=align_with_bgp,
                ),
                "packet_loss_stats": self.packet_loss_stats(win_start, win_end),
            }
            metrics["failure_events"].append(event_metrics)
        
        with open(output_path, 'w') as f:
            json.dump(metrics, f, indent=2)
    
    def summary_report(self, failure_times: List[float], 
                      as_class_map: Dict[int, str]) -> str:
        """Generate human-readable summary."""
        report = "=== Data Plane Convergence Summary ===\n\n"
        report += f"Total probes: {len(self.probes)}\n"
        report += f"Unique source-destination pairs: {len(set((p.src_as, p.dst_as) for p in self.probes))}\n\n"
        
        if failure_times:
            failure_time = failure_times[0]  # Focus on first failure
            report += f"Failure at t={failure_time}s\n"
            stats = self.recovery_latency_stats(failure_time)
            
            if stats:
                report += f"\nRecovery Latency Statistics:\n"
                report += f"  Mean: {stats['mean']:.3f}s\n"
                report += f"  p50:  {stats['p50']:.3f}s\n"
                report += f"  p95:  {stats['p95']:.3f}s\n"
                report += f"  p99:  {stats['p99']:.3f}s\n"
                report += f"  Max:  {stats['max']:.3f}s\n"
            
            # By AS class
            by_class = self.convergence_by_as_class(failure_time, as_class_map)
            if by_class:
                report += f"\nRecovery by AS Class:\n"
                for as_class, latencies in by_class.items():
                    report += f"  {as_class}: n={len(latencies)}, mean={np.mean(latencies):.3f}s, max={np.max(latencies):.3f}s\n"
        
        return report


def analyze_probes(csv_file: str, failure_times: List[float], 
                  as_class_map: Dict[int, str], output_dir: str = "build"):
    """Run analysis on probe CSV."""
    analyzer = DataPlaneAnalyzer()
    analyzer.load_probe_csv(csv_file)
    analyzer.set_failure_times(failure_times)
    
    print(analyzer.summary_report(failure_times, as_class_map))
    
    # Export metrics
    json_output = f"{output_dir}/data_plane_metrics.json"
    analyzer.export_metrics_json(json_output, failure_times, as_class_map)
    print(f"Exported metrics to: {json_output}")
    
    return analyzer


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        # Example: determine failure times from command line
        failure_times = [50.0]  # Default to 50s
        as_class_map = {101: 'core', 102: 'core', 103: 'core', 104: 'core', 
                        105: 'non-core', 106: 'non-core', 107: 'non-core', 108: 'non-core'}
        analyze_probes(sys.argv[1], failure_times, as_class_map)
    else:
        print("Usage: python3 data_plane_analyzer.py <probe_csv>")
