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