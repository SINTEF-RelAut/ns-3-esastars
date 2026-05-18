"""Stochastic path-latency model for LEO/MEO satellite links.

Models end-to-end path latency as a function of the inter-satellite distance D
and simulation time t.  The model draws a random hop-count from a Poisson
distribution whose rate is proportional to D, then accumulates per-hop delays
that include a slow sinusoidal variation (orbital geometry / load cycling) and
Gaussian jitter.

Distance between a LEO satellite and a MEO ground-station (or another orbital
shell) is sampled from the orbital geometry using randomised angular offsets.

Google-style docstrings are used throughout, per the project Python convention.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Optional, Sequence

import numpy as np


# ---------------------------------------------------------------------------
# Parameter container
# ---------------------------------------------------------------------------

@dataclass
class LatencyParams:
    """Default parameters for the stochastic latency model.

    Attributes:
        R_eff: Effective ISL reach [m].  Poisson rate is scaled so that a path
            spanning exactly R_eff has on average *alpha* hops.
        alpha: Routing-inefficiency factor (expected hops = alpha * D / R_eff).
        mu_link: Mean per-hop propagation + queuing delay [s].
        sigma_link: Per-hop delay jitter (1-sigma) [s].
        delta: Per-hop processing overhead [s], added deterministically each hop.
        beta: Amplitude of the sinusoidal variation (fractional, e.g. 0.08 = ±8 %).
        omega: Angular frequency of the periodic variation [rad/s].
    """

    R_eff: float = 1_400_000.0       # 1 400 km in metres
    alpha: float = 1.3
    mu_link: float = 5.5e-3          # 5.5 ms
    sigma_link: float = 1.2e-3       # 1.2 ms
    delta: float = 0.4e-3            # 0.4 ms
    beta: float = 0.08
    omega: float = 2.0 * math.pi / 1500.0   # 1 500-second cycle


# ---------------------------------------------------------------------------
# Distance model
# ---------------------------------------------------------------------------

def sample_distance(
    R_L: float,
    R_M: float,
    P: int,
    N_p: int,
    k: int,
    rng: Optional[np.random.Generator] = None,
) -> float:
    """Sample the chord distance between a LEO satellite and a MEO node.

    The angular separation Δ is built from two independent uniform random
    offsets:

    * φ_p  – inter-plane latitude offset,  Uniform(0, π / (2P))
    * θ_k  – in-plane longitude offset for the k-th satellite,
              Uniform((k-1)π / N_p, k π / N_p)

    The chord distance is then computed via the law of cosines:

        d = sqrt(R_L² + R_M² - 2 R_L R_M cos(Δ))

    Args:
        R_L: LEO orbital radius (from Earth centre) [m].
        R_M: MEO orbital radius (from Earth centre) [m].
        P: Number of orbital planes in the constellation.
        N_p: Number of satellites per orbital plane.
        k: Index of the satellite within its plane (1-based).
        rng: Optional NumPy random Generator for reproducibility.

    Returns:
        Chord distance in metres.
    """
    if rng is None:
        rng = np.random.default_rng()

    phi_p = rng.uniform(0.0, math.pi / (2.0 * P))
    theta_k = rng.uniform((k - 1) * math.pi / N_p, k * math.pi / N_p)

    delta_angle = math.sqrt(phi_p ** 2 + theta_k ** 2)

    d = math.sqrt(R_L ** 2 + R_M ** 2 - 2.0 * R_L * R_M * math.cos(delta_angle))
    return d


# ---------------------------------------------------------------------------
# Latency sampler
# ---------------------------------------------------------------------------

def sample_latency(
    D: float,
    t: float,
    params: LatencyParams = LatencyParams(),
    rng: Optional[np.random.Generator] = None,
) -> float:
    """Sample a single end-to-end path latency for distance D at time t.

    Algorithm:
        1. Draw hop-count H ~ Poisson(alpha * D / R_eff).
        2. For each hop accumulate:
           - a sinusoidally modulated Gaussian link delay, and
           - a fixed per-hop processing penalty (delta).

    Args:
        D: Path distance [m].
        t: Simulation time [s] (drives the periodic modulation).
        params: Model parameters; defaults to ``LatencyParams()``.
        rng: Optional NumPy random Generator for reproducibility.

    Returns:
        Sampled one-way latency in seconds.  Returns 0.0 when H == 0.
    """
    if rng is None:
        rng = np.random.default_rng()

    lam = params.alpha * (D / params.R_eff)
    H = int(rng.poisson(lam))

    if H == 0:
        return 0.0

    phases = rng.uniform(0.0, 2.0 * math.pi, size=H)
    periodic = params.beta * np.sin(params.omega * t + phases)   # shape (H,)
    link_means = params.mu_link * (1.0 + periodic)               # shape (H,)
    link_delays = rng.normal(link_means, params.sigma_link)       # shape (H,)

    total_latency = float(np.sum(link_delays)) + H * params.delta
    return total_latency


# ---------------------------------------------------------------------------
# Batch helper
# ---------------------------------------------------------------------------

def sample_latency_batch(
    D: float,
    times: Sequence[float],
    params: LatencyParams = LatencyParams(),
    seed: Optional[int] = None,
) -> np.ndarray:
    """Sample latencies for the same distance at multiple simulation times.

    Args:
        D: Path distance [m].
        times: Sequence of simulation times [s].
        params: Model parameters.
        seed: Optional integer seed for reproducibility.

    Returns:
        NumPy array of shape ``(len(times),)`` with sampled latencies [s].
    """
    rng = np.random.default_rng(seed)
    return np.array([sample_latency(D, float(t), params, rng) for t in times])
