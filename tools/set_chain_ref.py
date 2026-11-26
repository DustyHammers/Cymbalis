#!/usr/bin/env python3
"""Give a baked preset its sizzle chain, or store its measured reference level.

The sizzle chain (docs/PORT_MAP.md, "Sizzle chain"; Kris, 2026-11-26) is on STINE FLAT RIDE,
HAMMERED THIN RIDE, SWISH, RAVEN and EXTRA PAPER THIN 24" only: a bake has one when it has a
"chain" block. Two steps, as #76's skip thresholds:

    python tools/set_chain_ref.py resources/presets/stine_flat_ride.bake.json --init
    cymbalis_trims --bake resources/presets/stine_flat_ride.bake.json --chain-ref --json chain.json
    python tools/set_chain_ref.py resources/presets/stine_flat_ride.bake.json chain.json

--init adds the block: "seed", the chain's random generator's seed for this preset (FNV-1a 64 of
the preset id, the bake's file name, to 53 bits), and "ref_ms" null (no sizzle until measured; the
chain's damping already applies, which the measurement needs). The second step stores ref_ms, the
mean square of the output's 40-1000 Hz band 0.3-1.0 s after a lone BOW strike at velocity 0.9,
before the output gain, measured by tools/cymbalis_trims --chain-ref with the plugin's engine; the
other fields record the measurement. The bake is rewritten in place; nothing else in it changes.
Re-baking drops the block, as it drops the trims.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_preset import dump_json  # noqa: E402

CHAIN_PRESETS = ('stine_flat_ride', 'hammered_thin_ride', 'swish', 'raven', 'paperthin')


def fnv1a64(text):
    h = 0xcbf29ce484222325
    for byte in text.encode('utf-8'):
        h = ((h ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return h


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bake', help='bake JSON to update in place')
    ap.add_argument('chain', nargs='?', help='JSON written by cymbalis_trims --chain-ref --json')
    ap.add_argument('--init', action='store_true', help='add the chain block (seed, ref_ms null)')
    args = ap.parse_args()
    if args.init == (args.chain is not None):
        raise SystemExit('give either --init or the measurement JSON')

    preset = os.path.basename(args.bake)
    if not preset.endswith('.bake.json'):
        raise SystemExit('the bake file must be named <preset>.bake.json')
    preset = preset[:-len('.bake.json')]
    if preset not in CHAIN_PRESETS:
        raise SystemExit(f'{preset} has no sizzle chain (only {", ".join(CHAIN_PRESETS)})')
    with open(args.bake) as f:
        bake = json.load(f)

    if args.init:
        bake['chain'] = {'seed': fnv1a64(preset) & ((1 << 53) - 1), 'ref_ms': None,
                         'set_by': 'tools/set_chain_ref.py --init'}
    else:
        if 'chain' not in bake:
            raise SystemExit('the bake has no chain block: run --init first')
        with open(args.chain) as f:
            m = json.load(f)
        names = [s['name'] for s in bake['slots']]
        if m['slot'] not in names:
            raise SystemExit(f'measured on slot {m["slot"]}, which the bake does not have')
        ref = float(f'{float(m["ref_ms"]):.6g}')
        if not ref > 0.0:
            raise SystemExit(f'ref_ms {ref} is not above 0')
        bake['chain'] = {
            'seed': bake['chain']['seed'],
            'ref_ms': ref,
            'measured_by': m['measured_by'],
            'sample_rate': m['sample_rate'],
            'slot': m['slot'],
            'position': float(m['position']),
            'velocity': float(m['velocity']),
            'band_hz': m['band_hz'],
            'window_s': m['window_s'],
            'output_gain_db': float(m['output_gain_db']),
        }

    with open(args.bake, 'w') as f:
        f.write(dump_json(bake) + '\n')
    c = bake['chain']
    print(f'  {preset}: chain seed {c["seed"]}, ref_ms {c["ref_ms"]}')


if __name__ == '__main__':
    main()
