#!/usr/bin/env python3
"""Write a tuning file of the Cymbalis Tuning build into its preset's bake.

    python tools/apply_tuning.py nova_ride.tuning.json
    python tools/apply_tuning.py nova_ride.tuning.json --bake resources/presets/nova_ride.bake.json --dry-run

The tuning file is what the Tuning window's SAVE button writes (docs/TUNING.md). It is
validated before anything is written:

  * its format ("cymbalis-tuning") and version (1);
  * its preset id against the bake: resources/presets/<preset>.bake.json;
  * its bake format version against the bake's format_version;
  * every articulation and value it names.

The file's changes are applied on top of the bake's current values (its own values and any
tuning block it already has). Then:

  * each articulation's level at velocity 1.0 goes into slots[].level_trim.gain_db;
  * each articulation's excitation band gains go into its slot's excitation data,
    slots[].excitation_band_gains_db (band edges and 22 gains in dB); all 0 dB removes it;
  * each articulation's onset shift goes into its slot's onset data, slots[].onset_shift
    (band edges, 22 delays in ms and 22 speeds); delay 0 and speed 1 everywhere removes it;
  * every other value that differs from the bake's own value goes into the bake's
    "tuning" block, which the plugin's bake loader applies; per-band cymbal values
    (ring_length_bands) as 22 values. A value equal to the bake's own
    drops out of the block, so applying the same file twice gives the same bake.

Nothing else in the bake changes. Running it on resources/presets is a separate task, done
only when Kris asks.
"""
import argparse
import datetime
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_preset import dump_json  # noqa: E402

FORMAT = 'cymbalis-tuning'
VERSION = 1
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per-cymbal values (plugin/dsp/Tuning.cpp cymbalKeys) and the bake's own value of each.
CYMBAL_KEYS = [
    'output_gain_db', 'edge_velocity_remap_exponent', 'tau_ref_ms', 'ring_length',
    'wander_depth', 'wander_rate_hz', 'edge_wander_depth', 'edge_wander_rate',
    'mf_sens_depth', 'mf_sens_tau', 'mf_min_scale', 'mf_max_scale',
    'mf_bow_weight', 'mf_edge_weight', 'mf_bell_weight', 'mf_lo_hz', 'mf_hi_hz',
    'lf_sens_depth', 'lf_sens_tau', 'lf_min_scale', 'lf_max_scale', 'lf_cutoff_hz',
    'lf_bow_weight', 'lf_edge_weight', 'lf_bell_weight', 'lf_wander_rate_boost',
    'edge_band_env_onset_s',
]
RESTART_KEY = 'edge_frame_decay_restart'
# Per-band cymbal values (22 values, lowest band first): name -> (range, unit, neutral value).
CYMBAL_BAND_KEYS = {
    'ring_length_bands': ((0.25, 4.0), 'x', 1.0),
}
LEVEL_KEY, VEL06_KEY, VEL03_KEY = 'level_db', 'vel_0.6_db', 'vel_0.3_db'
# Excitation band gains (plugin/dsp/Tuning.h): 22 one-third-octave bands, edge j at
# 11404 x 2^((j - 21) / 3) Hz, 89.1 Hz up to 14368 Hz.
BANDS_KEY, SLOT_BANDS_KEY = 'band_gains_db', 'excitation_band_gains_db'
N_BANDS = 22
BAND_MIN_DB, BAND_MAX_DB = -24.0, 12.0
BAND_EDGES_HZ = [round(11404.0 * 2.0 ** ((j - 21) / 3.0), 3) for j in range(N_BANDS + 1)]
# Onset shift per band (docs/TUNING.md): delay in ms and speed, in the same bands.
DELAY_KEY, SPEED_KEY, SLOT_SHIFT_KEY = 'onset_delay_ms', 'onset_speed', 'onset_shift'
DELAY_MIN_MS, DELAY_MAX_MS = -50.0, 200.0
SPEED_MIN, SPEED_MAX = 0.25, 4.0
POSITIVE = ('ring_length', 'edge_velocity_remap_exponent', 'wander_rate_hz')


def fail(msg):
    raise SystemExit('apply_tuning: ' + msg)


def articulation_id(slot):
    """The entry's "articulation" key (a copied entry), else its overlay, else its name in lower case."""
    if slot.get('articulation'):
        return slot['articulation']
    return slot['overlay'] if slot.get('overlay') else slot['name'].lower()


def own_values(bake):
    """The bake's values without a tuning block (what loadBake calls `untuned`)."""
    e = bake['slots'][0]['effective']
    own = {k: float(e[k]) for k in CYMBAL_KEYS if k in e}
    own['output_gain_db'] = 0.0
    own['edge_velocity_remap_exponent'] = 0.5
    own['ring_length'] = 1.0
    own.setdefault('tau_ref_ms', 0.0)
    npz = bake.get('edge_npz')
    own['edge_band_env_onset_s'] = float(npz['onset_s']) if npz else 1.5
    own[RESTART_KEY] = True
    for key, (_, _, neutral) in CYMBAL_BAND_KEYS.items():
        own[key] = [neutral] * N_BANDS
    return own


def finite(name, v):
    if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
        fail(f'{name} must be a finite number, got {v!r}')
    return float(v)


def slot_bands(slot):
    """The slot's current excitation band gains (22 dB values; 0 dB without the entry)."""
    entry = slot.get(SLOT_BANDS_KEY)
    if not entry:
        return [0.0] * N_BANDS
    edges, gains = entry.get('band_edges_hz'), entry.get('gains_db')
    if not isinstance(edges, list) or not isinstance(gains, list) \
            or len(edges) != N_BANDS + 1 or len(gains) != N_BANDS:
        fail(f'slot {slot["name"]}: {SLOT_BANDS_KEY} needs {N_BANDS + 1} band edges and {N_BANDS} gains')
    if any(abs(finite('band edge', e) - want) > 0.5 for e, want in zip(edges, BAND_EDGES_HZ)):
        fail(f'slot {slot["name"]}: {SLOT_BANDS_KEY} band edges differ from the engine\'s bands')
    return [finite(f'slot {slot["name"]} band gain', g) for g in gains]


def slot_shift(slot):
    """The slot's current onset shift: (22 delays in ms, 22 speeds); 0 and 1 without the entry."""
    entry = slot.get(SLOT_SHIFT_KEY)
    if not entry:
        return [0.0] * N_BANDS, [1.0] * N_BANDS
    edges, delays, speeds = entry.get('band_edges_hz'), entry.get('delay_ms'), entry.get('speed')
    if not all(isinstance(x, list) for x in (edges, delays, speeds)) \
            or len(edges) != N_BANDS + 1 or len(delays) != N_BANDS or len(speeds) != N_BANDS:
        fail(f'slot {slot["name"]}: {SLOT_SHIFT_KEY} needs {N_BANDS + 1} band edges, {N_BANDS} delays and {N_BANDS} speeds')
    if any(abs(finite('band edge', e) - want) > 0.5 for e, want in zip(edges, BAND_EDGES_HZ)):
        fail(f'slot {slot["name"]}: {SLOT_SHIFT_KEY} band edges differ from the engine\'s bands')
    return ([finite(f'slot {slot["name"]} onset delay', d) for d in delays],
            [finite(f'slot {slot["name"]} onset speed', v) for v in speeds])


def band_values(name, v, lo, hi, unit):
    """22 values of a band row from the tuning file, each within lo..hi."""
    if not isinstance(v, list) or len(v) != N_BANDS:
        fail(f'{name} needs {N_BANDS} values')
    vals = [finite(f'{name}[{j}]', g) for j, g in enumerate(v)]
    if any(g < lo or g > hi for g in vals):
        fail(f'{name} values must lie in {lo:g} to {hi:g} {unit}')
    return vals


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tuning', help='tuning file written by the Tuning window (SAVE)')
    ap.add_argument('--bake', help='bake to update (default: resources/presets/<preset>.bake.json)')
    ap.add_argument('--dry-run', action='store_true', help='print the changes without writing the bake')
    args = ap.parse_args()

    with open(args.tuning) as f:
        tun = json.load(f)
    if tun.get('format') != FORMAT:
        fail(f'{args.tuning} is not a Cymbalis tuning file (format {tun.get("format")!r})')
    if tun.get('version') != VERSION:
        fail(f'tuning file version {tun.get("version")!r}; this tool reads version {VERSION}')
    preset = tun.get('preset')
    if not isinstance(preset, str) or not preset:
        fail('the tuning file has no preset id')
    bake_path = args.bake or os.path.join(REPO, 'resources', 'presets', preset + '.bake.json')
    if os.path.basename(bake_path) != preset + '.bake.json':
        fail(f'the tuning file is for preset {preset!r}, but the bake is {os.path.basename(bake_path)}')
    with open(bake_path) as f:
        bake = json.load(f)
    if bake.get('format') != 'cymbalis-bake':
        fail(f'{bake_path} is not a cymbalis bake')
    if tun.get('bake_format_version') != bake.get('format_version'):
        fail(f'the tuning file was made for bake format {tun.get("bake_format_version")!r}; '
             f'{os.path.basename(bake_path)} has format {bake.get("format_version")!r}')

    own = own_values(bake)
    block = bake.get('tuning') or {}
    current = dict(own)
    current.update(block.get('cymbal', {}))
    ids = {articulation_id(s): i for i, s in enumerate(bake['slots'])}
    curves = {aid: dict(block.get('articulations', {}).get(aid, {})) for aid in ids}
    bands = {aid: slot_bands(bake['slots'][i]) for aid, i in ids.items()}
    shifts = {aid: slot_shift(bake['slots'][i]) for aid, i in ids.items()}

    # The file's changes on top of the bake's current values.
    for key, v in (tun.get('cymbal') or {}).items():
        if key == RESTART_KEY:
            if not isinstance(v, bool):
                fail(f'{RESTART_KEY} must be true or false')
            current[key] = v
        elif key in CYMBAL_KEYS:
            current[key] = finite(key, v)
        elif key in CYMBAL_BAND_KEYS:
            (lo, hi), unit, _ = CYMBAL_BAND_KEYS[key]
            current[key] = band_values(key, v, lo, hi, unit)
        else:
            fail(f'unknown cymbal value {key!r}')
    for key in POSITIVE:
        if not current[key] > 0.0:
            fail(f'{key} must be above 0')
    levels = {}
    for aid, vals in (tun.get('articulations') or {}).items():
        if aid not in ids:
            fail(f'the bake has no articulation {aid!r} (it has {sorted(ids)})')
        for key, v in vals.items():
            if key == 'entry':
                continue
            if key == LEVEL_KEY:
                levels[aid] = finite(f'{aid}.{key}', v)
            elif key in (VEL06_KEY, VEL03_KEY):
                curves[aid][key] = finite(f'{aid}.{key}', v)
            elif key == BANDS_KEY:
                bands[aid] = band_values(f'{aid}.{key}', v, BAND_MIN_DB, BAND_MAX_DB, 'dB')
            elif key == DELAY_KEY:
                shifts[aid] = (band_values(f'{aid}.{key}', v, DELAY_MIN_MS, DELAY_MAX_MS, 'ms'), shifts[aid][1])
            elif key == SPEED_KEY:
                shifts[aid] = (shifts[aid][0], band_values(f'{aid}.{key}', v, SPEED_MIN, SPEED_MAX, 'x'))
            else:
                fail(f'unknown articulation value {aid}.{key}')

    # The new tuning block: what differs from the bake's own values.
    new_cymbal = {k: current[k] for k in CYMBAL_KEYS + [RESTART_KEY] + list(CYMBAL_BAND_KEYS)
                  if k in current and current[k] != own.get(k)}
    new_arts = {}
    for aid, vals in curves.items():
        kept = {k: v for k, v in vals.items() if v != 0.0}
        if kept:
            new_arts[aid] = kept
    now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    if new_cymbal or new_arts:
        bake['tuning'] = {
            'format': FORMAT,
            'version': VERSION,
            'applied_by': 'tools/apply_tuning.py',
            'applied_utc': now,
            'source': os.path.basename(args.tuning),
            'source_date': tun.get('date'),
            'cymbal': new_cymbal,
            'articulations': new_arts,
        }
    else:
        bake.pop('tuning', None)

    for aid, db in levels.items():
        slot = bake['slots'][ids[aid]]
        lt = slot.get('level_trim') or {}
        lt['gain_db'] = round(db, 2)
        lt['set_by'] = f'tools/apply_tuning.py from {os.path.basename(args.tuning)} ({now}); the measurement ' \
                       'fields describe the trim before that'
        slot['level_trim'] = lt

    # Band gains: stored with the slot's excitation data; the loader applies them to every
    # coupling path of that articulation's strikes.
    for aid, gains in bands.items():
        slot = bake['slots'][ids[aid]]
        if any(g != 0.0 for g in gains):
            slot[SLOT_BANDS_KEY] = {
                'band_edges_hz': BAND_EDGES_HZ,
                'gains_db': [round(g, 3) for g in gains],
                'set_by': f'tools/apply_tuning.py from {os.path.basename(args.tuning)} ({now})',
            }
        else:
            slot.pop(SLOT_BANDS_KEY, None)

    # Onset shifts: stored with the slot's onset data; the engine applies them to that
    # articulation's band envelopes and onset peak times at each strike.
    for aid, (delays, speeds) in shifts.items():
        slot = bake['slots'][ids[aid]]
        if any(d != 0.0 for d in delays) or any(v != 1.0 for v in speeds):
            slot[SLOT_SHIFT_KEY] = {
                'band_edges_hz': BAND_EDGES_HZ,
                'delay_ms': [round(d, 3) for d in delays],
                'speed': [round(v, 4) for v in speeds],
                'set_by': f'tools/apply_tuning.py from {os.path.basename(args.tuning)} ({now})',
            }
        else:
            slot.pop(SLOT_SHIFT_KEY, None)

    print(f'{os.path.basename(bake_path)}:')
    for aid, i in ids.items():
        lt = bake['slots'][i].get('level_trim') or {}
        print(f'  {aid:14s} level {lt.get("gain_db", 0.0):+.2f} dB   curve {new_arts.get(aid, {})}')
        if SLOT_BANDS_KEY in bake['slots'][i]:
            gains = bake['slots'][i][SLOT_BANDS_KEY]['gains_db']
            print(f'  {"":14s} band gains {" ".join(f"{g:+g}" for g in gains)} dB')
        if SLOT_SHIFT_KEY in bake['slots'][i]:
            sh = bake['slots'][i][SLOT_SHIFT_KEY]
            print(f'  {"":14s} onset delay {" ".join(f"{d:+g}" for d in sh["delay_ms"])} ms')
            print(f'  {"":14s} onset speed {" ".join(f"{v:g}" for v in sh["speed"])}')
    print('  tuning block: ' + (json.dumps(new_cymbal) if new_cymbal else 'no cymbal values'))
    if args.dry_run:
        print('  (dry run: nothing written)')
        return
    with open(bake_path, 'w') as f:
        f.write(dump_json(bake) + '\n')


if __name__ == '__main__':
    main()
