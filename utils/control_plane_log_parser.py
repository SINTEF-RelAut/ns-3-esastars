#!/usr/bin/env python3
"""
Control Plane Event Log Parser for SCION Convergence Analysis

This module captures and analyzes control plane events (beacons, path server updates,
host cache changes) and estimates convergence times.
"""

import json
import csv
from collections import defaultdict
from dataclasses import dataclass, asdict
from typing import Dict, List, Tuple, Optional
import re


@dataclass
class ControlPlaneEvent:
    """Represents a single control plane event."""
    timestamp_s: float
    event_type: str  # 'beacon', 'path_server_update', 'host_cache_update', 'route_change'
    source_as: int
    dest_as: Optional[int]
    details: str
    
    def to_dict(self):
        return asdict(self)


class ControlPlaneLogParser:
    """Parses simulation output for control plane events."""

    def __init__(self):
        self.events: List[ControlPlaneEvent] = []
        self.beacon_tree: Dict[int, List[Tuple[float, int]]] = defaultdict(list)  # {as_id: [(time, msg_count)]}
        self.convergence_times: Dict[str, float] = {}  # {event_key: time_to_convergence}

    def parse_file(self, filepath: str) -> List[ControlPlaneEvent]:
        """Parse NS3 debug output for control plane events."""
        pattern = r'\[AS(\d+)-([A-Z]+)\]\s+t=([0-9\.]+)\s+(.*)'
        
        with open(filepath, 'r', errors='ignore') as f:
            for line in f:
                match = re.search(pattern, line)
                if match:
                    as_num, event_type, time_str, details = match.groups()
                    time_s = float(time_str)
                    
                    event = ControlPlaneEvent(
                        timestamp_s=time_s,
                        event_type=event_type,
                        source_as=int(as_num),
                        dest_as=None,
                        details=details
                    )
                    self.events.append(event)
        
        self.events.sort(key=lambda e: e.timestamp_s)
        return self.events

    def parse_path_snapshots(self, filepath: str) -> List[ControlPlaneEvent]:
        """Parse path snapshot CSV and synthesize control-plane change events."""
        previous_valid_paths: Dict[Tuple[int, int], int] = {}

        with open(filepath, 'r', errors='ignore') as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    time_s = float(row['time_s'])
                    src_as = int(row['src_as'])
                    dst_as = int(row['dst_as'])
                    valid_paths = int(row['valid_paths'])
                except (KeyError, ValueError):
                    continue

                key = (src_as, dst_as)
                if key not in previous_valid_paths:
                    previous_valid_paths[key] = valid_paths
                    continue

                prev = previous_valid_paths[key]
                if valid_paths == prev:
                    continue

                if prev == 0 and valid_paths > 0:
                    event_type = 'PATH-UP'
                elif prev > 0 and valid_paths == 0:
                    event_type = 'PATH-DOWN'
                else:
                    event_type = 'PATH-CHANGE'

                details = f"valid_paths {prev} -> {valid_paths} for {src_as}->{dst_as}"
                self.events.append(
                    ControlPlaneEvent(
                        timestamp_s=time_s,
                        event_type=event_type,
                        source_as=src_as,
                        dest_as=dst_as,
                        details=details,
                    )
                )
                previous_valid_paths[key] = valid_paths

        self.events.sort(key=lambda e: e.timestamp_s)
        return self.events

    def extract_beacon_timeline(self) -> Dict[int, List[Tuple[float, str]]]:
        """Extract beacon propagation timeline for each AS."""
        beacon_events = defaultdict(list)
        
        for event in self.events:
            if 'BEACON' in event.event_type or 'beacon' in event.details.lower():
                beacon_events[event.source_as].append(
                    (event.timestamp_s, event.details)
                )
        
        return beacon_events
    
    def extract_path_server_updates(self) -> Dict[int, List[Tuple[float, str]]]:
        """Extract path server registration events."""
        ps_events = defaultdict(list)
        
        for event in self.events:
            if 'PS-SEND' in event.event_type or 'PATH-SERVER' in event.event_type:
                ps_events[event.source_as].append(
                    (event.timestamp_s, event.details)
                )
        
        return ps_events
    
    def extract_host_cache_updates(self) -> Dict[int, List[Tuple[float, str]]]:
        """Extract host segment cache updates."""
        cache_events = defaultdict(list)
        
        for event in self.events:
            if 'CACHE' in event.event_type or 'SEG-RX' in event.event_type:
                cache_events[event.source_as].append(
                    (event.timestamp_s, event.details)
                )
        
        return cache_events
    
    def estimate_convergence_time(self, failure_time_s: float, 
                                 convergence_threshold_s: float = 5.0) -> Tuple[float, str]:
        """
        Estimate convergence time by detecting when control plane stabilizes.
        
        Args:
            failure_time_s: Timestamp of the failure event
            convergence_threshold_s: Seconds of no new updates = converged
        
        Returns:
            (convergence_time_s, description)
        """
        events_after_failure = [e for e in self.events if e.timestamp_s >= failure_time_s]
        
        if not events_after_failure:
            return 0, "No events recorded after failure"
        
        # Track time of last significant event
        last_event_time = failure_time_s
        for event in events_after_failure:
            if any(x in event.event_type for x in ['PS-SEND', 'SEG-RX', 'CACHE']):
                last_event_time = event.timestamp_s
        
        convergence_time = last_event_time - failure_time_s
        return convergence_time, f"Last event at {last_event_time}s"
    
    def export_timeline_csv(self, output_path: str):
        """Export events as CSV for visualization."""
        with open(output_path, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=[
                'timestamp_s', 'event_type', 'source_as', 'dest_as', 'details'
            ])
            writer.writeheader()
            for event in self.events:
                writer.writerow(event.to_dict())
    
    def summary_report(self) -> str:
        """Generate human-readable summary."""
        beacon_timeline = self.extract_beacon_timeline()
        ps_timeline = self.extract_path_server_updates()
        cache_timeline = self.extract_host_cache_updates()
        
        report = "=== Control Plane Timeline Summary ===\n\n"

        report += f"Total events recorded: {len(self.events)}\n"
        if not self.events:
            report += "Time span: N/A\n\n"
            report += "No control-plane events were parsed.\n"
            return report

        report += f"Time span: {self.events[0].timestamp_s:.2f}s - {self.events[-1].timestamp_s:.2f}s\n\n"
        
        report += f"Beacon propagation events: {sum(len(v) for v in beacon_timeline.values())}\n"
        report += f"Path server updates: {sum(len(v) for v in ps_timeline.values())}\n"
        report += f"Host cache updates: {sum(len(v) for v in cache_timeline.values())}\n\n"
        
        report += "Events per AS:\n"
        as_event_counts = defaultdict(int)
        for event in self.events:
            as_event_counts[event.source_as] += 1
        for as_id in sorted(as_event_counts.keys()):
            report += f"  AS{as_id}: {as_event_counts[as_id]} events\n"
        
        return report


def parse_and_analyze(log_file: str, output_dir: str = "build", output_csv: str = ""):
    """Parse control-plane log file and generate analysis outputs."""
    parser = ControlPlaneLogParser()
    parser.parse_file(log_file)

    print(parser.summary_report())

    csv_output = output_csv or f"{output_dir}/control_plane_events.csv"
    parser.export_timeline_csv(csv_output)
    print(f"\nExported events to: {csv_output}")

    return parser


def parse_snapshots_and_analyze(snapshot_file: str, output_dir: str = "build", output_csv: str = ""):
    """Parse path snapshots CSV as a fallback control-plane signal source."""
    parser = ControlPlaneLogParser()
    parser.parse_path_snapshots(snapshot_file)

    print("Using path snapshots as control-plane fallback source")
    print(parser.summary_report())

    csv_output = output_csv or f"{output_dir}/control_plane_events.csv"
    parser.export_timeline_csv(csv_output)
    print(f"\nExported events to: {csv_output}")

    return parser


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 2 and sys.argv[1] == "--snapshots":
        parse_snapshots_and_analyze(sys.argv[2])
    elif len(sys.argv) > 1:
        parse_and_analyze(sys.argv[1])
    else:
        print("Usage: python3 control_plane_log_parser.py <log_file>")
        print("   or: python3 control_plane_log_parser.py --snapshots <path_snapshots.csv>")
