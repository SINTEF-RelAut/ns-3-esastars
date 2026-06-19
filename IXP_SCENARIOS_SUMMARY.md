# IXP Simulation Scenarios and Analysis Summary

This document describes the implemented IXP (Internet Exchange Point) scenarios, including SCION and BGP simulations across direct, visible, and hidden failover topologies.

---

## Overview

We have implemented three distinct IXP architecture models and corresponding simulation scenarios:

1. **Direct Links** – ASes connected directly without an IXP intermediary
2. **Visible IXP (AS110)** – Dual links through a transparent central IXP node (AS110)
3. **Hidden IXP (Anycast-like)** – Virtual IXP fabric with hidden failover semantics

Each model supports both **SCION** (control-plane path-aware networking) and **BGP** (traditional routing) simulations. The simulations measure control-plane convergence (BGP state changes, updates, detection delays) and data-plane recovery (packet probe timeouts, outages, switchover delays).

---

## Simulation Scenarios

### 1. SCION Scenarios (YAML-based)

SCION scenarios are defined via topology XML files and event JSON schedules, run through the unified `scion` executor.

| Scenario | Config File | Topology | Events File | Description |
|----------|-----------|----------|---------|-----------|
| **Visible AS110** | `configs/scenario_ixp_as110_visible_failover.yaml` | `topology/ixp_as110_dual_links_topology.xml` | `user_defined_events/scenario_ixp_as110_visible_failover_events.json` | Dual satellite-to-IXP links fail sequentially; IXP visible; recovery expected when backup links come up |
| **Hidden Anycast** | `configs/scenario_ixp_anycast_hidden_failover.yaml` | `topology/ixp_as110_dual_links_topology.xml` | `user_defined_events/scenario_ixp_anycast_hidden_failover_events.json` | Virtual IXP fabric manages failover transparently; satellite links appear redundant but controlled by VIXP |
| **Relay Sequential** | `configs/scenario_ixp_relay_sequential_failover.yaml` | `topology/ixp_as110_dual_links_topology.xml` | `user_defined_events/scenario_ixp_relay_sequential_failover_events.json` | Sequential link failures with relay node behavior |
| **Satellite Sequential** | `configs/scenario_ixp_satellite_sequential_failover.yaml` | `topology/ixp_as110_dual_links_topology.xml` | `user_defined_events/scenario_ixp_satellite_sequential_failover_events.json` | Sequential failures from satellite AS perspective |

#### Running SCION Scenarios

```bash
./waf --run "scion configs/scenario_ixp_as110_visible_failover.yaml"
./waf --run "scion configs/scenario_ixp_anycast_hidden_failover.yaml"
```

**Output:** SCION path selection logs and SCION-internal state in `build/scenario_ixp_*.txt`

**Limitation:** SCION executor does not instantiate BGP applications, so no BGP control-plane logs are generated from SCION scenarios.

---

### 2. BGP Scenarios (Standalone C++ Binary)

Dedicated BGP simulations for the visible and hidden failover architectures, with full BGP control-plane instrumentation.

**Binary:** `scratch/bgp-ixp-scenarios.cc` → compiled to `build/scratch/bgp-ixp-scenarios`

| Scenario | Scenario ID | Topology | Description |
|----------|------------|----------|-----------|
| **Visible AS110** | `--scenario=visible` | Hardcoded in C++ (aligned to `ixp_as110_dual_links_topology.xml`) | 9 ASes (101–108, 110); AS110 is transparent IXP relay; dual links from each satellite to AS110; failures are interface flaps |
| **Hidden Anycast** | `--scenario=hidden` | Same topology | Virtual IXP fabric manages failover; failures appear as virtual-IXP state changes, not direct interface flaps |

#### BGP Scenario Command-Line Interface

```bash
./waf --run "bgp-ixp-scenarios \
  --scenario=visible \
  --outDir=build/bgp_ixp_as110_visible_failover \
  --clockInterval=1.0 \
  --simTime=260 \
  --virtualIxpPortCount=4 \
  --virtualIxpRebalance=1.0"
```

**Parameters:**
- `scenario`: `visible` or `hidden`
- `outDir`: Output directory (default: `build/bgp_ixp_scenarios`)
- `clockInterval`: BGP FSM clock interval in seconds (default: 1.0)
- `simTime`: Simulation duration (default: 260s)
- `virtualIxpPortCount`: VIXP port count (default: 4)
- `virtualIxpRebalance`: VIXP rebalance period (default: 1.0s)
- `virtualIxpHoldDown`: VIXP hold-down (default: 0.0s)
- `verbose`: Print logs (default: true)

#### Failure Schedule

Both visible and hidden scenarios follow the same failure schedule:

| Time (s) | Event | Affected Links |
|----------|-------|-----------------|
| 0.1 | Initial satellite-to-IXP link disable | 102-110 (1020002), 103-110 (1030002), 104-110 (1040002), 105-110 (1050002) |
| 50.0 | Second satellite-to-IXP link disable | 102-110 (1020001) — now both 102 links are down |
| 55.0 | First satellite-to-IXP link restore | 102-110 (1020002) comes back up |
| 120.0 | Direct core link disable | 101-102 direct link goes down |

#### BGP Scenario Outputs

```
build/bgp_ixp_as110_visible_failover/
├── bgp_cp_as101.csv          -- BGP control-plane log for AS101
├── bgp_cp_as102.csv          -- BGP control-plane log for AS102
├── ... (one per AS)
├── link_events.csv            -- Link up/down events (topology-wide)
├── probe_101_102.csv          -- UDP probe results (RTT, timeouts)
├── probe_102_103.csv          -- (one per probe source-destination pair)
├── ... (more probes)
├── metrics.csv                -- Per-probe-pair convergence metrics
├── cp_summary.csv             -- Control-plane message counts per failure window
├── bgp_data_plane_metrics.json -- Aggregate data-plane and CP statistics
└── summary.md                 -- Markdown summary table

build/bgp_ixp_anycast_hidden_failover/
├── (same output structure)
```

**CSV Schemas:**

- `bgp_cp_as*.csv`: `time_s, local_asn, peer_asn, event, detail`
  - Events: `OPEN`, `KEEPALIVE`, `UPDATE`, `NOTIFICATION`, `STATE_CHANGE`
  - Details: e.g., `ESTABLISHED->IDLE`, `announce`, `withdraw`

- `link_events.csv`: `time_s, link_status, asn_a, asn_b, link_index`
  - Link status: `link_up` or `link_down`

- `probe_*.csv`: `time_s, seq, kind, rtt_ms`
  - Kind: `sent`, `reply`, `timeout`

- `metrics.csv`: Per-probe-pair aggregates (switchover delay, outage, recovery, BGP detection delay, status)

- `cp_summary.csv`: Per-failure-window control-plane message counts (OPEN, UPDATE_announce, UPDATE_withdraw, STATE_CHANGE, NOTIFICATION)

---

## Code Runners and Orchestration

### 1. BGP IXP Scenario Runner

**File:** `utils/bgp_ixp_scenario_runner.py`

Orchestrates compilation, execution, and analysis for both visible and hidden BGP scenarios.

**Usage:**
```bash
python3 utils/bgp_ixp_scenario_runner.py \
  --scenarios visible hidden \
  --output-dir build \
  --clock-interval 1.0
```

**What it does:**
1. Invokes `./waf --run "bgp-ixp-scenarios ..."` for each scenario
2. Calls `bgp_convergence_analysis.py` to compute convergence metrics
3. Generates `summary.md` with data-plane and control-plane tables
4. Outputs JSON metrics and CSV analysis files

### 2. BGP Convergence Analysis

**File:** `utils/bgp_convergence_analysis.py`

Analyzes probe logs (`probe_*.csv`), link events (`link_events.csv`), and BGP control-plane logs (`bgp_cp_as*.csv`) to compute:

**Per-probe-pair per-failure metrics:**
- Switchover delay (time from link-down to first probe timeout)
- Recovery delay (time from link-up to first post-outage probe reply)
- Outage duration (time from first timeout to first post-outage reply)
- BGP detection delay (time from link-down to BGP session ESTABLISHED→IDLE)

**Aggregate statistics:**
- Min, median, mean, max, stdev of each metric
- Status classification: `no_loss`, `no_recovery`, `switched_and_recovered`

**Usage:**
```bash
python3 utils/bgp_convergence_analysis.py \
  build/bgp_ixp_as110_visible_failover \
  --topology topology/ixp_as110_dual_links_topology.xml \
  --metrics build/bgp_ixp_as110_visible_failover/metrics.csv \
  --cp-summary build/bgp_ixp_as110_visible_failover/cp_summary.csv \
  --json-output build/bgp_ixp_as110_visible_failover/bgp_data_plane_metrics.json
```

### 3. Other Analysis Scripts

| Script | Purpose |
|--------|---------|
| `utils/bgp_scenario_runner.py` | Original BGP scenario runner (baseline scenarios) |
| `utils/comparative_analysis.py` | Compare SCION vs BGP results side-by-side |
| `utils/compare_bgp_scion_results.py` | Comparative analysis utilities |
| `utils/recovery_metrics.py` | Generic convergence metrics extraction |
| `utils/data_plane_analyzer.py` | Data-plane-specific metric computation |
| `utils/control_plane_log_parser.py` | Parse BGP control-plane CSVs |
| `utils/plot_*.py` | Visualization scripts (convergence trends, timelines, sweeps) |

---

## Topology Files

### Topology Definition

**File:** `topology/ixp_as110_dual_links_topology.xml`

Defines:
- **9 ASes:** 101, 102, 103, 104, 105, 106, 107, 108, 110
- **AS110 type:** IXP relay (central Internet Exchange Point)
- **Satellite ASes:** 101–108 (customers/peers connecting through the IXP)
- **Link topology:**
  - Dual links: each satellite AS connects twice to AS110
  - Direct core links: 101-102, 101-103, 104-106, 105-106 (non-IXP paths)
  - Auxiliary links: 101-107, 106-108

Each satellite AS has:
- Two interfaces to AS110 (primary and backup)
- One or more direct core links to other satellites

---

## Event Definitions (User-Defined Events)

User-defined events control link up/down timing for SCION scenarios.

### Visible AS110 Scenario Events

**File:** `user_defined_events/scenario_ixp_as110_visible_failover_events.json`

```json
[
  { "time": 0.1, "type": "link_down", "as_a": 102, "as_b": 110, "link_index": 1 },
  { "time": 0.1, "type": "link_down", "as_a": 103, "as_b": 110, "link_index": 1 },
  { "time": 0.1, "type": "link_down", "as_a": 104, "as_b": 110, "link_index": 1 },
  { "time": 0.1, "type": "link_down", "as_a": 105, "as_b": 110, "link_index": 1 },
  { "time": 50.0, "type": "link_down", "as_a": 102, "as_b": 110, "link_index": 0 },
  { "time": 55.0, "type": "link_up", "as_a": 102, "as_b": 110, "link_index": 1 },
  { "time": 120.0, "type": "link_down", "as_a": 101, "as_b": 102, "link_index": 0 }
]
```

### Hidden Anycast Scenario Events

**File:** `user_defined_events/scenario_ixp_anycast_hidden_failover_events.json`

Similar structure, but failures are managed through the VirtualIxpFabric layer, appearing as satellite-side interface state changes rather than direct link disables.

---

## Key Implementation Details

### Virtual IXP Fabric (VIXP)

**Location:** `src/SCION/model/virtual-ixp-fabric.{h,cc}`

The Virtual IXP Fabric provides:
- Transparent failover semantics for "hidden" IXP scenarios
- Integration with BGP via `BgpIxpEndpointAdapter`

**Used by:**
- SCION scenarios via `user-defined-events.cc`
- BGP scenarios via `scratch/bgp-ixp-scenarios.cc`

### BGP Integration

**BGP Module:** `src/bgp/model/`

Key classes for IXP scenarios:
- `Bgp` – BGP application entity
- `Peer` – BGP peer configuration
- `BgpIxpEndpointAdapter` – Ties BGP peer state to VIXP link quality signals

**Control-plane logging:**
- `Bgp::SetControlPlaneOutput()` – Redirects BGP FSM events to CSV
- Per-AS logs: `bgp_cp_as<N>.csv` with timestamps, event types, and state transitions

### SCION Runtime

**Location:** `src/SCION/scion.cc`

Entry point for SCION simulations:
- Loads YAML config (topology, events, parameters)
- Instantiates ASes, links, and applications
- Schedules user-defined events
- **Does NOT** instantiate BGP applications (BGP logging must use standalone BGP binary)



## Quick Start

### Run Both BGP Scenarios with Analysis

```bash
cd /home/henrikl/ns-3-scion-org
python3 utils/bgp_ixp_scenario_runner.py --scenarios visible hidden --output-dir build --clock-interval 1.0
```

### Run a Single BGP Scenario Manually

```bash
./waf --run "bgp-ixp-scenarios --scenario=visible --outDir=build/my_test --simTime=260"
python3 utils/bgp_convergence_analysis.py build/my_test \
  --topology topology/ixp_as110_dual_links_topology.xml \
  --metrics build/my_test/metrics.csv \
  --json-output build/my_test/bgp_data_plane_metrics.json
```

### Run a SCION Scenario

```bash
./waf --run "scion configs/scenario_ixp_as110_visible_failover.yaml" 2>&1 | tail -50
```

---

## Summary of Artifacts

| Artifact | Type | Location | Purpose |
|----------|------|----------|---------|
| Topology XML | Definition | `topology/ixp_as110_dual_links_topology.xml` | AS and link definitions |
| SCION Config YAML | Config | `configs/scenario_ixp_*.yaml` | SCION simulation parameters |
| Event JSON | Events | `user_defined_events/scenario_ixp_*_events.json` | Link failure schedule |
| BGP Binary | Code | `scratch/bgp-ixp-scenarios.cc` | Standalone BGP scenario simulator |
| BGP Runner | Script | `utils/bgp_ixp_scenario_runner.py` | Orchestration & analysis runner |
| Analysis | Script | `utils/bgp_convergence_analysis.py` | Metrics computation |
| Output CSV | Data | `build/bgp_ixp_*/bgp_cp_as*.csv` | BGP control-plane logs |
| Output CSV | Data | `build/bgp_ixp_*/probe_*.csv` | Data-plane UDP probes |
| Output Markdown | Report | `build/bgp_ixp_*/summary.md` | Human-readable convergence report |

