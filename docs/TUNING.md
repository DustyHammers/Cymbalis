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

The window has two parts, one below the other, each under a filled title band with a bronze rule:

- **ARTICULATIONS** ("affects only this articulation"): each articulation's level and velocity curve, and the band rows (excitation gain, onset delay and speed) of the articulation chosen in its **Articulation** box. A value here changes only the strikes of that articulation.
- **WHOLE CYMBAL** ("affects every articulation"): the values of the preset as a whole, by group. Some of them act only on strikes of some articulations, such as the edge wander (edge strikes) or the build-up weights (strikes at one place). Their labels name those articulations, for example "Bow brightness vs velocity (tau_ref): BOW, CENTER" on SMOKE FLAT RIDE.

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
- **Ring length per band** applies at the next control point. Every ringing strike decays at the new rates from there, without a step in level.
- **Energy cascade** and **loud-band damping** values apply from the next control point; **re-hit damping** values from the next strike.
- **Ring length is the exception:** it rebuilds the engine through the preset-switch path. The cymbal stops (30 ms fade) and restarts, and the change applies when the slider is released.

The values travel to the audio thread without locks or allocation.

### Per articulation

One set per bake entry: NOVA RIDE `bell_shoulder`, `bow_tip`, `edge`, `bell_tip`, `bow_shoulder`; SMOKE FLAT RIDE `bow`, `edge`, `center`, `bow_shoulder`, `center_shoulder`; HAMMERED THIN RIDE `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; SWISH `bell`, `bow_tip`, `bow_shoulder`, `edge`, `bell_shoulder`. The articulation id is the entry's `articulation` key if it has one, else its overlay, else its name in lower case. Two entries of a bake may not share an id. Which rows and sticks play each articulation: `docs/UI_SPEC.md` section 8.

**Copies.** Some articulations are copies of another entry's data under their own id (`"articulation"`, `"copy_of"`, made by `tools/bake_preset.py`). Each is a full articulation with every per-articulation value of its own: level, velocity curve, band gains, onset shift and the re-hit damping weight. Each starts as an exact copy of its source, its level trim included, so untuned it plays exactly as its source. Changing a copy leaves its source alone, and the reverse.

| Preset | Articulation | Copy of | Played by |
|---|---|---|---|
| SMOKE FLAT RIDE | `center` | `bow` (at the bell position) | CENTER and CENTER2, TIP |
| SMOKE FLAT RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| SMOKE FLAT RIDE | `center_shoulder` | `center` | CENTER and CENTER2, SHOULDER |
| HAMMERED THIN RIDE | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| HAMMERED THIN RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| SWISH | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| NOVA RIDE | `edge` | `bow_shoulder` (at the edge position, 0.0) | EDGE |

Nova Ride's `edge` replaced the base preset played at the edge (Nova has no edge captures). It is the one articulation that does not play as it did before: the EDGE row now sounds like the bow shoulder struck at the edge. A tuning file with `edge` values from before applies them to the new `edge`.

#### NOVA SHOULDER PROFILE

Each shoulder articulation that is a copy of its tip has a **NOVA SHOULDER PROFILE** button below its values: SMOKE FLAT RIDE's and HAMMERED THIN RIDE's (`bow_shoulder`, `center_shoulder`, `bell_shoulder`) and SWISH's `bell_shoulder`. SWISH's `bow_shoulder` is its own capture (an overlay), so it has none. It sets that articulation's band gains, attack gains and attack time to Nova Ride's shoulder-minus-tip difference: the bow profile for `bow_shoulder` and `center_shoulder`, the bell profile for `bell_shoulder`. It is a starting point to tune from. The button only fills those values, which then work as any others (reset, FLAT, save).

The two profiles are data, computed once by `tools/shoulder_profile.py` from the Nova bake and stored in `plugin/tuning/ShoulderProfiles.h` (`--check` verifies the header). The band gains:

1. Each mode's coupling at velocity 0.9 for Nova's tip and shoulder articulation at its base position, as the engine computes it: `exc_norm` × velocity participation ^ `vel_gain_power` × position weight. The bow profile uses `bow_tip` and `bow_shoulder` at 0.5 (bow velocity tables), the bell profile `bell_tip` and `bell_shoulder` at 1.0 (bell velocity tables).
2. Per band, the energy: the sum of the squared couplings of its modes.
3. The shoulder's band energies are scaled so that both have the same total energy; per band, the ratio shoulder / tip in dB.
4. Bands with fewer than 5 Nova modes, or with no energy in either articulation, are set to 0 dB: bands 1–4 (112–283 Hz, 0–3 modes) for both, and bands 0 and 5 for the bell (Nova's bell strikes put no energy there).
5. Smoothed over neighbouring bands: weights 1/4, 1/2, 1/4, renormalised over the neighbours that step 4 did not zero.
6. Clamped to ±12 dB.

| Band (Hz) | 89 | 112 | 141 | 178 | 225 | 283 | 356 | 449 | 566 | 713 | 898 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bow (dB) | −12.00 | 0 | 0 | 0 | 0 | +0.57 | +6.96 | +9.12 | +5.83 | +2.57 | +1.78 |
| bell (dB) | 0 | 0 | 0 | 0 | 0 | 0 | +1.07 | +2.96 | +2.19 | +0.42 | −0.66 |

| Band (Hz) | 1131 | 1426 | 1796 | 2263 | 2851 | 3592 | 4526 | 5702 | 7184 | 9051 | 11404 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bow (dB) | +3.71 | +6.59 | +7.27 | +4.44 | +3.61 | +4.39 | +3.18 | +1.12 | −1.46 | −3.04 | −3.75 |
| bell (dB) | −0.02 | +4.56 | +7.68 | +5.22 | +4.03 | +3.49 | −1.57 | −5.10 | −5.62 | −4.07 | −0.93 |

(Each band is named by its lower edge.) Nova's bow shoulder has almost no energy in band 0 (89–112 Hz, 6 modes): −35 dB there before the clamp.

The attack gains and time: the shoulder's onset gains relative to the tip's.

- Each Nova articulation's onset gains (9 octave bands) are taken in the 22 bands, as the plugin does (see "Attack band gains" below); an articulation without onset gains counts as 0 dB.
- The profile is shoulder minus tip, per band, clamped to ±12 dB, with the shoulder's attack time.
- Bow: `bow_shoulder` is neutral (0 dB, 80 ms) against `bow_tip`'s onset gains (80 ms), so the profile is the tip's gains inverted, 80 ms.
- Bell: `bell_shoulder` (300 ms) against `bell_tip` (120 ms), 300 ms. The 1.4–2.9 kHz octave (2.4 against 0.45, +14.5 dB) is clamped to +12 dB.

| Band (Hz) | 89 | 112 | 141 | 178 | 225 | 283 | 356 | 449 | 566 | 713 | 898 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bow attack (dB) | −1.58 | −1.58 | −1.58 | −0.26 | −0.26 | −0.26 | +0.45 | +0.45 | +0.45 | +0.82 | +0.82 |
| bell attack (dB) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

| Band (Hz) | 1131 | 1426 | 1796 | 2263 | 2851 | 3592 | 4526 | 5702 | 7184 | 9051 | 11404 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bow attack (dB) | +0.82 | +0.92 | +0.92 | +0.92 | +0.92 | +0.92 | +0.92 | −0.67 | −0.67 | −0.67 | −1.58 |
| bell attack (dB) | 0 | +12.00 | +12.00 | +12.00 | +5.11 | +5.11 | +5.11 | −11.02 | −11.02 | −11.02 | +3.00 |

Attack time: bow 80 ms, bell 300 ms.

| Value | Key | Range | Per strike | Effect |
|---|---|---|---|---|
| Level at velocity 1.0 | `level_db` | −40 to +40 dB | yes | The slot's level trim, initialised from the bake (`slots[].level_trim.gain_db`) |
| Offset at velocity 0.6 | `vel_0.6_db` | −24 to +24 dB | yes | Velocity curve, see below. Default 0 |
| Offset at velocity 0.3 | `vel_0.3_db` | −24 to +24 dB | yes | Velocity curve. Default 0 |
| Excitation band gains | `band_gains_db` | −24 to +12 dB per band | yes | 22 values, one per one-third-octave band, see below. Default 0 |
| Onset delay | `onset_delay_ms`, `onset_delay_ms_soft` | −50 to +200 ms per band | yes | 22 values per layer (HARD, SOFT), see "Onset shift" and "Velocity layers". Default 0 |
| Onset speed | `onset_speed`, `onset_speed_soft` | ×0.25 to ×4 per band | yes | 22 values per layer; above 1 is faster. Default 1 |
| Attack band gains | `attack_band_gains_db`, `attack_band_gains_db_soft` | −12 to +12 dB per band | yes | 22 values per layer, see "Attack band gains". Default: the articulation's onset gains (0 dB without), in both layers |
| Attack time | `attack_time_ms` | 10–1000 ms | yes | How fast the attack gains fade back to 0 dB. Default: the articulation's onset time (80 ms without onset gains) |
| Brightness vs velocity | `brightness_db_per_oct` | −6 to +6 dB per octave | yes | Tilt of a velocity-0.3 strike relative to velocity 1.0, see below. Default 0 |
| STICK CLICK gain | `stick_gain` | 0–24 | yes | Level of the stick click, see below. Default: the articulation's `stick_gain` (6.0 for SWISH `bow_tip`, 0 everywhere else: no click) |
| STICK CLICK band low, band high | `stick_bp_lo`, `stick_bp_hi` | 100–16000 Hz, 200–20000 Hz | yes | Edges of the click's band-pass. Default: the preset's values (2000 and 6000 Hz for NOVA RIDE and SWISH; Python's defaults 2500 and 9000 Hz for the 602 and Bettis) |
| STICK CLICK length | `stick_len_ms` | 1–50 ms | yes | Length of the noise burst. Default: the preset's (NOVA RIDE 20, SWISH 8; Python's default 8) |
| STICK CLICK decay | `stick_decay_ms` | 0.5–50 ms | yes | Exponential decay of the burst. Default: the preset's (3; Python's default 3) |

The **velocity curve** is a level offset on top of the current level law. It is interpolated in log velocity through three points: 0 dB at velocity 1.0, the 0.6 offset at 0.6, and the 0.3 offset at 0.3. Below 0.3 it continues with the 0.6-to-0.3 slope (clamped to ±60 dB). It changes the level only. The spectral velocity behaviour stays as it is: velocity tables, the Hertz tilt and the NPZ captures. The velocity is the engine's: the MIDI velocity after the Velocity Sensitivity knob.

The **stick click** is the sound of the stick itself: a burst of noise at the strike, band-passed (order-2 Butterworth between the band edges) and decaying exponentially over the burst. Its level is the gain × the position weight × velocity^1.7, at the articulation's level (Python's `_add_transient`, `docs/PORT_MAP.md`, "Swish"). The position weight comes from the preset (`stick_bow_weight`, `stick_bell_weight`, `stick_edge_weight` over the strike's bow, bell and edge fractions; SWISH and NOVA RIDE click only towards the bow). The noise comes from the engine's random sequence. At gain 0 there is no click; every articulation but SWISH's `bow_tip` starts there.

**Brightness vs velocity** is how much brighter (positive) or darker (negative) a soft strike of the articulation is than a full one. The value is the spectral tilt, in dB per octave, at velocity 0.3 relative to velocity 1.0. It is interpolated in log velocity like the velocity curve: 0 at 1.0, the value at 0.3, and the same slope below 0.3 (within ±24 dB per octave). The tilt pivots at 1 kHz: at the strike, each mode's excitation is scaled by tilt × log2(f / 1 kHz) dB, so the level at 1 kHz is unchanged. It acts on the coupling, like the band gains, whatever path gives it. At 0 it is skipped.

#### Excitation band gains

Each articulation has a gain per one-third-octave band. It scales the energy that articulation's strikes put into the modes of that band. It is not an output EQ:

- **What it scales:** the strike's coupling into each mode, whatever path gives it: the velocity tables, the Hertz tilt, the NPZ velw captures and, through them, the edge band envelopes.
- **What it leaves alone:** the decay, the onset, the shared cymbal's state (energy already ringing, the build-up, the wander) and the other articulations. A change applies from the next strike.

The bands are the 22 one-third-octave bands of the onset data. Band j runs from 11404 × 2^((j − 21)/3) Hz to 11404 × 2^((j − 20)/3) Hz: 89.1–112.3 Hz, 112.3–141.4 Hz, …, 9051–11404 Hz, 11404–14368 Hz. A mode belongs to the band its frequency falls in; modes below 89.1 Hz go to the first band, modes above 14368 Hz to the last.

#### Onset shift

Each articulation also has, per band, an **onset delay** (ms) and an **onset speed** (above 1 is faster). They move that articulation's onset in time, band by band. They act from the next strike, on that articulation's strikes only. The decay after the onset, the levels and the band gains are unchanged. They are tuning departures from the SPEAR onset (`docs/PORT_MAP.md`, live-engine decision 8).

How they act depends on the onset the strike has. The articulation struck decides which values apply, even when a blend knob moves the strike.

- **Band envelope.** This covers 602 and Bettis edge strikes, and on those presets any strike towards the edge (edge fraction above 0.1, such as BOW with Blend 2 past 20 %). Band j's envelope is read at (t − delay) × speed. Before the delay ends it holds its first frame. A negative delay starts that far into the envelope. The 20 ms in-frame decay and the envelope window (`edge_band_env_onset_s`) keep real time. A band slowed below ×1 may therefore still be inside its envelope when the window ends (the 602's envelopes run 9 s and its window 10 s; Bettis 16.2 s and 20 s). The strike then rings on from its own decaying level, as at any window end; a longer window lets the envelope finish. The envelope bands are the NPZ bands of the same number: the same thirds, within 0.1 %.
- **Onset ramp.** This covers Nova edge strikes, and Nova strikes towards the edge (edge fraction above 0.1). Each mode's ramp, from 0.4 of its level (the birth fraction) to full, peaks at peak time / speed + delay, never below 0. A peak time of 1 ms or less means no ramp.
- **Bow, bell and center strikes at their base positions.** The engine has no onset ramp here: Python skips it for strikes with an edge fraction of 0.1 or less ("skip ramp for bow"). Their peak time is therefore 0:
  - a positive delay ramps the band from 0.4 to full level over the delay;
  - the speed has no onset to act on;
  - a negative delay changes nothing.

The attack band gains (Nova's onset gains among them) are not shifted: they start at the strike. A shifted bow or bell onset costs some CPU while its ramp runs; with every band of every articulation shifted, NOVA RIDE at 96 kHz uses about 26 % of a core instead of 21 %.

#### Velocity layers

The onset delay, the onset speed and the attack gain each have two layers per articulation: **SOFT**, the value at velocity 0.3, and **HARD**, the value at velocity 1.0. At a strike, each band takes the value between the two in log velocity, as the velocity curve does: HARD at 1.0, SOFT at 0.3, and SOFT below 0.3. With weight t = ln(v) / ln(0.3) (0 at 1.0, 1 at 0.3):

- delay and attack gain: HARD + t × (SOFT − HARD);
- speed: geometric, HARD^(1 − t) × SOFT^t, so ×2 and ×0.5 meet at ×1 halfway.

They act on top of the onset the strike already has, as above. A band envelope keeps its own blend between the captures of its velocity, and the shift is applied to it. Bow, bell and center strikes act on their per-mode onset peak times. The attack time is a single value.

- **Defaults.** Both layers start equal, at the articulation's values: no delay, ×1, and the attack gains of the bake (Nova's from its onset gains, 0 dB elsewhere). With equal layers a band has the same value at every velocity, and every preset plays exactly as before.
- **Cost.** With equal layers the strike uses the articulation's values directly. With different layers it computes 22 values per strike from them (and 22 attack gains); nothing allocates.

In the window, a bar above the attack gain row chooses the layer that the **ATTACK GAIN**, **ONSET DELAY** and **ONSET SPEED** rows show and edit:

- **SOFT (0.3)** / **HARD (1.0)**: the layer shown. It starts on HARD.
- **LINK** (on by default): an edit, a band's reset or FLAT sets both layers to the value. Off: only the layer shown changes.
- **COPY SOFT -> HARD**: the SOFT layer's attack gains, onset delays and onset speeds become the HARD layer's.

Under the rows, **PEAK** shows when each band of the last strike of the chosen articulation peaked, in ms after the strike. Per mode this is the time its band envelope peaks (after the onset shift) or the end of its onset ramp, and 0 for a mode with neither. The band shows the mean over its modes, weighted by their energy at the strike. A dash means the band had no energy, or the articulation has not been struck since the plugin loaded.

#### Attack band gains

Each articulation has a gain per band at the strike, **ATTACK GAIN** (dB), that fades back to 0 dB with the **ATTACK TIME**: the gain at time t after the strike is 1 + (g − 1) · exp(−t / time), g the band's gain at the strike. It applies to that articulation's strikes only, from the next strike, and changes the onset only; the decay and levels after it are unchanged. It is how Nova's onset band gains (`bow_onset_band_gains`, `bow_onset_tau_ms`) always worked, now for every articulation of every preset.

- **Nova's onset gains are these.** `bell_shoulder`, `bow_tip` and `bell_tip` have onset gains in 9 octave bands (50–89 Hz, 89–178 Hz, … 11404–22050 Hz). Each of the 22 bands takes the value of the octave band that contains its centre, in dB, with the same time (300, 80 and 120 ms). Every Nova mode gets the gain it had: the only mode that falls in a different octave band (81 Hz) has the same gain in both. So NOVA RIDE plays exactly as before. The other articulations start at 0 dB with 80 ms.
- **Where.** As in Python, the attack gains do not apply to a bow or bell strike blended all the way to the edge (edge fraction 0.9 or more, Blend 2 above 90 %). An edge articulation's apply at the edge.
- **Gains within 0.01 dB of 0** (|g − 1| below 0.001) count as 0 dB, as Python's do.

In the window, the **BANDS** section below the articulations works like a graphic EQ:

- The **Articulation** box picks the articulation whose bands are shown.
- It has four rows of sliders: **GAIN** (dB), **ATTACK GAIN** (dB, with the **ATTACK TIME** under it), **ONSET DELAY** (ms) and **ONSET SPEED** (×, on a logarithmic scale with ×1 in the middle). The velocity layer bar sits above the attack gain row, and the **PEAK** readout under the onset speed row ("Velocity layers" above).
- Each row has one vertical slider per band. The label is the band's centre frequency; hold the pointer over the label to see the band's edges.
- Type a number into the box under a slider to enter it exactly.
- The arrow under each band resets it to the baked value.
- A row's **FLAT** sets every band of that row to no change: 0 dB, 0 ms or ×1.
- A changed band has a bronze bar at its left, like a changed row.

### Per cymbal

| Group | Value | Key | Range | Presets | Notes |
|---|---|---|---|---|---|
| Cymbal | Output gain | `output_gain_db` | −24 to +12 dB | all | Whole preset, for headroom; before the Volume knob |
| | Edge capture curve | `edge_velocity_remap_exponent` | 0.25–2.0 | 602, Bettis, Swish | Exponent of the NPZ velocity remap (0.5 = sqrt). Each capture still maps to itself; this shapes how the velocities between captures are reached |
| | Bow brightness vs velocity | `tau_ref_ms` | 0.05–8 ms | 602, Bettis, Swish (no velocity tables) | `hertz_vel_scale` contact time at velocity 1 |
| | Ring length | `ring_length` | 0.5–2.0 × | all | Multiplier of the baked T60 of every mode (t60_scale). Rebuilds the engine |
| | Ring length per band | `ring_length_bands` | ×0.25–×4 per band | all | 22 values, see below. Default ×1 |
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
| Edge envelope | EDGE ONSET ENVELOPE | `edge_band_envelope` | on/off | 602, Bettis, Swish | On (the bake; for Swish the bake switches it on, `docs/PORT_MAP.md`, "Swish"): an edge strike's level follows the NPZ band envelope for the window. Off: the strike rings from the start, with the onset ramp of its pk_t data, as Python plays Swish. Per strike |
| | Window | `edge_band_env_onset_s` | 0.1–30 s | 602, Bettis, Swish | How long an edge strike follows the NPZ band envelope; per strike |
| | 20 ms frame-decay restart | `edge_frame_decay_restart` | on/off | 602, Bettis, Swish | On (the bake): the in-frame decay restarts every 20 ms (`docs/PORT_MAP.md`, "The in-frame decay is a 50 Hz sawtooth"). Off: no in-frame decay; the level follows the band envelope alone. Per strike |
| Energy cascade | Amount | `cascade_amount` | 0–1 | all | 0 (default) switches it off. See "Energy cascade" below |
| | Threshold | `cascade_threshold_db` | −40 to 0 dB | all | Per band, relative to that band's energy after a velocity-1.0 BOW strike. Default −12 dB |
| | Speed (attack) | `cascade_speed_ms` | 5–1000 ms | all | How fast the transfer starts, and its time constant at amount 1. Default 100 ms |
| | Release | `cascade_release_ms` | 10–3000 ms | all | How fast the transfer stops once the band falls below the threshold. Default 150 ms |
| | Spread | `cascade_spread` | 1–6 bands | all | How far up the energy goes. Default 2 |
| | Lowest band, highest band | `cascade_low_band`, `cascade_high_band` | band 0–21 | all | The bands that take part (shown as their centre frequencies). Default 0 and 21: all |
| Loud-band damping | Amount | `loud_damp_amount` | 0–4 | all | 0 (default) switches it off. See "Loud-band damping" below |
| | Threshold | `loud_damp_threshold_db` | −40 to 0 dB | all | As the cascade's: per band, relative to a velocity-1.0 BOW strike. Default −12 dB |
| | Tilt | `loud_damp_tilt` | −1 to +1 | all | Below 0 more effect on the low bands, above 0 on the high bands. Default 0 |
| Re-hit damping | Amount | `rehit_damp_amount` | 0–1 | all | 0 (default) switches it off. See "Re-hit damping" below |
| | Time | `rehit_damp_time_ms` | 1–50 ms | all | How long the damping takes. Default 10 ms |
| | Weight, per articulation | `rehit_damp_weights` | 0–2 each | all | One row per articulation of the preset, e.g. "Weight: BOW". Default 1 |

A slider's range widens to include a baked or loaded value outside it. Values for other presets are hidden.

#### Ring length per band

Each of the 22 one-third-octave bands of the band gains has a ring-length multiplier, ×0.25 to ×4 (default ×1). It multiplies the T60 of every mode in the band, on top of **Ring length**: T60 = baked T60 × ring length × the band's multiplier. Modes belong to bands by their frequency, as for the band gains. It acts on every articulation: the T60 belongs to the mode, not to the strike.

In the window it is the **RING LENGTH PER BAND** row in the CYMBAL group: 22 vertical sliders on a logarithmic scale with ×1 in the middle, numeric entry, a per-band reset and FLAT (every band to ×1).

A change applies live, at the next control point (about 2.8 ms). The new decay coefficients are computed off the audio thread and handed over; there the ringing state is re-anchored, as a strike does without its contact, and every ringing strike decays at the new rates from that sample. Nothing steps. With every band at ×1 nothing changes and nothing is computed.

#### Energy cascade

After hard hits, energy moves from lower bands into higher ones over time, and the cymbal's total energy stays the same: the bloom and wash of a crash. The bands are the 22 one-third-octave bands of the band gains. It is a plugin mechanism (`docs/PORT_MAP.md`, plugin mechanism 10) and acts on the whole cymbal, whatever struck it.

At every control point (about 2.8 ms) the engine:

1. **Measures** the ringing energy of each band: every strike together, as the modes ring (before the LF and MF build-up scales).
2. **Compares** it with the band's threshold. 0 dB is that band's energy right after a velocity-1.0 strike of the preset's BOW articulation (as it plays now: its level and band gains count). At −12 dB, a band rings above its threshold after a BOW strike of about velocity 0.5 or harder.
3. **Activates** the band. Its activation rises towards 1 while it is above the threshold, with the attack time **Speed**, and falls towards 0 below it, with the **Release** time. So the transfer starts smoothly and stops smoothly once the band has fallen below the threshold.
4. **Moves** energy. Each band from the lowest to the highest gives up the fraction 1 − exp(−amount × activation × 2.8 ms / Speed) of its energy per control point. At amount 1 and full activation, a band's energy moves on with the time constant Speed.
   - The energy goes to the bands up to **Spread** above it, within the highest band. Each band further up gets half as much as the one below it.
   - Within a band it goes to the ringing modes in proportion to their excitation weights (`exc_norm`).
   - It arrives as new ringing energy in each mode, with a random phase from the engine's random sequence, and decays with the mode's own T60. A band that receives energy and is above its own threshold passes energy on in turn.

Energy is conserved: what leaves a band arrives above it. The moved part is exact; its interference with what already rings in a mode averages out over a band's modes, as when two strikes add. A highest band has nowhere to pass energy to, so energy gathers there.

Nothing clicks: the source modes are scaled down and the target modes raised across each control block, with no step. A strike that lands inside a block takes the part of that block's change already played.

With amount 0 the cascade is skipped entirely: nothing is measured or computed, and every preset plays exactly as before. In the window it is the **ENERGY CASCADE** group of the whole-cymbal part.

Limits:
- Energy goes only to modes that are ringing. Silent modes (below the skip threshold of 1e-9 × level) receive none, and a band with no ringing mode takes no energy.
- The moved energy is held by one strike component of the engine's pool of 24. When the pool holds 24 strikes and none of them has settled (dense playing, every strike still in its onset), nothing moves for that control point.
- During a strike's contact (NOVA RIDE only, about 0.3 ms) nothing moves.

#### Loud-band damping

While a band rings above its threshold, its modes decay faster. It is a plugin mechanism (`docs/PORT_MAP.md`, plugin mechanism 11) and acts on the whole cymbal.

At every control point the band energies are measured as for the energy cascade (every strike together, before the LF and MF build-up scales). For each band:

- **How far above** the threshold it is: 0 at the threshold, 1 at 20 dB above it and beyond (capped), linear in dB in between. It is smoothed over 20 ms, so the damping comes and goes without steps.
- **Tilt** weights the bands: 1 − tilt for the lowest band, 1 + tilt for the highest, linear in between. At −1 the low bands get up to twice the effect and the highest none; at +1 the reverse.
- **The decay rate** of the band's modes becomes rate × (1 + amount × tilt weight × how far above). The rate is the mode's own, from its T60. At amount 4, a band 20 dB or more above its threshold (tilt 0) decays five times as fast.

The extra decay acts on every strike ringing in the band alike. It is applied smoothly across each control block, with no step. With amount 0 it is skipped entirely. In the window it is the **LOUD-BAND DAMPING** group of the whole-cymbal part.

When the energy cascade is also on, the damping acts first in each control block, and the cascade moves part of what is left.

#### Re-hit damping

A new strike first damps what is already ringing, then adds its own energy. It is a plugin mechanism (`docs/PORT_MAP.md`, plugin mechanism 12) and acts on the whole cymbal.

- **How much:** at a strike, everything already ringing is scaled down to 1 − amount × velocity × the struck articulation's weight (never below 0). The velocity is the engine's: after the Velocity Sensitivity knob. For example, at amount 0.5, a velocity-0.8 BOW strike with weight 1 leaves 60 % of the ringing amplitude.
- **How fast:** the scaling runs over **Time** (1–50 ms) as a smooth S-curve, from the moment the strike's contact ends (NOVA RIDE: about 0.3 ms; the other presets have no contact phase). It is evaluated at the control points (about 2.8 ms apart) and interpolated in between. So a Time shorter than one control block still takes about one block, smoothly, with no click.
- **What it acts on:** only the energy that was already ringing, every strike of it. The new strike's own energy is not scaled.
- **Weights:** each articulation of the preset has its own weight (0–2, default 1). A weight of 0 means that articulation's strikes do not damp; 2 doubles the effect.
- **Strikes in quick succession:** a strike that lands while an earlier damping is still running carries what is left of it into its own: the earlier damping completes within the new strike's Time.

With amount 0 nothing is computed and every preset plays exactly as before (a damping already running when the amount is set to 0 completes). In the window it is the **RE-HIT DAMPING** group of the whole-cymbal part, with one weight row per articulation.

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
  "cymbal": { "mf_sens_depth": 0.3, "ring_length": 1.2,
              "ring_length_bands": [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.7, 0.6, 0.5, 0.5],
              "cascade_amount": 0.4, "cascade_threshold_db": -18.0, "cascade_spread": 3.0,
              "rehit_damp_amount": 0.5, "rehit_damp_weights": { "bell_tip": 1, "bell_shoulder": 1, "bow_tip": 1, "bow_shoulder": 1, "edge": 0.5 } },
  "articulations": {
    "bow_tip": { "entry": "BOW", "level_db": -1.0, "vel_0.6_db": -2.0 },
    "edge": { "entry": "EDGE", "band_gains_db": [0, 0, 0, -3.5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2.0, 2.0, 0, 0],
              "onset_delay_ms": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 25.0, 40.0] }
  },
  "effective": {
    "cymbal": { "output_gain_db": 0.0, "...": "every value of the preset" },
    "articulations": { "bow_tip": { "entry": "BOW", "level_db": -1.0, "vel_0.6_db": -2.0, "vel_0.3_db": 0.0, "band_gains_db": ["22 values"],
                                    "onset_delay_ms": ["22 values"], "onset_speed": ["22 values"] }, "...": "" }
  }
}
```

- `cymbal` and `articulations` hold only the values that differ from the bake; loading a file applies these on top of the bake. A per-band cymbal value (`ring_length_bands`) is written as all 22 bands, lowest first, when any differs. `rehit_damp_weights` is written with every articulation's weight, by articulation id, when any differs. An articulation's `band_gains_db`, `attack_band_gains_db`, `onset_delay_ms` and `onset_speed` are each written as all 22 bands, lowest first, when any of them differs. The last three are the HARD layers; their SOFT layers, `attack_band_gains_db_soft`, `onset_delay_ms_soft` and `onset_speed_soft`, are written with them when either layer differs. A file without the `_soft` keys (from before the layers) gives both layers its values.
- `effective` is a full copy of every value for reference; nothing reads it.
- `entry` names the bake entry (slot) of an articulation.
- `preset` is the preset id (`nova_ride`, `smoke_flat_ride`, `hammered_thin_ride`, `swish`); `bake_format_version` is the bake's `format_version`.

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
   - Each articulation's **onset shift** goes into its onset data, `slots[].onset_shift`: `{"band_edges_hz": [23 edges], "delay_ms": [22], "speed": [22], "delay_ms_soft": [22], "speed_soft": [22], "set_by"}`, both velocity layers. An articulation with delay 0 and speed 1 in every band of both layers has no entry.
   - Each articulation's **attack band gains and time** go into its attack data, `slots[].attack_band_gains`: `{"band_edges_hz": [23 edges], "gains_db": [22], "gains_db_soft": [22], "time_ms", "set_by"}`. An articulation whose values are those its onset gains give, in both layers (0 dB and 80 ms without onset gains), has no entry; the loader then takes them from the onset gains. A bake entry without the `_soft` arrays (from before the layers) uses its values for both layers.
   - Each articulation's **brightness vs velocity** goes into the tuning block with its velocity curve, and so does its **stick click** where it differs from the slot's own values (its `stick_*` parameters, Python's defaults where it has none).
   - **Every other value that differs from the bake's own** (the cymbal values and the velocity curves) goes into the bake's top-level `tuning` block: `{"format": "cymbalis-tuning", "version": 1, "applied_utc", "source", "cymbal": {...}, "articulations": {id: {"vel_0.6_db", "vel_0.3_db"}}}`. A per-band cymbal value is stored there as its 22 numbers.
   - A value equal to the bake's own drops out of the block, so applying the same file twice gives the same bake. Nothing else in the bake changes.
3. Rebuild the plugin. Its bake loader (`plugin/dsp/Bake.cpp`, `Tuning.cpp`) applies the block (`applyTuning`):
   - the parameters;
   - every mode's T60 times `ring_length` and its band's `ring_length_bands` factor;
   - the settings of the energy cascade, the loud-band damping and the re-hit damping (with its weights by articulation id), which the engine reads when it is built;
   - the LFO rates scaled to `wander_rate_hz`;
   - the LF and MF weights from `lf_cutoff_hz`, `mf_lo_hz` and `mf_hi_hz`;
   - the NPZ settings, the output gain and the velocity curves.

   The loader also reads each slot's band gains and turns them into one gain per mode for that articulation (`excitationModeGains`). The engine multiplies each strike's coupling by it (`docs/PORT_MAP.md`, live-engine decision 7). It reads each slot's onset shift, and the engine applies it to that articulation's band envelopes and onset peak times at each strike (decision 8). It reads each slot's attack band gains (from its attack data, else from its onset gains), which the engine applies at each strike (decision 15), and the brightness from the tuning block.

   The tuning build uses the same functions live, so the normal build plays what was tuned.

Re-baking a preset (`tools/bake_preset.py`) drops its trims, its band gains, its onset shifts, its attack data and its tuning block. `Engine`, the retired Python-matching path, refuses a bake with a tuning block, band gains, onset shifts or attack data.
