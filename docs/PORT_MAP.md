# Port map: Nova Ride, engine V19 → C++

This document describes how the Python reference engine produces a Nova Ride render. It is meant to guide the C++ port and the Python-vs-C++ comparisons. It describes the engine as it is. Nothing here proposes a behaviour change.

**Sources read:** `prototype/engine/*.py`, `prototype/load_preset.py`, `prototype/render_nova.py`, `prototype/NOVA_BOW_BL_SPLIT.json`, `presets/nova/**`, `docs/history/*`, `docs/BASELINE_MANIFEST.md`. Line numbers refer to `prototype/engine/multi_strike_engine_v19.py` (MD5 `bcd9e8d8af079b415a53d9968952a1bc`) unless another file is named.

**How the claims were checked:** I used a venv with numpy 2.5.4, scipy 1.18.1 and soundfile 0.14.0. Every locked file matches its manifest MD5. The baseline render check reproduces both expected MD5s (`bow_tip` `91ae9a68…`, `bow_shoulder` `b321ddb1…`). Every number marked *measured* comes from `load_engine(..., overrides=...)` renders, or from reading engine attributes after loading. No locked file was changed. Audio from these measurements was written only to a scratch directory, outside the repo.

## Nova facts used throughout

| Item | Value |
|---|---|
| Sample rate | 44100, a module constant (`SR`, line 52). The engine has no other rate. |
| Body modes | 2270. `birth_gate_s = 100` is larger than the largest birth (4.87 s), so every fingerprint mode is a body mode. Frequencies run 81.09 Hz to 18 981.6 Hz in ascending order. |
| Fingerprint | `data/nova_vel_participation.json`. The base velocity participation (VP) file also serves as the fingerprint. No overlay replaces it. Mode `id`s are **not** `0..n-1`. |
| T60 | SPEAR `t60` × `t60_scale` (1.0), capped by `np.interp(f, [200,1000,4000,10000,20000], [12,6,3,1.6,1.0])` (58 modes are capped), then floored at 0.02 s (no mode hits the floor). Range 0.05 to 12 s (lines 136–138). |
| Excitation | `exc_norm = exc / (max(exc) + 1e-9)` (line 140). |
| Contact length | 13 samples at every position. The preset's tip and shoulder tau values are equal, so the position blend has no effect. |
| Calibration scalar | `0.3282202728544299` for no overlay, `bell_tip` and `bell_shoulder`. `0.32834604084352215` for `bow_shoulder`. `0.4794880078673403` for `bow_tip`. |
| Render cost | About 20–27 s of CPU for the 15 s baseline render on this container. |

---

## a. How `render()` turns strikes into audio

### a.1 Top level: `render(strikes, total_dur)` (lines 500–705)

1. `N = int(total_dur * SR)`, then `out = zeros((N, 2))` as float64.
2. `rng = np.random.default_rng(seed)`. A fresh generator is created on every call (seed 7 for Nova).
3. `_schedule_sparkle(total_dur, rng)` does nothing for Nova (no clusters) and makes no draws.
4. Events are sorted by time with a stable sort, so ties keep their input order. Nova uses `articulation = 'stick'`, so each strike becomes one `'full'` event (line 530).
5. For each event, in this order (lines 606–696):
   1. If `ev.time − t_prev > 1e-4`, the gap `[t_prev, ev.time)` is rendered as **one** call to `_render_segment` (lines 590–593, 608–613).
   2. `_apply_strike(ev, rng)` (a.2).
   3. `_add_transient(...)` does nothing for Nova (`stick_gain = 0`, `atk_noise = 0`) and makes no draws.
   4. Onset-ramp bookkeeping (lines 621–650), LF accumulator (lines 652–672), MF accumulator (lines 674–688). See a.3.
   5. `t_prev = ev.time`.
6. The tail `[t_last, total_dur)` is rendered as one segment if it is longer than 1e-4 s (lines 697–702).
7. `out[:,1] = _hp(out[:,1], 120)` applies a 2nd-order Butterworth high-pass at 120 Hz to the **right channel only**. It is `scipy.signal.sosfilt` over the whole channel, starting from zero state (line 703). The single SOS section is `b = [0.9879833041335018, −1.9759666082670035, 0.9879833041335018]`, `a = [1, −1.9758222020741683, 0.9761110144598384]`.
8. `out *= calib_scalar` (line 704).

A **segment** is the time from one strike to the next strike, or to the end of the render. There is no block size. A 10 s tail is a single `_render_segment` call, and several mechanisms are evaluated once per segment (see f).

### a.2 `_apply_strike(ev, rng)` (lines 961–1133)

Here `v = ev.velocity`, `pos = ev.position` and `ef = clip(1 − pos/0.5, 0, 1)` (1 at the edge, 0 at bow and bell).

1. **Bow VP** (lines 963–977). `v` is clipped to [0, 1].
   - If `v ≤ norms[0]`: `curves[:,0] · v/(norms[0] + 1e-9)`.
   - If `v ≥ norms[-1]`: `curves[:,-1]`.
   - Otherwise: linear interpolation between columns `idx` and `idx+1`, where `idx = searchsorted(norms, v) − 1`, clipped to `[0, n−2]`, and the denominator carries `+1e-12`.
2. `stick_profile = 1` (`contact_tau_ms = 0`, lines 981–983).
3. **Position weight** (lines 985–995).
   - If `pos ≤ 0.5`: `edge_weight + (pos/0.5)·(1 − edge_weight)`.
   - Otherwise: `(1 − bf)·1 + bf·bell_weight`, with `bf = clip((pos − 0.5)/0.5, 0, 1)`.
4. **Bell VP blend** for `pos > 0.5` (lines 1001–1015): `vel = (1 − bf)·vel + bf·bell_vel`. The bell VP uses the same interpolation rule as the bow VP.
5. `edge_level_boost = 1.0`, so the boost is skipped (lines 1020–1022).
6. **Coupling** (lines 1033–1035): `exc_norm · vel**1.0 · stick_profile · pos_weight · exp(j·φ)`, with `φ = rng.uniform(0, 2π, 2270)`. This is the only RNG use per strike.
7. `_beo_injection = |coupling|` (line 1041). Nova never reads it.
8. **Contact**: `tip_contact_ms = 0.05 > 0` and `permode_contact = true` select the per-mode branch (lines 1067–1093).
   - `tau0 = 2` and `tauM = 7`, at every position.
   - `tau_i = tauM − (tauM − tau0)·sqrt(f_i/(f_max + 1e-9))` and `σ_i = max(0.28·tau_i, 1)`.
   - `n = int(tauM + 3·max σ) + 1 = 13`.
   - `F[i,k] = exp(−½((k − tau_i)/σ_i)²)`, with each row divided by `(row sum + 1e-9)`.
   - The engine stores `_active_contact = ('permode', F, coupling)`. **Nothing is added to `_state` yet.**
9. `_wander_scale = 1.0` and `_wander_rate_scale = 1.0`, because the edge wander parameters are 0 and `rate_jitter = 0`. The rate scale has a floor of 0.1 (lines 1114–1125).
10. `_wash_damp = max(0.3, 1 + 0.4·(v − 0.787))` (lines 1127–1130). This gives 0.685 at v 0, 0.805 at v 0.3, 0.945 at v 0.65, 1.045 at v 0.9 and 1.085 at v 1.0.
11. There is no HF bloom bank, so nothing more happens (lines 1132–1133).

### a.3 Per-strike bookkeeping in `render()` after `_apply_strike`

- **Onset-ramp target** (lines 621–650).
  - `pk_t = ef·edge_pk + (1 − ef)·bow_pk`, from `mode_pk_t` in `nova_band_envelopes.json`. That array is indexed by **mode position**, not by mode `id`.
  - `_beo_t_strike = ev.time` and `_beo_ef = ef`.
  - If `ef > 1e-3`, each mode's `pk_t` is multiplied by `1 + ef·(1/sqrt(max(frac(v)/frac(1.0), 0.01)) − 1)`. Here `frac` is the per-band fraction from `nova_vel_tilt.json` interpolated at `v`, and the bands are [0, 500, 2000, 8000, 99999) Hz.
- **LF accumulator** (lines 652–672).
  - `dt = ev.time − lf_t_last`, or 0 if `lf_t_last == 0`.
  - `S *= exp(−dt/1.5)`.
  - `lf_scale_now = min(1 + 0.2·S, 1.4)`. This value has no audio effect (b, row 9).
  - `S += v·w`, with `w` = 1.0 if `pos ≤ 0.25`, 0.05 if `pos ≥ 0.75`, otherwise 0.3.
  - `_wander_rate_scale = max(_wander_rate_scale, 1 + 0.2·S)`.
- **MF accumulator** (lines 674–688). It decays the same way, with tau 1.5. `mf_scale_now = max(1 − 0.25·S, 0.4)` is computed **before** this strike is added. Then `S += v·w`, with `w` = 1.0 at edge or bow and 0.1 at bell.

### a.4 `_render_segment(duration, t_abs, rng)` (lines 707–959)

1. `N = int(duration·SR)`. If `N == 0`, the function returns and the pending contact is **not** consumed. `rng` is never used here.
2. **Contact phase** (lines 721–736, 786). This runs only in the first segment after a strike. For `k` in `0 .. min(13, N) − 1`:
   - `state += F[:,k]·coupling`
   - `out[k] = Σ Re(state)·pick`
   - `state *= mode_step`, where `mode_step = exp(j2πf/SR)·exp(−ln1000/(T60·SR))` uses the **undamped** T60 (lines 277–278).

   No wander, ramp, onset gain, MF scale or `wash_damp` is applied in these samples. Afterwards `_active_contact` is set to `None`.
3. **NL redistribution** (lines 788–839). It runs **once per segment**, from the band energies of the state after contact, with `dt = duration`, the length of the whole segment.
4. **Free ring** (lines 841–922). This covers `n_free = N − n_contact` samples. For each mode with `|z| ≥ 1e-9`, with `A = |z|` and `φ0 = arg z`:

   ```
   wave(t) = A · exp(−ln1000 · t / T60') · cos(2πf·t + φ0 + wander(t_abs_arr))
             · ramp · onset_gain · mf_scale          (lf_scale is inert, see b)
   t         = local time from the start of the free phase
   T60'      = T60 / wash_damp     (exactly T60 when wash_damp == 1.0)
   t_abs_arr = t_abs + n_contact/SR + t
   wander(τ) = −(D/lr)·(cos(2π·lr·τ + lp) − cos(lp))
   D         = drift · wander_depth · wander_scale
   lr        = lfo_rate · wander_rate_scale
   ```

   Then `L += pick_L·wave` and `R += pick_R·wave`.
5. **State advance** (lines 924–926): `state *= exp(j2πf·n_free/SR)·exp(−ln1000·(n_free/SR)/T60)`. This uses the **undamped** T60 and no wander.
6. HF bloom: none for Nova.

### a.5 How strike times become sample positions

- A segment is written at output index `int(t_start·SR)` with length `int((t_end − t_start)·SR)` (`_write`, lines 1220–1222). `_write` uses `+=`, so overlapping writes add together.
- A strike's first force sample is written at `int(t·SR)`, and force sample `k` at `int(t·SR) + k`.
- The two floors are taken independently. This produces one-sample holes, rare overlaps, and strikes landing one sample early (f.6).
- The modal state advances by the number of samples actually rendered, which is `int(dur·SR)/SR` per segment. Wander, ramp and onset gains use absolute time, `t_abs + k/SR`.
- A gap of 1e-4 s or less (4 samples or fewer) is not rendered at all. No samples are written, the state is not advanced, and the next strike overwrites the pending contact (f.7).

### a.6 State carried between segments

| State | Set by | Used by | Notes |
|---|---|---|---|
| `_state` (2270 complex values) | Contact, NL scaling, state advance | Every segment | This is the only audio-carrying state. |
| `_active_contact` | Strike | The first segment after the strike with `N ≥ 1` | Lost if the next strike comes first. |
| `_nl_state` | NL, once per segment | NL | Starts at **0.918**, left over from calibration (a.7). |
| `_wash_damp` | The latest strike | The free-ring decay of **every** mode | It also changes the decay of earlier strikes' ring. |
| `_wander_rate_scale` | The latest strike plus the LF block | Wander | `_wander_scale` stays 1.0. |
| `_beo_t_strike`, `_beo_ef`, `_beo_pk_t_blended` | The latest strike | Ramp and onset gains for **every** mode | They are measured from the time since the latest strike. |
| `_lf_S`, `_lf_t_last`, `_mf_S`, `_mf_t_last`, `_mf_scale_now` | Strike bookkeeping | Wander rate, MF scale | |
| RNG position | 2270 doubles per strike | Strike phases | |
| HP filter state | — | The right channel | Filtered once after the loop. This is the same as a streaming biquad that starts at sample 0. |

These are stateless functions of time and are not carried: the wander phase (a function of absolute render time), and the ramp and onset-gain envelopes (functions of the time since the latest strike).

`render()` never calls `reset()`. A second `render()` on the same instance starts from the first render's ringing state, accumulators and `nl_state`. *Measured:* two identical `render()` calls on one instance differ by up to 0.386.

### a.7 Calibration: `_compute_calib_scalar` (lines 450–476), run at construction

1. `reset()`, then `rng = default_rng(42)`.
2. `_apply_strike` for an edge strike at v 1.0, t 0.05.
3. `_nl_ref_e = Σ|state|²`. This evaluates to **1e-30** because the contact has not been applied yet (f.1).
4. `_render_segment(0.25, t_abs = 0.05)`.
5. `peak = max(|L|, |R|)`, taken before the HP filter. `scalar = 0.89/peak`.

Afterwards it restores `_state`, the HF state and queue, the sparkle events and the two wander scales. It does **not** restore `_nl_state` (left at 0.918), `_wash_damp` (left at 1.0852), `_beo_injection` or `_nl_last_pos`.

The calibration render includes contact, wander (at absolute time 0.05 s), `wash_damp`, and NL with `dt = 0.25 s`. It has no onset ramp, no onset gains and no LF or MF scaling, because the strike is applied outside `render()` and `_beo_t_strike` is `None`. Because of this, the scalar changes with any override that touches contact, wander, `vel_len_tilt` or NL. The C++ side must compute it the same way, under the same overrides.

---

## b. Sound-producing mechanisms

"Off" means the override makes the mechanism a no-op in the Python output, by reading the code (exactly `×1.0` or `+0.0`). The calibration scalar changes along with it (a.7).

### Active for Nova

| # | Mechanism | Code | Reads | Nova | Turn off with `overrides` |
|---|---|---|---|---|---|
| 1 | Body modal bank: analytic free ring and state advance | 132–140, 277–278, 841–926 | Fingerprint `f`, `t60`, `exc`, `drift`, `birth`; `birth_gate_s`, `t60_scale`; T60 cap table | Always | None. This is the instrument. |
| 2 | Bow velocity participation | 159–175, 963–977 | `data_files.vel_participation` (`vel_norms`, per-id `vel_curve`); `vel_gain_power` (default 1.0) | Always. `hertz_vel_scale` is used only when there is no VP. | None. It comes from a data file, and `overrides` only patches parameters. |
| 3 | Bell VP blend | 177–194, 1001–1015 | `data_files.bell_vel_participation` | `pos > 0.5` | None. Bypassed when `pos ≤ 0.5`. |
| 4 | Position weight: edge ratios with band fallbacks, and bell ratios | 213–250, 985–995 | `edge_ratios`, `bell_ratios`; `_EDGE_BAND_FALLBACK` | Always | None. The weight is exactly 1 at `pos = 0.5`. |
| 5 | Random strike phases and stereo pick vectors | 205–207, 1033–1035 | `seed` | Always | None. |
| 6 | Per-mode Gaussian contact | 1067–1093, 721–736 | `tip_contact_ms`, `permode_contact`, `tip_tau0`, `tip_tau_max`, `shld_tau0`, `shld_tau_max`, `sigma_ratio` | Always (13 samples) | `tip_contact_ms: 0.0` gives instantaneous injection (lines 1063–1066). |
| 7 | LFO wander (phase modulation) | 209–211, 864–867 | `wander_depth`, `wander_rate_hz`, `drift`; `edge_wander_depth`, `edge_wander_rate`, `rate_jitter` (all 0) | Always | `wander_depth: 0.0` |
| 8 | LF accumulator → wander-rate boost | 346–355, 652–672 | `lf_sens_depth`, `lf_sens_tau`, `lf_edge_weight`, `lf_bow_weight`, `lf_bell_weight`, `lf_max_scale`, `lf_min_scale`, `lf_wander_rate_boost` | From the first strike. Python sets the rate only at a strike; the plugin's follows the decaying accumulator (live-engine decision 6). | `lf_wander_rate_boost: 0.0`. `lf_sens_depth: 0.0` also works and skips the whole block. |
| 9 | LF amplitude scaling | 391–397, 914–916 | `lf_cutoff_hz`, `lf_sens_depth`, `lf_max_scale` | **Inert in Python.** `lf_max_scale = 1.4` selects boost mode, but the scale is applied only when it is below 1. The plugin applies it in both modes, from the 2nd strike (live-engine decision 6). | Already off in Python. In the plugin, `lf_sens_depth: 0.0`. |
| 10 | MF accumulator suppression | 357–366, 399–405, 674–688, 917–919 | `mf_sens_depth`, `mf_sens_tau`, `mf_min_scale`, `mf_max_scale`, `mf_*_weight`; `mf_lo_hz`, `mf_hi_hz` (defaults 500 and 4000) | From the 2nd strike (the scale is 1.0 on the first). Python holds the scale until the next strike; the plugin's releases continuously and is smoothed (live-engine decision 6). | `mf_sens_depth: 0.0` |
| 11 | Velocity-dependent T60 (`wash_damp`) | 1127–1130, 861–863 | `vel_len_tilt`, `vel_len_tilt_ref` | Always | `vel_len_tilt: 0.0` (gives exactly `wash_damp = 1.0`) |
| 12 | Per-mode onset ramp | 281–302, 621–634, 847–856, 895–901 | `band_env_onset_path` (`mode_pk_t`), `band_env_birth_frac` | Only strikes with `ef > 0.1` (`pos < 0.45`), so the Nova edge | `band_env_birth_frac: 1.0` makes the ramp exactly 1. The path cannot be overridden to `None` (f.19). |
| 13 | Velocity tilt of `pk_t` | 368–390, 639–650 | `vel_tilt_path` | Computed when `ef > 1e-3`, but audible only through #12 | No override. It is inert when #12 is off, and exactly an identity at `v = 1.0`. |
| 14 | Nonlinear modal redistribution (NL) | 407–423, 466, 788–839 | `nl_amount`, plus defaults: `nl_threshold` 0.55, `nl_attack_ms` 100, `nl_release_ms` 150, `nl_k_lf_mf` 0.02, `nl_k_mf_hf` 0.04, `nl_k_lf_hf` 0.01, `nl_lf_hz` 500, `nl_hf_hz` 4000 | Always (`nl_amount = 1.0`) | `nl_amount: 0.0` |
| 15 | Onset band gains | 425–442, 902–913 | `bow_onset_band_gains`, `bow_onset_tau_ms` | Overlays `bow_tip`, `bell_tip`, `bell_shoulder`, at positions with `ef < 0.9` (`pos > 0.05`). `bow_shoulder` is all 1.0, so it is bypassed. Absent with no overlay. | `bow_onset_band_gains: None` |
| 16 | Calibration scalar | 444–476, 704 | Everything in a.7 | Always | None. Compare `out / eng._calib_scalar` if needed. |
| 17 | 120 Hz high-pass, right channel only | 703 | — | Always | None. The left channel is the unfiltered signal. |

### Present in the engine but inactive for Nova

| Mechanism | Code | Why it is inactive |
|---|---|---|
| Hammering tilt | 141–154 | `hammering_tilt = 0.0`. It would be applied twice if it were above 0 (f.18). |
| Contact sinc stick profile | 981–983 | `contact_tau_ms = 0.0` |
| Edge level boost | 1020–1022 | `edge_level_boost = 1.0`, and the gate is `> 1.0` |
| Gaussian contact (`'gauss'`) and release pulse | 1095–1112, 761–784 | `permode_contact = true` takes precedence. `release_gain` defaults to 0. |
| Hertz mass-spring contact | 738–759 | Unreachable. No code ever creates a `'hertz'` contact. |
| HF bloom bank | 252–274, 928–957, 1135–1149 | No `hf_bloom` data file; `hf_bloom_gain = 0` |
| Sparkle | 124–128, 1158–1189 | No `clusters` file; `sparkle_depth = 0` |
| Stick-click transient | 1194–1211 | `stick_gain = 0`. The base preset and all four overlays set it. |
| Attack noise burst | 1212–1217 | `atk_noise = 0` |
| Brush expansion | 508–528 | `articulation = 'stick'` |
| Edge band envelope (Bettis) | 304–327, 535–604, 690–694 | No `edge_band_env_path`. `_eb_active` is never set to anything but `None`, so `_render_gap_eb` is unreachable. |
| NPZ edge `velw` | 329–345, 1043–1061, 869–894 | No `edge_npz_path` |
| Velocity-bracketed edge `pk_t` | 289–296 | The Nova file uses `mode_pk_t` |
| Edge wander depth and rate, rate jitter | 1114–1124 | All 0. `rate_jitter > 0` would add 2 RNG draws per strike. |

Preset keys the engine never reads: `alpha_contact`, `stick_hp`, `t60_split_ms`. Defaults the engine never reads: `hertz_contact`, `m_stick`, `k_stiff`, `feedback_scale`, `v_scale`.

---

## c. Random-number generators in a Nova render

### Algorithm, verified against numpy 2.5.4

- `default_rng(s)` is PCG64 seeded through `SeedSequence(s)`. The C++ side can skip SeedSequence and start from the initial 128-bit `(state, inc)` that numpy produces:

  | Seed | Used for | `state` | `inc` |
  |---|---|---|---|
  | 7 | `rng0` and the render RNG | `0x9d0aecc4e7b188919ae2415a6b292038` | `0xc4751896ce8acebf66887f5ac070d959` |
  | 42 | Calibration | `0xcea44f6798798f2aacbc7c9d68860ac8` | `0xfa505436c9a8416e66caf2e28d25abff` |
  | 7 + 0xA2A2A2 = 10658473 | Wander | `0x7a3121f2d49679c13e18683063d45998` | `0x5b783832288ac2e0ba3700790ddf4375` |

- Each raw 64-bit output does two things:
  1. `state = state·0x2360ED051FC65DA44385DF649FCCF645 + inc (mod 2^128)`.
  2. Output `rotr64(hi64(state) ^ lo64(state), state >> 122)`.

  A pure-Python version of this matched numpy's `random_raw()` for 5000 outputs, for seeds 7 and 42.
- `uniform(lo, hi, n)`: `lo + (hi − lo)·u`, with `u = (raw >> 11)·2^−53` and one raw output per value. Compute `hi − lo` in double precision first. For example, `1.3 − 0.7 = 0.6000000000000001`.
- `choice([-1., 1.], n)`: one raw output gives two 32-bit halves, low half first. The index is `half >> 31`. So `n` values use `n/2` raw outputs (2270 is even, so no half is left over).
- With this, `_pick_L`, `_pick_R`, `_lfo_rates`, `_lfo_phases` and the strike phases were reproduced **bit-exactly** from the raw streams.

### Streams and call order

| Stream | Created | Calls, in order | Raw outputs |
|---|---|---|---|
| `rng0 = default_rng(7)` | `__init__`, line 205 | `uniform(0.6, 1.0, 2270)` × `choice([-1,1], 2270)` → `_pick_L`, then the same pair → `_pick_R` (lines 206–207). Python evaluates the left operand first, so uniform comes before choice. | 2270 + 1135 + 2270 + 1135 = 6810 |
| `wander_rng = default_rng(7 + 0xA2A2A2)` | `__init__`, line 209 | `uniform(0.7, 1.3, 2270)·1.5` → `_lfo_rates`, then `uniform(0, 2π, 2270)` → `_lfo_phases` | 4540 |
| `ref_rng = default_rng(42)` | Calibration, line 462 | One `uniform(0, 2π, 2270)` for the calibration strike. `_render_segment` ignores its `rng` argument. | 2270, once per engine instance |
| `rng = default_rng(7)` | Each `render()`, line 504 | Sparkle scheduling makes no draws. Each strike, in time-sorted order, makes one `uniform(0, 2π, 2270)` (line 1035) and nothing else: bloom, transients, NPZ and rate jitter are all off. | **2270 per strike**. Strike `k` uses raw outputs `[2270k, 2270(k+1))`. |
| `hf_rng`, `hf_w` (seeds + 0xC0C0C0, + 0xD1D1D1) | — | Not created for Nova, because there is no HF bloom data. | 0 |
| `rng_b` (brush) | — | Not created, because the articulation is `'stick'`. | 0 |

The render RNG and `rng0` share seed 7. Because of that, the first strike's phases are `2π·u_i`, where `|pick_L_i| = 0.6 + 0.4·u_i` uses the same `u_i`. *Measured:* the correlation is exactly 1.0 (f.11).

---

## d. What the overlays change

`load_preset.load_engine` merges the overlay's `parameters` into the preset's `parameters`, then merges the overlay's `data_files` into the preset's `data_files` (lines 225–232 of `prototype/load_preset.py`).

### Literal differences from no overlay

| Overlay | `data_files` change | Parameter changes (key absent in the base preset) |
|---|---|---|
| none ("edge") | — | `bow_onset_band_gains` absent, so onset gains are off. `bow_onset_tau` is the unused default, 0.08. |
| `bow_tip` | `vel_participation` → `nova_vel_participation_tip_s2a_ceil.json` | `bow_onset_band_gains = [1.20, 1.20, 1.03, 0.95, 0.91, 0.90, 0.90, 1.08, 1.20]`, `bow_onset_tau_ms = 80` |
| `bow_shoulder` | `vel_participation` → `nova_vel_participation_shoulder_correct.json` (the plugin's bake plays its V4 column at every velocity: live-engine decision 17) | `bow_onset_band_gains` all 1.0, so it is bypassed (line 433). `bow_onset_tau_ms = 80`, which equals the default and is unused. |
| `bell_tip` | `bell_vel_participation` → `nova_bell_vp_tip_corrected.json` | `bow_onset_band_gains = [1, 1, 1, 1, 1, 0.45, 0.75, 1.85, 0.85]`, `bow_onset_tau_ms = 120` |
| `bell_shoulder` | `bell_vel_participation` → `nova_bell_vp_shoulder_v2_corrected.json` | `bow_onset_band_gains = [1, 1, 1, 1, 1, 2.40, 1.35, 0.52, 1.20]`, `bow_onset_tau_ms = 300` |

Every overlay also sets `stick_gain = 0.0`, which is the same as the base value.

### Confirmation that everything else is identical

I loaded all five engines and compared every instance attribute against no overlay, plus all SpearLayer arrays (`freqs`, `t60s`, `excs`, `drifts`, `births`, `lives`, `ids`). *Measured:*

| Overlay | Attributes that differ | Nothing else differs |
|---|---|---|
| `bow_tip` | `_vel_curves` (1651 of 2270 rows), `_vel_norms` (`[0.2608, 0.5191, 0.6841, 0.9905, 1.0]` → `[0.3812, 0.7588, 1, 1, 1]`), `_bow_onset_band_gains`, `_bow_onset_mode_band`, `_params`, `_calib_scalar` (→ 0.47949, +3.29 dB), `_beo_injection` (calibration leftover, unused) | Yes |
| `bow_shoulder` | `_vel_curves` (1887 rows), `_vel_norms` (→ `[0.9905, 0.9905, 1, 1, 1]`), `_params`, `_calib_scalar` (→ 0.328346, +0.0033 dB), `_beo_injection` | Yes |
| `bell_tip` | `_bell_vel_curves` (2264 rows), `_bell_vel_norms` (`[0.35, 0.9]` → `[1.0, 1.0]`), `_bow_onset_band_gains`, `_bow_onset_mode_band`, `_bow_onset_tau` (0.12), `_params` | Yes. The calibration scalar is identical. |
| `bell_shoulder` | Same set as `bell_tip`, with its own values (`_bow_onset_tau` 0.30) | Yes. The calibration scalar is identical. |

All six VP files have 2270 modes in fingerprint order, no missing ids, no null `vel_curve` and no short curves. The bow-VP default of "all ones for a missing id" (line 170) and the bell-VP default of "all zeros" (line 189) are therefore never used.

### What that means for each strike position

*Measured:* single strike at v 0.65, 0.6 s render, compared with no overlay after dividing by each calibration scalar.

| Strike position | `bow_tip` | `bow_shoulder` | `bell_tip` | `bell_shoulder` |
|---|---|---|---|---|
| Edge (0.0) | Differs: bow VP, calibration | Differs: bow VP, calibration | **Bit-identical** | **Bit-identical** |
| Bow (0.5) | Differs: VP, onset gains, calibration | Differs: VP, calibration | **Differs: onset gains** | **Differs: onset gains** |
| Bell (1.0) | Differs: onset gains, calibration | Same up to the calibration scalar | Differs: bell VP, onset gains | Differs: bell VP, onset gains |

The bow VP governs every position up to 0.5, edge included. At `pos = 1.0` its weight is exactly `1 − bf = 0`. The onset gains are gated only by `ef < 0.9`, so the bell overlays' gains also act on bow strikes, and `bow_tip`'s gains act on bell strikes (f.15). Between 0.5 and 1.0 the two VPs blend linearly.

---

## e. Proposed porting order

In each stage, Python renders with the override set shown, and the C++ render must match it. Each stage switches on one more mechanism, so a mismatch points at the mechanism just added.

Compare the float64 output of `render()` before PCM quantisation. Start with a tolerance such as `max|diff| ≤ 1e-8`; the PCM24 step is 1.19e-7. Python itself only agrees with itself to about 3e-9: splitting a segment shifts its output by up to 2.5e-9 (*measured*, see below). Bit-exact output is not realistic, because numpy's vectorised `cos` and `exp` and its summation order will differ from C++. If the C++ uses a recursive oscillator instead of the per-segment analytic cosine, its phase error grows with segment length. The long-tail cases in stage 2 measure that.

```python
ALL_OFF = {
    'tip_contact_ms':       0.0,   # contact → instantaneous injection
    'wander_depth':         0.0,
    'lf_wander_rate_boost': 0.0,
    'vel_len_tilt':         0.0,
    'mf_sens_depth':        0.0,
    'band_env_birth_frac':  1.0,   # onset ramp ≡ 1
    'bow_onset_band_gains': None,  # overlay onset gains off
    'nl_amount':            0.0,
}
```

With `ALL_OFF`, the engine is linear and time-invariant. *Measured:* the same strike at 0.2 s and at 1.2 s gives bit-identical output once aligned. Splitting a segment with a zero-velocity strike changes the output only by rounding: `max|diff|` ≤ 2.5e-9, which is −149 to −178 dB, at edge, bow and bell, with no overlay, `bow_tip` and `bell_shoulder`.

| Stage | Adds | Overrides (remove keys from `ALL_OFF` cumulatively) | What to compare and which cases to cover |
|---|---|---|---|
| 0 | Data loading, static arrays, PCG64 | — (no audio) | Bit-exact against engine attributes: `_bF`, `_bT`, `_bD`, `_exc_norm`, `_vel_curves`, `_vel_norms`, `_bell_vel_curves`, `_bell_vel_norms`, `_edge_weight`, `_bell_weight`, `_pick_L`, `_pick_R`, `_lfo_rates`, `_lfo_phases`, `_beo_pk_t`, `_vel_tilt`, `_bow_onset_mode_band`, `_lf_sigmoid`, `_mf_bandpass`, the NL masks. Check every overlay. |
| 1 | Strike coupling (#2–#5) | `ALL_OFF` | After `eng.reset(); eng._apply_strike(SE(t,v,pos), np.random.default_rng(7))`, `eng._state` equals the coupling. Grid: pos {0, 0.1, 0.25, 0.45, 0.5, 0.75, 1.0} × v {0, 0.1, 0.2608, 0.3, 0.65, 0.9, 0.9905, 1.0}, plus every VP branch, for every overlay. |
| 2 | Modal free ring, segment scheduler, sample rounding, calibration, right-channel HP (#1, #16, #17) | `ALL_OFF` | Single strikes at each position; strike pairs off the sample grid (holes, f.6); gaps under 1e-4 s; tails of 10 s and longer. |
| 3 | Per-mode contact (#6) | − `tip_contact_ms` | Strikes under 13 samples apart and under 1e-4 s apart (f.7). |
| 4 | Wander (#7) | − `wander_depth` | The same strike at different absolute times. Multi-strike runs, where older ring has no wander during each contact phase (f.4). |
| 5 | LF accumulator → wander rate (#8) | − `lf_wander_rate_boost` | Multi-strike runs at mixed positions, including a strike at t = 0.0 (f.8). |
| 6 | `vel_len_tilt` (#11) | − `vel_len_tilt` | Multi-strike runs at different velocities and gap lengths, to cover the boundary steps in f.2. |
| 7 | MF accumulator (#10) | − `mf_sens_depth` | Two or more strikes at short spacing. |
| 8 | Edge onset ramp and velocity tilt (#12, #13) | − `band_env_birth_frac` | Positions below 0.45: first at v = 1.0, where the tilt is exactly an identity, then at other velocities. Include a second strike during a ramp (f.3). |
| 9 | Onset gains (#15) | − `bow_onset_band_gains` | Overlays `bow_tip`, `bell_tip`, `bell_shoulder`, at bow and bell positions. |
| 10 | NL (#14), which is the full preset | − `nl_amount` (`overrides=None`) | The harness must know each segment's length before rendering it (f.1). Finish with the baseline-check strikes and `render_nova.py`'s 47 s, 9-strike run for every overlay. |

Why this order:
- Contact comes before the output modifiers, because `n_contact` moves the free-phase start that wander, the ramp and the onset gains all use.
- The strike-level accumulators (5–7) come before the time-since-strike envelopes (8–9). They are simpler, and they can be checked at positions where 8–9 are inactive.
- NL comes last, because it is the only mechanism that needs information from the future (f.1). It also needs a decision about live playing before it can go into the real-time path. As a per-segment state transform, it can be added without touching the earlier stages.

---

## f. Dependencies on render length and segment boundaries, and things that look wrong

Reported only. Nothing here has been fixed.

**f.1 NL depends on segment length, which makes it non-causal and dependent on render length.** NL runs once per segment, at the segment's start, with `dt = duration` (lines 792, 807, 815–817): `T_LM = LF·ns·0.02·dt`, `T_LH = LF·ns·0.01·dt`, `T_MH = MF·ns·0.04·dt`.
- How much energy moves after a strike depends on the time until the **next** strike, or until the end of the render for the last strike. A live plugin cannot know this.
- `_nl_ref_e` is meant to be "post-strike state" energy. With per-mode contact it is taken before the contact is applied, so it evaluates to the 1e-30 floor (line 466). The normalised excitation is therefore always 1, the drive is always 1, and `nl_threshold` has no effect.
- `nl_state` starts at 0.918, left over from calibration (a.7).
- *Measured:* the first 1.0 s of a bow v 0.9 strike differs between a 1 s and a 2 s render at −31 dB re signal. It is bit-identical with `nl_amount = 0`.
- *Measured:* band energy right after a bow v 0.9 strike, relative to `nl_amount = 0`, by length of the following segment:

  | Segment | LF | MF | HF |
  |---|---|---|---|
  | 0.5 s | ×0.985 | ×0.980 | ×1.118 |
  | 4 s | ×0.880 | ×0.840 | ×1.95 |
  | 10 s | ×0.700 | ×0.600 | ×3.38 |
  | 26 s | ×0.220 | ×0 | ×6.96 |
  | 40 s | ×0 | ×0 | ×6.96 |

  A strike followed by about 25 s of silence loses its MF modes from the onset. After about 33 s it also loses its LF modes. In the locked baselines, segments are 4–5 s long and `render_nova.py`'s tail is 10 s. `SESSION_SUMMARY_V8` estimates the transfer "per 20 ms segment", but segments are not 20 ms.

**f.2 `wash_damp` decay does not match the state advance.** Within a segment, the output decays with `T60/wash_damp` (line 861). The state is advanced with plain `T60` (line 926). At the next strike every mode jumps by a factor of `10^(3·gap·(wash_damp − 1)/T60)`, and the new strike's `wash_damp` then re-times the decay of all older ring.
- Example: v 0.3, a 5 s gap and a 3 s T60 give ×0.106 (−19.5 dB). A 12 s T60 gives ×0.57.
- *Measured:* only `vel_len_tilt` on, broadband 10 ms RMS step at a zero-velocity strike 5 s later: −8.4 dB after a v 0.3 strike, +1.9 dB after a v 0.9 strike.

**f.3 Output-only modifiers act on all ringing modes and follow the latest strike.** The ramp, onset gains, MF scale and `wash_damp` scale the output, not the state, for every mode, including ring from earlier strikes.
- A new edge strike pulls the older ring back down to 40 % (`birth_frac`).
- A new bow strike re-applies the overlay onset gains to the older ring.
- A bow strike after an edge strike cancels what remains of the edge ramp.
- *Measured* (difference over the following second, caused by a zero-velocity strike 0.8 s later):

  | Situation | Difference |
  |---|---|
  | Edge v 0.65, then a zero strike at bow | −21.8 dB |
  | Edge v 0.65, then a zero strike at edge | −11.1 dB |
  | Bow v 0.9 with `bow_tip`, only onset gains on | −32.3 dB |
  | Bow v 0.9, MF only | −19.5 dB |
  | Bow v 0.9, NL only | −20.7 dB |
  | Bow v 0.9, `vel_len_tilt` only | −11.9 dB |
  | Bow v 0.9, all defaults | −14.3 dB |

**f.4 Discontinuity between the contact phase and the free phase.** During the 13 contact samples the output is `Re(state)·pick`, with no wander, ramp, onset gain, MF scale or `wash_damp` (line 733). At sample 13 all of them switch on. *Plugin:* the contact phase applies the cymbal-wide LF/MF gain and wander phase (live-engine decision 6); the strikes' own ramps, onset gains and `wash_damp` still pause there.
- *Measured:* for an edge strike at v 1.0, the ramp-on/ramp-off ratio is 1.00 for samples 0–10 and 0.40 from sample 15 on. That is an 8 dB step about 0.3 ms after the onset.
- Every older ringing mode loses its wander for those 13 samples. That gives a phase step at both ends of every contact.

**f.5 Wander uses absolute time, and its rate changes at every strike.** `wander(t)` is evaluated at absolute render time (line 867).
- `lf_wander_rate_boost` changes `lr` at every strike (line 670), so every ringing mode's phase jumps.
- *Measured:* the same strike at 0.2 s and at 1.2 s differs by −27.8 dB with wander on. It is bit-identical with wander off.
- A plugin needs a definition of "absolute time".
- *Plugin:* the time origin is plugin load (live-engine decision 2), and the wander phase is integrated over time, so rate and depth changes never make it jump (live-engine decision 6).

**f.6 Sample rounding leaves holes.** The write position `int(t_start·SR)` and the length `int(dur·SR)` are floored separately (a.5).
- *Measured:* on a 10 ms grid, 605 of 999 consecutive boundaries leave a one-sample hole. The hole is an exact 0.0 sample: the left channel at index 1322 for strikes at 0.02 s and 0.03 s.
- Over 550 165 arbitrary 1 ms-grid pairs, 47.8 % leave a hole and 64 overlap (that sample is summed twice).
- A strike at `t = n/SR` lands on sample `n − 1` for 29 of 401 sampled `n`.
- The locked renders use a 0.5 s grid and never hit this.

**f.7 Closely spaced strikes.**
- A gap of 1e-4 s (4 samples) or less is not rendered. The earlier strike's pending contact is overwritten, so that strike is lost entirely, and the state is not advanced over the gap. *Measured:* a v 0.9 strike followed 50 µs later by a zero-velocity strike outputs exactly 0.0.
- Strikes less than 13 samples apart cut the force short. *Measured:* 8 samples apart keeps 60 % of the energy.

**f.8 Strike at t = 0.0.** `dt = ... if lf_t_last > 0 else 0.0` (lines 654 and 676) treats `t_last = 0.0` as "no previous strike". The strike after a strike at exactly 0.0 therefore gets no accumulator decay. *Measured:* two edge v 0.9 strikes 0.5 s apart give `lf_S = mf_S = 1.8` when the first is at 0.0, and 1.545 when the pair is shifted by 1 ms. *Plugin:* each accumulator has an explicit "there was a strike" flag, so a strike at 0.0 decays like any other (live-engine decision 6).

**f.9 The LF accumulator is only half inert.** The amplitude scaling never applies (b, row 9), as `SESSION_SUMMARY_V8` already says. The accumulator still changes the wander rate through `lf_wander_rate_boost = 0.2`, which that summary does not mention. *Plugin:* the LF scale applies in both modes (live-engine decision 6), so Nova (`lf_max_scale` 1.4) and Bettis (1.7) now get the LF boost.

**f.10 The high-pass filter is on the right channel only** (line 703). The left channel keeps everything below 120 Hz. The lowest mode is at 81 Hz.

**f.11 The strike phases reuse `rng0`'s seed.** The render RNG and `rng0` are both `default_rng(seed)`, so the first strike's phases are perfectly correlated (r = 1.0) with `|pick_L|`. Each `render()` also restarts the phase sequence. A plugin has to decide when the stream restarts.

**f.12 State leaks out of calibration and across renders.** `render()` never resets the engine (a.6). Calibration leaves `nl_state = 0.918` and `wash_damp = 1.0852` behind (a.7).

**f.13 Calibration does not match what it claims.** The docstring says the edge-forte reference means "nothing clips". The calibration strike gets neither the edge ramp nor the onset gains, and its NL runs for only 0.25 s. The `bow_shoulder` baseline peaks at 1.28, as the manifest already notes.

**f.14 The "neutral" edge ratios are not neutral.** `nova_edge_excitation_ratios_neutral.json` lists 1090 modes, all with ratio 1.0. The other modes (and those below 500 Hz) fall back to Level-4 band values (lines 229–239). At the edge, 923 of 2270 modes are weighted:

| Band | Modes | Weight |
|---|---|---|
| 500 Hz–1 kHz | 108 | ×1.742 |
| 1–4 kHz | 496 | ×2.679 |
| 4–10 kHz | 319 | ×1.036 |

The weights fade out linearly towards `pos = 0.5`. Calibration uses them too.

**f.15 Overlays affect other positions.** The onset gains are gated only by `ef < 0.9`, so `bell_tip` and `bell_shoulder` change bow strikes, and `bow_tip` changes bell strikes (d). `SESSION_SUMMARY_NOVA_TIPSHLD` says position blending was removed, then later says "`bow_frac` position blending ensures natural crossfade".

**f.16 The velocity-tilt comment contradicts the data.** The comment at lines 637–638 says LF `pk_t` shrinks at soft velocities. With Nova's data the per-band `pk_t` factors are LF ×1.73, 500 Hz–2 kHz ×1.32, 2–8 kHz ×1.27 and above 8 kHz ×0.76 at v 0.3. At v 0.65 they are ×1.30, ×1.12, ×1.02 and ×0.91. So LF builds slower and very high frequencies build faster.

**f.17 Mode indexing is mixed.** VP curves and ratio files are matched by mode `id`. `mode_pk_t` is matched by array position (line 897). This is consistent today, because all files are in fingerprint order. Any `birth_gate_s` below 4.87 s would make `n_body < 2270`, and the `pk_t` values would silently land on the wrong modes.

**f.18 Dead code.**
- The Hertz contact branch is unreachable, and its parameters are never read.
- `_render_gap_eb` and `_eb_seg_scale` are unreachable (`_eb_active` is always `None`).
- Hammering tilt would be applied twice if it were above 0 (lines 144–147 and 151–154).
- `alpha_contact`, `stick_hp` and `t60_split_ms` in the preset are never read.
- The `bow`/`edge` `envelopes` and `meta` blocks in `nova_band_envelopes.json` are never read; only `mode_pk_t` is.

**f.19 Two mechanisms cannot be switched off with overrides.** `band_env_onset_path: None` and `vel_tilt_path: None` raise `TypeError` in `load_preset` (`os.path.join(base, None)`). That is why b uses `band_env_birth_frac: 1.0` instead.

**f.20 Velocity edge cases.** A velocity-0 strike adds no energy, but it still sets `wash_damp = 0.685`, updates the accumulators, replaces the ramp and onset state, and splits the segment. The VP clips velocity to [0, 1]; `wash_damp` and the accumulators use the raw value.

### Decisions needed from Kris before the real-time path

1. NL (f.1): how the plugin should get `dt` when the next strike is not yet known. The offline comparison harness can reproduce it exactly.
2. The time origin for wander (f.5): plugin start, transport, or something else.
3. When the strike-phase RNG stream restarts (f.11).
4. Whether the real-time path should reproduce the one-sample holes and the dropped or truncated close strikes (f.6, f.7). Reproducing them needs up to 4 samples of look-ahead.
5. Overlays: Python gives each overlay its own engine, with its own modal state and calibration scalar. Should the plugin keep one engine per overlay, or share the modal state between them?

---

## Live-engine decisions

Kris decided the five questions above as follows. These decisions override anything earlier in this document that conflicts with them.

1. **NL is off in the plugin** (`nl_amount = 0`). The plugin's reference sound is Python with the overrides `{'nl_amount': 0.0}`. Stage 10 of section e is therefore not ported, and f.1 does not apply to the plugin.
2. **The engine clock and the strike-phase random sequence start when the plugin loads.** The clock is the time base for wander (f.5). Neither the clock nor the sequence restarts on playback. Loading another preset starts the sequence again from that preset's seed; the clock carries on ("602 and Bettis", decision 6). Python's `render()` creates a fresh RNG and starts at t = 0 on every call. A comparison therefore treats each Python render as one plugin load: a freshly loaded engine renders the same sequence from sample 0.
3. **Every strike plays, at its exact sample.** Python's dropped close strikes and one-sample holes (f.6, f.7) are not reproduced. Comparison sequences use only strike times where Python has neither:
   - every gap is longer than 1e-4 s;
   - `int(a·SR) + int((b − a)·SR) == int(b·SR)` at every segment boundary;
   - every strike time is a whole number of samples.
4. **One shared cymbal.** All five slots ring the same modal state. Each slot's overlay data is applied to that slot's own strikes. The overlay data is the velocity tables and the onset gains, plus the slot's calibration level. For a sequence that uses only one slot, this matches Python, which gives each overlay its own engine. Python has no reference for sequences that mix slots.
5. **Slots** (Nova Ride; the 602 and Bettis slots are in "602 and Bettis" below):

| Slot | Overlay | Position |
|---|---|---|
| BELL | `bell_shoulder` | 1.0 |
| BOW | `bow_tip` | 0.5 |
| EDGE | `edge`: a copy of `bow_shoulder` (decision 13; no overlay until 2026-10-17) | 0.0 |
| BELL2 | `bell_tip` | 1.0 |
| BOW2 | `bow_shoulder` | 0.5 |

6. **LF and MF build-up follow their accumulators continuously, and wander is phase-continuous** (2026-10-16). A deliberate sound change for Nova and Bettis: LF now builds on repeated hits and MF releases between hits. SMOKE FLAT RIDE has both accumulators off and no wander, so it is unchanged. `FastEngine` only; `Engine`, the Python-matching path, is unchanged and refuses LF suppress mode, which it never implemented.
   - **The LF scale applies in both modes** (b, row 9; f.9). Python applies it only when it is below 1 (line 914), so boost mode never scaled anything. Per mode: `scale = lf + (1 − lf) · sigmoid((f − lf_cutoff_hz) / 200)`, as Python defines it. It is cymbal-wide, like the MF scale.
   - **The accumulators decay continuously:** `S(t) = S · exp(−Δt / tau)`. Python decays S and recomputes the scale only at a strike, so the scale held until the next hit and then jumped on the whole ringing cymbal (the likely cause of Nova's "sharp dry click at hit 7–8").
   - **The scales are evaluated at every control block and smoothed.** The targets are boost `min(1 + S·depth, max)` or suppress `max(1 − S·depth, min)`, as in Python. Changes go through a 20 ms one-pole, and the gains are interpolated across each block, so nothing steps.
   - **As in Python, a strike's own contribution to S is added after the scale for that strike is taken.** Until the next strike, the scale follows the decaying accumulator without that strike's contribution, which enters (smoothed) at the next strike. A single hit therefore sounds as before; the scale is 1.0 on the first. The wander-rate boost follows all of S, including the latest strike, as line 670 does.
   - **The contact phase applies the cymbal-wide gain and wander phase** taken at the strike to the whole state, so neither steps for the contact's samples. The strikes' own factors (onset gains, ramp, `wash_damp`) still pause there (f.4).
   - **Wander is integrated over time** (f.5). Each mode's LFO phase is `2π · lfo_rate · τ + lfo_phase`, where τ is the integral of the wander rate scale. Its wander phase changes per control block by `−(D/lr) · Δcos(LFO phase)`, with D and lr at that block's depth and rate. With a constant rate and depth this is line 867. A change of rate (`edge_wander_rate`, the LF boost) or of depth (`edge_wander_depth`) changes only the slope, never the phase. The LF rate boost follows the decaying accumulator at every control block.
   - **"No previous strike" is an explicit flag** on each accumulator, not `t_last > 0` (f.8).
   - **Effect at the baked values.** Nova: MF scale down to 0.4 (−8 dB in 500 Hz–4 kHz) and LF up to 1.4 (+2.9 dB below about 1.5 kHz) on dense playing, releasing with tau 1.5 s. Bettis: LF up to 1.7 (+4.6 dB below about 2 kHz) and MF up to 1.4 (+2.9 dB), releasing with tau 5 s and 3 s; louder repeated hits peak higher than the single hits the level trims were measured with.
7. **Excitation band gains per articulation** (2026-10-16, `docs/TUNING.md`). Each bake entry can carry a gain per one-third-octave band, `slots[].excitation_band_gains_db`, tuned by ear in Cymbalis Tuning and written by `tools/apply_tuning.py`. Python has nothing like it. With every gain at 0 dB, as in the shipped bakes, nothing changes.
   - **Bands.** The 22 one-third-octave bands of the onset data: edges at 11404 · 2^((j − 21)/3) Hz, j = 0 … 22 (89.09, 112.25, … 11404, 14368 Hz). A mode belongs to the band its frequency falls in; modes below 89.09 Hz go to band 0, modes above 14368 Hz to band 21 (`excitationBand`). The NPZ files' own table of band edges (…, 11392–14353 Hz) differs from these by about 0.1 %.
   - **Where it applies.** The per-mode gain multiplies the strike's coupling `m` in `FastEngine::strike()`, after the coupling path has produced it: the velocity tables (with `vel_gain_power`), the Hertz tilt, the NPZ velw row (after its peak normalisation) and the position weights. The contact phase and the free-ring injection take the coupling from there, and an edge strike's band-envelope state is copied from it (`Er` from `Br`). So it scales the energy that articulation's strikes put into each mode, in every path. Decay, onset ramp and onset gains, the shared cymbal's state and the other articulations are not touched.
   - **Why at the strike and not in the tables.** NPZ velw rows are peak-normalised after the velocity interpolation, so a pre-scaled row would be partly normalised away. Hertz entries have no per-mode table. And `vel_gain_power` would bend a pre-scaled velocity table. The bake therefore stores the gains with the articulation's excitation data. The loader turns them into one per-mode gain array per bake entry (`excitationModeGains`), and the strike multiplies by it: one multiply per mode, and only for entries with a gain that is not 0 dB.
   - **Tuning build.** The message thread computes the arrays when a value changes and hands them to the audio thread with the other tuning values, through the triple buffer: no locks, no allocation. They apply from the next strike.
   - `Engine`, the retired Python-matching path, refuses bakes with band gains.
8. **Onset shifts are tuning departures from the SPEAR onset, recorded per band in the tuning JSON** (2026-10-17, `docs/TUNING.md`, "Onset shift"). The SPEAR captures fix each band's onset timing: the NPZ band envelopes and the pk_t peak times. Kris can move it by ear, per articulation and per one-third-octave band (the 22 bands of decision 7). Each band gets a delay (−50 to +200 ms) and a speed (×0.25 to ×4). The tuning file records them per articulation (`onset_delay_ms`, `onset_speed`). `tools/apply_tuning.py` stores them with the articulation's bake entry (`slots[].onset_shift`), and the engine applies them at each strike of that articulation. At delay 0 and speed 1, as in the shipped bakes, nothing changes.
   - **Band envelope** (an envelope strike: the 602 and Bettis towards the edge): band j's envelope is read at (t − delay) × speed, in `sumComponents`. It holds its first frame before the delay ends. The in-frame decay and the envelope window keep real time. The strike keeps a copy of the values, so a change applies from the next strike. Each NPZ envelope band takes the shift band of its middle mode; for both presets that is the band of the same number.
   - **Onset ramp** (lines 896-901): each mode's peak time becomes pk_t / speed + delay, never below 0, with the mode's band from its frequency (`excitationBand`).
   - **No ramp in the engine.** At edge fraction 0.1 or less the engine has no ramp (lines 848-851, "skip ramp for bow"). This covers bow, bell and center strikes at their base positions. Their peak time counts as 0: a positive delay gives those modes a ramp from the birth fraction over the delay, and the speed acts on nothing. The task named these articulations as ones "with per-mode onset peak times". Their baked `pk_t_bow` is used only for strikes blended towards the edge, as in Python. Turning the bow ramp on at the base position would be a separate sound change.
   - **Why stored per entry and applied at the strike.** The envelopes and pk_t arrays are shared by every entry of a bake, so a per-articulation shift cannot be written into them. Like the band gains, it is stored with the entry.
   - **Cost.** Per strike, one division per mode; per control block, two multiplies per envelope band. A shifted bow or bell strike runs the ramp path until its last delayed band peaks. With random shifts on every band of every articulation, NOVA RIDE at 96 kHz goes from about 21 % to 26 % of a core (bench); the 602 and Bettis change little.
   - `Engine` refuses bakes with onset shifts.

### Plugin mechanisms of the whole cymbal (tuning)

These are plugin mechanisms; none is a port of Python. Kris tunes them by ear in Cymbalis Tuning (`docs/TUNING.md`), and `tools/apply_tuning.py` bakes them into the preset's `tuning` block, so the normal build plays them. At their neutral values (×1, amounts 0) each is skipped entirely: no computation, no cost, and every preset plays exactly as before.

9. **Ring length per band** (2026-10-17). Each of the 22 one-third-octave bands of decision 7 has a ring-length multiplier, `ring_length_bands` (×0.25 to ×4). A mode's T60 is baked T60 × `ring_length` × its band's multiplier (`tunedT60`, used by both the bake loader and the live path). It belongs to the mode, so it acts on every strike.
   - **Baked:** `applyTuning` multiplies `t60S`, and the engine is built with it, like `ring_length`.
   - **Live (tuning build):** the processor's message thread computes every decay coefficient at the prepared rate (`FastEngine::computeDecay`: decay rate, contact step, shared-oscillator steps, contact response). The coefficients are cached per preset and sent with each tuning message. The audio thread takes them at the next control point of the free phase (`applyDecay`):
     - it re-anchors the state as a strike does without its contact (`rebaseComponents`, then a new segment on the same control grid), so every ringing strike's amplitude is exact at that sample;
     - it swaps the arrays in (no allocation) and recomputes each ringing strike's own decay per block.
     - From there everything decays at the new rates, with no level step; a change during a contact waits for its end.
     - The overall ring length still rebuilds the engine, and a new cymbal gets the band factors baked in.
10. **Energy cascade** (2026-10-17). After hard hits, energy moves from lower one-third-octave bands into higher ones over time, with the cymbal's total energy kept: the bloom and wash of a crash. The idea comes from Python V19's NL redistribution (stage 10 above, lines 407–423 and 788–839: LF/MF/HF band energies, a drive with attack and release, energy-conserving transfers upward, `nl_attack_ms` 100 and `nl_release_ms` 150), which is off in the plugin (live-engine decision 1). It is not a port: it runs continuously, at every control point, on the 22 bands of decision 7 and across all strikes. Values: `cascade_amount` (0–1, 0 = off), `cascade_threshold_db`, `cascade_speed_ms`, `cascade_release_ms`, `cascade_spread`, `cascade_low_band`, `cascade_high_band` (`docs/TUNING.md`, "Energy cascade").
   - **Measure:** after the components are summed, band j's energy is the sum over its ringing modes of |Y A1|² (the shared oscillator and the components' sum at the block's end, before the LF/MF scales).
   - **Threshold:** `cascade_threshold_db` relative to the band's energy after a velocity-1 strike of the BOW articulation at its position, as its contact leaves it (|coupling × H|², with the articulation's calibration, level trim and band gains as it plays now; `computeCascadeRef`).
   - **Activation:** one-pole per band towards 1 above the threshold and 0 below it, with the attack time `cascade_speed_ms` or the release time.
   - **Transfer:** a band in [low, high] gives the fraction f = 1 − exp(−amount × activation × K/sr / speed) of its energy. Targets are the bands up to `cascade_spread` above it, within the highest band and with a ringing mode, weighted 1, 1/2, 1/4, … and normalised. Within a target band each ringing mode gets a share ∝ `exc_norm`.
   - **Source modes:** scaled by √(1 − f) through the shared oscillator: its anchor at the block's end (`Y`) and this block's end amplitude, so every strike in the mode is scaled alike and the block ramps to it. The gains are kept per mode for the segment (`oscG_`), so a strike's re-anchoring (`rebaseComponents`) includes them; a strike inside a block takes the part of the block's change already rendered.
   - **Target modes:** a dedicated component (the cascade component, decaying with each mode's T60: `wash_damp` 1) is raised in each target mode in phase with what it holds, so its energy rises by exactly the mode's share; a mode it does not hold yet takes a phase from the engine's strike-phase sequence (`rng_`). The rise ramps in over the block and enters the component at the next control point. Its interference with what else rings in the mode averages out over a band's modes, as two strikes' do.
   - **Range:** a mode can keep receiving energy while its shared oscillator decays far below 1 (its T60 over a long segment) and while the cascade component's own decay factor does. The amplitude relative to them would then outgrow float. So, while the cascade is on, a mode whose oscillator falls below 1e-3 has that magnitude moved into its components' amplitudes, and the cascade component's decay factor is folded in the same way (`renormOscillators`). What rings does not change.
   - **Pool:** the cascade component is one of the 24. A strike merging it away, or the drop of quiet components, ends it; the next transfer starts a new one. Without a free slot, two settled strikes are merged first (their own decay taken into their amplitudes); if none has settled, nothing moves for that control point.
   - **Neutral:** with amount 0 none of it runs: no measurement, no gains, no random numbers drawn, so every preset plays exactly as before. Both builds have it; the tuning build changes the values live (from the next control point). Silent modes take no energy. `strike()` and `process()` still do not allocate; at maximum settings NOVA RIDE uses about 21 % of a core at 96 kHz (20 % without).
11. **Loud-band damping** (2026-10-17). While a band rings above a threshold, its modes decay faster: decay rate × (1 + amount × how far above, capped). Values: `loud_damp_amount` (0–4, 0 = off), `loud_damp_threshold_db` (as the cascade's: relative to a velocity-1 BOW strike's band energy), `loud_damp_tilt` (−1 to +1). It is new: Python has nothing like it.
   - **How far above:** at every control point, from the cascade's band energies, x = min(dB above the threshold / 20 dB, 1), smoothed with a 20 ms one-pole.
   - **Tilt weight:** w_j = 1 + tilt × (j − 10.5) / 10.5 (bands 0..21), never below 0.
   - **Decay:** each ringing mode of the band gets the per-block gain exp(−ln 1000 / T60 × amount × w_j × x_j × K / sr). T60 is the mode's own, with ring length and per-band ring length. The gain goes through the shared oscillator, as the cascade's source gains do (the same block ramp, segment product and strike re-anchoring), so every strike in the mode is damped alike. That is the mode's decay rate times (1 + amount × w × x); a strike's own `wash_damp` is not part of the extra rate.
   - **With the cascade:** the damping applies first; the cascade's moved fraction is taken of the energy left after it.
   - **Neutral:** amount 0 skips it entirely.
12. **Re-hit damping** (2026-10-17). A new strike first damps what is already ringing, then adds its own energy. Values: `rehit_damp_amount` (0–1, 0 = off), `rehit_damp_time_ms` (1–50 ms), `rehit_damp_weights` (per articulation id, 0–2, default 1). It is new: Python has nothing like it.
   - **Target:** at a strike, every component already ringing (not the new one, nor one still in its contact) gets a gain ramp to s = max(0, 1 − amount × velocity × weight of the struck articulation).
   - **Shape:** a smoothstep over the ramp time, starting when the free phase starts (the end of the contact). The gain is evaluated per component at the control points and interpolated across the block in the component sum (`sumBlock<true>`, used only while some component has a ramp; otherwise the summation is the unchanged `sumBlock<false>`). The shortest ramp is therefore about one control block (2.7–2.9 ms), linear across it.
   - **Ramp end:** the end value goes into the component's amplitudes, and the ramp is dropped.
   - **Strikes:** a strike re-anchors the state with each component's current gain (as rendered) and continues its ramp relative to it. A further strike combines what is left of an earlier ramp with its own target over its own ramp time. Merged components keep the energy-weighted remainder.
   - **Live changes:** a ramp that is running when the amount goes to 0 completes.
   - **Neutral:** amount 0 changes nothing: no ramp is created, and the summation and strikes are as before.

### Articulations and sticks

13. **Every edge is SHOULDER only, and Nova's EDGE plays the bow shoulder at the edge** (2026-10-17). Nova has no edge captures. Its EDGE row played the base preset (no overlay) at position 0.0, a stand-in. That stand-in is gone from the bake. The row now plays `edge`, a copy of `bow_shoulder` (velocity tables, calibration, no onset gains) struck at 0.0, with bow_shoulder's level trim (−7.91 dB). As a separate articulation it is tuned apart from the bow shoulder. This is a deliberate sound change, and the only one: every other row of every preset plays as before. The 602 and Bettis edges are SHOULDER captures already and stay as they are. The EDGE stick box shows SHOULDER, fixed, on every preset.
14. **The 602 and Bettis get SHOULDER versions of their tip articulations, as copies** (2026-10-17). Their bell, bow and center are TIP. Each gets a SHOULDER copy that starts as an exact copy, its level trim included, and is tuned on its own: 602 `bow_shoulder` and `center_shoulder`, Bettis `bell_shoulder` and `bow_shoulder`. Python has no shoulder data for these presets; the copies exist to be tuned by ear.
   - **Sticks.** Their bell and bow rows offer TIP / SHOULDER, as Nova's do, and start on TIP. Each preset keeps its own stick choices (`docs/UI_SPEC.md` sections 6, 8 and 9).
   - **NOVA SHOULDER PROFILE.** The tuning button fills such a copy's band gains with Nova's shoulder-minus-tip difference (`docs/TUNING.md`), computed once from the Nova velocity tables by `tools/shoulder_profile.py`.
   - **Bake.** `tools/bake_preset.py` writes the copies (`COPIED_ENTRIES`); a copy named like an existing entry replaces it in place, which is how Nova's EDGE became the copy. The shipped bakes received exactly the re-bake's new entries, checked equal to a re-bake everywhere else, with their sources' trims.

### Per-articulation attack and brightness (tuning)

15. **Attack band gains replace the onset gains' octave bands** (2026-10-17). Every articulation has a gain per one-third-octave band at the strike that fades back to 0 dB with its attack time: gain(t) = 1 + (g − 1) · exp(−t / time). This is exactly how Python's onset band gains work (lines 425–442, 902–913), now with the 22 bands of decision 7 and for every articulation. Values: `attack_band_gains_db` and `attack_time_ms` per articulation (`docs/TUNING.md`, "Attack band gains").
   - **Nova's onset gains are expressed in this form.** The loader takes each of the 22 bands from the octave band (`bow_onset_band_edges_hz`) that contains the band's centre, in dB, and the time from `onset_tau_s` (Python's `bow_onset_tau_ms × 0.001`, line 435, which the whole milliseconds reproduce exactly). Per mode the engine uses the mode's 22-band (`excitationBand`) instead of its octave band (`bow_onset_mode_band`). For Nova they differ for one mode only, mode 0 at 81 Hz (octave band 0, 50–89 Hz, but 22-band 0, whose centre is in octave band 1). All three Nova onset gain sets have the same gain in octave bands 0 and 1. So every mode keeps its gain, |g − 1| < 0.001 still counts as none, and NOVA RIDE renders bit for bit as before (checked).
   - **Gate.** As in Python, a strike with edge fraction 0.9 or more (a bow or bell row blended to the edge) gets none. An articulation whose base position is the edge (0.25 or below) gets its attack gains at any position: Python never had onset gains there, so this changes nothing until they are tuned.
   - **Stored** in the bake with the slot (`slots[].attack_band_gains`) when tuned, else derived from the onset gains. `FastEngine` no longer reads `onset_gains`; `Engine` still does, and refuses a bake with attack data.
   - **Cost.** Per strike, a table lookup per mode, as before.
16. **Brightness vs velocity** (2026-10-17). Per articulation, the spectral tilt of a velocity-0.3 strike relative to a velocity-1.0 one, in dB per octave around 1 kHz (`brightness_db_per_oct`, −6 to +6). It is interpolated in log velocity like the velocity curve (0 at 1.0, within ±24 dB per octave), and multiplies each mode's coupling at the strike by (f / 1 kHz)^(tilt · ln 10 / (20 ln 2)), whatever the coupling path. 1 kHz is unchanged. At 0 it is skipped; otherwise it costs one `exp` per mode per strike. It is stored in the tuning block with the velocity curve. Python has nothing like it.

### Velocity tables

17. **Nova's bow shoulder plays V4 at every velocity** (2026-10-17). Its velocity table, `nova_vel_participation_shoulder_correct.json`, has the columns V4, V4, V5, V5, V5 at `vel_norms` 0.9905, 0.9905, 1, 1, 1. It played V4 up to velocity 0.9905 and V5 above it, so at MIDI 126 and 127. V5 has about half of V4's modes (531 against 1091 above 0.01). At velocity 1.0 its ring is dominated by 647 Hz and 2044 Hz, and it is 5.6 dB quieter than V4 at 0.98. So the bow shoulder, and the EDGE row copied from it (decision 13), turned bell-like at the top two velocities.
   - **The fix, in the bake only.** In `bow_shoulder`'s `bow_vp`, every V5 column is replaced with V4, for BOW2 and for EDGE. `vel_norms` stay as they are. Below 0.9905 the table gives V4 × v / 0.9905, as before; from 0.9905 on, V4.
   - **Unchanged:**
     - every velocity up to 0.9905;
     - `presets/` (the data file stays as it is);
     - the bell table, the level trims (measured at 0.9) and the NOVA SHOULDER PROFILE (computed at 0.9);
     - the calibration scalar. Python computes it with an edge strike at velocity 1.0 (a.7), so with V5. It is kept so that nothing below 0.9905 changes.
   - **Where.** `tools/bake_preset.py` applies it (`VP_COLUMN_FIXES`), so a re-bake keeps it, and records it in the bake's `notes`. Both builds play the same bake. The tuning build has no copy of the velocity tables.

### Velocity layers (tuning)

18. **The onset shift and the attack gains have SOFT and HARD velocity layers** (2026-10-17; `docs/TUNING.md`, "Velocity layers"). Per articulation and band, the onset delay, onset speed and attack gain each have a value at velocity 0.3 (SOFT) and at 1.0 (HARD). A strike takes each band between them with the weight t = ln v / ln 0.3, clamped to 0..1, as the velocity curve does. Delay and attack gain are interpolated linearly, speed geometrically (`layeredShift`, `layeredAttack`). Python has nothing like it.
   - **On top of the onset data.** As decisions 8 and 15: a band envelope keeps its blend between the captures of the strike's velocity and is read at (t − delay) × speed; an onset ramp peaks at peak time / speed + delay; a bow, bell or center strike at its base position has peak time 0.
   - **Neutral.** Both layers start as the articulation's single values (delay 0, speed 1, the attack gains of the bake). A slot whose layers are equal uses its values exactly as before (`shiftLayers_`, `attackLayers_`), so every preset plays bit for bit as before (checked).
   - **Per strike.** With different layers: 22 delays and speeds and 22 attack gains (one `pow` each) from the two arrays, into fixed arrays; no allocation.
   - **Stored** in the bake with the slot: `slots[].onset_shift.delay_ms_soft`, `speed_soft` and `slots[].attack_band_gains.gains_db_soft` beside the HARD values. Without them a bake entry uses one layer for both.
   - **Peak readout** (tuning build only, `FastEngine::onsetPeaksMs`). At each strike: per mode the time its band envelope peaks after the shift (its band's largest interpolated frame, `startEnvelope`), or its ramp's peak time, else 0; per one-third-octave band the mean weighted by |coupling|². The processor hands it to the Tuning window through atomics. It does not change the sound.

### Stereo (tuning)

19. **Per-band pan and width, and a tunable right-channel high-pass** (2026-10-24; `docs/TUNING.md`, "Stereo"). Python's stereo is fixed: each mode's output gains are its pick vector entries `pick_l`, `pick_r` (random from the seed, lines 205–207), and the right channel alone goes through a 120 Hz high-pass (line 703, f.10). The whole cymbal now has, per preset, a pan (`stereo_pan_bands`, L100 to R100) and a width (`stereo_width_bands`, 0–200 %) per band of decision 7, an overall width (`stereo_width`, 0–200 %), and the high-pass's on/off (`right_highpass`) and frequency (`right_highpass_hz`, 20–300 Hz). At centre, 100 %, on and 120 Hz everything plays bit for bit as before (checked).
   - **The model** (`stereoGains`, `plugin/dsp/Tuning.cpp`; the same in `tools/apply_tuning.py`). A mode's baked gains are a level sqrt(L² + R²) and a position atan2(|R|, |L|) / (π/4) − 1 (−1 hard left, +1 hard right; the baked ones lie within ±0.31). Its new position is band pan + position × band width × overall width, clamped to ±1. Its new gains are level × cos and level × sin of (position + 1) × π/4, each with its baked gain's sign. A band at centre and 100 % copies the baked gains unchanged.
   - **The band's mean position is not taken out.** A band's mean baked position is near centre but not at it (mostly within ±0.05, up to ±0.09, and −0.24 for a band with one mode), so a formula with it subtracted could not give the baked gains at centre and 100 %. Here the width scales each mode's whole position, the band's small mean offset with it.
   - **The gains the engine plays** are `BakeData::gainL`, `gainR`: the pick vectors, or a bake's `stereo` block (`gain_l`, `gain_r`, written by `tools/apply_tuning.py`; the loader checks each pair keeps its pick entries' level within 1e-9 and their signs). The pick vectors stay as baked and are still regenerated and checked against the seed.
   - **Live changes.** The Tuning window computes the gains on the message thread and sends them with the other values (`TuningMessage::gainL`, `gainR`). `FastEngine::setTuning` copies them, without allocating, into a pending set. At the next free-phase control point the engine moves to them linearly over one control block: per mode a gain step per sample (`kernel<G, true>`, used only in that block; kinks too). The block's end values become the gains. A change during a strike's contact waits for its end; a strike inside a ramp keeps the gains the ramp has reached, and the rest of the change follows. While the cymbal is silent, or before `prepare`, new gains apply at once. The free phase's culling bounds use the larger of the current and the target gains during a ramp.
   - **The high-pass** is the same 2nd-order Butterworth, at the tuned frequency, computed from the host rate. A change of frequency moves its five coefficients linearly over 20 ms. Every point on that line is a stable high-pass with its zeros at DC: both end filters have a = [1, a1, a2] inside the stability triangle, which is convex. On/off crossfades between the filtered and the unfiltered right channel over the same 20 ms; the filter keeps running while it fades out. Outside a ramp, on at a fixed frequency, the filter runs exactly as before.

### Implementation notes for these decisions

- **Calibration is applied when the strike is injected.** Python multiplies the whole output by one scalar at the end. With NL off, every remaining mechanism is linear in amplitude, so the two give the same result to within rounding.
- **The skip threshold is scaled to match.** Python skips a mode for a segment when `|z| < 1e-9` in its uncalibrated units (line 859). The C++ state holds calibrated amplitudes, so it uses `1e-9 × calibration` of the slot that struck most recently. For a single-slot sequence this is the same test as Python's.
- **Contacts in the live engine run in full.** In Python, a strike that lands while an earlier contact is still running cuts that contact short (f.7). `EngineConfig::truncateContactsLikePython` selects between the two behaviours. With `false`, the default and the live engine, every contact plays all its force samples. The contact phase of the new segment lasts until every running contact has finished. If more than 64 contacts overlap, the oldest one's remaining force is applied at once, so it still enters in full. With `true`, used with `cymbalis_render --python-contact-truncation`, a new strike discards the rest of a running contact, as Python does. The A/B sequences never overlap contacts, so both settings render them identically.
- **The plugin uses a faster engine.** `FastEngine` (`plugin/dsp/FastEngine.h`) keeps `Engine`'s segments, strike logic and the decisions above, with the superposition described in the next section. It renders the free ring with one recursive complex oscillator per mode in float, re-anchored from double every control block (128 samples at 44.1/48 kHz, 256 at 88.2/96 kHz, about 2.8 ms; for the 602 and Bettis presets a seventh or an eighth of the band envelope's 20 ms frame, see "602 and Bettis"). Wander, onset ramp, onset gains, the LF and MF scales and each strike's own decay are evaluated at the control points and interpolated; a mode whose ramp reaches 1 inside a block gets the exact ramp for that block. Every rate-dependent value is computed from the host rate: mode steps, the right channel's high-pass (120 Hz unless tuned, decision 19), and the contact force, whose Python taus and sigma floor are samples at 44.1 kHz (13 samples at 44.1 kHz, 14 at 48 kHz, 26 at 88.2 kHz, 28 at 96 kHz for Nova). The strike position is an argument, so the blend knobs can move it. It requires tip and shoulder taus to be equal (Nova's are), so the contact length does not depend on position.
- **Level trims (plugin only).** Each slot of the plugin's bake carries a level trim, `level_trim.gain_db`: the gain that gives a velocity-0.9 hit at its base position the same 1-second RMS (both channels) as one on BOW (tip). It is measured with `FastEngine` itself (`tools/cymbalis_trims`), stored by `tools/set_level_trims.py`, and can be edited by hand. `FastEngine` multiplies a strike's coupling by calibration × trim, and scales the skip threshold the same way; blends use their slot's trim. `Engine`, the Python-matching path, ignores it. Nova Ride: BELL shoulder +10.48 dB, BOW tip 0, EDGE −7.91 dB (bow shoulder's, decision 13; the stand-in had −8.61 dB), BELL tip +17.48 dB, BOW shoulder −7.91 dB.
- **The comparison pipeline** is `tools/bake_preset.py`, `tools/render_reference.py`, `cymbalis_render`, `tools/compare_ab.py` and `tools/run_ab.py`. The sequences are in `tests/sequences/`. Each stage's sequences switch on everything up to that stage and nothing after it.

### Superposition in the plugin

Kris's decision, a deliberate departure from Python V19 in `FastEngine` only (`Engine`, the Python-matching path, is unchanged). In Python, several effects follow the latest strike and act on everything that is ringing (f.2, f.3), so a bell hit recolours and shortens the bow ring and vice versa. In the plugin, each strike's own effects apply only to the energy that strike added:

- the onset band gains of its slot's overlay and their tau (since 2026-10-17 its articulation's attack band gains and time, decision 15);
- the edge onset ramp, with its velocity tilt;
- the velocity-dependent decay (`wash_damp` from `vel_len_tilt`).

These stay cymbal-wide, as in Python: the shared modal state (strikes add together), the LF and MF accumulators' scales, the LF accumulator's wander-rate boost, and the wander (live-engine decision 6).

How `FastEngine` does it:

- Each strike is a component: per mode, the complex amplitude its contact left, its own decay relative to the shared oscillator, its onset envelope and its ramp. One oscillator per mode carries what the components share: frequency, wander, and the slowest decay a strike can have (T60 divided by the smallest `wash_damp` of a velocity in 0..1, 0.685 for Nova), so each component's own decay only ever falls.
- At every control point the components are summed per mode (times the LF/MF gain) and the sum is interpolated across the block.
- At a new strike, every component is brought to that sample and their sum is the state the contact phase starts from. For the contact's samples the whole state is output with only the cymbal-wide LF/MF gain and wander phase, so the rest of f.4 still holds: older strikes' gains and ramps pause there.
- A single strike is computed as before. The f.2 jump at the next strike no longer exists, because each strike's decay is applied to its own energy rather than to the output.
- The pool holds 24 components. When it is full, the two settled components (no onset envelope or ramp left) with the closest decays are merged, decay weighted by energy per mode; if none are settled, the two oldest are merged and what remains of their envelopes is dropped. Components and modes below the skip threshold are released.
- `strike()` and `process()` do not allocate. `cymbalis_bench` reports the cost.

## 602 and Bettis (Smoke Flat Ride, Hammered Thin Ride)

Added 2026-10-15. The plugin is the reference (`CLAUDE.md`): these presets were ported with the Python engine as a guide and are checked by ear, with no render comparison. Both are baked with `nl_amount = 0`, as Nova is (Bettis's JSON sets 1.0). Nova Ride's bake is unchanged, and so is every code path it uses: each new mechanism is used only when a bake carries its data.

### What each preset uses

| | 602 (`602_Baseline3.json`) | Bettis (`Bettis_Baseline1.json`) |
|---|---|---|
| Modes | 900 | 780 |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) | same |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling and NPZ band envelope, 10 s window | same, 20 s window, with `edge_rms_correction` |
| Bow and bell strikes | standard coupling; no position weights (no ratio files) | standard coupling with edge and bell ratio weights |
| Onset ramp data | velocity-bracketed edge pk_t (2000 entries; see below) | velocity-bracketed edge pk_t, edge velocity tilt |
| Contact | none (`tip_contact_ms` 0): instantaneous strikes | none |
| MF / LF accumulators | off | MF in boost mode (depth 0.4, tau 3 s, max 1.4; bow 1, edge 1, bell 0.2); LF in boost mode (depth 0.2, tau 5 s, max 1.7, cutoff 2 kHz; bow 0.5, edge 1, bell 0.1), applied since live-engine decision 6 |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, NL | off | off |

Every other key the two presets set either has its default effect or is not read by the engine (`alpha_contact`, `stick_hp`).

### What was ported, and where

- **Bake tool** (`tools/bake_preset.py --preset 602|bettis`, entries in `tools/ab_common.py` `PRESETS`). New fields, written only when a preset uses them, so a Nova re-bake keeps its layout:
  - `shared.pk_t_edge_by_vel_s` and `pk_t_edge_velocities`: the velocity-bracketed edge pk_t, one row per velocity. Previously rejected.
  - `edge_npz`: the NPZ velocities, velw table, band envelopes (velocities × frames × bands, zero-padded as Python pads them), each mode's band, frame length, and the switches `edge_use_velw`, `edge_velw_normalize`, `edge_rms_correction`, `edge_band_env_onset_s`, the NPZ level exponent (`vel_gain_power`, default 0.7 there) and whether an edge band envelope file is set.
  - `effective.tau_ref_ms` and `bow_vp: null` when a preset has no velocity participation table.
  - `notes`: facts about the data (602's pk_t length).
- **Loader** (`plugin/dsp/Bake.{h,cpp}`): reads those fields (`EdgeNpz`, `pkTEdgeByVel`, `EngineParams::tauRefMs`, `SlotData::hasBowVp`). The stage gate now accepts `hertz_vel_scale`, NPZ edge data and the edge band envelope. An edge band envelope without NPZ data is accepted and reported as having no effect, because Python never applies it (`_eb_active` is never set).
- **FastEngine** (`plugin/dsp/FastEngine.cpp`):
  - `hertzVelocity()`: `hertz_vel_scale` (v² × H(f, τ(v)) / H(f, τ_ref), with v clipped to 1e-4..1).
  - `npzCoupling()`: for an edge strike, the velw row at the remapped velocity, peak-normalised, times v^p and a second set of random phases (Python draws both sets, lines 1033-1060).
  - Envelope strikes (`startEnvelope()`, `envelopeAt()`, `restartFrames()`): for its onset window, an edge strike's level in each mode is |coupling| × its band envelope × the in-frame decay `exp(−ln1000·(t mod 20 ms)/T60)` (lines 869-894). The band envelope is interpolated between the NPZ velocities, peak-normalised, and multiplied by the RMS correction. After the window the strike rings with its own decaying amplitude, as in Python.
  - The bracketed edge pk_t is interpolated per strike, then the velocity tilt is applied as before.
  - A strike without a contact phase now gets its own decay at once. Before this, `FastEngine` set it only when a contact ended, which every Nova strike has.
- **`Engine`**, the Python-matching A/B path, rejects bakes that need any of these. It is retired, and they exist only in `FastEngine`.

### Live-engine decisions for these presets

1. **The band envelope belongs to its strike** (the superposition decision above). In Python, one envelope, that of the latest edge strike, replaces the level of every ringing mode, including energy from later bow strikes, and the onset ramp never applies again after it (lines 869-896). In the plugin, each edge strike's envelope drives only its own energy, and other strikes ring normally. A strike with an envelope has no onset ramp, as in Python.
2. **The shared oscillator does not decay for these two presets.** An envelope strike's level follows its envelope, not its T60, so every strike carries its whole decay (`oscWash_ = 0`). Nova keeps its oscillator decay.
3. **The control block divides the 20 ms frame.** It is 126 samples at 44.1 kHz, 120 at 48 kHz, 252 at 88.2 kHz and 240 at 96 kHz (7 or 8 blocks per frame), so the in-frame decay restarts on a control point. At each later strike, the next restart of every running envelope is rounded to the new control grid (at most half a block, about 1.4 ms, once per strike). Nova keeps 128/256.
4. **Slots.** SMOKE FLAT RIDE: BOW and BOW2 play the 602 bow and EDGE the 602 edge. BELL and BELL2, labelled CENTER and CENTER2, play the 602 center (2026-10-17; silent before). HAMMERED THIN RIDE: BELL/BELL2 play the bell, BOW/BOW2 the bow, EDGE the edge. Those are the TIP versions. Since 2026-10-17 the bell and bow rows also offer SHOULDER copies (decision 14 under "Articulations and sticks"). EDGE is SHOULDER only (`docs/UI_SPEC.md` section 8). Bake entries: 602 `BOW` (`bow.json`, 0.5), `EDGE` (`edge.json`, 0.0), `CENTER` (a copy of `BOW` at 1.0, articulation id `center`), `BOW_SHOULDER` (a copy of `BOW`) and `CENTER_SHOULDER` (a copy of `CENTER`); Bettis `BELL` (1.0), `BOW` (0.5) and `EDGE` (0.0), with no overlays, `BELL_SHOULDER` and `BOW_SHOULDER` (copies). Python has no center or shoulder data for these presets: those are plugin entries made of the tip data, to be tuned by ear (`docs/TUNING.md`).
5. **Level trims follow the SPEAR captures, capped by peak** (2026-10-15; they replaced trims that made every slot equally loud at velocity 0.9). All levels are at full velocity (1.0), the RMS of the first 500 ms (both channels).
   - BOW is matched to Nova BOW (tip), so the presets stay level with each other. It is never lowered, even where it peaks above 0.9.
   - EDGE and BELL go to their SPEAR target, the level the cymbal's loudest SPEAR captures have relative to its bow (mean over the first 500 ms; recording gain assumed equal). But they go no higher than the trim at which one hit peaks at 0.9 (about −1 dBFS) before the Volume knob.
   - Each hit draws its own random phases, so its peak changes from hit to hit, and the highest of many hits keeps rising the more hits are measured. "The peak of one hit" is therefore the 99th percentile of the peaks of 1024 hits (first second of each), each with the next phase draw of the preset's sequence, as the plugin plays them.
   - BOW2 and BELL2 share BOW's and BELL's bake entries and trims.
   - The 602's CENTER starts with BOW's trim. Measured again, it has no SPEAR target and is matched to Nova's BOW (tip) like BOW, so it gets BOW's trim.
   - The shoulder copies (602 `BOW_SHOULDER`, `CENTER_SHOULDER`; Bettis `BELL_SHOULDER`, `BOW_SHOULDER`) start with their sources' trims.
   - Each bake records, next to the trim (`slots[].level_trim`): `spear_target_db`, and where capped, `capped`, `capped_level_db`, `uncapped_gain_db` and `cap_reason`.
   - Measured with `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target SLOT=DB --peak-limit 0.9` (`docs/BUILDS.md`). Nova keeps its own trims (velocity 0.9, 1 s).

   | Preset | Entry | SPEAR target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile) | Capped |
   |---|---|---|---|---|---|---|
   | SMOKE FLAT RIDE | BOW | 0 dB | −1.76 dB | 0 dB | 1.04 | no (BOW) |
   | SMOKE FLAT RIDE | EDGE | +2.8 dB | −0.05 dB | +2.8 dB | 0.90 | no |
   | HAMMERED THIN RIDE | BOW | 0 dB | −0.85 dB | 0 dB | 0.92 | no (BOW) |
   | HAMMERED THIN RIDE | BELL | +2.7 dB | +10.44 dB | +0.8 dB | 0.90 | yes (+12.32 dB would peak at 1.12) |
   | HAMMERED THIN RIDE | EDGE | +14.2 dB | +5.36 dB | +0.6 dB | 0.90 | yes (+18.95 dB would peak at 4.30) |

6. **One engine; a preset is data loaded into it.** The three bakes are parsed at plugin load. The processor owns one `FastEngine`, loaded with the current preset's bake. Choosing a preset, or restoring a session with a different one, goes the same way:
   - A loader thread builds the engine state for the new bake at the current sample rate and hands it to the audio thread through an atomic pointer, without locks. It takes over at the next block.
   - It continues the old engine's clock (`FastEngine::continueFrom`), so the engine clock never restarts. Only the clock carries over.
   - If the old cymbal is still ringing, it fades out over 30 ms (raised cosine) alongside the new one. The audio thread then hands it back, and the loader thread frees it.
   - Outside that fade only one engine exists. Nothing on the audio thread allocates, frees, locks or touches files.
   - The preset is session state, not a parameter.
   - The strike-phase random sequence starts from the loaded preset's own seed (NOVA RIDE 7, SMOKE FLAT RIDE and HAMMERED THIN RIDE 11), as in Python. This happens each time a preset is loaded: at plugin load, on a switch and on a session restore. Each bake's pick vectors and wander tables also come from its own seed.
   - The MF and LF accumulators belong to the cymbal and start fresh with the new one.
7. **Bettis bell and bow follow velocity linearly** (2026-10-25). With no measured velocity tables, Bettis's bell and bow strikes use the Hertz law, amplitude v² (`hertzVelocity`): −12 dB at MIDI 64 and −20 dB at MIDI 40, where Nova and every edge fall about linearly (−6 and −10 dB). On pads that left the bell and bow too quiet at mid velocities.
   - The fix is a velocity curve (`docs/TUNING.md`) on the four bell and bow articulations, tip and shoulder (`bell`, `bow`, `bell_shoulder`, `bow_shoulder`): `vel_0.6_db` 4.437 (20·log10(1/0.6)) and `vel_0.3_db` 10.458 (20·log10(1/0.3)). Interpolated in log velocity, it turns v² into v at every velocity, within 0.001 dB down to 0.15; below 0.3 the same slope continues.
   - It is the bake's tuning block, applied with `tools/apply_tuning.py`; re-baking Bettis drops it, as any tuning.
   - Level only: the Hertz law's spectral term (softer strikes darker) stays. At velocity 1.0 the curve is 0 dB, so the trims (decision 5, measured at 1.0) and full-velocity hits do not change.
   - EDGE is not touched: its NPZ captures are already linear in velocity. The 602 and Swish are not changed yet.

### Things in the source data or engine that look wrong (ported as they are)

- **602 notes vs parameters.** The 602 JSON's notes say `t60_scale=2.5`, but the parameter is 3.5. They also say "same 2000-mode pool as B1/B2", but the fingerprint has 900 modes. The NPZ notes say it is for the 900-mode v3 fingerprint.
- **602 pk_t length.** `602_pkt_data.json` has 2000 entries per array for 900 modes. Python reads them by position, so the first 900 are used (f.17). The bake does the same and records it.
- **The NPZ velocity remap adds the lowest velocity twice (fixed in the plugin, 2026-10-15).** At line 1051, `_evels[0] + np.interp(..., _evels)` gives a mapped velocity from 2 × v_min to v_min + v_max, so the softest velw rows are never used. That meant 0.15–0.25 for 602 (every strike was shifted up by about 0.15, and from about 0.87 up the 1.0 capture played), and 0.273–0.355 for Bettis (from about 0.72 up the 1.0 capture played). `FastEngine::npzCoupling` now does what the comment intends: the sqrt remap over the captured velocities, with no extra offset; each capture maps to itself, and below the softest capture its row is used. Only the velw coupling used the shifted velocity. The band envelopes always interpolated at the true velocity and are unchanged.
- **The NPZ envelopes are shorter than their windows.** 602's run 8–9 s for a 10 s window; Bettis's run 12.8–16.2 s for a 20 s window. Velocities with fewer frames are zero-padded, and the last frame is held after the data ends. A strike's level therefore drops toward zero before the window ends, then jumps to its exponential decay when the window ends. The decay is from the strike, at the mode's T60. Ported as is.
- **The in-frame decay is a 50 Hz sawtooth.** It restarts every 20 ms (`t_in_frame`, line 891), which amplitude-modulates every envelope mode at 50 Hz by up to e^(−6.9·0.02/T60) per frame: 37 % for a 0.3 s T60.
- **The pk_t ramps have no effect in these presets.** With the NPZ envelope active, the edge pk_t ramp never applies (it is skipped whenever the envelope is active), and bow strikes never ramp. Bettis's velocity-bracketed pk_t and edge velocity tilt are therefore inaudible here.
- **Bettis's LF accumulator is inert in Python.** It is in boost mode (`lf_max_scale` 1.7), which never scales amplitude (line 914), and `lf_wander_rate_boost` is 0 with `wander_depth` 0. The plugin applies the LF scale in both modes (live-engine decision 6), so Bettis's LF builds up to ×1.7 on repeated hits.
- **Bettis's `edge_band_env_path` file is never used.** It is a 4-band, 200-frame envelope that the NPZ path overrides. 602's file is an all-zero trigger: it only switches the NPZ envelope on.
- **NPZ edge coupling ignores the excitation table.** It skips `exc_norm`, the edge position weights and `edge_level_boost`. The calibration strike (Python's `_compute_calib_scalar`) is rendered without the band envelope, so the calibration scalar does not reflect the envelope's level. The trims make up for it.
- **`hertz_vel_scale` scales amplitude by v².** Its docstring says the Hertz peak force goes as v^(6/5); the code comments that v² is a perceptual calibration. Bettis's bell and bow are offset to v by a velocity curve (decision 7).

## Swish (22" China)

Added 2026-10-17. As for the 602 and Bettis, the plugin is the reference: Swish was ported with the Python engine as a guide and is checked by ear, with no render comparison. It is baked with `nl_amount = 0`, as its JSON sets. NOVA RIDE, SMOKE FLAT RIDE and HAMMERED THIN RIDE play bit for bit as before (checked), because each new mechanism acts only where a bake asks for it: a stick gain above 0, or an NPZ band envelope switched on by the bake.

### What Swish uses

| | Swish (`Swish_Baseline1.json`) |
|---|---|
| Modes | 900 (every mode is born before `birth_gate_s` 100) |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Contact | per mode (`permode_contact`, `tip_contact_ms` 2.0): tip and shoulder taus 2 and 7 samples, `sigma_ratio` 0.28, 13 samples at 44.1 kHz; the same at every position, as Nova's |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (6 velocities, 0.2–1.0) and, switched on by the bake, the NPZ band envelope: 20 bands, 100 Hz–10 kHz, 20 s window, `edge_rms_correction` all 1, level exponent `vel_gain_power` 1.0 |
| Bow and bell strikes | standard coupling with the edge ratios (neutral, all 1) and the bell ratios (v11) as position weights |
| Onset ramp data | bow pk_t (BowV4), velocity-bracketed edge pk_t (6 velocities), edge velocity tilt (4 bands) |
| MF / LF accumulators | MF in boost mode (depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2); LF in boost mode (depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1), applied as live-engine decision 6 |
| Stick click | `bow_tip` only (`stick_gain` 6.0; 2–6 kHz, 8 ms, decay 3 ms; bow weight 1, bell 0, edge 0) |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

Every other key either has its default effect or is not read by the engine: `alpha_contact`, `stick_hp`, `atk_len`, `atk_dec`, and `tip_contact_ms` and `shld_contact_ms` beyond switching the per-mode contact on (the per-mode contact uses only the taus).

### What was ported, and where

- **Stick click** (`_add_transient`, lines 1194-1211; `FastEngine::startStick`, `renderStick`, `butterworthBandPass2`). The plugin's first noise source.
  - **When.** `render()` calls it right after `_apply_strike` (line 620); `strike()` starts it after the strike's own random draws.
  - **The burst.** `int(stick_len_ms × 0.001 × rate)` standard normal values × `exp(−k / (stick_decay_ms × 0.001 × rate))`. Python draws them with `rng.standard_normal` from the strike rng. The plugin draws them from the same strike-phase sequence, at the same point, as Box–Muller pairs of uniforms. numpy's ziggurat is not reproduced, so the strikes after a bow-tip strike draw other phases than Python's would; there is no comparison.
  - **The band-pass.** `dsp_filters._bp`: scipy `butter(2, [lo, hi] / nyquist, 'band')`, here designed at the host rate as two biquads (checked against scipy: coefficients and response to about 1e-15). Each click has its own filter starting from zero, as Python filters each click on its own.
  - **The level.** `stick_gain` × the position weight (`stick_bow_weight` × bow fraction + `stick_bell_weight` × bell fraction + `stick_edge_weight` × edge fraction, Python's fractions of the strike position) × velocity^1.7, at the strike's level: calibration × level trim × velocity curve. Python scales the whole output by the calibration (line 704); the trim and the velocity curve are the plugin's, and the click keeps its balance with the strike it belongs to.
  - **Output.** Both channels, before the right channel's 120 Hz high-pass, as in `render()`.
  - **Rate.** Python runs at 44.1 kHz. The noise is scaled by √(rate / 44.1 kHz), so the band-limited burst keeps its level at every rate.
  - **Voices.** Eight, each with a burst buffer of up to 50 ms, allocated in `prepare()`. When all are busy the oldest is replaced. A voice ends when its burst is over and its filter has rung down below 1e-9 of the noise level, or a second after the burst; Python's filter runs to the end of the render. `strike()` and `process()` do not allocate. Per strike: the burst and the filter design; per sample: two biquads per running voice.
  - **Loader.** `stick_gain` is per articulation (an overlay sets it) and no longer counts as an engine-wide value. The band, length, decay and weights are read from the slot's parameters with Python's `p.get` defaults (2500 Hz, 9000 Hz, 8 ms, 3 ms, weights 1). `Engine`, the retired path, refuses a click. Every articulation of the other presets has `stick_gain` 0, so nothing changes there.
- **NPZ edge data with its own band layout.** The NPZ code of the 602 and Bettis already takes the band count from the data (`EdgeNpz::numBands`, `band_index`). Swish's 20 bands load as they are: `our_band_idx` agrees with `band_edges_hz` for every mode. The one place tied to the 22 bands is the onset shift, where each envelope band takes the shift band of its middle mode; for Swish 14 of the 20 envelope bands map to the shift band of the same number. Nothing assumes 22 envelope bands.
- **The edge band envelope, switched on by the bake.** Python builds the NPZ envelope only when an edge band envelope file is also set (line 691); `Swish_Baseline1.json` sets none, so Python never runs it. The bake sets `edge_npz.envelope_active` and records why in `edge_npz.envelope_set_by` and in its `notes` (`tools/bake_preset.py` `ENVELOPE_ON`), as the 602's all-zero trigger file does in Python. The loader accepts an `envelope_active` that differs from the preset's `edge_band_env` only with that note.
  - **With a contact.** Swish is the first preset with both a contact and an envelope. The envelope scales what the contact leaves, and its time runs from the strike, contact included (`_s0`, line 877).
  - **EDGE ONSET ENVELOPE** (tuning build, `edge_band_envelope`; `docs/TUNING.md`) switches it per strike, from the next strike. Off, edge strikes play as Python plays Swish: the velw coupling and the edge onset ramp. So that the switch can act live, an NPZ preset's engine structure now follows its NPZ data rather than the switch: the control block that divides the 20 ms frame, the shared oscillator without decay, and the envelope buffers. For the 602 and Bettis, whose envelope is on, that is what it was.
- **Bake** (`tools/bake_preset.py --preset swish`; entries in `tools/ab_common.py` `PRESETS`). `BELL` (no overlay, 1.0, the base preset's v11 bell ratios), `BOW` (`bow_tip`, 0.5), `BOW_SHOULDER` (`bow_shoulder`, 0.5), `EDGE` (no overlay, 0.0), and `BELL_SHOULDER`, a copy of `BELL` (`COPIED_ENTRIES`). Every entry has the same calibration scalar, 0.0435: Python's calibration is an edge strike at velocity 1.0, which no overlay changes and which has no click.

### Live-engine decisions for Swish

1. **Slots** (`docs/UI_SPEC.md` section 8). BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow_tip` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). Every row with a choice starts on TIP.
2. **Level trims**, as for the 602 and Bettis (their decision 5): velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their SPEAR targets (Bow V6, Bell V3, Edge V7: BELL +5.3 dB and EDGE +4.7 dB above BOW), capped where one hit (99th percentile of 1024 hits) would peak above 0.9. `bow_shoulder` takes BOW's target and `bell_shoulder` BELL's. Measured with `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=5.3 --spear-target BELL_SHOULDER=5.3 --spear-target EDGE=4.7 --peak-limit 0.9`. Each bake entry records its target, cap and reason (`slots[].level_trim`).

   | Entry | SPEAR target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile) | Capped |
   |---|---|---|---|---|---|
   | BOW (`bow_tip`) | 0 dB | +8.86 dB | 0 dB | 1.45 (with the click) | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | +8.96 dB | 0 dB | 1.11 | no (BOW's target) |
   | BELL | +5.3 dB | +7.60 dB | −0.23 dB | 0.90 | yes (+13.13 dB would peak at 1.70) |
   | BELL_SHOULDER | +5.3 dB (BELL's) | +7.60 dB | −0.23 dB | 0.90 | yes (as BELL) |
   | EDGE | +4.7 dB | −3.44 dB | −0.77 dB | 0.90 | yes (+2.03 dB would peak at 1.69) |

3. **Seed.** The strike-phase sequence starts from Swish's seed, 7.

### Things in the Swish data or engine that look wrong (ported as they are)

- **The NPZ velw is the same at all six velocities.** An edge strike's spectrum therefore does not change with velocity through the captures; only its level (velocity^1.0) and the envelope's interpolation between velocities do. The NPZ notes call velw "per-band audio onset shape from actual recordings (0-50ms per velocity)", but the engine uses it as each mode's coupling weight.
- **The edge excitation ratios are neutral** (all 1.0, "Pending real edge characterisation"), so a bow strike blended towards the edge has no position weighting. `edge_rms_correction` is all 1.0 too.
- **The NPZ envelopes are shorter than the 20 s window.** They run 11.7–14 s (587–700 frames), are zero-padded, and end at 0.02–0.2 % of their peak. After the window a strike rings at its own decay, by then at least 120 dB down (T60 at most 10 s), so the jump at 20 s is inaudible.
- **The outer envelope bands hold far more than their range.** Band 0 holds every mode below 125.9 Hz (106 modes, down to 19 Hz), band 19 every mode above 7.94 kHz (209 modes, up to 23.7 kHz); they follow the envelopes of the 100–126 Hz and 7.9–10 kHz bands.
- **14 modes lie above 22.05 kHz** (up to 23.68 kHz). At 44.1 kHz they fold back below Nyquist, as in Python; the 602 has 8 such modes.
- **pk_t runs up to 5 s**, bow and edge. With the envelope on, the edge pk_t and the edge velocity tilt are inaudible (the envelope skips the ramp, as for Bettis). With EDGE ONSET ENVELOPE off, some edge modes ramp in over up to 5 s. The bow pk_t (BowV4 at velocity 0.65) applies only to strikes blended towards the edge.
- **The calibration does not see the envelope or the click.** The calibration strike (`_compute_calib_scalar`) is an edge strike without the band envelope and without `_add_transient`; the trims make up for the level.
- **BOW (tip) peaks at 1.45 at velocity 1.0** (99th percentile, with its trim) before the Volume knob, from the click on top of the strike. BOW is never capped, by the rule; BOW_SHOULDER, without the click, peaks at 1.11.
- **The click is bow-only.** `stick_bell_weight` and `stick_edge_weight` are 0: there is no click at the bell or the edge, and a bow row blended towards the edge clicks less, with its bow fraction.
