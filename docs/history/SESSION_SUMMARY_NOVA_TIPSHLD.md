# Cymbalis — Nova Bow & Bell Tip/Shoulder Articulation Session
## Engine: multi_strike_engine_v19.py (V20) | Package: Cymbalis_V7_Nova_Package.zip

---

## OVERVIEW

This session added **stick tip vs stick shoulder** articulation differentiation for both **bow** and **bell** positions on the Nova preset (18" Heavy Dry Inverted China). The methodology is SPEAR-data-driven throughout — no perceptual guessing. The same approach is designed to generalize to any cymbal preset.

---

## ENGINE CHANGES (cymbal-agnostic)

### 1. `bow_onset_band_gains` Mechanism (V20)

**What it does:** Applies a time-decaying exponential gain multiplier per frequency band at the onset of any bow/bell strike. Gains start at their specified value at t=strike and decay to 1.0 (neutral) over `bow_onset_tau_ms` milliseconds. This shapes the attack spectral character without affecting the sustained ring.

**9-band structure** (matching existing engine band edges):
```
Band 0: 50–89 Hz
Band 1: 89–178 Hz
Band 2: 178–356 Hz
Band 3: 356–713 Hz
Band 4: 713–1425 Hz
Band 5: 1425–2851 Hz
Band 6: 2851–5702 Hz
Band 7: 5702–11404 Hz
Band 8: 11404–22050 Hz
```

**Articulation JSON parameters:**
```json
"bow_onset_band_gains": [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0],
"bow_onset_tau_ms": 80.0
```

- If `bow_onset_band_gains` is absent or all 1.0: mechanism is fully bypassed (zero cost).
- The gate `ef < 0.9` ensures gains are inactive at pure edge position.
- Position blending (`_bow_frac`) was **removed** from the original implementation — gains apply at full nominal value at any non-edge position.

**How to derive gains for a new cymbal:**
1. Analyse SPEAR captures for tip vs shoulder at bow position.
2. Compute normalized band energy at onset for each articulation.
3. Gains ≈ `tip_onset_band / shoulder_onset_band` (or normalized to a neutral reference).
4. Tune `tau_ms` to match the decay rate observed in the SPEAR (tip typically shorter, shoulder longer for body content).

### 2. `load_preset.py` — `data_files` Override in Articulations

Articulation JSON files can now redirect any data file loaded by the engine:

```json
{
  "data_files": {
    "vel_participation": "data/nova_vel_participation_tip.json",
    "bell_vel_participation": "data/nova_bell_vp_tip_corrected.json"
  },
  "parameters": { ... }
}
```

This enables per-articulation vel_participation without any engine changes. The override merges with the preset's base `data_files` before loading.

---

## NOVA BOW TIP/SHOULDER — METHODOLOGY

### SPEAR Analysis

- **V1–V3 captures** (softer to medium forte): spectral centroid at onset ≈ 12.2 kHz → **tip character** (brighter, less body)
- **V4–V5 captures** (forte to max): spectral centroid at onset ≈ 7.2 kHz → **shoulder character** (warmer, more body)
- V3 is the crossover velocity, shared by both characterizations.

### Vel Participation Split

**Tip VP** (`nova_vel_participation_tip.json`):
- `vel_curves`: `[V1, V2, V3, V3, V3]` — caps at V3, preventing V4/V5 body character
- `vel_norms`: remapped so V3 = velocity 1.0

**Shoulder VP** (`nova_vel_participation_shoulder.json`):
- `vel_curves`: `[V3, V3, V3, max(V3,V4), max(V3,V5)]` — floors at V3, adds V4/V5 body
- `vel_norms`: `[0.0, 0.0, 0.33, 0.67, 1.0]`

### Onset Gains (bow_tip.json)

Derived from normalized SPEAR onset band ratios (tip/shoulder):
```json
"bow_onset_band_gains": [1.20, 1.20, 1.03, 0.95, 0.91, 0.90, 0.90, 1.08, 1.20],
"bow_onset_tau_ms": 80.0
```
Shoulder uses neutral gains (all 1.0 → bypassed).

### Baseline MD5s (Bow)
| Articulation | MD5 |
|---|---|
| Bow Tip BL | `caaa1ce3cbd9b616845186b311db854c` |
| Bow Shoulder BL | `afe26c24a7085c018b765edfa53319ee` |

---

## NOVA BELL TIP/SHOULDER — METHODOLOGY

### New SPEAR Captures
Two new captures were made specifically for bell articulation differentiation:
- `Nova Bell - V1.txt` → from `Bell_-_Tip.wav` (stick tip on bell)
- `Nova Bell - V2.txt` → from `Bell_Shoulder.wav` (stick shoulder on bell)

**Key SPEAR findings:**
- **Tip onset**: 59% energy in 5702–11404 Hz, centroid 9213 Hz, gradual decay
- **Shoulder onset**: 35% in 11404+ Hz (fast transient), 26% in 2–4 kHz body, centroid falls 4× faster

### Bell VP Construction

**Tip VP** (`nova_bell_vp_tip_corrected.json`):
- Built by frequency-matching `Nova Bell - V1.txt` SPEAR partials to engine modes
- Bell_weight correction applied: each mode's `vel_curve` divided by its `bell_weight` (floor 0.02, cap 20×), then energy-normalized
- This pre-compensates for `bell_weight` suppression, so synthesis reproduces actual SPEAR amplitudes throughout the ring

**Shoulder VP** (`nova_bell_vp_shoulder_v2_corrected.json`):
- Built by frequency-matching `Nova Bell - V2.txt` SPEAR partials to engine modes
- Same bell_weight correction applied

**Why bell_weight correction is necessary:**  
At bell position, `pos_weight = bell_weight`. Body modes (below 2 kHz) have `bell_weight` of 0.07–0.11, suppressing them to near-zero without correction. SPEAR playback is clean because SPEAR uses raw measured amplitudes; the engine suppresses body modes unless the VP pre-compensates.

### Onset Gains (bell articulations)

Derived from SPEAR S/T onset ratios:

**Bell Tip** (tau=120ms):
```json
"bow_onset_band_gains": [1.0, 1.0, 1.0, 1.0, 1.0, 0.45, 0.75, 1.85, 0.85]
```

**Bell Shoulder** (tau=300ms):
```json
"bow_onset_band_gains": [1.0, 1.0, 1.0, 1.0, 1.0, 2.40, 1.35, 0.52, 1.20]
```

### Baseline MD5s (Bell)
| Articulation | MD5 |
|---|---|
| Bell Tip BL | `6662bc64df10daddd8d91effc80fee31` |
| Bell Shoulder BL | `d4f16f27c6a39e2e67515187d36ebfcd` |

---

## COMBINED POSITION ARTICULATIONS

`tip.json` and `shoulder.json` handle full bow→bell position sweep (position 0→1):
- Override both `vel_participation` (bow character) and `bell_vel_participation` (bell character)
- Onset gains apply at all non-edge positions
- `bow_frac` position blending ensures natural crossfade

---

## GENERALIZING TO OTHER CYMBALS

### Required SPEAR captures
- **Bow**: Minimum 3 velocity layers for tip (soft→forte tip), 2–3 for shoulder (medium→forte shoulder). V3 is the shared crossover.
- **Bell**: 1 tip capture + 1 shoulder capture. Quiet environment, clean isolated bell strikes.

### VP split recipe
For any cymbal with V1–VN bow captures:
1. Identify the velocity crossover (e.g., V3) separating tip and shoulder character
2. **Tip VP**: `vel_curves = [V1, V2, V3, V3, V3]` capped at crossover
3. **Shoulder VP**: `vel_curves = [V3, V3, V3, max(V3,V4), max(V3,V5)]` floored at crossover
4. Run the local amplitude ceiling rebuild if anomalous short-T60 modes emerge

### Bell_weight correction recipe
1. Load engine to get `_bell_weight` per mode
2. For each mode: `corrected_vel_curve = vel_curve / max(bell_weight, BW_FLOOR)`
3. Cap at `MAX_BOOST` (20×), then energy-normalize so overall bell volume is preserved
4. Use `BW_FLOOR = 0.02`, `MAX_BOOST = 20` as starting parameters

### Onset gain derivation recipe
1. Parse SPEAR captures for tip and shoulder
2. Compute per-band energy at onset window (first 60ms)
3. Normalize each articulation's band energy to sum = 1
4. **Tip gains** ≈ `tip_norm / shoulder_norm` per band (clipped 0.4–3.0)
5. **Shoulder gains** ≈ `shoulder_norm / tip_norm` per band
6. `tau_ms`: match the centroid fall rate from SPEAR (tip faster = shorter tau)

---

## KNOWN ISSUES — OPEN FOR NEXT SESSION

### Bow: Anomalous Ringing in ~3800–4200 Hz Range

**Symptom:** A ringing tone with wobble (~2Hz amplitude modulation) that appears at onset and decays faster than the cymbal body. Present in both bow tip and shoulder, less prominent in the original V1–V5 mixed VP.

**Root cause (diagnosed):** The V3-cap VP construction gives equal amplitude (1.0) to both members of closely-spaced mode doublets in the ~4000 Hz region, maximizing beat modulation. In the original V1–V5 VP, V4/V5 naturally gave unequal amplitudes to doublet pair members, damping the beat.

**Confirmed contributors:**
- Mode 1149 (3986.9 Hz, T60=50ms) — in both tip and shoulder VP
- Mode 1154 (4003.7 Hz, T60=60ms) — tip VP, minimal in shoulder
- Additional short-T60 modes in the 3900–4200 Hz cluster (see mode list in next session)

**Progress:** Zeroing modes 1149+1154 together reduced beating noticeably but did not eliminate the ring. Remaining short-T60 modes in 3900–4200 Hz are the next target.

**Fix direction:** Zero all modes with T60 < 400ms in the 3800–4200 Hz range from both tip and shoulder VPs. Start with the shortest T60 (50–200ms) cluster, verify ring removal, then selectively restore long-T60 modes to preserve character.

**Note:** The original engine also has this ring at lower prominence. The VP split exposed it by equalizing doublet pair amplitudes. The fix should be applied to both the tip and shoulder VPs.

---

## FILE MANIFEST — NOVA CHANGES THIS SESSION

### Engine (modified)
| File | Change |
|---|---|
| `engine/multi_strike_engine_v19.py` | V20: added `bow_onset_band_gains` init + free-ring application; removed `_bow_frac` position blending |
| `load_preset.py` | Added `data_files` override support in articulation JSON loading |

### Nova Articulations (new)
| File | Description |
|---|---|
| `articulations/bow_tip.json` | Bow tip: tip VP + onset gains [1.20,1.20,1.03,0.95,0.91,0.90,0.90,1.08,1.20], tau=80ms |
| `articulations/bow_shoulder.json` | Bow shoulder: shoulder VP + neutral gains, tau=80ms |
| `articulations/bell_tip.json` | Bell tip: corrected tip bell VP + onset gains, tau=120ms |
| `articulations/bell_shoulder.json` | Bell shoulder: corrected shoulder bell VP + onset gains, tau=300ms |
| `articulations/tip.json` | Combined position sweep — tip character bow→bell |
| `articulations/shoulder.json` | Combined position sweep — shoulder character bow→bell |

### Nova Data (new)
| File | Description |
|---|---|
| `data/nova_vel_participation_tip.json` | Bow tip VP: V1-V3 cap, remapped vel_norms |
| `data/nova_vel_participation_shoulder.json` | Bow shoulder VP: V3-V5 floor |
| `data/nova_bell_vel_participation_tip_v2.json` | Bell tip VP: freq-matched from Nova Bell V1.txt SPEAR |
| `data/nova_bell_vp_tip_corrected.json` | Bell tip VP with bell_weight correction applied |
| `data/nova_bell_vp_shoulder_v2.json` | Bell shoulder VP: freq-matched from Nova Bell V2.txt SPEAR |
| `data/nova_bell_vp_shoulder_v2_corrected.json` | Bell shoulder VP with bell_weight correction applied |

### Nova SPEAR (new)
| File | Source |
|---|---|
| `spear/Nova Bell - V1.txt` | From Bell_-_Tip.wav — stick tip on bell |
| `spear/Nova Bell - V2.txt` | From Bell_Shoulder.wav — stick shoulder on bell |
| `spear/Nova Bow - V1.txt` through `V5.txt` | Original bow captures V1–V5 |
