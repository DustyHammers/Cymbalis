"""Approved reference for the wireframe cymbal (UI_SPEC section 11): Kris approved the mockups
made with this script on 2026-10-27. Documentation only: Python (numpy, scipy, OpenCV), not
built, not run in CI. Keys: nova = NOVA RIDE, hammered = HAMMERED THIN RIDE, smoke = SMOKE FLAT
RIDE, swish = SWISH, raven = RAVEN. Colours below are OpenCV BGR. Coordinates are 1536x1024
design units.
Shapes approved by Kris 2026-10-27 (profiles in inches, lathe zones, hammering styles).
The hammering is drawn on the surface; dents pressed into the mesh are a later step."""
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
CYMBALS = {
    'nova': dict(title='NOVA RIDE', R=10.0, bell=0.40, hammer='scale', hammer_zone=(0.43, 0.925),
                 profile=[(0.0, 1.55), (1.0, 1.25), (2.7, 0.80), (3.3, 0.62), (4.0, 0.36), (4.4, 0.33),
                          (5.85, 0.33), (6.4, 0.43), (7.1, 0.64), (7.85, 0.80), (8.45, 0.75), (9.2, 0.45), (10.0, 0.0)],
                 lathe=[(0.27, 0.405), (0.935, 1.0)], fine_lathe=[]),
    # Bettis, 20" paper-thin ride: a large rounded lathed bell (8" across), a low, gently falling bow
    # with large, deep, irregular hammer dents, a lathed band at the edge.
    # (Kris: the bell is the darker centre, about 4" across; the bright lathed disc around it, to 4",
    # is the bow's lathed inner zone.)
    'hammered': dict(title='HAMMERED THIN RIDE', R=10.0, bell=0.19, hammer='dimple', hammer_zone=(0.43, 0.92),
                     profile=[(0.0, 1.50), (0.5, 1.47), (1.0, 1.36), (1.4, 1.14), (1.7, 0.88), (1.9, 0.75), (2.0, 0.71), (2.3, 0.68), (4.0, 0.58),
                              (5.5, 0.48), (7.5, 0.30), (9.3, 0.10), (10.0, 0.0)],
                     lathe=[(0.0, 0.405), (0.92, 1.0)], fine_lathe=[(0.03, 0.40)]),
    # 602 flat ride, 20": no bell, a very low profile, dense radial hammering, a lathed edge band
    # set off by two rings.
    'smoke': dict(title='SMOKE FLAT RIDE', R=10.0, bell=0.16, hammer='radial', hammer_zone=(0.03, 0.885),
                  profile=[(0.0, 0.42), (1.0, 0.39), (3.0, 0.31), (6.0, 0.19), (8.8, 0.07), (9.4, 0.04), (10.0, 0.0)],
                  lathe=[(0.885, 1.0)], fine_lathe=[(0.89, 0.95)]),
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
        for a, b in self.c['fine_lathe']:
            for r in np.arange(a, b, 0.012):
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
            if self.in_lathe(rr):
                c = c * 0.55 + LATHE * (0.30 + 0.35 * light)
            return c + (HOT - c) * gg

        def col(i0, j0, i1, j1):
            c = colour(0.5 * (RR[i0, j0] + RR[i1, j1]), 0.5 * (shade[i0, j0] + shade[i1, j1]),
                       0.5 * (sy[i0, j0] + sy[i1, j1]), 0.5 * (g[i0, j0] + g[i1, j1]))
            return tuple(float(x) for x in c)

        for i in range(NR):
            thick = 2 if i in (self.bell_ring, NR - 1) else 1
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
            elif kind == 'l':
                # lathe lines: per-segment brightness from the screen height (lighter at the top)
                ytl = 1 - (py - ytop) / max(ybot - ytop, 1)
                for q in range(0, len(px) - 1, 2):
                    c = colour(float(r[q]), 0.9, float(py[q])) * (0.30 + 0.25 * ytl[q])
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
