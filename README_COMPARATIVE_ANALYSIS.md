# BGP vs SCION Comparative Analysis Framework

## Overview

This framework provides a complete testing and analysis pipeline for comparing BGP and SCION protocols across four failure scenarios on an 8-AS network topology. The framework captures both **control plane behavior** (beacon propagation, path server updates) and **data plane metrics** (packet loss, recovery latency, convergence time).

## Test Scenarios

### Scenario A: Core Link Failure
- **Failure**: AS101-AS102 link down at t=50s for 30 seconds
- **Focus**: Impact on core-to-core connectivity
- **AS Classes Affected**: Primarily core ASes (101-104)
- **Configuration**: `configs/scenario_a_core_link_failure.yaml`
- **Expected Outcome**: SCION should show rapid convergence via beacon propagation; BGP may show longer convergence due to MRAI

### Scenario B: Edge Link Failure
- **Failure**: AS107-AS108 link down at t=50s for 30 seconds  
- **Focus**: Impact on customer (non-core) to customer connectivity
- **AS Classes Affected**: Non-core ASes (105-108)
- **Configuration**: `configs/scenario_b_edge_link_failure.yaml`
- **Expected Outcome**: SCION non-core hosts depend on beacon propagation from core; BGP similarly depends on RIB updates spreading

### Scenario C: Dual Link Failure
- **Failure**: AS101-AS102 AND AS101-AS103 links down simultaneously at t=50s for 30 seconds
- **Focus**: Multi-failure resilience and alternate path availability
- **AS Classes Affected**: All ASes (must use remaining core paths)
- **Configuration**: `configs/scenario_c_dual_link_failure.yaml`
- **Expected Outcome**: Tests availability of alternative paths; SCION's path diversity vs BGP's multi-path convergence

### Scenario D: Recovery Latency Variance Test
- **Failure**: AS101-AS102 link failure at THREE different times:
  - First failure: t=30s-60s
  - Second failure: t=120s-150s (after initial convergence)
  - Third failure: t=210s-240s (after full stability)
- **Focus**: Consistency of recovery behavior across protocol states
- **Configuration**: `configs/scenario_d_recovery_variance.yaml`
- **Expected Outcome**: SCION should show consistent latencies if beacon epoch-based; BGP may vary based on MRAI state

## Network Topology

The test topology uses 8 ASes in a single ISD:

```
        ┌─────────────────────────────────────┐
        │  CORE NETWORK (Tier 1)              │
        │  AS101 ─── AS102                    │
        │   │   \    /  │                     │
        │   │    \  /   │                     │
        │   │     \/    │                     │
        │  AS103 ─── AS104                    │
        └─────────────────────────────────────┘
              │              │
        ┌─────┴──────┐ ┌────┴──────┐
        │   REGION 1 │ │  REGION 2 │
        │            │ │           │
        │ AS105 ─ AS106│ ─ ? │ AS107 ─ AS108
        └────────────┘ └───────────┘
        
Core ASes:   101, 102, 103, 104
Non-Core:    105, 106, 107, 108
```

## Running Tests

### Quick Start (All Scenarios)

```bash
# Show the quick start guide
python3 utils/comparative_analysis.py --quick-start

# Run all scenarios automatically
python3 utils/comparative_analysis.py

# Run with custom output directory
python3 utils/comparative_analysis.py --output-dir results
```

### Run Individual Scenarios

```bash
# Scenario A: Core link failure
./waf --run "scion configs/scenario_a_core_link_failure.yaml"

# Scenario B: Edge link failure
./waf --run "scion configs/scenario_b_edge_link_failure.yaml"

# Scenario C: Dual failures
./waf --run "scion configs/scenario_c_dual_link_failure.yaml"

# Scenario D: Recovery variance
./waf --run "scion configs/scenario_d_recovery_variance.yaml"
```

### Run Specific Scenarios Only

```bash
# Only scenarios A and C
python3 utils/comparative_analysis.py --scenarios A,C

# Only scenario B
python3 utils/comparative_analysis.py --scenarios B
```

## Analysis Pipeline

### Step 1: Simulation

Each scenario runs as an NS3 SCION simulation:
- Duration: 200-300 seconds
- Probing: 600+ SCION probes per scenario
- Probes: All combinations of source-destination pairs across 8 ASes
- Logging: Control plane events, path updates, host cache changes

### Step 2: Control Plane Analysis

Parse simulation logs to extract:
- **Beacon timeline**: When beacons propagated through network
- **Path server updates**: When registrations changed
- **Host cache updates**: When segment caches were updated
- **Convergence time**: When control plane stabilized (no new updates for 5 seconds)

Run manually:
```bash
python3 utils/control_plane_log_parser.py build/scenario_a_control_plane.log
```

Output: `build/control_plane_events.csv` with timeline of all events

### Step 3: Data Plane Analysis

Analyze probe results from CSV:
- **Outage time**: Duration from failure to first successful probe
- **Packet loss**: Percentage of failed probes per source-destination pair
- **Recovery latency**: Histogram of recovery times across all pairs
- **AS class impact**: Group recovery metrics by core vs. non-core

Run manually:
```bash
python3 utils/data_plane_analyzer.py build/scion_probe_107_108.csv
```

Output: `build/data_plane_metrics.json` with recovery statistics

### Step 4: Visualization

Generate four comparison visualizations:

1. **Packet Loss Timeline** (`*_packet_loss.png`)
   - Shows packet loss % over time for each src-dst pair
   - Failure event marked with red dashed line
   - Square-wave pattern shows recovery

2. **Recovery Latency CDF** (`*_recovery_cdf.png`)
   - Cumulative distribution of recovery times
   - Shows p50, p95, p99 percentiles
   - Compare against BGP baseline

3. **AS Class Heatmap** (`*_as_class_heatmap.png`)
   - Grouped by core vs. non-core ASes
   - Shows mean recovery latency per class
   - Reveals AS role impact

4. **Control Plane Timeline** (`*_control_plane.png`)
   - Events per AS over time (Gantt-style)
   - Color-coded by event type (beaconing, path update, cache, etc.)
   - Shows propagation delay through network

Run visualization manually:
```bash
python3 utils/visualization_tools.py scenario_a build/scion_probe_*.csv \
  build/data_plane_metrics.json build/control_plane_events.csv
```

## Output Structure

```
build/
├── scenario_a_core_link_failure.txt              # Simulation output
├── scenario_a_control_plane.log                  # Control plane events
├── scenario_a_packet_loss.png                    # Visualization
├── scenario_a_recovery_cdf.png
├── scenario_a_as_class_heatmap.png
├── scenario_a_control_plane.png
├── scenario_a_summary.md                         # Text summary
│
├── scenario_b_edge_link_failure.txt
├── scenario_b_control_plane.log
├── scenario_b_packet_loss.png
├── scenario_b_recovery_cdf.png
├── scenario_b_as_class_heatmap.png
├── scenario_b_control_plane.png
├── scenario_b_summary.md
│
├── scenario_c_dual_link_failure.txt
├── [...]
│
├── scenario_d_recovery_variance.txt
├── [...]
│
├── COMPARISON_REPORT.md                          # Final analysis
├── scion_probe_101_102.csv                       # Raw probe results
├── scion_probe_101_103.csv
├── [... one CSV per pair ...]
│
├── data_plane_metrics.json                       # Parsed metrics
└── control_plane_events.csv                      # Event timeline
```

## Metrics Captured

### Control Plane

| Metric | Description | Unit |
|--------|-------------|------|
| Beacon Propagation Time | Time from beacon generation to reception | seconds |
| Path Server Registration Delay | Time from beacon arrival to path registration | seconds |
| Control Plane Convergence Time | Time from failure to last control plane event | seconds |
| Message Count | Total beacons, UPDATEs, or path segments | count |
| As-Hop Count | Average beacon path length | hops |

### Data Plane

| Metric | Description | Unit |
|--------|-------------|------|
| Outage Time | Duration of zero successful deliveries | seconds |
| Packet Loss | Failed probes / total probes × 100 | percent |
| Recovery Latency (p50) | 50th percentile recovery time | seconds |
| Recovery Latency (p95) | 95th percentile recovery time | seconds |
| Recovery Latency (p99) | 99th percentile recovery time | seconds |
| By-AS-Class Recovery | Recovery time grouped by core/non-core | seconds |

## Comparison with BGP

To compare with BGP:

1. **Separate simulations**: Run equivalent BGP scenario in parallel
   - Same topology
   - Same failure events
   - Same probing schedule
   - Same source-destination pairs

2. **Data collection**: Extract equivalent metrics from BGP logs
   - Convergence time (when routing table stabilizes)
   - Route flapping (path oscillation)
   - Recovery latency per pair

3. **Side-by-side comparison**: Use same visualization scripts
   - Read BGP metrics instead of SCION
   - Generate parallel visualizations
   - Create comparison tables

4. **Statistical comparison**:
   - Hypothesis test: Is mean recovery latency significantly different?
   - Effect size: How much faster/slower is one protocol?
   - Reliability: How consistent are results (variance)?

### Expected BGP Results

Typical BGP characteristics:
- Longer convergence time (exponential backoff, MRAI delays)
- Higher variance in recovery latency (depends on random MRAI timing)
- Fewer parallel paths during convergence
- Slower non-core AS convergence (IBGP/EBGP dependency)

## Configuration Reference

### scenario_*.yaml Structure

```yaml
time_resolution: NS                    # Simulation time unit
topology: topology/topology.xml        # Network topology file
output: build/scenario_X.txt           # Main output file
simulation_duration: 200s              # Total simulation time

events_file: user_defined_events/scenario_X_events.json

beacon_service:
  policy: baseline
  period: 10s                          # Beacon generation interval
  first_beaconing: 0s
  last_beaconing: 200s

control_plane_logging:                 # Enable for detailed logs
  beacon_events: true
  path_server_updates: true
  log_file: build/scenario_X_control_plane.log

path_snapshot_logger:
  period: 2s                           # Frequency of path snapshots
  output: build/scenario_X_paths.csv
  max_paths_per_dst: 5

data_plane_probing:
  start: 10s                           # When probing begins
  period: 1s                           # Probe interval
  timeout: 2s
  count: 180                           # Number of probes per pair
  pairs:                               # List of source-destination pairs
    - src_as: 101
      dst_as: 102
      src_host: 2
      dst_host: 2
```

### Scenario Events JSON

```json
{
  "events": [
    {
      "time": "50s",
      "type": "link_down",
      "args": ["1", "101", "1010000"],
      "description": "Link AS101-AS102 down"
    },
    {
      "time": "80s",
      "type": "link_up",
      "args": ["1", "101", "1010000"],
      "description": "Link AS101-AS102 recovered"
    }
  ]
}
```

## Interpreting Results

### Example: Scenario A Results

**Expected SCION behavior**:
- Beacon arrives at AS102 within ~100-200ms
- Path server receives beacon and registers segment: ~50-100ms
- Host receives registered segment within 1-2 beaconing periods: ~10-20s
- New paths composed and probes succeed: ~20-30s outage
- **Total convergence**: ~20-30 seconds

**Expected BGP behavior**:
- AS102 detects failure via BFD/interface monitoring: ~50-100ms
- Sendes BGP WITHDRAW message: immediate
- AS101 receives and computes new path: ~100-200ms
- MRAI timer fires (reduces oscillation): 0-30s random delay
- AS101 sends UPDATE with new path: ~5-10s total
- Host routing table updates and probes succeed: ~30-60s outage
- **Total convergence**: ~30-60+ seconds (MRAI dependent)

**Comparison**:
- SCION: ~20-30s, p99 < 40s
- BGP: ~30-60s, p99 > 100s (due to MRAI variance)
- **Winner**: SCION (1.5-2x faster)

### Red Flags

Look for these issues in results:

1. **No recovery** (p99 = ∞): Check simulation completed, probes ran
2. **All success** (loss = 0%): May indicate probes didn't run during failure
3. **Very long convergence** (>180s): Check beacon settings, network is very large
4. **Huge variance** (p99 >> p50): Possible async effects, BGP MRAI randomization

## Troubleshooting

### Simulation Failed
```bash
# Check if config file exists
ls -la configs/scenario_a_core_link_failure.yaml

# Check if topology and events exist
ls -la topology/manual_link_fail_6as_topology.xml
ls -la user_defined_events/scenario_a_core_link_failure_events.json

# Try rebuilding
./waf clean
./waf
```

### No Probe Results
```bash
# Check if probes ran
ls -la build/scion_probe_*.csv

# Check simulation output for errors
tail -100 build/scenario_a_core_link_failure.txt
```

### Visualization Scripts Error
```bash
# Install dependencies
pip install matplotlib seaborn numpy

# Run with verbose output
python3 -u utils/visualization_tools.py scenario_a build/scion_probe_*.csv 2>&1
```

## Advanced Usage

### Custom AS Classes

Edit `utils/comparative_analysis.py`:
```python
AS_CLASS_MAP = {
    101: 'core',      # Edit classifications here
    102: 'core',
    105: 'non-core',
    # ...
}
```

### Modify Failure Times

Edit scenario JSON file to change when failures occur:
```json
{
  "time": "100s",           # Change to different time
  "type": "link_down",
  "args": ["1", "101", "1010000"]
}
```

### Change Beacon Period

Edit scenario YAML:
```yaml
beacon_service:
  period: 5s              # Faster convergence testing
```

### Add Custom Probe Pairs

Edit scenario YAML:
```yaml
data_plane_probing:
  pairs:
    - src_as: 101
      dst_as: 108         # Add this pair
```

## References

- **Test scenarios proposal**: See `./BGP_vs_SCION_TEST_PROPOSAL.md`
- **SCION architecture**: [SCION official documentation](https://www.scion-architecture.net/)
- **BGP convergence**: RFC 4271, RFC 6811
- **Network simulation**: [NS3 documentation](https://www.nsnam.org/)

## Questions?

For issues or enhancements:
1. Check simulation output logs
2. Review generated visualizations
3. Compare metrics with expected values
4. Check GitHub issues / project documentation
