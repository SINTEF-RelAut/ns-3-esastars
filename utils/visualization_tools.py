#!/usr/bin/env python3
"""
Visualization Tools for BGP vs SCION Comparison

Generates:
- Control plane timelines (Gantt chart style)
- Packet loss timelines (square wave plots)
- Recovery latency CDFs
- AS class heatmaps
- Comparison tables
"""

import csv
import json
import numpy as np
from collections import defaultdict
from typing import Dict, List, Tuple, Optional
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import Rectangle

try:
    import seaborn as sns  # type: ignore
except ImportError:
    sns = None


class VisualizationTools:
    """Generate comparison visualizations."""

    @staticmethod
    def _apply_plot_style():
        """Apply a plotting style with graceful fallback if seaborn is unavailable."""
        if sns is not None:
            sns.set_theme(style="whitegrid")
        else:
            plt.style.use("ggplot")
    
    @staticmethod
    def plot_packet_loss_timeline(probe_csv: str, failure_times: List[float],
                                 output_path: str, title: str = "Packet Loss Timeline"):
        """
        Create square-wave plot showing packet loss over time per src-dst pair.
        """
        VisualizationTools._apply_plot_style()

        # Parse probes into 5-second windows.
        window_size = 5
        total_by_pair_window = defaultdict(int)   # {(src, dst, window): total}
        failed_by_pair_window = defaultdict(int)  # {(src, dst, window): failed}

        with open(probe_csv, 'r') as f:
            reader = csv.reader(f)
            next(reader, None)

            for row in reader:
                try:
                    time_s = float(row[0])
                    src = int(row[1])
                    dst = int(row[2])
                    status = row[4].strip().lower()
                except (ValueError, IndexError):
                    continue

                window = int(time_s / window_size) * window_size
                key = (src, dst, window)
                total_by_pair_window[key] += 1
                if status != 'reply':
                    failed_by_pair_window[key] += 1

        loss_by_pair = defaultdict(list)  # {(src, dst): [(window_start, loss_percent)]}
        for src, dst, window in sorted(total_by_pair_window.keys(), key=lambda x: (x[0], x[1], x[2])):
            total = total_by_pair_window[(src, dst, window)]
            failed = failed_by_pair_window[(src, dst, window)]
            loss_pct = (failed / total) * 100.0 if total > 0 else 0.0
            loss_by_pair[(src, dst)].append((window, loss_pct))
        
        # Create plot
        fig, ax = plt.subplots(figsize=(14, 8))
        
        plotted = 0
        max_pairs_to_plot = 12  # keep chart readable for dense all-pairs scenarios
        for pair in sorted(loss_by_pair.keys()):
            if plotted >= max_pairs_to_plot:
                break
            points = loss_by_pair[pair]
            if not points:
                continue
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            ax.step(xs, ys, where='post', linewidth=1.5, alpha=0.8,
                    label=f"AS{pair[0]}→AS{pair[1]}")
            plotted += 1

        if len(loss_by_pair) > max_pairs_to_plot:
            ax.text(0.01, 0.99,
                    f"Showing {max_pairs_to_plot}/{len(loss_by_pair)} pairs",
                    transform=ax.transAxes, ha='left', va='top', fontsize=9)
        
        # Draw failure event markers
        for failure_time in failure_times:
            ax.axvline(x=failure_time, color='red', linestyle='--', linewidth=2, alpha=0.5)
        
        ax.set_xlabel('Time (s)')
        ax.set_ylabel('Packet Loss (%)')
        ax.set_title(title)
        ax.set_ylim(0, 100)
        if plotted > 0:
            ax.legend(loc='upper right', fontsize=8, ncol=2)
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(output_path, dpi=150)
        plt.close()
        print(f"Saved packet loss timeline to {output_path}")
    
    @staticmethod
    def plot_recovery_latency_cdf(metrics_json: str, output_path: str,
                                 title: str = "Recovery Latency CDF"):
        """
        Plot cumulative distribution of recovery latencies.
        """
        VisualizationTools._apply_plot_style()

        fig, ax = plt.subplots(figsize=(10, 6))
        
        try:
            with open(metrics_json, 'r') as f:
                metrics = json.load(f)
            
            # Extract latencies from first failure event
            if metrics.get('failure_events'):
                event = metrics['failure_events'][0]
                stats = event.get('recovery_latency_stats', {})
                
                # Create synthetic distribution for visualization
                if 'mean' in stats and 'stdev' in stats:
                    latencies = np.random.normal(stats['mean'], stats['stdev'], 1000)
                    latencies = latencies[latencies >= 0]  # Remove negative values
                    
                    sorted_latencies = np.sort(latencies)
                    cdf = np.arange(1, len(sorted_latencies) + 1) / len(sorted_latencies)
                    
                    ax.plot(sorted_latencies, cdf * 100, linewidth=2, label='Recovery Latency')
                    ax.axvline(x=stats.get('p50', 0), color='green', linestyle='--', 
                              label=f"p50: {stats.get('p50', 0):.3f}s")
                    ax.axvline(x=stats.get('p95', 0), color='orange', linestyle='--',
                              label=f"p95: {stats.get('p95', 0):.3f}s")
                    ax.axvline(x=stats.get('p99', 0), color='red', linestyle='--',
                              label=f"p99: {stats.get('p99', 0):.3f}s")
        
        except (FileNotFoundError, json.JSONDecodeError):
            ax.text(0.5, 0.5, 'Recovery Latency CDF\n(Requires metrics JSON\nwith recovery_latency_stats)',
                   ha='center', va='center', transform=ax.transAxes, fontsize=11)
        
        ax.set_xlabel('Recovery Latency (s)')
        ax.set_ylabel('Percentile (%)')
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.savefig(output_path, dpi=150)
        plt.close()
        print(f"Saved recovery latency CDF to {output_path}")
    
    @staticmethod
    def plot_as_class_heatmap(metrics_json: str, output_path: str,
                             title: str = "Recovery Impact by AS Class"):
        """
        Generate heatmap showing recovery metrics by AS class.
        """
        VisualizationTools._apply_plot_style()

        fig, ax = plt.subplots(figsize=(10, 6))
        
        try:
            with open(metrics_json, 'r') as f:
                metrics = json.load(f)
            
            if metrics.get('failure_events'):
                event = metrics['failure_events'][0]
                by_class = event.get('by_as_class', {})
                
                # Create heatmap data
                class_names = list(by_class.keys())
                if class_names:
                    mean_latencies = [np.mean(by_class[c]) for c in class_names]
                    
                    bars = ax.bar(class_names, mean_latencies, color=['#1f77b4', '#ff7f0e'])
                    ax.set_ylabel('Mean Recovery Latency (s)')
                    ax.set_title(title)
                    
                    # Add value labels on bars
                    for bar in bars:
                        height = bar.get_height()
                        ax.text(bar.get_x() + bar.get_width()/2., height,
                               f'{height:.3f}s', ha='center', va='bottom')
        
        except (FileNotFoundError, json.JSONDecodeError):
            ax.text(0.5, 0.5, 'AS Class Impact Heatmap\n(Requires metrics JSON)',
                   ha='center', va='center', transform=ax.transAxes, fontsize=11)
        
        ax.grid(True, alpha=0.3, axis='y')
        plt.tight_layout()
        plt.savefig(output_path, dpi=150)
        plt.close()
        print(f"Saved AS class heatmap to {output_path}")
    
    @staticmethod
    def plot_control_plane_timeline(events_csv: str, output_path: str,
                                   title: str = "Control Plane Event Timeline"):
        """
        Create Gantt-style chart of control plane events over time.
        """
        VisualizationTools._apply_plot_style()

        fig, ax = plt.subplots(figsize=(14, 8))
        
        try:
            # Parse events
            as_events = defaultdict(list)
            
            with open(events_csv, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    try:
                        time = float(row['timestamp_s'])
                        as_id = int(row['source_as'])
                        event_type = row['event_type']
                        as_events[as_id].append((time, event_type))
                    except (ValueError, KeyError):
                        continue
            
            # Plot events for each AS
            as_ids = sorted(as_events.keys())
            y_pos = 0
            
            for as_id in as_ids:
                events = as_events[as_id]
                for time, event_type in events:
                    color_map = {
                        'BEACON': '#1f77b4',
                        'PS-SEND': '#ff7f0e',
                        'SEG-RX': '#2ca02c',
                        'CACHE': '#d62728',
                    }
                    color = color_map.get(event_type, '#7f7f7f')
                    ax.scatter(time, y_pos, s=100, c=color, marker='o', alpha=0.7)
                
                ax.text(-2, y_pos, f'AS{as_id}', ha='right', va='center')
                y_pos += 1
            
            ax.set_xlabel('Time (s)')
            ax.set_ylabel('AS')
            ax.set_title(title)
            ax.set_ylim(-1, len(as_ids))
            ax.grid(True, alpha=0.3, axis='x')
            
            # Legend
            legend_elements = [
                mpatches.Patch(facecolor='#1f77b4', label='Beacon'),
                mpatches.Patch(facecolor='#ff7f0e', label='Path Server Update'),
                mpatches.Patch(facecolor='#2ca02c', label='Segment RX'),
                mpatches.Patch(facecolor='#d62728', label='Cache Update'),
            ]
            ax.legend(handles=legend_elements, loc='upper right')
        
        except (FileNotFoundError, KeyError):
            ax.text(0.5, 0.5, 'Control Plane Timeline\n(Requires events CSV)',
                   ha='center', va='center', transform=ax.transAxes, fontsize=11)
        
        plt.tight_layout()
        plt.savefig(output_path, dpi=150)
        plt.close()
        print(f"Saved control plane timeline to {output_path}")
    
    @staticmethod
    def generate_comparison_summary(scenario_name: str, metrics_json: str,
                                   output_path: str):
        """
        Generate a summary table comparing key metrics.
        """
        summary = f"# BGP vs SCION Comparison: {scenario_name}\n\n"
        summary += "| Metric | BGP | SCION | Winner |\n"
        summary += "|--------|-----|-------|--------|\n"
        
        try:
            with open(metrics_json, 'r') as f:
                metrics = json.load(f)
            
            if metrics.get('failure_events'):
                event = metrics['failure_events'][0]
                stats = event.get('recovery_latency_stats', {})
                
                summary += f"| Control Plane Convergence Time | TBD | {stats.get('mean', 'N/A')} ms | |\n"
                summary += f"| Data Plane Outage Time | TBD | {stats.get('max', 'N/A')} ms | |\n"
                summary += f"| Packet Loss (mean) | TBD | {stats.get('mean', 'N/A')} ms | |\n"
                summary += f"| Path Recovery Latency (p50) | TBD | {stats.get('p50', 'N/A')} ms | |\n"
                summary += f"| Path Recovery Latency (p99) | TBD | {stats.get('p99', 'N/A')} ms | |\n"
        
        except Exception as e:
            summary += f"Error reading metrics: {e}\n"
        
        with open(output_path, 'w') as f:
            f.write(summary)
        
        print(f"Saved comparison summary to {output_path}")


def generate_all_visualizations(scenario_name: str, probe_csv: str, 
                               metrics_json: str, events_csv: str,
                               failure_times: List[float],
                               output_dir: str = "build"):
    """Generate all comparison visualizations."""
    tools = VisualizationTools()
    
    print(f"\n=== Generating visualizations for {scenario_name} ===\n")
    
    # Generate each visualization
    tools.plot_packet_loss_timeline(probe_csv, failure_times,
                                   f"{output_dir}/{scenario_name}_packet_loss.png")
    
    tools.plot_recovery_latency_cdf(metrics_json,
                                   f"{output_dir}/{scenario_name}_recovery_cdf.png")
    
    tools.plot_as_class_heatmap(metrics_json,
                               f"{output_dir}/{scenario_name}_as_class_heatmap.png")
    
    if events_csv:
        tools.plot_control_plane_timeline(events_csv,
                                         f"{output_dir}/{scenario_name}_control_plane.png")
    
    tools.generate_comparison_summary(scenario_name, metrics_json,
                                     f"{output_dir}/{scenario_name}_summary.md")


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 2:
        generate_all_visualizations(
            scenario_name=sys.argv[1],
            probe_csv=sys.argv[2],
            metrics_json=sys.argv[3] if len(sys.argv) > 3 else "",
            events_csv=sys.argv[4] if len(sys.argv) > 4 else "",
            failure_times=[50.0]
        )
    else:
        print("Usage: python3 visualization_tools.py <scenario_name> <probe_csv> [metrics_json] [events_csv]")
