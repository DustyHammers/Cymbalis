#!/usr/bin/env python3
"""Compute NOVA SHOULDER PROFILE: Nova Ride's shoulder-minus-tip difference per band.

    python tools/shoulder_profile.py            # write plugin/tuning/ShoulderProfiles.h
    python tools/shoulder_profile.py --check    # fail if the header is not what this computes

The Tuning window's NOVA SHOULDER PROFILE button (docs/TUNING.md) fills a shoulder
articulation of SMOKE FLAT RIDE or HAMMERED THIN RIDE with these band gains, attack gains and
attack time: the bow profile for bow and center, the bell profile for bell. They are computed
once, here, from the Nova bake (resources/presets/nova_ride.bake.json), as the engine plays it.

Band gains:

  1. Each mode's coupling at velocity 0.9 for the tip and the shoulder articulation at its
     base position (bow: bow_tip and bow_shoulder at 0.5; bell: bell_tip and bell_shoulder
     at 1.0): exc_norm x velocity participation ^ vel_gain_power x position weight, the
     velocity tables interpolated as FastEngine::velocityParticipation does (the bell rows
     use the bell tables).
  2. Per one-third-octave band of the band gains (22 bands, plugin/dsp/Tuning.h), the
     energy: the sum of the squared couplings of its modes.
  3. The shoulder's band energies scaled so that both have the same total energy; per band
     the ratio shoulder / tip, in dB.
  4. Bands with fewer than 5 Nova modes (or no energy) are set to 0 dB.
  5. Smoothed over neighbouring bands: weights 1/4, 1/2, 1/4 over the band and its two
     neighbours, renormalised over those that are not set to 0 dB by step 4.
  6. Clamped to +/-12 dB and rounded to 0.01 dB.

Attack gains and time: the shoulder's onset gains relative to the tip's. Each articulation's
onset gains (bow_onset_band_gains, 9 octave bands; none = 0 dB) are taken per band of the
22 as the octave band that contains the band's centre, in dB, as the plugin's loader does;
the profile is shoulder minus tip, clamped to +/-12 dB and rounded to 0.01 dB. The attack
time is the shoulder's onset time (bow: bow_shoulder, neutral, 80 ms, against bow_tip's
80 ms; bell: bell_shoulder 300 ms against bell_tip's 120 ms).
"""
import argparse
import json
import math
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAKE = os.path.join(REPO, 'resources', 'presets', 'nova_ride.bake.json')
HEADER = os.path.join(REPO, 'plugin', 'tuning', 'ShoulderProfiles.h')
N_BANDS = 22
VELOCITY = 0.9
MIN_MODES = 5
CLAMP_DB = 12.0
# (profile, tip articulation, shoulder articulation, base position)
PROFILES = [('bow', 'bow_tip', 'bow_shoulder', 0.5), ('bell', 'bell_tip', 'bell_shoulder', 1.0)]


def band_of(freq):
    """The band of a frequency (plugin/dsp/Tuning.cpp excitationBand)."""
    j = 0
    while j + 1 < N_BANDS and freq >= 11404.0 * 2.0 ** ((j + 1 - 21) / 3.0):
        j += 1
    return j


def participation(table, v):
    """FastEngine::velocityParticipation: the table's curves at velocity v, per mode."""
    norms = table['vel_norms']['data']
    rows, nv = table['curves']['shape']
    curves = table['curves']['data']
    if v <= norms[0]:
        s = v / (norms[0] + 1e-9)
        return [curves[i * nv] * s for i in range(rows)]
    if v >= norms[-1]:
        return [curves[i * nv + nv - 1] for i in range(rows)]
    idx = min(max(next(k for k, x in enumerate(norms) if x >= v) - 1, 0), nv - 2)
    t = (v - norms[idx]) / (norms[idx + 1] - norms[idx] + 1e-12)
    return [curves[i * nv + idx] * (1.0 - t) + curves[i * nv + idx + 1] * t for i in range(rows)]


def couplings(bake, slot, pos):
    """|coupling| per mode of a velocity-0.9 strike of `slot` at `pos` (without calibration)."""
    sh = bake['shared']
    exc, edge_w, bell_w = sh['exc_norm']['data'], sh['edge_weight']['data'], sh['bell_weight']['data']
    vel = participation(slot['bow_vp'], VELOCITY)
    if pos > 0.5 and slot['bell_vp'] is not None:
        bf = min(max((pos - 0.5) / 0.5, 0.0), 1.0)
        bell = participation(slot['bell_vp'], VELOCITY)
        vel = [(1.0 - bf) * a + bf * b for a, b in zip(vel, bell)]
    power = float(slot['effective']['vel_gain_power'])
    out = []
    for i, e in enumerate(exc):
        if pos <= 0.5:
            w = edge_w[i] + (pos / 0.5) * (1.0 - edge_w[i])
        else:
            bf = min(max((pos - 0.5) / 0.5, 0.0), 1.0)
            w = (1.0 - bf) + bf * bell_w[i]
        out.append(abs(e * math.copysign(abs(vel[i]) ** power, vel[i]) * w))
    return out


def profile(bake, tip_id, shoulder_id, pos):
    slots = {s['overlay']: s for s in bake['slots'] if s['overlay']}
    freq = bake['shared']['freq_hz']['data']
    bands = [band_of(f) for f in freq]
    count = [bands.count(j) for j in range(N_BANDS)]
    e = {}
    for name in (tip_id, shoulder_id):
        c = couplings(bake, slots[name], pos)
        e[name] = [sum(c[i] ** 2 for i in range(len(c)) if bands[i] == j) for j in range(N_BANDS)]
    scale = sum(e[tip_id]) / sum(e[shoulder_id])
    raw = []
    for j in range(N_BANDS):
        ok = count[j] >= MIN_MODES and e[tip_id][j] > 0.0 and e[shoulder_id][j] > 0.0
        raw.append(10.0 * math.log10(e[shoulder_id][j] * scale / e[tip_id][j]) if ok else None)
    smooth = []
    for j in range(N_BANDS):
        if raw[j] is None:
            smooth.append(0.0)
            continue
        num = den = 0.0
        for k, w in ((j - 1, 0.25), (j, 0.5), (j + 1, 0.25)):
            if 0 <= k < N_BANDS and raw[k] is not None:
                num += w * raw[k]
                den += w
        smooth.append(num / den)
    return [round(min(max(x, -CLAMP_DB), CLAMP_DB), 2) for x in smooth], count, raw


def attack_db(bake, slot):
    """An articulation's attack gains (dB per band) from its onset gains (plugin/dsp/Tuning.cpp
    attackFromOnsetGains): each band takes the onset band that contains its centre."""
    if slot['onset_gains'] is None:
        return [0.0] * N_BANDS
    gains = slot['onset_gains']['data']
    edges = bake['shared']['bow_onset_band_edges_hz']['data']
    out = []
    for j in range(N_BANDS):
        centre = 11404.0 * 2.0 ** ((j - 21 + 0.5) / 3.0)
        k = 0
        while k + 1 < len(gains) and centre >= edges[k + 1]:
            k += 1
        out.append(20.0 * math.log10(gains[k]))
    return out


def attack_profile(bake, tip_id, shoulder_id):
    slots = {s['overlay']: s for s in bake['slots'] if s['overlay']}
    tip, sh = attack_db(bake, slots[tip_id]), attack_db(bake, slots[shoulder_id])
    gains = [round(min(max(b - a, -CLAMP_DB), CLAMP_DB), 2) for a, b in zip(tip, sh)]
    ms = round(slots[shoulder_id]['onset_tau_s'] * 1000.0, 6)
    return gains, ms, round(slots[tip_id]['onset_tau_s'] * 1000.0, 6)


def header(profiles):
    lines = [
        '#pragma once',
        '',
        '// Generated by tools/shoulder_profile.py from resources/presets/nova_ride.bake.json; do not',
        '// edit. NOVA SHOULDER PROFILE (docs/TUNING.md): Nova Ride\'s shoulder-minus-tip difference',
        '// per band of the band gains, in dB, for the shoulder articulations of the other presets.',
        '// How it is computed: tools/shoulder_profile.py.',
        '',
        '#include "Tuning.h"',
        '',
        'namespace cymbalis::tuning',
        '{',
        'struct NovaShoulderProfile',
        '{',
        '    const char* name;                                // "bow" or "bell"',
        '    std::array<double, kExcitationBands> bandDb;     // band gains, lowest band first',
        '    std::array<double, kExcitationBands> attackDb;   // attack gains, lowest band first',
        '    double attackMs;                                 // attack time',
        '};',
        '',
    ]
    def row(values):
        return ['        ' + ', '.join(f'{v:.2f}' for v in values[:11]) + ',',
                '        ' + ', '.join(f'{v:.2f}' for v in values[11:]) + ',']
    for name, values, attack, ms in profiles:
        lines.append(f'// Nova {name}_shoulder relative to {name}_tip.')
        lines.append(f'inline constexpr NovaShoulderProfile kNovaShoulder{name.capitalize()} {{')
        lines.append(f'    "{name}",')
        lines += ['    {'] + row(values) + ['    },']
        lines += ['    {'] + row(attack) + ['    },']
        lines.append(f'    {ms:.1f},')
        lines.append('};')
    lines += ['', '} // namespace cymbalis::tuning', '']
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='fail if the header differs from what this computes')
    args = ap.parse_args()
    with open(BAKE) as f:
        bake = json.load(f)
    out = []
    for name, tip, shoulder, pos in PROFILES:
        values, count, raw = profile(bake, tip, shoulder, pos)
        attack, ms, tip_ms = attack_profile(bake, tip, shoulder)
        out.append((name, values, attack, ms))
        print(f'{name}: shoulder - tip per band (dB), velocity {VELOCITY}, position {pos}')
        for j in range(N_BANDS):
            lo, hi = 11404.0 * 2.0 ** ((j - 21) / 3.0), 11404.0 * 2.0 ** ((j - 20) / 3.0)
            r = '   n/a' if raw[j] is None else f'{raw[j]:+6.2f}'
            print(f'  band {j:2d} {lo:7.0f}-{hi:7.0f} Hz  modes {count[j]:4d}  raw {r}  profile {values[j]:+6.2f}'
                  f'  attack {attack[j]:+6.2f}')
        print(f'  attack time {ms:g} ms (the shoulder\'s; tip {tip_ms:g} ms)')
    text = header(out)
    if args.check:
        with open(HEADER) as f:
            if f.read() != text:
                sys.exit(f'{os.path.relpath(HEADER, REPO)} is not what tools/shoulder_profile.py computes')
        print('header up to date')
        return
    with open(HEADER, 'w') as f:
        f.write(text)
    print(f'wrote {os.path.relpath(HEADER, REPO)}')


if __name__ == '__main__':
    main()
