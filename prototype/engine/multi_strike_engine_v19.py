"""
multi_strike_engine_v10alpha.py
Engine V10 Alpha — Extended with edge articulation (Stine Baseline 3).

Architecture
------------
BODY PATH    : 977 cosine oscillators from MAIN SPEAR (birth < 0.30s)
               LFO wander active (SPEAR drift magnitude, random LFO phase)
               T60 exponential decay (original caps)
               FEM contact damping (position-dependent)
               Edge excitation weighting (position-dependent, Baseline 3+)
               Contact duration filter (sinc, tau=0 neutral)

BLOOM PATH   : 2799 HF cosine oscillators from New SPEAR (>4kHz, top 80% energy)
               Birth-time delayed activation (t_strike + birth_i)
               T60 exponential decay (original caps)
               Bloom excitation is position-invariant (no edge ratio data)

SPARKLE      : Cluster-level Hann gain on body modes only

Baselines:
  Baseline 2 : Stine_Baseline2.json  — edge_ratios_path=None, all weights=1.0
  Baseline 3 : Stine_Baseline3.json  — edge_ratios_path set, per-mode position weighting

Backward compatibility:
  edge_ratios_path=None (default) → _edge_weight all-ones → identical to locked Baseline 2.

Constitution compliance:
  Body/bloom synthesis    : Level 1/2 (SPEAR-derived)
  Edge excitation weights : Level 1  (matched modes, freq >= 500Hz)
                            Level 4  (band fallbacks — unmatched / <500Hz)
  T60 caps                : Level 5
  LFO wander              : Level 5

Band fallback provenance (Level 4 — labeled per Article X):
  <500Hz    : 1.0    dominant body cluster; peak in both recordings, norm ratio ≈ 1.0
  500-1kHz  : 1.742  band median from ratio table (n=1, low confidence)
  1-4kHz    : 2.679  band median from ratio table (n=15, usable)
  4-10kHz   : 1.036  band median from ratio table (n=60, good coverage)
  >10kHz    : 1.0    no data
"""

import numpy as np
import json
from dataclasses import dataclass
from typing import List, Tuple, Optional, Dict

from spear_layer import SpearLayer
from hertz_velocity import hertz_vel_scale
from dsp_filters import _hp, _bp

SR      = 44100
LOG1000 = np.log(1000.0)
_BIRTH_GATE = 0.30

_T60_CAP_F   = np.array([200., 1000., 4000., 10000., 20000.])
_T60_CAP_VAL = np.array([12.,    6.,    3.,     1.6,    1.0])

_EDGE_BAND_FALLBACK = {
    'lt500':  1.0,
    'b500':   1.742,
    'b1000':  2.6794,
    'b4000':  1.0356,
    'gt10k':  1.0,
}


@dataclass
class StrikeEvent:
    time:     float
    velocity: float
    position: float


class MultiStrikeEngineV16:

    DEFAULT_PARAMS: Dict = dict(
        tau_ref_ms=0.2, wander_rate_hz=1.5, wander_depth=1.0,
        alpha_contact=0.3, contact_tau_ms=0.0,
        hf_bloom_gain=2.0, sparkle_rate=6.0, sparkle_depth=0.35, sparkle_dur_ms=25.0,
        articulation='stick', brush_strikes=18, brush_dur_ms=35.0,
        brush_pos_std=0.015, brush_vel_scale=0.30,
        stick_gain=0.0, stick_hp=4800, atk_noise=0.0, atk_len=0.008, atk_dec=0.002,
        t60_scale=1.0, seed=5,
        hammering_tilt=0.0,
        # V14g: contact force model.
        # tip_contact_ms = 0  →  V12 instantaneous (bit-identical).
        tip_contact_ms=0.0,
        shld_contact_ms=1.2,
        # Hertz mass-spring contact: force emerges from stick/cymbal dynamics
        hertz_contact=False,
        m_stick=0.001,
        k_stiff=100.0,
        feedback_scale=0.01,
        v_scale=0.1,
        max_contact_samples=500,
        # Per-mode contact: each mode gets its own Gaussian force envelope
        # based on bending wave group velocity (v_g ∝ √f).
        # HF modes peak early (short travel time), LF modes peak late.
        # DC-normalised per mode → body ring = V12, only timing differs.
        permode_contact=False,
        tip_tau0=3.0,      # HF peak sample at bow/tip
        tip_tau_max=10.0,  # LF peak sample at bow/tip
        shld_tau0=10.0,    # HF peak sample at edge/shoulder
        shld_tau_max=35.0, # LF peak sample at edge/shoulder
        sigma_ratio=0.35,  # Gaussian width = sigma_ratio × peak_time
    )

    DEFAULT_POSITIONS: Dict = {
        'edge': (0.230, 0.0),
        'bow':  (0.127, 0.0),
        'bell': (0.040, 0.0),
    }

    def __init__(self, spear,
                 hf_bloom_path=None, cluster_path=None, edge_ratios_path=None, bell_ratios_path=None,
                 vel_participation_path=None,
                 bell_vel_participation_path=None,
                 positions=None, **overrides):
        self._spear     = spear
        self._params    = {**self.DEFAULT_PARAMS, **overrides}
        self._positions = positions or self.DEFAULT_POSITIONS

        self._clusters = []
        if cluster_path:
            with open(cluster_path) as f:
                self._clusters = json.load(f).get('clusters', [])
        self._sparkle_events = []

        p = self._params

        _birth_gate    = float(p.get('birth_gate_s', _BIRTH_GATE))
        body_mask      = spear.births < _birth_gate
        self._bF       = spear.freqs[body_mask]
        self._bD       = spear.drifts[body_mask]
        raw_t60        = spear.t60s[body_mask] * p['t60_scale']
        caps           = np.interp(self._bF, _T60_CAP_F, _T60_CAP_VAL)
        self._bT       = np.maximum(np.minimum(raw_t60, caps), 0.02)
        exc            = spear.excs[body_mask]
        self._exc_norm = exc / (exc.max() + 1e-9)
        # Hammering tilt: boosts low-mid excitation, reduces HF — approximates
        # spectral redistribution from dense hand hammering (Level 5, labeled).
        # f_pivot=4000Hz: below = boost, above = cut. Default 0.0 = neutral.
        _htilt = float(p.get('hammering_tilt', 0.0))
        if _htilt != 0.0:
            _tilt = np.clip(1.0 + _htilt * (4000.0 - self._bF) / 4000.0, 0.7, 1.3)
            self._exc_norm *= _tilt
        # Hammering spectral tilt (Level 5) — approximates dense hand-hammering energy
        # redistribution: boosts 1-3kHz modes, reduces >4kHz. f_pivot=4000Hz.
        # Default 0.0 = neutral. Rollback: set hammering_tilt to 0.0 or remove line.
        _h_tilt = float(p.get('hammering_tilt', 0.0))
        if _h_tilt > 0.0:
            _tilt_curve = np.clip(1.0 + _h_tilt * (4000.0 - self._bF) / 4000.0, 0.7, 1.4)
            self._exc_norm *= _tilt_curve
        # Velocity participation curves (per-mode amplitude vs velocity)
        self._vel_curves = None   # (n_body, n_vel) — set below if provided
        self._vel_norms  = None   # (n_vel,) normalised velocity values

        if vel_participation_path:
            with open(vel_participation_path) as _f:
                _vp = json.load(_f)
            self._vel_norms = np.array(_vp['vel_norms'], dtype=np.float64)
            # Extract vel_curves ordered same as body modes
            _vp_modes = _vp['modes']
            # Map fingerprint body-mode ids to participation indices
            _id_to_vc = {int(m['id']): m.get('vel_curve', None)
                         for m in _vp_modes}
            _bids = spear.ids[body_mask]
            _n_vel = len(self._vel_norms)
            _vc_arr = np.ones((len(self._bF), _n_vel), dtype=np.float64)
            for _i, _bid in enumerate(_bids):
                _vc = _id_to_vc.get(int(_bid))
                if _vc is not None:
                    _vc_arr[_i] = np.array(_vc[:_n_vel], dtype=np.float64)
            self._vel_curves = _vc_arr

        # Bell-specific velocity participation (Level 1 — Bell-1/Bell-2 SPEAR)
        # When bell_frac>0, blend bell_vel_curves into vel in _apply_strike.
        self._bell_vel_curves = None
        self._bell_vel_norms  = None
        if bell_vel_participation_path:
            with open(bell_vel_participation_path) as _f:
                _bvp = json.load(_f)
            self._bell_vel_norms = np.array(_bvp['vel_norms'], dtype=np.float64)
            _bvp_modes = _bvp['modes']
            _bid_to_vc = {int(m['id']): m.get('vel_curve', None) for m in _bvp_modes}
            _bids2 = spear.ids[body_mask]
            _n_bvel = len(self._bell_vel_norms)
            _bvc_arr = np.zeros((len(self._bF), _n_bvel), dtype=np.float64)
            for _i, _bid in enumerate(_bids2):
                _vc = _bid_to_vc.get(int(_bid))
                if _vc is not None:
                    _bvc_arr[_i] = np.array(_vc[:_n_bvel], dtype=np.float64)
            self._bell_vel_curves = _bvc_arr

        n_body         = len(self._bF)
        self._state    = np.zeros(n_body, dtype=np.complex128)
        self._wander_scale = 1.0   # velocity+position-dependent wander depth; reset per strike
        self._wash_damp   = 1.0   # velocity-dependent T60 scaling (vel_len_tilt)
        self._wander_rate_scale = 1.0  # same for LFO rate
        self._mf_S         = 0.0
        self._mf_t_last    = 0.0
        self._mf_scale_now = 1.0

        rng0 = np.random.default_rng(p['seed'])
        self._pick_L = rng0.uniform(0.6,1.0,n_body)*rng0.choice([-1.,1.],n_body)
        self._pick_R = rng0.uniform(0.6,1.0,n_body)*rng0.choice([-1.,1.],n_body)

        wander_rng       = np.random.default_rng(p['seed'] + 0xA2A2A2)
        self._lfo_rates  = wander_rng.uniform(0.7,1.3,n_body) * p['wander_rate_hz']
        self._lfo_phases = wander_rng.uniform(0.0,2.0*np.pi,n_body)

        # Edge and bell excitation weight arrays
        self._edge_weight = np.ones(n_body, dtype=np.float64)
        self._bell_weight = np.ones(n_body, dtype=np.float64)

        if edge_ratios_path:
            with open(edge_ratios_path) as f:
                ratio_data = json.load(f)
            ratio_lookup = {m['fp_id']: float(m['ratio_norm'])
                            for m in ratio_data['modes']}
            body_fp_ids = spear.ids[body_mask]

            for i in range(n_body):
                fp_id_i = int(body_fp_ids[i])
                freq_i  = float(self._bF[i])
                if fp_id_i in ratio_lookup and freq_i >= 500.0:
                    self._edge_weight[i] = ratio_lookup[fp_id_i]
                else:
                    if freq_i < 500.0:
                        self._edge_weight[i] = _EDGE_BAND_FALLBACK['lt500']
                    elif freq_i < 1000.0:
                        self._edge_weight[i] = _EDGE_BAND_FALLBACK['b500']
                    elif freq_i < 4000.0:
                        self._edge_weight[i] = _EDGE_BAND_FALLBACK['b1000']
                    elif freq_i < 10000.0:
                        self._edge_weight[i] = _EDGE_BAND_FALLBACK['b4000']
                    else:
                        self._edge_weight[i] = _EDGE_BAND_FALLBACK['gt10k']

        # Bell excitation weight array (Level 1 — SPEAR bell/bow ratio)
        if bell_ratios_path:
            with open(bell_ratios_path) as _f:
                _bell_data = json.load(_f)
            _bell_lookup = {m['fp_id']: float(m['ratio_norm'])
                            for m in _bell_data['modes']}
            _bfids = spear.ids[body_mask]
            for _i in range(n_body):
                if int(_bfids[_i]) in _bell_lookup:
                    self._bell_weight[_i] = _bell_lookup[int(_bfids[_i])]

        # HF bloom modes
        self._hf_n = 0
        self._hf_state    = np.zeros(0, dtype=np.complex128)
        self._hf_act_queue = []
        if hf_bloom_path:
            with open(hf_bloom_path) as f:
                hf_data = json.load(f)
            modes       = hf_data['modes']
            global_peak = float(hf_data['global_peak_amp'])
            self._hf_n  = len(modes)
            self._hfF     = np.array([m['f']       for m in modes])
            self._hfT     = np.array([m['t60']      for m in modes])
            self._hfBirth = np.array([m['birth']    for m in modes])
            self._hfExc   = np.array([m['peak_amp'] for m in modes])/(global_peak+1e-12)
            # Zero bloom 4800-6500Hz — tinnitus artifact, likely above 5500Hz
            self._hfExc[(self._hfF >= 4800.0) & (self._hfF <= 6500.0)] = 0.0
            hf_rng = np.random.default_rng(p['seed']+0xC0C0C0)
            self._hf_pick_L = hf_rng.uniform(0.5,1.0,self._hf_n)*hf_rng.choice([-1.,1.],self._hf_n)
            self._hf_pick_R = hf_rng.uniform(0.5,1.0,self._hf_n)*hf_rng.choice([-1.,1.],self._hf_n)
            hf_w = np.random.default_rng(p['seed']+0xD1D1D1)
            self._hf_lfo_rates  = hf_w.uniform(0.5,1.5,self._hf_n)*p['wander_rate_hz']
            self._hf_lfo_phases = hf_w.uniform(0.0,2.0*np.pi,self._hf_n)
            self._hf_state      = np.zeros(self._hf_n, dtype=np.complex128)

        # V14g: per-sample mode step for contact force integration loop.
        self._mode_step = (np.exp(1j * 2.0 * np.pi * self._bF / SR)
                           * np.exp(-LOG1000 / (self._bT * SR)))
        self._active_contact    = None

        # V19: per-mode onset ramp from SPEAR pk_t data
        self._beo_pk_t      = None
        self._beo_t_strike  = None
        self._beo_birth_frac = 0.3

        _beo_path = p.get('band_env_onset_path', None)
        if _beo_path:
            with open(_beo_path) as _f: _d = json.load(_f)
            if 'edge_by_vel' in _d:
                # Bettis-style: velocity-bracketed edge pk_t
                _bow_pk      = np.array(_d['bow'], dtype=np.float64)
                _edge_vs     = np.array(_d['edge_velocities'], dtype=np.float64)
                _edge_by_vel = np.array([_d['edge_by_vel'][str(v)]
                                         for v in _d['edge_velocities']], dtype=np.float64)
                self._beo_pk_t   = (_bow_pk, _edge_by_vel, _edge_vs)  # (bow, n_vel×n_modes, vels)
                self._beo_birth_frac = float(p.get('band_env_birth_frac', 0.4))
            elif 'mode_pk_t' in _d:
                # Stine/Nova-style: single bow + edge array
                _bow_pk  = np.array(_d['mode_pk_t']['bow'],  dtype=np.float64)
                _edge_pk = np.array(_d['mode_pk_t']['edge'], dtype=np.float64)
                self._beo_pk_t   = (_bow_pk, _edge_pk, None)  # None = no velocity bracketing
                self._beo_birth_frac = float(p.get('band_env_birth_frac', 0.5))

        # Bettis-style edge band envelope onset (velocity-bracketed, band-level)
        self._edge_benv      = None   # (n_vel, n_frames, N_BANDS) normalized
        self._edge_benv_vels = None   # (n_vel,)
        self._edge_benv_fdur = None
        self._edge_benv_mband= None   # (n_modes,) band index

        _ebenv_path = p.get('edge_band_env_path', None)
        if _ebenv_path:
            with open(_ebenv_path) as _f: _ed = json.load(_f)
            _eb_vels  = np.array(_ed['velocities'], dtype=np.float64)
            _eb_envs  = np.array(_ed['envelopes'],  dtype=np.float64)  # (n_vel, n_frames, N_BANDS)
            _eb_edges = _ed['band_edges_hz']
            _eb_NB    = len(_eb_edges) - 1
            _eb_fdur  = float(_ed['frame_dur_s'])
            _eb_mband = np.zeros(len(self._bF), dtype=np.int32)
            for _i, _f in enumerate(self._bF):
                _eb_mband[_i] = _eb_NB - 1
                for _b in range(_eb_NB):
                    if _eb_edges[_b] <= _f < _eb_edges[_b+1]:
                        _eb_mband[_i] = _b; break
            self._edge_benv       = _eb_envs
            self._edge_benv_vels  = _eb_vels
            self._edge_benv_fdur  = _eb_fdur
            self._edge_benv_mband = _eb_mband

        # NPZ-derived edge: Bettis-tuned velw + 5-band envelopes — overrides edge_band_env
        self._edge_npz = None
        _enpz_path = p.get('edge_npz_path', None)
        if _enpz_path:
            with open(_enpz_path) as _f: _nd = json.load(_f)
            _evels = np.array(_nd['velocities'], dtype=np.float64)
            _eW    = np.array([_nd['velw'][str(v)] for v in _nd['velocities']],
                               dtype=np.float64)              # (5, n_modes)
            _raws  = [_nd['envelopes'][str(v)] for v in _nd['velocities']]
            _nfr   = max(len(e) for e in _raws)
            _nbd   = len(_raws[0][0])
            _eE    = np.zeros((len(_evels), _nfr, _nbd), dtype=np.float64)
            for _vi, _env in enumerate(_raws):
                _arr = np.array(_env, dtype=np.float64)
                _eE[_vi, :len(_arr)] = _arr                   # (5, n_frames, 5)
            _eBand = np.array(_nd['our_band_idx'], dtype=np.int32)
            self._edge_npz = (_evels, _eW, _eE, _eBand, float(_nd['frame_dur_s']))
        self._lf_t_last    = 0.0
        self._lf_scale_now = 1.0   # current hit's LF scale, set per-hit in render()
        self._lf_cutoff_hz = float(p.get('lf_cutoff_hz',    2000.0))
        self._lf_depth     = float(p.get('lf_sens_depth',   0.0))
        self._lf_tau       = float(p.get('lf_sens_tau',     5.0))
        self._lf_min       = float(p.get('lf_min_scale',    0.3))
        self._lf_max       = float(p.get('lf_max_scale',    1.0))  # >1.0 = boost mode
        self._lf_w_edge    = float(p.get('lf_edge_weight',  1.0))
        self._lf_w_bow     = float(p.get('lf_bow_weight',   0.5))
        self._lf_w_bell    = float(p.get('lf_bell_weight',  0.1))

        # MF energy transfer — parallel to lf_S, bandpass-shaped (500–4kHz body)
        self._mf_depth     = float(p.get('mf_sens_depth',   0.0))
        self._mf_tau       = float(p.get('mf_sens_tau',     5.0))
        self._mf_min       = float(p.get('mf_min_scale',    0.3))
        self._mf_max       = float(p.get('mf_max_scale',    1.0))
        self._mf_w_edge    = float(p.get('mf_edge_weight',  1.0))
        self._mf_w_bow     = float(p.get('mf_bow_weight',   0.5))
        self._mf_w_bell    = float(p.get('mf_bell_weight',  0.1))
        self._mf_lo_hz     = float(p.get('mf_lo_hz',        500.0))
        self._mf_hi_hz     = float(p.get('mf_hi_hz',       4000.0))

        # Bettis-derived velocity spectral tilt
        # At strike: coupling *= per-mode tilt derived from Bettis vel curves.
        # Fully data-derived: shape, magnitudes from Bettis V1-V5 SPEAR captures.
        self._vel_tilt = None
        _vt_path = p.get('vel_tilt_path', None)
        if _vt_path:
            with open(_vt_path) as _f: _vd = json.load(_f)
            _vt_vels    = np.array(_vd['velocities'], dtype=np.float64)
            _vt_edges   = _vd['band_edges_hz']          # [0,500,2000,8000,99999]
            _vt_ref_vel = float(_vd['reference_velocity'])
            _NVB        = len(_vt_edges) - 1
            # Per-band fraction arrays (n_vel, n_bands)
            _vt_fracs   = np.array([_vd['fractions'][str(v)] for v in _vt_vels])
            # Reference fractions at vel=1.0
            _ref_idx    = int(np.argmin(np.abs(_vt_vels - _vt_ref_vel)))
            _vt_ref     = _vt_fracs[_ref_idx]          # (n_bands,)
            # Per-mode 4-band index
            _vt_mband   = np.full(len(self._bF), _NVB-1, dtype=np.int32)
            for _i, _f in enumerate(self._bF):
                for _b in range(_NVB):
                    if _vt_edges[_b] <= _f < _vt_edges[_b+1]:
                        _vt_mband[_i] = _b; break
            self._vel_tilt = (_vt_vels, _vt_fracs, _vt_ref, _vt_mband, _NVB)
        # sigmoid_val[i] = 1/(1+exp(-(f_i - cutoff)/200)) — HF fraction
        # At render time: mode_scale = lf_scale + (1-lf_scale)*sigmoid_val
        if self._lf_depth > 0.0:
            self._lf_sigmoid = 1.0 / (1.0 + np.exp(
                -(self._bF - self._lf_cutoff_hz) / 200.0))
        else:
            self._lf_sigmoid = None

        # MF bandpass sigmoid: rises at mf_lo_hz, falls at mf_hi_hz
        if self._mf_depth > 0.0:
            _mf_lo = 1.0 / (1.0 + np.exp(-(self._bF - self._mf_lo_hz) / 200.0))
            _mf_hi = 1.0 / (1.0 + np.exp(-(self._bF - self._mf_hi_hz) / 200.0))
            self._mf_bandpass = _mf_lo * (1.0 - _mf_hi)
        else:
            self._mf_bandpass = None

        # ── Nonlinear modal redistribution — stateful control signal ─────────
        # nlState is a dimensionless 0–1 control. Energy never leaves LF+MF+HF.
        # nl_amount=0.0 → full bypass.
        self._nl_amount    = float(p.get('nl_amount',    0.0))
        self._nl_threshold = float(p.get('nl_threshold', 0.55))
        self._nl_atk_tau   = float(p.get('nl_attack_ms',  100.0)) * 0.001
        self._nl_rel_tau   = float(p.get('nl_release_ms', 150.0)) * 0.001
        self._nl_k_lf_mf   = float(p.get('nl_k_lf_mf',   0.02))
        self._nl_k_mf_hf   = float(p.get('nl_k_mf_hf',   0.04))
        self._nl_k_lf_hf   = float(p.get('nl_k_lf_hf',   0.01))
        _nl_lf_hz          = float(p.get('nl_lf_hz',    500.0))
        _nl_hf_hz          = float(p.get('nl_hf_hz',   4000.0))
        self._nl_lf_mask   = self._bF <  _nl_lf_hz
        self._nl_mf_mask   = (self._bF >= _nl_lf_hz) & (self._bF < _nl_hf_hz)
        self._nl_hf_mask   = self._bF >= _nl_hf_hz
        self._nl_state     = 0.0   # control state — persists across segments
        self._nl_ref_e     = 1.0   # set in _compute_calib_scalar

        # ── Bow onset spectral tilt  (tip vs shoulder articulation) ─────────
        # bow_onset_band_gains: per-band amplitude multiplier applied at strike
        # onset, decaying exponentially to 1.0 (neutral) over bow_onset_tau_ms.
        # Derived from SPEAR spectral centroid ratio: tip=12.2kHz / shoulder=7.2kHz.
        # Shoulder = all 1.0 (neutral / default). Bypass: omit or set all to 1.0.
        _bog = p.get('bow_onset_band_gains', None)
        _BOW_ONSET_EDGES = np.array([50, 89, 178, 356, 713, 1425, 2851,
                                      5702, 11404, 22050], dtype=np.float64)
        if _bog is not None and any(abs(g - 1.0) > 1e-3 for g in _bog):
            self._bow_onset_band_gains = np.array(_bog, dtype=np.float64)
            self._bow_onset_tau = float(p.get('bow_onset_tau_ms', 80.0)) * 0.001
            self._bow_onset_mode_band = np.searchsorted(
                _BOW_ONSET_EDGES[1:], self._bF
            ).clip(0, len(_bog) - 1).astype(np.int32)
        else:
            self._bow_onset_band_gains = None
            self._bow_onset_tau        = 0.08
            self._bow_onset_mode_band  = None

        # Fixed calibration scalar — replaces per-render adaptive normalization.
        # Anchored to isolated vel=1.0 bow forte strike. All dynamics preserved
        # relative to this reference. Soft hits are naturally soft; edge forte
        # is naturally louder than bow forte by the SPEAR-derived ratio.
        self._calib_scalar = self._compute_calib_scalar()

    def _compute_calib_scalar(self):
        """Render one vel=1.0 edge forte strike in isolation. Return 0.89/peak.
        Anchors to the loudest expected hit (edge forte with edge_level_boost)
        so nothing clips. Bow forte lands proportionally quieter, which is
        physically correct — edge crashes are louder than bow rides."""
        saved_state    = self._state.copy()
        saved_hf       = self._hf_state.copy()
        saved_queue    = list(self._hf_act_queue)
        saved_sparkle  = list(self._sparkle_events)
        saved_wander   = self._wander_scale
        saved_wrate    = self._wander_rate_scale
        self.reset()
        ref_rng = np.random.default_rng(42)
        ref_ev  = StrikeEvent(time=0.05, velocity=1.0, position=0.0)  # edge forte
        self._apply_strike(ref_ev, ref_rng)
        # Capture peak modal energy as NL excitation reference (post-strike state)
        self._nl_ref_e = max(float(np.sum(np.abs(self._state)**2)), 1e-30)
        L, R = self._render_segment(0.25, 0.05, ref_rng)
        peak = float(max(np.max(np.abs(L)), np.max(np.abs(R)), 1e-9))
        scalar = 0.89 / peak
        self._state[:]       = saved_state
        self._hf_state[:]    = saved_hf
        self._hf_act_queue   = saved_queue
        self._sparkle_events = saved_sparkle
        self._wander_scale   = saved_wander
        self._wander_rate_scale = saved_wrate
        return scalar

    def reset(self):
        self._state[:]    = 0j
        self._hf_state[:] = 0j
        self._hf_act_queue = []
        self._sparkle_events = []
        self._wander_scale = 1.0
        self._wander_rate_scale = 1.0
        self._active_contact    = None
        self._beo_t_strike      = None
        self._beo_ef            = 0.0
        self._nl_state          = 0.0
        self._lf_S              = 0.0
        self._lf_t_last         = 0.0
        self._lf_scale_now      = 1.0
        self._mf_S              = 0.0
        self._mf_t_last         = 0.0
        self._mf_scale_now      = 1.0
        self._eb_active         = None
        self._eb_t0             = None
        self._eb_inj            = None
        self._eb_npz_band       = None   # NPZ 5-band assignment when active

    def render(self, strikes, total_dur):
        N   = int(total_dur * SR)
        out = np.zeros((N,2), dtype=np.float64)
        p   = self._params
        rng = np.random.default_rng(p['seed'])
        self._schedule_sparkle(total_dur, rng)

        events = sorted(strikes, key=lambda s: s.time)
        if p['articulation'] == 'brush':
            expanded_body = []
            bloom_events  = []
            for ev in events:
                n    = int(p['brush_strikes'])
                dur  = float(p['brush_dur_ms']) * 0.001
                vsc  = float(p['brush_vel_scale'])
                pstd = float(p['brush_pos_std'])
                rng_b = np.random.default_rng(p['seed'] + int(ev.time * 1000))
                micro_vel = float(np.clip(ev.velocity * vsc / np.sqrt(n), 0.01, 1.0))
                for i in range(n):
                    dt  = (i / max(n - 1, 1)) * dur
                    pos = float(np.clip(ev.position + rng_b.normal(0, pstd), 0., 1.))
                    expanded_body.append(('body', StrikeEvent(
                        time=ev.time + dt, velocity=micro_vel, position=pos)))
                centroid = StrikeEvent(
                    time=ev.time + dur * 0.5,
                    velocity=float(np.clip(ev.velocity * vsc, 0.01, 1.0)),
                    position=ev.position)
                bloom_events.append(('bloom', centroid))
            all_events = sorted(expanded_body + bloom_events, key=lambda x: x[1].time)
        else:
            all_events = [('full', ev) for ev in events]

        # Onset window for edge band envelope
        _EB_ONSET_S = float(p.get('edge_band_env_onset_s', 1.5))

        def _eb_build(ev):
            """Build per-sample band scale array at strike time. No sub-segments needed."""
            ef_loc = float(np.clip(1.0 - float(ev.position)/0.5, 0., 1.))
            onset_N = int(_EB_ONSET_S * SR)

            if self._edge_npz is not None and ef_loc > 0.1:
                _evels, _eW, _eE, _eBand, _efdur = self._edge_npz
                _v = float(ev.velocity)
                n_frames = _eE.shape[1]; n_bands = _eE.shape[2]
                # Velocity-interpolate band envelope
                blended = np.zeros((n_frames, n_bands))
                for b in range(n_bands):
                    for k in range(n_frames):
                        blended[k, b] = np.interp(_v, _evels, _eE[:, k, b])
                pk = blended.max()
                if pk > 1e-12: blended /= pk
                # Level-preserving RMS correction: compensates for per-band exc_norm
                # distribution mismatch between our 780-mode fingerprint and NPZ 2000-mode bank.
                # correction[b] = (NPZ_rms[b]/Our_rms[b]) × K_level
                # K_level preserves total weighted output → calibration unchanged.
                # Values pre-computed from exc_norm data; stored in preset as edge_rms_correction.
                _rms_corr = p.get('edge_rms_correction', None)
                if _rms_corr is not None:
                    _corr = np.array(_rms_corr, dtype=np.float64)
                    blended = blended * _corr[np.newaxis, :]
                # Upsample to per-sample resolution (smooth, no steps)
                t_samp = np.arange(onset_N) / SR
                kf = np.clip(t_samp / _efdur, 0, n_frames-1)
                k0 = kf.astype(np.int32); k1 = np.minimum(k0+1, n_frames-1)
                fr = kf - k0
                band_scale = np.zeros((n_bands, onset_N))
                for b in range(n_bands):
                    band_scale[b] = blended[k0, b]*(1-fr) + blended[k1, b]*fr
                self._eb_band_scale  = band_scale   # (N_BANDS, onset_N)
                self._eb_npz_band    = _eBand
                self._eb_t0          = ev.time
                self._eb_inj         = (self._beo_injection.copy()
                                        if self._beo_injection is not None else None)
                return
            self._eb_band_scale = None

        def _eb_seg_scale(t_abs, n_free):
            """Per-mode scale array for this sub-segment from edge band envelope."""
            if self._eb_active is None or self._eb_t0 is None: return None
            t_rel = t_abs - self._eb_t0
            fdur  = self._edge_benv_fdur; env = self._eb_active
            kf    = t_rel / fdur
            k0    = max(0, min(int(kf), len(env)-1))
            k1    = min(k0+1, len(env)-1); fr = kf - k0
            band_val = env[k0] * (1-fr) + env[k1] * fr   # (N_BANDS,)
            # Use NPZ 5-band assignment when available, else default 4-band
            mband = getattr(self, '_eb_npz_band', None)
            if mband is None: mband = self._edge_benv_mband
            return band_val[mband]  # (n_modes,)

        def _render_gap(t_start, t_end):
            if t_end - t_start < 1e-4: return
            L,R = self._render_segment(t_end-t_start, t_start, rng)
            self._write(out, L, R, t_start, N)

        def _render_gap_eb(t_start, t_end):
            """Render gap; per-sample scale from _eb_band_scale applied inside _render_segment."""
            if t_end - t_start < 1e-4: return
            L,R = self._render_segment(t_end-t_start, t_start, rng)
            self._write(out, L, R, t_start, N)
            # Clear band scale after onset window passes
            if self._eb_band_scale is not None and self._eb_t0 is not None:
                if t_end - self._eb_t0 >= _EB_ONSET_S:
                    self._eb_band_scale = None; self._eb_t0 = None
                    self._eb_npz_band = None; self._eb_inj = None

        t_prev = 0.0
        for ev_type, ev in all_events:
            dur = ev.time - t_prev
            if dur > 1e-4:
                if self._eb_active is not None:
                    _render_gap_eb(t_prev, ev.time)
                else:
                    _render_gap(t_prev, ev.time)
            if ev_type == 'body':
                self._apply_strike(ev, rng, skip_bloom=True)
            elif ev_type == 'bloom':
                self._add_bloom_activation(ev, rng)
            else:
                self._apply_strike(ev, rng, skip_bloom=False)
                self._add_transient(out, ev, rng, N)
            if ev_type in ('full','body') and self._beo_pk_t is not None:
                bow_pk, edge_data, edge_vs = self._beo_pk_t
                ef = float(np.clip(1.0 - float(ev.position)/0.5, 0., 1.))
                if edge_vs is None:
                    # Stine/Nova: single edge array
                    edge_pk = edge_data
                else:
                    # Bettis: velocity-bracketed — interpolate by velocity
                    v = float(ev.velocity)
                    edge_pk = np.array([np.interp(v, edge_vs, edge_data[:, i])
                                        for i in range(edge_data.shape[1])])
                self._beo_pk_t_blended = ef * edge_pk + (1.0 - ef) * bow_pk
                self._beo_t_strike = ev.time
                self._beo_ef = ef

                # Bettis velocity pk_t scaling — edge only, gated by edge_frac
                # At soft: LF pk_t shrinks (builds faster), HF pk_t grows (builds slower)
                # At forte: all scales = 1.0 → no change from base
                if self._vel_tilt is not None and ef > 1e-3:
                    _vt_vels, _vt_fracs, _vt_ref, _vt_mband, _NVB = self._vel_tilt
                    _v = float(ev.velocity)
                    _frac_v = np.array([np.interp(_v, _vt_vels, _vt_fracs[:, b])
                                        for b in range(_NVB)])
                    _ratio = np.where(_vt_ref > 1e-9, _frac_v / _vt_ref, 1.0)
                    # pk_t scale: 1/sqrt(ratio) — LF more prominent → pk_t shrinks
                    _pkt_scale_band = 1.0 / np.sqrt(np.maximum(_ratio, 0.01))
                    _pkt_scale_mode = _pkt_scale_band[_vt_mband]
                    # Blend by edge_frac: zero effect at bow, full at edge
                    _pkt_scale_mode = 1.0 + ef * (_pkt_scale_mode - 1.0)
                    self._beo_pk_t_blended = self._beo_pk_t_blended * _pkt_scale_mode

            # LF energy transfer — update S and compute scale for this hit
            if ev_type in ('full','body') and self._lf_depth > 0.0:
                dt = ev.time - self._lf_t_last if self._lf_t_last > 0 else 0.0
                self._lf_S *= np.exp(-dt / self._lf_tau)
                self._lf_scale_now = (
                    min(1.0 + self._lf_S * self._lf_depth, self._lf_max)
                    if self._lf_max > 1.0
                    else max(1.0 - self._lf_S * self._lf_depth, self._lf_min)
                )
                pos = float(ev.position)
                if pos <= 0.25:   lfw = self._lf_w_edge
                elif pos >= 0.75: lfw = self._lf_w_bell
                else:             lfw = self._lf_w_bow
                self._lf_S    += float(ev.velocity) * lfw
                self._lf_t_last = ev.time
                # Wander rate linked to LF accumulator
                _lf_wr_boost = float(self._params.get('lf_wander_rate_boost', 0.0))
                if _lf_wr_boost > 0.0:
                    self._wander_rate_scale = max(
                        self._wander_rate_scale,
                        1.0 + self._lf_S * _lf_wr_boost)

            # MF energy transfer — parallel accumulator for body register buildup
            if ev_type in ('full','body') and self._mf_depth > 0.0:
                dt = ev.time - self._mf_t_last if self._mf_t_last > 0 else 0.0
                self._mf_S *= np.exp(-dt / self._mf_tau)
                self._mf_scale_now = (
                    min(1.0 + self._mf_S * self._mf_depth, self._mf_max)
                    if self._mf_max > 1.0
                    else max(1.0 - self._mf_S * self._mf_depth, self._mf_min)
                )
                pos = float(ev.position)
                if pos <= 0.25:   mfw = self._mf_w_edge
                elif pos >= 0.75: mfw = self._mf_w_bell
                else:             mfw = self._mf_w_bow
                self._mf_S    += float(ev.velocity) * mfw
                self._mf_t_last = ev.time

            # Activate edge band envelope for edge hits
            if ev_type in ('full','body') and self._edge_benv is not None:
                ef = float(np.clip(1.0 - float(ev.position)/0.5, 0., 1.))
                if ef > 0.1:
                    _eb_build(ev)

            t_prev = ev.time
        dur = total_dur - t_prev
        if dur > 1e-4:
            if self._eb_active is not None:
                _render_gap_eb(t_prev, total_dur)
            else:
                _render_gap(t_prev, total_dur)
        out[:,1] = _hp(out[:,1], 120)
        out *= self._calib_scalar
        return out

    def _render_segment(self, duration, t_abs, rng):
        N     = int(duration * SR)
        out_L = np.zeros(N); out_R = np.zeros(N)
        if N == 0: return out_L, out_R
        p         = self._params
        t_seg_end = t_abs + duration
        sparkle   = self._build_sparkle_gains(t_abs, N)

        # ── V14g: contact force integration phase ────────────────────────────
        # Per-sample loop for N_contact samples.
        # Each step: apply force sample to modal state, read output, advance phases.
        # No normalization — force shape drives spectral consequence naturally.
        # HF modes cycle through more phase positions during longer contacts →
        # receive partially cancelling contributions → less HF for shoulder.
        n_contact = 0
        if self._active_contact is not None:
            mode = self._active_contact[0]

            if mode == 'permode':
                # ── Per-mode force envelopes ───────────────────────────────────
                # force_mat[i, k]: force on mode i at sample k (DC-normalised per mode)
                # All vectorised: state += force_mat[:, k] * coupling each sample
                _, force_mat, coupling = self._active_contact
                N_contact = min(force_mat.shape[1], N)
                for k in range(N_contact):
                    self._state += force_mat[:, k] * coupling
                    out_L[k] = np.dot(self._state.real, self._pick_L)
                    out_R[k] = np.dot(self._state.real, self._pick_R)
                    self._state *= self._mode_step
                n_contact = N_contact

            elif mode == 'hertz':
                # ── Hertz mass-spring contact (sample-domain units) ────────────
                # Time in samples. v in units/sample. a = F/m in units/sample².
                # Contact duration emerges: T ≈ π√(m/k) samples.
                # For T≈22 samples: m/k ≈ 49 (e.g. m=49, k=1, v_scale=0.01).
                _, coupling, v_stick, x_stick, m, k_s, fb, phi_c = self._active_contact
                max_n = min(int(p.get('max_contact_samples', 500)), N)

                for k in range(max_n):
                    x_cymbal = fb * np.dot(self._state.real, phi_c)
                    delta = x_stick - x_cymbal
                    if delta <= 0.0 and k > 2:
                        break
                    delta = max(0.0, delta)
                    F = k_s * (delta ** 1.5)
                    v_stick -= F / m      # sample-domain: no dt
                    x_stick += v_stick    # sample-domain: no dt
                    self._state += F * coupling
                    out_L[k] = np.dot(self._state.real, self._pick_L)
                    out_R[k] = np.dot(self._state.real, self._pick_R)
                    self._state *= self._mode_step
                    n_contact = k + 1

            else:
                # ── Gaussian predetermined force buffer ───────────────────────
                _, force_buf, coupling = self._active_contact
                N_contact = min(len(force_buf), N)

                if N_contact > 0:
                    t_contact = np.arange(N_contact, dtype=np.float64) / SR
                    t_abs_c   = t_abs + t_contact
                    wander_c  = np.zeros((len(self._bF), N_contact))
                    for i in range(len(self._bF)):
                        D  = float(self._bD[i]) * p['wander_depth'] * self._wander_scale
                        lr = float(self._lfo_rates[i]) * self._wander_rate_scale
                        lp = float(self._lfo_phases[i])
                        wander_c[i] = -(D/lr)*(np.cos(2.*np.pi*lr*t_abs_c+lp)-np.cos(lp))

                for k in range(N_contact):
                    self._state += force_buf[k] * coupling
                    for i in range(len(self._bF)):
                        z = self._state[i]; A = abs(z)
                        if A < 1e-9: continue
                        out_L[k] += self._pick_L[i] * A * np.cos(np.angle(z) + wander_c[i, k])
                        out_R[k] += self._pick_R[i] * A * np.cos(np.angle(z) + wander_c[i, k])
                    self._state *= self._mode_step
                n_contact = N_contact

            self._active_contact = None

        # ── Nonlinear modal redistribution — stateful control signal ─────────
        # nlState (0–1) drives redistribution. Energy never leaves LF+MF+HF.
        # nlState has attack/release memory — effect builds during sustain.
        if self._nl_amount > 1e-6:
            _dt = duration
            _A  = np.abs(self._state)
            _E  = _A * _A
            _LF = float(_E[self._nl_lf_mask].sum())
            _MF = float(_E[self._nl_mf_mask].sum())
            _HF = float(_E[self._nl_hf_mask].sum())

            # Normalized excitation → smoothstep drive
            _exc   = float(np.clip((_LF+_MF+_HF) / self._nl_ref_e, 0.0, 1.0))
            _thr   = self._nl_threshold
            _t     = float(np.clip((_exc - _thr) / max(1.0 - _thr, 1e-6), 0.0, 1.0))
            _drive = _t * _t * (3.0 - 2.0 * _t)

            # Attack / release: nlState moves toward drive with separate time constants
            _tau   = self._nl_atk_tau if _drive >= self._nl_state else self._nl_rel_tau
            _coeff = 1.0 - float(np.exp(-_dt / max(_tau, 1e-6)))
            self._nl_state = float(np.clip(
                self._nl_state + (_drive - self._nl_state) * _coeff, 0.0, 1.0))

            if self._nl_state > 1e-6:
                _ns = self._nl_state * self._nl_amount

                # Transfers: energy × effective_coupling × dt
                _T_LM = _LF * _ns * self._nl_k_lf_mf * _dt
                _T_MH = _MF * _ns * self._nl_k_mf_hf * _dt
                _T_LH = _LF * _ns * self._nl_k_lf_hf * _dt

                # Clamp — LF cannot overdrain
                _LF_out = _T_LM + _T_LH
                if _LF_out > _LF:
                    _s = _LF / (_LF_out + 1e-30)
                    _T_LM *= _s; _T_LH *= _s
                # MF cannot overdrain (after receiving from LF)
                if _T_MH > _MF + _T_LM:
                    _T_MH = _MF + _T_LM

                # New band energies — conserved: newLF+newMF+newHF == LF+MF+HF
                _LF_new = max(0.0, _LF - _T_LM - _T_LH)
                _MF_new = max(0.0, _MF + _T_LM - _T_MH)
                _HF_new = _HF + _T_MH + _T_LH

                # Energy-conserving amplitude scaling per band
                for _mask, _E_old, _E_new in (
                        (self._nl_lf_mask, _LF, _LF_new),
                        (self._nl_mf_mask, _MF, _MF_new),
                        (self._nl_hf_mask, _HF, _HF_new)):
                    if _E_old > 1e-20 and abs(_E_new - _E_old) > 1e-20:
                        self._state[_mask] *= float(np.sqrt(_E_new / _E_old))

        # ── Analytical free-ring phase (n_contact..N) ─────────────────────────
        n_free = N - n_contact
        if n_free > 0:
            t_local   = np.arange(n_free, dtype=np.float64) / SR
            t_abs_arr = (t_abs + n_contact / SR) + t_local

            # V19: time-since-strike per sample for onset ramp (edge only)
            _do_ramp = (self._beo_pk_t is not None
                        and self._beo_t_strike is not None
                        and hasattr(self, '_beo_pk_t_blended')
                        and self._beo_ef > 0.1)   # skip ramp for bow
            if _do_ramp:
                _t_elapsed = t_abs_arr - self._beo_t_strike   # (n_free,)
                _bf        = self._beo_birth_frac
                _pk_t_arr  = self._beo_pk_t_blended            # (n_body,)

            for i in range(len(self._bF)):
                z = self._state[i]; A = abs(z)
                if A < 1e-9: continue
                phi_0 = np.angle(z); f = float(self._bF[i])
                t60 = (float(self._bT[i]) if self._wash_damp == 1.0
                       else float(self._bT[i]) / max(self._wash_damp, 0.1))
                decay = np.exp(-LOG1000*t_local/t60)
                D  = float(self._bD[i])*p['wander_depth']*self._wander_scale
                lr = float(self._lfo_rates[i])*self._wander_rate_scale
                lp = float(self._lfo_phases[i])
                wander = -(D/lr)*(np.cos(2.*np.pi*lr*t_abs_arr+lp)-np.cos(lp))
                wave = A*decay*np.cos(2.*np.pi*f*t_local+phi_0+wander)
                # NPZ edge band envelope — per-sample smooth scale, NO additional T60 during onset
                _ebs = getattr(self,'_eb_band_scale',None)
                if (_ebs is not None and self._eb_inj is not None
                        and self._eb_t0 is not None):
                    _b   = int(self._eb_npz_band[i]
                               if self._eb_npz_band is not None else 0)
                    _inj = self._eb_inj[i]
                    if _inj > 1e-12:
                        _s0 = int(round((t_abs + n_contact/SR - self._eb_t0)*SR))
                        _s0 = max(0, _s0)
                        _onset_N = _ebs.shape[1]
                        base = np.cos(2.*np.pi*f*t_local+phi_0+wander)
                        if _s0 >= _onset_N:
                            # Past onset — normal T60
                            wave = A * decay * base
                        else:
                            _nc = min(_onset_N - _s0, n_free)
                            sc_arr = A * decay.copy()
                            # Onset portion: inj × band_env × T60-within-20ms-frame
                            # t_in_frame resets every 20ms — matches previous engine
                            _FDUR_S = 0.020
                            _t_onset = (_s0 + np.arange(_nc)) / SR  # time since onset
                            _t_in_frame = _t_onset % _FDUR_S
                            _frame_decay = np.exp(-LOG1000 * _t_in_frame / t60)
                            sc_arr[:_nc] = _inj * _ebs[_b, _s0:_s0+_nc] * _frame_decay
                            wave = sc_arr * base
                # V19: per-mode onset ramp (bow/Stine/Nova)
                elif _do_ramp:
                    _pt = float(_pk_t_arr[i])
                    if _pt > 0.001:
                        _ramp = np.minimum(1.0, _bf + (1.0-_bf)*_t_elapsed/_pt)
                        _ramp = np.maximum(_ramp, 0.0)
                        wave *= _ramp
                # V20: bow onset spectral tilt (tip vs shoulder articulation)
                # Gain decays exp from bow_onset_band_gains[band] → 1.0 over tau.
                # Gate: skip at pure edge (ef >= 0.9); full strength at bow or bell.
                if (self._bow_onset_band_gains is not None
                        and self._beo_t_strike is not None
                        and self._beo_ef < 0.9):   # skip at pure edge
                    _b   = int(self._bow_onset_mode_band[i])
                    _g0  = float(self._bow_onset_band_gains[_b])
                    if abs(_g0 - 1.0) > 1e-3:
                        _t_e = t_abs_arr - self._beo_t_strike   # secs since strike
                        _env = np.exp(-np.maximum(_t_e, 0.0) / self._bow_onset_tau)
                        wave *= (1.0 + (_g0 - 1.0) * _env)
                if self._lf_sigmoid is not None and self._lf_scale_now < 1.0 - 1e-6:
                    _ms = self._lf_scale_now + (1.0 - self._lf_scale_now) * self._lf_sigmoid[i]
                    wave *= _ms
                if self._mf_bandpass is not None and abs(self._mf_scale_now - 1.0) > 1e-6:
                    _ms = 1.0 + (self._mf_scale_now - 1.0) * self._mf_bandpass[i]
                    wave *= _ms
                if i in sparkle: wave *= sparkle[i][n_contact:]
                out_L[n_contact:] += self._pick_L[i]*wave
                out_R[n_contact:] += self._pick_R[i]*wave

        free_dur = n_free / SR
        self._state *= (np.exp(1j*2.*np.pi*self._bF*free_dur)
                        *np.exp(-LOG1000*free_dur/self._bT))

        if self._hf_n > 0:
            t_local_full = np.arange(N, dtype=np.float64) / SR
            for i in range(self._hf_n):
                z = self._hf_state[i]; A = abs(z)
                if A < 1e-9: continue
                phi_0=np.angle(z); f=float(self._hfF[i]); t60=float(self._hfT[i])
                decay=np.exp(-LOG1000*t_local_full/t60)
                wave=A*decay*np.cos(2.*np.pi*f*t_local_full+phi_0)
                out_L+=self._hf_pick_L[i]*wave; out_R+=self._hf_pick_R[i]*wave

            self._hf_state *= (np.exp(1j*2.*np.pi*self._hfF*duration)
                               *np.exp(-LOG1000*duration/self._hfT))

            remaining = []
            for t_act,i,amp,phi in self._hf_act_queue:
                if t_act >= t_seg_end: remaining.append((t_act,i,amp,phi)); continue
                s_act = max(0, int(round((t_act-t_abs)*SR)))
                if t_act < t_abs: self._hf_state[i]+=amp*np.exp(1j*phi); continue
                nc = N-s_act
                if nc <= 0: continue
                f=float(self._hfF[i]); t60=float(self._hfT[i])
                tfa=np.arange(nc,dtype=np.float64)/SR
                wave=amp*np.exp(-LOG1000*tfa/t60)*np.cos(2.*np.pi*f*tfa+phi)
                out_L[s_act:]+=self._hf_pick_L[i]*wave
                out_R[s_act:]+=self._hf_pick_R[i]*wave
                t_end=nc/SR
                self._hf_state[i]+=(amp*np.exp(1j*phi)
                                    *np.exp(-LOG1000*t_end/t60)
                                    *np.exp(1j*2.*np.pi*f*t_end))
            self._hf_act_queue=remaining

        return out_L, out_R

    def _apply_strike(self, ev, rng, skip_bloom=False):
        p = self._params
        if self._vel_curves is not None and self._vel_norms is not None:
            # Per-mode SPEAR velocity participation (replaces hertz_vel_scale)
            _v = float(np.clip(ev.velocity, 0.0, 1.0))
            _norms = self._vel_norms
            _curves= self._vel_curves
            if _v <= _norms[0]:
                # Below lowest measured velocity: scale V1 curve proportionally
                vel = _curves[:, 0] * (_v / (_norms[0] + 1e-9))
            elif _v >= _norms[-1]:
                vel = _curves[:, -1]
            else:
                _idx = int(np.searchsorted(_norms, _v)) - 1
                _idx = int(np.clip(_idx, 0, len(_norms)-2))
                _t   = (_v - _norms[_idx]) / (_norms[_idx+1] - _norms[_idx] + 1e-12)
                vel  = _curves[:, _idx]*(1.0-_t) + _curves[:, _idx+1]*_t
        else:
            vel=hertz_vel_scale(self._bF,ev.velocity,tau_ref_ms=p['tau_ref_ms'])

        tau_ms = float(p.get('contact_tau_ms', 0.0))
        stick_profile = (np.abs(np.sinc(self._bF * tau_ms * 0.001))
                         if tau_ms > 0.0 else np.ones(len(self._bF)))

        # Position-dependent excitation weight
        pos       = float(np.clip(ev.position, 0., 1.))
        edge_frac = float(np.clip(1.0 - pos / 0.5, 0., 1.))  # 1.0=edge, 0.0=bow/bell

        if pos <= 0.5:
            t_blend    = pos / 0.5
            pos_weight = self._edge_weight + t_blend * (1.0 - self._edge_weight)
        else:
            bell_frac  = float(np.clip((pos - 0.5) / 0.5, 0.0, 1.0))
            pos_weight = ((1.0 - bell_frac) * np.ones(len(self._bF), dtype=np.float64)
                          + bell_frac * self._bell_weight)

        # At bell position, blend bow vel_participation toward bell-specific
        # vel_participation (built from Bell-1/Bell-2 SPEAR captures).
        # At bow (bell_frac=0): bow V1-V5 governs. At bell (bell_frac=1):
        # bell B1/B2 governs. In between: linear blend.
        if pos > 0.5 and self._bell_vel_curves is not None:
            bell_frac_v = float(np.clip((pos - 0.5) / 0.5, 0.0, 1.0))
            _v = float(np.clip(ev.velocity, 0.0, 1.0))
            _bn = self._bell_vel_norms
            _bc = self._bell_vel_curves
            if _v <= _bn[0]:
                bell_vel = _bc[:, 0] * (_v / (_bn[0] + 1e-9))
            elif _v >= _bn[-1]:
                bell_vel = _bc[:, -1]
            else:
                _bi = int(np.searchsorted(_bn, _v)) - 1
                _bi = int(np.clip(_bi, 0, len(_bn)-2))
                _bt = (_v - _bn[_bi]) / (_bn[_bi+1] - _bn[_bi] + 1e-12)
                bell_vel = _bc[:, _bi]*(1.0-_bt) + _bc[:, _bi+1]*_bt
            vel = (1.0 - bell_frac_v) * vel + bell_frac_v * bell_vel

        # Overall edge level boost — preserves spectral shape, scales all modes equally.
        # Source: SPEAR absolute peak ratio (edge_crash / bow_forte) = 0.127392/0.072089 = 1.767
        # Level 1 measurement. Scales with velocity and edge position fraction.
        edge_level_boost = float(p.get('edge_level_boost', 1.0))
        if edge_level_boost > 1.0 and edge_frac > 0.0:
            pos_weight = pos_weight * (1.0 + (edge_level_boost - 1.0) * float(ev.velocity) * edge_frac)

        tip_ms  = float(p.get('tip_contact_ms',  0.0))
        shld_ms = float(p.get('shld_contact_ms', 1.2))

        # Coupling: identical factors to V12. RNG call BEFORE wander/rate_jitter
        # to preserve calibration scalar identity at tip_ms=0.
        # Two-stage early correction: bake in amplitude at t=T_SPLIT
        # exp(-T_SPLIT × LOG1000 / T60_early) per mode.
        # Modes with fast early decay (sub-bass, air) arrive at T_SPLIT
        # with reduced amplitude. Late T60 (SPEAR) governs sustained ring.
        coupling = (self._exc_norm * (vel ** float(p.get('vel_gain_power', 1.0)))
                    * stick_profile * pos_weight
                    * np.exp(1j * rng.uniform(0., 2.*np.pi, len(self._bF))))

        # Always capture standard exc_norm injection for band envelope onset
        if self._edge_npz is not None:
            self._beo_injection = np.abs(coupling).copy()
        elif self._beo_pk_t is not None:
            self._beo_injection = np.abs(coupling).copy()

        # NPZ edge velw — replaces coupling for edge hits (Bettis only)
        # Set edge_use_velw=false to use standard exc_norm coupling instead (per previous engine design)
        if self._edge_npz is not None and edge_frac > 0.1 and p.get('edge_use_velw', True):
            _evels, _eW, _eE, _eBand, _efdur = self._edge_npz
            _v = float(ev.velocity)
            # vel^0.5 remapping for more gradual LF/HF transition
            _vels_norm = (_evels - _evels[0]) / (_evels[-1] - _evels[0])
            _v_norm    = (_v - _evels[0]) / (_evels[-1] - _evels[0])
            _v_mapped  = _evels[0] + np.interp(np.sqrt(np.clip(_v_norm, 0, 1)),
                                                np.sqrt(_vels_norm), _evels)
            _velw = np.array([np.interp(_v_mapped, _evels, _eW[:, i])
                               for i in range(_eW.shape[1])])
            _pk = _velw.max()
            if p.get('edge_velw_normalize', True) and _pk > 1e-12:
                _velw /= _pk
            _level = float(ev.velocity) ** float(p.get('vel_gain_power', 0.7))
            coupling = (_velw * _level
                        * np.exp(1j * rng.uniform(0., 2.*np.pi, len(self._bF))))
            self._beo_injection = np.abs(coupling).copy()

        if tip_ms <= 0.0:
            # V12 instantaneous — bit-identical
            self._state += coupling
            self._active_contact = None
        elif p.get('permode_contact', False):
            # ── Per-mode onset: bending wave group velocity (√f ordering) ─────
            # HF modes (faster wave) peak early, LF modes peak late.
            # DC-normalised per mode: body energy = V12, no spectral filtering.
            # tip_tau0/shld_tau0   : HF peak sample at bow / edge
            # tip_tau_max/shld_tau_max : LF peak sample at bow / edge
            tip_tau0  = float(p.get('tip_tau0',    3.0))
            tip_tauM  = float(p.get('tip_tau_max', 10.0))
            shld_tau0 = float(p.get('shld_tau0',  10.0))
            shld_tauM = float(p.get('shld_tau_max', 35.0))
            sig_ratio = float(p.get('sigma_ratio',  0.35))

            tau0 = tip_tau0 + edge_frac * (shld_tau0 - tip_tau0)
            tauM = tip_tauM + edge_frac * (shld_tauM - tip_tauM)

            # √f scaling: HF → tau0 (early), LF → tauM (late)
            f_norm = np.sqrt(self._bF / (float(self._bF.max()) + 1e-9))
            tau_i  = tauM - (tauM - tau0) * f_norm
            sig_i  = np.maximum(tau_i * sig_ratio, 1.0)

            n_contact = int(tauM + 3.0 * float(sig_i.max())) + 1
            k_arr     = np.arange(n_contact, dtype=np.float64)
            force_mat = np.exp(-0.5 * ((k_arr[None, :] - tau_i[:, None])
                                        / sig_i[:, None]) ** 2)
            force_mat /= (force_mat.sum(axis=1, keepdims=True) + 1e-9)

            self._active_contact = ('permode', force_mat, coupling)

        else:
            # Gaussian contact force, DC-normalised
            contact_ms = tip_ms + edge_frac * (shld_ms - tip_ms)
            N1 = max(1, int(contact_ms * 0.001 * SR))
            k1 = np.arange(N1, dtype=np.float64)
            f1 = np.exp(-0.5 * ((k1 - N1/2.0) / (N1/4.0))**2)
            f1 /= (f1.sum() + 1e-9)
            release_gain  = float(p.get('release_gain',  0.0))
            release_ratio = float(p.get('release_ratio', 0.25))
            if release_gain > 0.0:
                N2 = max(1, int(N1 * release_ratio))
                k2 = np.arange(N2, dtype=np.float64)
                f2 = np.exp(-0.5 * ((k2 - N2/2.0) / (N2/4.0))**2)
                f2 = f2 / (f2.sum() + 1e-9) * release_gain
                force_buf = np.concatenate([f1, f2])
            else:
                force_buf = f1
            self._active_contact = ('gauss', force_buf, coupling)

        # Update wander depth scale — resets to 1.0 at bow, boosts at edge+high vel (Level 5)
        edge_wander_depth = float(p.get('edge_wander_depth', 0.0))
        self._nl_last_pos  = float(ev.position)   # used by _add_transient for position weighting
        self._wander_scale = 1.0 + float(ev.velocity) * edge_frac * edge_wander_depth
        edge_wander_rate = float(p.get('edge_wander_rate', 0.0))
        self._wander_rate_scale = 1.0 + float(ev.velocity) * edge_frac * edge_wander_rate
        # Per-hit rate jitter: bow ±rate_jitter×1.5Hz, edge forte adds same again → ±2× total
        rate_jitter = float(p.get('rate_jitter', 0.0))
        if rate_jitter > 0.0:
            self._wander_rate_scale += rng.uniform(-rate_jitter, rate_jitter)
            self._wander_rate_scale += rng.uniform(-rate_jitter, rate_jitter) * edge_frac * float(ev.velocity)
        self._wander_rate_scale = max(self._wander_rate_scale, 0.1)  # safety floor

        # Velocity-dependent T60 scaling (vel_len_tilt, default 0.0 = no effect)
        _vlt     = float(p.get('vel_len_tilt', 0.0))
        _vlt_ref = float(p.get('vel_len_tilt_ref', 0.787))
        self._wash_damp = max(0.3, 1.0 + _vlt * (float(ev.velocity) - _vlt_ref))

        if not skip_bloom and self._hf_n>0:
            self._add_bloom_activation(ev, rng)

    def _add_bloom_activation(self, ev, rng):
        p = self._params
        g = float(p['hf_bloom_gain'])
        # Edge bloom boost: edge+high vel activates bloom harder → crash wash character (Level 5)
        edge_bloom_boost = float(p.get('edge_bloom_boost', 1.0))
        if edge_bloom_boost > 1.0:
            edge_frac = float(np.clip(1.0 - float(ev.position) / 0.5, 0., 1.))
            g *= (1.0 + (edge_bloom_boost - 1.0) * float(ev.velocity) * edge_frac)
        hv=hertz_vel_scale(self._hfF,ev.velocity,tau_ref_ms=p['tau_ref_ms'])
        ha=self._hfExc*hv*g
        hp=rng.uniform(0.,2.*np.pi,self._hf_n)
        ta=ev.time+self._hfBirth
        for i in range(self._hf_n):
            if ha[i]>1e-12:
                self._hf_act_queue.append((float(ta[i]),i,float(ha[i]),float(hp[i])))

    def _pos_to_xy(self, pos):
        pos=float(np.clip(pos,0.,1.))
        ex,ey=self._positions['edge']; bx,by=self._positions['bow']
        lx,ly=self._positions['bell']
        if pos<=0.5: t=pos/0.5; return ex+t*(bx-ex),ey+t*(by-ey)
        else: t=(pos-.5)/.5; return bx+t*(lx-bx),by+t*(ly-by)

    def _schedule_sparkle(self, total_dur, rng):
        self._sparkle_events=[]
        p=self._params
        if not self._clusters or p['sparkle_depth']<=0.: return
        rate=max(float(p['sparkle_rate']),1e-3); depth=float(p['sparkle_depth'])
        dur_s=float(p['sparkle_dur_ms'])*0.001
        w=np.array([max(c['stability_score'],1e-6) for c in self._clusters]); w/=w.sum()
        t=0.
        while t<total_dur:
            t+=rng.exponential(1./rate)
            if t>=total_dur: break
            self._sparkle_events.append((t,t+dur_s,int(rng.choice(len(self._clusters),p=w)),depth))

    def _build_sparkle_gains(self, t_seg_start, N):
        gains={}
        if not self._sparkle_events: return gains
        t_end=t_seg_start+N/SR
        for ev_s,ev_e,cidx,depth in self._sparkle_events:
            ov0=max(ev_s,t_seg_start); ov1=min(ev_e,t_end)
            if ov0>=ov1: continue
            s0=max(0,int(round((ov0-t_seg_start)*SR)))
            s1=min(N,int(round((ov1-t_seg_start)*SR)))
            if s0>=s1: continue
            dur=ev_e-ev_s
            if dur<1e-6: continue
            tin=np.clip(t_seg_start+np.arange(s0,s1,dtype=np.float64)/SR-ev_s,0.,dur)
            hann=np.sin(np.pi*tin/dur)**2
            for midx in self._clusters[cidx]['body_mode_indices']:
                if midx<0 or midx>=len(self._bF): continue
                if midx not in gains: gains[midx]=np.ones(N,dtype=np.float64)
                gains[midx][s0:s1]+=depth*hann
        return gains

    def _add_transient(self, out, ev, rng, N_total):
        p=self._params; n_off=int(ev.time*SR); n_av=N_total-n_off
        if n_av<=0: return
        if p['stick_gain']>0:
            pos = float(getattr(self, '_nl_last_pos', 0.5))
            bow_frac  = float(np.clip(1.0 - abs(pos - 0.5) / 0.5, 0.0, 1.0))
            bell_frac = float(np.clip((pos - 0.5) / 0.5, 0.0, 1.0))
            edge_frac = float(np.clip(1.0 - pos / 0.5, 0.0, 1.0))
            pos_w = (bow_frac  * float(p.get('stick_bow_weight',  1.0))
                   + bell_frac * float(p.get('stick_bell_weight', 1.0))
                   + edge_frac * float(p.get('stick_edge_weight', 1.0)))
            if pos_w > 1e-6:
                nc  = int(p.get('stick_len_ms',  8.0) * 0.001 * SR)
                dec = p.get('stick_decay_ms', 3.0) * 0.001 * SR
                cl  = rng.standard_normal(nc) * np.exp(-np.arange(nc) / dec)
                clf = np.zeros(n_av); clf[:min(nc,n_av)] = cl[:min(nc,n_av)]
                bp_lo = p.get('stick_bp_lo', 2500)
                bp_hi = p.get('stick_bp_hi', 9000)
                clf = _bp(clf, bp_lo, bp_hi)
                g   = p['stick_gain'] * pos_w * ev.velocity**1.7
                out[n_off:,0] += g*clf; out[n_off:,1] += g*clf
        if p['atk_noise']>0:
            nb=int(p['atk_len']*SR); b=rng.standard_normal(nb)*np.exp(-np.arange(nb)/(p['atk_dec']*SR))
            bf=np.zeros(n_av); bf[:min(nb,n_av)]=b[:min(nb,n_av)]
            bf=_bp(bf,300,6500); bpk=max(float(np.max(np.abs(bf))),1e-9)
            bf/=bpk; g=p['atk_noise']*ev.velocity**1.4
            out[n_off:,0]+=g*bf*.9; out[n_off:,1]+=g*bf*.9

    @staticmethod
    def _write(out, L, R, t_start, N_total):
        n=int(t_start*SR); ne=min(n+len(L),N_total); nc=ne-n
        if nc>0: out[n:ne,0]+=L[:nc]; out[n:ne,1]+=R[:nc]
