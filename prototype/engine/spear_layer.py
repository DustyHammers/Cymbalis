"""
spear_layer.py — Steps 4 & 5: SPEAR analysis layer + calibration clusters.

Step 4: SpearLayer wraps the fingerprint as a read-only truth dataset.
        Does NOT affect audio. Provides structured access to SPEAR data.

Step 5: CalibrationClusters partitions modes into behavioral regions (offline only).
        Cluster JSON is inspectable; not used in audio until the router (Step 7).

Source authority (Constitution Article III / Article XV):
  All data here is Level 1 (direct SPEAR measurement).
  Clusters are Level 2 (statistical reconstruction traceable to Level 1).

Cluster definitions (from SPEAR birth-time statistics):
  attack_cluster : birth < 30ms  — 373 modes, full spectrum
  body_cluster   : 30ms <= birth < 300ms — 604 modes
  bloom_cluster  : birth >= 300ms — 923 modes, 483 of these >4kHz (sparkle region)
"""

import json
import numpy as np
from dataclasses import dataclass, asdict
from typing import List, Dict, Optional


@dataclass
class ModeCluster:
    """A named behavioral cluster of SPEAR-measured modes."""
    name: str
    mode_indices: List[int]       # indices into the full fingerprint
    freq_range: tuple             # (min_hz, max_hz)
    birth_range: tuple            # (min_s, max_s)
    n_modes: int
    mean_t60: float
    mean_exc: float
    hf_fraction: float            # fraction of modes > 4kHz


class SpearLayer:
    """
    Step 4: Read-only SPEAR truth dataset.
    
    Loads the fingerprint and exposes structured per-mode arrays.
    Never influences audio directly — supplies truth constraints only.
    
    Usage:
        spear = SpearLayer("stine_fingerprint.json")
        print(spear.n_modes, spear.freqs.min(), spear.freqs.max())
    """

    def __init__(self, fingerprint_path: str):
        with open(fingerprint_path) as fh:
            raw = json.load(fh)
        if isinstance(raw, dict):   # participation-format: root is dict with 'modes' key
            raw = raw['modes']

        self._raw = raw
        n = len(raw)

        # Level 1 arrays — immutable
        self.freqs  = np.array([m['f']     for m in raw])
        self.t60s   = np.array([m['t60']   for m in raw])
        self.excs   = np.array([m['exc']   for m in raw])
        self.drifts = np.array([m['drift'] for m in raw])
        self.births = np.array([m['birth'] for m in raw])
        self.lives  = np.array([m['life']  for m in raw])
        self.ids    = np.array([m['id']    for m in raw])

        self._n = n

    @property
    def n_modes(self) -> int:
        return self._n

    def modes_active_at(self, t: float) -> np.ndarray:
        """Return boolean mask of modes active at time t seconds."""
        born  = self.births <= t
        alive = (self.births + self.lives) >= t
        return born & alive

    def n_modes_active_at(self, t: float) -> int:
        return int(self.modes_active_at(t).sum())

    def summary(self) -> Dict:
        return {
            'n_modes': self._n,
            'freq_range': (float(self.freqs.min()), float(self.freqs.max())),
            't60_range':  (float(self.t60s.min()),  float(self.t60s.max())),
            'birth_range':(float(self.births.min()), float(self.births.max())),
            'modes_at_750ms': self.n_modes_active_at(0.75),
            'hf_modes_at_750ms': int(
                (self.modes_active_at(0.75) & (self.freqs > 4000)).sum()
            ),
        }


class CalibrationClusters:
    """
    Step 5: Offline partition of SPEAR modes into behavioral clusters.

    Clusters define ROUTING and BLENDING weights for the hybrid engine.
    Computed once offline; stored as JSON for inspection.
    Not used in audio until Step 7 (router).
    """

    CLUSTER_DEFS = {
        'attack': {'birth_max': 0.030},    # modes present at strike onset
        'body':   {'birth_min': 0.030, 'birth_max': 0.300},
        'bloom':  {'birth_min': 0.300},    # late-arrival sparkle modes
    }

    def __init__(self, spear: SpearLayer):
        self._spear = spear
        self.clusters: Dict[str, ModeCluster] = {}
        self._build_clusters()

    def _build_clusters(self):
        sp = self._spear
        for name, bounds in self.CLUSTER_DEFS.items():
            bmin = bounds.get('birth_min', 0.0)
            bmax = bounds.get('birth_max', 1e9)
            mask = (sp.births >= bmin) & (sp.births < bmax)
            idx  = np.where(mask)[0].tolist()
            if not idx:
                continue
            idx_arr = np.array(idx)
            f_sel = sp.freqs[idx_arr]
            self.clusters[name] = ModeCluster(
                name=name,
                mode_indices=idx,
                freq_range=(float(f_sel.min()), float(f_sel.max())),
                birth_range=(float(sp.births[idx_arr].min()),
                             float(sp.births[idx_arr].max())),
                n_modes=len(idx),
                mean_t60=float(sp.t60s[idx_arr].mean()),
                mean_exc=float(sp.excs[idx_arr].mean()),
                hf_fraction=float((f_sel > 4000).mean()),
            )

    def save_json(self, path: str):
        """Persist cluster definitions for inspection."""
        out = {}
        for name, c in self.clusters.items():
            d = asdict(c)
            d.pop('mode_indices')  # too large; reconstructable from bounds
            out[name] = d
        with open(path, 'w') as fh:
            json.dump(out, fh, indent=2)

    def cluster_confidence(self, t: float, cluster_name: str) -> float:
        """
        Confidence score [0,1] that cluster_name is the dominant behavioral
        regime at time t. Used by the router (Step 7).

        Simple time-based heuristic until a proper model is fitted:
          attack: confidence peaks at t=0, falls to 0 by 150ms
          body:   confidence peaks at t=150ms
          bloom:  confidence rises from 300ms onward
        """
        if cluster_name == 'attack':
            return float(np.clip(1.0 - t / 0.15, 0.0, 1.0))
        elif cluster_name == 'body':
            peak = np.exp(-0.5 * ((t - 0.15) / 0.15) ** 2)
            return float(np.clip(peak, 0.0, 1.0))
        elif cluster_name == 'bloom':
            return float(np.clip((t - 0.10) / 0.4, 0.0, 1.0))
        return 0.0

    def print_summary(self):
        sp = self._spear
        print(f"SpearLayer: {sp.n_modes} modes total")
        print(f"  Active at 750ms: {sp.n_modes_active_at(0.75)} "
              f"(HF: {int((sp.modes_active_at(0.75) & (sp.freqs>4000)).sum())})")
        for name, c in self.clusters.items():
            print(f"  [{name}] {c.n_modes} modes  "
                  f"birth={c.birth_range[0]:.3f}-{c.birth_range[1]:.3f}s  "
                  f"HF={c.hf_fraction*100:.0f}%  "
                  f"mean_t60={c.mean_t60:.2f}s")
