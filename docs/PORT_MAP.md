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

9. **Ring length per band** (2026-10-17). Each of the 22 one-third-octave bands of decision 7 has a ring-length multiplier, `ring_length_bands` (×0.25 to ×4). A mode's T60 is baked T60 × `ring_length` × its band's multiplier (`tunedT60`, used by both the bake loader and the live path). It belongs to the mode, so it acts on every strike. On an edge strike with a band envelope it also scales the envelope's time after a kept onset (decision 20).
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

13. **Every edge is SHOULDER only, and Nova's EDGE plays the bow shoulder at the edge** (2026-10-17; NOVA's EDGE since 2026-11-27: "NOVA RIDE edge v2"). Nova has no edge captures. Its EDGE row played the base preset (no overlay) at position 0.0, a stand-in. That stand-in is gone from the bake. The row now plays `edge`, a copy of `bow_shoulder` (velocity tables, calibration, no onset gains) struck at 0.0, with bow_shoulder's level trim (−7.91 dB). As a separate articulation it is tuned apart from the bow shoulder. This is a deliberate sound change, and the only one: every other row of every preset plays as before. The 602 and Bettis edges are SHOULDER captures already and stay as they are. The EDGE stick box shows SHOULDER, fixed, on every preset.
14. **The 602 and Bettis get SHOULDER versions of their tip articulations, as copies** (2026-10-17). Their bell, bow and center are TIP. Each gets a SHOULDER copy that starts as an exact copy, its level trim included, and is tuned on its own: 602 `bow_shoulder` and `center_shoulder`, Bettis `bell_shoulder` and `bow_shoulder`. Python has no shoulder data for these presets; the copies exist to be tuned by ear.
   - **Sticks.** Their bell and bow rows offer TIP / SHOULDER, as Nova's do. They started on TIP; since 2026-10-31 BELL and BOW start on TIP and BELL2 and BOW2 on SHOULDER, on every preset, Nova included (`docs/UI_SPEC.md` section 8, "Default sticks"). Each preset keeps its own stick choices (`docs/UI_SPEC.md` sections 6, 8 and 9).
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

### Ring length on the band envelope (tuning)

20. **The ring length acts on edge strikes with a band envelope, as a time scale after a kept onset** (2026-10-27; `docs/TUNING.md`, "Ring length"). Such a strike's level follows its recorded NPZ band envelope for its window, not its T60 ("602 and Bettis", decision 1). So `ring_length` and `ring_length_bands` (decision 9) changed only its 20 ms in-frame decay and its ring after the window, and Kris heard no change on SMOKE FLAT RIDE, HAMMERED THIN RIDE, SWISH and RAVEN edge hits. Now they also scale the envelope in time. Python has nothing like it.
    - **Why a time scale.** The first version (PR #41) scaled the envelope's level after each band's peak, peak × (env / peak)^(1/x). Kris compared renders of the options on RAVEN at ring length 0.5 (2026-10-27). The level scaling sounded unnatural: the bloom kept its full length and then collapsed, and the recorded flutter was exaggerated. He chose the time scale as the most natural, so it replaces the level scaling.
    - **Factor.** Per envelope band, x = `ring_length` × the `ring_length_bands` value of the band's onset-shift band (that of its middle mode, decision 8). For the 602, Bettis and Raven that is the band of the same number. Swish's 20 envelope bands take 18 of the 22; bands 4, 10, 19 and 20 act on no envelope.
    - **Time scale** (`ringTime`, in `sumComponents`). With te the envelope time as read before (after the onset shift), the band's envelope is read at te' = te up to the kept onset k, and at te' = k + (te − k) / x after it. So the recording after k (bloom, flutter, decay) runs x times as long, at its own levels. At x = 1, te' = te.
    - **Kept onset** (`edge_ring_keep_ms`, 0–300 ms, default 60 ms). A per-cymbal tuning value (`CymbalTuning::edgeRingKeepMs`), in the Tuning window's CYMBAL group under Ring length, saved in tuning files and stored in the bake's tuning block by `tools/apply_tuning.py` (60 ms, the bake's own, drops out). It acts only where a factor is not 1.
    - **With the onset shift.** The envelope time is (t − delay) × speed as before, then scaled after k. The PEAK readout (decision 18) maps each band's peak back through both.
    - **Window** (`computeEnvRing`), as before. The strike's window is the Window (`edge_band_env_onset_s`, the tuned value) times the largest factor of an envelope band with modes, within 1 and 2. With no factor above 1 it is unchanged.
    - **The in-frame decay** is unchanged. It follows the modes' T60, which the ring length scales as before. `edge_frame_decay_restart` switches it off per cymbal.
    - **At the strike.** The factors and k are taken at the strike and kept with it, like the onset shift. A change of `ring_length_bands` or of the kept onset reaches the envelopes from the next strike; the T60s change at the next control point, as before. A change of `ring_length` rebuilds the engine, as before.
    - **Neutral.** With every factor 1, as in the shipped bakes, nothing is computed and the envelope and window are as before, whatever k is. Every preset plays bit for bit as before (`tools/cymbalis_identity`, in memory: every preset, 44.1 and 96 kHz, 128- and 64-sample blocks, with the bake's tuning and with the cascade and dampings on; with k at 60, 0 and 300 ms).
    - **Cost.** Per strike, one factor per envelope band. Per control block, per envelope strike and band, a multiply and an add. No allocation: the per-band array is allocated with the components.
    - **The held last frame.** Where a velocity's envelope data end before the window, its last frame is held until the window ends, as before ("602 and Bettis", "The NPZ envelopes are shorter than their windows"). Most of those frames are 0. With the time scale the data end at k + (data length − k) × x and the held level is the recording's.
      - Below 1 the data end sooner and the hold lasts longer. At ×0.5 with k = 60 ms: RAVEN's 8.7 s of data end at 4.4 s (window 9.2 s); SMOKE FLAT RIDE's 9 s at 4.5 s (10 s); HAMMERED THIN RIDE's 16.2 s at 8.1 s (20 s); SWISH's 14 s at 7.0 s (20 s).
      - At ×2 they end at 17.3, 17.9, 32.3 and 27.9 s, within the doubled windows (18.4, 20, 40 and 40 s).
      - The frames held are as recorded. RAVEN holds 2–11 bands at every velocity, the loudest −52 to −73 dB below the strike's loudest band peak. SMOKE FLAT RIDE's 0.65 capture holds 9 bands, the loudest at −39 dB. HAMMERED THIN RIDE and SWISH hold one band at one velocity, below −70 dB.

### The edge's voice (tuning)

21. **The edge's long-ringing modes take the bow's pattern** (2026-10-29; `docs/TUNING.md`, "Edge voice from bow"). On the presets with NPZ edge data, an edge strike's per-mode levels are the NPZ velw table, which comes from matching the recorded edge partials to the preset's modes (lines 1043–1061). Those partials are dense and drift, so they land on other modes than the bow's: the edge's onset and band levels are right (Kris), but its tail rings on a different set of modes and sounds like a different cymbal. Of the 20 longest-ringing modes (coupling × √T60) the edge shares with the bow 8 on Bettis (the closest by ear), 3 on Konstan (2 since its T60 refit, 2026-10-31), 2 on the 602 and 0 on Swish. Python has nothing like it.
    - **The value.** `edge_voice_from_bow` (0–100 %, per cymbal; not a host parameter). Within each envelope band, each mode's weight α grows with its baked T60 against the band's median (0 at half the median or less, 1 at twice or more, over the modes with `exc_norm` > 0); the long-ringing modes move from the edge's velw pattern to the bow's `exc_norm` pattern by a·α, and the band is scaled back to its velw norm at every NPZ velocity. The short-ringing modes keep the edge's pattern, and the envelopes are untouched, so the crash, the onset and the velocity behaviour stay as recorded.
    - **Per strike.** `FastEngine` keeps its own velw table (the bake's at 0 %) and reads it in `npzCoupling` at the strike. A new value's table is computed off the audio thread and handed over with the other tuning values; it applies from the next strike.
    - **At 100 %**, of the 20 longest-ringing edge modes, the bow's share 14 on the 602 (was 2), 6 on Swish (was 0) and 10 on Konstan (was 2; 3 before its T60 refit). No band has α = 1 for most of its modes (the most is 43 %, the 602's 566–713 Hz band), so no band takes the bow's pattern whole.
    - **Starting values.** Konstan starts at 100 %, Kris's choice by ear from renders in the project session (2026-10-29), with EDGE re-trimmed at 100 %; the 602, Bettis, Swish, Raven, Extra Paper Thin and Crash 16 stay at 0 %. The 602 was tried at 100 % in the build and set back to 0 % by Kris by ear (2026-10-31): he prefers its edge as it was.
    - **Neutral.** At 0 % every preset plays bit for bit as before (`tools/cymbalis_identity`, in memory).
    - **Cost.** None per strike or block: the table replaces the bake's. Per value change, one pass over the velw table on the message thread.

### Hit-to-hit variation (tuning)

22. **Each strike's position and velocity vary a little** (2026-10-31; `docs/TUNING.md`, "Hit variation"). Every strike draws fresh mode phases (Python's `rng`), so the beating of close modes and the attack's texture change from hit to hit, but a mode's level is fixed by the row, the velocity, the stick and the blend knobs. Measured on RAVEN, 8 identical bow tip hits one second apart differ by 0.56 dB in level and 0.07 dB in a typical band: repeated hits sound like copies. On a real cymbal the main cause of the difference is where the stick lands, which changes the balance of the modes, and the engine already models it through the strike position. Kris chose version B of three renders by ear (made in the project session): position ±0.04 and velocity ±3 %, with which the 8 hits differ by 1.2 dB in level and 0.33 dB in a typical band. Python has nothing like it.
    - **The values.** `hit_variation` (on/off), `hit_position_spread` (0–0.15, default 0.04) and `hit_velocity_spread_pct` (0–15 %, default 3), per cymbal, for every articulation and row; not host parameters. Every bake has it on with the defaults since 2026-10-31 (its tuning block, `tools/apply_tuning.py`).
    - **Per strike.** At the top of `FastEngine::strike`, before anything reads them: p′ = p + U(−s, s), mirrored at 0 and 1 (−p′, 2 − p′), and kept on its side of 0.45 (at least 0.45 when p ≥ 0.45, else at most the double below 0.45). 0.45 is where the edge fraction 1 − p/0.5 passes 0.1, the switch between the bow's paths and an edge strike's NPZ coupling, band envelope and onset ramp (table b, mechanism 12; `edge_frac > 0.1`), so no strike changes paths. v′ = v · (1 + U(−d, d)), at most 1. Every later use of the strike's position and velocity takes p′ and v′: the position weights, edge and bell fractions, NPZ coupling and velocity, band envelope, onset ramp and layers, velocity tables and curve, level, contact and stick click. The slot's base position, which only identifies an edge articulation, is unchanged.
    - **Its own random sequence.** A PCG64 of its own, seeded from the bake's strike-phase state and two constants, gives the two numbers (two draws per strike). The strike-phase stream draws what it drew before, and a preset repeats its variation from each load. Presets whose bakes share a strike-phase seed (Nova and Swish, the 602 and Bettis, Raven and Konstan) share the sequence. No allocation or locking; a change applies from the next strike.
    - **Neutral.** Off, every preset plays bit for bit as before (`tools/cymbalis_identity`, in memory), and each bake with it switched off plays bit for bit as before it was switched on.
    - **Checked in memory** (2026-10-31, at the largest spreads, 0.15 and 15 %): two renders of the same sequence are identical; over 1000 strikes per base position (0, 0.02, 0.3, 0.44, 0.449, 0.45, 0.451, 0.5, 0.7, 0.98, 1) the positions stay in [0, 1], within the spread after the mirror, and on their side of 0.45, and the velocities within the spread and at most 1; a strike at 0.5 never took the envelope path and one at 0.0 always did (1000 each, the five presets with NPZ edge data); with the stick click off, the strike-phase stream stays in step with an engine without the variation, strike after strike, so every varied strike took the NPZ path exactly as the unvaried one.
    - **The ends are one-sided.** At 0 and 1 the mirror only moves a strike inward, so the bell rows (1.0) take in some of the bow's modes and get louder on average. Mean change over 48 strikes against the same strikes without it (velocity 1.0, first 500 ms; Nova 0.9 and 1 s, as its trims): Konstan BELL and BELL_SHOULDER +3.9 dB (single strikes +0.1 to +6.9 dB; its bell is trimmed +26 dB), Nova BELL2 +1.8 dB and BELL +1.0 dB, Raven BELL and BELL_SHOULDER +0.75 dB, Bettis BELL +0.4 dB; every other row 0.3 dB or less (BOW −0.1 to −0.3 dB: a velocity of 1.0 only goes down). The trims are unchanged (Kris to decide).
    - **The stick click.** Its weight follows p′. Where an articulation's click weight is 0 at its position, a varied strike can click quietly: on Swish, BOW2 at BLEND POSITION 2 = 100 % strikes the bow articulation (bow weight 1, edge 0) at 0.0, and at up to 0.04 it clicks with a weight of up to 0.08. The click's noise comes from the strike-phase stream, so from then on that stream runs ahead of where it would be without the variation.
    - **Cost.** Two random numbers and a few comparisons per strike.

### Implementation notes for these decisions

- **Calibration is applied when the strike is injected.** Python multiplies the whole output by one scalar at the end. With NL off, every remaining mechanism is linear in amplitude, so the two give the same result to within rounding.
- **The skip threshold is scaled to match.** Python skips a mode for a segment when `|z| < 1e-9` in its uncalibrated units (line 859). The C++ state holds calibrated amplitudes, so `Engine` uses `1e-9 × calibration` of the slot that struck most recently. For a single-slot sequence this is the same test as Python's, and `Engine` keeps it. `FastEngine` uses this too for a bake without the per-slot factor. Since 2026-11-25 it puts the threshold 140 dB below a full hit on that slot instead ("One silence rule: 140 dB below a full hit" below).
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

### Cost of a strike

`cymbalis_bench --dense` plays a strike every 24 ms and times every block; `cymbalis_bench_profile` times the engine's parts as well (`tools/cymbalis_bench/Dense.cpp`, `plugin/dsp/EngineProfile.h`; docs/BUILDS.md). What they show, on the CI Mac (Apple M1, virtual, 3 cores):

- **A block with a strike costs about three times one without** (NOVA RIDE, 44.1 kHz, 128 samples: about 650 µs against 230 µs), at 64 samples five to thirteen times, since a strike's work does not shrink with the block. That is the engine's own spike: it is the same block in every run. The single blocks of 3 to 30 ms seen before are the virtual machine: never the same block in two runs. The dense mode therefore takes the smallest of three runs per block.
- **What a strike does at its sample**, largest first. It brings every ringing component to the strike (per mode the shared oscillator's rotation and decay, then a pass over the 24 components). It starts the free phase at the contact's end (per ringing mode the wander and oscillator anchor, the older components stepped over the contact, its own decay per mode). It evaluates the cymbal-wide state at the strike (wander and contact factors per mode). It merges two components, because in dense playing the pool is full at every strike; the pair search is negligible, the merged decay costs an exp per mode. It adds a second control point at the free start, with the quiet-mode check, and the coupling per mode. With a band envelope (SMOKE FLAT RIDE, HAMMERED THIN RIDE, SWISH) the largest part is restarting the in-frame decay of every envelope strike still in its window on the new control grid: an exp per mode per strike.
- **All of it depends on the strike's time, velocity or the state at that sample**, so it cannot be computed before the strike without changing the output, and strikes stay sample-accurate. What could move: the strike phases come from a sequence that does not depend on the strike, so the next strike's phases and their cos and sin are drawn ahead, a quarter of the modes per `process()` call (`drawPhasesAhead`). The free phase's skip test compares |z|² with the threshold² and calls hypot only within 1e-12 of it.
- **The per-mode exp, sin and cos of a strike run two at a time** (`plugin/dsp/VecMath.h`: fdlibm's algorithms in 128-bit vectors, each value within 1 ulp of libm's, most of them equal). A 1-ulp change of a double almost never reaches a float, and the output stays bit-identical to the libm version in every check (every preset, 44.1 and 96 kHz, 128- and 64-sample blocks, dense and sparse playing, the cascade and dampings on and off, arm64 and x86_64); it is not guaranteed to be.
- **What is left is mostly passes over the 24 components** (bringing them to the strike, stepping them over the contact, the quiet-mode check, two component sums), and the free ring's kernel running from colder caches after them. They grow with the number of modes and components, not with the block, so at 64 samples a strike block stays several times an ordinary one.
- **Denormals** are flushed in `processBlock` (`juce::ScopedNoDenormals`: FTZ and DAZ on x86_64, FZ on arm64) and the bench does the same; `--ftz-off` shows the cost without.

**Checking that a change to `FastEngine` keeps the sound.** `tools/cymbalis_identity/run.sh` compiles the working tree's `FastEngine` and the one at a git ref (default `origin/main`, its class renamed) into one program with `cymbalis_fast`'s flags, and compares their output in memory, sample by sample, with no audio written: every preset, 44.1 and 96 kHz, 128- and 64-sample blocks, a strike every 24 ms for 5 s and then strikes 0.4 s apart at positions in between, first with the bake's tuning and then with the energy cascade, loud-band and re-hit damping on. It prints, per case, whether the output is bit-identical, the largest difference relative to the peak and how many samples differ, and fails if any does. It needs a configured build for the bake loader (`--build DIR`, default `build`); on a Mac, `--arch arm64` and `--arch x86_64` (under Rosetta) check both slices. `--time [--runs N]` instead times both engines in one process on a real-time thread, both rendering each block at its deadline (which one first alternates, so a slow moment of a shared machine falls on both), the smallest of the runs per block: blocks with no strike, one strike and two (a flam), at 128 and 64 samples, 44.1 kHz.

```
cmake --build build --target cymbalis_engine
tools/cymbalis_identity/run.sh --build build                # identity against origin/main
tools/cymbalis_identity/run.sh --build build --arch x86_64  # the Intel slice (Mac)
tools/cymbalis_identity/run.sh --build build --time --runs 4
```

### Multi-cymbal rendering (step 1)

Step 1 of the high-priority "multi-threaded rendering" item (Kris, 2026-11-25). The goal is 4–10 cymbals in one Cymbalis instance. A plugin renders on the host's one audio thread, and a NOVA RIDE strike block costs about 650 µs of a 2.9 ms block (above), so several cymbals struck in one block would miss the deadline on one core. This step adds an engine pool and a kit benchmark to measure it on CI's Mac. **The plugin doesn't use the pool yet:** nothing in the plugin, the sound, the UI or the parameters changes (`tools/cymbalis_identity`: every preset bit for bit as before).

**The pool** (`plugin/dsp/EnginePool.h/.cpp`, library `cymbalis_pool`; plain C++, no JUCE; only `cymbalis_bench` links it):

- **One block.** The caller queues each engine's strikes and pedal moves at their sample offsets, then `render()` renders every engine into its own buffer, split at its events as the plugin splits at MIDI events, and mixes the buffers.
- **The mix, in a fixed order.** The buffers are summed engine 0, 1, 2 … into the output, whichever thread rendered them. Float addition is not associative, so summing in the order the engines finish would change the last bits from run to run. With a fixed order the output is bit-identical at any number of threads:
  - each engine keeps its own state and random sequences;
  - an engine is rendered by one thread at a time;
  - the workers take the calling thread's floating-point mode (flush to zero) before each engine.
- **The hand-out.** One atomic counter hands the engines out, heaviest first, and the calling thread takes engines from it too:
  - **struck in this block:** by their number of strikes, then their ringing modes;
  - **still ringing:** by their ringing modes (`FastEngine::activeModes`: every mode during a contact). It is a field read, as cheap a cost estimate as the engine has; a strike costs far more than a block of ringing (above), hence strikes first;
  - **silent** (`isSilent`) last: about 1 µs a block.

  The counter also carries the block's number, so a worker that wakes late can't take an engine of the next block.
- **Waiting.**
  - **The caller** waits only for engines already in progress on a worker, never for work it could take itself.
  - **When fewer than two engines need rendering** (struck, or not silent), the caller renders the block alone and no worker is woken.
  - **Otherwise** it wakes as many workers as there are engines to share, up to threads − 1.
- **The workers** (threads − 1) are made in `prepare()` (sample rate, block size, engines), never on the audio path. Between blocks a worker:
  - spins for 50 µs (`EnginePool::kDefaultSpinMicros`), then sleeps on a semaphore of its own (macOS: a Mach semaphore, available from macOS 11, unlike `os_sync_wait_on_address`; Linux: a POSIX semaphore);
  - is woken by the caller with a flag exchange and a signal: no lock, no allocation, and no system call on the audio path but that signal.

  **Why 50 µs.**
  - **It is about twice what waking a sleeping real-time thread costs** on a Mac, so a worker never wastes more than about two wake-ups' worth of CPU waiting.
  - **It is a small part of the shortest block** (667 µs at 96 kHz / 64), so it bounds the cost: at most 50 µs per worker per block, 7.5 % of a core at 96 kHz / 64 and 1.7 % at 44.1 kHz / 128.
  - **It bridges the gap to the next block only when that gap is short,** which is when blocks come back to back and the deadline is near. Otherwise the workers sleep and the next block pays a wake-up.

  The bench measures the alternative, workers spinning through the whole block.
- **Real time.**
  - **Policy.** The workers run with the time-constraint policy `cymbalis_bench --dense` gives its own thread (`EnginePool::makeRealtime`): period one block, computation half of it, constraint one block. Linux: SCHED_FIFO where permitted.
  - **The workgroup hook.** `Options::joinWorkgroup` and `leaveWorkgroup` are called once on each worker thread, at its start and its end, for the host's audio workgroup. The plugin will pass JUCE's `AudioWorkgroup` there in a later step. JUCE 7.0.12's AU wrapper delivers it (`AudioProcessor::audioWorkgroupContextChanged`); its VST3 wrapper doesn't, so there the time-constraint policy is the only route. The bench has no workgroup and leaves the hook empty.
- **The engine count** is fixed for the pool's life; changing it means `prepare()` again, off the audio path. `release()` (and the destructor) stop and join every worker.
- **Checked:**
  - a ThreadSanitizer build of the bench on Linux (GCC 13), on short 4-thread kit cases, reports no data race, with workers spinning 0, 5, 50 and 1000 µs;
  - every threaded mix in every bench run is bit-identical to one thread's.

**The kit benchmark** (`cymbalis_bench --kit`, `tools/cymbalis_bench/Kit.cpp`):

- **The kit, in order** (the first N play):
  1. NOVA RIDE
  2. HI-HAT, its pedal moving as in `--dense`
  3. CRASH 16"
  4. KONSTAN CRASH 18"
  5. STINE FLAT RIDE
  6. RAVEN
  7. SWISH
  8. EXTRA PAPER THIN 24"
  9. HAMMERED THIN RIDE
  10. SMOKE FLAT RIDE, from `resources/presets/archive/`, read as a file
- **The patterns.** Each comes from fixed seeds, one random sequence per cymbal, so the first N cymbals play the same at every N and every sample rate.
  - **groove:** 140 bpm.
    - The ride plays 8ths on BOW, one in four on BELL, at velocities 0.45–0.75.
    - The hi-hat plays 8ths on TIP.
    - Each other cymbal crashes on EDGE (0.8–1.0) once every 2 bars, cymbal k on beat k − 3 of the two bars.
    - On bar 1 of every 4, the ride, CRASH 16" and KONSTAN hit on the same sample.
    - It starts one bar early (the lead, in which cymbals 7–10 crash). The timed part starts on bar 1.
  - **stress:** every cymbal is hit every 200 ms, on a random row at a random velocity (0.3–1.0). Every 4th hit lands on the same sample for all cymbals; the others are 20 ms apart from cymbal to cymbal. The lead is 0.8 s, and the timed part starts on a hit of every cymbal.
  - **quiet:** 10 cymbals, the ride playing its groove part and the nine others silent. They are never struck, which is the same state as rung out (`isSilent`). Against the ride alone it gives what a silent cymbal costs per block.
- **The cases:** N = 1, 2, 4, 6 and 10 cymbals; threads 1 up to the performance cores (1 cymbal only at 1 and the most, since the pool never wakes a worker for one); 44.1 and 96 kHz; 128- and 64-sample blocks.
- **Each run:**
  - builds fresh engines, so every run starts from the same state;
  - renders the lead back to back;
  - waits for the workers to fall asleep;
  - then renders the timed seconds block by block, each block waiting for its deadline, on a real-time thread as `--dense` does.
- **Repeats.** Each case runs three times in turn, at every thread count each time, and the smallest time per block is kept: the runner is a virtual machine.
- **Failures.** The run fails if any run's mix differs from its case's one-thread mix by a single bit in any block (lead included), or if a sample is not finite. It never fails on timing.
- **Run length.** The bench defaults to 60 s per run. CI runs 2 s: the whole set at 60 s would take about 6 hours. A step of about 15 minutes keeps the job within its hour.

**Reading it.** One table per pattern, one row per case:

- **Mean, p99, p99.9, worst:** the block time as a percentage of the block's duration. The p99.9 of a 2 s run is close to its worst: 689 blocks at 44.1 kHz / 128, 3000 at 96 kHz / 64.
- **Over 100 % / over 70 %:** blocks that missed the deadline, and blocks with little margin left.
- **Speed-up:** one thread's mean and p99 over this row's.
- **Workers' CPU:** all the workers together, as a percentage of one core, spinning included, and the part spent rendering. The difference is what waiting costs.
- **Wake-up:** from the caller's dispatch to a worker starting, median and p99, and how many wake-ups found the worker asleep. A spinning worker starts in under a microsecond.
- **Blocks shared:** the blocks the workers were woken for.

Then the quiet cymbals, and the 10-cymbal cases with the workers spinning through each block (3 ms), for comparison. A one-thread row is in every table: it is the reference.

**First CI numbers** (run 45648241812, 2026-11-25; Apple M1, virtual, 3 performance cores; 2 s per run, smallest of 3 runs per block; workers sleeping after 50 µs).

- **Bit identity:** all 336 threaded runs are bit-identical to one thread's.
- **The step** took 13.8 minutes and the job 47.6.

Ten cymbals, block time as a percentage of the block (mean / worst / blocks over 100 %):

| Rate / block | Pattern | 1 thread | 2 threads | 3 threads | 3 threads, workers spinning through |
|---|---|---|---|---|---|
| 44.1 kHz / 128 | groove | 36 / 87 / 0 | 24 / 64 / 0 | 17 / 42 / 0 | 8.7 / 19 / 0 |
| 44.1 kHz / 128 | stress | 41 / 168 / 3 | 26 / 83 / 0 | 13 / 44 / 0 | 12 / 34 / 0 |
| 44.1 kHz / 64 | groove | 45 / 174 / 7 | 23 / 80 / 0 | 20 / 75 / 0 | |
| 44.1 kHz / 64 | stress | 42 / 272 / 17 | 23 / 155 / 1 | 22 / 109 / 1 | |
| 96 kHz / 128 | groove | 43 / 102 / 1 | 38 / 125 / 3 | 28 / 114 / 1 | |
| 96 kHz / 128 | stress | 45 / 136 / 7 | 35 / 184 / 2 | 32 / 134 / 2 | |
| 96 kHz / 64 | groove | 36 / 170 / 9 | 29 / 126 / 2 | 17 / 62 / 0 | 17 / 71 / 0 |
| 96 kHz / 64 | stress | 45 / 250 / 28 | 38 / 163 / 48 | 32 / 189 / 6 | 19 / 118 / 2 |

The spinning-through runs have their own one-thread rows: 34 / 84 / 0 and 48 / 158 / 1 at 44.1 kHz / 128, 39 / 162 / 29 and 45 / 292 / 34 at 96 kHz / 64.

What they say:

- **How much of the difference is the runner's.**
  - With one cymbal the pool never shares a block, so its rows at 1 and at 3 threads do the same work. Yet their means differ by up to 1.6× (stress, 44.1 kHz / 128: 12.7 % against 7.7 %) and their worst blocks by up to 1.7×. In 7 of the 8 pairs the one-thread row is the slower, which inflates the speed-ups somewhat.
  - The same one-thread case measured in the two steps differs by up to 16 % in mean and 17 % in worst block.
  - So, at 2 s per run on this virtual machine, differences under about 1.5× between rows are noise. The large ones hold: blocks over 100 % going from dozens to a few, worst blocks halving.
- **Where one thread misses the deadline.**
  - **44.1 kHz / 128:** never in the groove up to 10 cymbals (worst 87 %). In the stress pattern from 6 cymbals: 1 block at 6, 3 at 10 (worst 168 %).
  - **44.1 kHz / 64:** from 4 cymbals in both patterns. At 10: 7 blocks in the groove, 17 in the stress pattern (worst 272 %).
  - **96 kHz / 128:** from 2 cymbals (worst 232 % in the groove, the ride and hi-hat struck in one block).
  - **96 kHz / 64:** already with one: 6–7 blocks over, worst 152 %. A single NOVA strike outruns a 667 µs block (`--dense`, same run: NOVA's worst block is 238 % there). The pool can't help, since the unit of work is one engine for one block.
- **What each thread buys.**
  - **44.1 kHz:** a second thread clears the misses but one at 10 cymbals, 64 samples; the mean speed-up is about 1.5–2× at 10 cymbals. A third thread brings the worst blocks to 42–44 % at 128 samples (75–109 % at 64), with mean speed-ups of 1.9–3.1×.
  - **96 kHz:** less. 2 threads give 1.1–1.3×, 3 threads 1.4–2.1×, and blocks over 100 % remain at 64 samples.
- **What waiting costs.**
  - **Sleeping (50 µs spin), the default:**
    - **wake-up:** median 27–44 µs, p99 110–340 µs at 96 kHz / 64 and up to 1 ms at 44.1 kHz / 128 (a longer sleep, a slower wake on this virtual machine);
    - **CPU:** the workers' CPU beyond rendering is 2–18 % of a core;
    - **share of wake-ups:** 83–100 % of them found the worker asleep.

    At 96 kHz / 64, a 30–40 µs wake-up is 5–6 % of the block, and the p99 up to half of it. That is why 2 threads there did no better than 1 in the stress pattern (48 blocks over against 28).
  - **Spinning through (3 ms):** the wake-up is 0.3–0.4 µs, and the misses go (96 kHz / 64 stress: 2 blocks over 100 %, 3 over 70 %, against 6 and 69 sleeping). But the two workers use about two whole cores (180–200 % of one) to render 19–43 % of one. In a host that is 1.6–1.8 cores taken from every other track and plugin. Rendering itself also got faster when the cores were kept busy (44.1 kHz / 128 groove: mean 8.7 % against 16.7 % sleeping), so this virtual machine pays for idle cores waking up. Real hardware, and an audio workgroup (AU), may pay less.

    The choice between the two, or something in between, is for a later step, measured in a host:
    - spin only after a heavy block;
    - spin until the block's expected end;
    - the workgroup.
- **Quiet cymbals.**
  - **Cost:** one silent cymbal costs between −2 and +14 µs a block here. That spread is noise: the same case's one- and three-thread rows differ by more than the nine silent cymbals add. Locally the cost is 1–2.5 µs, so a silent cymbal is close to free (under 0.5 % of a block).
  - **What does cost:** cymbals in their quiet tails (above), which can last 70–110 s after a hit at −130 dB and below. The project session's "about 3 % each" was most likely such ringing, not silence.

**Local numbers (Linux container, Intel Xeon 2.1 GHz, 4 cores, a virtual machine; not the target).** 44.1 kHz / 128, 10 cymbals, blocks back to back:

| Threads | groove: mean | stress: mean | stress: worst |
|---|---|---|---|
| 1 | 28 % | 35 % | 89 % |
| 4 | 9.6 % | 11.7 % | 31 % |

- **Speed-ups:** about 1.9× with 2 threads, 2.5× with 3 and 3× with 4.
- **A sleeping worker wakes in about 13–50 µs** (p99 40–160 µs); a spinning one in 0.5 µs.
- **A silent cymbal costs about 1–2.5 µs a block.**

**What makes splitting harder** (nothing changed here):

- **The engine shares nothing between instances.** No static or global state; the bake is shared read-only. So engines render in parallel without locks.
- **The profile build's part timers are global** (`EngineProfile.h`, "for one engine on one thread"), so `--kit` is in `cymbalis_bench` only, not in `cymbalis_bench_profile`.
- **`FastEngine` must not be copied.** Its implicit copy constructor exists, but a stick voice points into its own engine's noise buffer (`StickVoice::noise`), so a copy would read the original's. The pool holds pointers and never copies; the bench builds a fresh engine per run.
- **The unit of work is one engine for one block.** One cymbal's strike can't be split across threads, so a block in which a single NOVA strike outruns the deadline stays over it at any thread count. Locally, at 96 kHz / 64, one cymbal's worst block was 103–110 % at every thread count.
- **Quiet tails are rendered for a long time.** A component stops being rendered below 1e-9 × its level. This is what step 1 measured; step 3, "One silence rule: 140 dB below a full hit" below, has the rule since.
  - **One strike at 0.8 on each row** (44.1 kHz) rings until `isSilent` for:
    - CRASH 16": 72–78 s;
    - EXTRA PAPER THIN 24": 81–101 s;
    - STINE FLAT RIDE: 102–113 s;
    - KONSTAN: 40–48 s.

    For CRASH 16", EXTRA PAPER THIN and STINE, everything after 60 s is 130–180 dB below full scale.
  - **After 4 s of strikes** (every 200 ms, each row in turn), the others are silent within 26–33 s (HI-HAT within 2 s). CRASH 16", EXTRA PAPER THIN and STINE are still not silent 60 s later.
  - **The cost:** a cymbal in such a tail costs about 13 µs a block at 44.1 kHz / 128 (locally), against about 1 µs silent.

  This was not changed in step 1: the skip threshold is part of the sound. Step 3 changed it, with Kris's approval.

### Multi-cymbal rendering (step 2): the shared job pool

Step 2 (Kris, 2026-11-25) of the plan "Cymbalis + Tympanis: one physically modelled kit". The pool from step 1 becomes instrument-neutral: **dhpool** (`shared/dhpool/`, `shared/dhpool/README-pool.md`), a job pool that can schedule Cymbalis's cymbals and Tympanis's drums alike. `EnginePool` is rebuilt on top of it. **The plugin still doesn't use the pool:** nothing changes in the plugin, the sound, the UI or the parameters (`tools/cymbalis_identity`: every preset bit for bit as before).

**Why scheduling and mixing are separate now.**
- **What step 1's pool owned:** `FastEngine` only, plus the events, the float buffers and the mix.
- **What Tympanis needs instead:**
  - its drums render in double precision through its own `Kit`;
  - its snare buzz is a coupling pass that runs after every drum has rendered (`Kit::renderChunk`, Tympanis `plugin/dsp/Engine.cpp`). Each snare is driven by the rest of the kit's mix, the snares in order, each hearing the earlier snares' sympathetic ring and wires.

  A pool that mixes can't do that.
- **dhpool only hands out jobs:** `run(count, order, fn, ctx)` calls `fn(ctx, order[k])` for every k, on the calling thread and the workers, and returns when all are done. `fn` is a plain function pointer, so nothing is allocated.
- **The caller owns everything else:** the events, the buffers (float or double), the hand-out order, the mix, and any serial work between runs.

**What dhpool keeps from step 1:**
- the atomic hand-out with a generation, so a late worker never takes a job of another run;
- the spin-then-sleep handshake, with no wake-up lost or left over;
- the time-constraint policy on the workers. The period is now an option (one block), since the pool no longer knows the block;
- no lock, allocation or system call on the audio path but the wake-up signal;
- a clean shutdown;
- with `count < 2` or one thread, the caller runs the jobs alone and wakes no worker. Each run hands the caller's floating-point control to the workers, as before.

**New in dhpool:**
- **Several runs per block.**
  - `run()` may be called more than once in a block, for example the drums, then the snare pass on the caller, then more parallel work.
  - After its last job a worker spins for 50 µs before it sleeps, so a run that follows a short serial pass finds it awake.
  - In the self-test (two runs and two serial passes per block, a 1.5 ms pause every 8th block), only the first run after each pause needed a worker woken from sleep: 256 of 3966 wake-ups at 2 threads, 2000 blocks.
  - A serial pass longer than the spin puts the workers to sleep, and the next run pays a wake-up (30–40 µs on CI's Mac, step 1).
- **The workgroup generation.**
  - **Step 1's limit:** each worker joined its workgroup once, at its start.
  - **The fix:** `setWorkgroupHook(join, leave)` may now be called at any time off the audio path, as from JUCE's `audioWorkgroupContextChanged`, which hosts can call more than once. It publishes a new hook pair and bumps a generation.
  - **Each worker** checks the generation at every wake-up and, if it changed, leaves the workgroup it joined and joins the new one before it takes a job. A worker joins the current hook at its start and leaves it at its end.
  - **The calling thread never joins.** Hook pairs no worker holds any more are freed at `release()`. They are few: one per call.
- **The caller's wait.** In a shared run, the time from the hand-out counter running out (the caller has nothing left to take) to the last job still on a worker returning. It is the cost of a worker being slow, late or preempted mid-job, which the review of #74 asked to see. It is kept with the wake-up delays when timing is asked for, and `cymbalis_bench --kit` reports its median, p99 and max per case.
- **Size.** At most 4095 jobs per run: the counter holds a 40-bit generation, a 12-bit count and a 12-bit next job.

**`EnginePool` on dhpool.** Its interface and behaviour stay:
- the per-engine event queues and float buffers;
- the hand-out order: struck, then ringing, then silent;
- the fixed-order mix;
- one thread when fewer than two engines need rendering.

What changed:
- **The run holds only the engines that need rendering.** The caller renders the silent ones itself after the run. Their cost is about 1 µs each. Step 1 put them at the end of the hand-out, so the caller almost always took them anyway. This keeps step 1's number of workers woken: one per engine to share, up to threads − 1.
- **`setWorkgroupHook`** passes dhpool's through, and `callerWaitsMicros()` is new.
- **Bit identity is checked against step 1.** `tools/cymbalis_bench/EnginePool74.*` is a copy of step 1's pool, unchanged but for its name and kept for the bench only. For every `--kit` case, the bench renders the same events through it at the case's most threads (blocks back to back) and requires the new pool's one-thread mix to match it in every block, lead included.

**Checked:**
- **ThreadSanitizer** (GCC 13, Linux): the self-test (full and `--quick`) and 4-thread `--kit` cases (stress and groove, 4 cymbals; spin 0, 50 and 1000 µs; paced and back to back) report no data race.
- **The self-test** (`dhpool_selftest`, also in CI): every job exactly once per run; the same bits at 1–4 threads with flush to zero on and off; two runs per block; workgroup hook changes reaching every worker, never the caller, each join with its leave; 60 prepare / release cycles leaving no thread behind.
  - **It catches a pool without the floating-point hand-over.** Checked with a copy of the pool with that line removed: its results differ at 2–4 threads.
  - **Step 1's bench never tested that hand-over:** its workers were created after the bench turned flush to zero on, and a new thread takes its creator's mode. The self-test now starts the workers in the other mode.
- **Locally,** every `--kit` case is bit-identical at 2–4 threads to one thread, and to step 1's pool (44 cases).

**First CI numbers** (run 45664324717, 2026-11-25; Apple M1, virtual, 3 cores; against step 1's run 45648241812).

- **Checks.** All 336 threaded runs are bit-identical to one thread, and all 48 cases' one-thread mixes to step 1's pool. The self-test passed, in 0.3 minutes.
- **Time.** The kit step took 14.1 minutes (step 1: 13.8), the job 47.9 (47.6).

Ten cymbals, mean / worst / blocks over 100 % (% of the block), step 1 → step 2, and step 2's caller wait at 3 threads (median / p99 / max, µs):

| Rate / block | Pattern | 2 threads | 3 threads | Caller's wait, 3 threads |
|---|---|---|---|---|
| 44.1 kHz / 128 | groove | 24 / 64 / 0 → 23 / 52 / 0 | 17 / 42 / 0 → 15 / 40 / 0 | 0.2 / 305 / 1816 |
| 44.1 kHz / 128 | stress | 26 / 83 / 0 → 33 / 90 / 0 | 13 / 44 / 0 → 16 / 36 / 0 | 58 / 283 / 2735 |
| 44.1 kHz / 64 | groove | 23 / 80 / 0 → 26 / 159 / 1 | 20 / 74 / 0 → 17 / 74 / 0 | 10 / 270 / 9991 |
| 44.1 kHz / 64 | stress | 23 / 155 / 1 → 25 / 99 / 0 | 22 / 109 / 1 → 23 / 109 / 1 | 16 / 427 / 5074 |
| 96 kHz / 128 | groove | 38 / 125 / 3 → 43 / 108 / 2 | 28 / 114 / 1 → 29 / 93 / 0 | 34 / 886 / 5662 |
| 96 kHz / 128 | stress | 35 / 184 / 2 → 34 / 191 / 1 | 32 / 134 / 2 → 26 / 129 / 1 | 30 / 650 / 6668 |
| 96 kHz / 64 | groove | 29 / 126 / 2 → 29 / 164 / 3 | 17 / 62 / 0 → 21 / 70 / 0 | 9.3 / 185 / 6536 |
| 96 kHz / 64 | stress | 38 / 163 / 48 → 38 / 194 / 23 | 32 / 189 / 6 → 32 / 191 / 5 | 20 / 301 / 6554 |

What they say:

- **No measurable change from step 1.**
  - Over all groove and stress cases, the mean block time is within 1 % of step 1's at 2 and 3 threads (geometric mean of the ratios: 1.01 and 1.01), and the worst blocks 4 % lower (0.96).
  - Blocks over 100 %: 98 → 64 at 2 threads, 54 → 49 at 3.
  - The one-thread rows, whose code did not change, move as much: 0.99 in mean, 275 → 278 blocks over. So these differences are the runner's.
- **The caller's wait, in shared blocks, pooled over the three runs:**
  - **Median:** usually about 0.1 µs (the caller finishes last or waits for nothing). It is 28–77 µs in a few cases, where a worker holds the heaviest cymbal, a NOVA strike, while the caller finishes the light ones.
  - **p99:** 20–890 µs, typically about 220 µs.
  - **Max:** 0.3 to 12 ms in every threaded case, typically about 4 ms. That is a worker stopped mid-job for several milliseconds. On this virtual machine a thread on the deadline sometimes loses its core for that long, and the one-thread runs see the same directly (`--dense`'s worst block per run: 3–27 ms).
  - **Why the block tables don't show them:** the block tables keep the smallest of the three runs per block, and no stall falls on the same block twice. The caller-wait stat, pooled over the runs, shows what each such stall costs once a cymbal is on another thread: the caller can't take back an engine in the middle of its block.
  - **What could reduce them** is for a later step, measured in a host on real hardware: the host's audio workgroup (the hook is ready), smaller jobs, or the caller taking the heaviest engine itself before waking the workers.
- **Wake-ups:** median 27–28 µs, as in step 1 (27–44 µs).
- **Quiet cymbals:** again within the noise (−12 to +12 µs per silent cymbal).

**Driving Tympanis's `Kit` with dhpool.** Read at Tympanis `ab23ec8`; nothing in Tympanis is changed here.
- **`Kit::renderChunk` does everything in one private function:** each drum into its own double buffer, the mix, the snare pass and the stereo gains. dhpool can't be put under it from outside. `Kit` would need its stages exposed:
  - rendering one drum into its buffer, which becomes the job;
  - the mix, the snare pass and the stereo mix, which stay on the caller after the run.
- **The mix stays bit-identical.** `renderChunk` adds each drum into the mix in drum order as it goes, so summing the drums' buffers in drum order after a parallel run gives the same bits. `Drum::render` adds into its buffer, so each job clears its own first, as `renderChunk` does now.
- **The snare pass is serial by nature.** Each snare is driven by the mix with the earlier snares' sympathetic head and wires already in it, so the snares can't render in parallel without changing the sound. The pass stays on the caller between runs. It is cheap: per snare, its head modes × the samples, plus a band-passed noise bank.
- **Many short runs.**
  - Tympanis's `processBlock` renders each stretch between note-ons separately, and `renderStereo` splits a stretch into 1024-sample chunks. Each piece would be one run, so a flam makes 2–3 runs in a block. With the spin, only the first needs a wake-up.
  - A piece of a few samples (two hits close together) costs a whole dispatch for little work. A size below which the caller renders alone may be worth having; a later step, measured.
- **No cost estimate yet.** `Drum` has `silent()` but no cheap measure of its work: its active-mode count is private. Heaviest first would need a getter.
- **No shared state found.** No static or global state in Tympanis's engine; the kit data is shared read-only; every drum has its own noise generators. Double buffers make no difference to dhpool.
- **A question for the merge:** whether the cymbals should drive the snares' buzz too. If they should, the snare pass needs the whole kit's mix, cymbals included (float into double), before it runs.

### One silence rule: 140 dB below a full hit (step 3)

Step 3 of the plan "Cymbalis + Tympanis: one physically modelled kit". **Kris approved 140 dB on 2026-11-25.** `FastEngine` now stops rendering what has decayed 140 dB below a full hit, as Tympanis does: its −140 dB gate clears and skips a drum's slot that has decayed 140 dB below a full hit. The two engines then behave alike in the kit.

This is a sound change by definition, but only far below hearing. No parameter, UI or tuning changes. The bakes gain one field per slot.

**Before.** A mode or component stopped being rendered below the skip threshold, 1e-9 × the level (calibration × trim) of the last slot struck: Python's `amp_skip_threshold`, scaled to calibrated amplitudes. Against a velocity-1.0 hit on that slot, that was 159–197 dB down, depending on the slot (the table below). Quiet tails rang for up to 113 s ("Multi-cymbal rendering (step 1)", "Quiet tails are rendered for a long time").

**The measurement: why each slot has its own factor.**
- **What was measured:** per preset and slot, the peak of a velocity-1.0 hit at the slot's base position against the slot's level, with `tools/cymbalis_trims --skip-thresholds`:
  - the median peak of 16 hits, each with the next phase draw, first second, both channels;
  - HIT VARIATION off;
  - no trim, since a trim scales the peak and the level alike.
- **The result:** the ratios span −21.1 dB (HI-HAT TIP) to +16.7 dB (SWISH EDGE), 37.8 dB, not within ±6 dB of one value.
- **Why they spread:** the trims bring every slot's peak close to the others (0.07–0.27 at the plugin's level), while the levels themselves range from 0.03 to 2.45.
- **The consequence:** one constant would have left some slots' thresholds over 20 dB from 140 dB. So each slot gets its own factor:
  - **Where it lives:** `skip_threshold.rel` in the bake, stored by `tools/set_skip_thresholds.py` with the measurement beside it (`docs/BUILDS.md`).
  - **What it gives:** rel × calibration × trim is 140 dB below that slot's full hit.
  - **How the engine uses it:** in place of `amp_skip_threshold`, for the last slot struck, as before.
- **A bake without the factor** keeps `amp_skip_threshold` × level.

| Preset | Full-hit peak re level, by row (dB) | Old threshold re full hit (dB) |
|---|---|---|
| NOVA RIDE | BELL −15.0, BOW −8.1, EDGE −1.2, BELL2 −20.5, BOW2 +0.1 | −159.5 to −180.1 |
| HI-HAT | TIP −21.1, EDGE −11.2, FOOT −19.3 | −158.9 to −168.8 |
| CRASH 16" | BELL −9.5, BOW +5.4, EDGE +6.2 (shoulders as their rows) | −170.5 to −186.2 |
| KONSTAN CRASH 18" | BELL −16.1, BOW +12.1, EDGE +6.1 | −163.9 to −192.1 |
| STINE FLAT RIDE | BOW +14.0, EDGE −2.5, CENTER +13.4 | −177.5 to −194.0 |
| RAVEN | BELL −2.6, BOW +12.0, EDGE +4.4 | −177.4 to −192.0 |
| SWISH | BELL +5.2, BOW +7.3, BOW_SHOULDER +5.5, EDGE +16.7 | −185.2 to −196.7 |
| EXTRA PAPER THIN 24" | BELL +0.4, BOW +14.3, EDGE −0.2 | −179.8 to −194.3 |
| HAMMERED THIN RIDE | BELL −3.6, BOW +8.4, EDGE +3.5 | −176.4 to −188.4 |
| SMOKE FLAT RIDE (archive) | BOW and CENTER +10.7, EDGE +7.7 | −187.7 to −190.7 |

**Where it applies.** Every use of the skip threshold moves together:
- the segment threshold at the start of each free phase, which drops a mode and clears what the components and contacts left in it;
- the periodic drop of quiet modes and of components below it in every mode, every few control blocks.

Nothing else changes: the onset, the cascade, the dampings, the build-up, the hi-hat's choke and ring floor, and the NPZ edge envelopes. `Engine`, the Python-matching path, keeps 1e-9: the Python engine is locked and `Engine` mirrors it.

**The HI-HAT does not change at all.** Its strikes end by their own rules, at 120 dB under the strike's peak or after 8 s ("Hi-hat"), before the skip threshold matters. Its output is bit for bit as before in every check below, and its factors are stored only so that every slot has one.

**Checked in memory against `main`** (`tools/cymbalis_identity/run.sh --bound`, at 44.1 and 96 kHz, 128-sample blocks). Each sequence was rendered by both engines and compared as it went:
- **Single hits:** every row at velocities 0.2, 0.5 and 1.0, 120 s each.
- **Dense:** the `--dense` pattern for 60 s, with the bake's build-up, cascade and dampings, and the pedal moving on HI-HAT.

| Preset | Single hits: worst 10 ms window, 44.1 / 96 kHz (dB re full BOW hit) | Dense | RMS in the first 30 s, largest deviation |
|---|---|---|---|
| NOVA RIDE | −134.1 / −134.2 | −124.2 / −124.5 | 0.0004 dB |
| HI-HAT (re TIP) | identical | identical | 0 |
| CRASH 16" | −140.6 / −140.7 | identical | 0.0003 dB |
| KONSTAN CRASH 18" | −140.8 / −140.8 | identical | 0.0006 dB |
| STINE FLAT RIDE | −142.6 / −142.9 | identical | 0.0005 dB |
| RAVEN | −140.9 / −141.1 | identical | 0.0023 dB |
| SWISH | −139.3 / −141.5 | identical | 0.0074 dB |
| EXTRA PAPER THIN 24" | −140.4 / −140.5 | identical | 0.0005 dB |
| HAMMERED THIN RIDE | −138.0 / −138.1 | identical | 0.0022 dB |

- **The difference stays inside the bound.** Its RMS over 10 ms windows stays under −120 dB re a velocity-1.0 BOW hit (HI-HAT: TIP), as `main` plays it, everywhere: at worst −124.2 dB (NOVA, dense), and −134 to −143 dB for single hits. The largest single sample is −119.7 dB (NOVA, 96 kHz), far below 24-bit resolution at the plugin's output level.
- **No step at a drop.** A mode dropped at −140 dB leaves a step under −140 dB of the full hit, and no window around a drop shows more than that.
- **Loudness is unchanged.** In the first 30 s after each hit, the RMS of every 1 s window where `main`'s is above −100 dB re the hit is within 0.0074 dB of `main`'s (bound 0.01 dB).
- **Why dense playing barely changes:** strikes every 24 ms keep almost every mode above both thresholds. Only NOVA differs there, by −124 dB. Most likely, among its 2270 modes, some are excited by a strike below the new threshold and are dropped at the segment's start. Each is under −140 dB, and together they give the −124 dB. This is not traced mode by mode.

**The tails, one hit at 0.8 on each row, 44.1 kHz,** time until `isSilent`, `main` → now:
- CRASH 16": 72–78 → 53–57 s;
- EXTRA PAPER THIN 24": 81–101 → 57–68 s;
- STINE FLAT RIDE: 102–113 → 66–88 s;
- KONSTAN CRASH 18": 40–48 → 33–35 s;
- NOVA RIDE: 19–27 → 16–21 s;
- RAVEN: 27–33 → 21–24 s;
- SWISH: 29–30 → 21 s;
- HAMMERED THIN RIDE: 27–30 → 21–23 s;
- HI-HAT: 1.8–2.3 s, unchanged.

The long rides still ring for a minute or more. Their slowest modes (T60 of 25–40 s) need that long to fall 140 dB below the hit, and the rule keeps them until then. The cost of a cymbal in its tail falls as its modes drop one by one, and the tail ends 20–35 s sooner than before.

**Cost.**
- **One hit's tail, timed directly** (a BOW hit at 0.8, 120 s at 44.1 kHz / 128, both engines in one program, smallest of 3 runs, the Linux container):
  - the whole ring costs 18–35 % less CPU;
  - at 20–30 s after the hit a block costs a third to two-thirds less;
  - by 40 s both engines are close to the silent floor of about 1 µs a block.

| Preset | CPU for the 120 s, main → now (ms) | µs per block at 20–30 s | at 40–50 s |
|---|---|---|---|
| NOVA RIDE | 217 → 179 | 2.8 → 1.8 | 1.7 → 1.8 |
| CRASH 16" | 192 → 145 | 3.3 → 1.8 | 1.5 → 1.4 |
| KONSTAN CRASH 18" | 205 → 153 | 3.6 → 1.6 | 1.3 → 0.9 |
| STINE FLAT RIDE | 256 → 193 | 7.5 → 4.0 | 2.4 → 1.6 |
| RAVEN | 163 → 125 | 3.1 → 1.0 | 1.0 → 1.0 |
| SWISH | 150 → 105 | 1.9 → 0.9 | 0.9 → 0.9 |
| EXTRA PAPER THIN 24" | 304 → 214 | 7.5 → 3.2 | 2.5 → 1.7 |
| HAMMERED THIN RIDE | 275 → 178 | 1.9 → 1.0 | 0.9 → 0.9 |

- **On CI** (run 45685356961, against #75's run 45664324717):
  - **Real time:** every preset stays at 3× real time or better. The CPU benchmark's worst is NOVA at 96 kHz, 13.7 % of a core (#75's run: 16.5 %).
  - **`cymbalis_bench --kit` shows no change it can measure, and none was expected.** Its quiet cymbals were never struck, so the rule doesn't touch them. Its groove and stress patterns strike every cymbal within seconds, so no tail comes near −140 dB inside a 2 s window.
  - **The differences are the runner's.** The kit's means moved by +4 to +18 % against #75's run, and its blocks over 100 % by up to ±70, in both directions. The one-thread rows, whose work the rule leaves unchanged inside those windows, moved just as much.
  - **Every threaded mix was again bit-identical to one thread, and to #74's pool,** for 316 + 20 runs and 48 cases.

## 602 and Bettis (Smoke Flat Ride, Hammered Thin Ride)

Added 2026-10-15. SMOKE FLAT RIDE was in the plugin until 2026-11-23, when STINE FLAT RIDE took its place ("Stine Flat Ride" below); its bake is kept in `resources/presets/archive/`. The plugin is the reference (`CLAUDE.md`): these presets were ported with the Python engine as a guide and are checked by ear, with no render comparison. Both are baked with `nl_amount = 0`, as Nova is (Bettis's JSON sets 1.0). Nova Ride's bake is unchanged, and so is every code path it uses: each new mechanism is used only when a bake carries its data.

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
   - The 602 stays at EDGE VOICE FROM BOW 0 % with its EDGE trim −0.05 dB (Kris, 2026-10-31). At 100 % it would be −0.60 dB: the voiced edge is 0.55 dB louder over the first 500 ms.

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

1. **Slots** (`docs/UI_SPEC.md` section 8). BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow_tip` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). BELL and BOW start on TIP, BELL2 and BOW2 on SHOULDER (`docs/UI_SPEC.md` section 8).
2. **Level trims**, as for the 602 and Bettis (their decision 5): velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their SPEAR targets (Bow V6, Bell V3, Edge V7: BELL +5.3 dB and EDGE +4.7 dB above BOW), capped where one hit (99th percentile of 1024 hits) would peak above 0.9. `bow_shoulder` takes BOW's target and `bell_shoulder` BELL's. Measured with `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=5.3 --spear-target BELL_SHOULDER=5.3 --spear-target EDGE=4.7 --peak-limit 0.9`. Each bake entry records its target, cap and reason (`slots[].level_trim`).

   | Entry | SPEAR target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile) | Capped |
   |---|---|---|---|---|---|
   | BOW (`bow_tip`) | 0 dB | +8.86 dB | 0 dB | 1.45 (with the click) | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | +8.96 dB | 0 dB | 1.11 | no (BOW's target) |
   | BELL | +5.3 dB | +7.60 dB | −0.23 dB | 0.90 | yes (+13.13 dB would peak at 1.70) |
   | BELL_SHOULDER | +5.3 dB (BELL's) | +7.60 dB | −0.23 dB | 0.90 | yes (as BELL) |
   | EDGE | +4.7 dB | −3.44 dB | −0.77 dB | 0.90 | yes (+2.03 dB would peak at 1.69) |

3. **Seed.** The strike-phase sequence starts from Swish's seed, 7.
4. **The edge explodes on impact** (2026-10-29, Kris's choice by ear; `docs/TUNING.md`, "Per articulation"). Swish's edge envelopes start 4–10 dB under their peak in every band and swell to it over 120–480 ms: a velocity-1.0 edge hit's loudest 10 ms came about 200 ms in, 7.5 dB above its first 10 ms. It sounded like a synth envelope rather than a China's edge. Its stick click could not help: `stick_edge_weight` is 0 in every Swish slot. The click's position weights are now per-articulation tuning values, and Swish's `edge` has, in its tuning block and attack data:
    - a stick click: edge weight 1.0, gain 6.0, band 1000–10000 Hz, 12 ms, decay 5 ms;
    - attack gains, both layers: 0 dB for 89–356 Hz, +4 dB for 449–898 Hz, +8 dB for 1131–2263 Hz, +10 dB for 2851–11404 Hz, with an attack time of 25 ms.

   A hard edge hit now peaks in its first 10 ms; after 0.43 s it plays bit for bit as before, so the recorded swell follows unchanged. EDGE's trim stays −3.44 dB: measured again it would be −3.88 dB (capped), since the click raises the hit's peak about 0.4 dB above the cap.

### Things in the Swish data or engine that look wrong (ported as they are)

- **The NPZ velw is the same at all six velocities.** An edge strike's spectrum therefore does not change with velocity through the captures; only its level (velocity^1.0) and the envelope's interpolation between velocities do. The NPZ notes call velw "per-band audio onset shape from actual recordings (0-50ms per velocity)", but the engine uses it as each mode's coupling weight.
- **The edge excitation ratios are neutral** (all 1.0, "Pending real edge characterisation"), so a bow strike blended towards the edge has no position weighting. `edge_rms_correction` is all 1.0 too.
- **The NPZ envelopes are shorter than the 20 s window.** They run 11.7–14 s (587–700 frames), are zero-padded, and end at 0.02–0.2 % of their peak. After the window a strike rings at its own decay, by then at least 120 dB down (T60 at most 10 s), so the jump at 20 s is inaudible.
- **The outer envelope bands hold far more than their range.** Band 0 holds every mode below 125.9 Hz (106 modes, down to 19 Hz), band 19 every mode above 7.94 kHz (209 modes, up to 23.7 kHz); they follow the envelopes of the 100–126 Hz and 7.9–10 kHz bands.
- **14 modes lie above 22.05 kHz** (up to 23.68 kHz). At 44.1 kHz they fold back below Nyquist, as in Python; the 602 has 8 such modes.
- **pk_t runs up to 5 s**, bow and edge. With the envelope on, the edge pk_t and the edge velocity tilt are inaudible (the envelope skips the ramp, as for Bettis). With EDGE ONSET ENVELOPE off, some edge modes ramp in over up to 5 s. The bow pk_t (BowV4 at velocity 0.65) applies only to strikes blended towards the edge.
- **The calibration does not see the envelope or the click.** The calibration strike (`_compute_calib_scalar`) is an edge strike without the band envelope and without `_add_transient`; the trims make up for the level.
- **BOW (tip) peaks at 1.45 at velocity 1.0** (99th percentile, with its trim) before the Volume knob, from the click on top of the strike. BOW is never capped, by the rule; BOW_SHOULDER, without the click, peaks at 1.11.
- **The click is bow-only** in the data. `stick_bell_weight` and `stick_edge_weight` are 0: there is no click at the bell, and a bow row blended towards the edge clicks less, with its bow fraction. The edge has its own click since 2026-10-29 (decision 4).

## Raven (20" dry ride)

Added 2026-10-27. Raven was built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, Raven) in the Bettis format, and needs no new engine code: every mechanism it uses is one the 602, Bettis and Swish already have. It is baked with `nl_amount = 0`, as its JSON sets. NOVA RIDE, SMOKE FLAT RIDE, HAMMERED THIN RIDE and SWISH bake and play bit for bit as before (checked: re-baked with the new tools they are identical but for `created_utc`, and `tools/cymbalis_identity` finds the engine's output identical to main's in memory).

### What Raven uses

| | Raven (`Raven_Baseline1.json`) |
|---|---|
| Modes | 900, 34 Hz–19.8 kHz (none above 22.05 kHz). 682 seen in at least two bow captures; 218 seen only in the edge captures, with `exc` 0: they sound only on edge strikes, through the NPZ velw |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (7 velocities, 0.119–1.0) and the NPZ band envelope, switched on by the all-zero trigger file (as the 602's): 22 bands, exactly the plugin's thirds, 8.7 s of data, 9.2 s window, `edge_rms_correction` per band, level exponent `vel_gain_power` 1.0 |
| Bow and bell strikes | standard coupling with the neutral edge ratios (all 1) and the bell ratios (from the bell captures) as position weights |
| Onset ramp data | bow pk_t, velocity-bracketed edge pk_t (7 velocities), edge velocity tilt (4 bands) |
| Contact | none (`permode_contact` false, `tip_contact_ms` 0): instantaneous strikes |
| MF / LF accumulators | MF in boost mode (depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2); LF in boost mode (depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1), applied as live-engine decision 6. As Swish |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, stick click, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

Every other key either has its default effect or is not read by the engine (`alpha_contact`, `stick_hp`, `atk_len`, `atk_dec`). With NPZ data, the engine is built as for the 602 and Bettis (their decisions 2 and 3): the shared oscillator carries no decay, and the control block divides the 20 ms frame.

### Bake and plugin

- **Bake** (`tools/bake_preset.py --preset raven`; `tools/ab_common.py` `PRESETS`): `BELL` (1.0), `BOW` (0.5) and `EDGE` (0.0), no overlays, and the copies `BELL_SHOULDER` (`bell_shoulder`, from `BELL`) and `BOW_SHOULDER` (`bow_shoulder`, from `BOW`) (`COPIED_ENTRIES`), as Bettis. No `ENVELOPE_ON` entry: the trigger file switches the envelope on (`edge_npz.envelope_active` true). Every entry has the same calibration scalar, 0.0766 (Python's calibration strike is an edge strike at velocity 1.0).
- **Preset** RAVEN, after SWISH (`plugin/SlotTable.h`): HAMMERED THIN RIDE's rows (`docs/UI_SPEC.md` section 8); photo `Raven.png`; hit display bell zone 0.29 of the photo's radius, measured from the photo.

### Live-engine decisions for Raven

1. **Slots.** BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). BELL and BOW start on TIP, BELL2 and BOW2 on SHOULDER (`docs/UI_SPEC.md` section 8). Raven has no shoulder captures for the bell and bow: the copies are tip data, to be tuned by ear (`docs/TUNING.md`; they have the NOVA SHOULDER PROFILE button).
2. **Level trims**, as for the 602, Bettis and Swish (their decision 5): velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their SPEAR targets (Bell V4 and Edge V7 against Bow V7, `data/raven_build_info.json`: BELL +4.72 dB and EDGE +16.23 dB above BOW), capped where one hit (99th percentile of 1024 hits) would peak above 0.9 before the output gain. `bell_shoulder` takes BELL's target; the copies come out with their sources' trims, since they have their data. The tuning block (below) was applied first: the trims tool measures Nova's BOW with Nova's −11 dB output gain, so the cap is 0.25365 after Raven's (0.9 × 10^(−11/20)), and a bake without the block would have been trimmed 11 dB too low. Measured with `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=4.72 --spear-target BELL_SHOULDER=4.72 --spear-target EDGE=16.23 --peak-limit 0.25365`. Each bake entry records its target, cap and reason (`slots[].level_trim`).

   | Entry | SPEAR target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile), before the output gain | Capped |
   |---|---|---|---|---|---|
   | BOW | 0 dB | −1.23 dB | 0 dB | 1.26 | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | −1.23 dB | 0 dB | 1.26 | no (BOW's target) |
   | BELL | +4.72 dB | +10.61 dB | −2.44 dB | 0.90 | yes (+17.77 dB would peak at 2.06) |
   | BELL_SHOULDER | +4.72 dB (BELL's) | +10.61 dB | −2.44 dB | 0.90 | yes (as BELL) |
   | EDGE | +16.23 dB | +3.77 dB | −0.88 dB | 0.90 | yes (+20.88 dB would peak at 6.46) |

3. **Tuning block** (`tools/apply_tuning.py`), as the other presets have: `output_gain_db` −11.0 (headroom, `docs/BUILDS.md`), and on `bell`, `bow`, `bell_shoulder` and `bow_shoulder` the velocity curve that makes the Hertz law's v² linear (`vel_0.6_db` 4.437, `vel_0.3_db` 10.458; Bettis's decision 7). EDGE is not touched: its NPZ captures are already linear in velocity. At velocity 1.0 the curve is 0 dB, so it does not change the trims.
4. **Seed.** The strike-phase sequence starts from Raven's seed, 13.

### Things in the Raven data or engine that look wrong (ported as they are)

- **The NPZ envelopes are a little shorter than the window, and some low modes ring far longer.** The envelopes run 8.7 s (435 frames) for a 9.2 s window; the last frame is held to the window's end. The strike then rings from its own decay, which for the longest low modes is far above the held envelope, so their level jumps up 9.2 s after an edge strike. Relative to the strike's coupling, the median mode of band 1 (116–127 Hz) goes from −84 dB to −23 dB, band 4 (224–283 Hz) from −60 dB to −29 dB, and band 9 (713–898 Hz) from nothing to −61 dB. It is the same mechanism as the 602's and Bettis's window ends, but there the envelopes end well before their windows and the low modes ring less.
- **`edge_rms_correction` caps the lowest bands.** It divides each band by its velw norm, within ×0.25–×4. Bands 0, 2 and 4 would need ×24.4, ×15.6 and ×9.2 and stay at ×4, so on edge strikes they are about 16, 12 and 7 dB below their SPEAR level. Band 1 has ×1.0 where its norm asks ×28.5 (about 29 dB below): at velocity 0.194 neither of its two modes has any velw, which is probably why the build left it at 1. Band 3 (178–224 Hz) has no modes.
- **Band 1's two modes take turns with velocity.** 115.8 Hz has velw at 0.119, 0.348, 0.528 and 1.0; 127.4 Hz at 0.438 and 0.725; at 0.194 neither. The interpolation between captures crossfades between them.
- **Five modes have T60 exactly 30 s** (127, 170, 299, 343 and 403 Hz), which looks like a cap in the build's decay fits.
- **92 modes have bell ratio 0**, 46 of them with `exc` above 0: a bell strike leaves those out; a bow strike plays them.
- **BELL and EDGE stay well below their SPEAR targets** once capped (BELL −2.44 dB against +4.72, EDGE −0.88 dB against +16.23). EDGE at its target would peak at 6.5 before the output gain; its SPEAR level is that of Edge V7, a much harder strike than the loudest bow capture.
- **BOW peaks at 1.26 at velocity 1.0** before the output gain (99th percentile, with its trim): BOW is never capped, by the rule.
- **The pk_t ramps have no effect** with the envelope on (as for Bettis): the edge pk_t and the edge velocity tilt are inaudible, and bow strikes never ramp. With EDGE ONSET ENVELOPE off, edge modes ramp in over up to 8.4 s (the edge pk_t's longest).
- **The calibration does not see the envelope** (as for the 602, Bettis and Swish); the trims make up for the level.

## Konstan Crash 18"

Added 2026-10-28. Konstan was built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, Konstan Crash 18") in the Bettis format, as Raven, and needs no new engine code: every mechanism it uses is one Raven already has. Its preset JSON differs from Raven's only in its data files, its edge envelope window (`edge_band_env_onset_s` 7.6 s, Raven 9.2 s) and its `edge_rms_correction`. It is baked with `nl_amount = 0`. NOVA RIDE, SMOKE FLAT RIDE, HAMMERED THIN RIDE, SWISH and RAVEN bake and play bit for bit as before (checked: re-baked with the new tools they are identical but for `created_utc` and the build machine's name, and `tools/cymbalis_identity` finds the engine's output identical to main's in memory for every preset, Konstan included).

### What Konstan uses

| | Konstan (`Konstan_Baseline1.json`) |
|---|---|
| Modes | 900, 17.2 Hz–18.8 kHz (none above 22.05 kHz). 700 seen in at least two bow captures; 200 seen only in the edge captures, with `exc` 0: they sound only on edge strikes, through the NPZ velw |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (7 velocities, 0.122–1.0) and the NPZ band envelope, switched on by the all-zero trigger file: 22 bands, exactly the plugin's thirds, 7.1 s of data (356 frames), 7.6 s window, `edge_rms_correction` per band, level exponent `vel_gain_power` 1.0 |
| Bow and bell strikes | standard coupling with the neutral edge ratios (all 1) and the inferred bell ratios (below) as position weights |
| Onset ramp data | bow pk_t, velocity-bracketed edge pk_t (7 velocities), edge velocity tilt (4 bands) |
| Contact | none (`permode_contact` false, `tip_contact_ms` 0): instantaneous strikes |
| MF / LF accumulators | as Raven (MF in boost mode, depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2. LF in boost mode, depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1) |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, stick click, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

**The bell is inferred, not captured.** There are no bell captures. The builder (`build_preset.py --infer-bell --bell-seed 3`) gave the bell the bow's modes with the bell-against-bow tilt measured on NOVA RIDE, RAVEN and HAMMERED THIN RIDE (eased below 1.5 kHz), an extra tilt and a per-mode scatter from Nova's and Raven's bells, made purer than theirs; Kris chose it by ear (`docs/BASELINE_MANIFEST.md`). Its shaping values are in `data/konstan_build_info.json` (`bell_shaping`). The bell's level target is RAVEN's measured +4.72 dB above BOW. Nothing in the plugin treats it differently from a captured bell; real bell captures would replace it.

### Bake and plugin

- **Bake** (`tools/bake_preset.py --preset konstan`; `tools/ab_common.py` `PRESETS`): `BELL` (1.0), `BOW` (0.5) and `EDGE` (0.0), no overlays, and the copies `BELL_SHOULDER` (`bell_shoulder`, from `BELL`) and `BOW_SHOULDER` (`bow_shoulder`, from `BOW`) (`COPIED_ENTRIES`), as Raven. No `ENVELOPE_ON` entry: the trigger file switches the envelope on (`edge_npz.envelope_active` true). Its T60s are baked without V19's cap (`T60_UNCAPPED`, decision 5). Every entry has the same calibration scalar, 0.0662 (0.0692 before the T60 refit).
- **Preset** KONSTAN CRASH 18", after RAVEN (`plugin/SlotTable.h`): RAVEN's rows (`docs/UI_SPEC.md` section 8); its wireframe cymbal `kKonstanShape` (section 11), whose bell radius, 0.25, is the hit display's bell zone (section 10).

### Live-engine decisions for Konstan

1. **Slots.** As Raven: BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). BELL and BOW start on TIP, BELL2 and BOW2 on SHOULDER (`docs/UI_SPEC.md` section 8). The copies are tip data, to be tuned by ear (`docs/TUNING.md`; they have the NOVA SHOULDER PROFILE button).
2. **Level trims**, as for Raven: velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their SPEAR targets (`data/konstan_build_info.json`: BELL +4.72 dB, RAVEN's measured value since the bell is inferred, and EDGE +13.5 dB above BOW, Edge V7 against Bow V6), capped where one hit (99th percentile of 1024 hits) would peak above 0.9 before the output gain. `bell_shoulder` takes BELL's target; the copies come out with their sources' trims. The tuning block (below) was applied first, so the cap is `--peak-limit 0.25365` (0.9 × 10^(−11/20), `docs/BUILDS.md`): `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=4.72 --spear-target BELL_SHOULDER=4.72 --spear-target EDGE=13.5 --peak-limit 0.25365`. Each bake entry records its target, cap and reason (`slots[].level_trim`).

   Measured again on 2026-10-31 after the T60 refit (decision 5), with the tuning block in place (EDGE VOICE FROM BOW 100 % included):

   | Entry | SPEAR target vs BOW | Trim (before the refit) | Level vs BOW | Peak of one hit with trim (99th percentile), before the output gain | Capped |
   |---|---|---|---|---|---|
   | BOW | 0 dB | −1.37 dB (+0.10) | 0 dB | 1.02 | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | −1.37 dB (+0.10) | 0 dB | 1.02 | no (BOW's target) |
   | BELL | +4.72 dB | +26.04 dB (+25.92) | +1.20 dB | 0.90 | yes (+29.56 dB would peak at 1.35) |
   | BELL_SHOULDER | +4.72 dB (BELL's) | +26.04 dB (+25.92) | +1.20 dB | 0.90 | yes (as BELL) |
   | EDGE | +13.5 dB | +3.32 dB (+3.33) | −0.22 dB | 0.90 | yes (+17.04 dB would peak at 4.36) |

   The project session's estimate (BELL about +19.5 dB, peaking at 0.44; EDGE capped about 11 dB above BOW) is what the trims tool gives on a bake without the tuning block: there Nova's BOW is measured with its −11 dB and Konstan's without, so every level against BOW reads 11 dB high. EDGE's trim, +2.20 dB, is the same either way; with the block in place BOW is matched to Nova and BELL meets the 0.9 cap first.
3. **Tuning block** (`tools/apply_tuning.py`), as Raven's: `output_gain_db` −11.0, and on `bell`, `bow`, `bell_shoulder` and `bow_shoulder` the velocity curve that makes the Hertz law's v² linear (`vel_0.6_db` 4.437, `vel_0.3_db` 10.458). EDGE is not touched. Since 2026-10-29 also `edge_voice_from_bow` 100 (live-engine decision 21), with EDGE re-trimmed at 100 %: +2.20 → +3.33 dB, capped as before (the voiced edge is 1.46 dB quieter over the first 500 ms and peaks lower). Put back unchanged after the T60 refit's re-bake (decision 5).
4. **Seed.** The strike-phase sequence starts from Konstan's seed, 13 (Raven's too).
5. **The bow's decay times come from its recordings, without V19's T60 cap** (2026-10-31, Kris's choice by ear: version C of three renders in the project session).
   - **The problem.** KONSTAN's bow sounded too low for an 18" crash and unlike its edge. Its first 150 ms matched the recording; after that its modes above about 1 kHz died 2–3× faster than in the bow captures, so the sound sank into its 376–445 Hz modes. Two things cut those T60s: the SPEAR builder's per-band calibration, and V19's T60 cap (`_T60_CAP_F` / `_T60_CAP_VAL`: 12 s at 200 Hz, 6 s at 1 kHz, 3 s at 4 kHz, 1.6 s at 10 kHz), which the bake applied. The edge did not have the problem: its band envelope holds the highs for its 7.6 s window.
   - **The refit** (`docs/BASELINE_MANIFEST.md`, Konstan, "T60 refit from the bow captures"). 577 of the 700 bow modes got T60s fitted from Bow V4–V6 (median ×2.6, each limited to ×1/3–×3). The fingerprint holds every mode's effective T60: the refit, or the old value after V19's cap for the others.
   - **No cap.** Those T60s are meant to be used without V19's cap, so the bake writes them as they are: `tools/bake_preset.py` has a per-preset set, `T60_UNCAPPED` = {konstan}. For those presets, after the engine is built, each mode's T60 is max(fingerprint `t60` × `t60_scale`, 0.02) in place of V19's capped value, the contact-phase mode step is recomputed with V19's formula, and so is the calibration scalar, whose reference edge strike rings with these T60s (0.06665 → 0.06619 against the capped T60s of the same fingerprint). The Python engine is unchanged; the bake tool only replaces the values. This is a plugin-side decision: the plugin is the reference (`CLAUDE.md`). Every other preset bakes byte for byte as before (but for `created_utc`).
   - **What changed in the bake.** 593 of the 900 T60s (median ×2.5); 298 are above V19's cap. The bow's modes above 1 kHz now have a median T60 of 2.69 s (1.15 s before). Nothing else in the bake depends on the T60s but the mode step and the calibration scalar. The plugin applies no T60 cap of its own: every mode's T60 is the baked one × the ring length × its band's ring length (`tunedT60`).
   - **The trims** were measured again (decision 2): BOW −1.37 dB (was +0.10; its first 500 ms are 1.47 dB louder with the highs ringing on), BELL +26.04 dB (+25.92, capped), EDGE +3.32 dB (+3.33, capped); the shoulder copies take their sources'.
   - **EDGE VOICE FROM BOW**'s weights α come from the baked T60s, so its check values at 100 % moved: modes with α = 1 from 67 to 42, with 0 < α < 1 from 556 to 592; shared long-ringing modes still 10 of 20 at 100 % (2 at 0 %, was 3) (`docs/TUNING.md`).

### Things in the Konstan data or engine that look wrong (ported as they are)

- **The BELL rows get far louder with BLEND POSITION 1** (measured before the T60 refit, with BELL's trim then). BELL's trim (+25.92 dB then, +26.04 dB now) lifts the inferred bell, whose level is low because its energy sits in a few modes. Moving a BELL row toward the bow brings the bow's modes in under that trim: one velocity-1.0 hit (99th percentile of 64, after the output gain) peaks at 0.26 at the bell, 1.57 at blend 25 %, 2.94 at 50 % and 6.47 at 100 % (position 0.5, about 26 dB above BOW). The other presets' BELL rows rise too, but to 1.1–1.3 at 100 % (NOVA RIDE 1.13, HAMMERED THIN RIDE 1.12, RAVEN 1.31). At the default (blend 0) nothing changes. A strike every 24 ms through every entry, with in-between positions (`tools/cymbalis_identity`), peaks at 2.80 on Konstan against 0.64–1.07 on the others.
- **Seven modes are below 20 Hz** (17.2–19.8 Hz; four of them with `exc` above 0, so bow strikes play them), and 19 below the lowest band (89 Hz), 13 of them excited. Raven's lowest mode is 34 Hz.
- **The mode at 106.2 Hz had T60 exactly 30 s** in the first fingerprint, which looks like the build's cap (Raven had five); V19's cap made it 12 s, and the refit fingerprint holds 12 s. Band 0's T60 calibration factor is exactly 6.0, against a measured band decay of 68 s, which looks like a limit too.
- **Seven modes ring longer than 12 s** since the refit (decision 5), where V19's cap would allow at most 12 s even at 200 Hz and below: 569.2 Hz at 17.1 s (9.2 s before), and six between 1.8 and 2.8 kHz at 12.1–12.8 s (1778.1, 2168.9, 2638.8, 2644.0, 2813.9 and 2838.2 Hz; 4.1–4.8 s before), all bow modes (`exc` 0.08–0.26). The cap at those frequencies is 4.5–6 s.
- **The bell's centroid is 0.83 × the bow's** (`bell_shaping.centroid_vs_bow` 0.828), where the bell data's description says the tilt puts it at 0.7 ×.
- **The NPZ envelopes are a little shorter than the window**: 7.1 s of data for a 7.6 s window; the last frame is held to the window's end, as for Raven.
- **`edge_rms_correction` is at its limits in the lowest bands**: band 0 at ×4 (the cap), bands 1–4 at ×1.0. Band 2 (141–178 Hz) has no modes; band 1's one mode has no velw at velocities 0.265 and 0.745, band 4's two modes none at 0.122 and 0.265, and band 3's three none at 0.828, so the interpolation between captures fades those bands in and out with velocity.
- **BELL and EDGE stay below their SPEAR targets** once capped (BELL +1.20 dB against +4.72, EDGE −0.22 dB against +13.5 at EDGE VOICE FROM BOW 100 %; decision 5).
- **BOW peaks at 1.02 at velocity 1.0** before the output gain (99th percentile, with its trim; 1.23 before the T60 refit): BOW is never capped, by the rule.
- **The pk_t ramps have no effect** with the envelope on (as for Raven): the edge pk_t and the edge velocity tilt are inaudible, and bow strikes never ramp. With EDGE ONSET ENVELOPE off, edge modes ramp in over up to 6.9 s (the edge pk_t's longest).
- **The calibration does not see the envelope** (as for the other NPZ presets); the trims make up for the level.

## Extra Paper Thin Ride 24"

Added 2026-11-21. EXTRA PAPER THIN 24" was built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, Extra Paper Thin Ride 24") in the Bettis format, as Raven and Konstan, and needs no new engine code: every mechanism it uses is one Konstan already has, its uncapped T60s included. Its preset JSON differs from Konstan's only in its data files, its edge envelope window (`edge_band_env_onset_s` 14.2 s, Konstan 7.6 s) and its `edge_rms_correction`. It is baked with `nl_amount = 0`. Its edge is the one Kris chose by ear the same day ("C", below), which replaced a first version built from Edge V1, V2 and V6. Every other preset bakes and plays bit for bit as before (checked: re-baked with the new tools they are identical but for `created_utc` and the build machine's Python string, the tuning blocks and trims that re-baking drops aside, and `tools/cymbalis_identity` finds the engine's output identical to main's in memory for every preset).

### What Paper Thin uses

| | Paper Thin (`PaperThin_Baseline1.json`) |
|---|---|
| Modes | 900, 37.0 Hz–19.1 kHz (none above 22.05 kHz). 771 seen in at least two bow captures; 129 seen only in Edge V3, V5 and V6, with `exc` 0: they sound only on edge strikes, through the NPZ velw. Built with `--low-wash`: up to 24 edge modes in each band below 400 Hz, the crash wash |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (3 rows, at velocities 0.536, 0.649 and 1.0, all with Edge V5's mode weights) and the NPZ band envelope, switched on by the all-zero trigger file: 22 bands, exactly the plugin's thirds, 13.7 s of data (685 frames), 14.2 s window, `edge_rms_correction` per band, level exponent `vel_gain_power` 1.0 |
| Bow and bell strikes | standard coupling with the neutral edge ratios (all 1) and the bell ratios, measured from Bell V1–V3 against Bow V2–V3, as position weights |
| Onset ramp data | bow pk_t, velocity-bracketed edge pk_t (3 velocities), edge velocity tilt (4 bands) |
| Contact | none (`permode_contact` false, `tip_contact_ms` 0): instantaneous strikes |
| MF / LF accumulators | as Raven and Konstan (MF in boost mode, depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2. LF in boost mode, depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1) |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, stick click, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

**The edge** (Kris, by ear, 2026-11-21, "C"; `docs/BASELINE_MANIFEST.md`; `data/paperthin_build_info.json`, `edge_captures_used` and `edge_npz_rework`). It is reworked data, not the captures as recorded; the bake takes it as it is, and nothing in the plugin chooses captures or rows.

- **Edge V1 and V2 are left out:** their onset is as bright as the bow's (centroid 3.7–4.5 kHz in the first 150 ms, the bow 4.3–5.4 kHz), and they sounded like a tip.
- **Rows V5, V3 and V6, at velocities 0.536, 0.649 and 1.0.** The row velocities are set so each row renders at its recorded level relative to the loudest (V5 0.552, V3 0.606, V6 1.0); V5 is quieter than V3. An edge strike between rows interpolates between them, as on every NPZ preset; below 0.536 it takes the V5 row.
- **Every row has Edge V5's mode weights (velw).** The loud recordings light up a different set of partials, which Kris heard as a different articulation at high velocity, so the edge is the same articulation at every velocity: only its band levels and envelopes change. EDGE VOICE FROM BOW still acts on that one pattern (it starts at 0 %).
- **The V3 and V6 rows keep each band's level and decay but build up to the band's peak as Bettis's edge does at 0.67 and 1.0** (per band, Bettis's normalised rise to its peak, then the row's own decay from its peak). As recorded, V3's upper bands peaked within 20–60 ms (a ping) and V6's low bands within 20 ms (a thump); on Bettis the upper bands swell over 200–300 ms at high velocity.

### Bake and plugin

- **Bake** (`tools/bake_preset.py --preset paperthin`; `tools/ab_common.py` `PRESETS`): `BELL` (1.0), `BOW` (0.5) and `EDGE` (0.0), no overlays, and the copies `BELL_SHOULDER` (`bell_shoulder`, from `BELL`) and `BOW_SHOULDER` (`bow_shoulder`, from `BOW`) (`COPIED_ENTRIES`), as Raven and Konstan. No `ENVELOPE_ON` entry: the trigger file switches the envelope on (`edge_npz.envelope_active` true). Its T60s are baked without V19's cap (`T60_UNCAPPED` = {konstan, paperthin}; below). Every entry has the same calibration scalar, 0.1006 (0.1011 with the capped T60s). 5 slots, 900 modes; every locked file matches the manifest.
- **Preset** EXTRA PAPER THIN 24", id `paperthin`, after RAVEN and before KONSTAN CRASH 18" (`plugin/SlotTable.h`): RAVEN's and KONSTAN's rows (`docs/UI_SPEC.md` section 8); its wireframe cymbal `kPaperThinShape` (section 11), whose bell radius, 0.20, is the hit display's bell zone (section 10).

### Live-engine decisions for Paper Thin

1. **Slots.** As Raven and Konstan: BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). BELL and BOW start on TIP, BELL2 and BOW2 on SHOULDER (`docs/UI_SPEC.md` section 8). There are no shoulder captures for the bell and bow: the copies are tip data, to be tuned by ear (`docs/TUNING.md`; they have the NOVA SHOULDER PROFILE button, the bell profile on `bell_shoulder` and the bow profile on `bow_shoulder`).
2. **Level trims**, as for Raven and Konstan: velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their SPEAR targets (`data/paperthin_build_info.json`: BELL +8.69 dB and EDGE +15.77 dB above BOW, the loudest captures), capped where one hit (99th percentile of 1024 hits) would peak above 0.9 before the output gain. `bell_shoulder` takes BELL's target; the copies come out with their sources' trims. The tuning block (below) was applied first, so the cap is `--peak-limit 0.25365` (0.9 × 10^(−11/20), `docs/BUILDS.md`): `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=8.69 --spear-target BELL_SHOULDER=8.69 --spear-target EDGE=15.77 --peak-limit 0.25365`. EDGE then gets its boost (decision 6). Each bake entry records its target, cap and reason, and EDGE's its boost (`slots[].level_trim`).

   | Entry | SPEAR target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile), before the output gain | Capped |
   |---|---|---|---|---|---|
   | BOW | 0 dB | −7.55 dB | 0 dB | 0.99 | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | −7.55 dB | 0 dB | 0.99 | no (BOW's target) |
   | BELL | +8.69 dB | +5.43 dB | +0.27 dB | 0.90 | yes (+13.85 dB would peak at 2.4) |
   | BELL_SHOULDER | +8.69 dB (BELL's) | +5.43 dB | +0.27 dB | 0.90 | yes (as BELL) |
   | EDGE | +15.77 dB | +5.19 dB capped, +9.19 dB with the boost | −1.21 dB at velocity 0.6 and below, +2.79 dB at 1.0 | 0.90 capped, 1.43 with the boost | yes (+22.17 dB would peak at 6.4) |

   Its first spec gave `--peak-limit 0.9`; that is what the trims tool gives on a bake without the tuning block, as for Konstan (decision 2 there), and Kris confirmed RAVEN's and KONSTAN's cap with the reworked edge (2026-11-21).
3. **Tuning block** (`tools/apply_tuning.py`), as Raven's and Konstan's: `output_gain_db` −11.0, `hit_variation` true, and on `bell`, `bow`, `bell_shoulder` and `bow_shoulder` the velocity curve that makes the Hertz law's v² linear (`vel_0.6_db` 4.437, `vel_0.3_db` 10.458). EDGE's NPZ captures are already linear in velocity; its curve is the boost's (decision 6): `vel_0.6_db` −4.0, `vel_0.3_db` −4.0. EDGE VOICE FROM BOW is not in the block, so it starts at 0 %, as Raven's.
4. **Seed.** The strike-phase sequence starts from Paper Thin's seed, 13 (Raven's and Konstan's too).
5. **The bow's decay times come from its recordings, without V19's T60 cap**, as Konstan's (its decision 5).
   - **The refit** (`docs/BASELINE_MANIFEST.md`, Extra Paper Thin Ride 24"; `data/paperthin_build_info.json`, `t60_refit`). Konstan's method, on Bow V1–V3: 653 of the 771 bow modes got T60s fitted from the captures (median ×3.0, each limited to ×1/3–×3). The fingerprint holds every mode's effective T60: the refit, or the old value after V19's cap for the others.
   - **No cap.** `paperthin` is in `T60_UNCAPPED`, so each mode's T60 is max(fingerprint `t60` × `t60_scale`, 0.02), the contact-phase mode step is recomputed from it, and so is the calibration scalar (0.10107 → 0.10058).
   - **What that changes in the bake.** 328 of the 900 T60s differ from the capped ones, all bow modes: 327 by more than 0.01 s, the manifest's count, and one (260.9 Hz) by 0.00004 s, the fingerprint's four decimals against V19's interpolated cap. The bow's modes above 1 kHz have a median T60 of 2.75 s (2.29 s with the cap). 23 modes ring longer than 12 s, the longest 36.6 s at 476.2 Hz, 25.6 s at 254.7 Hz and 21.9 s at 938.2 Hz. The captures' band decays are 10–27 s from 180 Hz to 2 kHz (the manifest), so a long ring is the cymbal's.
6. **EDGE is louder at high velocity** (Kris, 2026-11-21: "a large unruly sounding cymbal, it should be a bit louder at high velocity", without clipping).
   - **The boost.** EDGE's trim is 4 dB above its capped trim (+5.19 → +9.19 dB), and its velocity curve takes the 4 dB back off at velocity 0.6 and below (`vel_0.6_db` −4.0, `vel_0.3_db` −4.0; the curve is flat below 0.6, `docs/TUNING.md`). Edge strikes at 0.6 and below play exactly as with the capped trim alone; between 0.6 and 1.0 the extra level comes in, linear in log velocity: +1.21 dB at 0.7, +2.25 dB at 0.8, +3.18 dB at 0.9, +4.00 dB at 1.0 (checked in memory, first 500 ms; at 0.1, 0.3, 0.5 and 0.6 the output is sample for sample the capped trim's). `tools/set_level_trims.py --boost EDGE=4` stores it, and checks that the curve is in the tuning block (`docs/BUILDS.md`).
   - **The clipping check** (in memory, at the plugin's output after the −11 dB output gain, Volume 0 dB). Kris asked for +5 dB, lowered in 0.5 dB steps until both peaked below 0.9:
     - one EDGE hit at velocity 1.0, 99th percentile of 1024 hits (as `tools/cymbalis_trims` measures, HIT VARIATION off);
     - a dense run: 8 EDGE hits at 1.0, 150 ms apart, with BOW hits at 0.8 every 150 ms over the same time (with each EDGE hit, and 75 ms after it), HIT VARIATION on, 2 s of ring after the last. Each strike draws its own phases, so the dense run's peak is also taken as the 99th percentile, of 1024 runs.

     | Boost | One EDGE hit (99th percentile of 1024) | Dense run, first after loading | Dense run, 99th percentile of 1024 (BOW with EDGE / 75 ms after) |
     |---|---|---|---|
     | +5 dB | 0.45 | 0.75 | 0.94 / 0.95 |
     | +4.5 dB | 0.43 | 0.72 | 0.902 / 0.906 |
     | **+4 dB** | **0.40** | **0.69** | **0.86 / 0.86** |

     At +4 dB the loudest of the 1024 dense runs peaked at 0.96. EDGE's single hit stays far below the cap at any of these boosts; the dense run, with the bow ringing under it, sets the limit.

### Things in the Paper Thin data or engine that look wrong (ported as they are)

- **157 of the 771 bow modes ring longer than their band's measured decay** (`t60_band_measured_s`, loud bow captures). The largest: 476.2 Hz at 36.6 s against 17.5 s for its band (449–566 Hz), 938.2 Hz at 21.9 s against 14.3 s, 1972.3 Hz at 18.6 s against 10.8 s and 2388.5 Hz at 20.0 s against 9.4 s. Nine bow modes above 1 kHz ring 15.6–20.0 s, 2.9–4.3 × V19's cap there.
- **The refit's median change is ×3.0, its limit**: at least half the refitted modes would have gone further.
- **The lowest three bands' T60 calibration factors are exactly 6.0** (`t60_band_factor`), against measured band decays of 116, 62 and 60 s (89–141 Hz), which looks like a limit in the builder, as Konstan's band 0.
- **`edge_rms_correction` is at its cap in the four lowest bands** (×4, 89–178 Hz). By the velw norms (the same in every row) they would need ×10.8, ×7.0, ×21.6 and ×4.06, so on edge strikes bands 0–2 are about 8.6, 4.9 and 14.6 dB below their SPEAR level (band 3 0.1 dB). In band 1 only 6 of its 24 modes have any velw.
- **The NPZ envelopes are a little shorter than the window**: 13.7 s of data for a 14.2 s window; the last frame is held to the window's end, as for Raven and Konstan.
- **65 modes have bell ratio 0**, 25 of them with `exc` above 0: a bell strike leaves those out; a bow strike plays them.
- **16 modes are below the lowest band (89 Hz)**, 15 of them excited, from 37.0 Hz.
- **BELL and EDGE stay well below their SPEAR targets** once capped (BELL +0.27 dB against +8.69; EDGE −1.21 dB at 0.6 and below and +2.79 dB at 1.0 with its boost, against +15.77; decisions 2 and 6).
- **BOW peaks at 0.99 at velocity 1.0** before the output gain (99th percentile, with its trim): BOW is never capped, by the rule.
- **The pk_t ramps have no effect** with the envelope on (as for Raven and Konstan): the edge pk_t and the edge velocity tilt are inaudible, and bow strikes never ramp. With EDGE ONSET ENVELOPE off, edge modes ramp in over up to 13.25 s (the edge pk_t's longest).
- **The calibration does not see the envelope** (as for the other NPZ presets); the trims make up for the level.

## Crash 16"

Added 2026-11-22. CRASH 16" (a 16" A Zildjian crash) was built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, Crash 16") in the Bettis format, as Raven, Konstan and Extra Paper Thin, then calibrated by rendering the plugin against the recordings the captures came from; Kris chose its sound by ear from the project session's plugin-engine renders ("v3"). It needs no new engine code: every mechanism it uses is one Extra Paper Thin already has, its uncapped T60s included. Its preset JSON differs from Konstan's only in its data files, its edge envelope window (`edge_band_env_onset_s` 8.9 s) and its `edge_rms_correction`. It is baked with `nl_amount = 0`. Every other preset bakes and plays bit for bit as before (their bakes and the engine are unchanged).

### What Crash 16 uses

| | Crash 16 (`AzCrash_Baseline1.json`) |
|---|---|
| Modes | 900, 56.6 Hz–20.3 kHz (none above 22.05 kHz). 706 from the bow captures; 194 seen only in the edge captures, with `exc` 0: they sound only on edge strikes, through the NPZ velw |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (3 rows, at velocities 0.27, 0.522 and 1.0: Edge V1, V2 and V3, every row with Edge V2's mode weights above 112 Hz) and the NPZ band envelope, switched on by the all-zero trigger file: 22 bands, exactly the plugin's thirds, 8.4 s of data (422 frames), 8.9 s window, `edge_rms_correction` per band, level exponent `vel_gain_power` 1.0 |
| Bow and bell strikes | standard coupling with the neutral edge ratios (all 1) and the bell ratios (measured from Bell V1, then calibrated by rendering, below) as position weights |
| Onset ramp data | bow pk_t, velocity-bracketed edge pk_t (3 velocities), edge velocity tilt (4 bands) |
| Contact | none (`permode_contact` false, `tip_contact_ms` 0): instantaneous strikes |
| MF / LF accumulators | as Raven, Konstan and Extra Paper Thin (MF in boost mode, depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2. LF in boost mode, depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1) |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, stick click, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

**Calibrated by rendering** (`docs/BASELINE_MANIFEST.md`; `data/azcrash_build_info.json`, `render_calibration`). Kris heard the first build's edge as too high-pitched and its bow as too heavy in the low modes. The SPEAR resynthesis matched the recordings within about 1 dB per range; the built preset did not (the bow short of 280–900 Hz with excess lows and highs in its tail, the loud edge about 5 dB short below 2.8 kHz with excess 7–18 kHz). So the plugin was rendered and compared with the recordings per range (89–280, 280–900, 900–2800, 2800–7000 and 7000–18000 Hz; 0–0.15, 0.3–1.0 and 1.0–3.0 s) and the data adjusted until they matched: the bow's mode `exc` and T60 per range, the bell ratios of the modes the bell strikes, and the edge's band envelopes per band. What remains (the manifest): the bow's first 150 ms lacks the strike's low thump and 4–11 kHz sizzle, which one fixed decay per mode cannot make; the bell's tail is thin at 280–900 Hz and bright on top. The bake takes the data as it is; nothing in the plugin calibrates.

**The edge** (Kris, by ear, 2026-11-22; the manifest and `data/azcrash_edge_npz_data.json`'s notes). It is reworked data, set up so the edge's level rises steadily with velocity:

- **One set of mode weights (velw) on every row, Edge V2's.** Below 900 Hz the three captures' matched partials barely overlapped (similarity 0.08 between V2 and V3 below 280 Hz), and Kris heard the loudest edge as a different cymbal. Checked in the bake: the three rows differ only in the 25 modes below 112 Hz (56.6–112.0 Hz).
- **The modes below 112 Hz are weighted from the SPEAR energy within ±4 % of each mode, frame by frame,** with the split between the sub-89 Hz and the 89–112 Hz modes set per row. On a loud hit the lowest partial glides from about 75 to 111 Hz over the first second, so the builder's ±0.25 % match gave those modes almost no weight, and the loud edge lost its 60–100 Hz body.
- **Each row's band envelopes are calibrated per band and time window** (0–0.2, 0.3–1.5 and 1.5–4 s) until the plugin matched its recording.
- **The row velocities, 0.27, 0.522 and 1.0,** are set so each row renders at its recording's level relative to the loudest (0.224, 0.5 and 1.0: −13 and −6 dB). Below 0.27 an edge strike takes the V1 row.
- **Every row's envelope peaks at frame 1, band 7, at 1.0.** The engine normalises a blend of two rows by the blend's peak (`startEnvelope`), and the blended velw by its own peak (`npzCoupling`). With the rows peaking in different places, strikes between two rows came out up to about 5 dB louder than the rows themselves; with one set of velw and a shared envelope peak they don't, since every blend then peaks at 1.0 in the same place.

The edge's level against velocity, checked in memory (RMS of the first 500 ms, HIT VARIATION off, against velocity 1.0; the project session's values in brackets): −18.9 dB at 0.15 (−18), −13.8 dB at 0.27 (−13), −8.4 dB at 0.45 (−8), −6.5 dB at 0.522, −5.3 dB at 0.6 (−5), −2.6 dB at 0.8 (−2.4), 0 dB at 1.0. It rises steadily, with no step at the rows.

### Bake and plugin

- **Bake** (`tools/bake_preset.py --preset azcrash`; `tools/ab_common.py` `PRESETS`): `BELL` (1.0), `BOW` (0.5) and `EDGE` (0.0), no overlays, and the copies `BELL_SHOULDER` (`bell_shoulder`, from `BELL`) and `BOW_SHOULDER` (`bow_shoulder`, from `BOW`) (`COPIED_ENTRIES`), as Extra Paper Thin. No `ENVELOPE_ON` entry: the trigger file switches the envelope on (`edge_npz.envelope_active` true). Its T60s are baked without V19's cap (`T60_UNCAPPED` = {konstan, paperthin, azcrash}; below). Every entry has the same calibration scalar, 0.083536 (0.083541 with the capped T60s). 5 slots, 900 modes; every locked file matches the manifest.
- **Preset** CRASH 16", id `azcrash`, after KONSTAN CRASH 18" and before HI-HAT (`plugin/SlotTable.h`): EXTRA PAPER THIN 24"'s rows (`docs/UI_SPEC.md` section 8); its wireframe cymbal `kAzCrashShape` (section 11), drawn at its true size (`drawScale` 0.8), whose bell radius, 0.25, is the hit display's bell zone (section 10).

### Live-engine decisions for Crash 16

1. **Slots.** As Extra Paper Thin: BELL and BELL2 play `bell` (TIP) or `bell_shoulder` (SHOULDER); BOW and BOW2 play `bow` (TIP) or `bow_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed). BELL and BOW start on TIP, BELL2 and BOW2 on SHOULDER (`docs/UI_SPEC.md` section 8). There are no shoulder captures for the bell and bow: the copies are tip data, to be tuned by ear (`docs/TUNING.md`; they have the NOVA SHOULDER PROFILE button, the bell profile on `bell_shoulder` and the bow profile on `bow_shoulder`).
2. **Level trims**, as for Raven, Konstan and Extra Paper Thin: velocity 1.0, the mean level of the first 500 ms. BOW is matched to Nova BOW (tip) and never capped. BELL and EDGE go to their targets, capped where one hit (99th percentile of 1024 hits) would peak above 0.9 before the output gain. EDGE's target is its SPEAR level, +18.1 dB above BOW (Edge V3 against Bow V2). BELL's is not: its only capture is soft (Bell V1, −9.66 dB against Bow V2, about level with the soft Bow V1), so its target is −0.7 dB against BOW, the level in the renders Kris heard (`data/azcrash_build_info.json`). `bell_shoulder` takes BELL's target; the copies come out with their sources' trims. The tuning block (below) was applied first, so the cap is `--peak-limit 0.25365` (0.9 × 10^(−11/20), `docs/BUILDS.md`): `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=-0.7 --spear-target BELL_SHOULDER=-0.7 --spear-target EDGE=18.1 --peak-limit 0.25365`. Each bake entry records its target, cap and reason (`slots[].level_trim`). The values are the project session's.

   | Entry | Target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile), before the output gain | Capped |
   |---|---|---|---|---|---|
   | BOW | 0 dB | +3.82 dB | 0 dB | 1.13 | no (BOW) |
   | BOW_SHOULDER | 0 dB (BOW's) | +3.82 dB | 0 dB | 1.13 | no (BOW's target) |
   | BELL | −0.7 dB | +16.81 dB | −0.70 dB | 0.88 | no |
   | BELL_SHOULDER | −0.7 dB (BELL's) | +16.81 dB | −0.70 dB | 0.88 | no (as BELL) |
   | EDGE | +18.1 dB | +1.02 dB | −2.56 dB | 0.90 | yes (+21.68 dB would peak at 9.7) |

3. **Tuning block** (`tools/apply_tuning.py`), as the other rides': `output_gain_db` −11.0, `hit_variation` true, and on `bell`, `bow`, `bell_shoulder` and `bow_shoulder` the velocity curve that makes the Hertz law's v² linear (`vel_0.6_db` 4.437, `vel_0.3_db` 10.458). EDGE is not touched: its NPZ rows are already linear in velocity (above). EDGE VOICE FROM BOW is not in the block, so it starts at 0 %, as Raven's.
4. **Seed.** The strike-phase sequence starts from Crash 16's seed, 13 (Raven's, Konstan's and Extra Paper Thin's too).
5. **The bow's decay times come from its recordings and the render calibration, without V19's T60 cap**, as Konstan's (its decision 5).
   - **The refit and calibration** (the manifest): 617 modes refitted from the bow captures (median ×2.1), then each range's T60 adjusted by rendering (within ×0.5–2). The fingerprint holds every mode's effective T60.
   - **No cap.** `azcrash` is in `T60_UNCAPPED`, so each mode's T60 is max(fingerprint `t60` × `t60_scale`, 0.02), the contact-phase mode step is recomputed from it, and so is the calibration scalar (0.083541 → 0.083536).
   - **What that changes in the bake.** 322 of the 900 T60s differ from the capped ones, the manifest's count: 311 by more than 0.01 s; of the other 11, nine edge-only modes differ by under 0.0001 s (the fingerprint's four decimals against V19's interpolated cap) and two bow modes by 0.003 and 0.006 s. The bow's modes above 1 kHz have a median T60 of 2.36 s (2.08 s with the cap). 14 modes ring longer than 12 s, the longest 29.9 s at 2004.1 Hz.
6. **The 152 Hz hum removed** (Kris, 2026-11-22: "needs to go"; the data branch's commit 51d5f75, `docs/BASELINE_MANIFEST.md`). The project session's render calibration had stretched four quiet bow modes, at 151.2, 151.5, 152.7 and 153.2 Hz (fingerprint ids 49, 50, 52 and 53), to a T60 of 363.9 s, the same value to the fourth decimal: 30 × V19's cap there and 9 × their band's measured decay (39.4 s, 141–178 Hz). They are that band's only bow modes and quiet (`exc` 0.0017–0.0035; together about 4·10⁻⁶ of the bow's excitation energy), so one hit barely sounds them, but they built up while the cymbal was played: after a minute of bow hits a 152 Hz hum was all that was left ringing once the playing stopped, and it lasted for minutes. Their T60 is now 12.0 s, their value before the calibration (their neighbours ring 13.0 s at 127–128 Hz and 5.6 s at 194–196 Hz), which is also V19's cap there. Nothing else in the preset changed: the bake differs only in those four modes' T60 and contact-phase mode step; the calibration scalar and the trims (BELL +16.81, BOW +3.82, EDGE +1.02 dB) came out as before, BOW's RMS in its first 500 ms 0.00001 dB lower. Checked in memory:
   - **The hum is gone:** a minute of BOW hits (one every 0.3 s, velocities 0.5–0.75), then stop. 5 s after the last hit the 150–155 Hz band is at −109.5 dB full scale, against −58.3 dB before (51.1 dB lower; the project session: −64 and −114 dB). Before, it was most of what was left ringing then (everything −56.8 dB); now everything is at −62.3 dB. While playing, the band is at −70.7 dB against −54.9 dB, the whole sound at −24.1 dB either way.
   - **Nothing else moves:** over a minute of playing, every 0.3 s, then 10 s, the new bake differs from the old by 32.7 dB under the signal on BOW (the project session: 34 dB) and 47.3 dB on BELL (a bell strike moved inward by HIT VARIATION takes in some bow modes), with 99.999 % of the difference's energy in 145–160 Hz: the hum itself. The rest is 86 dB (BOW) and 111 dB (BELL) under the signal: the MF and LF build-up read the band energies, so those four modes' decay moves the other low modes that little. EDGE is bit for bit as before (those modes have no edge weight).

Its CPU cost (`cymbalis_bench`, 60 s, this container): 3.5 % of a core at 44.1 kHz (about 28× real time; the project session measured 5.7 %) and 6.3 % at 96 kHz.

### Things in the Crash 16 data or engine that look wrong (ported as they are)

- **Four bow modes at 151.2–153.2 Hz had T60 363.9 s**, from the render calibration, which left a 152 Hz hum ringing for minutes after playing; set back to 12.0 s since 2026-11-22 (decision 6).
- **230 of the 706 bow modes ring longer than their band's measured decay** (`t60_band_measured_s`), for example 2004.1 Hz at 29.9 s against 6.9 s and 11507 Hz at 6.7 s against 1.2 s.
- **The bell ratios file has 14 ratios above 1** (at most 2.26), where its own description and the manifest say 34. The bake has the file's values.
- **`edge_rms_correction` is at its cap in the five lowest bands** (×4, 89–224 Hz). By the velw norms (the same in every row but for the modes below 112 Hz) bands 0, 1, 2 and 4 would need about ×9.6, ×12.0, ×14.9 and ×7.0, and band 3 ×3.3 where it has ×4: the correction does not follow the rows' velw (it is "as built", per the NPZ notes). The band envelopes were then calibrated by rendering through the plugin, correction included, so the edge's levels are the calibrated ones; changing the correction would undo that. In band 1 only one of its 15 modes has velw.
- **Two rows also reach their envelope peak elsewhere**: V2 at frame 0, band 7 (1.0) and V3 at frame 2, band 18 (1.0), besides frame 1, band 7. A blend of two rows still peaks at 1.0 at frame 1, band 7, so it does no harm.
- **The NPZ envelopes are a little shorter than the window**: 8.4 s of data for an 8.9 s window; the last frame is held to the window's end, as for Raven, Konstan and Extra Paper Thin.
- **217 modes have bell ratio 0**, 91 of them with `exc` above 0: a bell strike leaves those out; a bow strike plays them.
- **6 modes are below the lowest band (89 Hz)**, all excited, from 56.6 Hz.
- **EDGE stays below its SPEAR target** once capped (−2.56 dB against +18.1); EDGE at its target would peak at 9.7 before the output gain.
- **BOW peaks at 1.13 at velocity 1.0** before the output gain (99th percentile, with its trim): BOW is never capped, by the rule.
- **The pk_t ramps have no effect** with the envelope on (as for the other NPZ presets): the edge pk_t and the edge velocity tilt are inaudible, and bow strikes never ramp. With EDGE ONSET ENVELOPE off, edge modes ramp in over up to 7.95 s (the edge pk_t's longest).
- **The calibration does not see the envelope** (as for the other NPZ presets); the trims make up for the level.
- **`data/azcrash_build_info.json` has no `t60_refit` record** (null); the refit's numbers are in the manifest only.

## Stine Flat Ride

Added 2026-11-23. STINE FLAT RIDE takes SMOKE FLAT RIDE's place in the plugin: Kris chose it by ear on 2026-11-23, from the project session's renders of both cymbals playing the same score, and named it. It was built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, "Stine Flat Ride"; working name "New Flat") in the Bettis format, as Raven, Konstan and Crash 16", and needs no new engine code: every mechanism it uses is one Crash 16" already has, its uncapped T60s included. Its preset JSON differs from Crash 16"'s only in its data files, its edge envelope window (`edge_band_env_onset_s` 30 s) and its `edge_rms_correction`, and it names no bell ratios file. It is baked with `nl_amount = 0`. Every other preset bakes and plays bit for bit as before (their bakes and the engine are unchanged).

### What Stine uses

| | Stine (`Stine_Baseline1.json`) |
|---|---|
| Modes | 900, 29.6 Hz–19.0 kHz (none above 22.05 kHz). 746 from the bow captures; 154 seen only in the edge captures, with `exc` 0: they sound only on edge strikes, through the NPZ velw |
| Velocity response | `hertz_vel_scale`, `tau_ref_ms` 0.2 (no participation tables) |
| Edge strikes (`edge_frac > 0.1`) | NPZ velw coupling (3 rows, at velocities 0.142, 0.325 and 1.0: Edge V1, V2 and V3) and the NPZ band envelope, switched on by the all-zero trigger file: 22 bands, exactly the plugin's thirds, 30 s of data (1500 frames), 30 s window, `edge_rms_correction` per band, level exponent `vel_gain_power` 1.0 |
| Bow strikes | standard coupling with the neutral edge ratios (all 1); no bell ratios |
| Onset ramp data | bow pk_t, velocity-bracketed edge pk_t (3 velocities), edge velocity tilt (4 bands) |
| Contact | none (`permode_contact` false, `tip_contact_ms` 0): instantaneous strikes |
| MF / LF accumulators | as Crash 16" (MF in boost mode, depth 0.4, tau 3 s, max 1.4, 500 Hz–4 kHz; bow 1, edge 1, bell 0.2. LF in boost mode, depth 0.2, tau 3 s, max 1.7, cutoff 1.5 kHz; bow 0.5, edge 1, bell 0.1) |
| Wander, `vel_len_tilt`, onset gains, bloom, sparkle, stick click, attack noise, NL, edge level boost, rate jitter, hammering tilt | off |

**No bell.** A flat ride: there are no bell captures, and the preset names no bell ratios file, as SMOKE FLAT RIDE named none. The builder needs `--infer-bell` to run without bell captures; `data/stine_build_info.json` records the inferred bell (`bell_inferred`), which nothing uses. The plugin's bell rows play CENTER, a copy of BOW struck at position 1.0, as SMOKE FLAT RIDE's did.

**The T60s are refit from the bow captures, as Konstan's** (its decision 5; the manifest): 520 of the 746 bow modes refitted (median ×2.29), meant to be used without V19's cap. The captures' low range (89–400 Hz) rings 17.5–33.8 s.

**The edge envelopes run 30 s** (1500 frames) **and so does the window** (`edge_band_env_onset_s` 30.0), as the manifest describes. The captures end at 11.7 s; with the builder's 12.2 s window an edge hit's ring came back about 29 dB at the window's end, where the envelope hands each mode back to its own T60 (up to 40 s here). So each band was extended past its capture by its decay over the capture's last 3 s (T60 limited to 1–60 s), and a band that dies inside its capture stays silent. `data/stine_build_info.json` records the builder's values (586 frames, 12.2 s); the preset's are the ones used.

### Bake and plugin

- **Bake** (`tools/bake_preset.py --preset stine`; `tools/ab_common.py` `PRESETS`): SMOKE FLAT RIDE's layout. `BOW` (0.5) and `EDGE` (0.0), no articulation folder, and the copies `CENTER` (`center`, from `BOW`, at 1.0), `BOW_SHOULDER` (`bow_shoulder`, from `BOW`, at 0.5) and `CENTER_SHOULDER` (`center_shoulder`, from `CENTER`, at 1.0) (`COPIED_ENTRIES`), exactly as `'602'` has them. No `ENVELOPE_ON` entry: the trigger file switches the envelope on (`edge_npz.envelope_active` true, `onset_s` 30.0). Its T60s are baked without V19's cap (`T60_UNCAPPED` = {konstan, paperthin, azcrash, stine}; decision 4). Every entry has the same calibration scalar, 0.11862169212183724 (0.11956305757274611 with the capped T60s). 5 slots, 900 modes; every locked file matches the manifest.
- **Preset** STINE FLAT RIDE, id `stine_flat_ride`, second in the CYMBAL list, in SMOKE FLAT RIDE's place (`plugin/SlotTable.h`): SMOKE FLAT RIDE's five rows, sticks, labels (CENTER, CENTER2, and CENTER · BOW under Blend 1) and notes (`docs/UI_SPEC.md` section 8); SMOKE FLAT RIDE's wireframe cymbal `kSmokeShape`, unchanged (Kris: "Same wireframe render as smoke"; section 11), so the drawing is SMOKE FLAT RIDE's, bit for bit. CMake packs `stine_flat_ride` in `smoke_flat_ride`'s place.
- **SMOKE FLAT RIDE is kept.** Its data, `presets/602/`, stays as it is, locked. Its bake moved unchanged (`git mv`, the same bytes) to `resources/presets/archive/smoke_flat_ride.bake.json`; nothing builds, packs or tests the archive (CMake packs a list of names, and `tools/cymbalis_identity` and the other tools read `resources/presets/*.bake.json`, which does not descend into `archive/`). The `'602'` entries stay in `tools/ab_common.py` and `tools/bake_preset.py`, so `--preset 602` still re-bakes it (`docs/BUILDS.md`). The plugin as it was, SMOKE FLAT RIDE included, is on the branch `backup/smoke-flat-ride-602` (main at `81c1620`).
- **Saved sessions** (`plugin/PluginProcessor.cpp`, `kFormerPresets`). A session saved with preset `smoke_flat_ride` opens on STINE FLAT RIDE with that preset's stick choices and notes: a missing `stick_stine_flat_ride_*` key is read from `stick_smoke_flat_ride_*` (and a prefixed note key the same way; SMOKE FLAT RIDE's notes were the shared `note_*` keys, which STINE reads as they are). Its SMOKE tuning is not applied: the session's Tuning child for `smoke_flat_ride` names another preset and is ignored, so STINE starts at its bake's values, and a tuning file for `smoke_flat_ride` is refused like any file for another preset (`docs/TUNING.md`).

### Live-engine decisions for Stine

1. **Slots.** As SMOKE FLAT RIDE: BOW and BOW2 play `bow` (TIP) or `bow_shoulder` (SHOULDER); BELL and BELL2, labelled CENTER and CENTER2, play `center` (TIP) or `center_shoulder` (SHOULDER); EDGE plays `edge` (SHOULDER, fixed) (`docs/UI_SPEC.md` section 8). The copies are tip data, to be tuned by ear (`docs/TUNING.md`; the shoulders have the NOVA SHOULDER PROFILE button, with the bow profile).
2. **Level trims**, as for Raven and the presets after it: velocity 1.0, the mean level of the first 500 ms, measured after the tuning block was applied (`--peak-limit 0.25365`, `docs/BUILDS.md`). BOW is matched to Nova BOW (tip) and never capped; CENTER (no SPEAR target) is matched to it too and gets BOW's trim; the shoulder copies take their sources'. EDGE goes to its SPEAR target, +19.25 dB above BOW (Edge V3 against Bow V3, `data/stine_build_info.json`), capped where one hit (99th percentile of 1024 hits) would peak above 0.9 before the output gain: `tools/cymbalis_trims --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=19.25 --peak-limit 0.25365`. The values are the project session's.

   | Entry | Target vs BOW | Trim | Level vs BOW | Peak of one hit with trim (99th percentile), after the output gain | Capped |
   |---|---|---|---|---|---|
   | BOW | 0 dB | −8.83 dB | 0 dB | 0.280 | no (BOW) |
   | CENTER | 0 dB (BOW's) | −8.83 dB | 0 dB | 0.256 | no (BOW's target) |
   | BOW_SHOULDER | 0 dB (BOW's) | −8.83 dB | 0 dB | 0.280 | no (as BOW) |
   | CENTER_SHOULDER | 0 dB (BOW's) | −8.83 dB | 0 dB | 0.256 | no (as CENTER) |
   | EDGE | +19.25 dB | +5.44 dB | −2.95 dB | 0.254 | yes (+27.64 dB would peak at 3.27) |

3. **Tuning block** (`tools/apply_tuning.py`, the file `stine_flat_ride.tuning.json`), as the other new rides': `output_gain_db` −11.0, `hit_variation` true, and on `bow`, `center`, `bow_shoulder` and `center_shoulder` the velocity curve that makes the Hertz law's v² linear (`vel_0.6_db` 4.437, `vel_0.3_db` 10.458). EDGE is not touched: its NPZ rows are already linear in velocity. EDGE VOICE FROM BOW is not in the block, so it starts at 0 %. Its check values at 100 % (`tools/pack_bake`, `docs/TUNING.md`): modes with α = 1: 52, with 0 < α < 1: 590, shared long-ringing modes 8 of 20 (2 at 0 %).
4. **No T60 cap**, as Konstan, Extra Paper Thin and Crash 16": `stine` is in `T60_UNCAPPED`, so each mode's T60 is max(fingerprint `t60` × `t60_scale`, 0.02), the contact-phase mode step is recomputed from it, and so is the calibration scalar (0.119563 → 0.118622). 297 of the 900 T60s differ from the capped ones: 278 by more than 0.0001 s, the manifest's count; the other 19 (8 edge-only modes and 11 bow modes) by under 0.0001 s, the fingerprint's four decimals against V19's interpolated cap. 49 modes ring longer than 12 s, the longest 39.9 s at 292.7 Hz (33.4 s and 32.2 s at 252.9 and 253.6 Hz).
5. **Seed.** The strike-phase sequence starts from Stine's seed, 13 (Raven's, Konstan's, Extra Paper Thin's and Crash 16"'s too).

**Checked in memory** (2026-11-23, 44.1 kHz, the engine's output with the tuning block's −11 dB):

- **Levels**, single hits, HIT VARIATION off, RMS of the two channels' mean over the first 500 ms (the project session's in brackets): BOW −43.5 (−43.5), −36.6 (−36.2) and −31.5 dBFS (−31.0) at 0.3, 0.6 and 1.0; EDGE −46.7 (−45.0), −38.8 (−38.0) and −35.7 dBFS (−35.2); CENTER −38.4 (−38.2) at 0.5 and −31.5 dBFS (−31.1) at 1.0. EDGE's spectral centroid over 0.05–0.6 s: 1000, 2858 and 4363 Hz at 0.3, 0.6 and 1.0 (933, 3207 and 4398 Hz).
- **No ring returning.** After one EDGE hit at 1.0, every 250 ms from 1.25 s to 30 s stays below the level at 1 s (13.0 dB under the first 500 ms; the loudest later block, at 1.25 s, is 3.9 dB under it). At the window's end the ring steps up: 74.9 dB under the hit in 29.75–30 s, 59.6 dB in 30–30.25 s (the project session: 74 → 67 dB), still 46.6 dB under the level at 1 s.
- **Peaks.** The loudest of 128 single hits at 1.0, HIT VARIATION on as baked: 0.244 on BOW, CENTER and their shoulders, 0.151 on EDGE, within the 0.9 cap (0.254 after the output gain). The trims tool's 99th percentile with HIT VARIATION off is higher on BOW (0.280) and CENTER (0.256): BOW is never capped, as SMOKE FLAT RIDE's BOW was not.
- **Headroom** (`docs/BUILDS.md`, "Headroom"): the stress pattern peaks at +4.7 dBFS before the −11 dB, −6.3 dBFS after; the worst of 16 phase sequences −6.2 dBFS; the loudest single hit −1.3 → −12.3 dBFS (BOW, CENTER and their shoulders).
- **Every other preset** plays bit for bit as on main (`tools/cymbalis_identity`, 44.1 and 96 kHz, blocks of 128 and 64, cascade and dampings on and off).

Its CPU cost (`cymbalis_bench`, 60 s, every slot, EDGE hits included, this container): 4.6 % of a core at 44.1 kHz (about 22× real time), 5.0 % at 48 kHz and 8.1 % at 96 kHz. A dense run of BOW hits (one every 0.125 s for 60 s, its low modes ringing up to 40 s): 6.8× real time at 44.1 kHz and 5.4× at 96 kHz.

### Things in the Stine data or engine that look wrong (ported as they are)

- **EDGE is not steady in level between its first two rows.** RMS of the first 500 ms against velocity 1.0, HIT VARIATION off: −17.4 dB at 0.142 (the V1 row), −11.2 at 0.2, −11.5 at 0.25, −11.0 at 0.3, −11.1 at 0.325 (the V2 row), then rising steadily (−8.8 at 0.4, −5.5 at 0.5, −3.1 at 0.6, 0 at 1.0). Between 0.142 and 0.325 the edge jumps to the V2 row's level by 0.2 and stays flat. V1's envelope peaks at frame 4 (band 6), V2's and V3's at frame 1 (band 6): the engine normalises a blend of two rows by the blend's own peak (`startEnvelope`), which lifts blends of rows peaking in different places, as Crash 16"'s data notes describe (there the rows were aligned to one peak).
- **The ring steps up at the 30 s window's end**, by 15.3 dB (74.9 → 59.6 dB under the hit), where the project session measured about 7 dB (74 → 67). It stays far under the level at 1 s.
- **`edge_rms_correction` is at its cap in the five lowest bands** (×4, 89–224 Hz), as Crash 16"'s.
- **27 modes are below the lowest band (89 Hz)**, 25 of them excited, from 29.6 Hz.
- **EDGE stays below its SPEAR target** once capped (−2.95 dB against +19.25); at its target it would peak at 3.27 after the output gain.
- **BOW peaks at 0.280 at velocity 1.0** after the output gain (99th percentile, with its trim), above the 0.254 limit: BOW is never capped, by the rule.
- **`data/stine_build_info.json` has no `t60_refit` record** (null); the refit's numbers are in the manifest only. Its `edge_envelope_frames` and `edge_band_env_onset_s` are the builder's (586, 12.2 s), not the preset's (1500, 30 s).
- **The pk_t ramps have no effect** with the envelope on (as for the other NPZ presets), and **the calibration does not see the envelope**; the trims make up for the level.

## Hi-hat

Added 2026-11-20 (Kris approved the sound by ear on 2026-11-20, from prototype renders made in the project session). HI-HAT is a pair of 14" GF A60 hats, built from Kris's SPEAR captures (`docs/BASELINE_MANIFEST.md`, HI-HAT). It is not a V19 preset: the Python engine cannot load it. Its strike model is the approved reference, `docs/hihat_reference.py` (documentation only, not run in CI), and the plugin follows its knots, rules and constants. Strikes are not bit-identical to the reference, but the per-strike envelopes and mode weights agree with it to within float rounding (below). This is part 1, the sound and the playing. The editor's hi-hat UI (the wireframe, the pedal drawing and a PEDAL CC selector) is part 2.

### The model

- **Knots** (`presets/hihat/HiHat_A60.json`): ten captures, each a band envelope (the plugin's 22 one-third-octave bands, 10 ms frames) and each mode's weight within its band (683 modes, unit norm per band):
  - closed TIP and closed EDGE, each at velocities 0.3, 0.6 and 1.0 (V1–V3);
  - EDGE opened in four steps: openness 0.25, 0.5, 0.75 and 1.0 (V4–V7), at velocities 0.6, 0.6, 1.0 and 1.0 (matched by attack level to the closed edge);
  - since 2026-11-26, three foot splash captures (SplashV1–V3, `presets/hihat/data/hihat_splash.json`), openness 1.0, at velocities 0.72, 0.76 and 0.85 ("The foot splash" below).
- **A strike** at openness o and velocity v, for articulation TIP, EDGE or FOOT (`HiHat.envelope`):
  - its band envelopes are interpolated in log amplitude between the two knots around o (openness 0 is the closed knot), every knot padded to the longer of the two with 1e-9;
  - closed: between the two closed knots around v (0.3 / 0.6 / 1.0) in log amplitude; outside 0.3–1.0 the nearest knot, moved by the closed level law (each knot's level per band over the first 100 ms after its peak, linear in v between the knots and extrapolated with the nearest two outside);
  - an open knot is moved from its own velocity to v by the closed EDGE's level change per band;
  - TIP at an opening (there is no open tip capture): the EDGE's envelope there, with that open knot's own mode weights, times the closed TIP/EDGE level difference at v smoothed to its straight-line fit over the 22 bands (least squares on the band index); TIP OPEN LEVEL (a Tuning window value, the bake's 60 %) takes value × the knot's openness of that difference off at each open knot (the open modes and the smoothing since 2026-11-21, below);
  - FOOT: the closed EDGE's own envelopes (SPEAR's rise), at its velocity, whatever the openness;
  - SPLASH (since 2026-11-26): the splash knot nearest v, its own envelopes (SPEAR's rise) and weights, moved from the knot's velocity to v by the closed EDGE's level change per band, whatever the openness ("The foot splash" below);
  - each mode's amplitude is its weight × its band's envelope, the weights of the nearer of the two knots alone (the upper from halfway; between closed and the 0.25 knot the closed side's are the closed knots' at v, interpolated in v), linear between the 10 ms frames, for the envelope's whole length (up to 7 s). No in-frame decay, no 20 ms frame restart. One frame after the last, the strike has faded to 0 (a 10 ms fade) and ends;
  - a random start phase per mode per strike, from the strike-phase sequence, as on every preset.
- **The open modes** (Kris, 2026-11-21, by ear from the project session's plugin-engine renders, "version C"). With the hats mostly or fully open (CC about 100–127), TIP and EDGE sounded phasey, almost higher in pitch, worst on TIP. Three causes, each changed (TIP and EDGE with o > 0; closed hits and FOOT unchanged): (1) an open TIP knot took the open EDGE's envelope with the closed TIP's mode weights, the clamped hats' partials, which die within a fraction of a second on a closed hit but rang for seconds on an open one (in bands 0–8 and 9–14 the closed TIP's weights have a cosine similarity of only 0.29 and 0.52 with the fully open knot's): it now takes the open knot's own weights, as EDGE does; (2) the tip/edge level difference, from the closed captures' first 100 ms, jumps by up to 13 dB between neighbouring bands (at velocity 0.6, −23 dB at 141 Hz and +7 dB at 283 Hz), which over a long open ring sounded like a comb filter: it is now its straight-line fit, d̂_b = c0 + k b; (3) between two knots the blend of both knots' weights rang two slightly different sets of partials at once (the 0.75 and 1.0 knots' weights have a similarity of 0.29 in bands 0–8 and 0.66 in bands 9–14), and was not unit norm per band (those bands up to about 2 dB low): the nearer knot's weights alone now. The engine does it in `hiHatEnvelope` (`openKnot`, `closed` with separate envelope and weight shares).
- **The open-hat options** (Tuning window values, all off by default; Kris, 2026-11-21; `docs/TUNING.md`, "HI-HAT: the open-hat options"). FREE RING: the 1.0 knot plays as a single free cymbal, its weights from `presets/hihat/data/hihat_weights_free.json` (from Kris's data branch `presets-hihat-free`, commit d6c1683) and its stick envelope smoothed in the bake (`HiHat.free_envelope`; the knot's `stick_free` and `weights_free` in the bake, both versions baked, so it acts from the next strike); the fully open capture still has the bottom cymbal in it (its level pulses at about 1 and 4 Hz; about 7,200 short SPEAR tracks in 1.5 s against about 1,700 for a closed hit; only about 240 of its modes come from partials lasting 0.4 s or more). TIP MODES: an open TIP knot's weights times, per mode, (w_tip + 0.001) / (w_edge + 0.001) of the closed TIP's and EDGE's weights at v, clamped to ±value dB, each band back to the knot's norm (`hiHatEnvelope`, `closedWeights`). MAX OPEN: `setPedal` scales the pedal's openness, o = CC / 127 × value, so every openness rule and the drawing follow it (`pedal_openness` in the reference).
- **Stick firing.** SPEAR shows every strike rising over 20–40 ms (its analysis window), which sounds like the pedal closing the hats, not a stick. For TIP and EDGE each band's rise is replaced by its decay after the peak, extended back to t = 0, at most +9 dB above the captured peak (`_impulsive`: the peak within the first 5 frames, the slope over the 3 frames after it). FOOT keeps SPEAR's rise.
- **Velocity** is the engine's velocity: after Velocity Sensitivity, with HIT VARIATION's velocity spread; the articulation's velocity curve (`vel_0.6_db`, `vel_0.3_db`) is a level on top, as on every preset. Positions and the position spread do not apply.

### The pedal

- **Pedal CC:** a MIDI CC, default CC 1 (the mod wheel), settable to 1–127 per plugin instance (`CymbalisProcessor::setPedalCc`; saved in the session as `pedalCc`; not a host parameter; set in the editor's PEDAL box, part 2). It is read on every MIDI channel (since 2026-11-21; until then only on the instance's MIDI channel, which dropped a keyboard's mod wheel on channel 1 when the pads were on 16), at its sample; the notes are still read on the MIDI channel only: the processor renders up to it and hands its value / 127 to the engine (`FastEngine::setPedal`). A new cymbal (a preset switch) takes the pedal where it is. Before any CC the hats are closed (o = 0).
- **Openness:** smoothed with a 10 ms one-pole. The engine keeps it as segments (from each CC, an exponential towards its value), so the openness at any time is exact: a strike takes it at its note-on.
- **The choke.** While the pedal is more closed than at a ringing strike, that strike is damped further, per band, at the extra rate `max(rate(o_now), CHOKE_RATE × (o_strike − o_now) / o_strike) − rate(o_strike)`, never negative (`HiHat.choke_rate`). CHOKE_RATE = ln(1000) / 0.3 s (every band at least a 0.3 s T60 fully closed); rate(o) is interpolated in log between the measured band decay rates of the closed EDGE V3 and the four open knots, each band at least RING FLOOR × the median of the 22 at o (since the ring tails fix, below; RING FLOOR is a Tuning window value, the bake's 100 %: closed 7.3 per second, a 0.95 s T60; 0.25 and 0.5 open about 3.1, 0.75 open 1.5, fully open 1.7). The floor applies wherever a TIP or EDGE strike's rate is used (its ring-on past its captured envelope, the opening's target, the choke's target and reference), with RING FLOOR as it was at the strike's note-on (`hhRingFloor`); FOOT's choke keeps the raw rates (`hiHatRate`, `hiHatRateMedian`). Once a strike has opened up, its choke measures from the highest opening instead of o_strike (Opening, below). The engine applies it to the strike's band envelopes at every control point (about 2.9 ms), exactly as a piecewise exponential from the openness at each control point. Opening the pedal again stops the damping; it never brings level back.
- **Opening** (approved by Kris on 2026-11-21, prototype renders version B; `HiHat.level_step`): the pedal moves a ringing TIP or EDGE strike's decay both ways. Per strike and band the level L (natural log of amplitude) steps at every control point by `level_step(s_env, o_strike, o_now, dt, t_since_strike, opened, o_ref, at_floor)`, s_env being its envelope's own log step over the block as the strike reads it. A strike that has not opened up follows its envelope: closing, the choke above; otherwise s_env. Opening (o_now > o_strike + OPEN_EPS, OPEN_EPS = 0.004, half a CC step; from OPEN_AFTER = 30 ms after the strike) opens it up, for the rest of its life, and raises o_ref, the highest openness the pedal has opened it to (from o_strike), to o_now. From then on its level is a state of its own, stepped on every control block whatever the pedal does, from the envelope's step clamped, s_c = s_env clipped to ±STEP_MAX × dt (STEP_MAX = 50 per second); where either end of the step has the envelope at its floor, under FLOOR = 1e-6 of the strike's peak (−120 dB) or 0, s_c = −rate(o_strike) dt, as past the end of its captured envelope. Closing, from the highest opening (o_now < o_ref − OPEN_EPS; opening back up while still under it too, with the smaller extra rate): slope = (1 − w_ref) s_c − w_ref rate(o_ref) dt − extra dt, w_ref = min(1, (o_ref − o_strike) / OPEN_BLEND), extra = max(max(rate(o_now), CHOKE_RATE × (o_ref − o_now) / o_ref) − rate(o_ref), 0); opening, w = min(1, (o_now − o_strike) / OPEN_BLEND), OPEN_BLEND = 0.1, slope = (1 − w) s_c + w (−rate(o_now) dt), never above max(s_c, 0), so opening never makes a band louder (a captured bloom still shows) and a strike opened fully decays at rate(o_now) from the level it had; otherwise s_c. Past the end of its captured envelope a strike decays at rate(o_strike) (s_env = −rate(o_strike) dt), so a hit opened after it was struck rings on at the opening's rate instead of stopping when its closed data ends; it ends when every band is 120 dB under the strike's peak, or at 8 s at most with the 10 ms end fade. The engine carries this as the gain on the strike's envelope (`hhGain`), which may now also rise above 1 to cancel the envelope's own decay, bounded so that no band goes above its envelope's peak (`hiHatPedalStep`, the control loop, `hiHatEnvelopeAt`). Once the strike has opened up (`hhOpened`, set on its first opening block) the gain takes g1 = g0 · exp(slope − s) with s the envelope's raw step on every block, so v1 · g1 = v0 · g0 · exp(slope): the envelope's dropouts cannot move the level. Each strike keeps o_ref and the band rates there (`hhOpenRef`, `hhRateRef`).
- **The pedal bursts fix** (approved by Kris on 2026-11-21; the opened state, OPEN_EPS and STEP_MAX above). With fast pedal movement Kris heard a "phase swooshing or sucking" sound. SPEAR's partials stop and restart, so a knot's band envelope falls to its floor (ln 1e-9) for a few frames and comes back: one-frame drops of more than 26 dB occur 176 times in `edge_open_100`, 137 in `edge_open_75` and 44–71 times in each closed knot. Before the fix, the opening rule was applied as a gain only on blocks where the pedal was opening: during a dropout the envelope's step is about −20 in one block, so the gain rose by about w × 20, and when the pedal step changed before the envelope came back (w changed, or the pedal went still or closed) the gain was never brought back down. The band then came back up to e^20 too loud, held at its peak by the peak cap: a burst until the envelope dipped again. The smoothed pedal settling towards its target also put strikes struck with a held pedal into the opening branch (o 1e-5 above o_strike); OPEN_EPS keeps them out. The openness of a new strike is still taken at its note-on. FOOT is unchanged: only the choke applies to it, and it ends with its captured envelope (the prompt's "FOOT is unchanged"; it is the pedal closing the hats).
- **The ring tails fix** (approved by Kris on 2026-11-21; the floor rule, o_ref and the rate floor above). Kris heard the lowest band ring on for seconds after a hit (in his recording the low tail hangs on after 1.6 s), and in quick playing with the pedal opening and closing, a frequency still ringing with the hats fully closed and nothing playing (on TIP). Three causes. (1) Past the point where a band's partials stop, the captured envelope is flat at its floor (ln 1e-9), a step of 0; an opened strike followed that step, so it held its level until the 8 s limit (a TIP at velocity 0.7 struck closed, opened to 0.9 and closed again: 0.7–18 kHz held at about −30 dB re its peak from 1.3 s to about 4.5 s, with the hats closed). (2) A strike struck closed was never choked after the pedal opened it: the choke measured from o_strike (0, or about 2e-15 left over from the smoothing), so it had nothing to close from (an extra rate of about 8e-13) and rang on at its envelope's own rate. (3) The low bands' own rates are very slow: band 0 (89–112 Hz) 0.87 per second closed (T60 about 8 s) and 0.5–1.05 open, bands 1–8 2–5.5 closed; a strike rings on at rate(o_strike) past its captured envelope, and an opened one at rate(o). The prompt's "FOOT unchanged" is read as covering the rate floor too: FOOT's choke keeps the raw rates, and FOOT plays bit for bit as before.
- **Closed hits ring on past their data.** The closed captures end with their lowest band (around 89 Hz) still at about −50 to −58 dB under the hit's peak (TIP V1 −55, V3 −58; EDGE V1 −50, V2 −56; the louder ones at −73 to −94 dB), and its closed rate is slow (0.87/s, about 7.5 dB/s). With the rule above a closed hit no longer stops when its data ends: its lowest band rings on at that rate. A soft closed hit (velocity 0.3) therefore lasts to the 8 s limit, its tail from about −55 dB under its peak (−108 dB re full scale at 2 s, −132 dB at 4 s, before the output gain); the louder ones, whose data ends 120 dB down, end with their data (about 1.8–2.0 s) as before.
- **FOOT** has no row and no note: the pedal plays it. It fires when the smoothed openness falls below 0.05 after it was at 0.15 or more since the last FOOT (or since the start), at the sample where it crosses 0.05 (solved exactly within the pedal's segment). Its velocity is how fast the pedal closed: the openness drop per second over the last 50 ms, / 10, within 0.1–1.0 (`foot_velocity`). It strikes like any other articulation (its own level trim and tuning values), so the choke applies to everything ringing when the pedal closes, the FOOT included (it fires at openness 0.05 and the pedal goes on closing). Since 2026-11-26 a tap to closed too short for the smoothed openness to reach 0.05 fires it too ("The fast tap" below), and a FOOT arms the foot splash ("The foot splash" below).
- No pedal-chick note (Kris).

### The foot splash

Added 2026-11-26. Kris approved it by ear on 2026-11-25, from the project session's renders made with a prototype in a copy of `FastEngine` (B: "B sounds right"). A foot splash is the pedal pressed down quickly and let up, so the hats close briefly, open, and ring with the foot up. Kris: "pedal only" (no note or row), the timing as tuning values, and the captures may all be at about the same level ("it's hard to get dynamics in this articulation").

- **Before:** the close fired FOOT (the chick) and choked what rang; the FOOT is choked by its own close ("Things in the hi-hat data or model that look wrong" below), and reopening played nothing. So a splash came out as a chick and then silence.
- **Data:** `presets/hihat/data/hihat_splash.json`, from Kris's data branch `presets-hihat-splash` (commit e4a5ea2; its MD5 and note in `docs/BASELINE_MANIFEST.md`, HI-HAT). It holds three knots from the captures SplashV1–V3, in the hi-hat's knot format:
  - velocity 0.72, 0.76 and 0.85, matched by attack level to the closed EDGE;
  - openness 1.0 (the capture ends with the foot up);
  - 961–963 frames (9.6 s) of the 22 bands, and the 683 modes' weights.
- **Bake** (`tools/bake_hihat.py`):
  - the knots, with articulation `splash`: their envelope as both `stick` and `raw` (the splash keeps SPEAR's rise, as FOOT does), their weights, and `level_db` (`_lvl100`);
  - the entry SPLASH: articulation `splash`, position 0.0, no row and no note, no stick click, EDGE's level trim (`--same-as SPLASH=EDGE`) and its own skip threshold factor (`cymbalis_trims --skip-thresholds`, 4.75e-9);
  - SPLASH WINDOW 150 ms and SPLASH REOPEN 25 % in the tuning block (`hh_splash_window_ms`, `hh_splash_reopen`; `docs/TUNING.md`, "HI-HAT: the foot splash").
  - Every existing entry, knot and value stayed as it was: the bake's diff against #76's has only additions, apart from the `python.platform` line.
- **Trigger** (`process`, `footStrike`, `splashStrike`):
  - After a FOOT fires, the splash is armed until the FOOT's time + SPLASH WINDOW, with the window and REOPEN as they are at the FOOT.
  - If the smoothed openness reaches SPLASH REOPEN within that time, SPLASH is struck at the sample where it crosses. The crossing is solved exactly within the pedal's segment, as FOOT's is, so `process` splits the block there. If the openness is already above REOPEN, the strike comes at once (after a fast tap from wide open, below).
  - Past the window it is disarmed, and nothing changes from before. One splash per FOOT.
- **Strike** (`strikeHiHat`, `hiHatEnvelope`):
  - Velocity: the FOOT's (how fast the pedal closed), with HIT VARIATION as on every strike.
  - Knot: the splash knot nearest that velocity, its raw envelope and weights. It is moved from the knot's velocity to the strike's by the closed EDGE's level change per band, as an open knot is moved.
  - SPLASH's articulation values apply as FOOT's do: level, velocity curve, brightness vs velocity, band gains, attack, onset shift. It has no stick click.
  - **Its envelope time starts at the FOOT's time.** The strike reads its envelope at (t − t_FOOT) (`hhEnvT0`), so the FOOT is its attack and the wash picks up where the recording is at that moment, with no added delay. The FOOT itself plays as before.
  - It rings on past its envelope as TIP and EDGE do, at the open rate, up to `kHiHatMaxRingS` (8 s), with the end fade. RING FLOOR applies as on TIP and EDGE.
  - It draws no hit, as FOOT.
- **Choke** (`hiHatPedalStep`, `hhSplash`). This replaces the TIP/EDGE choke and opening rule, for SPLASH only:
  - It is struck as fully open: o_strike = 1.0, its rates at 1.0.
  - It keeps o_max (`hhOMax`), the highest smoothed openness since its strike, starting from the openness at the strike.
  - Each control block, every band takes an extra decay of max(max(rate(o_now), CHOKE_RATE × (o_max − o_now) / o_max) − rate(1.0), 0).
  - So closing chokes it like any open hit, a foot resting part-way open makes it die at least as fast as an open hit at that opening would, and fully open it plays as captured.
  - The opening rule never applies to it.
- **Why this design** (the prototype's): the splash captures hold the close's chick and the hats reopening, but the plugin strikes SPLASH only where the pedal has reopened, 40–90 ms after the FOOT in the figures below. Reading the capture from the FOOT's time joins the recording where the pedal is, after the FOOT that already played the chick, so nothing is doubled or delayed. Striking it as fully open with its own o_max lets the pedal choke it, whatever openness it was struck at.
- **The prototype's figures and the plugin's.** Mono mean of both channels, levels in dB re full scale after the output gain, HIT VARIATION as baked, windows after the stomp; "today" is main before the splash, "now" is this engine, in brackets the prototype's:

  | Gesture | 0.2–0.6 s, today / now | 1.0–1.5 s, today / now | 2–3 s, today / now |
  |---|---|---|---|
  | Fully open: 1 → 0 over 30 ms, 30 ms closed, 0 → 1 over 70 ms | −72 / −48 (prototype −48) | −93 / −59 (−59) | −106 / −65 (−65) |
  | Half open: 0.5 → 0 over 100 ms, 30 ms closed, 0 → 1 over 100 ms | −81 / −65 (prototype −64; its "today" −80) | −99 / −77 (−77; −100) | −215 / −85 (−85; −200) |
  | Held closed 0.4 s before the release | −91 / −91 | −159 / −159 | −177 / −177 |
  | Splash, closed again 1.2 s later | −72 / −48 | −49 / −49 (the second close's FOOT) | −231 / −231 (choked, silence) |

  Held closed, no splash is struck, and it is bit for bit as before.

### The fast tap (FOOT)

Added 2026-11-26, with the foot splash, as part of the same approved spec.

- **The fault:** the pedal is smoothed with a 10 ms one-pole. When the CC touched closed (below `footClosedBelow`, 0.05) for less than about 15–20 ms and came back up, the smoothed openness never got below 0.05, so no FOOT fired and a fast tap played nothing. The project session's first splash renders, with a 5 ms tap, were silent in the plugin as it was.
- **The fix** (`setPedal`, `footTapLow_`): while FOOT is armed, if a pedal CC has gone below 0.05 since the last FOOT (or since arming), and a later CC is back at or above 0.05 before the smoothed openness crossed 0.05, FOOT fires at that later CC's sample. Its velocity comes from the smoothed openness, as before. A CC set at once (a new cymbal, MAX OPEN) clears it.
- **What stays the same:** every FOOT the smoothed crossing fires is exactly as before, at the same sample with the same velocity. Only the taps that played nothing gain a chick, a few ms late (the tap's dwell). The splash rule then applies to such a FOOT like any other. TIP and EDGE's choke and opening still follow the smoothed openness.

### Bake and plugin

- **Bake** (`tools/bake_hihat.py` → `resources/presets/hihat.bake.json`, packed like the others; `docs/BUILDS.md`). It runs the reference's preparation (stick envelopes, levels, band rates) and writes the plugin's bake format: the shared per-mode arrays (frequencies; each mode's T60 from the fully open knot's decay rate of its band, ln 1000 / rate, for what the engine still uses T60 for: the cascade component; `exc_norm` the fully open knot's weights, the cascade's share weights; position weights 1; pick vectors and LFO tables drawn as V19 draws them, seed 14), four entries and a `hihat` block (per knot its stick envelope, the closed EDGE knots' and the splash knots' raw envelopes, its weights and its level per band; the band decay rates; the choke rate, the FOOT rules and the pedal smoothing). Entries: `TIP` (articulation `tip`, position 0.5), `EDGE` (`edge`, 0.0), `FOOT` (`foot`, 0.0), `SPLASH` (`splash`, 0.0; since 2026-11-26). No contact, onset ramp, velocity table or NPZ data. Its tuning block: output gain −11 dB and HIT VARIATION on, as every preset.
- **Engine** (`FastEngine`; the other presets' paths are untouched): every hi-hat strike is an envelope strike whose envelopes are its own, interpolated at the strike (about 0.15–0.2 ms; `strikeHiHat`, `hiHatEnvelope`), not on the per-sample path. Each mode's coupling is its weight × the envelope's peak, with the strike's phase and level; the envelope, divided by its peak, scales it at every control point, times the choke. The shared oscillator carries no decay. The pool holds 48 hi-hat strikes (24 on the other presets) and never merges them: when it is full, the quietest strike's slot is taken.
- **Preset** HI-HAT (placeholder name until Kris names it), id `hihat`, after CRASH 16" (`plugin/SlotTable.h`; after KONSTAN CRASH 18" until 2026-11-22). Rows: BOW plays TIP (stick TIP, fixed), EDGE plays EDGE (SHOULDER, fixed), on General MIDI's closed and open hi-hat notes, 42 (F#1) and 46 (A#1). No BELL, BELL2 or BOW2 rows. The hi-hat keeps its own notes, apart from the notes the cymbals share (`docs/UI_SPEC.md` sections 7–9). The Blend Position knobs do nothing on it (each row strikes at its base position), and the editor hides them on it; its wireframe (the two hats opening with the pedal), its pedal drawing and its PEDAL CC box came with part 2, the editor (`docs/UI_SPEC.md` sections 3 and 11, 2026-11-20).

### Levels and the stick click

- **Level trims** (`tools/cymbalis_trims`, `docs/BUILDS.md`): velocity 1.0, the first 500 ms, against Nova's BOW (tip), the hats closed. EDGE's target is 0 dB, capped where one hit (99th percentile of 1024) would peak above 0.9 before the output gain (`--peak-limit 0.25365`); TIP, FOOT and SPLASH take EDGE's trim (`--same-as`), so they keep their captured levels relative to EDGE. SPLASH (2026-11-26) was given EDGE's trim as stored; the other three trims were kept, not measured again.

  | Entry | Trim | Level vs Nova BOW (tip), first 500 ms | Peak of one hit with trim (99th percentile), after the −11 dB | Capped |
  |---|---|---|---|---|
  | EDGE | −3.85 dB | −9.88 dB | 0.254 | yes (+6.03 dB would match BOW) |
  | TIP | −3.85 dB (EDGE's) | −22.33 dB | 0.076 | no (EDGE's trim) |
  | FOOT | −3.85 dB (EDGE's) | −14.69 dB | 0.091 | no (EDGE's trim) |
  | SPLASH | −3.85 dB (EDGE's) | −20.72 dB (struck alone with the hats closed, from its envelope's start; the plugin strikes it only from the pedal) | 0.041 | no (EDGE's trim) |

- **Stick click** (the per-articulation click values): TIP 1500–16000 Hz, 12 ms, decay 1.2 ms; EDGE 900–12000 Hz, 15 ms, decay 2.5 ms; FOOT and SPLASH none; position weights 1. Each gain is set so that on a closed hit at velocity 1.0 the click's first-3 ms RMS is 3 dB under the cymbal's own (mean power over 256 hits, measured with FastEngine in memory): **TIP 0.1398, EDGE 0.4189**. They started at 6.0, which put the click 29.6 dB (TIP) and 20.1 dB (EDGE) above the hit's first 3 ms.

### What the Tuning window acts on

Per articulation (TIP, EDGE, FOOT, SPLASH): level, velocity curve, brightness vs velocity, excitation band gains, attack band gains and time, onset delay and speed (on the strike's band envelopes, as on an NPZ edge strike), stick click gain, band, length and decay. Hidden for the hi-hat: the stick click's position weights (they stay 1). Per cymbal, as on every preset: output gain, ring length and ring length per band (they scale each band's envelope time after the kept onset), kept onset, HIT VARIATION and its velocity spread, stereo, wander, MF and LF build-up, energy cascade, loud-band damping (their 0 dB reference: a closed velocity-1.0 TIP hit at its peak), re-hit damping. Its own (2026-11-21): TIP OPEN LEVEL, TIP OPEN TICK (the TIP's stick click raised by value × the strike's openness, dB) and RING FLOOR, starting at 60 %, +4 dB and 100 % (`docs/TUNING.md`, "HI-HAT: the open tip and the ring floor"); FREE RING, TIP MODES and MAX OPEN, all off (`docs/TUNING.md`, "HI-HAT: the open-hat options"); SPLASH WINDOW and SPLASH REOPEN, starting at 150 ms and 25 % (2026-11-26; `docs/TUNING.md`, "HI-HAT: the foot splash"). Hidden: POSITION SPREAD, the edge capture curve, `tau_ref`, EDGE VOICE FROM BOW and the edge envelope window and switches (`docs/TUNING.md`).

### Checked (in memory, 2026-11-20)

- **The envelopes follow the reference**: over 114 strikes (TIP and EDGE at openness 0, 0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9, 1.0 and FOOT, each at velocities 0.1, 0.3, 0.45, 0.6, 0.8, 1.0), the engine's band envelopes match `HiHat.envelope` to 6e-8 relative (float storage) wherever they are above −120 dB, and the mode weights to 3e-16.
- **Onset**: closed TIP and EDGE at velocity 1.0 peak at t = 0 (rendered without the click, mean of 64 hits: within 0.5 dB of their loudest from the first 0.25 ms); FOOT rises over 28 ms (−20 dB to its peak at 30 ms).
- **Opening** (in memory, 2026-11-21; stick clicks on, HIT VARIATION off; levels re full scale before the output gain, mean square over 50 ms): a closed EDGE hit at velocity 0.8, the pedal opening fully 0.1 s later over 0.15 s: −69.8 dB 0.5 s after the hit, against −83.5 dB kept closed (+13.7 dB; the prototype: about −48 against −65, +17 dB); three EDGE hits 0.2 s apart while the pedal opens over 0.6 s and stays open: −55.7 dB 1.8 s after the first, against −63.3 dB before the change (+7.6 dB; the prototype +8 dB). The plugin's absolute levels are lower than the prototype's figures (its trims and output gain differ); the direction and size agree. Opening never raises a band: over every control block of TIP and EDGE hits at velocity 0.3, 0.8 and 1.0 with the pedal swept open, half shut and open again (22,726 band steps each, 10,000–15,000 of them with the gain raised against the envelope's decay), no band's log step exceeded max(its envelope's, 0). The choke figures below are unchanged to the decimal. Every other preset's engine output is bit-identical (`tools/cymbalis_identity`).
- **The pedal bursts fix** (in memory, 2026-11-21; HIT VARIATION off). The burst case, an EDGE hit at velocity 0.9 struck at pedal 0.5 and the pedal rocked 0.2 ↔ 0.8 (period 0.5 s, a CC every 8 ms, sine and triangle, eight phases each; no FOOT fires): before the fix the loudest rise of the four bands (0–400 Hz, 400–2000 Hz, 2–8 kHz, 8–20 kHz) within 30 ms after the first 0.1 s was 70–94 dB in each of the sixteen runs; after it, at most 8.5 dB, measured from the modes' amplitudes summed in power (no beating between modes), at −104 dB re full scale or lower in the low bands, where the pedal crosses the strike's openness during a dropout. Measured from the output (STFT, 1024 points), at most 17.6 dB; the same hit with the pedal held, which the fix does not change, shows up to 14 dB the same way: beating between modes. Per control block no opened strike's band rises more than 1.26 dB (STEP_MAX × 2.9 ms), there and in 10 s of random fast playing (three seeds; before: up to 115 dB). Closed hits, and the choke on hits that never opened, are bit for bit as before (TIP and EDGE at 0.3–1.0; struck at 0.25–1.0 and closed in 60 ms). Hits with a held pedal match: struck 0.1 s or more after the pedal moved, within −77 dB of their peak, most bit for bit; struck while it still settles within OPEN_EPS (60 ms after a move), within 0.1 dB in level. The two opening expectations above are within 0.07 dB of before (−69.70 against −69.77 dB 0.5 s after the hit; −55.72 dB 1.8 s after the first of three, as before). Every other preset is bit-identical (`tools/cymbalis_identity`). Cost: `cymbalis_bench` 4.2–4.7 % of a core at 44.1 kHz and 6.7–6.8 % at 96 kHz; dense random playing with the pedal moving every 20–120 ms renders at 21–25× real time (before: 27–31×).
- **The ring tails fix** (in memory, 2026-11-21; HIT VARIATION off; band groups 89–700 Hz, 0.7–2.8 kHz and 2.8–18 kHz from the modes' amplitudes summed in power, re the strike's peak). The quick case, a TIP at velocity 0.7 struck closed at 1.00 s, the pedal opening to 0.9 over 1.00–1.05 s and closing over 1.20–1.26 s: before, −44, −30 and −26 dB at 2.0 s; after, −72, −72 and −60 dB at 1.5 s and −172, −185 and −207 dB at 2.0 s, all three under −120 dB from 1.75 s (before: still above it at 6 s). After the close every band decays at the full choke (at least CHOKE_RATE, about 200 dB/s), so at 2.0 s the two upper groups are far under the prompt's "about −70 and −60 dB"; those figures match a decay at the floored closed rate (7.3 per second) instead. A closed TIP at velocity 0.8 opened to 1 over 0.30–0.38 s and closed over 0.70–0.76 s: at 1.5 s −172, −185 and −207 dB (before: −62, −73 and −66), under −150 dB from 1.39 s. A closed EDGE at velocity 0.8 with the pedal held closed is bit for bit as before until its captured envelope ends (2.17 s), then its low tail is shorter (it ends before 2.3 s; before, it was at −122 dB at 2.3 s). Strikes with a held pedal (TIP and EDGE, openness 0–1) are bit for bit as before until their captured envelopes end, and FOOT (alone, and fired by a fast close) bit for bit throughout. An open EDGE hit closed in 60 ms is choked as before within 0.2 dB (10.2, 37.7 and 75.4 dB under the hit unchoked 0.1, 0.2 and 0.4 s after the close). Random fast playing until 3.0 s, then the pedal closed: the output 3.6–4.2 s, from −77 to −158 dB re its loudest 50 ms before (eight seeds), to −146 to −168 dB; played until 3.5 s with FOOT off, its last 0.2 s (4.0–4.2 s) from −70 to −123 dB to −109 to −127 dB. No new jumps: with FOOT off, the largest 30 ms rise of a group away from the hits is 0.7–2.6 dB with the pedal moving and 1.8–2.4 dB with the same hits and the pedal held (eight seeds); per control block no opened strike's band rises more than 1.21 dB; the pedal bursts check gives at most 2.6 dB (before: 8.1 dB). The two opening expectations above come out lower, as opened low bands now decay at least at the median rate: −70.23 dB 0.5 s after the hit (−0.5 dB; −77.92 at 1.0 s, −1.1; −85.94 at 1.5 s, −2.4) and −59.14 dB 1.8 s after the first of three (−3.4 dB; −46.02 at 1.0 s, −1.3; −65.07 at 2.2 s, −5.9). The engine's floored rates match `HiHat.rate` to four decimals. Every other preset is bit-identical (`tools/cymbalis_identity`). Cost: `cymbalis_bench` 3.2 % of a core at 44.1 kHz and 5.4–5.7 % at 96 kHz; dense random playing with the pedal moving every 20–120 ms renders at 35–41× real time (before: 28–31×; the tails end sooner).
- **TIP OPEN LEVEL, TIP OPEN TICK, RING FLOOR** (in memory, 2026-11-21; HIT VARIATION off). At 0 %, 0 dB and 100 % the hi-hat plays bit for bit as before them (`tools/cymbalis_identity`'s patterns, and open TIP and EDGE hits held at openness 0.25–1, closed hits opened and closed again, and 9 s of random fast playing with the pedal moving, against main). Closed hits (TIP and EDGE at 0.3, 0.8 and 1.0) are bit for bit the same at 0 % / 0 dB, 60 % / +4 dB and 100 % / +12 dB, and so are TIP strikes struck closed and opened later. A fully open TIP against a fully open EDGE at velocity 0.8 (stick clicks on, the first 300 ms): −8.5 dB at 0 % / 0 dB, −4.2 dB at the bake's 60 % / +4 dB (−5.5 dB at 60 % open), −2.7 dB at 80 % / +6 dB. The click rises by exactly value × o (+4.00 dB fully open and +2.00 dB half open at +4 dB). The open TIP's band envelopes match `HiHat.envelope(…, tip_open_level)` to 6e-8 relative at TIP OPEN LEVEL 0, 60 and 100 %. RING FLOOR, a closed EDGE at velocity 0.8 with the pedal closed, band 0 past its captured envelope (2.17 s): 0.87 per second at 0 % (the raw rate; the strike ends at 2.97 s), 3.5 at 50 %, 7.1 at 100 %, 14.1 at 200 % (ends at 2.36, 2.27 and 2.22 s). The PR #56 check points at 0 %, 100 % and 200 %: a closed EDGE opened fully, −69.70, −70.23 and −76.08 dB 0.5 s after the hit (−76.80, −77.92, −90.47 at 1.0 s; −83.59, −85.94, −105.32 at 1.5 s); three EDGE hits while the pedal opens, −55.72, −59.14 and −79.08 dB 1.8 s after the first (−44.73, −46.02, −54.81 at 1.0 s; −59.16, −65.07, −90.79 at 2.2 s). At 0 % they are the figures from before the ring tails fix. Every other preset is bit-identical (`tools/cymbalis_identity`). Cost: `cymbalis_bench` 3.2–3.3 % of a core at 44.1 kHz and 5.6–6.0 % at 96 kHz, at RING FLOOR 100 % and 0 % alike; dense random playing with the pedal moving every 20–120 ms renders at 32–39× real time at 0, 100 and 200 %.
- **The open-hat options** (in memory, 2026-11-21; HIT VARIATION off). At the defaults (FREE RING off, TIP MODES 0, MAX OPEN 100 %) the hi-hat plays bit for bit as before (open TIP and EDGE hits at openness 0.8–1.0, closed hits, a fast close firing FOOT, FOOT struck alone, a closed TIP opened and closed, 9 s of random fast playing with the pedal moving, three seeds; and `tools/cymbalis_identity`). With FREE RING on, TIP MODES 20 dB and MAX OPEN 75 %, closed hits (TIP and EDGE at 0.3–1.0) and FOOT struck alone are bit for bit as at the defaults (FOOT fired by a close from CC 127 changes with MAX OPEN, the pedal starting lower). FREE RING alone: TIP and EDGE at openness 0.25, 0.5 and 0.75 bit for bit, from 0.76 on changed (the strikes that use the 1.0 knot). TIP MODES alone: EDGE and a TIP on the closed side (openness 0.1) bit for bit; open TIP strikes' band norms as the knot's to 1e-15 (openness 0.3–1.0, velocity 0.45–1.0, 6 and 20 dB). MAX OPEN 80 %: CC 127 gives openness 0.800, set at once or smoothed. The engine's envelopes and weights match `HiHat.envelope(…, free_ring, tip_modes_db)` to 6e-8 and 5e-16 relative (576 strikes). The re-baked hi-hat reproduces every existing array of the bake bit for bit; it adds the 1.0 knot's `stick_free` and `weights_free` and the source's MD5. Every other preset is bit-identical. Cost: `cymbalis_bench` 3.4 % of a core at 44.1 kHz and 5.8 % at 96 kHz; dense playing with fast pedal moves at 37× real time at the defaults, 31–37× with the options on.
- **The open modes** (in memory, 2026-11-21; HIT VARIATION off). An open TIP minus an open EDGE at CC 127 (openness 1), velocity 0.9, 0.15–1.5 s, per excitation band, at TIP OPEN LEVEL 0: before, mean −9.0 dB, a line from −6.7 to −11.3 dB, a standard deviation around it of 3.1 dB and jumps of up to 11.7 dB between neighbouring bands (the output, STFT; from the modes' amplitudes 3.2 dB); after, mean −8.8 dB, a line from −6.8 to −10.7 dB, 0.02 dB around it (the two now ring the same modes with the same envelope, so only the fit remains). At the bake's 60 %: mean −3.5 dB, from −2.7 to −4.3 dB (before: −3.7 dB, 2.3 dB around its line). The engine's band envelopes still match `HiHat.envelope` to 6e-8 relative and its mode weights exactly (TIP, EDGE and FOOT at openness 0–1 and velocities 0.1–1.0, TIP OPEN LEVEL 0 and 60 %); every open strike's weights are exactly one knot's, except between closed and the 0.25 knot below its middle, where they are the closed knots' at v. An EDGE at CC 100 (openness 0.79, between the 0.75 and 1.0 knots), velocity 0.8, 0.05–1.0 s, per band against before: +0.9 to +1.0 dB in bands 0–6 and 8, +0.3 to +0.7 dB in 7 and 9–12, under +0.3 dB above (the modes' amplitudes; in the output up to +1.4 dB): the blend's lost norm. Closed hits (TIP and EDGE at 0.3–1.0), FOOT (fired by a fast close) and EDGE held at a knot's opening (0.25, 0.5 and 1.0, velocity 0.5–0.9) are bit for bit as before. Every other preset is bit-identical (`tools/cymbalis_identity`). Cost: `cymbalis_bench` 3.4 % of a core at 44.1 kHz and 5.9 % at 96 kHz; dense playing with the pedal moving every 20–120 ms at 33–34× real time (before: 35–38×).
- **The foot splash and the fast tap** (in memory, 2026-11-26; `tools/cymbalis_identity/run.sh --splash`: this engine against main's, 128-sample blocks, HIT VARIATION as baked).
  - **Every other preset** is bit for bit as main: the identity sequences at 44.1 and 96 kHz, 128- and 64-sample blocks, with the bake's tuning and with the cascade and both dampings on.
  - **HI-HAT without a splash or a fast tap** is bit for bit as main, 0 samples differing at 44.1 and 96 kHz:
    - TIP, EDGE and FOOT every 24 ms with the dense bench's pedal (20 FOOTs);
    - TIP and EDGE every 125 ms with the pedal opening and closing over 4 s (7 FOOTs);
    - open 1 s on EDGE, closed over 30 ms and held 2 s on TIP, repeated (6 FOOTs);
    - open/closed 8ths at 120 bpm: EDGE open on the "and", the pedal closing on the beat and reopening 200 ms after it (30 FOOTs, no splash).
    - None of these patterns, nor the benches' pedal patterns, has a splash gesture or a fast tap: each plays the same FOOTs as before and no splash.
  - **The 8ths by SPLASH WINDOW:** at 100 and 150 ms no splash, bit for bit; at 250 ms a splash after each of the 30 FOOTs.
  - **The gestures:** the table in "The foot splash" above, within 1 dB of the prototype's figures.
  - **The join:** see "The splash's join is a level rise, not a click" below.
  - **The fast taps,** from CC 25 to CC 0 and back:
    - 5 and 10 ms played no FOOT before and play one now;
    - 20 and 40 ms (the smoothed openness reaches 0.05) play one FOOT, bit for bit as before;
    - a 5 ms tap from CC 127 plays one FOOT and one SPLASH, at the same sample (below).
  - **Cost:** the splash gestures render at 65× real time on one thread. `cymbalis_bench` shows 3.3 % of a core at 44.1 kHz and 5.9 % at 96 kHz, every slot playing (SPLASH among them, struck directly) with the pedal moving; that is about 3.4 % and 5.9 % before. `--dense` (a strike every 24 ms, SPLASH every fourth, struck directly), 44.1 kHz / 128: median block 254 µs, p99 842 µs, strike() mean 295 µs. Main's engine and bake give 241, 787 and 250 µs; this engine with main's bake (no SPLASH) gives 246, 806 and 245 µs, the same within the runs' spread. SPLASH's strikes cost more: its envelopes are 9.6 s long.
- **Choke**: an open EDGE hit (openness 1, velocity 1.0), the pedal closed in 60 ms (CC 127 → 0 every 5 ms) at 0.5 s: 12 dB under the same hit unchoked 0.1 s after the close, 39 dB at 0.2 s, 77 dB at 0.4 s (the reference's formula: 77.6 dB for the open hit at 0.4 s); 98 dB under the hit's peak at 0.4 s, nothing above −120 dB of it from 0.55 s after the close, silence by 1 s. The FOOT the close fires (at 0.570 s, velocity 1.0) is choked too.
- **FOOT**: once per close, at the sample where the smoothed openness crosses 0.05 and with the velocity from the closing speed, as computed independently per sample (open then closed in 60 ms: velocity 1.0; closed over 1 s: 0.107; a jump; never above 0.12: none; a wobble under 0.15 after a close: one; reopened to 0.3 and closed: two, the second at 0.499).
- **The pedal CC** is saved and restored with the session (`pedalCc`), as are the hi-hat's notes (`note_hihat_BOW`, `note_hihat_EDGE`); an older session gets CC 1 and 42 / 46. Through `processBlock`: notes 42 and 46 play, the cymbals' BOW note (62) does not, another CC fires no FOOT, the pedal CC closing does (on any channel since 2026-11-21).
- **Every other preset plays bit for bit as before** (`tools/cymbalis_identity` against main: all six, 44.1 and 96 kHz, 128- and 64-sample blocks, with the default tuning and with the cascade and both dampings on), and the packed bakes round-trip (`tools/pack_bake`, at build time).
- **Cost** (`cymbalis_bench`, Linux container, Xeon 2.1 GHz): every slot playing with the pedal moving (a CC every 10 ms, open and closed every 1.5 s), 3.9 % of a core at 44.1 kHz and 7.1 % at 96 kHz (256-sample blocks); a strike every 24 ms with the pedal moving (`--dense`): blocks with a strike median 0.34–0.45 ms, strike() 0.18 ms mean. The pool is full in that dense case (48 strikes of up to 7 s), and the quietest strike is replaced.

### Things in the hi-hat data or model that look wrong (ported as they are)

- **The choke is stronger than the prompt's expectation.** The prompt expects an open EDGE hit, closed in 60 ms, to be "about 46 dB down 0.4 s after the close". The reference's formula (CHOKE_RATE, at least a 0.3 s T60 fully closed: about 200 dB/s) gives about 77 dB under the same hit unchoked at 0.4 s, in the reference and in the plugin alike, and about 100 dB under the hit's peak. The closest figure to 46 dB is the FOOT chick the close fires, *without* the choke: 50 dB under the open hit's peak at 0.4 s. With the choke, the FOOT is damped too.
- **The FOOT is choked by its own close.** It fires at openness 0.05 and the smoothed pedal goes on to 0, so (o_strike − o_now) / o_strike reaches 1 within about 30 ms, and its low bands (rate about 1/s) are damped at about 22/s: the FOOT's captured tail (2.2 s, a low body around 150–300 Hz) is gone 0.4 s after the close (107 dB under the open hit's peak, against 50 dB without the choke). If the FOOT should ring on, it would need to be exempt from the choke of the close that fired it.
- **Ring length is not monotonic in openness between every pair of knots.** Measured as the time for a strike's total energy to fall 40 or 60 dB under its peak (TIP and EDGE, velocities 0.3, 0.6 and 1.0, steps of 0.01): 36 of 48 knot intervals stay within their end knots' ring lengths (to 10 ms). The others dip or overshoot, the same in the reference (whose envelopes the plugin matches): closed → 0.25 at velocity 0.3, EDGE's −40 dB ring goes from 1.84 s down to 0.43 s at openness 0.03 and back up to 1.42 s (the soft closed knot's low body keeps the whole strike above −40 dB for 1.8 s; a little of the open knot's brighter, louder early part raises the peak); 0.75 → 1.0, the −60 dB ring dips 0.07–0.42 s under 6.3 s; 0.25 → 0.5 at velocity 1.0, 0.14 s under 3.33 s. Log interpolation of envelopes whose peaks and spectra differ does this.
- **The knots themselves do not ring longer as they open**: at −60 dB the 0.75 knot rings 6.9 s and the fully open one 6.3 s; at −40 dB the soft closed EDGE (1.84 s) outlasts the 0.25 opening (1.42 s). V4 and V5 (0.25 and 0.5) measure almost the same (`docs/BASELINE_MANIFEST.md`).
- **EDGE stays 9.9 dB under Nova's BOW** over the first 500 ms: the peak cap (0.9) holds it at −3.85 dB, against +6.03 dB to match. The stick firing puts a hit's peak at its very start, so its peak-to-RMS ratio is high. TIP is 22.3 dB under Nova's BOW (12.5 dB under EDGE, as captured).
- **The stick click's starting gain (6.0) was 20–30 dB too loud** against these hits (above): the hats' envelopes are absolute SPEAR amplitudes and their calibration scalar is 1, where V19's presets scale the click with a calibration of about 0.07.
- **What the pedal bursts fix left** (2026-11-21, reported). Each fast close fires a FOOT, as designed. A hit that rang past its captured data continued at its openness's band rates, the lowest band's 0.87 per second closed (T60 about 8 s): the ring tails fix floors the rates at the median (above). A strike that never opened still plays its envelope's dropouts as captured. An opened strike could still rise by up to 8.5 dB within 30 ms where the pedal crossed its openness during a dropout (the blend w fell between the dropout's fall and its return); since the ring tails fix, closing blends to the rate at the highest opening, and the same check gives at most 2.6 dB.
- **Dense playing fills the pool.** A strike every 24 ms keeps 48 strikes of up to 7 s ringing; the quietest is replaced without a fade. At ordinary playing (8–12 strikes per second, mostly closed) the pool holds them all.
- **A fast tap from wide open plays FOOT and SPLASH together** (2026-11-26, reported). With the pedal at CC 127, a 5 ms tap to closed brings the smoothed openness down only to about 0.6. The fast-tap FOOT fires at the rising CC; the openness is then still above SPLASH REOPEN, so SPLASH is struck at the same sample, its envelope from its start (the FOOT's time is now). Physically that is a very short splash, so it is left as the spec gives it. If a splash should need the openness to have dipped below REOPEN first, that would be a change to the trigger.
- **The splash's captures outlast the strike's 8 s.** The captures are still 43–47 dB under their own peak at 8 s (9.6 s of data). A splash with the foot left up ends at `kHiHatMaxRingS` (8 s after its strike) with the 10 ms end fade, about 20 dB under its level at 2–3 s (−65 dB re full scale in the fully open gesture, so about −85 dB). This is as the spec gives it ("up to `kHiHatMaxRingS`"); a longer limit would be a change.
- **The splash's join is a level rise, not a click** (2026-11-26). In the fully open gesture the splash is struck 87.2 ms after the stomp (38.3 ms after the FOOT). Over 5 ms windows high-passed at 1 kHz, the output steps up by 2.7 dB where the splash enters (−38.8 to −36.1 dB re full scale). The prototype's check asked for 2 dB. The splash alone enters at about the FOOT's level (−39.7 dB in its first 5 ms, then −40.0, −39.6, −41.0, −41.3 and −41.6: no onset peak), so two sounds of about equal level add up to about +2.7 dB. The FOOT alone already steps by up to 2.6 dB between 5 ms windows over the 30 ms before.
- **The FOOT is still choked by its own close.** With a splash, the FOOT chick stays as it was, and the ring after it is the splash's.

## NOVA RIDE edge v2

NOVA RIDE gets a real EDGE, built from Kris's new edge captures. Kris approved it by ear on 2026-11-27 from the project session's renders; the task prompt is the spec. NOVA never had edge captures: since 2026-10-17 its EDGE row played the bow shoulder at the edge (decision 13 above; `COPIED_ENTRIES`). Kris recorded four edge hits (Edge V1–V4, with some room noise; the captures stay out of the repo). The project session built the edge from them on NOVA's own modes, as RAVEN's and the others' edges were built. The data is `presets/nova/edge_v2/` (data branch `presets-nova-edge-v2`, commit `50168a8`; `docs/BASELINE_MANIFEST.md`, its note and MD5s).

### The data

- **The matching.** Each capture's partials go to their nearest NOVA mode within ±0.25 %; partials below 90 Hz and at 110–122 Hz (room, stand) are left out. The velocities come from the captures' levels: 0.30, 0.524, 0.932 and 1.0.
- **The added modes.** Half the edge's energy between 356 and 713 Hz had no NOVA mode, among them its strongest partials (652–665 Hz): NOVA's modes come from bow and bell captures, which have almost nothing there. So 34 edge-only modes are added, between 215 and 708 Hz (2270 → 2304). With them 84–94 % of each capture's energy lands on a NOVA mode, up from 66–88 %.
  - **Their own values** (`nova_edge_v2_modes.json`): frequency, T60, drift, mode id (900000 on), stereo picks, LFO rate and phase.
  - **Nothing from BOW and BELL:** `exc_norm`, `bell_weight` and the bow peak times are 0, and the bow and bell velocity tables give them nothing.
  - **From the nearest NOVA mode** (named in the file): every other per-mode value (edge weight, the LF / MF / HF masks and shapes, the tilt and onset bands), their contact-force rows, and their **stereo picks**, so the edge sits in the cymbal's stereo field. `mode_step` comes from their own frequency and T60.
  - **At their frequency:** they are inserted where they fall, so the modes stay in frequency order, as the NPZ envelope bands must rise with the mode index. Every NOVA mode keeps every value, in its order.
- **The edge NPZ** (`nova_edge_v2_npz.json`, in the final mode order):
  - **velw:** per mode, the peak amplitude of the partials assigned to it, ^0.8, normalised per velocity; level law 1.3.
  - **The envelopes:** the 22 plugin bands, 20 ms frames from the strike, 749 frames (15.0 s); the window (`onset_s`) 15.5 s.
  - **Their cleaning:** gaps filled; a 140 ms median and a 60 ms Gaussian after 150 ms; continued at their own slope past the noise floor; then decaying 1 / 0.85 as fast after 160 ms, **the ring 15 % shorter** (Kris).
  - **The rest:** the band index, the RMS correction from the velw norms as `build_preset.py` does, and per-mode edge peak times (no audible effect: NPZ edge strikes follow the envelopes).
- **The settings** (`nova_edge_v2_settings.json`):
  - **The 20 ms frame-decay restart is off.** On NOVA's fast-decaying modes it clicked at every 20 ms frame ("The in-frame decay is a 50 Hz sawtooth"). Off, the level follows the band envelope alone.
  - **The velocity settings:** the `edge` articulation's velocity curve −2 dB at 0.6 and at 0.3; the EDGE level trim −2.11 dB (matched to the shipped EDGE at velocity 0.9, 1 s RMS from the strike, then +2 dB: Kris, "the hardest hits a dB or two louder"; not re-measured).
  - **Every slot's edge parameters and flags:** `edge_npz` and `edge_band_env` on, the window, the RMS correction, velw on and normalised.

### The build step

`tools/apply_nova_edge_v2.py` takes `resources/presets/nova_ride.bake.json` as it was on `main` and writes it with the edge v2 changes. Nothing else changes: the other slots' trims, skip thresholds and the tuning block stay; only EDGE's skip threshold was measured again (`cymbalis_trims --skip-thresholds`, stored with `set_skip_thresholds.py --only EDGE`: 1.131e-7, was 8.749e-8). The bake records the step (`edge_v2`: when, the data's MD5s, the approval), and the step refuses to run twice. NOVA can no longer be re-baked from `presets/` alone: the order is `bake_preset.py`, edge v2, the level trims keeping EDGE's (`set_level_trims.py --keep EDGE`), the skip thresholds (`docs/BUILDS.md`).

Its output equals the approved test bake (`assemble_reference.py` run on `main`'s bake) array for array: all 62 arrays. The differences are only the step's own records (`added_modes`, `edge_v2`, the NPZ block's `source`), the EDGE trim's record (the same −2.11 dB), and `render_phase_uniform.per_strike`, which stays NOVA's own 2270 (below).

### The loader and the engine: added modes (general)

- **The bake lists them.** `added_modes.indices` names the added modes, written by the build step, not inferred from `mode_id` (`BakeData::addedModes`, `numOwnModes`).
- **Seeded values.** The loader checks the preset's own modes, in their order, against the streams drawn for their count, exactly as before: `pick_l` / `pick_r`, `lfo_rate_hz` / `lfo_phase` ("PCG64/numpy mismatch" otherwise). It takes the added modes' values as baked. `render_phase_uniform.per_strike` must equal the own modes' count. A hi-hat bake may not have added modes.
- **The strike phases.** A strike draws its phases in this order: the own modes first, in their order, from the render stream as before, so each gets exactly the draw it always got; then the added modes from a stream of their own (`addRng_`: two outputs of a copy of the render stream, after the two HIT VARIATION takes, mixed with constants), so the render stream's sequence is unchanged by them. That keeps BOW, BELL, BELL2 and BOW2's phases what they were in any sequence of strikes, not only a lone one. A literal reading of the task (`per_strike` set to 2304, one stream in index order, as the approved renders drew) would shift every strike after the first by 34 draws. The NPZ edge strike's second set of phases draws the same way; the energy cascade draws an added mode's phase from `addRng_`. EDGE is therefore not bit for bit with the approved renders (the task allows it).
- **Everything else** that runs over the modes takes 2304 as any count: the build-up, the silence rule, the stereo gains and `tools/apply_tuning.py`'s stereo block (a stereo tuning file writes 2304 gains on NOVA; checked). **Presets without added modes** load and play bit for bit as before: every other preset is bit for bit as `main` (`tools/cymbalis_identity/run.sh`).
- **For the kit:** the loader's support is general; any instrument whose captured articulations sound modes its bank lacks can use it.

### NOVA is now a preset with NPZ edge data, and that reaches its other rows

**NOVA's BOW, BELL, BELL2 and BOW2 are not bit for bit as `main`.** The task asks for it; it cannot be had without an engine change:
- **The added modes alone change nothing.** With the bake's edge NPZ block taken out, the four rows are bit for bit as `main`: lone strikes at 0.3, 0.6, 0.9 and 1.0, BOW2 at the bow end of BLEND POSITION, and the four rows in turn for 11 s, at 44.1 kHz / 128 and 96 kHz / 64, HIT VARIATION off and on.
- **The NPZ block gives NOVA the NPZ presets' engine structure** ("Swish", above: an NPZ preset's engine structure follows its NPZ data). The shared oscillator has no decay, so every strike carries its whole decay in its own component, because a strike following a band envelope cannot ride a decaying oscillator. The control block also divides the 20 ms frame (126 samples at 44.1 kHz instead of 128, 120 at 48 kHz, 240 at 96 kHz).
- **The difference:** at most −51.7 dB re the peak on lone strikes (−54 to −58 dB in the sequences). Nearly all of it is the oscillator: with only the control block changed it is −89 to −100 dB. It is the same arithmetic split differently: the decay interpolated linearly across each control block now carries the whole decay of the fast-decaying high modes. Every other NPZ preset (STINE, HAMMERED THIN, SWISH, RAVEN, EXTRA PAPER THIN, KONSTAN, CRASH 16) has always played this way, and so did the approved test bake.
- **Kris decides:** accept it, or ask for an engine change so that a preset's structure no longer depends on having NPZ data (for instance, envelope strikes compensating the oscillator's decay and being re-referenced often enough): a separate task.

Also, as on the other NPZ presets, a strike towards the edge (edge fraction above 0.1: BOW and BOW2 with BLEND POSITION 2 past 20 %) now takes the new edge data.

### Checks (`tools/cymbalis_identity/run.sh --nova-edge-v2`, in memory)

The new EDGE, lone strikes at 44.1 kHz (mono mean, dB re full scale after the output gain, RMS of 0.05–0.55 s after the strike, HIT VARIATION as baked), and its stereo over the first 2 s:

| Velocity | 0.35 | 0.65 | 0.8 | 0.95 | 1.0 |
|---|---|---|---|---|---|
| Level | −41.4 | −38.5 | −31.0 | −30.1 | −27.5 |
| Approved | −40.9 | −37.6 | −30.1 | −29.4 | −27.6 |
| Difference | −0.5 | −0.9 | −0.9 | −0.7 | +0.1 |
| L/R balance | −0.11 dB | −0.18 dB | +0.13 dB | +0.08 dB | +0.24 dB |
| L–R correlation | +0.208 | +0.403 | +0.386 | +0.306 | +0.155 |
| 50 Hz share | 0.02 % | 0.04 % | 0.03 % | 0.04 % | 0.12 % |

- **Levels: 0.5 to 0.9 dB under the approved renders at 0.35 to 0.95** (the task expects about ±0.5 dB); the check allows 1.5 dB.
  - **The phases account for part of it.** With the approved renders' draw order (one stream, index order) the levels are −41.2, −38.1, −30.7, −30.1 and −28.4 dB: 0.3 to 0.8 dB under.
  - **Strike to strike, the level moves a lot.** Over 16 strikes, it moves 0.9 to 4.3 dB (at 0.65 from −40.0 to −38.5, at 1.0 from −31.8 to −27.5); the first strike is the loudest.
  - **What is left is not explained here.** At 0.65 and 0.8 the approved values lie above all 16 strikes.
- **Stereo.** The L/R balance is within 0.24 dB of centre at every velocity. EDGE's L–R correlation (+0.155 to +0.403) lies within NOVA's BOW, BELL and BOW2 at the same velocities (−0.238 to +0.404). At 1.0 it is below the approved renders' +0.21 to +0.43, because BOW's own correlation falls to −0.18 and −0.24 at 0.95 and 1.0.
- **No clicks.** The 50 Hz share is at most 0.12 % (the check allows 1.5 %). The measure is this check's own (the project session's numbers, approved 0.9 %, today's EDGE 0.7 %, the restart on 5.0 %, are on another scale): the 2–8 kHz band's power in 1 ms frames, over its own 100 ms moving average, minus 1, over 0.5–2.5 s; its spectrum (Hann window, 0.5 Hz bins); the power within 1 Hz of 50 Hz as a share of 1–450 Hz. On the same scale, at 0.8: `main`'s EDGE 0.12 %, the new EDGE with the frame-decay restart switched on 2.22 %.
- **Bench** (`cymbalis_bench`, this repo's Linux machine, 48 kHz / 256):
  - **All slots playing:** NOVA 14.1 % of a core (13.3 % on `main`), about 7× real time.
  - **Dense** (`--dense`, a strike every 24 ms): blocks median 736 µs (780), worst 1771 µs (1616) of 5333.
  - **The single-strike spike moves.** `strike()` worst 496 µs (333), mean 319 µs (294), because an EDGE strike now does the NPZ coupling (a second set of phases, the velw tables); blocks with a strike worst 1920 µs (1669), 36 % of the budget. The 34 modes themselves add about 1.5 % to every strike.

### Merge impact

- **Engine:** no change to the threading model. NOVA plays 34 more modes; its EDGE strikes do the NPZ coupling, as every NPZ preset's do.
- **Bake pipeline:** NOVA's bake is `bake_preset.py`, then edge v2, then the trims (keeping EDGE's) and skip thresholds. The kit's bake pipeline must keep that order.
- **Saved state, parameters, MIDI, UI, dhpool:** none. The edge values are existing tuning values; the Tuning window shows NOVA the edge envelope values because its bake has NPZ data.
- **Bench:** NOVA's figures move slightly with the added modes and the NPZ edge.
