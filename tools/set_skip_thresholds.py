#!/usr/bin/env python3
"""Store each slot's skip threshold factor in a baked preset.

The factors come from tools/cymbalis_trims --skip-thresholds, which measures with FastEngine,
the plugin's engine, the peak of a velocity-1.0 hit on each slot against the slot's level:

    cymbalis_trims --bake resources/presets/nova_ride.bake.json --skip-thresholds --json skip.json
    python tools/set_skip_thresholds.py resources/presets/nova_ride.bake.json skip.json

docs/BUILDS.md has the commands for every preset. Each slot gets a "skip_threshold" object.
FastEngine uses only its "rel": anything it stops rendering (a mode, a component) is below
rel x calibration x trim of the last slot struck, which puts it 140 dB below a full hit on
that slot (docs/PORT_MAP.md, "One silence rule: 140 dB below a full hit"; Kris, 2026-11-25).
The other fields record the measurement. Engine, the Python-matching path, keeps
constants.amp_skip_threshold (1e-9). The bake is rewritten in place; nothing else in it
changes. A trim does not change the factor (it scales the peak and the level alike), so the
order with tools/set_level_trims.py does not matter.
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
    ap.add_argument('skip', help='JSON written by cymbalis_trims --skip-thresholds --json')
    args = ap.parse_args()

    with open(args.bake) as f:
        bake = json.load(f)
    with open(args.skip) as f:
        skip = json.load(f)

    names = [s['name'] for s in bake['slots']]
    if sorted(names) != sorted(skip['slots']):
        raise SystemExit(f'the factors are for slots {sorted(skip["slots"])}, the bake has {sorted(names)}')
    for s in bake['slots']:
        m = skip['slots'][s['name']]
        if float(m['position']) != float(s['position']):
            raise SystemExit(f'slot {s["name"]}: measured at position {m["position"]}, bake has {s["position"]}')
        rel = float(f'{float(m["skip_threshold_rel"]):.4g}')
        if not (0.0 < rel < 1e-3):
            raise SystemExit(f'slot {s["name"]}: skip_threshold_rel {rel} is out of range')
        s['skip_threshold'] = {
            'rel': rel,
            'below_full_hit_db': float(skip['below_full_hit_db']),
            'full_hit_peak_db_re_level': round(float(m['full_hit_peak_db_re_level']), 2),
            'measured_by': skip['measured_by'],
            'sample_rate': skip['sample_rate'],
            'velocity': float(skip['velocity']),
            'peak_hits': int(skip['peak_hits']),
            'peak_statistic': skip['peak_statistic'],
            'peak_window_s': float(skip['peak_window_s']),
        }

    with open(args.bake, 'w') as f:
        f.write(dump_json(bake) + '\n')
    for s in bake['slots']:
        t = s['skip_threshold']
        print(f'  {s["name"]:16s} full hit {t["full_hit_peak_db_re_level"]:+7.2f} dB re level, '
              f'skip threshold rel {t["rel"]:.4g}')


if __name__ == '__main__':
    main()
