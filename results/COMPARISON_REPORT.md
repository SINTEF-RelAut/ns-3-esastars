# BGP vs SCION Comparative Analysis Report

Generated: 2026-03-26T14:13:50.365184

## Executive Summary

This report compares BGP and SCION protocols across four failure scenarios:
- **Scenario A**: Core link failure (AS101-AS102)
- **Scenario B**: Edge link failure (AS107-AS108)
- **Scenario C**: Dual simultaneous failures
- **Scenario D**: Recovery variance across different times

## Scenario Results

### Scenario A: Core Link Failure (AS101-AS102)

**Status**: completed

**Generated Outputs**:
- Packet loss timeline: `scenario_a_packet_loss.png`
- Recovery latency CDF: `scenario_a_recovery_cdf.png`
- AS class heatmap: `scenario_a_as_class_heatmap.png`
- Control plane timeline: `scenario_a_control_plane.png`
- Summary: `scenario_a_summary.md`

### Scenario B: Edge Link Failure (AS107-AS108)

**Status**: failed

**Generated Outputs**:
- Packet loss timeline: `scenario_b_packet_loss.png`
- Recovery latency CDF: `scenario_b_recovery_cdf.png`
- AS class heatmap: `scenario_b_as_class_heatmap.png`
- Control plane timeline: `scenario_b_control_plane.png`
- Summary: `scenario_b_summary.md`

### Scenario C: Dual Core Link Failure

**Status**: completed

**Generated Outputs**:
- Packet loss timeline: `scenario_c_packet_loss.png`
- Recovery latency CDF: `scenario_c_recovery_cdf.png`
- AS class heatmap: `scenario_c_as_class_heatmap.png`
- Control plane timeline: `scenario_c_control_plane.png`
- Summary: `scenario_c_summary.md`

### Scenario D: Recovery Latency Variance

**Status**: completed

**Generated Outputs**:
- Packet loss timeline: `scenario_d_packet_loss.png`
- Recovery latency CDF: `scenario_d_recovery_cdf.png`
- AS class heatmap: `scenario_d_as_class_heatmap.png`
- Control plane timeline: `scenario_d_control_plane.png`
- Summary: `scenario_d_summary.md`

## Analysis Methodology

### Control Plane Analysis
- Beacon propagation timeline
- Path server segment registration events
- Host cache update sequence
- Convergence time estimation

### Data Plane Analysis
- Packet loss percentage (failed probes / total probes)
- Outage duration (time to first successful probe)
- Recovery latency percentiles (p50, p95, p99)
- Stratification by AS class (core vs. non-core)

## Key Findings

*(To be populated with actual results from runs)*

## Visualizations Generated

1. **Packet Loss Timelines**: Show recovery pattern for each src-dst pair
2. **Recovery Latency CDFs**: Compare recovery times across pairs
3. **AS Class Heatmaps**: Impact of core vs. non-core classification
4. **Control Plane Timelines**: Event sequence during convergence

## Files and Scripts

### Configuration Files
- `configs/scenario_a_core_link_failure.yaml`
- `configs/scenario_b_edge_link_failure.yaml`
- `configs/scenario_c_dual_link_failure.yaml`
- `configs/scenario_d_recovery_variance.yaml`

### Analysis Scripts
- `utils/control_plane_log_parser.py`: Parse simulation logs
- `utils/data_plane_analyzer.py`: Extract recovery metrics from probes
- `utils/visualization_tools.py`: Generate comparison plots
- `utils/comparative_analysis.py`: Orchestrate full pipeline

### Running the Analysis
```bash
# Run all scenarios
python3 utils/comparative_analysis.py

# Run specific scenarios
python3 utils/comparative_analysis.py --scenarios A,B,C

# Specify output directory
python3 utils/comparative_analysis.py --scenarios A,B,C,D --output-dir results
```

