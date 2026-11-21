"""Approved reference for the HI-HAT preset's strike model (Kris, 2026-11-20). Documentation only:
Python (numpy, scipy), not built, not run in CI, no audio in the repo. It reads presets/hihat/ and
gives, for a strike, each mode's amplitude envelope; the plugin's engine does the same in C++.

Model (prototype renders approved by ear in the project session, 2026-11-20):
- Knots: closed TIP and EDGE at velocities 0.3 / 0.6 / 1.0, and EDGE opened in four steps
  (openness 0.25 / 0.5 / 0.75 / 1.0) at one velocity each.
- A strike at openness o and velocity v: per band, the envelope is interpolated in log amplitude
  between the two knots around o; velocity comes from the closed captures directly (closed knots)
  or, for the open knots, from the closed EDGE's level change with velocity per band (dB, first
  100 ms). Each mode's amplitude = its weight (interpolated the same way) x its band's envelope.
- Stick articulations (TIP, EDGE) fire the modes at full level on the strike: SPEAR smears the
  strike over its first frames (a 20-40 ms rise); that rise is replaced by the band's decay after
  its peak, extended back to t = 0 (at most +9 dB above the captured peak). FOOT keeps SPEAR's own
  rise (it is the pedal closing the cymbals, not a stick).
- The tip at an opening uses the EDGE's envelope at that opening times the per-band level
  difference between the closed tip and the closed edge at that velocity, with the closed tip's
  mode weights. TIP OPEN LEVEL (tip_open_level, a Tuning window value, 0 .. 1: Kris, 2026-11-21)
  takes part of that difference off on open hats: each open knot's tip at the knot's openness x
  uses (1 - tip_open_level x) x the difference, so the tip between knots follows it smoothly in o
  (0, or a closed strike: as captured). The gap grows with velocity (the closed tip is about 4.5 dB
  under the closed edge at 0.6 and 10.9 dB at 1.0), so a hard tip on open hats sat 7-8 dB under
  the edge at every opening.
- The pedal moves a ringing strike's decay both ways (approved by Kris, 2026-11-21: prototype
  renders, version B; the opened state, the threshold and the clamp: Kris, 2026-11-21, the pedal
  bursts fix; the floor rule, the choke from the highest opening and the rate floor: Kris,
  2026-11-21, the ring tails fix). Per strike and band its level L (natural log of amplitude) is
  followed at the control rate (at most 10 ms): L(t) = L(t - dt) + step, (step, opened, o_ref) =
  level_step(s_env, o_strike, o_now, dt, t_since_strike, opened, o_ref, at_floor), s_env being the
  captured envelope's own log step over dt (ln E(t) - ln E(t - dt), as the strike reads its
  envelope), o_strike its openness at the strike, o_now the pedal's smoothed openness now,
  `opened` whether the strike has opened up and o_ref the highest openness the pedal has opened
  it to (False and o_strike at the strike; level_step returns both), at_floor per band whether
  either end of the step has the envelope under FLOOR of the strike's peak (or 0). rate(o) is
  interpolated in log between the knots' measured band decay rates, each band at least ring_floor
  x the median of the 22 there (RING FLOOR, a Tuning window value, 0 .. 2, 1 = 100 %, taken at a
  strike's note-on: at 1, closed 7.3 per second, a 0.95 s T60, and 1.5-3.1 open; 0: the raw
  rates): the lowest bands' own rates (0.5-1.05 per second for band 0) would ring on for seconds.
  - A strike that has not opened up follows its envelope: closing (o_now < o_strike), the pedal
    choke damps it further at each band's extra rate max(rate(o_now), CHOKE_RATE x (o_strike -
    o_now) / o_strike) - rate(o_strike), never negative (choke_rate); otherwise s_env.
  - Opening (o_now > o_strike + OPEN_EPS, half a CC step, so a pedal still settling towards where
    it was at the strike does not count), from OPEN_AFTER after the strike: the strike has opened
    up, from this step on for the rest of its life, and o_ref rises to o_now.
  - Once opened, its level is a state of its own: the envelope gives only its step clamped,
    s_c = s_env clipped to +-STEP_MAX dt. SPEAR's partials stop and restart, so a knot's band
    envelope can fall to its floor for a few frames and come back (a log step of about -20 and
    then +20); clamped, these dropouts cannot move the level, nor leave it raised when the pedal
    step changes while the envelope is down. Where the band's partials have stopped, its envelope
    sits flat at its floor (a step of 0): at the floor (at_floor, or a step that is not finite)
    s_c = -rate(o_strike) dt, as past the end of the captured envelope, so an opened band does
    not hold its level there. On every step: closing, from the highest opening (o_now < o_ref -
    OPEN_EPS; opening back up while still under it too), the step blends from s_c to the rate at
    o_ref as the opening did, w_ref = min(1, (o_ref - o_strike) / OPEN_BLEND), less the choke's
    extra rate from o_ref: slope = (1 - w_ref) s_c - w_ref rate(o_ref) dt - choke_rate(o_ref,
    o_now) dt (a strike struck closed has its opening to close from); opening, the step blends
    from s_c to the pedal's rate, w = min(1, (o_now - o_strike) / OPEN_BLEND), slope = (1 - w) s_c
    + w (-rate(o_now) dt), never above max(s_c, 0): opening never makes a band louder (a captured
    bloom still shows), and fully opened (w = 1) it decays at rate(o_now) from the level it had;
    otherwise s_c.
  - Past the end of its captured envelope a strike takes s_env = -rate(o_strike) dt, so a hit
    opened after it was struck rings on at the opening's rate instead of stopping when its closed
    data ends. It ends when every band is 120 dB under its peak level, or at MAX_RING_S at most,
    with the 10 ms end fade.
  - The openness of a new strike is taken at its note-on. FOOT is unchanged: the plugin applies
    only the choke to it (it is the pedal closing), with the raw band rates (rate(o, 0)), and it
    ends with its captured envelope.
"""
import json
import numpy as np

NB = 22
FR = 0.01
E22 = 11404 * 2 ** ((np.arange(NB + 1) - 21) / 3)
CHOKE_RATE = 6.91 / 0.3          # 1/s (amplitude): every band at least a 0.3 s T60 when fully closed
OPEN_BLEND = 0.1                 # the openness over which an opened strike blends to the pedal's rate
OPEN_AFTER = 0.03                # s: the pedal opens a strike up only from this long after it
OPEN_EPS = 0.004                 # the pedal opens a strike up only this far above its openness (half a CC step)
STEP_MAX = 50.0                  # 1/s: the largest log step per second an opened strike takes from its envelope
FLOOR = 1e-6                     # an envelope under this share of the strike's peak (-120 dB) is at its floor
MAX_RING_S = 8.0                 # s: the longest a TIP or EDGE strike rings
IMPULSE_LOOK = 5                 # frames searched for the band's peak
IMPULSE_MAX_DB = 9.0
FOOT = dict(closed_below=0.05, rearm_above=0.15, speed_window_s=0.05, full_speed=10.0, v_min=0.1)
# The stick tick per articulation (the plugin's stick click; see the prompt for its values). TIP OPEN
# TICK (tip_open_tick_db, a Tuning window value, 0 .. 12 dB) raises the TIP's by tip_open_tick_db x
# o_strike dB: the cymbal behind it grows with the opening, the captured click did not.


class HiHat:
    def __init__(self, root='presets/hihat'):
        p = json.load(open(f'{root}/HiHat_A60.json'))
        self.freq = np.array(p['modes']['freq_hz']); self.band = np.array(p['modes']['band'])
        env = json.load(open(f'{root}/data/hihat_envelopes.json'))['envelopes']
        wts = json.load(open(f'{root}/data/hihat_weights.json'))['weights']
        self.knots = {k['id']: k for k in p['knots']}
        self.raw = {k: np.array(v).T for k, v in env.items()}          # NB x frames
        self.w = {k: np.array(v) for k, v in wts.items()}
        self.stick = {k: self._impulsive(e) for k, e in self.raw.items()}
        self.lvl = {k: self._lvl100(e) for k, e in self.raw.items()}
        self.rate_knots = [('edge_closed_v3', 0.0), ('edge_open_25', 0.25), ('edge_open_50', 0.5),
                           ('edge_open_75', 0.75), ('edge_open_100', 1.0)]
        self.rates = np.array([self._band_rate(self.raw[k]) for k, _ in self.rate_knots])

    # ---------------------------------------------------------------- preparation (the bake)
    @staticmethod
    def _impulsive(e):
        out = e.copy()
        for b in range(e.shape[0]):
            x = e[b]
            if x.max() <= 0:
                continue
            p = int(np.argmax(x[:IMPULSE_LOOK])); q = min(len(x) - 1, p + 3)
            slope = (20 * np.log10(x[q] / x[p])) / (q - p) if x[q] > 0 and x[p] > 0 else -1.0
            slope = min(slope, 0.0)
            for k in range(p + 1):
                out[b, k] = x[p] * 10 ** (min(-slope * (p - k), IMPULSE_MAX_DB) / 20)
        return out

    @staticmethod
    def _lvl100(e):
        p = int(np.argmax((e ** 2).sum(0)))
        return 10 * np.log10((e[:, p:p + 10] ** 2).mean(1) + 1e-20)

    @staticmethod
    def _band_rate(e):
        en = e ** 2; p = int(np.argmax(en.sum(0))); r = np.zeros(NB)
        for b in range(NB):
            y = 10 * np.log10(en[b, p:] / max(en[b, p:].max(), 1e-30) + 1e-15); x = np.arange(len(y)) * FR
            m = (x > 0.05) & (x < 1.5) & (y > -60)
            k = np.polyfit(x[m], y[m], 1)[0] if m.sum() > 5 else -20
            r[b] = max(-k, 2.0) / 8.686
        return r

    # ---------------------------------------------------------------- velocity and openness
    def closed_level(self, art, v):
        ids = [f'{art}_closed_v{i}' for i in (1, 2, 3)]; vs = np.array([0.3, 0.6, 1.0])
        ls = np.array([self.lvl[k] for k in ids])
        if v < vs[0]:
            return ls[0] + (v - vs[0]) * (ls[1] - ls[0]) / (vs[1] - vs[0])
        if v > vs[2]:
            return ls[2] + (v - vs[2]) * (ls[2] - ls[1]) / (vs[2] - vs[1])
        return np.array([np.interp(v, vs, ls[:, b]) for b in range(NB)])

    def _pad(self, e, n):
        out = np.full((NB, n), 1e-9); m = min(n, e.shape[1]); out[:, :m] = np.maximum(e[:, :m], 1e-9); return out

    def _closed(self, art, v, n, src):
        ids = [f'{art}_closed_v{i}' for i in (1, 2, 3)]; vs = [0.3, 0.6, 1.0]
        if v <= vs[0]:
            i0 = i1 = 0; a = 0.0
        elif v >= vs[2]:
            i0 = i1 = 2; a = 0.0
        else:
            i0 = 0 if v < vs[1] else 1; i1 = i0 + 1; a = (v - vs[i0]) / (vs[i1] - vs[i0])
        e = np.exp((1 - a) * np.log(self._pad(src[ids[i0]], n)) + a * np.log(self._pad(src[ids[i1]], n)))
        w = (1 - a) * self.w[ids[i0]] + a * self.w[ids[i1]]
        if a == 0.0 and v != vs[i0]:
            e = e * (10 ** ((self.closed_level(art, v) - self.closed_level(art, vs[i0])) / 20))[:, None]
        return e, w

    def envelope(self, art, o, v, n, tip_open_level=0.0):
        """Band envelopes (NB x n frames of 10 ms) and mode weights for a strike.
        art: 'tip' | 'edge' | 'foot'; o: openness 0..1 at the strike; v: velocity 0..1;
        tip_open_level: TIP OPEN LEVEL, 0..1 (the module's docstring)."""
        if art == 'foot':
            return self._closed('edge', v, n, self.raw)
        opens = [k for k in self.knots.values() if k['openness'] > 0]
        opens.sort(key=lambda k: k['openness'])
        xs = [0.0] + [k['openness'] for k in opens]
        def knot(i):
            if i == 0:
                return self._closed(art, v, n, self.stick)
            k = opens[i - 1]
            e = self._pad(self.stick[k['id']], n) * (10 ** ((self.closed_level('edge', v) - self.closed_level('edge', k['velocity'])) / 20))[:, None]
            if art == 'tip':
                keep = 1.0 - tip_open_level * k['openness']
                g = 10 ** (keep * (self.closed_level('tip', v) - self.closed_level('edge', v)) / 20)
                _, wt = self._closed('tip', v, 1, self.stick)
                return e * g[:, None], wt
            return e, self.w[k['id']]
        if o <= 0:
            return knot(0)
        j = min(max(int(np.searchsorted(xs, o)) - 1, 0), len(xs) - 2)
        a = (o - xs[j]) / (xs[j + 1] - xs[j])
        e0, w0 = knot(j); e1, w1 = knot(j + 1)
        return np.exp((1 - a) * np.log(e0) + a * np.log(e1)), (1 - a) * w0 + a * w1

    def rate(self, o, ring_floor=1.0):
        """Each band's decay rate (amplitude, 1/s) at openness o, interpolated in log between the
        rate knots, each at least ring_floor x the median of the 22 there (RING FLOOR, 0..2; TIP and
        EDGE take the strike's; FOOT's choke takes 0, the raw rates)."""
        xs = [x for _, x in self.rate_knots]
        r = np.exp(np.array([np.interp(o, xs, np.log(self.rates[:, b])) for b in range(NB)]))
        return np.maximum(r, ring_floor * np.median(r))

    def choke_rate(self, o_from, o_now, ring_floor=1.0):
        """Extra amplitude decay rate per band (1/s) for a ringing strike closing from o_from (its
        openness at the strike; once opened up, the highest it was opened to) while the pedal is
        at o_now."""
        if o_now >= o_from:
            return np.zeros(NB)
        target = np.maximum(self.rate(o_now, ring_floor), CHOKE_RATE * (o_from - o_now) / max(o_from, 1e-6))
        return np.maximum(target - self.rate(o_from, ring_floor), 0.0)

    def level_step(self, s_env, o_strike, o_now, dt, t_since_strike, opened=False, o_ref=None, at_floor=None,
                   ring_floor=1.0):
        """A ringing TIP or EDGE strike's log level step per band over dt (natural log of amplitude;
        see the module's docstring), whether it has opened up and the highest openness the pedal
        has opened it to. s_env: the captured envelope's own log step per band over dt
        (ln E(t) - ln E(t - dt); past the end of its captured envelope -rate(o_strike) dt; not
        finite where either end is 0). opened, o_ref: as returned by the previous step (False and
        o_strike, or None, at the strike). at_floor: per band, whether either end of the step has
        the envelope under FLOOR of the strike's peak (E < FLOOR x E.max() over the strike's
        envelope; None: none). ring_floor: RING FLOOR at the strike's note-on (rate). Returns
        (step, opened, o_ref)."""
        s_env = np.asarray(s_env, float)
        o_ref = o_strike if o_ref is None else o_ref
        opening = o_now > o_strike + OPEN_EPS and t_since_strike >= OPEN_AFTER
        if not (opened or opening):
            if o_now < o_strike:
                return s_env - self.choke_rate(o_strike, o_now, ring_floor) * dt, False, o_ref
            return s_env, False, o_ref
        m = STEP_MAX * dt
        floor = ~np.isfinite(s_env) if at_floor is None else (np.asarray(at_floor, bool) | ~np.isfinite(s_env))
        s_c = np.where(floor, -self.rate(o_strike, ring_floor) * dt, np.clip(np.where(np.isfinite(s_env), s_env, 0.0), -m, m))
        if opened and o_now < o_ref - OPEN_EPS:
            w_ref = min(1.0, (o_ref - o_strike) / OPEN_BLEND)
            slope = (1.0 - w_ref) * s_c - w_ref * self.rate(o_ref, ring_floor) * dt - self.choke_rate(o_ref, o_now, ring_floor) * dt
            return slope, True, o_ref
        if opening:
            o_ref = max(o_ref, o_now)
            w = min(1.0, (o_now - o_strike) / OPEN_BLEND)
            slope = (1.0 - w) * s_c + w * (-self.rate(o_now, ring_floor) * dt)
            return np.minimum(slope, np.maximum(s_c, 0.0)), True, o_ref
        return s_c, True, o_ref


def foot_velocity(open_history, dt):
    """open_history: the pedal's openness over the last FOOT['speed_window_s'] seconds (oldest first),
    sampled every dt. The closing speed (openness per second) sets the velocity."""
    speed = max(0.0, (open_history[0] - open_history[-1]) / max(dt * (len(open_history) - 1), 1e-6))
    return float(np.clip(speed / FOOT['full_speed'], FOOT['v_min'], 1.0))
