This repository builds on the ns-3-scion simulation repository described in README_SCION.md and the ns3-bgp speaker application described in src/bgp/README_BGP.

It has been extended for simulation of inter-domain routing between satellite constellations. 
It has support for using generated topologies from orbital simulations and for using stochastic models for the dynamics of the inter-constellation satellite topology.
 
 # Quick Build and Run

This repository uses `waf` to build ns-3 and run simulations.

## Build

From the repository root:

```bash
CCFLAGS_EXTRA="-O3 -fopenmp -std=c++17" CXXFLAGS_EXTRA="-O3 -fopenmp -std=c++17" ./waf configure --enable-examples --enable-tests
./waf
```

## Run (example)

Run one of the SCION/BGP scenario programs:

```bash
./waf --run scion-ixp-integrated-scenario
```

Example of full pipeline:

export LD_LIBRARY_PATH=$PWD/build/lib

python3 utils/run_stochastic_interval_ixp_timing_sweep.py \
  --families splitsame splitvis \
  --scenarios visible \
  --protocols bgp scion \
  --seed-start 1 --seed-count 10 \
  --mrai-values 1 2 4 6 8 \
  --densities dense \
  --sim-time 5000 --warmup 60 \
  --output-subdir 2026-09-15/two_ixp_split

python3 utils/analyze_timing_sweep.py build/2026-09-15/two_ixp_split \
  --warmup 60 --out-csv build/2026-09-15/two_ixp_split/pair_metrics.csv

python3 utils/plot_pair_metrics.py build/2026-09-15/two_ixp_split/pair_metrics.csv \
  --out-dir build/2026-09-15/two_ixp_split/figs