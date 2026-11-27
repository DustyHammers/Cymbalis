#!/usr/bin/env python3
"""NOVA RIDE edge v2: give NOVA's bake its real EDGE, from Kris's Edge V1-V4 captures.

    python tools/apply_nova_edge_v2.py [resources/presets/nova_ride.bake.json] [--out OUT]

Approved by Kris on 2026-11-27 (docs/PORT_MAP.md, "NOVA RIDE edge v2"). The data is
presets/nova/edge_v2/ (checked against docs/BASELINE_MANIFEST.md); presets/nova/edge_v2/
assemble_reference.py documents how the approved test bake was made, and this step makes the
same bake but for the order of the random phases (below). The bake is rewritten in place
unless --out is given. It changes nothing but:

  * the 34 added modes (nova_edge_v2_modes.json), inserted at their frequency into every
    per-mode array, so the modes stay in frequency order (the NPZ envelope bands must rise with
    the mode index). Their own values (frequency, T60, drift, mode id, stereo picks, LFO rate
    and phase) come from the file; exc_norm, bell_weight and the bow peak times are 0 (BOW and
    BELL put nothing into them); mode_step is computed from their frequency and T60; every other
    per-mode value, and their contact force rows, are copied from the NOVA mode the file names
    (frequency-shaped values). The bow and bell velocity tables give them nothing. Every NOVA
    mode keeps every value, in its order;
  * the edge NPZ block and the per-mode edge peak times (nova_edge_v2_npz.json);
  * every slot's edge parameters and flags, the EDGE level trim, and two tuning values: the 20 ms
    frame-decay restart off and the edge articulation's velocity curve (nova_edge_v2_settings.json);
  * "added_modes": the added modes' indices, which the plugin's loader needs (plugin/dsp/Bake.h):
    it checks the NOVA modes' seeded values against NOVA's streams and takes the added modes' as
    baked. The strike phases: the NOVA modes draw from the render stream exactly as before
    (render_phase_uniform.per_strike stays NOVA's own count), the added modes from a stream of
    their own after them, so BOW, BELL, BELL2 and BOW2 play bit for bit as before. The
    reference assembly instead set per_strike to the new count (one stream in index order);
  * "edge_v2": the record that this step ran. It refuses to run twice.

Where it sits in NOVA's pipeline (docs/BUILDS.md): NOVA can no longer be re-baked from presets/
alone. After tools/bake_preset.py --preset nova, this step; then the level trims, keeping EDGE's
(tools/set_level_trims.py ... --keep EDGE: edge v2 sets it, not re-measured), and the skip
thresholds (tools/set_skip_thresholds.py). On the bake as it was on main (trims and skip
thresholds already there), only EDGE's skip threshold is measured again:

    build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --skip-thresholds --json skip.json
    python tools/set_skip_thresholds.py resources/presets/nova_ride.bake.json skip.json --only EDGE
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_preset import dump_json, enc  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'presets', 'nova', 'edge_v2')
FILES = ('nova_edge_v2_modes.json', 'nova_edge_v2_npz.json', 'nova_edge_v2_settings.json')
LN1000 = 6.907755278982137
# The added modes' own values, by bake key: the key in nova_edge_v2_modes.json's "added".
OWN = {'mode_id': 'mode_id', 'freq_hz': 'f', 't60_s': 't60_s', 'drift_hz': 'drift_hz', 'pick_l': 'pick_l',
       'pick_r': 'pick_r', 'lfo_rate_hz': 'lfo_rate_hz', 'lfo_phase': 'lfo_phase'}
ZERO = ('exc_norm', 'bell_weight', 'pk_t_bow_s')


def manifest_md5():
    """The locked MD5s of presets/nova/edge_v2/ (docs/BASELINE_MANIFEST.md)."""
    out = {}
    with open(os.path.join(ROOT, 'docs', 'BASELINE_MANIFEST.md')) as f:
        for line in f:
            m = re.match(r'\| `presets/nova/edge_v2/([^`]+)` \| `([0-9a-f]{32})` \|', line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


def md5(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def get(o):
    return np.array(o['data'], dtype=np.float64).reshape(o['shape'])


def put(values, like):
    """values encoded as the bake encodes arrays, integers where the original was integers."""
    integer = bool(like['data']) and all(isinstance(x, int) and not isinstance(x, bool) for x in like['data'])
    return enc(np.asarray(values), integer=integer)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bake', nargs='?', default=os.path.join(ROOT, 'resources', 'presets', 'nova_ride.bake.json'),
                    help='NOVA RIDE bake (rewritten in place unless --out)')
    ap.add_argument('--out', help='write here instead')
    args = ap.parse_args()

    locked = manifest_md5()
    md5s = {}
    for name in FILES:
        md5s[name] = md5(os.path.join(DATA, name))
        if locked.get(name) != md5s[name]:
            raise SystemExit(f'{name}: MD5 {md5s[name]} does not match docs/BASELINE_MANIFEST.md ({locked.get(name)})')
    with open(args.bake) as f:
        b = json.load(f)
    if b.get('preset') not in ('nova', 'nova_ride') and 'nova' not in str(b.get('preset', '')).lower():
        raise SystemExit(f'{args.bake}: not NOVA RIDE\'s bake (preset {b.get("preset")!r})')
    if 'edge_v2' in b or 'added_modes' in b:
        raise SystemExit(f'{args.bake}: edge v2 is already applied ({b.get("edge_v2", {}).get("applied_utc", "added_modes present")})')
    with open(os.path.join(DATA, FILES[0])) as f:
        M = json.load(f)
    with open(os.path.join(DATA, FILES[1])) as f:
        Z = json.load(f)
    with open(os.path.join(DATA, FILES[2])) as f:
        St = json.load(f)

    sh = b['shared']
    n0, n1 = int(b['n_modes']), int(M['n_modes'])
    if n0 != int(M['n_nova']):
        raise SystemExit(f'the bake has {n0} modes, edge v2 is for NOVA\'s {M["n_nova"]}')
    order = np.array(M['order'], dtype=np.int64)
    own_pos = np.nonzero(order >= 0)[0]
    add_pos = np.nonzero(order < 0)[0]
    added = M['added']
    if len(order) != n1 or len(add_pos) != len(added) or not np.array_equal(order[own_pos], np.arange(n0)):
        raise SystemExit('nova_edge_v2_modes.json: "order" must keep every NOVA mode, in its order, and place each added mode')
    near = np.array([a['nearest_nova'] for a in added], dtype=np.int64)
    sr = float(b['sample_rate'])

    def expand(a, new_values):
        out = np.empty((n1,) + a.shape[1:])
        out[own_pos] = a
        out[add_pos] = new_values
        return out

    # Every per-mode array of the shared block.
    for key in list(sh):
        o = sh[key]
        if not (isinstance(o, dict) and 'shape' in o):
            continue
        a = get(o)
        if a.ndim != 1 or a.shape[0] != n0:
            continue
        if key in OWN:
            nv = np.array([x[OWN[key]] for x in added], dtype=np.float64)
        elif key in ZERO:
            nv = np.zeros(len(added))
        elif key in ('mode_step_re', 'mode_step_im'):
            t60 = np.array([x['t60_s'] for x in added])
            f = np.array([x['f'] for x in added])
            mag = np.exp(-LN1000 / (t60 * sr))
            ang = 2 * np.pi * f / sr
            nv = mag * (np.cos(ang) if key.endswith('re') else np.sin(ang))
        else:
            nv = a[near]
        sh[key] = put(expand(a, nv), o)
    freq = get(sh['freq_hz'])
    if not np.all(np.diff(freq) > 0):
        raise SystemExit('the modes are not in frequency order after the insertion')
    sh['pk_t_edge_velocities'] = enc(np.array(Z['pk_t_edge_velocities'], dtype=np.float64))
    sh['pk_t_edge_by_vel_s'] = enc(np.array(Z['pk_t_edge_by_vel_s'], dtype=np.float64))

    # The slots: the velocity tables give the added modes nothing; their contact force rows are
    # their NOVA mode's; every slot's edge parameters and flags; EDGE's trim.
    for s in b['slots']:
        for key in ('bow_vp', 'bell_vp'):
            if s.get(key) is None:
                continue
            c = get(s[key]['curves'])
            s[key]['curves'] = put(expand(c, np.zeros((len(added), c.shape[1]))), s[key]['curves'])
        c = get(s['contact_force_python'])
        s['contact_force_python'] = put(expand(c, c[near]), s['contact_force_python'])
        s['params'].update(St['slot_params'])
        s['effective'].update(St['effective_flags'])
        if s['name'] == 'EDGE':
            before = s.get('level_trim') or {}
            s['level_trim'] = {
                'gain_db': float(St['edge_level_trim_db']),
                'set_by': 'tools/apply_nova_edge_v2.py (presets/nova/edge_v2/nova_edge_v2_settings.json)',
                'note': St['edge_level_trim_note'] + '; not re-measured',
                'previous_gain_db': before.get('gain_db'),
            }

    b['edge_npz'] = {
        'velocities': enc(np.array(Z['velocities'], dtype=np.float64)),
        'velw': enc(np.array(Z['velw'], dtype=np.float64)),
        'envelopes': enc(np.array(Z['envelopes'], dtype=np.float64)),
        'band_index': enc(np.array(Z['band_index']), integer=True),
        'frame_dur_s': Z['frame_dur_s'],
        'use_velw': Z['use_velw'],
        'velw_normalize': Z['velw_normalize'],
        'level_vel_gain_power': Z['level_vel_gain_power'],
        'rms_correction': enc(np.array(Z['rms_correction'], dtype=np.float64)),
        'envelope_active': Z['envelope_active'],
        'onset_s': Z['onset_s'],
        'frame_decay_period_s': Z['frame_decay_period_s'],
        'source': 'presets/nova/edge_v2/nova_edge_v2_npz.json (Kris\'s Edge V1-V4 captures, 2026-11-27)',
    }
    if not np.all(np.diff(np.array(Z['band_index'])) >= 0):
        raise SystemExit('nova_edge_v2_npz.json: band_index must rise with the mode index')

    tun = b.setdefault('tuning', {})
    tun.setdefault('cymbal', {}).update(St['tuning_cymbal'])
    for art, v in St['tuning_articulations'].items():
        tun.setdefault('articulations', {}).setdefault(art, {}).update(v)

    b['n_modes'] = n1
    # render_phase_uniform.per_strike stays NOVA's own count: the NOVA modes draw from the
    # render stream as before, the added modes from a stream of their own (plugin/dsp/FastEngine.h).
    now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    b['added_modes'] = {
        'indices': [int(k) for k in add_pos],
        'set_by': 'tools/apply_nova_edge_v2.py',
        'note': ('edge-only modes inserted at their frequency (presets/nova/edge_v2/nova_edge_v2_modes.json); their seeded '
                 'values are as baked, NOVA\'s own modes are checked against NOVA\'s streams; a strike\'s phases: NOVA\'s '
                 'modes from the render stream as before, the added modes after them from a stream of their own'),
    }
    b['edge_v2'] = {
        'applied_by': 'tools/apply_nova_edge_v2.py',
        'applied_utc': now,
        'source': 'presets/nova/edge_v2',
        'md5': md5s,
        'modes': f'{n0} -> {n1} ({len(added)} added)',
        'approved': 'Kris, 2026-11-27, by ear (docs/PORT_MAP.md, "NOVA RIDE edge v2")',
    }
    out = args.out or args.bake
    with open(out, 'w') as f:
        f.write(dump_json(b) + '\n')
    print(f'edge v2 applied to {os.path.relpath(out, ROOT)}: {n0} -> {n1} modes, EDGE trim {St["edge_level_trim_db"]:+.2f} dB, '
          f'edge NPZ {len(Z["velocities"])} velocities x {len(Z["envelopes"][0])} frames x {len(Z["envelopes"][0][0])} bands')


if __name__ == '__main__':
    main()
