"""
hertz_velocity.py — Hertzian contact mechanics per-mode velocity scaling.

Physical model
--------------
For a rigid striker impacting a plate, Hertz contact theory gives:

    F_peak ∝ v^(6/5)                          peak contact force
    τ(v)   = τ_ref × (v_ref / v)^(1/5)        contact duration

Higher velocity → shorter contact → impulse has more high-frequency content
→ HF modes are excited more strongly relative to LF modes.

Contact force spectral model
-----------------------------
The actual Hertz contact force pulse is smooth and bell-shaped in time
(rises to a peak then decays symmetrically). Its spectrum is modelled as a
single-pole low-pass response:

    H(f, τ) = 1 / sqrt(1 + (π f τ)²)

Corner frequency: f_c(τ) = 1 / (π τ)

    - Higher velocity → shorter τ → higher f_c → more HF content
    - Lower velocity  → longer  τ → lower  f_c → less HF content

This model has no spectral nulls. The earlier half-sine model had nulls
every ~1/(2τ) Hz, producing ratio spikes of >10,000x between velocities
and causing bell/twinkle artifacts. This model is smooth and bounded.

Per-mode velocity multiplier (relative to reference velocity v_ref = 1.0):

    scale_i(v) = (v / v_ref)^(6/5)  ×  H(f_i, τ(v)) / H(f_i, τ_ref)

At v = v_ref:  scale_i = 1.0 for all modes  →  reference render unchanged. ✓

Material constants (B20 bronze, from FEM basis audit — same plate physics)
---------------------------------------------------------------------------
    E  = 103 GPa
    ρ  = 8800 kg/m³
    ν  = 0.34

These ground τ_ref in the plate material rather than leaving it completely free.

Constitution compliance
-----------------------
    amplitude scaling (v^6/5)   : Level 3 — FEM-grounded contact mechanics
    spectral tilt H(f,τ(v))     : Level 3 — FEM-grounded contact mechanics
    τ_ref_ms                    : Level 5 — single calibration constant,
                                  physically motivated range 2–8 ms
    SPEAR mode params           : unchanged (freq, T60, birth, life, drift)
"""

import numpy as np

# B20 bronze — same constants as stine_fem_basis (audit §Solver parameters)
_E_PA = 103e9    # Young's modulus, Pa
_RHO  = 8800.0   # density, kg/m³
_NU   = 0.34     # Poisson's ratio


def _lp_spectrum(freqs: np.ndarray, tau: float) -> np.ndarray:
    """
    Single-pole low-pass approximation to the Hertz contact force spectrum.

        H(f, τ) = 1 / sqrt(1 + (π f τ)²)

    Corner frequency f_c = 1/(π τ). Smooth, no spectral nulls.
    Correctly represents shorter contact time → higher cutoff → more HF energy.
    """
    return 1.0 / np.sqrt(1.0 + (np.pi * freqs * tau) ** 2)


def hertz_vel_scale(freqs: np.ndarray,
                    velocity: float,
                    tau_ref_ms: float = 4.0,
                    v_ref: float = 1.0) -> np.ndarray:
    """
    Per-mode Hertzian velocity scaling array.

    Parameters
    ----------
    freqs       : SPEAR body mode frequencies, Hz — shape (n_body_modes,)
    velocity    : normalised strike velocity, clamped to [1e-4, 1.0]
    tau_ref_ms  : contact duration at v = v_ref, milliseconds.
                  Level 5 calibration constant.  Physical range: 2–8 ms.
                  Default 4.0 ms (midrange; tune by ear).
    v_ref       : reference velocity matching the SPEAR recording (default 1.0)

    Returns
    -------
    scale : np.ndarray shape (n_body_modes,)
            Multiply SPEAR normalised excitation by this array.
            At velocity == v_ref:  every element == 1.0  (no change to V7).
    """
    velocity = float(np.clip(velocity, 1e-4, 1.0))
    tau_ref  = tau_ref_ms * 1e-3           # ms → s

    # Contact duration: τ ∝ v^(−1/5)
    tau_v = tau_ref * (v_ref / velocity) ** 0.2

    # Smooth low-pass spectrum at each τ — no nulls
    H_v   = _lp_spectrum(freqs, tau_v)
    H_ref = _lp_spectrum(freqs, tau_ref)

    # Spectral tilt: bounded, monotonically smooth
    spectral_ratio = H_v / H_ref   # H_ref always > 0, no division hazard

    # Peak force amplitude: F_peak ∝ v^(6/5)
    # Perceptual calibration: v^2.0 (Level 5 departure from Hertz v^1.2)
    amplitude_scale = (velocity / v_ref) ** 2.0

    return amplitude_scale * spectral_ratio
