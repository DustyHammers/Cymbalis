#!/usr/bin/env python3
"""Store level trims in a baked preset.

The trims come from tools/cymbalis_trims, which measures them with FastEngine, the
plugin's engine:

    cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
    python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json

docs/BUILDS.md has the commands for every preset. Each slot of the bake gets a
"level_trim" object. The plugin uses only its "gain_db" (rounded to 0.01 dB here); edit
that number to adjust a slot's level by ear. The other fields record the measurement.
The bake is rewritten in place; nothing else in it changes.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_preset import dump_json  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bake', help='bake JSON to update in place')
    ap.add_argument('trims', help='JSON written by cymbalis_trims --json')
    args = ap.parse_args()

    with open(args.bake) as f:
        bake = json.load(f)
    with open(args.trims) as f:
        trims = json.load(f)

    names = [s['name'] for s in bake['slots']]
    if sorted(names) != sorted(trims['slots']):
        raise SystemExit(f'the trims are for slots {sorted(trims["slots"])}, the bake has {sorted(names)}')
    for s in bake['slots']:
        t = trims['slots'][s['name']]
        if float(t['position']) != float(s['position']):
            raise SystemExit(f'slot {s["name"]}: trim measured at position {t["position"]}, bake has {s["position"]}')
        lt = {'gain_db': round(float(t['gain_db']), 2)}
        # Next to the trim: the slot's SPEAR target and, where the peak limit capped it, the
        # level it reaches instead and why (cymbalis_trims --spear-target, --peak-limit).
        for key in ('spear_target_db', 'capped', 'capped_level_db', 'cap_reason', 'uncapped_gain_db'):
            if key in t:
                lt[key] = t[key]
        lt.update({
            'measured_gain_db': float(t['gain_db']),
            'level_db': float(t['level_db']),              # relative to the reference, with the trim
            'measured_by': trims['measured_by'],
            'sample_rate': trims['sample_rate'],
            'velocity': float(trims['velocity']),
            'strike_time_s': float(trims['strike_time_s']),
            'window_s': float(trims['window_s']),
            'position': float(t['position']),
            'rms': float(t['rms']),                        # over window_s, both channels, no trim
            'reference_slot': trims['reference_slot'],
            'reference_rms': float(trims['reference_rms']),
        })
        if 'peak' in t:
            lt.update({
                'peak': float(t['peak']),                  # one hit with the trim, see peak_percentile
                'first_hit_peak': float(t['first_hit_peak']),
                'peak_limit': float(trims['peak_limit']),
                'peak_hits': int(trims['peak_hits']),
                'peak_percentile': float(trims['peak_percentile']),
                'peak_window_s': float(trims['peak_window_s']),
            })
        s['level_trim'] = lt

    with open(args.bake, 'w') as f:
        f.write(dump_json(bake) + '\n')
    for s in bake['slots']:
        print(f'  {s["name"]:6s} overlay={str(s["overlay"]):14s} level trim {s["level_trim"]["gain_db"]:+.2f} dB')


if __name__ == '__main__':
    main()
