# Tuning by ear: the Cymbalis Tuning build

Kris tunes each cymbal by ear in a hidden Tuning window, saves the settings as a tuning file (JSON), and brings the file to the project session. There, `tools/apply_tuning.py` writes it into the preset's bake in `resources/presets/`, and the normal plugin plays the tuned values. This document is the approved spec, 2026-10-16. The tuning adds no host parameters; the four plugin parameters (`docs/UI_SPEC.md` section 9) stay as they are.

## The two builds

| | Cymbalis | Cymbalis Tuning |
|---|---|---|
| CMake | default (`CYMBALIS_TUNING` OFF) | `-DCYMBALIS_TUNING=ON` |
| Product name | Cymbalis | Cymbalis Tuning |
| Plugin code | `Cymb` | `CymT` |
| Bundle ID | `com.dhammers.cymbalis` | `com.dhammers.cymbalistuning` |
| Tuning window, live values, tuning files, tuning in the session | no | yes |

The two install side by side. The normal build has no tuning window, no live tuning values, no tuning files and no tuning in its session state. Like the tuning build, it reads a bake's `tuning` block when it loads the bake (below), so it plays what was tuned. CI builds both (`docs/BUILDS.md`).

## Opening the window

In Cymbalis Tuning, click **TUNING**, the box in the editor's top right corner (above the STICK ARTICULATION and MIDI NOTE headers). It opens the Tuning window; a second click closes it. While the window is open the box has the light outline of an open box. **Cmd-click on the CYMBALIS logo** (Ctrl-click on Linux) opens it too. The rest of the editor is unchanged, and the normal build has neither the button nor the window.

The Tuning window is attached to the editor's window. On macOS it is a child window of it:

- it is always in front of the editor and of the host's windows, never behind them;
- it opens on the editor's Space and moves with the editor;
- it hides while the host is in the background, so it does not cover other applications.

On other systems it is kept on top.

It opens below the TUNING box, right-aligned with it, so the box stays clickable; where the screen has no room below, it opens to the left of the box. If you move it, it stays where you put it, unless that would cover the box when it next opens. Its close button hides it, like the second click on TUNING. It closes when the editor closes.

The window uses plain JUCE controls in the editor's palette and font:

- Each value is a slider with a numeric box. Type a number into the box to enter it exactly.
- The arrow button at the right of a row resets that value to the baked one.
- A changed value has a bronze bar at the left of its row.
- **MODIFIED** next to the preset name means some value differs from the bake.
- **RESET ALL** returns every value of the preset to the bake.

Everything acts on the current preset; choosing another preset in the CYMBAL box shows its values. Each preset keeps its own tuning while you switch.

## The values

Changes apply live:
- **Per-strike values** take effect from the next strike.
- **Build-up (MF/LF) values** move the cymbal's scales from the next control point, through their 20 ms smoothing.
- **Wander depth and rate** change at the next control point, without a phase jump.
- **Output gain** is smoothed (20 ms).
- **Ring length is the exception:** it rebuilds the engine through the preset-switch path. The cymbal stops (30 ms fade) and restarts, and the change applies when the slider is released.

The values travel to the audio thread without locks or allocation.

### Per articulation

One set per bake entry: NOVA RIDE `bell_tip`, `bell_shoulder`, `bow_tip`, `bow_shoulder`, `edge`; SMOKE FLAT RIDE `bow`, `edge`, `center`; HAMMERED THIN RIDE `bell`, `bow`, `edge`. The articulation id is the entry's `articulation` key if it has one, else its overlay, else its name in lower case. Two entries of a bake may not share an id.

**center** (SMOKE FLAT RIDE) is a full articulation with its own values: level, velocity curve and band gains. Its bake entry, `CENTER`, is a copy of the 602's `BOW` data (`"articulation": "center"`, `"copy_of": "BOW"`, made by `tools/bake_preset.py`). Its level trim starts at BOW's (−1.76 dB), so untuned it sounds like the bow. The CENTER and CENTER2 rows play it (`docs/UI_SPEC.md` section 8). Changing `center` leaves `bow` alone, and the reverse.

| Value | Key | Range | Per strike | Effect |
|---|---|---|---|---|
| Level at velocity 1.0 | `level_db` | −40 to +40 dB | yes | The slot's level trim, initialised from the bake (`slots[].level_trim.gain_db`) |
| Offset at velocity 0.6 | `vel_0.6_db` | −24 to +24 dB | yes | Velocity curve, see below. Default 0 |
| Offset at velocity 0.3 | `vel_0.3_db` | −24 to +24 dB | yes | Velocity curve. Default 0 |
| Excitation band gains | `band_gains_db` | −24 to +12 dB per band | yes | 22 values, one per one-third-octave band, see below. Default 0 |

The **velocity curve** is a level offset on top of the current level law. It is interpolated in log velocity through three points: 0 dB at velocity 1.0, the 0.6 offset at 0.6, and the 0.3 offset at 0.3. Below 0.3 it continues with the 0.6-to-0.3 slope (clamped to ±60 dB). It changes the level only. The spectral velocity behaviour stays as it is: velocity tables, the Hertz tilt and the NPZ captures. The velocity is the engine's: the MIDI velocity after the Velocity Sensitivity knob.

#### Excitation band gains

Each articulation has a gain per one-third-octave band. It scales the energy that articulation's strikes put into the modes of that band. It is not an output EQ:

- **What it scales:** the strike's coupling into each mode, whatever path gives it: the velocity tables, the Hertz tilt, the NPZ velw captures and, through them, the edge band envelopes.
- **What it leaves alone:** the decay, the onset, the shared cymbal's state (energy already ringing, the build-up, the wander) and the other articulations. A change applies from the next strike.

The bands are the 22 one-third-octave bands of the onset data. Band j runs from 11404 × 2^((j − 21)/3) Hz to 11404 × 2^((j − 20)/3) Hz: 89.1–112.3 Hz, 112.3–141.4 Hz, …, 9051–11404 Hz, 11404–14368 Hz. A mode belongs to the band its frequency falls in; modes below 89.1 Hz go to the first band, modes above 14368 Hz to the last.

In the window, the **EXCITATION BANDS** section below the articulations works like a graphic EQ:

- The **Articulation** box picks the articulation whose bands are shown.
- There is one vertical slider per band. Its label is the band's centre frequency; hold the pointer over the label to see the band's edges.
- Type a number into the box under a slider to enter it exactly.
- The arrow under each band resets it to the baked value.
- **FLAT** sets every band of the shown articulation to 0 dB.
- A changed band has a bronze bar at its left, like a changed row.

### Per cymbal

| Group | Value | Key | Range | Presets | Notes |
|---|---|---|---|---|---|
| Cymbal | Output gain | `output_gain_db` | −24 to +12 dB | all | Whole preset, for headroom; before the Volume knob |
| | Edge capture curve | `edge_velocity_remap_exponent` | 0.25–2.0 | 602, Bettis | Exponent of the NPZ velocity remap (0.5 = sqrt). Each capture still maps to itself; this shapes how the velocities between captures are reached |
| | Bow brightness vs velocity | `tau_ref_ms` | 0.05–8 ms | 602, Bettis (no velocity tables) | `hertz_vel_scale` contact time at velocity 1 |
| | Ring length | `ring_length` | 0.5–2.0 × | all | Multiplier of the baked T60 of every mode (t60_scale). Rebuilds the engine |
| Wander | Wander depth | `wander_depth` | 0–4 | all | |
| | Wander rate | `wander_rate_hz` | 0.05–10 Hz | all | Each mode's LFO keeps its baked ratio (0.7–1.3) to this rate |
| | Edge wander depth | `edge_wander_depth` | 0–4 | all | Depth scale added by edge strikes (× velocity × edge fraction), per strike |
| | Edge wander rate | `edge_wander_rate` | 0–4 | all | Rate scale added by edge strikes, per strike |
| MF build-up | Depth | `mf_sens_depth` | 0–2 | all | 0 switches it off |
| | Tau | `mf_sens_tau` | 0.05–20 s | all | Release of the accumulator |
| | Min scale | `mf_min_scale` | 0–1 | all | Floor in suppress mode |
| | Max scale | `mf_max_scale` | 0.1–3 | all | Above 1 = boost mode up to this, else suppress mode |
| | Bow, edge, bell weights | `mf_bow_weight`, `mf_edge_weight`, `mf_bell_weight` | 0–2 | all | How much a strike at each position adds |
| | Low edge, high edge | `mf_lo_hz`, `mf_hi_hz` | 50–8000 Hz, 200–16000 Hz | all | The band-pass of the MF scale |
| LF build-up | Depth, tau, min scale, max scale | `lf_sens_depth`, `lf_sens_tau`, `lf_min_scale`, `lf_max_scale` | as MF | all | As MF; the LF scale applies in both modes (`docs/PORT_MAP.md`, live-engine decision 6) |
| | Cutoff | `lf_cutoff_hz` | 100–8000 Hz | all | Modes below it get the LF scale (sigmoid, 200 Hz wide) |
| | Bow, edge, bell weights | `lf_bow_weight`, `lf_edge_weight`, `lf_bell_weight` | 0–2 | all | |
| | Wander-rate boost | `lf_wander_rate_boost` | 0–2 | all | The LF accumulator speeds up the wander |
| Edge envelope | Window | `edge_band_env_onset_s` | 0.1–30 s | 602, Bettis | How long an edge strike follows the NPZ band envelope; per strike |
| | 20 ms frame-decay restart | `edge_frame_decay_restart` | on/off | 602, Bettis | On (the bake): the in-frame decay restarts every 20 ms (`docs/PORT_MAP.md`, "The in-frame decay is a 50 Hz sawtooth"). Off: no in-frame decay; the level follows the band envelope alone. Per strike |

A slider's range widens to include a baked or loaded value outside it. Values for other presets are hidden.

### Meters

- **Output, after the Volume knob:** the peak (held for 1.5 s) and the RMS over about 300 ms, in dBFS. **CLIP** lights when a sample reaches 0 dBFS; click it to reset it.
- **Last hit, before the Volume knob:**
  - the row and articulation it played, and its velocity;
  - its peak, and the RMS of the first 500 ms after the strike, both channels.

  The 500 ms include whatever else is ringing. "(measuring)" shows until the 500 ms are over.

## Tuning files

**SAVE...** writes the current preset's tuning to a file (default name `<preset>.tuning.json`). **LOAD...** reads one. A file for another preset is refused with a message. So is a file for another bake format, an unknown value, band gains that are not 22 numbers from −24 to +12 dB, or a ring length, edge capture curve, wander rate or `tau_ref_ms` that is not above 0. The tuning build also saves each preset's tuning (if it differs from the bake) in its session state, and restores it with the session.

```json
{
  "format": "cymbalis-tuning",
  "version": 1,
  "preset": "nova_ride",
  "bake_format_version": 1,
  "date": "2026-10-16T14:02:11.000+02:00",
  "producer": "Cymbalis Tuning",
  "cymbal": { "mf_sens_depth": 0.3, "ring_length": 1.2 },
  "articulations": {
    "bow_tip": { "entry": "BOW", "level_db": -1.0, "vel_0.6_db": -2.0 },
    "edge": { "entry": "EDGE", "band_gains_db": [0, 0, 0, -3.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2.0, 2.0, 0, 0] }
  },
  "effective": {
    "cymbal": { "output_gain_db": 0.0, "...": "every value of the preset" },
    "articulations": { "bow_tip": { "entry": "BOW", "level_db": -1.0, "vel_0.6_db": -2.0, "vel_0.3_db": 0.0, "band_gains_db": ["22 values"] }, "...": "" }
  }
}
```

- `cymbal` and `articulations` hold only the values that differ from the bake; loading a file applies these on top of the bake. An articulation's `band_gains_db` is written as all 22 bands, lowest first, when any of them differs.
- `effective` is a full copy of every value for reference; nothing reads it.
- `entry` names the bake entry (slot) of an articulation.
- `preset` is the preset id (`nova_ride`, `smoke_flat_ride`, `hammered_thin_ride`); `bake_format_version` is the bake's `format_version`.

## The round trip

1. Tune in Cymbalis Tuning, **SAVE...** the file, and bring it to the project session.
2. When Kris asks (running it is a separate task), apply it:

   ```
   python tools/apply_tuning.py nova_ride.tuning.json
   ```

   - The tool first validates the file: its format and version, the preset id against `resources/presets/<preset>.bake.json`, the bake format, and every articulation and value it names. `--bake PATH` picks another bake (its file name must still be `<preset>.bake.json`); `--dry-run` prints the result without writing.
   - It applies the file's changes on top of the bake's current values: the bake's own values and any `tuning` block it already has.
   - Each articulation's **level** goes into `slots[].level_trim.gain_db` (with a `set_by` note).
   - Each articulation's **band gains** go into its excitation data, `slots[].excitation_band_gains_db`: `{"band_edges_hz": [23 edges], "gains_db": [22 gains], "set_by"}`. An articulation whose bands are all 0 dB has no entry; applying FLAT removes it.
   - **Every other value that differs from the bake's own** (the cymbal values and the velocity curves) goes into the bake's top-level `tuning` block: `{"format": "cymbalis-tuning", "version": 1, "applied_utc", "source", "cymbal": {...}, "articulations": {id: {"vel_0.6_db", "vel_0.3_db"}}}`.
   - A value equal to the bake's own drops out of the block, so applying the same file twice gives the same bake. Nothing else in the bake changes.
3. Rebuild the plugin. Its bake loader (`plugin/dsp/Bake.cpp`, `Tuning.cpp`) applies the block (`applyTuning`):
   - the parameters;
   - every mode's T60 times `ring_length`;
   - the LFO rates scaled to `wander_rate_hz`;
   - the LF and MF weights from `lf_cutoff_hz`, `mf_lo_hz` and `mf_hi_hz`;
   - the NPZ settings, the output gain and the velocity curves.

   The loader also reads each slot's band gains and turns them into one gain per mode for that articulation (`excitationModeGains`). The engine multiplies each strike's coupling by it (`docs/PORT_MAP.md`, live-engine decision 7).

   The tuning build uses the same functions live, so the normal build plays what was tuned.

Re-baking a preset (`tools/bake_preset.py`) drops its trims, its band gains and its tuning block. `Engine`, the retired Python-matching path, refuses a bake with a tuning block or band gains.
