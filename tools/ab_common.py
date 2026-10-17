"""
ab_common.py — shared definitions for the Python side of the A/B pipeline.

Slot table, sequence loading and validation, the Python engine loader and
small helpers used by bake_preset.py, render_reference.py, compare_ab.py and
run_ab.py. The slot table follows docs/PORT_MAP.md, "Live-engine decisions".
"""
import datetime
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTOTYPE = os.path.join(REPO, 'prototype')
PRESET = os.path.join(REPO, 'presets', 'nova', 'Nova_Baseline19.json')
ARTICULATION_DIR = os.path.join(REPO, 'presets', 'nova', 'articulations')

SEQUENCE_FORMAT_VERSION = 1

# name, overlay (None = no overlay), strike position
SLOTS = [
    ('BELL',  'bell_shoulder', 1.0),
    ('BOW',   'bow_tip',       0.5),
    ('EDGE',  None,            0.0),
    ('BELL2', 'bell_tip',      1.0),
    ('BOW2',  'bow_shoulder',  0.5),
]
SLOT_BY_NAME = {name: (name, overlay, pos) for name, overlay, pos in SLOTS}

# Presets the plugin bakes: base preset JSON, articulation folder and bake entries
# (name, overlay, base position). Nova is the A/B pipeline's preset and the default.
PRESETS = {
    'nova': {
        'preset': PRESET,
        'articulations': ARTICULATION_DIR,
        'slots': SLOTS,
    },
    '602': {
        'preset': os.path.join(REPO, 'presets', '602', '602_Baseline3.json'),
        'articulations': os.path.join(REPO, 'presets', '602', 'articulations'),
        'slots': [('BOW', 'bow', 0.5), ('EDGE', 'edge', 0.0)],
    },
    'bettis': {
        'preset': os.path.join(REPO, 'presets', 'bettis', 'Bettis_Baseline1.json'),
        'articulations': None,
        'slots': [('BELL', None, 1.0), ('BOW', None, 0.5), ('EDGE', None, 0.0)],
    },
    'swish': {
        'preset': os.path.join(REPO, 'presets', 'swish', 'Swish_Baseline1.json'),
        'articulations': os.path.join(REPO, 'presets', 'swish', 'articulations'),
        'slots': [('BELL', None, 1.0), ('BOW', 'bow_tip', 0.5), ('BOW_SHOULDER', 'bow_shoulder', 0.5),
                  ('EDGE', None, 0.0)],
    },
}


def overlay_path(overlay, preset='nova'):
    return None if overlay is None else os.path.join(PRESETS[preset]['articulations'], overlay + '.json')


def load_python_engine(overlay, overrides, preset='nova'):
    """Load the V19 engine through prototype/load_preset.py (the only supported way)."""
    if PROTOTYPE not in sys.path:
        sys.path.insert(0, PROTOTYPE)
    from load_preset import load_engine
    return load_engine(PRESETS[preset]['preset'], articulation=overlay_path(overlay, preset),
                       overrides=dict(overrides) if overrides else None)


def utc_stamp():
    """Timestamp for unique render filenames (CLAUDE.md: every render gets one)."""
    return datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')


def fnv1a64_bytes(data):
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def load_sequence(path):
    with open(path) as f:
        seq = json.load(f)
    if seq.get('format_version') != SEQUENCE_FORMAT_VERSION:
        raise SystemExit(f'{path}: unsupported sequence format_version {seq.get("format_version")!r}')
    for key in ('name', 'slot', 'duration_s', 'strikes', 'overrides'):
        if key not in seq:
            raise SystemExit(f'{path}: missing key {key!r}')
    if seq['slot'] not in SLOT_BY_NAME:
        raise SystemExit(f'{path}: unknown slot {seq["slot"]!r}')
    check_python_timing(seq, path)
    return seq


def check_python_timing(seq, path, sr=44100):
    """Reject strike times where Python drops a strike or leaves a one-sample hole
    (PORT_MAP f.6, f.7), or where a strike is not on a whole sample."""
    times = [float(t) for t, _ in seq['strikes']]
    if times != sorted(times):
        raise SystemExit(f'{path}: strikes must be in time order')
    dur = float(seq['duration_s'])
    for t in times:
        if not (0.0 <= t < dur):
            raise SystemExit(f'{path}: strike time {t} outside [0, duration)')
        if abs(t * sr - round(t * sr)) > 1e-9 or int(t * sr) != round(t * sr):
            raise SystemExit(f'{path}: strike time {t} is not a whole sample in Python (int(t*SR) != round(t*SR))')
    # A strike at exactly t = 0 is fine: Python skips the empty gap [0, 0) and no
    # contact is pending, so nothing is dropped.
    bounds = ([0.0] if times and times[0] > 0.0 else []) + times + [dur]
    for a, b in zip(bounds[:-1], bounds[1:]):
        if b - a <= 1e-4:
            raise SystemExit(f'{path}: gap {a}..{b} is <= 1e-4 s; Python would skip it (f.7)')
        if int(a * sr) + int((b - a) * sr) != int(b * sr):
            raise SystemExit(f'{path}: boundary {a}..{b} leaves a Python hole/overlap (f.6)')
