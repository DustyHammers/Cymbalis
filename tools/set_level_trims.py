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

--boost SLOT=DB raises SLOT's trim by DB on top of the measured one (capped or not), for a
slot whose velocity curve takes the boost back off below velocity 0.6: its tuning block
must give it vel_0.6_db and vel_0.3_db of -DB (tools/apply_tuning.py, applied first), so
its strikes at velocity 0.6 and below play exactly as with the measured trim and the extra
level comes in between 0.6 and 1.0. --boost-reason records why. EXTRA PAPER THIN 24"'s
EDGE (docs/PORT_MAP.md, "Extra Paper Thin Ride 24"").

--anchor SLOT keeps SLOT, whose SPEAR target the peak limit capped, at its capped trim and
lowers every other slot by D = its SPEAR target - its capped level against BOW, so that SLOT
sits at its target against BOW and the others keep their levels against each other (they
all go down by D, so their peaks only fall). --anchor-reason records why. CRASH 16"'s EDGE
(docs/PORT_MAP.md, "Crash 16""): with BOW at Nova's level the cap held EDGE about level
with BOW, where its recordings put it 18 dB above.
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
    ap.add_argument('--boost', action='append', default=[], metavar='SLOT=DB',
                    help='raise SLOT\'s trim by DB; its velocity curve must be -DB at 0.6 and 0.3')
    ap.add_argument('--boost-reason', default='', help='why the boost (recorded in level_trim)')
    ap.add_argument('--anchor', metavar='SLOT',
                    help='keep SLOT (capped) at its trim and lower every other slot by its target minus its capped level')
    ap.add_argument('--anchor-reason', default='', help='why the anchor (recorded in level_trim)')
    args = ap.parse_args()
    if args.anchor and not args.anchor_reason:
        raise SystemExit('--anchor needs --anchor-reason')
    if args.anchor and args.boost:
        raise SystemExit('--anchor and --boost do not go together')
    boosts = {}
    for b in args.boost:
        name, _, db = b.partition('=')
        try:
            boosts[name] = float(db)
        except ValueError:
            raise SystemExit(f'--boost {b}: expected SLOT=DB')
    if boosts and not args.boost_reason:
        raise SystemExit('--boost needs --boost-reason')

    with open(args.bake) as f:
        bake = json.load(f)
    with open(args.trims) as f:
        trims = json.load(f)

    names = [s['name'] for s in bake['slots']]
    for name, db in boosts.items():
        if name not in names:
            raise SystemExit(f'--boost {name}: the bake has no such slot')
        slot = bake['slots'][names.index(name)]
        art = slot.get('articulation') or (slot['overlay'] if slot.get('overlay') else slot['name'].lower())
        curve = ((bake.get('tuning') or {}).get('articulations') or {}).get(art, {})
        if curve.get('vel_0.6_db') != -db or curve.get('vel_0.3_db') != -db:
            raise SystemExit(f'--boost {name}={db:g}: the bake\'s tuning block must give {art} vel_0.6_db and '
                             f'vel_0.3_db of {-db:g} (it has {curve.get("vel_0.6_db")} and {curve.get("vel_0.3_db")}); '
                             'apply the tuning file first')
    if sorted(names) != sorted(trims['slots']):
        raise SystemExit(f'the trims are for slots {sorted(trims["slots"])}, the bake has {sorted(names)}')
    lower = 0.0
    if args.anchor:
        a = trims['slots'].get(args.anchor)
        if a is None:
            raise SystemExit(f'--anchor {args.anchor}: the trims have no such slot')
        if not a.get('capped'):
            raise SystemExit(f'--anchor {args.anchor}: its SPEAR target was not capped, so nothing to lower to')
        lower = float(a['spear_target_db']) - float(a['capped_level_db'])  # D, dB
    for s in bake['slots']:
        t = trims['slots'][s['name']]
        if float(t['position']) != float(s['position']):
            raise SystemExit(f'slot {s["name"]}: trim measured at position {t["position"]}, bake has {s["position"]}')
        lt = {'gain_db': round(float(t['gain_db']), 2)}
        # Next to the trim: the slot's SPEAR target and, where the peak limit capped it, the
        # level it reaches instead and why (cymbalis_trims --spear-target, --peak-limit).
        for key in ('spear_target_db', 'capped', 'capped_level_db', 'cap_reason', 'uncapped_gain_db', 'same_as'):
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
        if s['name'] in boosts:
            db = boosts[s['name']]
            lt['gain_db'] = round(float(t['gain_db']) + db, 2)
            lt['boost_db'] = db
            lt['boosted_from_gain_db'] = round(float(t['gain_db']), 2)
            lt['boost_reason'] = args.boost_reason
            lt['level_db_with_boost'] = float(t['level_db']) + db  # at velocity 1.0
            if 'peak' in t:
                # The peak at velocity 1.0 with the boost (the curve is 0 dB there).
                lt['peak_with_boost'] = float(t['peak']) * 10.0 ** (db / 20.0)
        if args.anchor and s['name'] == args.anchor:
            # Its capped trim stays; the others come down by D (below).
            lt['anchor'] = True
            lt['others_lowered_by_db'] = lower
            lt['level_vs_bow_db'] = float(t['level_db']) + lower  # = its SPEAR target
            lt['anchor_reason'] = args.anchor_reason
        elif args.anchor:
            lt['gain_db'] = round(float(t['gain_db']) - lower, 2)
            lt['lowered_by_db'] = lower
            lt['lowered_from_gain_db'] = round(float(t['gain_db']), 2)
            lt['lowered_for'] = args.anchor
            lt['anchor_reason'] = args.anchor_reason
            lt['level_db_lowered'] = float(t['level_db']) - lower  # against the reference
            if 'peak' in t:
                lt['peak_lowered'] = float(t['peak']) * 10.0 ** (-lower / 20.0)
        s['level_trim'] = lt

    with open(args.bake, 'w') as f:
        f.write(dump_json(bake) + '\n')
    for s in bake['slots']:
        print(f'  {s["name"]:6s} overlay={str(s["overlay"]):14s} level trim {s["level_trim"]["gain_db"]:+.2f} dB')


if __name__ == '__main__':
    main()
