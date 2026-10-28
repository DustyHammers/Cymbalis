"""Approved reference for the wireframe cymbal (docs/UI_SPEC.md section 11). Documentation
only: Python (numpy, scipy, OpenCV), not built, not run in CI. Colours are OpenCV BGR;
coordinates are 1536x1024 design units. Keys: nova = NOVA RIDE, hammered = HAMMERED THIN RIDE,
smoke = SMOKE FLAT RIDE, swish = SWISH, raven = RAVEN.

History:
- 2026-10-27: shapes, lathe zones and hammering approved by Kris (PR #44).
- 2026-10-28, approved by Kris:
  - NOVA RIDE: a bell like Raven's (edge 2.2"), a longer, shallower bow down to the flat ring;
    gold lathing measured from Nova.png.
  - HAMMERED THIN RIDE: the hammering pressed into the mesh as dents (class Cymbal2 below,
    DENTED); gold lathing measured from its photo.
  - SMOKE FLAT RIDE: the hammered body in a lighter gold ('tint'), the dark lathed edge band
    measured from its photo.
  SWISH and RAVEN are unchanged.
"""
import math
import numpy as np
import cv2
from scipy.special import jv, jn_zeros
from scipy.interpolate import PchipInterpolator

# ---------------------------------------------------------------- cymbal shapes
# profile: (radius in inches, height in inches) from the centre to the rim, smoothed between points.
# lathe: radial zones (fraction of R) drawn in the bright lathed colour, with fine lathe lines.
# hammer: the surface texture on the bow: 'scale' (fish-scale arcs), 'dimple' (large irregular
# dents), 'radial' (short radial strokes), None (lathed only).
# Each photo's mean brightness by radius, measured 2026-10-28 from resources/ui/<photo>.png:
# 200 values, 0.005 R steps from the centre hole (rim radius: Nova 354, Hammered 354.5, Smoke
# 356.5 px = 10"). They shade the gold lathing (Cymbal.lum).
NOVA_PHOTO_LUM = [4.7, 5.1, 6.1, 6.9, 7.7, 60.5, 121.6, 110.4, 112.0, 109.3, 110.9, 111.9, 108.7, 107.5, 117.0, 92.3, 98.7, 97.3, 95.5, 95.1, 93.8, 94.1, 93.4, 92.3, 93.8, 92.6, 93.7, 91.8, 92.4, 89.3, 91.6, 92.1, 93.9, 92.9, 96.1, 99.1, 101.5, 104.0, 106.7, 110.1, 112.7, 116.4, 123.9, 118.9, 102.3, 97.4, 96.7, 97.1, 98.7, 98.6, 98.0, 101.7, 100.2, 101.5, 103.2, 102.8, 103.0, 105.6, 105.7, 105.2, 108.0, 108.3, 108.5, 109.8, 110.7, 110.4, 110.0, 111.2, 114.5, 115.2, 128.3, 108.1, 77.0, 79.6, 80.5, 109.5, 120.5, 115.0, 119.5, 94.6, 64.8, 62.6, 60.5, 58.4, 57.0, 56.1, 55.0, 54.9, 54.8, 55.0, 55.0, 55.6, 55.7, 55.7, 55.6, 55.3, 54.8, 55.4, 54.7, 54.9, 55.7, 56.0, 56.2, 56.3, 56.6, 57.0, 57.5, 57.9, 58.8, 59.4, 59.7, 60.4, 60.5, 61.5, 62.2, 63.0, 63.4, 64.3, 65.3, 65.3, 65.5, 66.6, 67.9, 68.6, 69.7, 70.2, 71.0, 72.5, 73.0, 72.8, 73.8, 74.2, 75.9, 76.2, 78.5, 80.6, 80.6, 81.8, 82.7, 83.9, 83.2, 83.0, 82.4, 81.8, 81.5, 80.8, 81.0, 82.3, 83.2, 82.3, 82.3, 82.7, 83.6, 82.7, 83.7, 82.9, 84.1, 83.1, 83.9, 83.6, 83.9, 83.0, 82.4, 81.7, 81.1, 81.1, 80.5, 80.2, 78.8, 78.5, 77.6, 76.7, 76.6, 76.0, 75.6, 73.9, 73.1, 71.4, 73.0, 72.6, 72.1, 71.3, 72.6, 71.3, 71.3, 69.7, 69.4, 68.6, 68.6, 67.9, 67.7, 67.6, 73.4, 93.1, 93.8, 92.2, 91.5, 89.9, 87.3, 90.4]
HAMMERED_PHOTO_LUM = [0, 0, 0, 0, 0, 83.2, 81.7, 77.1, 79.7, 76.9, 76.9, 76.2, 79.3, 76.0, 81.8, 77.0, 66.3, 72.0, 71.3, 68.9, 70.3, 66.7, 67.7, 72.4, 70.2, 63.4, 65.0, 62.7, 64.5, 64.6, 65.1, 64.5, 65.8, 66.5, 69.8, 68.5, 73.5, 76.0, 76.8, 78.6, 79.7, 84.0, 80.3, 78.7, 77.7, 77.9, 78.5, 80.1, 80.4, 78.2, 78.6, 78.9, 77.2, 77.6, 76.1, 72.7, 73.6, 72.7, 73.7, 73.1, 71.9, 71.3, 76.6, 77.1, 76.5, 77.4, 75.8, 73.3, 71.6, 71.8, 72.4, 73.3, 75.2, 72.7, 73.7, 72.1, 71.6, 70.4, 71.9, 73.4, 71.5, 67.3, 62.9, 59.8, 56.2, 52.6, 54.8, 55.2, 54.5, 55.9, 55.1, 55.8, 54.9, 52.9, 53.6, 52.6, 53.1, 52.7, 51.1, 52.2, 50.7, 50.7, 51.9, 51.2, 53.6, 54.7, 54.7, 54.6, 54.2, 54.4, 55.3, 54.3, 55.1, 55.2, 55.3, 54.9, 54.5, 55.9, 56.3, 55.9, 55.2, 55.6, 55.3, 55.8, 56.0, 57.4, 57.0, 56.6, 57.5, 57.5, 57.4, 57.6, 57.6, 56.7, 56.6, 57.4, 59.3, 58.7, 58.9, 60.0, 59.3, 59.5, 59.5, 60.1, 58.8, 57.8, 57.0, 57.2, 56.7, 56.7, 56.8, 57.8, 57.9, 58.2, 59.2, 59.0, 59.2, 61.7, 62.6, 63.2, 63.7, 63.6, 64.6, 63.3, 63.8, 64.6, 62.9, 62.1, 61.9, 60.4, 60.5, 60.3, 58.6, 57.9, 57.5, 56.7, 57.8, 57.9, 57.9, 58.3, 60.8, 64.2, 65.2, 66.5, 67.4, 70.8, 70.5, 70.0, 69.7, 69.8, 69.8, 69.4, 68.9, 67.6, 66.8, 66.4, 66.5, 66.7, 63.8, 68.0]
SMOKE_PHOTO_LUM = [0, 0, 0, 0, 0, 68.1, 100.8, 100.0, 89.0, 79.4, 81.0, 82.2, 82.5, 82.4, 79.8, 78.0, 76.7, 77.0, 77.1, 81.6, 83.3, 85.9, 88.0, 90.2, 95.5, 101.5, 102.0, 102.3, 99.8, 99.4, 102.5, 106.7, 106.4, 106.9, 107.6, 109.4, 112.4, 114.3, 115.3, 114.6, 115.1, 116.9, 118.9, 119.8, 119.4, 120.7, 119.9, 121.0, 122.9, 123.4, 122.8, 118.9, 119.1, 118.6, 119.9, 119.7, 119.6, 120.0, 120.0, 119.9, 122.6, 121.6, 121.3, 121.4, 123.0, 120.4, 122.2, 120.5, 120.5, 118.7, 116.7, 116.2, 116.7, 115.6, 113.9, 113.3, 112.6, 113.4, 115.0, 115.0, 114.3, 114.9, 113.6, 114.7, 113.5, 114.0, 112.6, 113.0, 112.3, 111.6, 111.3, 111.7, 110.6, 108.7, 106.9, 107.3, 107.2, 108.2, 106.2, 104.3, 104.1, 103.2, 102.4, 102.5, 103.0, 103.7, 103.4, 102.0, 101.4, 100.5, 99.9, 98.6, 98.8, 97.9, 97.5, 96.5, 95.6, 94.7, 95.0, 93.3, 92.0, 90.5, 89.7, 89.6, 89.3, 89.1, 87.5, 87.6, 87.1, 86.6, 85.7, 85.5, 85.7, 84.0, 82.7, 80.7, 79.2, 79.4, 78.5, 78.4, 77.5, 76.9, 75.8, 76.0, 75.3, 76.1, 76.7, 76.6, 76.4, 74.6, 74.4, 74.0, 73.7, 73.3, 72.9, 72.8, 71.7, 70.8, 70.5, 70.4, 71.3, 71.4, 70.6, 69.5, 70.1, 69.7, 69.0, 69.3, 70.0, 70.2, 69.3, 68.4, 56.3, 44.3, 39.1, 59.7, 87.6, 99.7, 91.3, 68.5, 47.7, 40.5, 39.5, 39.2, 38.5, 37.7, 37.3, 37.6, 37.5, 37.7, 38.3, 48.2, 79.2, 91.0, 73.0, 49.8, 40.9, 39.4, 39.5, 43.5]

CYMBALS = {
    'nova': dict(title='NOVA RIDE', R=10.0, bell=0.22, hammer='scale', hammer_zone=(0.43, 0.925),
                 # (Kris 2026-10-28: a bell like Raven's; the bow falls in a longer, shallower slope to
                 # the flat ring.) Ring radii measured from Nova.png (rim 354 px = 10"): bell edge 2.2",
                 # a small step around the hole at 0.72", wide gold ring 2.2-3.55", dark groove
                 # 3.55-3.73", narrow gold ring 3.73-3.97", dark hammered bow from 4.0", gold rim band
                 # from 9.58".
                 profile=[(0.0, 0.90), (0.5, 0.88), (1.3, 0.74), (1.8, 0.53), (2.05, 0.41), (2.25, 0.35),
                          (2.45, 0.33), (4.1, 0.33), (5.0, 0.40), (6.0, 0.55), (7.0, 0.71), (7.85, 0.80),
                          (8.45, 0.75), (9.2, 0.45), (10.0, 0.0)],
                 # Line brightness in the gold zones follows the photo's measured brightness at each
                 # radius (NOVA_PHOTO_LUM).
                 # (Kris 2026-10-28: one large lathed area that includes the bell, out to 3.55"; a small
                 # unlathed ring 3.55-3.73"; a small lathed ring 3.73-3.97"; then the hammered bow.)
                 lathe=[], fine_lathe=[], gold=[(0.025, 0.355), (0.373, 0.397), (0.958, 1.0)],
                 gold_step=0.006, photo_lum=NOVA_PHOTO_LUM, bell_line=False),
    # (Kris: the bell is the darker centre, about 4" across; the bright lathed disc around it, to 4",
    # is the bow's lathed inner zone.)
    'hammered': dict(title='HAMMERED THIN RIDE', R=10.0, bell=0.19, hammer='dimple', hammer_zone=(0.43, 0.92),
                     profile=[(0.0, 1.50), (0.5, 1.47), (1.0, 1.36), (1.4, 1.14), (1.7, 0.88), (1.9, 0.75), (2.0, 0.71), (2.3, 0.68), (4.0, 0.58),
                              (5.5, 0.48), (7.5, 0.30), (9.3, 0.10), (10.0, 0.0)],
                     # Lathing measured from HammeredThinRide.png (hole centre, rim 354.5 px = 10"): one
                     # lathed area from the hole to 4.05", the bell (the darker centre) included, and a
                     # lathed band at the rim from 9.0". No groove or separate ring in this photo.
                     lathe=[], fine_lathe=[], gold=[(0.025, 0.405), (0.90, 1.0, 0.6)],
                     gold_step=0.006, photo_lum=HAMMERED_PHOTO_LUM, bell_line=False),
    # 602 flat ride, 20": no bell, a very low profile, dense radial hammering, a lathed edge band
    # set off by two rings.
    # Lathing measured from SmokeFlatRide.png (hole centre, rim 356.5 px = 10"): hammered all the
    # way in to the hole, with no bell or ring at the centre; the hammering ends at 8.55"; from
    # there to the rim a dark lathed band: a dark groove 8.58-8.75", a bright narrow ring
    # 8.77-8.93", dark lathing 8.97-9.52", a bright narrow ring 9.57-9.70", dark lathing to the rim.
    'smoke': dict(title='SMOKE FLAT RIDE', R=10.0, bell=0.16, hammer='radial', hammer_zone=(0.03, 0.855),
                  profile=[(0.0, 0.42), (1.0, 0.39), (3.0, 0.31), (6.0, 0.19), (8.8, 0.07), (9.4, 0.04), (10.0, 0.0)],
                  # (Kris 2026-10-28: most of the cymbal is the lighter gold, the dark bands at the edge.)
                  tint=[(0.0, 0.857)],
                  lathe=[], fine_lathe=[], gold=[(0.858, 1.0)], gold_lines=[0.885, 0.965],
                  zone_edges=False, gold_step=0.004, photo_lum=SMOKE_PHOTO_LUM, lum_smooth=1, bell_line=False),
    # 22" china: a small squat bell with steep sides, a straight bow sloping down, then the outer
    # flange (last ~2.5") turned up. Lathed all over, no hammering.
    'swish': dict(title='SWISH', R=11.0, bell=0.22, hammer=None, hammer_zone=(0, 0),
                  profile=[(0.0, 1.55), (1.4, 1.48), (2.0, 1.25), (2.35, 0.92), (2.6, 0.80), (4.5, 0.66),
                           (7.0, 0.42), (8.4, 0.24), (8.75, 0.22), (9.4, 0.30), (10.2, 0.50), (11.0, 0.78)],
                  lathe=[(0.0, 1.0)], fine_lathe=[(0.03, 0.21), (0.24, 0.78), (0.80, 1.0)]),
    # Raven, 20" dry ride: a small lathed bell, a moderately high bow with scale-shaped hammering,
    # two bright lathed rings at the edge.
    # (Kris: smaller, shorter bell with its own deep lathing; larger hammer scales running in a
    # spiral around the surface.)
    'raven': dict(title='RAVEN', R=10.0, bell=0.22, hammer='spiral', hammer_zone=(0.25, 0.93),
                  profile=[(0.0, 1.22), (0.5, 1.20), (1.3, 1.06), (1.85, 0.86), (2.1, 0.76), (2.3, 0.73),
                           (5.0, 0.58), (7.0, 0.40), (9.0, 0.14), (10.0, 0.0)],
                  lathe=[(0.0, 0.225), (0.935, 1.0)], fine_lathe=[], bell_grooves=(0.035, 0.215)),
}

ROSE_DARK = np.array([58, 74, 112], np.float32)     # BGR: deep rose bronze
ROSE_LIGHT = np.array([150, 176, 222], np.float32)  # BGR: light rose gold (top, under the spotlight)
LATHE = np.array([110, 160, 205], np.float32)       # BGR: lathed bright bronze
GOLD = np.array([140, 200, 236], np.float32)        # BGR: pale gold of a deep-lathed bell
HOT = np.array([215, 238, 255], np.float32)         # warm white for the lit pattern
ARC_T = np.linspace(0, 1, 6)

MODES = [(m, n, z) for m in range(0, 7) for n, z in enumerate(jn_zeros(m, 3))]
MOT = {'edge': (0.025, 1.3, 1.4, 0.15), 'bow': (0.006, 0.5, 2.0, 0.0), 'bell': (0.002, 0.3, 2.6, 0.0)}


class Cymbal:
    def __init__(self, key, elev_deg=38, cx=787, cy=400, scale=345, camd=5.0):
        c = CYMBALS[key]; self.c = c; self.key = key
        R = c['R']
        pr = np.array([p[0] for p in c['profile']]) / R; pz = np.array([p[1] for p in c['profile']]) / R
        self._prof = PchipInterpolator(pr, pz)
        self.bell = c['bell']
        b = self.bell
        rings = [np.linspace(0.03, b, max(4, int(b / 0.04))), np.linspace(b + 0.035, 0.92, max(6, int((0.92 - b) / 0.035))),
                 [0.94, 0.96, 0.98, 1.0]]
        self.rings = np.concatenate(rings); self.NR = len(self.rings); self.NT = 80
        th = np.linspace(0, 2 * np.pi, self.NT, endpoint=False)
        self.RR, self.TT = np.meshgrid(self.rings, th, indexing='ij')
        self.X0, self.Y0, self.Z0 = self.RR * np.cos(self.TT), self.RR * np.sin(self.TT), self.height(self.RR)
        self.ELEV = math.radians(elev_deg); self.CX, self.CY, self.SCALE, self.CAMD = cx, cy, scale, camd
        self.bell_ring = int(np.argmin(np.abs(self.rings - b)))
        self.pos_r = {'bell': 0.6 * b, 'bow': (b + 0.95) / 2, 'edge': 0.97}
        self._build_texture()

    def height(self, r):
        return self._prof(np.clip(np.asarray(r, float), 0, 1))

    def in_lathe(self, r):
        return any(a <= r <= b for a, b in self.c['lathe'])

    def lum(self, r):
        if not hasattr(self, '_lum'):
            import json
            p = self.c.get('photo_lum')
            self._lum = (np.array(json.load(open(p))) if isinstance(p, str) else np.array(p, float)) if p else None
            if self._lum is not None:
                n = self.c.get('lum_smooth', 9)
                self._lum = np.convolve(np.pad(self._lum, n // 2, mode='edge'), np.ones(n) / n, mode='valid')
                self._lum = self._lum / np.percentile(self._lum[5:80], 98)
        if self._lum is None:
            return 1.0
        return float(np.clip(self._lum[min(int(r / 0.005), len(self._lum) - 1)], 0.3, 1.15))

    def gold_k(self, r):
        # a gold zone's optional third value dims it (HAMMERED's rim band: 0.6)
        for z in self.c.get('gold', []):
            if z[0] <= r <= z[1]:
                return z[2] if len(z) > 2 else 1.0
        return 1.0

    def in_gold(self, r):
        # gold colour: lathed gold zones, and 'tint' zones (gold metal without lathe lines)
        return any(z[0] <= r <= z[1] for z in self.c.get('gold', []) + self.c.get('tint', []))

    def _build_texture(self):
        rng = np.random.default_rng(11)
        self.tex = []    # list of (kind, polar point arrays (r, th))
        h = self.c['hammer']; z0, z1 = self.c['hammer_zone']
        if h == 'scale':
            r = z0
            while r < z1 - 0.01:
                step = 0.05 / r; t0 = rng.uniform(0, step)
                while t0 < 2 * np.pi:
                    t = t0 + step * 0.82 * ARC_T
                    self.tex.append(('h', r + rng.uniform(-0.004, 0.004) + 0.014 * np.sin(np.pi * ARC_T), t))
                    t0 += step * rng.uniform(0.92, 1.08)
                r += 0.03
        elif h == 'dimple':
            # irregular dents, placed at random, growing larger towards the edge
            area = np.pi * (z1 ** 2 - z0 ** 2); n = int(area / 0.011)
            u = np.linspace(0, 2 * np.pi, 13)
            for _ in range(n):
                r = math.sqrt(rng.uniform(z0 ** 2, z1 ** 2)); t = rng.uniform(0, 2 * np.pi)
                grow = (r - z0) / (z1 - z0)
                a = rng.uniform(0.012, 0.026) * (0.8 + 1.6 * grow); bb = a * rng.uniform(0.35, 0.9); rot = rng.uniform(0, np.pi)
                wob = 1 + 0.25 * np.sin(2 * u + rng.uniform(0, 6.3)) * rng.uniform(0.3, 1) + 0.15 * np.sin(3 * u + rng.uniform(0, 6.3))
                wob[-1] = wob[0]
                dx = wob * (a * np.cos(u) * math.cos(rot) - bb * np.sin(u) * math.sin(rot))
                dy = wob * (a * np.cos(u) * math.sin(rot) + bb * np.sin(u) * math.cos(rot))
                x = r * math.cos(t) + dx; y = r * math.sin(t) + dy
                self.tex.append(('h', np.hypot(x, y), np.arctan2(y, x)))
        elif h == 'spiral':
            # large scale-shaped marks laid along an Archimedean spiral (pitch 0.055 R)
            pitch, width = 0.065, 0.105
            th = 0.0; r = z0
            while r < z1:
                step = width / r
                t = th + step * 0.85 * ARC_T
                rr = z0 + pitch * t / (2 * np.pi) + 0.032 * np.sin(np.pi * ARC_T) + rng.uniform(-0.005, 0.005)
                self.tex.append(('h', rr, t))
                th += step * rng.uniform(0.95, 1.05); r = z0 + pitch * th / (2 * np.pi)
        elif h == 'radial':
            area = np.pi * (z1 ** 2 - z0 ** 2); n = int(area / 0.0025)
            for _ in range(n):
                r = math.sqrt(rng.uniform(max(z0, 0.05) ** 2, z1 ** 2)); t = rng.uniform(0, 2 * np.pi)
                L = rng.uniform(0.015, 0.035)
                self.tex.append(('h', np.array([r - L / 2, r + L / 2]), np.array([t, t + rng.uniform(-0.01, 0.01)])))
        # fine lathe lines: thin full rings, every 0.012 R
        if 'bell_grooves' in self.c:
            # deep lathe grooves on the bell: irregular spacing, bold, in a paler gold
            a, b = self.c['bell_grooves']; r = a
            while r < b:
                t = np.linspace(0, 2 * np.pi, 97)
                self.tex.append(('g', np.full_like(t, r), t, rng.uniform(0.55, 1.0)))
                r += rng.uniform(0.011, 0.026)
        edges = sorted(({r for z in self.c.get('gold', []) for r in z[:2] if 0.02 < r < 0.999} if self.c.get('zone_edges', True) else set())
                       | set(self.c.get('gold_lines', [])))
        for r in edges:
            if True:
                    t = np.linspace(0, 2 * np.pi, 97)
                    self.tex.append(('e', np.full_like(t, r), t))
        for a, b in self.c['fine_lathe']:
            for r in np.arange(a, b, 0.012):
                t = np.linspace(0, 2 * np.pi, 97)
                self.tex.append(('l', np.full_like(t, r), t))
        for z in self.c.get('gold', []):
            for r in np.arange(z[0], z[1], self.c.get('gold_step', 0.012)):
                t = np.linspace(0, 2 * np.pi, 97)
                self.tex.append(('l', np.full_like(t, r), t))

    def project(self, X, Y, Z):
        yv = Y * math.sin(self.ELEV) - Z * math.cos(self.ELEV)
        zv = Y * math.cos(self.ELEV) + Z * math.sin(self.ELEV)
        persp = self.CAMD / (self.CAMD - zv)
        return self.CX + self.SCALE * X * persp, self.CY + self.SCALE * yv * persp, zv

    # ------------------------------------------------------------ motion
    def set_hits(self, hits, seed=3):
        rng = np.random.default_rng(seed); ang = math.radians(45)
        self.hits = hits
        self.pre = []
        for (t, art, v) in hits:
            a = ang + math.radians(rng.uniform(-12, 12)); r = self.pos_r[art]
            w = []
            for (m, n, z) in MODES:
                amp = abs(jv(m, z * r)) / max(np.abs(jv(m, z * np.linspace(0, 1, 50))).max(), 1e-6)
                pref = {'bell': 1.0 / (1 + m), 'bow': 1.0 / (1 + abs(m - 2)), 'edge': (1 + m) / 7.0}[art]
                w.append(amp * pref * math.exp(-(m + 1.5 * n) / (2.0 + 6.0 * v)))
            w = np.array(w); self.pre.append((t, art, v, a, w / w.max()))

    def modal(self, t):
        disp = np.zeros_like(self.RR)
        for (th, art, v, ang, w) in self.pre:
            age = t - th
            if age < 0:
                continue
            for k, (m, n, z) in enumerate(MODES):
                if w[k] < 0.05:
                    continue
                tau = 0.25 + 1.2 / (1 + m + 2 * n) * (1.6 if art == 'edge' else 1.0)
                a = v * w[k] * math.exp(-age / tau) * (1 - math.exp(-age / 0.03))
                if a < 1e-3:
                    continue
                s = jv(m, z * self.RR) * np.cos(m * (self.TT - ang)); s /= max(np.abs(s).max(), 1e-6)
                disp += a * s * math.cos(2 * math.pi * (2.0 + 0.8 * (m + n)) * age)
        return disp

    def wobble(self, t):
        T = np.zeros(2); spin = 0.0
        for (th, art, v, ang, w) in self.pre:
            age = t - th
            if age < 0:
                continue
            A, tau, f, sp = MOT[art]
            ramp = (1 - math.exp(-age / 0.02)) * math.exp(-age / tau)
            u = np.array([math.cos(ang), math.sin(ang)]); p = np.array([-u[1], u[0]])
            d = math.cos(2 * math.pi * f * age) * u + 0.85 * math.sin(2 * math.pi * f * age) * p
            T += A * v * ramp * d; spin += sp * v * ramp * math.cos(2 * math.pi * f * age)
        return T, spin

    # ------------------------------------------------------------ drawing
    def draw(self, img, t=0.0, animate=True):
        """Draws the cymbal (and the hit flashes) onto img (float32 BGR, full size)."""
        RR, TT, NR, NT = self.RR, self.TT, self.NR, self.NT
        if animate and getattr(self, 'pre', None):
            disp = self.modal(t); T, spin = self.wobble(t)
        else:
            disp = np.zeros_like(RR); T, spin = np.zeros(2), 0.0
        k = np.linalg.norm(T); cs, sn = math.cos(math.radians(spin)), math.sin(math.radians(spin))
        X = self.X0 * cs - self.Y0 * sn; Y = self.X0 * sn + self.Y0 * cs
        stiff = np.clip((RR - (self.bell - 0.1)) / 0.15, 0.12, 1.0)
        Z = self.Z0 + 0.010 * disp * stiff
        if k > 1e-6:
            Z = Z - 2.2 * (X * T[0] + Y * T[1])
        sx, sy, sz = self.project(X, Y, Z)
        ad = np.abs(disp) * stiff; mx = ad.max()
        lvl = min(1.0, mx / 0.35) ** 0.5 if mx > 1e-4 else 0.0
        g = (ad / max(mx, 1e-6)) ** 1.8 * lvl
        layer = np.zeros_like(img)
        ytop, ybot = sy.min(), sy.max()
        dzr = np.gradient(Z, self.rings, axis=0); dzt = np.gradient(Z, axis=1) / (2 * np.pi / NT) / np.maximum(RR, 1e-3)
        nx_ = -(dzr * np.cos(TT) - dzt * np.sin(TT)); ny_ = -(dzr * np.sin(TT) + dzt * np.cos(TT))
        nn = np.sqrt(nx_ ** 2 + ny_ ** 2 + 1); LGT = np.array([0.0, -0.55, 0.83])
        shade = np.clip((ny_ * LGT[1] + LGT[2]) / nn, 0, 1)

        def colour(rr, sh, yy, gg=0.0):
            ytl = 1 - (yy - ytop) / max(ybot - ytop, 1)
            light = np.clip(((sh - 0.86) / 0.14) * 0.8 + 0.2 * ytl, 0, 1)
            c = ROSE_DARK + (ROSE_LIGHT - ROSE_DARK) * (0.25 + 0.75 * light) ** 1.4
            if any(z[0] <= rr <= z[1] for z in self.c.get('tint', [])):
                c = c * 0.15 + GOLD * (0.62 + 0.38 * light) * self.lum(rr)      # lighter gold metal
            elif self.in_gold(rr):
                c = c * 0.25 + GOLD * (0.42 + 0.48 * light) * self.lum(rr) * self.gold_k(rr)
            elif self.in_lathe(rr):
                c = c * 0.55 + LATHE * (0.30 + 0.35 * light)
            return c + (HOT - c) * gg

        def col(i0, j0, i1, j1):
            c = colour(0.5 * (RR[i0, j0] + RR[i1, j1]), 0.5 * (shade[i0, j0] + shade[i1, j1]),
                       0.5 * (sy[i0, j0] + sy[i1, j1]), 0.5 * (g[i0, j0] + g[i1, j1]))
            return tuple(float(x) for x in c)

        for i in range(NR):
            thick = 2 if (i == NR - 1 or (i == self.bell_ring and self.c.get('bell_line', True))) else 1
            for j in range(NT):
                j2 = (j + 1) % NT
                cv2.line(layer, (int(sx[i, j] * 4), int(sy[i, j] * 4)), (int(sx[i, j2] * 4), int(sy[i, j2] * 4)),
                         col(i, j, i, j2), thick, cv2.LINE_AA, shift=2)
        for j in range(0, NT, 2):
            for i in range(NR - 1):
                cv2.line(layer, (int(sx[i, j] * 4), int(sy[i, j] * 4)), (int(sx[i + 1, j] * 4), int(sy[i + 1, j] * 4)),
                         col(i, j, i + 1, j), 1, cv2.LINE_AA, shift=2)
        # texture (fixed to the surface: follows spin and tilt; not the modal flex)
        tex_layer = np.zeros_like(img)
        for item in self.tex:
            kind, r, th = item[:3]
            x = r * np.cos(th); y = r * np.sin(th)
            X2, Y2 = x * cs - y * sn, x * sn + y * cs
            Z2 = self.height(r) + 0.0015
            if k > 1e-6:
                Z2 = Z2 - 2.2 * (X2 * T[0] + Y2 * T[1])
            px, py, pz = self.project(X2, Y2, Z2)
            if kind == 'g':
                ytl = 1 - (py - ytop) / max(ybot - ytop, 1)
                for q in range(0, len(px) - 2, 2):
                    c = GOLD * (0.30 + 0.32 * ytl[q]) * item[3]
                    cv2.line(tex_layer, (int(px[q] * 4), int(py[q] * 4)), (int(px[q + 2] * 4), int(py[q + 2] * 4)),
                             tuple(float(v) for v in c), 2, cv2.LINE_AA, shift=2)
            elif kind == 'e':
                ytl = 1 - (py - ytop) / max(ybot - ytop, 1)
                for q in range(0, len(px) - 2, 2):
                    c = GOLD * (0.55 + 0.40 * ytl[q]) * self.gold_k(float(r[q]))
                    cv2.line(tex_layer, (int(px[q] * 4), int(py[q] * 4)), (int(px[q + 2] * 4), int(py[q + 2] * 4)),
                             tuple(float(v) for v in c), 2, cv2.LINE_AA, shift=2)
            elif kind == 'l':
                # lathe lines: per-segment brightness from the screen height (lighter at the top)
                ytl = 1 - (py - ytop) / max(ybot - ytop, 1)
                for q in range(0, len(px) - 1, 2):
                    c = colour(float(r[q]), 0.9, float(py[q])) * ((0.50 + 0.35 * ytl[q]) * self.lum(float(r[q])) if self.in_gold(float(r[q])) else (0.30 + 0.25 * ytl[q]))
                    cv2.line(tex_layer, (int(px[q] * 4), int(py[q] * 4)), (int(px[q + 2 if q + 2 < len(px) else q + 1] * 4), int(py[q + 2 if q + 2 < len(px) else q + 1] * 4)),
                             tuple(float(v) for v in c), 1, cv2.LINE_AA, shift=2)
            else:
                c = colour(float(np.mean(r)), 0.9, float(py.mean())) * (1.0 if self.c['hammer'] == 'spiral' else 0.8)
                pts = np.stack([px * 4, py * 4], 1).astype(np.int32)
                cv2.polylines(tex_layer, [pts], False, tuple(float(v) for v in c), 1, cv2.LINE_AA, shift=2)
        layer = np.maximum(layer, tex_layer)
        bloom = cv2.GaussianBlur(layer, (0, 0), 3.0)
        img = np.maximum(img, layer) + 0.5 * bloom * (layer.max(axis=2, keepdims=True) > 150)
        if animate and getattr(self, 'pre', None):
            for (th, art, v, ang, w) in self.pre:
                age = t - th; T_ = 0.25 + 0.25 * v
                if 0 <= age < T_:
                    r = self.pos_r[art]
                    hx, hy, hz = self.project(np.array(r * math.cos(ang)), np.array(r * math.sin(ang)), self.height(np.array(r)))
                    kk = (1 - age / T_) ** 2; rad = int((10 + 22 * v) * (1 - 0.3 * age / T_))
                    spot = np.zeros_like(img); cv2.circle(spot, (int(hx), int(hy)), rad, (230, 245, 255), -1, cv2.LINE_AA)
                    img = img + cv2.GaussianBlur(spot, (0, 0), rad * 0.6) * kk * (0.4 + 0.6 * v)
        return img

    def profile_strip(self, w=1100, h=230):
        img = np.full((h, w, 3), 15, np.uint8); R = self.c['R']
        half = (w - 100) / 2 / 11.0      # pixels per inch, same scale for every cymbal (22" fits)
        cx = w // 2
        r = np.linspace(-1, 1, 900); z = self.height(np.abs(r)) * R
        pts = np.stack([cx + r * R * half, h - 50 - z * half], 1).astype(np.int32)
        cv2.polylines(img, [pts], False, (140, 170, 215), 2, cv2.LINE_AA)
        for xin in range(-(int(R) // 2) * 2, int(R) + 1, 2):
            x = int(cx + xin * half); cv2.line(img, (x, h - 35), (x, h - 28), (90, 90, 90), 1)
            cv2.putText(img, f'{abs(xin)}"', (x - 12, h - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (120, 120, 120), 1, cv2.LINE_AA)
        cv2.putText(img, f'{self.c["title"]}  ({2 * R:.0f}" across; height to the same scale)', (20, 26),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (170, 170, 170), 1, cv2.LINE_AA)
        return img


# =====================================================================================
# Cymbal2: the hammering pressed into the surface as dents (HAMMERED THIN RIDE). The mesh is
# sampled finely (197 radial x 480 around) so the rings and spokes bend through each dent; the
# lighting picks out the dent walls. The defaults are the approved values.
# =====================================================================================
DENTED = {'hammered'}



class Cymbal2:
    def __init__(self, key, elev_deg=38, cx=787, cy=400, scale=345, camd=5.0, dent_depth=2.4,
                 ring_step=4, nt=480, spoke_every=12, dent_size=2.0, lip=0.0, dent_light=0.0, dent_contrast=5.0, small_dents=110):
        c = CYMBALS[key]; self.c = c; self.key = key
        R = c['R']
        pr = np.array([p[0] for p in c['profile']]) / R; pz = np.array([p[1] for p in c['profile']]) / R
        self._prof = PchipInterpolator(pr, pz)
        self.bell = b = c['bell']
        # fine polar grid: 0.005 R radially, 480 samples around
        self.rf = np.linspace(0.02, 1.0, 197); self.NTf = nt
        self.tf = np.linspace(0, 2 * np.pi, nt, endpoint=False)
        self.RR, self.TT = np.meshgrid(self.rf, self.tf, indexing='ij')
        self.XX, self.YY = self.RR * np.cos(self.TT), self.RR * np.sin(self.TT)
        self.dent_depth = dent_depth; self.dent_size = dent_size; self.lip = lip; self.dent_light = dent_light; self.dent_contrast = dent_contrast; self.small_dents = small_dents
        self.dents = self._make_dents()
        self.DZ = self._dz(self.XX, self.YY)
        self.Z0 = self.height(self.RR) + self.DZ
        # rings drawn: every ring_step-th radial sample, plus the bell edge and the rim
        idx = set(range(0, len(self.rf), ring_step)) | {len(self.rf) - 1}
        self.bell_ring = int(np.argmin(np.abs(self.rf - b))); idx.add(self.bell_ring)
        self.ring_idx = sorted(idx)
        self.spoke_idx = list(range(0, nt, spoke_every))
        self.ELEV = math.radians(elev_deg); self.CX, self.CY, self.SCALE, self.CAMD = cx, cy, scale, camd
        self.pos_r = {'bell': 0.6 * b, 'bow': (b + 0.95) / 2, 'edge': 0.97}
        self.lathe_mask = np.zeros_like(self.RR, bool)
        for a, bb in c['lathe']:
            self.lathe_mask |= (self.RR >= a) & (self.RR <= bb)
        self.gold_mask = np.zeros_like(self.RR, bool)
        for z in c.get('gold', []):
            self.gold_mask |= (self.RR >= z[0]) & (self.RR <= z[1])
        self.lumR = np.array([self.lum(r) * self.gold_k(r) for r in self.rf])[:, None] * np.ones((1, nt))
        self._lines()
        # modal shapes: radial parts precomputed on the fine grid
        self.Jr = [jv(m, z * self.rf) for (m, n, z) in MODES]
        self.Jn = [max(np.abs(jv(m, z * np.linspace(0, 1, 50))).max(), 1e-6) for (m, n, z) in MODES]

    def lum(self, r):
        if not hasattr(self, '_lum'):
            import json
            p = self.c.get('photo_lum')
            self._lum = (np.array(json.load(open(p))) if isinstance(p, str) else np.array(p, float)) if p else None
            if self._lum is not None:
                self._lum = np.convolve(np.pad(self._lum, 4, mode='edge'), np.ones(9) / 9, mode='valid')
                self._lum = self._lum / np.percentile(self._lum[5:80], 98)
        if self._lum is None:
            return 1.0
        return float(np.clip(self._lum[min(int(r / 0.005), len(self._lum) - 1)], 0.3, 1.15))

    def gold_k(self, r):
        for z in self.c.get('gold', []):
            if z[0] <= r <= z[1]:
                return z[2] if len(z) > 2 else 1.0
        return 1.0

    def height(self, r):
        return self._prof(np.clip(np.asarray(r, float), 0, 1))

    # ------------------------------------------------------------ hammering as dents
    def _make_dents(self):
        """(x, y, a (along), b (across), rot, depth) in units of R."""
        rng = np.random.default_rng(11)
        c = self.c; h = c['hammer']; z0, z1 = c['hammer_zone']; D = []
        if h == 'dimple':
            area = np.pi * (z1 ** 2 - z0 ** 2); n = int(area / 0.011 / self.dent_size ** 1.5)
            for _ in range(n):
                r = math.sqrt(rng.uniform(z0 ** 2, z1 ** 2)); t = rng.uniform(0, 2 * np.pi)
                grow = (r - z0) / (z1 - z0)
                a = rng.uniform(0.012, 0.026) * (0.8 + 1.6 * grow) * self.dent_size; bb = a * rng.uniform(0.45, 0.9)
                D.append((r * math.cos(t), r * math.sin(t), a, bb, rng.uniform(0, np.pi), 0.0042 * (a / 0.03) ** 0.7 * rng.uniform(0.6, 1.2)))
            # smaller dents in the gaps between the large ones (no overlap)
            tries = 0; added = 0
            while added < self.small_dents and tries < 20000:
                tries += 1
                r = math.sqrt(rng.uniform(z0 ** 2, z1 ** 2)); t = rng.uniform(0, 2 * np.pi)
                grow = (r - z0) / (z1 - z0)
                a = rng.uniform(0.018, 0.028) * (0.85 + 0.5 * grow); bb = a * rng.uniform(0.5, 0.9)
                x, y = r * math.cos(t), r * math.sin(t)
                if any(math.hypot(x - d[0], y - d[1]) < 0.9 * (a + d[2]) for d in D):
                    continue
                D.append((x, y, a, bb, rng.uniform(0, np.pi), 0.0042 * (a / 0.03) ** 0.7 * rng.uniform(0.6, 1.1))); added += 1
        elif h in ('scale', 'spiral'):
            if h == 'scale':
                r = z0
                while r < z1 - 0.01:
                    step = 0.05 / r; t0 = rng.uniform(0, step)
                    while t0 < 2 * np.pi:
                        tm = t0 + step * 0.41; rr = r + rng.uniform(-0.004, 0.004)
                        D.append((rr * math.cos(tm), rr * math.sin(tm), 0.022, 0.011, tm + np.pi / 2, 0.0032))
                        t0 += step * rng.uniform(0.92, 1.08)
                    r += 0.03
            else:
                pitch, width = 0.065, 0.105; th = 0.0; r = z0
                while r < z1:
                    step = width / r; tm = th + step * 0.42
                    rr = z0 + pitch * tm / (2 * np.pi) + rng.uniform(-0.005, 0.005)
                    D.append((rr * math.cos(tm), rr * math.sin(tm), 0.042, 0.022, tm + np.pi / 2, 0.0045))
                    th += step * rng.uniform(0.95, 1.05); r = z0 + pitch * th / (2 * np.pi)
        elif h == 'radial':
            area = np.pi * (z1 ** 2 - z0 ** 2); n = int(area / 0.0025)
            for _ in range(n):
                r = math.sqrt(rng.uniform(max(z0, 0.05) ** 2, z1 ** 2)); t = rng.uniform(0, 2 * np.pi)
                L = rng.uniform(0.015, 0.035)
                D.append((r * math.cos(t), r * math.sin(t), L / 2, 0.006, t, 0.0016))
        return D

    def _dz(self, X, Y):
        dz = np.zeros_like(X)
        for (x, y, a, b, rot, d) in self.dents:
            dx = X - x; dy = Y - y
            near = (np.abs(dx) < 3 * a) & (np.abs(dy) < 3 * a)
            if not near.any():
                continue
            u = dx[near] * math.cos(rot) + dy[near] * math.sin(rot)
            v = -dx[near] * math.sin(rot) + dy[near] * math.cos(rot)
            q = (u / a) ** 2 + (v / b) ** 2
            # a dish with a slight raised lip where the metal is pushed out
            dz[near] += d * self.dent_depth * (-np.exp(-1.6 * q) + self.lip * np.exp(-((np.sqrt(q) - 1.15) ** 2) / 0.08))
        return dz

    def _lines(self):
        """Lathe lines and bell grooves: thin surface rings (no dents in lathed zones)."""
        rng = np.random.default_rng(5); self.tex = []
        if 'bell_grooves' in self.c:
            a, b = self.c['bell_grooves']; r = a
            while r < b:
                self.tex.append(('g', r, rng.uniform(0.55, 1.0))); r += rng.uniform(0.011, 0.026)
        for a, b in self.c['fine_lathe']:
            for r in np.arange(a, b, 0.012):
                self.tex.append(('l', r, 1.0))
        for z in self.c.get('gold', []):
            for r in np.arange(z[0], z[1], self.c.get('gold_step', 0.012)):
                self.tex.append(('G', r, self.lum(r) * self.gold_k(r)))
        for r in sorted({r for z in self.c.get('gold', []) for r in z[:2] if 0.03 < r < 0.999} | set(self.c.get('gold_lines', []))):
            self.tex.append(('E', r, self.gold_k(r)))

    def project(self, X, Y, Z):
        yv = Y * math.sin(self.ELEV) - Z * math.cos(self.ELEV)
        zv = Y * math.cos(self.ELEV) + Z * math.sin(self.ELEV)
        persp = self.CAMD / (self.CAMD - zv)
        return self.CX + self.SCALE * X * persp, self.CY + self.SCALE * yv * persp, zv

    # ------------------------------------------------------------ motion
    def set_hits(self, hits, seed=3):
        rng = np.random.default_rng(seed); ang = math.radians(45); self.pre = []
        for (t, art, v) in hits:
            a = ang + math.radians(rng.uniform(-12, 12)); r = self.pos_r[art]; w = []
            for k, (m, n, z) in enumerate(MODES):
                amp = abs(jv(m, z * r)) / self.Jn[k] if hasattr(self, 'Jn') else 0
                pref = {'bell': 1.0 / (1 + m), 'bow': 1.0 / (1 + abs(m - 2)), 'edge': (1 + m) / 7.0}[art]
                w.append(amp * pref * math.exp(-(m + 1.5 * n) / (2.0 + 6.0 * v)))
            w = np.array(w); self.pre.append((t, art, v, a, w / w.max()))

    def modal(self, t):
        disp = np.zeros_like(self.RR)
        for (th, art, v, ang, w) in self.pre:
            age = t - th
            if age < 0:
                continue
            for k, (m, n, z) in enumerate(MODES):
                if w[k] < 0.05:
                    continue
                tau = 0.25 + 1.2 / (1 + m + 2 * n) * (1.6 if art == 'edge' else 1.0)
                a = v * w[k] * math.exp(-age / tau) * (1 - math.exp(-age / 0.03))
                if a < 1e-3:
                    continue
                s = np.outer(self.Jr[k], np.cos(m * (self.tf - ang)))
                s /= max(np.abs(s).max(), 1e-6)
                disp += a * s * math.cos(2 * math.pi * (2.0 + 0.8 * (m + n)) * age)
        return disp

    def wobble(self, t):
        T = np.zeros(2); spin = 0.0
        for (th, art, v, ang, w) in self.pre:
            age = t - th
            if age < 0:
                continue
            A, tau, f, sp = MOT[art]
            ramp = (1 - math.exp(-age / 0.02)) * math.exp(-age / tau)
            u = np.array([math.cos(ang), math.sin(ang)]); p = np.array([-u[1], u[0]])
            d = math.cos(2 * math.pi * f * age) * u + 0.85 * math.sin(2 * math.pi * f * age) * p
            T += A * v * ramp * d; spin += sp * v * ramp * math.cos(2 * math.pi * f * age)
        return T, spin

    # ------------------------------------------------------------ drawing
    def draw(self, img, t=0.0, animate=True):
        RR, TT = self.RR, self.TT
        live = animate and getattr(self, 'pre', None)
        disp = self.modal(t) if live else np.zeros_like(RR)
        T, spin = self.wobble(t) if live else (np.zeros(2), 0.0)
        k = np.linalg.norm(T); cs, sn = math.cos(math.radians(spin)), math.sin(math.radians(spin))
        X = self.XX * cs - self.YY * sn; Y = self.XX * sn + self.YY * cs
        stiff = np.clip((RR - (self.bell - 0.1)) / 0.15, 0.12, 1.0)
        Z = self.Z0 + 0.010 * disp * stiff
        if k > 1e-6:
            Z = Z - 2.2 * (X * T[0] + Y * T[1])
        sx, sy, sz = self.project(X, Y, Z)
        ad = np.abs(disp) * stiff; mx = ad.max()
        lvl = min(1.0, mx / 0.35) ** 0.5 if mx > 1e-4 else 0.0
        g = (ad / max(mx, 1e-6)) ** 1.8 * lvl
        ytop, ybot = sy.min(), sy.max()
        # surface normal from the full height field (profile + dents + flex), in the cymbal's frame
        LGT = np.array([0.0, -0.55, 0.83])
        def shade_of(Zs):
            dzr = np.gradient(Zs, self.rf, axis=0)
            dzt = np.gradient(Zs, axis=1) / (2 * np.pi / self.NTf) / np.maximum(RR, 1e-3)
            ny_ = -(dzr * np.sin(TT) + dzt * np.cos(TT))
            return np.clip((ny_ * LGT[1] + LGT[2]) / np.sqrt(dzr ** 2 + dzt ** 2 + 1), 0, 1)
        shade_s = shade_of(Z - self.DZ)                 # the smooth surface: the overall lighting
        dshade = shade_of(Z) - shade_s                  # what the dents add: lit and shadowed walls
        ytl = 1 - (sy - ytop) / max(ybot - ytop, 1)
        light = np.clip(((shade_s - 0.86) / 0.14) * 0.8 + 0.2 * ytl + self.dent_light * dshade, 0, 1)
        C = ROSE_DARK + (ROSE_LIGHT - ROSE_DARK) * ((0.25 + 0.75 * light) ** 1.4)[..., None]
        Cl = C * 0.55 + LATHE * (0.30 + 0.35 * light)[..., None]
        C = np.where(self.lathe_mask[..., None], Cl, C)
        Cg = C * 0.25 + GOLD * ((0.42 + 0.48 * light) * self.lumR)[..., None]
        C = np.where(self.gold_mask[..., None], Cg, C)
        # dent walls: shadowed sides fade, lit sides brighten toward the highlight
        f = np.clip(1 + self.dent_contrast * dshade, 0.25, 1.9)[..., None]
        C = np.where(f > 1, C + (ROSE_LIGHT * 1.15 - C) * np.clip(f - 1, 0, 1), C * f)
        C = C + (HOT - C) * g[..., None]
        P = np.stack([sx * 4, sy * 4], -1).astype(np.int32)
        layer = np.zeros_like(img)
        NT = self.NTf
        for i in self.ring_idx:
            thick = 2 if (i == len(self.rf) - 1 or (i == self.bell_ring and self.c.get('bell_line', True))) else 1
            ci = 0.5 * (C[i] + np.roll(C[i], -1, axis=0)); Pi = P[i]
            for j in range(NT):
                j2 = (j + 1) % NT
                cv2.line(layer, tuple(Pi[j]), tuple(Pi[j2]), tuple(float(x) for x in ci[j]), thick, cv2.LINE_AA, shift=2)
        for j in self.spoke_idx:
            cj = 0.5 * (C[:-1, j] + C[1:, j]); Pj = P[:, j]
            for i in range(len(self.rf) - 1):
                cv2.line(layer, tuple(Pj[i]), tuple(Pj[i + 1]), tuple(float(x) for x in cj[i]), 1, cv2.LINE_AA, shift=2)
        # lathe lines and bell grooves (follow the tilt, spin and profile)
        tq = np.linspace(0, 2 * np.pi, 97)
        for (kind, r, inten) in self.tex:
            x = r * np.cos(tq); y = r * np.sin(tq)
            X2, Y2 = x * cs - y * sn, x * sn + y * cs
            Z2 = self.height(r) + 0.0015 + np.zeros_like(tq)
            if k > 1e-6:
                Z2 = Z2 - 2.2 * (X2 * T[0] + Y2 * T[1])
            px, py, pz = self.project(X2, Y2, Z2)
            yt = 1 - (py - ytop) / max(ybot - ytop, 1)
            for q in range(0, len(px) - 2, 2):
                if kind == 'g':
                    c = GOLD * (0.30 + 0.32 * yt[q]) * inten; th = 2
                elif kind == 'G':
                    c = GOLD * (0.50 + 0.35 * yt[q]) * inten; th = 1
                elif kind == 'E':
                    c = GOLD * (0.55 + 0.40 * yt[q]) * inten; th = 2
                else:
                    lt = np.clip(0.2 * yt[q], 0, 1); base_c = ROSE_DARK + (ROSE_LIGHT - ROSE_DARK) * (0.25 + 0.75 * lt) ** 1.4
                    c = (base_c * 0.55 + LATHE * (0.30 + 0.35 * lt)) * (0.30 + 0.25 * yt[q]); th = 1
                cv2.line(layer, (int(px[q] * 4), int(py[q] * 4)), (int(px[q + 2] * 4), int(py[q + 2] * 4)),
                         tuple(float(v) for v in c), th, cv2.LINE_AA, shift=2)
        bloom = cv2.GaussianBlur(layer, (0, 0), 3.0)
        img = np.maximum(img, layer) + 0.5 * bloom * (layer.max(axis=2, keepdims=True) > 150)
        if live:
            for (th, art, v, ang, w) in self.pre:
                age = t - th; T_ = 0.25 + 0.25 * v
                if 0 <= age < T_:
                    r = self.pos_r[art]
                    hx, hy, hz = self.project(np.array(r * math.cos(ang)), np.array(r * math.sin(ang)), self.height(np.array(r)))
                    kk = (1 - age / T_) ** 2; rad = int((10 + 22 * v) * (1 - 0.3 * age / T_))
                    spot = np.zeros_like(img); cv2.circle(spot, (int(hx), int(hy)), rad, (230, 245, 255), -1, cv2.LINE_AA)
                    img = img + cv2.GaussianBlur(spot, (0, 0), rad * 0.6) * kk * (0.4 + 0.6 * v)
        return img
