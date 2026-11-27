"""REFERENCE ONLY (project session, 2026-11-27): how the approved NOVA RIDE edge v2 test bake was assembled.

    python3 -I assemble_reference.py SHIPPED_NOVA_BAKE EDGE_V2_DIR OUT_BAKE

Starts from the shipped nova_ride.bake.json and changes nothing but:
  * the 34 added modes, inserted at their frequency into every per-mode array (nova_edge_v2_modes.json);
  * the edge NPZ block and the per-mode edge peak times (nova_edge_v2_npz.json);
  * every slot's edge parameters and flags, the EDGE level trim, two tuning values (nova_edge_v2_settings.json);
  * the stereo picks of the modes in nova_edge_v2_picks.json.
The real build step belongs in tools/ (Claude Code); this file only documents the approved result exactly.
"""
import sys, os, json, struct
import numpy as np


def fnv(vals):
    h = 0xcbf29ce484222325
    for x in vals:
        for byte in struct.pack('<d', float(x)):
            h ^= byte; h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return '0x%016x' % h


def arr(a):
    a = np.asarray(a, float); flat = [float(x) for x in a.ravel()]
    return dict(shape=list(a.shape), fnv1a64=fnv(flat), data=flat)


def get(o):
    return np.array(o['data'], float).reshape(o['shape'])


ship_p, d, out_p = sys.argv[1:4]
b = json.load(open(ship_p))
M = json.load(open(os.path.join(d, 'nova_edge_v2_modes.json')))
Z = json.load(open(os.path.join(d, 'nova_edge_v2_npz.json')))
St = json.load(open(os.path.join(d, 'nova_edge_v2_settings.json')))
sh = b['shared']; n0 = b['n_modes']; n1 = M['n_modes']; order = np.array(M['order'])
assert n0 == M['n_nova']
add = M['added']; pos_add = np.nonzero(order < 0)[0]; near = np.array([a['nearest_nova'] for a in add])
own = {'mode_id': 'mode_id', 'freq_hz': 'f', 't60_s': 't60_s', 'drift_hz': 'drift_hz', 'pick_l': 'pick_l', 'pick_r': 'pick_r',
       'lfo_rate_hz': 'lfo_rate_hz', 'lfo_phase': 'lfo_phase'}
zero = {'exc_norm', 'bell_weight', 'pk_t_bow_s'}
sr = float(b['sample_rate'])


def expand(a, new_vals):
    out = np.empty(n1); out[order >= 0] = a[order[order >= 0]]; out[pos_add] = new_vals; return out


for key in list(sh):
    o = sh[key]
    if not (isinstance(o, dict) and 'shape' in o):
        continue
    a = get(o)
    if a.ndim != 1 or a.shape[0] != n0:
        continue
    if key in own:
        nv = np.array([x[own[key]] for x in add], float)
    elif key in zero:
        nv = np.zeros(len(add))
    elif key in ('mode_step_re', 'mode_step_im'):
        t60 = np.array([x['t60_s'] for x in add]); f = np.array([x['f'] for x in add])
        mag = np.exp(-6.907755278982137 / (t60 * sr)); ang = 2 * np.pi * f / sr
        nv = mag * (np.cos(ang) if key.endswith('re') else np.sin(ang))
    else:
        nv = a[near]                      # frequency-shaped values from the nearest NOVA mode
    sh[key] = arr(expand(a, nv))
sh['pk_t_edge_velocities'] = arr(Z['pk_t_edge_velocities'])
sh['pk_t_edge_by_vel_s'] = arr(Z['pk_t_edge_by_vel_s'])
for s in b['slots']:
    for key in ('bow_vp', 'bell_vp'):
        c = get(s[key]['curves']); o2 = np.zeros((n1, c.shape[1])); o2[order >= 0] = c[order[order >= 0]]
        s[key]['curves'] = arr(o2)
    c = get(s['contact_force_python']); o2 = np.empty((n1, c.shape[1])); o2[order >= 0] = c[order[order >= 0]]; o2[pos_add] = c[near]
    s['contact_force_python'] = arr(o2)
    s['params'].update(St['slot_params']); s['effective'].update(St['effective_flags'])
    if s['name'] == 'EDGE':
        s['level_trim']['gain_db'] = St['edge_level_trim_db']
b['edge_npz'] = dict(velocities=arr(Z['velocities']), velw=arr(Z['velw']), envelopes=arr(Z['envelopes']),
                     band_index=arr(Z['band_index']), frame_dur_s=Z['frame_dur_s'], use_velw=Z['use_velw'],
                     velw_normalize=Z['velw_normalize'], level_vel_gain_power=Z['level_vel_gain_power'],
                     rms_correction=arr(Z['rms_correction']), envelope_active=Z['envelope_active'], onset_s=Z['onset_s'],
                     frame_decay_period_s=Z['frame_decay_period_s'])
b['tuning'].setdefault('cymbal', {}).update(St['tuning_cymbal'])
for art, v in St['tuning_articulations'].items():
    b['tuning'].setdefault('articulations', {}).setdefault(art, {}).update(v)
pk_path = os.path.join(d, 'nova_edge_v2_picks.json')
if os.path.exists(pk_path):                 # the edge's own modes placed in the cymbal's stereo field (2026-11-27)
    P = json.load(open(pk_path))
    L = get(sh['pick_l']); R = get(sh['pick_r'])
    for m in P['modes']:
        L[m['index']] = m['pick_l']; R[m['index']] = m['pick_r']
    sh['pick_l'] = arr(L); sh['pick_r'] = arr(R)
b['n_modes'] = n1
b['rng']['render_phase_uniform']['per_strike'] = n1
json.dump(b, open(out_p, 'w'))
print('assembled', n0, '->', n1, 'modes')
