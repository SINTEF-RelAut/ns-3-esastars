#!/usr/bin/env python3
"""
Unified BGP vs SCION Comparison Analysis Pipeline

This script orchestrates the full analysis workflow:
1. Run SCION simulations for all scenarios (A/B/C)
2. Parse control plane logs
3. Analyze data plane metrics
4. Generate visualizations
5. Compare results

Usage:
    ./comparative_analysis.py [--scenarios A,B,C] [--output-dir build]
"""

import os
import subprocess
import json
import argparse
import glob
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple


class ComparativeAnalysisPipeline:
    """Master orchestration script for BGP vs SCION comparison."""
    
    # Default AS classification
    AS_CLASS_MAP = {
        101: 'core',
        102: 'core',
        103: 'core',
        104: 'core',
        105: 'non-core',
        106: 'non-core',
        107: 'non-core',
        108: 'non-core',
    }
    
    # Scenario configurations
    SCENARIOS = {
        'A': {
            'name': 'Core Link Failure (AS101-AS102)',
            'config': 'configs/scenario_a_core_link_failure.yaml',
            'failure_times': [50.0],
            'description': 'Single link failure between two core ASes'
        },
        'B': {
            'name': 'Dual Core Link Failure',
            'config': 'configs/scenario_b_dual_link_failure.yaml',
            'failure_times': [50.0],
            'description': 'Simultaneous failure of two core path segments'
        },
        'C': {
            'name': 'Recovery Latency Variance',
            'config': 'configs/scenario_c_recovery_variance.yaml',
            'failure_times': [30.0, 120.0, 210.0],
            'description': 'Same failure at different times to measure variance'
        },
    }
    
    def __init__(self, output_dir: str = "build", scenarios: List[str] = None):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        self.scenarios = scenarios or ['A', 'B', 'C']
        self.results = {}

    def _find_control_log(self, scenario: str) -> str:
        """Find the best control-plane log candidate for a scenario."""
        scenario_lower = scenario.lower()
        candidates = [
            self.output_dir / f"scenario_{scenario_lower}_control_plane.log",
            self.output_dir / f"scenario_{scenario_lower}.log",
            self.output_dir / f"scenario_{scenario_lower}_core_link_failure_control_plane.log",
            self.output_dir / f"scenario_{scenario_lower}_edge_link_failure_control_plane.log",
            self.output_dir / f"scenario_{scenario_lower}_dual_link_failure_control_plane.log",
            self.output_dir / f"scenario_{scenario_lower}_recovery_variance_control_plane.log",
        ]

        for candidate in candidates:
            if candidate.exists():
                return str(candidate)

        # Fall back to a flexible glob match.
        glob_pattern = str(self.output_dir / f"scenario_{scenario_lower}*control_plane*.log")
        matches = sorted(glob.glob(glob_pattern))
        if matches:
            return matches[0]

        return ""

    def _find_path_snapshot(self, scenario: str) -> str:
        """Find path snapshot CSV for a scenario (fallback control-plane source)."""
        scenario_lower = scenario.lower()
        candidates = [
            self.output_dir / f"scenario_{scenario_lower}_path_snapshots.csv",
            self.output_dir / f"scenario_{scenario_lower}_core_link_failure_path_snapshots.csv",
            self.output_dir / f"scenario_{scenario_lower}_edge_link_failure_path_snapshots.csv",
            self.output_dir / f"scenario_{scenario_lower}_dual_link_failure_path_snapshots.csv",
            self.output_dir / f"scenario_{scenario_lower}_recovery_variance_path_snapshots.csv",
        ]

        for candidate in candidates:
            if candidate.exists():
                return str(candidate)

        matches = sorted(glob.glob(str(self.output_dir / f"scenario_{scenario_lower}*path_snapshot*.csv")))
        if matches:
            return matches[0]

        return ""
    
    def run_scion_simulation(self, scenario: str, config_file: str) -> bool:
        """
        Run SCION simulation for a scenario.
        
        Returns:
            True if successful, False otherwise
        """
        print(f"\n{'='*70}")
        print(f"Running SCION simulation for Scenario {scenario}")
        print(f"{'='*70}")
        
        # Check if config exists
        if not os.path.exists(config_file):
            print(f"ERROR: Config file not found: {config_file}")
            return False
        
        # Build command
        cmd = ['./waf', '--run', f'scion {config_file}']
        
        print(f"Command: {' '.join(cmd)}")
        print(f"Output directory: {self.output_dir}")
        
        try:
            result = subprocess.run(
                cmd,
                cwd='.',
                capture_output=True,
                text=True,
                timeout=600  # 10 minute timeout
            )
            
            if result.returncode == 0:
                print(f"✓ Simulation completed successfully")
                return True
            else:
                print(f"✗ Simulation failed with return code {result.returncode}")
                print(f"STDERR:\n{result.stderr[-500:]}")  # Last 500 chars of stderr
                return False
        
        except subprocess.TimeoutExpired:
            print(f"✗ Simulation timed out after 600 seconds")
            return False
        except Exception as e:
            print(f"✗ Error running simulation: {e}")
            return False
    
    def analyze_scenario(self, scenario: str, config: Dict) -> Dict:
        """
        Run complete analysis for one scenario.
        
        Returns:
            Dictionary of analysis results
        """
        scenario_name = f"scenario_{scenario.lower()}"
        results = {
            'scenario': scenario,
            'name': config['name'],
            'status': 'pending',
            'steps': {}
        }
        
        # Step 1: Run simulation
        print(f"\n[1/4] Running simulation...")
        sim_success = self.run_scion_simulation(scenario, config['config'])
        results['steps']['simulation'] = sim_success
        
        if not sim_success:
            results['status'] = 'failed'
            return results
        
        # Step 2: Parse control plane logs (or path snapshots fallback)
        print(f"\n[2/4] Analyzing control plane...")
        control_log = self._find_control_log(scenario)
        path_snapshot = self._find_path_snapshot(scenario)
        scenario_events_csv = f"{self.output_dir}/scenario_{scenario.lower()}_control_plane_events.csv"

        try:
            from control_plane_log_parser import parse_and_analyze, parse_snapshots_and_analyze

            if control_log:
                parse_and_analyze(control_log, str(self.output_dir), output_csv=scenario_events_csv)
                results['steps']['control_plane_analysis'] = True
            elif path_snapshot:
                parse_snapshots_and_analyze(path_snapshot, str(self.output_dir), output_csv=scenario_events_csv)
                results['steps']['control_plane_analysis'] = True
            else:
                print(
                    f"Warning: Control plane analysis skipped: no control log or path snapshot found for scenario {scenario} in {self.output_dir}"
                )
                results['steps']['control_plane_analysis'] = False
        except Exception as e:
            print(f"Warning: Control plane analysis failed: {e}")
            results['steps']['control_plane_analysis'] = False
        
        # Step 3: Analyze data plane
        print(f"\n[3/4] Analyzing data plane...")
        # Look for actual probe files
        probe_files = glob.glob(f"{self.output_dir}/scion_probe_*.csv")
        
        data_plane_metrics = {}
        if probe_files:
            try:
                from data_plane_analyzer import analyze_probes
                analyzer = analyze_probes(
                    probe_files[0],
                    config['failure_times'],
                    self.AS_CLASS_MAP,
                    str(self.output_dir)
                )
                data_plane_metrics = analyzer.recovery_latency_stats(config['failure_times'][0])
                results['steps']['data_plane_analysis'] = True
            except Exception as e:
                print(f"Warning: Data plane analysis failed: {e}")
                results['steps']['data_plane_analysis'] = False
        
        # Step 4: Generate visualizations
        print(f"\n[4/4] Generating visualizations...")
        try:
            from visualization_tools import generate_all_visualizations
            
            metrics_json = f"{self.output_dir}/data_plane_metrics.json"
            events_csv = f"{self.output_dir}/scenario_{scenario.lower()}_control_plane_events.csv"
            
            # Try to find any available probe CSV
            if probe_files:
                generate_all_visualizations(
                    scenario_name=scenario_name,
                    probe_csv=probe_files[0],
                    metrics_json=metrics_json if os.path.exists(metrics_json) else "",
                    events_csv=events_csv if os.path.exists(events_csv) else "",
                    failure_times=config['failure_times'],
                    output_dir=str(self.output_dir)
                )
                results['steps']['visualization'] = True
            else:
                print("Warning: No probe CSV files found for visualization")
                results['steps']['visualization'] = False
        
        except Exception as e:
            print(f"Warning: Visualization generation failed: {e}")
            results['steps']['visualization'] = False
        
        # Compile results
        results['status'] = 'completed'
        results['metrics'] = data_plane_metrics
        
        return results
    
    def run_all_scenarios(self):
        """Run analysis for all selected scenarios."""
        print(f"\n{'='*70}")
        print(f"BGP vs SCION Comparative Analysis")
        print(f"Scenarios: {', '.join(self.scenarios)}")
        print(f"Output directory: {self.output_dir}")
        print(f"{'='*70}")
        
        for scenario in self.scenarios:
            if scenario not in self.SCENARIOS:
                print(f"Unknown scenario: {scenario}")
                continue
            
            config = self.SCENARIOS[scenario]
            print(f"\n{'─'*70}")
            print(f"SCENARIO {scenario}: {config['name']}")
            print(f"{'─'*70}")
            print(f"Description: {config['description']}")
            
            result = self.analyze_scenario(scenario, config)
            self.results[scenario] = result
            
            self._print_scenario_summary(result)
    
    def _print_scenario_summary(self, result: Dict):
        """Print summary of scenario results."""
        print(f"\nScenario Summary:")
        print(f"  Status: {result['status']}")
        print(f"  Steps completed:")
        for step, success in result.get('steps', {}).items():
            status = '✓' if success else '✗'
            print(f"    {status} {step}")
        
        if result.get('metrics'):
            metrics = result['metrics']
            print(f"  Key Metrics:")
            print(f"    Recovery Time (p50): {metrics.get('p50', 'N/A'):.3f}s")
            print(f"    Recovery Time (p99): {metrics.get('p99', 'N/A'):.3f}s")
            print(f"    Mean Recovery Time: {metrics.get('mean', 'N/A'):.3f}s")
    
    def generate_final_report(self):
        """Generate final comparison report."""
        report_path = self.output_dir / "COMPARISON_REPORT.md"
        
        report = "# BGP vs SCION Comparative Analysis Report\n\n"
        report += f"Generated: {datetime.now().isoformat()}\n\n"
        
        report += "## Executive Summary\n\n"
        report += "This report compares BGP and SCION protocols across three failure scenarios:\n"
        report += "- **Scenario A**: Core link failure (AS101-AS102)\n"
        report += "- **Scenario C**: Dual simultaneous failures\n"
        report += "- **Scenario D**: Recovery variance across different times\n\n"
        
        report += "## Scenario Results\n\n"
        
        for scenario, result in self.results.items():
            report += f"### Scenario {scenario}: {result['name']}\n\n"
            report += f"**Status**: {result['status']}\n\n"
            
            if result.get('metrics'):
                metrics = result['metrics']
                report += "**Recovery Latency Statistics (SCION)**:\n"
                report += f"- Mean: {metrics.get('mean', 'N/A'):.3f}s\n"
                report += f"- p50: {metrics.get('p50', 'N/A'):.3f}s\n"
                report += f"- p95: {metrics.get('p95', 'N/A'):.3f}s\n"
                report += f"- p99: {metrics.get('p99', 'N/A'):.3f}s\n\n"
            
            report += "**Generated Outputs**:\n"
            scenario_prefix = f"scenario_{scenario.lower()}"
            report += f"- Packet loss timeline: `{scenario_prefix}_packet_loss.png`\n"
            report += f"- Recovery latency CDF: `{scenario_prefix}_recovery_cdf.png`\n"
            report += f"- AS class heatmap: `{scenario_prefix}_as_class_heatmap.png`\n"
            report += f"- Control plane timeline: `{scenario_prefix}_control_plane.png`\n"
            report += f"- Summary: `{scenario_prefix}_summary.md`\n\n"
        
        report += "## Analysis Methodology\n\n"
        report += "### Control Plane Analysis\n"
        report += "- Beacon propagation timeline\n"
        report += "- Path server segment registration events\n"
        report += "- Host cache update sequence\n"
        report += "- Convergence time estimation\n\n"
        
        report += "### Data Plane Analysis\n"
        report += "- Packet loss percentage (failed probes / total probes)\n"
        report += "- Outage duration (time to first successful probe)\n"
        report += "- Recovery latency percentiles (p50, p95, p99)\n"
        report += "- Stratification by AS class (core vs. non-core)\n\n"
        
        report += "## Key Findings\n\n"
        report += "*(To be populated with actual results from runs)*\n\n"
        
        report += "## Visualizations Generated\n\n"
        report += "1. **Packet Loss Timelines**: Show recovery pattern for each src-dst pair\n"
        report += "2. **Recovery Latency CDFs**: Compare recovery times across pairs\n"
        report += "3. **AS Class Heatmaps**: Impact of core vs. non-core classification\n"
        report += "4. **Control Plane Timelines**: Event sequence during convergence\n\n"
        
        report += "## Files and Scripts\n\n"
        report += "### Configuration Files\n"
        report += "- `configs/scenario_a_core_link_failure.yaml`\n"
        report += "- `configs/scenario_c_dual_link_failure.yaml`\n"
        report += "- `configs/scenario_d_recovery_variance.yaml`\n\n"
        
        report += "### Analysis Scripts\n"
        report += "- `utils/control_plane_log_parser.py`: Parse simulation logs\n"
        report += "- `utils/data_plane_analyzer.py`: Extract recovery metrics from probes\n"
        report += "- `utils/visualization_tools.py`: Generate comparison plots\n"
        report += "- `utils/comparative_analysis.py`: Orchestrate full pipeline\n\n"
        
        report += "### Running the Analysis\n"
        report += "```bash\n"
        report += "# Run all scenarios\n"
        report += "python3 utils/comparative_analysis.py\n\n"
        report += "# Run specific scenarios\n"
        report += "python3 utils/comparative_analysis.py --scenarios A,C\n\n"
        report += "# Specify output directory\n"
        report += "python3 utils/comparative_analysis.py --scenarios A,C,D --output-dir results\n"
        report += "```\n\n"
        
        with open(report_path, 'w') as f:
            f.write(report)
        
        print(f"\n{'='*70}")
        print(f"Final report saved to: {report_path}")
        print(f"{'='*70}\n")
    
    def print_quick_start_guide(self):
        """Print quick start guide."""
        guide = """
╔══════════════════════════════════════════════════════════════════════════════╗
║                    BGP vs SCION Comparison - Quick Start Guide               ║
╚══════════════════════════════════════════════════════════════════════════════╝

1. RUN INDIVIDUAL SCENARIOS
   ────────────────────────
   
   # Scenario A: Core link failure
   ./waf --run "scion configs/scenario_a_core_link_failure.yaml"
   
    # Scenario C: Dual failures
   ./waf --run "scion configs/scenario_c_dual_link_failure.yaml"
   
   # Scenario D: Recovery variance
   ./waf --run "scion configs/scenario_d_recovery_variance.yaml"

2. ANALYZE RESULTS
   ─────────────────
   
   # Parse control plane logs
   python3 utils/control_plane_log_parser.py build/scenario_a_control_plane.log
   
   # Analyze data plane metrics
   python3 utils/data_plane_analyzer.py build/scion_probe_*.csv
   
   # Generate visualizations
   python3 utils/visualization_tools.py scenario_a build/scion_probe_*.csv

3. FULL AUTOMATED PIPELINE
   ────────────────────────
   
   # Run and analyze all scenarios
   python3 utils/comparative_analysis.py
   
   # Run specific scenarios only
    python3 utils/comparative_analysis.py --scenarios A,C

4. OUTPUT FILES
   ─────────────
   
   For each scenario, you'll get:
   - *_packet_loss.png        : Packet loss over time
   - *_recovery_cdf.png       : Recovery latency distribution
   - *_as_class_heatmap.png   : AS class impact
   - *_control_plane.png      : Control plane events timeline
   - *_summary.md             : Results summary
   - COMPARISON_REPORT.md     : Final analysis report

5. EXPECTED OUTPUTS LOCATION
   ──────────────────────────
   
   build/scenario_a_*
   build/scenario_c_*
   build/scenario_d_*
   build/COMPARISON_REPORT.md

╚══════════════════════════════════════════════════════════════════════════════╝
        """
        print(guide)


def main():
    parser = argparse.ArgumentParser(
        description='BGP vs SCION Comparative Analysis Pipeline'
    )
    parser.add_argument(
        '--scenarios',
        default='A,C,D',
        help='Comma-separated list of scenarios to run (default: A,C,D)'
    )
    parser.add_argument(
        '--output-dir',
        default='build',
        help='Output directory for results (default: build)'
    )
    parser.add_argument(
        '--quick-start',
        action='store_true',
        help='Print quick start guide'
    )
    
    args = parser.parse_args()
    
    if args.quick_start:
        pipeline = ComparativeAnalysisPipeline()
        pipeline.print_quick_start_guide()
        return
    
    scenarios = args.scenarios.split(',')
    pipeline = ComparativeAnalysisPipeline(
        output_dir=args.output_dir,
        scenarios=scenarios
    )
    
    pipeline.run_all_scenarios()
    pipeline.generate_final_report()
    
    print("\n✓ Analysis complete!")


if __name__ == '__main__':
    main()
