#!/usr/bin/env python3
"""
bake_preset.py — export everything the C++ engine needs from the Python V19 engine.

Loads the engine through prototype/load_preset.py once per slot (each slot's
overlay, all with the same overrides) and writes one JSON file:

  * shared per-mode arrays, written once. These must be bit-identical across the
    five slot engines; the script fails loudly if they are not;
  * per-slot data: velocity tables, onset gains, calibration scalar, position,
    and the effective parameter values the engine uses;
  * initial PCG64 states of every RNG stream, plus check values;
  * constants: sample rate, ln(1000), 2*pi, the mode-skip threshold, and the
    right-channel high-pass SOS;
  * the format version and the MD5s of every source file that went into it.
    Locked files are checked against docs/BASELINE_MANIFEST.md.

Every numeric array is written as {"shape", "fnv1a64", "data"}. The checksum is
FNV-1a 64 over the little-endian float64 bytes of the values, so the C++ loader
can prove that it parsed every value exactly.

  * for presets with NPZ edge data (602, Bettis): the NPZ velw tables, band
    envelopes and their switches ("edge_npz"); a velocity-bracketed edge pk_t is
    written as one row per velocity. Presets without velocity participation tables
    record tau_ref_ms for hertz_vel_scale.
  * copied entries (COPIED_ENTRIES): an entry with another entry's data under its own
    articulation id ("articulation", "copy_of"), tuned on its own in the plugin: the
    shoulder articulations of the 602, Bettis and Raven, the 602's CENTER (a copy of its BOW) and
    Nova's EDGE (a copy of BOW2, bow_shoulder, at the edge position). A copy named like an
    existing entry replaces it in place: Nova's EDGE was the base preset at the edge.
  * the edge band envelope switched on by the bake (ENVELOPE_ON): Python builds the NPZ band
    envelope only when an edge band envelope file is also set (line 691); for a preset in
    ENVELOPE_ON the bake sets edge_npz.envelope_active and says why (envelope_set_by), as the
    602's all-zero trigger file does in Python.
  * velocity-table fixes (VP_COLUMN_FIXES): columns of an overlay's velocity table replaced
    with another column of the same table, in the bake only (presets/ is unchanged), for every
    entry with that overlay, copies included. Each fix is recorded in the bake's notes.

Usage:
    python tools/bake_preset.py --overrides-from tests/sequences/edge_3hits_15s.json --out bake.json
    python tools/bake_preset.py --overrides '{"nl_amount": 0.0}' --out bake.json
    python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out bake.json
"""
import argparse
import copy
import hashlib
import json
import os
import platform
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ab_common as ab  # noqa: E402

BAKE_FORMAT = 'cymbalis-bake'
BAKE_FORMAT_VERSION = 1
AMP_SKIP_THRESHOLD = 1e-9          # multi_strike_engine_v19.py line 859: `if A < 1e-9: continue`
BOW_ONSET_EDGES = [50, 89, 178, 356, 713, 1425, 2851, 5702, 11404, 22050]   # lines 431-432
# Shared arrays written only when a preset has them (absent from older bakes).
OPTIONAL_SHARED = ('pk_t_edge_by_vel_s', 'pk_t_edge_velocities')
# Entries that copy another entry's data under their own articulation id, so that the plugin
# tunes them on their own (docs/TUNING.md): (name, articulation id, source entry, position).
# A source can be an earlier copy. A copy named like an entry of the preset replaces that
# entry where it stands. SMOKE FLAT RIDE's bell rows play CENTER; the rows' SHOULDER sticks
# play the *_SHOULDER copies; every EDGE row is shoulder only (docs/UI_SPEC.md section 8).
COPIED_ENTRIES = {
    'nova': [('EDGE', 'edge', 'BOW2', 0.0)],
    '602': [('CENTER', 'center', 'BOW', 1.0),
            ('BOW_SHOULDER', 'bow_shoulder', 'BOW', 0.5),
            ('CENTER_SHOULDER', 'center_shoulder', 'CENTER', 1.0)],
    'bettis': [('BELL_SHOULDER', 'bell_shoulder', 'BELL', 1.0),
               ('BOW_SHOULDER', 'bow_shoulder', 'BOW', 0.5)],
    'swish': [('BELL_SHOULDER', 'bell_shoulder', 'BELL', 1.0)],
    'raven': [('BELL_SHOULDER', 'bell_shoulder', 'BELL', 1.0),
              ('BOW_SHOULDER', 'bow_shoulder', 'BOW', 0.5)],
}
# Presets whose NPZ edge band envelope the bake switches on (docs/PORT_MAP.md, "Swish"):
# preset -> the reason recorded in edge_npz.envelope_set_by.
ENVELOPE_ON = {
    'swish': 'Swish_Baseline1.json sets no edge_band_env_path, so Python never runs the NPZ band '
             'envelope; the plugin switches it on (Kris, 2026-10-17), as the 602\'s trigger file does',
}
# Velocity-table columns replaced in the bake (docs/PORT_MAP.md, live-engine decision 17):
# (overlay, table, its data file, its column labels, label replaced, replacing label, reason).
# vel_norms stay as they are. The labels are checked against the data file.
VP_COLUMN_FIXES = {
    'nova': [('bow_shoulder', 'bow_vp', 'presets/nova/data/nova_vel_participation_shoulder_correct.json',
              ['V4', 'V4', 'V5', 'V5', 'V5'], 'V5', 'V4',
              'above velocity 0.9905 (MIDI 126-127) V5 made the bow shoulder bell-like: it has about '
              'half of V4\'s modes (531 against 1091 above 0.01), its ring at velocity 1.0 is dominated '
              'by 647 Hz and 2044 Hz, and it is 5.6 dB quieter than V4 at 0.98')],
}


class BakeError(SystemExit):
    pass


# ---------------------------------------------------------------------------
# array encoding
# ---------------------------------------------------------------------------
def fnv_hex(arr):
    data = np.ascontiguousarray(np.asarray(arr, dtype='<f8')).tobytes()
    return '0x%016x' % ab.fnv1a64_bytes(data)


def enc(arr, integer=False):
    a = np.asarray(arr)
    if a.dtype == bool:
        a = a.astype(np.int64)
        integer = True
    if not np.all(np.isfinite(a.astype(np.float64))):
        raise BakeError('non-finite value in baked array')
    flat = a.reshape(-1)
    data = [int(x) for x in flat] if integer else [float(x) for x in flat.astype(np.float64)]
    return {'shape': list(a.shape), 'fnv1a64': fnv_hex(a.astype(np.float64)), 'data': data}


def enc_opt(arr, integer=False):
    return None if arr is None else enc(arr, integer)


# ---------------------------------------------------------------------------
# JSON writer: indented structure, numeric lists on one line
# ---------------------------------------------------------------------------
def dump_json(obj, indent=0):
    pad = '  ' * indent
    if isinstance(obj, dict):
        if not obj:
            return '{}'
        items = [f'{pad}  {json.dumps(k)}: {dump_json(v, indent + 1)}' for k, v in obj.items()]
        return '{\n' + ',\n'.join(items) + '\n' + pad + '}'
    if isinstance(obj, list):
        if all(isinstance(x, (int, float, bool)) or x is None for x in obj):
            return '[' + ','.join(json.dumps(x, allow_nan=False) for x in obj) + ']'
        items = [f'{pad}  {dump_json(v, indent + 1)}' for v in obj]
        return '[\n' + ',\n'.join(items) + '\n' + pad + ']'
    return json.dumps(obj, allow_nan=False)


# ---------------------------------------------------------------------------
# engine extraction
# ---------------------------------------------------------------------------
def engine_globals(eng):
    """Module globals of the loaded engine (load_preset does not register the module)."""
    return type(eng).render.__globals__


def shared_arrays(eng, notes=None):
    """Arrays that depend only on the fingerprint, the base preset and the overrides.
    Facts worth knowing about the data are appended to `notes`."""
    sp = eng._spear
    body_mask = sp.births < float(eng._params.get('birth_gate_s', 0.30))
    out = {
        'mode_id': (sp.ids[body_mask], True),
        'freq_hz': (eng._bF, False),
        't60_s': (eng._bT, False),
        'drift_hz': (eng._bD, False),
        'exc_norm': (eng._exc_norm, False),
        'edge_weight': (eng._edge_weight, False),
        'bell_weight': (eng._bell_weight, False),
        'pick_l': (eng._pick_L, False),
        'pick_r': (eng._pick_R, False),
        'lfo_rate_hz': (eng._lfo_rates, False),
        'lfo_phase': (eng._lfo_phases, False),
        'nl_lf_mask': (eng._nl_lf_mask, True),
        'nl_mf_mask': (eng._nl_mf_mask, True),
        'nl_hf_mask': (eng._nl_hf_mask, True),
        'lf_sigmoid': (eng._lf_sigmoid, False),
        'mf_bandpass': (eng._mf_bandpass, False),
        # Per-sample mode step of the contact phase (__init__ lines 277-278).
        'mode_step_re': (eng._mode_step.real, False),
        'mode_step_im': (eng._mode_step.imag, False),
    }
    if eng._beo_pk_t is not None:
        bow_pk, edge_pk, edge_vs = eng._beo_pk_t
        n = len(eng._bF)
        # The ramp reads pk_t by mode position (line 897: _pk_t_arr[i] for i < n_body),
        # so entries beyond the mode pool are never used (PORT_MAP f.17).
        if bow_pk.shape[-1] != n or edge_pk.shape[-1] != n:
            if bow_pk.shape[-1] < n or edge_pk.shape[-1] < n:
                raise BakeError(f'FAIL: pk_t data has {bow_pk.shape[-1]} entries for {n} modes')
            if notes is not None:
                notes.append(f'pk_t data has {bow_pk.shape[-1]} entries per array for {n} modes; '
                             f'the first {n} are used, by position, as in Python')
            bow_pk = bow_pk[:n]
            edge_pk = edge_pk[..., :n]
        out['pk_t_bow_s'] = (bow_pk, False)
        if edge_vs is None:
            out['pk_t_edge_s'] = (edge_pk, False)
        else:
            # Bettis format: one edge pk_t row per measured velocity (render() lines 628-631).
            out['pk_t_edge_s'] = (None, False)
            out['pk_t_edge_by_vel_s'] = (np.ascontiguousarray(edge_pk), False)
            out['pk_t_edge_velocities'] = (edge_vs, False)
    else:
        out['pk_t_bow_s'] = (None, False)
        out['pk_t_edge_s'] = (None, False)
    if eng._vel_tilt is not None:
        vt_vels, vt_fracs, vt_ref, vt_mband, nvb = eng._vel_tilt
        out['vel_tilt_velocities'] = (vt_vels, False)
        out['vel_tilt_fractions'] = (vt_fracs, False)
        out['vel_tilt_reference'] = (vt_ref, False)
        out['vel_tilt_mode_band'] = (vt_mband, True)
    else:
        for k in ('vel_tilt_velocities', 'vel_tilt_fractions', 'vel_tilt_reference', 'vel_tilt_mode_band'):
            out[k] = (None, False)
    # Bow-onset band of each mode, computed exactly as lines 436-438. The engine
    # only stores it when an overlay has active gains; checked against it below.
    edges = np.array(BOW_ONSET_EDGES, dtype=np.float64)
    mband = np.searchsorted(edges[1:], eng._bF).clip(0, len(BOW_ONSET_EDGES) - 2).astype(np.int32)
    if eng._bow_onset_mode_band is not None and not np.array_equal(mband, eng._bow_onset_mode_band):
        raise BakeError('bow-onset mode band recomputation differs from the engine')
    out['bow_onset_mode_band'] = (mband, True)
    return out


def scalar_shared(eng):
    g = engine_globals(eng)
    p = eng._params
    return {
        'sample_rate': int(g['SR']),
        'log1000': float(g['LOG1000']),
        'two_pi': float(2. * np.pi),
        'seed': int(p['seed']),
        'n_body': int(len(eng._bF)),
        'band_env_birth_frac': float(eng._beo_birth_frac),
    }


def effective_params(eng):
    """Effective values of every switch the C++ stage gate needs, mirroring the
    p.get(...) defaults in multi_strike_engine_v19.py."""
    p = eng._params
    gains = eng._bow_onset_band_gains
    return {
        'articulation': str(p['articulation']),
        'seed': int(p['seed']),
        'vel_participation': eng._vel_curves is not None,
        'bell_vel_participation': eng._bell_vel_curves is not None,
        'vel_gain_power': float(p.get('vel_gain_power', 1.0)),
        'contact_tau_ms': float(p.get('contact_tau_ms', 0.0)),
        'edge_level_boost': float(p.get('edge_level_boost', 1.0)),
        'hammering_tilt': float(p.get('hammering_tilt', 0.0)),
        'tip_contact_ms': float(p.get('tip_contact_ms', 0.0)),
        'shld_contact_ms': float(p.get('shld_contact_ms', 1.2)),
        'permode_contact': bool(p.get('permode_contact', False)),
        'tip_tau0': float(p.get('tip_tau0', 3.0)),
        'tip_tau_max': float(p.get('tip_tau_max', 10.0)),
        'shld_tau0': float(p.get('shld_tau0', 10.0)),
        'shld_tau_max': float(p.get('shld_tau_max', 35.0)),
        'sigma_ratio': float(p.get('sigma_ratio', 0.35)),
        'release_gain': float(p.get('release_gain', 0.0)),
        'wander_depth': float(p['wander_depth']),
        'wander_rate_hz': float(p['wander_rate_hz']),
        'edge_wander_depth': float(p.get('edge_wander_depth', 0.0)),
        'edge_wander_rate': float(p.get('edge_wander_rate', 0.0)),
        'rate_jitter': float(p.get('rate_jitter', 0.0)),
        'lf_sens_depth': float(eng._lf_depth),
        'lf_sens_tau': float(eng._lf_tau),
        'lf_min_scale': float(eng._lf_min),
        'lf_max_scale': float(eng._lf_max),
        'lf_edge_weight': float(eng._lf_w_edge),
        'lf_bow_weight': float(eng._lf_w_bow),
        'lf_bell_weight': float(eng._lf_w_bell),
        'lf_cutoff_hz': float(eng._lf_cutoff_hz),
        'lf_wander_rate_boost': float(p.get('lf_wander_rate_boost', 0.0)),
        'mf_sens_depth': float(eng._mf_depth),
        'mf_sens_tau': float(eng._mf_tau),
        'mf_min_scale': float(eng._mf_min),
        'mf_max_scale': float(eng._mf_max),
        'mf_edge_weight': float(eng._mf_w_edge),
        'mf_bow_weight': float(eng._mf_w_bow),
        'mf_bell_weight': float(eng._mf_w_bell),
        'mf_lo_hz': float(eng._mf_lo_hz),
        'mf_hi_hz': float(eng._mf_hi_hz),
        'vel_len_tilt': float(p.get('vel_len_tilt', 0.0)),
        'vel_len_tilt_ref': float(p.get('vel_len_tilt_ref', 0.787)),
        'band_env_onset': eng._beo_pk_t is not None,
        'band_env_birth_frac': float(eng._beo_birth_frac),
        'vel_tilt': eng._vel_tilt is not None,
        'bow_onset_gains_active': gains is not None,
        'nl_amount': float(eng._nl_amount),
        'hf_bloom_modes': int(eng._hf_n),
        'hf_bloom_gain': float(p['hf_bloom_gain']),
        'sparkle_clusters': int(len(eng._clusters)),
        'sparkle_depth': float(p['sparkle_depth']),
        'stick_gain': float(p['stick_gain']),
        'atk_noise': float(p['atk_noise']),
        'edge_band_env': eng._edge_benv is not None,
        'edge_npz': eng._edge_npz is not None,
        **({} if eng._vel_curves is not None else {
            # hertz_vel_scale (line 979) is used when there is no velocity participation table.
            'tau_ref_ms': float(p['tau_ref_ms']),
        }),
    }


def edge_npz_block(eng):
    """The NPZ edge data (__init__ lines 329-345) and the switches render() and
    _apply_strike() read with it (lines 533-573, 869-894, 1043-1061). None without NPZ."""
    if eng._edge_npz is None:
        return None
    p = eng._params
    evels, velw, env, band, fdur = eng._edge_npz
    n = len(eng._bF)
    if velw.shape != (len(evels), n) or band.shape != (n,):
        raise BakeError(f'FAIL: NPZ edge data is for {velw.shape[1]} modes, the preset has {n}')
    if band.min() < 0 or band.max() >= env.shape[2]:
        raise BakeError('FAIL: NPZ our_band_idx is outside the envelope bands')
    corr = p.get('edge_rms_correction', None)
    if corr is not None and len(corr) != env.shape[2]:
        raise BakeError(f'FAIL: edge_rms_correction has {len(corr)} values for {env.shape[2]} NPZ bands')
    return {
        'velocities': enc(evels),
        'velw': enc(velw),
        'envelopes': enc(env),              # velocities x frames x bands, zero-padded as in Python
        'band_index': enc(band, True),
        'frame_dur_s': float(fdur),
        'use_velw': bool(p.get('edge_use_velw', True)),
        'velw_normalize': bool(p.get('edge_velw_normalize', True)),
        'level_vel_gain_power': float(p.get('vel_gain_power', 0.7)),   # line 1058 default
        'rms_correction': enc_opt(None if corr is None else np.array(corr, dtype=np.float64)),
        # The envelope is built only when an edge band envelope file is also set (line 691).
        'envelope_active': eng._edge_benv is not None,
        'onset_s': float(p.get('edge_band_env_onset_s', 1.5)),         # line 533
        'frame_decay_period_s': 0.020,                                   # line 889
    }


def python_contact_force(eng, SE, position):
    """The per-mode contact force matrix Python builds for a strike at this position
    (_apply_strike lines 1067-1093), as a check value for the C++ computation. Mutates
    the engine, so it runs after everything else has been extracted."""
    p = eng._params
    if not (float(p.get('tip_contact_ms', 0.0)) > 0.0 and p.get('permode_contact', False)):
        return None
    eng.reset()
    eng._apply_strike(SE(0.0, 1.0, position), np.random.default_rng(0))
    kind, force, _ = eng._active_contact
    if kind != 'permode':
        raise BakeError(f'expected a permode contact, got {kind!r}')
    eng.reset()
    return force


def json_safe(v):
    if isinstance(v, (bool, int, str)) or v is None:
        return v
    if isinstance(v, float):
        return v if np.isfinite(v) else repr(v)
    if isinstance(v, (list, tuple)):
        return [json_safe(x) for x in v]
    if isinstance(v, dict):
        return {str(k): json_safe(x) for k, x in v.items()}
    if isinstance(v, (np.floating, np.integer, np.bool_)):
        return json_safe(v.item())
    return repr(v)


def repo_rel(path):
    try:
        return os.path.relpath(path, ab.REPO)
    except ValueError:
        return path


def slot_block(eng, name, overlay, position, preset):
    p = eng._params
    raw_params = {k: (repo_rel(v) if k.endswith('_path') and isinstance(v, str) else v) for k, v in p.items()}
    gains = eng._bow_onset_band_gains
    return {
        'name': name,
        'overlay': overlay,
        'overlay_file': None if overlay is None else repo_rel(ab.overlay_path(overlay, preset)),
        'position': float(position),
        'calib_scalar': float(eng._calib_scalar),
        'bow_vp': (None if eng._vel_curves is None else
                   {'vel_norms': enc(eng._vel_norms), 'curves': enc(eng._vel_curves)}),
        'bell_vp': (None if eng._bell_vel_curves is None else
                    {'vel_norms': enc(eng._bell_vel_norms), 'curves': enc(eng._bell_vel_curves)}),
        'onset_gains': enc_opt(gains),
        'onset_tau_s': float(eng._bow_onset_tau),
        'effective': effective_params(eng),
        'params': json_safe(raw_params),
    }


def copied_entry(src, name, articulation, position):
    """Entry `name`: a copy of the slot block `src` with its own articulation id and position.
    The source's level trim, band gains and onset shift are not copied (a re-bake has none)."""
    blk = {'name': name, 'articulation': articulation, 'copy_of': src['name']}
    for key, val in src.items():
        if key not in blk and key not in ('level_trim', 'excitation_band_gains_db', 'onset_shift'):
            blk[key] = copy.deepcopy(val)
    blk['position'] = float(position)
    return blk


def fix_vp_columns(slot_blocks, fixes, notes):
    """Apply VP_COLUMN_FIXES to every slot block with the fix's overlay, copies included."""
    for overlay, table, path, labels, old, new, reason in fixes:
        with open(os.path.join(ab.REPO, path)) as f:
            if json.load(f).get('vel_labels') != labels:
                raise BakeError(f'FAIL: {path} does not have the column labels {labels}')
        cols = [k for k, x in enumerate(labels) if x == old]
        src = labels.index(new)
        names, below = [], None
        for blk in slot_blocks:
            if blk['overlay'] != overlay:
                continue
            vp = blk[table]
            if vp is None or vp['curves']['shape'][1] != len(labels):
                raise BakeError(f'FAIL: slot {blk["name"]}: {table} does not have {len(labels)} columns')
            c = np.array(vp['curves']['data'], dtype=np.float64).reshape(vp['curves']['shape'])
            c[:, cols] = c[:, [src]]
            vp['curves'] = enc(c)
            names.append(blk['name'] + (f' (a copy of {blk["copy_of"]})' if 'copy_of' in blk else ''))
            below = vp['vel_norms']['data'][cols[0] - 1] if cols[0] > 0 else None
        if not names:
            raise BakeError(f'FAIL: no slot plays overlay {overlay}')
        notes.append(f'{overlay} {table} in slots {", ".join(names)}: every {old} column '
                     f'({", ".join(str(k) for k in cols)}) replaced with {new} (column {src}), vel_norms '
                     f'kept, so every velocity plays {new}'
                     + (f' and nothing up to velocity {below} changes' if below is not None else '')
                     + f'; {path} is unchanged. Why: {reason}.')


# ---------------------------------------------------------------------------
# RNG streams
# ---------------------------------------------------------------------------
def rng_stream(seed, n_raw_check=8):
    g = np.random.default_rng(seed)
    st = g.bit_generator.state
    if st['bit_generator'] != 'PCG64':
        raise BakeError(f'numpy default_rng is {st["bit_generator"]}, expected PCG64')
    raw = np.random.default_rng(seed).bit_generator.random_raw(n_raw_check)
    return {
        'seed': int(seed),
        'state': '0x%032x' % st['state']['state'],
        'inc': '0x%032x' % st['state']['inc'],
        'first_raw_u64': ['0x%016x' % int(x) for x in raw],
    }


def rng_block(seed, n_body):
    phases_head = np.random.default_rng(seed).uniform(0., 2. * np.pi, n_body)[:8]
    return {
        'algorithm': 'numpy PCG64 (XSL-RR 128/64), SeedSequence-seeded; states are post-seeding',
        'streams': {
            # __init__ line 205: pick vectors; render() line 504: strike phases (same seed)
            'pick': rng_stream(seed),
            'render': rng_stream(seed),
            # __init__ line 209
            'wander': rng_stream(seed + 0xA2A2A2),
            # _compute_calib_scalar line 462 (calibration is baked, listed for completeness)
            'calibration': rng_stream(42),
        },
        'render_phase_uniform': {'low': 0.0, 'high': float(2. * np.pi), 'per_strike': int(n_body)},
        'render_phase_head': [float(x) for x in phases_head],
    }


# ---------------------------------------------------------------------------
# provenance
# ---------------------------------------------------------------------------
def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def manifest_md5s():
    path = os.path.join(ab.REPO, 'docs', 'BASELINE_MANIFEST.md')
    table = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'\|\s*`([^`]+)`\s*\|\s*`([0-9a-f]{32})`\s*\|', line)
            if m and '/' in m.group(1):
                table[m.group(1)] = m.group(2)
    return table


def source_files(slots, preset_key):
    preset_path = ab.PRESETS[preset_key]['preset']
    with open(preset_path) as f:
        preset = json.load(f)
    base = os.path.dirname(preset_path)
    files = [os.path.join(ab.PROTOTYPE, 'engine', n) for n in sorted(os.listdir(os.path.join(ab.PROTOTYPE, 'engine')))
             if n.endswith('.py')]
    files.append(os.path.join(ab.PROTOTYPE, 'load_preset.py'))
    files.append(preset_path)
    data = set(preset['data_files'].values())
    for k in ('band_env_onset_path', 'vel_tilt_path', 'edge_band_env_path', 'edge_npz_path'):
        if k in preset['parameters']:
            data.add(preset['parameters'][k])
    for _, overlay, _ in slots:
        if overlay is None:
            continue
        files.append(ab.overlay_path(overlay, preset_key))
        with open(ab.overlay_path(overlay, preset_key)) as f:
            data.update(json.load(f).get('data_files', {}).values())
    files += [os.path.join(base, d) for d in sorted(data)]
    return files


def provenance(slots, preset_key, allow_mismatch):
    manifest = manifest_md5s()
    out = {}
    bad = []
    for path in source_files(slots, preset_key):
        rel = repo_rel(path)
        digest = md5(path)
        locked = manifest.get(rel)
        out[rel] = {'md5': digest, 'manifest_md5': locked,
                    'matches_manifest': None if locked is None else digest == locked}
        if locked is not None and digest != locked:
            bad.append(rel)
    if bad and not allow_mismatch:
        raise BakeError('FAIL: locked files differ from docs/BASELINE_MANIFEST.md: ' + ', '.join(bad))
    return out


# ---------------------------------------------------------------------------
def same(a, b):
    if a is None or b is None:
        return a is None and b is None
    a = np.asarray(a)
    b = np.asarray(b)
    return a.shape == b.shape and a.dtype == b.dtype and np.array_equal(a, b)


def bake(overrides, preset='nova', allow_manifest_mismatch=False):
    from scipy.signal import butter, sosfilt
    import scipy

    slots = ab.PRESETS[preset]['slots']
    engines = []
    strike_event = None
    for name, overlay, pos in slots:
        eng, SE = ab.load_python_engine(overlay, overrides, preset)
        strike_event = SE
        engines.append((name, overlay, pos, eng))

    ref_name, _, _, ref_eng = engines[0]
    notes = []
    ref_arrays = shared_arrays(ref_eng, notes)
    ref_scalars = scalar_shared(ref_eng)
    ref_npz = edge_npz_block(ref_eng)
    for name, _, _, eng in engines[1:]:
        if edge_npz_block(eng) != ref_npz:
            raise BakeError(f'FAIL: NPZ edge data differs between slots {ref_name} and {name}')
        arrays = shared_arrays(eng)
        for key, (val, _) in arrays.items():
            if not same(val, ref_arrays[key][0]):
                n_diff = ('shape/None mismatch' if val is None or ref_arrays[key][0] is None
                          or np.shape(val) != np.shape(ref_arrays[key][0])
                          else f'{int(np.sum(np.asarray(val) != np.asarray(ref_arrays[key][0])))} elements differ')
                raise BakeError(f'FAIL: shared array {key!r} differs between slots {ref_name} and {name} ({n_diff})')
        if scalar_shared(eng) != ref_scalars:
            raise BakeError(f'FAIL: shared scalars differ between slots {ref_name} and {name}: '
                            f'{scalar_shared(eng)} vs {ref_scalars}')

    g = engine_globals(ref_eng)
    sr = int(g['SR'])
    sos = butter(2, 120 / (sr / 2), btype='high', output='sos')
    probe = np.random.default_rng(1).standard_normal(4096)
    if not np.array_equal(g['_hp'](probe, 120), sosfilt(sos, probe)):
        raise BakeError('FAIL: baked high-pass SOS does not reproduce dsp_filters._hp exactly')

    if ref_npz is not None and preset in ENVELOPE_ON and not ref_npz['envelope_active']:
        ref_npz['envelope_active'] = True
        ref_npz['envelope_set_by'] = ENVELOPE_ON[preset]
        notes.append('edge_npz.envelope_active is set by the bake: ' + ENVELOPE_ON[preset])

    shared = {k: (None if v is None else enc(v, integer)) for k, (v, integer) in ref_arrays.items()
              if not (v is None and k in OPTIONAL_SHARED)}
    shared['bow_onset_band_edges_hz'] = enc(np.array(BOW_ONSET_EDGES, dtype=np.float64))

    slot_blocks = [slot_block(eng, name, overlay, pos, preset) for name, overlay, pos, eng in engines]
    for blk, (name, overlay, pos, eng) in zip(slot_blocks, engines):
        blk['contact_force_python'] = enc_opt(python_contact_force(eng, strike_event, pos))
    source_engine = [eng for _, _, _, eng in engines]  # the engine whose data each block holds
    for name, articulation, src_name, pos in COPIED_ENTRIES.get(preset, []):
        names = [blk['name'] for blk in slot_blocks]
        k = names.index(src_name)
        blk = copied_entry(slot_blocks[k], name, articulation, pos)
        blk['contact_force_python'] = enc_opt(python_contact_force(source_engine[k], strike_event, pos))
        if name in names:  # replaces that entry where it stands
            slot_blocks[names.index(name)] = blk
            source_engine[names.index(name)] = source_engine[k]
        else:
            slot_blocks.append(blk)
            source_engine.append(source_engine[k])
    fix_vp_columns(slot_blocks, VP_COLUMN_FIXES.get(preset, []), notes)

    out = {
        'format': BAKE_FORMAT,
        'format_version': BAKE_FORMAT_VERSION,
        'generator': 'tools/bake_preset.py',
        'created_utc': ab.utc_stamp(),
        'python': {'version': platform.python_version(), 'numpy': np.__version__,
                   'scipy': scipy.__version__, 'platform': platform.platform()},
        'preset': repo_rel(ab.PRESETS[preset]['preset']),
        'overrides': json_safe(dict(overrides)),
        'sample_rate': sr,
        'constants': {
            'log1000': ref_scalars['log1000'],
            'two_pi': ref_scalars['two_pi'],
            'amp_skip_threshold': AMP_SKIP_THRESHOLD,
            'segment_skip_s': 1e-4,
        },
        'n_modes': ref_scalars['n_body'],
        'hp_right': {'cutoff_hz': 120.0, 'order': 2, 'sos': enc(sos)},
        'shared': shared,
        'rng': rng_block(ref_scalars['seed'], ref_scalars['n_body']),
        'slots': slot_blocks,
        'sources': provenance([(n, o, p) for n, o, p, _ in engines], preset, allow_manifest_mismatch),
    }
    # Keys only for the mechanisms a preset uses, so the Nova bake keeps its layout.
    if ref_npz is not None:
        out['edge_npz'] = ref_npz
    if notes:
        out['notes'] = notes
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--overrides', help='overrides as a JSON object')
    src.add_argument('--overrides-from', help='take the overrides from a sequence JSON file')
    ap.add_argument('--out', required=True, help='output bake JSON path')
    ap.add_argument('--preset', choices=sorted(ab.PRESETS), default='nova',
                    help='which preset to bake (default nova); the bake entries are listed in ab_common.PRESETS')
    ap.add_argument('--allow-manifest-mismatch', action='store_true',
                    help='record, but do not fail on, locked files whose MD5 differs from the manifest')
    args = ap.parse_args()

    if args.overrides_from:
        overrides = ab.load_sequence(args.overrides_from)['overrides']
    else:
        overrides = json.loads(args.overrides)
        if not isinstance(overrides, dict):
            raise SystemExit('--overrides must be a JSON object')

    data = bake(overrides, preset=args.preset, allow_manifest_mismatch=args.allow_manifest_mismatch)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as f:
        f.write(dump_json(data) + '\n')
    print(f'baked {len(data["slots"])} slots, {data["n_modes"]} modes -> {args.out} '
          f'({os.path.getsize(args.out) / 1e6:.1f} MB)')
    for s in data['slots']:
        print(f'  {s["name"]:6s} overlay={str(s["overlay"]):14s} pos={s["position"]:.1f} calib={s["calib_scalar"]!r}')
    for note in data.get('notes', []):
        print(f'  note: {note}')


if __name__ == '__main__':
    main()
