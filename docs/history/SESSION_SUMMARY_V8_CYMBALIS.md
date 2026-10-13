# Cymbalis V8 — Session Summary
**Engine:** `multi_strike_engine_v19.py` (unchanged from V7)
**Session focus:** Nova bow articulation fixes — ring artifact, velocity curve normalization, shoulder VP reconstruction

---

## Baselines Entering This Session

| Item | MD5 |
|---|---|
| Engine | `bcd9e8d8af079b415a53d9968952a1bc` |
| Nova_Baseline19.json | `e2025486455f50371ea0fc097482a61f` |
| Bow Tip BL (TIPSHLD render) | `caaa1ce3cbd9b616845186b311db854c` |
| Bow Shoulder BL (TIPSHLD render) | `afe26c24a7085c018b765edfa53319ee` |

---

## Work Completed

### 1 — Stage 2A Ring Fix (Bow Tip + Shoulder)

**Problem:** Anomalous ~4000 Hz ringing with ~2 Hz beating amplitude modulation present in bow renders from attack onset. Identified in TIPSHLD session as doublet pair beating (confirmed contributors: mode 491 at 3986 Hz T60=50ms, mode 1410 at 4003 Hz T60=60ms). Additional short-T60 modes in the 3800–4200 Hz cluster were also implicated.

**Fix:** Zeroed `vel_curve` for all modes with T60 < 200ms in the 3800–4200 Hz range in both tip and shoulder VP files. Applied to bow VP only; `vc[1]` (medium velocity) left at original SPEAR value to avoid activating V-shape modes at medium velocity.

**Result:** Ring artifact reduced. Stage 2A VP files:
- `nova_vel_participation_tip_stage2a.json`
- `nova_vel_participation_shoulder_stage2a.json`

Baseline locked: `NOVA_BOW_S2A_BASELINE.json`

---

### 2 — Vel-Curve Ratio Ceiling (Bow Tip)

**Problem:** Successive bow hits showed spectral accumulation — certain frequency bands (400–500 Hz, 2000 Hz, 6300 Hz) growing disproportionately faster than the overall level increase from soft to forte. Traced to vel_curve nonlinearity: modes with vc[0]/vc[-1] ratios of 10–18× caused extreme spectral shifts between soft and forte.

**Investigation:**
- LF accumulator: **non-functional** in Nova (`lf_max_scale=1.4 > 1.0` puts it in boost mode; application condition `lf_scale_now < 1.0` never met).
- MF accumulator: **negligible** at 5-second strike spacing (tau=1.5s, accumulator decays 96% between hits).
- Root cause confirmed: vel_curve nonlinearity in specific modes, not ring carryover or accumulator mechanisms.

**Fix:** Applied 4.0× ratio ceiling to `vc[0]` in tip VP. Modes where `max(vel_curve)/vc[0] > 4.0` had `vc[0]` raised to `max(vel_curve)/4.0`. `vc[1]` deliberately left at original SPEAR value (raising it caused V-shape modes — those with `vc[1]=0` in SPEAR — to activate at medium velocity with long T60 ring, producing HF persistence at medium velocity).

**Result:** Most extreme accumulation eliminated (63 Hz, 80 Hz, 1000 Hz, 6300 Hz, 16000 Hz cleaned up). Residual 400–500 Hz accumulation persists (modes with ratios of 3–4× slip under the 4.0× ceiling) and 2000 Hz mild accumulation remains. Accepted as current baseline.

Baseline locked: `NOVA_BOW_CEIL_BASELINE.json`

**Active bow tip baseline:** `nova_vel_participation_tip_s2a_ceil.json`

---

### 3 — Bow Shoulder VP Reconstruction

**Problem identified:** The V7 shoulder VP (`nova_vel_participation_shoulder.json`) was built as `[V3, V3, V3, max(V3,V4), max(V3,V5)]` — V3 (the tip/shoulder crossover capture) was incorrectly included as a floor. This caused:

1. **534 Group 2 modes** (33% of active modes) with `vc[2]=0` (absent from V3) but `vc[3]>0` (present in V4). At medium velocity, these 534 modes jumped from completely silent to ~94% of V4 participation simultaneously — creating a sharp spectral step that read as pitch rise and wrong medium-velocity character.

2. **V3 LF contamination:** V3 mean participation in 0–500 Hz = 0.56 vs V4 mean = 0.23. The `max(V3,V4)` operation caused LF body modes to use V3 values (higher), giving the shoulder VP more body than the actual shoulder captures justified.

3. **vel_norms[0]=0.0 bug:** The build script set `vel_norms[0]=0.0` with the comment "scaled down below threshold." This is a bug — with `vel_norm[0]=0.0`, the engine's extrapolation branch (`vel <= norms[0]`) never triggers for any `vel > 0`. All MIDI velocities produced flat V4 participation with no dynamic scaling, causing all three hits (soft/medium/forte) to render at identical amplitude.

**Source:** `build_nova_vp.py` (uploaded this session) confirmed the correct construction:
```
vel_curves = [V4, V4, V5, V5, V5]   # V4 from col 3, V5 from col 4 of mixed VP
vel_norms  = [0.0, v4_vel/v5_vel, 1.0, 1.0, 1.0]
```
The `vel_norms[0]=0.0` in the script is the bug. Correct value is `v4_vel/v5_vel = 0.9905`, which enables the extrapolation branch: `vel = V4 × (MIDI_velocity / 0.9905)` for any MIDI velocity below the V4 capture level.

**Fix applied:**
- Rebuilt shoulder VP using V4 (col 3) and V5 (col 4) from original mixed VP (`nova_vel_participation.json`) — no SPEAR parsing, no V3.
- Set `vel_norms = [0.9905, 0.9905, 1.0, 1.0, 1.0]`
- Applied Stage 2A ring fix (6 modes zeroed in 3800–4200 Hz)

**Result:** Pitch rise eliminated. Medium velocity character correct. Velocity dynamic range restored.

**Active bow shoulder baseline:** `nova_vel_participation_shoulder_correct.json`

**CEIL shoulder reference** (V3-floored version with ceiling, has more LF body): preserved at `reference/nova_bow_shoulder_CEIL_BL/`.

Baseline locked: `NOVA_BOW_BL_SPLIT.json`

---

### 4 — NL Coupling Analysis

Confirmed NL coupling is active in Nova (`nl_amount=1.0`) but operating below audible threshold at current k values:
- `nl_k_lf_mf = 0.02`, `nl_k_mf_hf = 0.04`, `nl_k_lf_hf = 0.01`
- Transfer per 20ms segment ≈ 0.02–0.08% of band energy
- Over 1 second: 1–4% total LF→MF/HF transfer

Not contributing to any audible artifacts. Mechanism is correctly implemented and ready for shimmer development (target k values in discussion for future session).

LF accumulator boost mode confirmed non-functional (`lf_max_scale=1.4`, condition `lf_scale_now < 1.0` never met). Flagged for future fix if LF boost is desired.

---

## Final Baseline State (V8)

| Articulation | VP File | MD5 |
|---|---|---|
| Bow Tip | `nova_vel_participation_tip_s2a_ceil.json` | `1d143acc1baf05397f436b5472f28d8e` |
| Bow Shoulder | `nova_vel_participation_shoulder_correct.json` | `b35e81435c5fa52165c8c7758326a988` |
| Bell Tip | `nova_bell_vp_tip_corrected.json` | (unchanged from V7) |
| Bell Shoulder | `nova_bell_vp_shoulder_v2_corrected.json` | (unchanged from V7) |

| Render | MD5 |
|---|---|
| Bow Tip CEIL render | `91ae9a6856c0f58ab4244b90cb078f73` |
| Bow Shoulder correct render | `b321ddb1f50137caa7a40b1a853ada3f` |

Engine MD5 (unchanged): `bcd9e8d8af079b415a53d9968952a1bc`

---

## Open Items — Nova

### High Priority
- **Bow tip medium velocity** — iso_medium sounds incorrect (too much HF character relative to soft/forte). Traced to vel_curve interpolation behaviour at vel=0.65 in the V1–V3 tip VP structure. `vel_norms=[0.381, 0.759, 1.0, 1.0, 1.0]` means vel=0.65 interpolates 71.2% from vc[1] (V2 capture at vel=0.759). The V2 capture character at that velocity is driving the issue. Requires: either a new intermediate SPEAR capture at ~0.55 velocity, or re-examination of V2 capture data.
- **Edge SPEAR captures** — no real edge captures exist for Nova yet. Edge articulation uses neutral excitation ratios. Priority for next recording session.
- **Bow shoulder body** — correct V4/V5 VP has less LF body than the CEIL version (V3 LF participation ~0.56 vs V4 ~0.23 in 0–500 Hz). Physical question: do V4/V5 shoulder captures genuinely have less LF body at high velocity, or does the shoulder need a soft capture (below V4) to represent gentler shoulder strikes? Consider a V3.5 capture at ~vel=0.6 if recordings are redone.

### Medium Priority
- **LF accumulator bug** — `lf_max_scale=1.4` puts it in boost mode, but the engine only applies LF when `lf_scale_now < 1.0`, which never happens in boost mode. Engine fix required to enable LF boost.
- **NL coupling shimmer development** — current k values (0.01–0.04) produce negligible shimmer. Target k values TBD from perceptual testing. Bell NL coupling triads (using bell identity partials as sources) not yet implemented.
- **Bell articulation refinement** — flagged as weakest articulation across multiple sessions. Further work deferred.
- **vel_gain_power** — not explicitly set in Nova preset (defaults to 1.0). Dynamic range sweep not conducted. Flagged in V20 session.
- **MF suppress click** — sharp dry click at hit 7–8 in rapid forte sequences. `mf_min_scale`/`mf_sens_tau` adjustment deferred.

### Low Priority / On Horizon
- Edge LFO wander parameters (all zero, ready to activate once real edge captures exist)
- Choke late-sustain spectral mismatch

---

## Files Added to Package

| File | Location | Purpose |
|---|---|---|
| `nova_vel_participation_tip_s2a_ceil.json` | `presets/nova/data/` | Bow tip baseline (S2A + 4.0× ceiling) |
| `nova_vel_participation_shoulder_correct.json` | `presets/nova/data/` | Bow shoulder baseline (V4/V5 only, fixed vel_norms) |
| `build_nova_vp.py` | root | VP builder script — must be included in all future packages |
| `NOVA_BOW_BL_SPLIT.json` | root | Baseline lock: tip CEIL + shoulder correct |
| `nova_vel_participation_shoulder_CEIL.json` | `reference/nova_bow_shoulder_CEIL_BL/` | CEIL shoulder reference (V3-floored, not default) |
| `Nova_bow_shoulder_CEIL_BL_render.wav` | `reference/nova_bow_shoulder_CEIL_BL/` | CEIL shoulder reference render |

## Files Removed from V7

All intermediate session VP files (stage2a, ceil_v2, ceil2.5, v4v5 attempt) and superseded originals (`nova_vel_participation_tip.json`, `nova_vel_participation_shoulder.json`) excluded from V8 Nova preset folder. Source originals are preserved in `nova_vel_participation.json` (never modify).
