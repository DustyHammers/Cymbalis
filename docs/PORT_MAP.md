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
| 8 | LF accumulator → wander-rate boost | 346–355, 652–672 | `lf_sens_depth`, `lf_sens_tau`, `lf_edge_weight`, `lf_bow_weight`, `lf_bell_weight`, `lf_max_scale`, `lf_min_scale`, `lf_wander_rate_boost` | From the first strike | `lf_wander_rate_boost: 0.0`. `lf_sens_depth: 0.0` also works and skips the whole block. |
| 9 | (LF amplitude scaling) | 391–397, 914–916 | `lf_cutoff_hz`, `lf_sens_depth`, `lf_max_scale` | **Inert.** `lf_max_scale = 1.4` selects boost mode, but the scale is applied only when it is below 1. | Already off. |
| 10 | MF accumulator suppression | 357–366, 399–405, 674–688, 917–919 | `mf_sens_depth`, `mf_sens_tau`, `mf_min_scale`, `mf_max_scale`, `mf_*_weight`; `mf_lo_hz`, `mf_hi_hz` (defaults 500 and 4000) | From the 2nd strike (the scale is 1.0 on the first) | `mf_sens_depth: 0.0` |
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
| `bow_shoulder` | `vel_participation` → `nova_vel_participation_shoulder_correct.json` | `bow_onset_band_gains` all 1.0, so it is bypassed (line 433). `bow_onset_tau_ms = 80`, which equals the default and is unused. |
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

**f.4 Discontinuity between the contact phase and the free phase.** During the 13 contact samples the output is `Re(state)·pick`, with no wander, ramp, onset gain, MF scale or `wash_damp` (line 733). At sample 13 all of them switch on.
- *Measured:* for an edge strike at v 1.0, the ramp-on/ramp-off ratio is 1.00 for samples 0–10 and 0.40 from sample 15 on. That is an 8 dB step about 0.3 ms after the onset.
- Every older ringing mode loses its wander for those 13 samples. That gives a phase step at both ends of every contact.

**f.5 Wander uses absolute time, and its rate changes at every strike.** `wander(t)` is evaluated at absolute render time (line 867).
- `lf_wander_rate_boost` changes `lr` at every strike (line 670), so every ringing mode's phase jumps.
- *Measured:* the same strike at 0.2 s and at 1.2 s differs by −27.8 dB with wander on. It is bit-identical with wander off.
- A plugin needs a definition of "absolute time".

**f.6 Sample rounding leaves holes.** The write position `int(t_start·SR)` and the length `int(dur·SR)` are floored separately (a.5).
- *Measured:* on a 10 ms grid, 605 of 999 consecutive boundaries leave a one-sample hole. The hole is an exact 0.0 sample: the left channel at index 1322 for strikes at 0.02 s and 0.03 s.
- Over 550 165 arbitrary 1 ms-grid pairs, 47.8 % leave a hole and 64 overlap (that sample is summed twice).
- A strike at `t = n/SR` lands on sample `n − 1` for 29 of 401 sampled `n`.
- The locked renders use a 0.5 s grid and never hit this.

**f.7 Closely spaced strikes.**
- A gap of 1e-4 s (4 samples) or less is not rendered. The earlier strike's pending contact is overwritten, so that strike is lost entirely, and the state is not advanced over the gap. *Measured:* a v 0.9 strike followed 50 µs later by a zero-velocity strike outputs exactly 0.0.
- Strikes less than 13 samples apart cut the force short. *Measured:* 8 samples apart keeps 60 % of the energy.

**f.8 Strike at t = 0.0.** `dt = ... if lf_t_last > 0 else 0.0` (lines 654 and 676) treats `t_last = 0.0` as "no previous strike". The strike after a strike at exactly 0.0 therefore gets no accumulator decay. *Measured:* two edge v 0.9 strikes 0.5 s apart give `lf_S = mf_S = 1.8` when the first is at 0.0, and 1.545 when the pair is shifted by 1 ms.

**f.9 The LF accumulator is only half inert.** The amplitude scaling never applies (b, row 9), as `SESSION_SUMMARY_V8` already says. The accumulator still changes the wander rate through `lf_wander_rate_boost = 0.2`, which that summary does not mention.

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
2. **The engine clock and the strike-phase random sequence start when the plugin loads.** The clock is the time base for wander (f.5). Neither the clock nor the sequence restarts on playback. Python's `render()` creates a fresh RNG and starts at t = 0 on every call. A comparison therefore treats each Python render as one plugin load: a freshly loaded engine renders the same sequence from sample 0.
3. **Every strike plays, at its exact sample.** Python's dropped close strikes and one-sample holes (f.6, f.7) are not reproduced. Comparison sequences use only strike times where Python has neither:
   - every gap is longer than 1e-4 s;
   - `int(a·SR) + int((b − a)·SR) == int(b·SR)` at every segment boundary;
   - every strike time is a whole number of samples.
4. **One shared cymbal.** All five slots ring the same modal state. Each slot's overlay data is applied to that slot's own strikes. The overlay data is the velocity tables and the onset gains, plus the slot's calibration level. For a sequence that uses only one slot, this matches Python, which gives each overlay its own engine. Python has no reference for sequences that mix slots.
5. **Slots:**

| Slot | Overlay | Position |
|---|---|---|
| BELL | `bell_shoulder` | 1.0 |
| BOW | `bow_tip` | 0.5 |
| EDGE | no overlay | 0.0 |
| BELL2 | `bell_tip` | 1.0 |
| BOW2 | `bow_shoulder` | 0.5 |

### Implementation notes for these decisions

- **Calibration is applied when the strike is injected.** Python multiplies the whole output by one scalar at the end. With NL off, every remaining mechanism is linear in amplitude, so the two give the same result to within rounding.
- **The skip threshold is scaled to match.** Python skips a mode for a segment when `|z| < 1e-9` in its uncalibrated units (line 859). The C++ state holds calibrated amplitudes, so it uses `1e-9 × calibration` of the slot that struck most recently. For a single-slot sequence this is the same test as Python's.
- **The comparison pipeline** is `tools/bake_preset.py`, `tools/render_reference.py`, `cymbalis_render`, `tools/compare_ab.py` and `tools/run_ab.py`. The sequences are in `tests/sequences/`.
