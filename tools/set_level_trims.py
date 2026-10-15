#!/usr/bin/env python3
"""Store level trims in a baked preset.

The trims come from tools/cymbalis_trims, which measures them with FastEngine, the
plugin's engine:

    cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
    python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json

(docs/BUILDS.md has the commands for every preset.) Each slot of the bake gets a "level_trim" object. The plugin uses only its "gain_db"
(rounded to 0.01 dB here); edit that number to adjust a slot's level by ear. The other
fields record the measurement. The bake is rewritten in place; nothing else in it changes.
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
        s['level_trim'] = {
            'gain_db': round(float(t['gain_db']), 2),
            'measured_gain_db': float(t['gain_db']),
            'measured_by': trims['measured_by'],
            'sample_rate': trims['sample_rate'],
            'velocity': float(trims['velocity']),
            'strike_time_s': float(trims['strike_time_s']),
            'window_s': float(trims['window_s']),
            'position': float(t['position']),
            'target_db': float(t.get('target_db', 0.0)),   # level relative to the reference
            'rms': float(t['rms']),                        # over window_s, both channels
            'reference_slot': trims['reference_slot'],
            'reference_rms': float(trims['reference_rms']),
        }

    with open(args.bake, 'w') as f:
        f.write(dump_json(bake) + '\n')
    for s in bake['slots']:
        print(f'  {s["name"]:6s} overlay={str(s["overlay"]):14s} level trim {s["level_trim"]["gain_db"]:+.2f} dB')


if __name__ == '__main__':
    main()
