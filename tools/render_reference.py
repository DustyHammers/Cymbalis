#!/usr/bin/env python3
"""
render_reference.py — render a test sequence with the Python V19 engine.

The engine is loaded through prototype/load_preset.py with the slot's overlay
and the sequence's overrides, and render() runs once on a fresh engine. Each
fresh engine counts as one plugin load (docs/PORT_MAP.md, Live-engine
decisions). The output is written as a 32-bit float WAV, with no PCM
rounding. An optional 64-bit float WAV keeps the full float64 output for A/B
checks below float32 resolution.

Usage:
    python tools/render_reference.py tests/sequences/edge_3hits_15s.json
    python tools/render_reference.py SEQ.json --out a.f32.wav --out64 a.f64.wav
Without --out, the file goes to renders/<name>_py_<UTC timestamp>.f32.wav.
"""
import argparse
import json
import os
import sys
import time

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ab_common as ab  # noqa: E402


def render_sequence(seq):
    _, overlay, position = ab.SLOT_BY_NAME[seq['slot']]
    eng, SE = ab.load_python_engine(overlay, seq['overrides'])
    strikes = [SE(float(t), float(v), position) for t, v in seq['strikes']]
    audio = eng.render(strikes, float(seq['duration_s']))
    sr = int(type(eng).render.__globals__['SR'])
    return audio, sr, eng


def write_wavs(audio, sr, out32=None, out64=None):
    if out32:
        os.makedirs(os.path.dirname(os.path.abspath(out32)), exist_ok=True)
        sf.write(out32, audio.astype(np.float32), sr, subtype='FLOAT')
    if out64:
        os.makedirs(os.path.dirname(os.path.abspath(out64)), exist_ok=True)
        sf.write(out64, audio.astype(np.float64), sr, subtype='DOUBLE')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('sequence')
    ap.add_argument('--out', help='32-bit float WAV path')
    ap.add_argument('--out64', help='optional 64-bit float WAV path')
    ap.add_argument('--info', help='optional JSON file for render metadata')
    args = ap.parse_args()

    seq = ab.load_sequence(args.sequence)
    out32 = args.out or os.path.join(ab.REPO, 'renders', f'{seq["name"]}_py_{ab.utc_stamp()}.f32.wav')

    t0 = time.time()
    audio, sr, eng = render_sequence(seq)
    elapsed = time.time() - t0
    write_wavs(audio, sr, out32, args.out64)

    info = {
        'sequence': seq['name'], 'slot': seq['slot'], 'engine': 'python-v19',
        'samples': int(audio.shape[0]), 'sample_rate': sr, 'calib_scalar': float(eng._calib_scalar),
        'peak': float(np.max(np.abs(audio))), 'seconds': round(elapsed, 2),
        'out': out32, 'out64': args.out64,
    }
    if args.info:
        with open(args.info, 'w') as f:
            json.dump(info, f, indent=2)
    print(json.dumps(info))


if __name__ == '__main__':
    main()
