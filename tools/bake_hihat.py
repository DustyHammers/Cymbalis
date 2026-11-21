#!/usr/bin/env python3
"""
bake_hihat.py — the HI-HAT preset's bake, from presets/hihat/ (docs/PORT_MAP.md, "Hi-hat").

The hi-hat is not a V19 preset: the V19 engine cannot load it. Its strike model is the
approved reference, docs/hihat_reference.py (Kris, 2026-11-20), whose preparation this tool
runs: the stick envelopes (each band's SPEAR rise replaced by its decay after the peak, at most
+9 dB above it), each knot's level per band (the closed level law) and the band decay rates
(the pedal choke). The plugin's engine (FastEngine) does the per-strike part: the knots
interpolated at the strike's openness and velocity.

The bake has the format of tools/bake_preset.py, so the plugin's loader reads it as any other:
  * the shared per-mode arrays: the 683 mode frequencies; each mode's T60 from the fully open
    knot's decay rate of its band (ln 1000 / rate); exc_norm the fully open knot's mode
    weights (the energy cascade's share weights); position weights 1; no drift; the pick
    vectors and LFO tables drawn as V19 draws them, from SEED;
  * three entries: TIP (articulation tip, position 0.5, the BOW row), EDGE (edge, position
    0.0, the EDGE row) and FOOT (foot, the pedal; no row), each with its stick click
    (STICK_CLICK) and no contact, onset ramp, velocity table or NPZ data;
  * the "hihat" block: per knot its stick envelope, its raw envelope (the closed EDGE knots:
    FOOT plays them), its mode weights and its level per band (the fully open knot also its FREE
    RING weights, from hihat_weights_free.json, and its smoothed stick envelope); the band decay rates at
    openness 0 (the closed EDGE V3) to 1; the choke rate, the FOOT rules, the pedal smoothing;
  * a tuning block: the output gain of every preset (-11 dB), HIT VARIATION on, and the hi-hat's
    TIP OPEN LEVEL 60 %, TIP OPEN TICK +4 dB and RING FLOOR 100 %, and FREE RING off, TIP MODES 0 dB
    and MAX OPEN 100 % (docs/TUNING.md).

Level trims are measured afterwards with tools/cymbalis_trims and stored with
tools/set_level_trims.py (docs/BUILDS.md); re-baking drops them.

Usage:
    python tools/bake_hihat.py --out resources/presets/hihat.bake.json
"""
import argparse
import os
import platform
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'docs'))
from bake_preset import (AMP_SKIP_THRESHOLD, BAKE_FORMAT, BAKE_FORMAT_VERSION, BakeError, dump_json,  # noqa: E402
                         enc, manifest_md5s, md5, rng_block)
import hihat_reference as ref  # noqa: E402

SR = 44100
LOG1000 = float(np.log(1000.0))
SEED = 14                      # the pick vectors' and LFO tables' seed (any fixed value)
WANDER_RATE_HZ = 1.5
TAU_REF_MS = 0.2               # unused: the hi-hat's strikes do not take the hertz velocity law
PRESET_DIR = 'presets/hihat'
SOURCES = ['presets/hihat/HiHat_A60.json', 'presets/hihat/data/hihat_envelopes.json',
           'presets/hihat/data/hihat_weights.json', 'docs/hihat_reference.py',
           'presets/hihat/data/hihat_weights_free.json']
# The stick click per articulation (Kris, 2026-11-20): gain, band (Hz), length and decay (ms).
# The gains are set so that on a closed hit at velocity 1.0 the click's first-3 ms RMS is 3 dB
# under the cymbal's own (mean power over 256 hits, measured with FastEngine; docs/PORT_MAP.md,
# "Hi-hat"). They started at 6.0, which put the click about 30 dB (TIP) and 20 dB (EDGE) above
# the hit. FOOT has none.
STICK_CLICK = {
    'tip': dict(stick_gain=0.1398, stick_bp_lo=1500.0, stick_bp_hi=16000.0, stick_len_ms=12.0, stick_decay_ms=1.2),
    'edge': dict(stick_gain=0.4189, stick_bp_lo=900.0, stick_bp_hi=12000.0, stick_len_ms=15.0, stick_decay_ms=2.5),
    'foot': dict(stick_gain=0.0),
}
# (entry name, articulation id, position): the BOW row plays TIP, the EDGE row EDGE; FOOT has no row.
ENTRIES = [('TIP', 'tip', 0.5), ('EDGE', 'edge', 0.0), ('FOOT', 'foot', 0.0)]


def effective(stick_gain):
    """The engine-wide switches the loader reads (tools/bake_preset.py effective_params), with
    V19's defaults where the hi-hat has nothing of its own: no contact, wander, accumulators,
    onset ramp, NPZ data or noise."""
    return {
        'articulation': 'stick', 'seed': SEED, 'vel_participation': False, 'bell_vel_participation': False,
        'vel_gain_power': 1.0, 'contact_tau_ms': 0.0, 'edge_level_boost': 1.0, 'hammering_tilt': 0.0,
        'tip_contact_ms': 0.0, 'shld_contact_ms': 1.2, 'permode_contact': False, 'tip_tau0': 3.0,
        'tip_tau_max': 10.0, 'shld_tau0': 10.0, 'shld_tau_max': 35.0, 'sigma_ratio': 0.35, 'release_gain': 0.0,
        'wander_depth': 0.0, 'wander_rate_hz': WANDER_RATE_HZ, 'edge_wander_depth': 0.0, 'edge_wander_rate': 0.0,
        'rate_jitter': 0.0, 'lf_sens_depth': 0.0, 'lf_sens_tau': 5.0, 'lf_min_scale': 0.3, 'lf_max_scale': 1.0,
        'lf_edge_weight': 1.0, 'lf_bow_weight': 0.5, 'lf_bell_weight': 0.1, 'lf_cutoff_hz': 2000.0,
        'lf_wander_rate_boost': 0.0, 'mf_sens_depth': 0.0, 'mf_sens_tau': 5.0, 'mf_min_scale': 0.3,
        'mf_max_scale': 1.0, 'mf_edge_weight': 1.0, 'mf_bow_weight': 0.5, 'mf_bell_weight': 0.1,
        'mf_lo_hz': 500.0, 'mf_hi_hz': 4000.0, 'vel_len_tilt': 0.0, 'vel_len_tilt_ref': 0.787,
        'band_env_onset': False, 'band_env_birth_frac': 0.5, 'vel_tilt': False, 'bow_onset_gains_active': False,
        'nl_amount': 0.0, 'hf_bloom_modes': 0, 'hf_bloom_gain': 0.0, 'sparkle_clusters': 0, 'sparkle_depth': 0.0,
        'stick_gain': float(stick_gain), 'atk_noise': 0.0, 'edge_band_env': False, 'edge_npz': False,
        'tau_ref_ms': TAU_REF_MS,
    }


def provenance():
    manifest = manifest_md5s()
    out, bad = {}, []
    for rel in SOURCES:
        digest = md5(os.path.join(REPO, rel))
        locked = manifest.get(rel)
        out[rel] = {'md5': digest, 'manifest_md5': locked, 'matches_manifest': None if locked is None else digest == locked}
        if locked is not None and digest != locked:
            bad.append(rel)
    if bad:
        raise BakeError('FAIL: locked files differ from docs/BASELINE_MANIFEST.md: ' + ', '.join(bad))
    for rel in SOURCES[:3]:
        if out[rel]['manifest_md5'] is None:
            raise BakeError(f'FAIL: {rel} is not in docs/BASELINE_MANIFEST.md')
    return out


def bake():
    from scipy.signal import butter
    import scipy

    h = ref.HiHat(os.path.join(REPO, PRESET_DIR))
    n = len(h.freq)
    nb = ref.NB
    band = h.band.astype(np.int64)
    if not (np.all(np.diff(h.freq) > 0) and np.all(np.diff(band) >= 0)):
        raise BakeError('FAIL: the modes must rise in frequency and band')
    # Each mode's band is its one-third-octave band (the plugin's excitation bands).
    calc = np.clip(np.searchsorted(ref.E22[1:], h.freq, side='right'), 0, nb - 1)
    if not np.array_equal(calc, band):
        raise BakeError('FAIL: a mode is not in the one-third-octave band the preset gives it')

    # Each mode's T60 from the fully open knot's decay rate of its band (amplitude, 1/s).
    open_full = h.rates[[x for _, x in h.rate_knots].index(1.0)]
    t60 = LOG1000 / open_full[band]
    sos = butter(2, 120 / (SR / 2), btype='high', output='sos')
    pick = np.random.default_rng(SEED)        # __init__ lines 205-207, as V19
    pick_l = pick.uniform(0.6, 1.0, n) * pick.choice([-1., 1.], n)
    pick_r = pick.uniform(0.6, 1.0, n) * pick.choice([-1., 1.], n)
    wander = np.random.default_rng(SEED + 0xA2A2A2)   # lines 209-211
    lfo_rate = wander.uniform(0.7, 1.3, n) * WANDER_RATE_HZ
    lfo_phase = wander.uniform(0., 2. * np.pi, n)
    step = np.exp(1j * 2.0 * np.pi * h.freq / SR) * np.exp(-LOG1000 / (t60 * SR))
    null_keys = ('mode_id', 'nl_lf_mask', 'nl_mf_mask', 'nl_hf_mask', 'lf_sigmoid', 'mf_bandpass', 'pk_t_bow_s',
                 'pk_t_edge_s', 'vel_tilt_velocities', 'vel_tilt_fractions', 'vel_tilt_reference',
                 'vel_tilt_mode_band', 'bow_onset_mode_band', 'bow_onset_band_edges_hz')
    shared = {
        'freq_hz': enc(h.freq), 't60_s': enc(t60), 'drift_hz': enc(np.zeros(n)),
        'exc_norm': enc(h.w['edge_open_100']), 'edge_weight': enc(np.ones(n)), 'bell_weight': enc(np.ones(n)),
        'pick_l': enc(pick_l), 'pick_r': enc(pick_r), 'lfo_rate_hz': enc(lfo_rate), 'lfo_phase': enc(lfo_phase),
        'mode_step_re': enc(step.real), 'mode_step_im': enc(step.imag),
        **{k: None for k in null_keys},
    }

    knots = []
    for k in h.knots.values():
        kid = k['id']
        closed_edge = k['articulation'] == 'edge' and k['openness'] == 0.0
        knots.append({
            'id': kid, 'capture': k['capture'], 'articulation': k['articulation'],
            'openness': float(k['openness']), 'velocity': float(k['velocity']),
            'frames': int(h.raw[kid].shape[1]),
            'stick': enc(h.stick[kid]),                       # bands x frames
            'raw': enc(h.raw[kid]) if closed_edge else None,  # FOOT plays the closed EDGE's own rise
            'weights': enc(h.w[kid]),
            'level_db': enc(h.lvl[kid]),
            # FREE RING (a Tuning window option): the fully open knot as a single free cymbal.
            **({'stick_free': enc(h.stick_free[kid]), 'weights_free': enc(h.w_free[kid])} if kid in h.w_free else {}),
        })
    hihat = {
        'format': 'cymbalis-hihat-1',
        'reference': 'docs/hihat_reference.py',
        'frame_s': float(ref.FR), 'bands': nb,
        'mode_band': enc(band, True),
        'knots': knots,
        'rate_knots': {'ids': [kid for kid, _ in h.rate_knots],
                       'openness': enc(np.array([x for _, x in h.rate_knots], dtype=np.float64)),
                       'rates': enc(h.rates)},
        'choke_rate': float(ref.CHOKE_RATE),
        'stick_firing': {'look_frames': ref.IMPULSE_LOOK, 'max_db': ref.IMPULSE_MAX_DB},
        'foot': {k: float(v) for k, v in ref.FOOT.items()},
        'pedal_smoothing_s': 0.01,
        'end_fade_s': float(ref.FR),
    }

    slots = []
    for name, art, pos in ENTRIES:
        click = STICK_CLICK[art]
        slots.append({
            'name': name, 'articulation': art, 'overlay': None, 'overlay_file': None,
            'position': float(pos), 'calib_scalar': 1.0, 'bow_vp': None, 'bell_vp': None,
            'onset_gains': None, 'onset_tau_s': 0.08,
            'effective': effective(click['stick_gain']),
            'params': {'seed': SEED, 'tau_ref_ms': TAU_REF_MS, 'wander_rate_hz': WANDER_RATE_HZ, 'wander_depth': 0.0,
                       **click},
            'contact_force_python': None,
        })

    return {
        'format': BAKE_FORMAT,
        'format_version': BAKE_FORMAT_VERSION,
        'generator': 'tools/bake_hihat.py',
        'python': {'version': platform.python_version(), 'numpy': np.__version__, 'scipy': scipy.__version__,
                   'platform': platform.platform()},
        'preset': PRESET_DIR + '/HiHat_A60.json',
        'overrides': {},
        'sample_rate': SR,
        'constants': {'log1000': LOG1000, 'two_pi': float(2. * np.pi), 'amp_skip_threshold': AMP_SKIP_THRESHOLD,
                      'segment_skip_s': 1e-4},
        'n_modes': int(n),
        'hp_right': {'cutoff_hz': 120.0, 'order': 2, 'sos': enc(sos)},
        'shared': shared,
        'rng': rng_block(SEED, int(n)),
        'slots': slots,
        'sources': provenance(),
        'hihat': hihat,
        'notes': ['Not a V19 preset: the strike model of docs/hihat_reference.py (Kris, 2026-11-20), '
                  'interpolated per strike by FastEngine.',
                  'T60 of each mode: ln(1000) / the fully open knot\'s decay rate of its band; exc_norm: the fully '
                  'open knot\'s mode weights. Position weights 1, no drift.'],
        'tuning': {'format': 'cymbalis-tuning', 'version': 1, 'applied_by': 'tools/bake_hihat.py',
                   'cymbal': {'output_gain_db': -11.0, 'hit_variation': True, 'hh_tip_open_level': 60.0,
                              'hh_tip_open_tick_db': 4.0, 'hh_ring_floor': 100.0, 'hh_free_ring': False,
                              'hh_tip_modes_db': 0.0, 'hh_max_open': 100.0}},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, help='output bake JSON path')
    args = ap.parse_args()
    data = bake()
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as f:
        f.write(dump_json(data) + '\n')
    print(f'baked {len(data["slots"])} entries, {data["n_modes"]} modes, {len(data["hihat"]["knots"])} knots -> '
          f'{args.out} ({os.path.getsize(args.out) / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
