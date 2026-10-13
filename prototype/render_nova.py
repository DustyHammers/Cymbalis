"""
render_nova.py — Standard Nova 18" Heavy Dry Inverted China render.

Bell s/m/f  → t=1.5 / 5.5 / 9.5s   (position=1.0)
Bow  s/m/f  → t=15  / 19  / 23s    (position=0.5)
Edge s/m/f  → t=29  / 33  / 37s    (position=0.0)

Bow articulation:
  Default (shoulder): no articulation flag needed
  Tip:                --tip flag  OR  pass articulation='bow_tip'

Usage:
    python render_nova.py [output_dir] [--tip]
"""
import os, sys, time
import numpy as np
import soundfile as sf
from load_preset import load_engine

OUTPUT_DIR  = next((a for a in sys.argv[1:] if not a.startswith('--')), 'renders')
BOW_TIP     = '--tip' in sys.argv
os.makedirs(OUTPUT_DIR, exist_ok=True)

PRESET      = os.path.join('presets', 'nova', 'Nova_Baseline19.json')
ART_DIR     = os.path.join('presets', 'nova', 'articulations')
ARTICULATION = os.path.join(ART_DIR, 'bow_tip.json') if BOW_TIP else None

STRIKES = [
    (1.5,  0.30, 1.0), (5.5,  0.65, 1.0), (9.5,  0.90, 1.0),   # bell
    (15.0, 0.30, 0.5), (19.0, 0.65, 0.5), (23.0, 0.90, 0.5),   # bow
    (29.0, 0.30, 0.0), (33.0, 0.65, 0.0), (37.0, 0.90, 0.0),   # edge
]

art_label = 'tip' if BOW_TIP else 'shoulder'
print(f'Loading Nova (bow={art_label})...', end=' ', flush=True)
t0 = time.time()
eng, SE = load_engine(PRESET, articulation=ARTICULATION)
strikes = [SE(t, v, pos) for t, v, pos in STRIKES]
audio = eng.render(strikes, 47.0)
elapsed = time.time() - t0

out_path = os.path.join(OUTPUT_DIR, f'Nova_B19_full_{art_label}.wav')
sf.write(out_path, audio, 44100, subtype='PCM_24')
print(f'{elapsed:.1f}s  peak={np.max(np.abs(audio)):.4f}')
print(f'Written: {out_path}')
