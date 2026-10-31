# Tuning by ear: the Tuning window

Kris tunes each cymbal by ear in Cymbalis's Tuning window, saves the settings as a tuning file (JSON), and brings the file to the project session. There, `tools/apply_tuning.py` writes it into the preset's bake in `resources/presets/`, and the plugin plays the tuned values from the bake. This document is the approved spec, 2026-10-16. The tuning adds no host parameters; the plugin parameters (`docs/UI_SPEC.md` section 9) stay as they are.

## One plugin, with the tuning code

Cymbalis (plugin code `Cymb`, bundle ID `com.dhammers.cymbalis`) has the TUNING button, the Tuning window, live tuning values, tuning files and tuning in its session state. The CMake option `CYMBALIS_WITH_TUNING` (ON by default) builds this tuning code in; the code tests the macro `CYMBALIS_TUNING`. `-DCYMBALIS_WITH_TUNING=OFF` builds a lean Cymbalis under the same identity, with no button, no window, no live values, no tuning files and no tuning in its session state. Both read a bake's `tuning` block when they load the bake (below), so both play what was tuned. CI builds only the full plugin (`docs/BUILDS.md`).

Cymbalis Tuning (plugin code `CymT`), the separate tuning build of earlier versions, is no longer built. Its tuning files load in Cymbalis (**LOAD...**).

## Opening the window

In Cymbalis, click **TUNING**, the box under the CYMBAL box (left column; `docs/UI_SPEC.md` section 3, layout v3). It opens the Tuning window; a second click closes it. While the window is open the box has the light outline of an open box. **Cmd-click on the CYMBALIS logo** (Ctrl-click on Linux) opens it too. The rest of the editor is unchanged; a lean build (above) has neither the button nor the window.

The Tuning window is attached to the editor's window. On macOS it is a child window of it:

- it is always in front of the editor and of the host's windows, never behind them;
- it opens on the editor's Space and moves with the editor;
- it hides while the host is in the background, so it does not cover other applications.

On other systems it is kept on top.

It opens at the editor's top right, where it opened before layout v3: below the MIDI CHANNEL box (1404,30, 110×36, the TUNING button's place in layout v2), right-aligned with it, so that box stays clickable; where the screen has no room below, it opens to the left of it. If you move it, it stays where you put it, unless that would cover that box when it next opens. The window is 860 pixels wide when it opens, so on an editor narrower than about 1120 pixels it reaches over the right part of the TUNING button. Its close button hides it, like the second click on TUNING. It closes when the editor closes.

The window has two parts, one below the other, each under a filled title band with a bronze rule:

- **ARTICULATIONS** ("affects only this articulation"): each articulation's level and velocity curve, and the band rows (excitation gain, onset delay and speed) of the articulation chosen in its **Articulation** box. A value here changes only the strikes of that articulation.
- **WHOLE CYMBAL** ("affects every articulation"): the values of the preset as a whole, by group. Some of them act only on strikes of some articulations, such as the edge wander (edge strikes) or the build-up weights (strikes at one place). Their labels name those articulations, for example "Bow brightness vs velocity (tau_ref): BOW, CENTER" on SMOKE FLAT RIDE.

The window uses plain JUCE controls in the editor's palette and font:

- Each value is a slider with a numeric box. Type a number into the box to enter it exactly.
- The mouse wheel and trackpad scrolling change no value. Over a slider or a numeric box, the band sliders included, they scroll the window's list instead. A value changes only by a click and drag, a typed number or a button (reset, FLAT, RESET ALL, LOAD...).
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
- **Ring length per band** applies at the next control point. Every ringing strike decays at the new rates from there, without a step in level. An edge strike's band envelope keeps the factors it was struck with; the change reaches the envelope from the next strike ("Ring length" below).
- **Energy cascade** and **loud-band damping** values apply from the next control point; **re-hit damping** values from the next strike.
- **Ring length is the exception:** it rebuilds the engine through the preset-switch path. The cymbal stops (30 ms fade) and restarts, and the change applies when the slider is released.

The values travel to the audio thread without locks or allocation.

### Per articulation

One set per bake entry: NOVA RIDE `bell_shoulder`, `bow_tip`, `edge`, `bell_tip`, `bow_shoulder`; SMOKE FLAT RIDE `bow`, `edge`, `center`, `bow_shoulder`, `center_shoulder`; HAMMERED THIN RIDE `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; SWISH `bell`, `bow_tip`, `bow_shoulder`, `edge`, `bell_shoulder`; RAVEN `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; KONSTAN CRASH 18" `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`. The articulation id is the entry's `articulation` key if it has one, else its overlay, else its name in lower case. Two entries of a bake may not share an id. Which rows and sticks play each articulation: `docs/UI_SPEC.md` section 8.

**Copies.** Some articulations are copies of another entry's data under their own id (`"articulation"`, `"copy_of"`, made by `tools/bake_preset.py`). Each is a full articulation with every per-articulation value of its own: level, velocity curve, band gains, onset shift and the re-hit damping weight. Each starts as an exact copy of its source, its level trim included, so untuned it plays exactly as its source. Changing a copy leaves its source alone, and the reverse.

| Preset | Articulation | Copy of | Played by |
|---|---|---|---|
| SMOKE FLAT RIDE | `center` | `bow` (at the bell position) | CENTER and CENTER2, TIP |
| SMOKE FLAT RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| SMOKE FLAT RIDE | `center_shoulder` | `center` | CENTER and CENTER2, SHOULDER |
| HAMMERED THIN RIDE | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| HAMMERED THIN RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| SWISH | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| RAVEN | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| RAVEN | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| KONSTAN CRASH 18" | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| KONSTAN CRASH 18" | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| NOVA RIDE | `edge` | `bow_shoulder` (at the edge position, 0.0) | EDGE |

Nova Ride's `edge` replaced the base preset played at the edge (Nova has no edge captures). It is the one articulation that does not play as it did before: the EDGE row now sounds like the bow shoulder struck at the edge. A tuning file with `edge` values from before applies them to the new `edge`.

#### NOVA SHOULDER PROFILE

Each shoulder articulation that is a copy of its tip has a **NOVA SHOULDER PROFILE** button below its values: SMOKE FLAT RIDE's, HAMMERED THIN RIDE's, RAVEN's and KONSTAN CRASH 18"'s (`bow_shoulder`, `center_shoulder`, `bell_shoulder`) and SWISH's `bell_shoulder`. SWISH's `bow_shoulder` is its own capture (an overlay), so it has none. It sets that articulation's band gains, attack gains and attack time to Nova Ride's shoulder-minus-tip difference: the bow profile for `bow_shoulder` and `center_shoulder`, the bell profile for `bell_shoulder`. It is a starting point to tune from. The button only fills those values, which then work as any others (reset, FLAT, save).

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
| STICK CLICK gain | `stick_gain` | 0–24 | yes | Level of the stick click, see below. Default: the articulation's `stick_gain` (6.0 for SWISH `bow_tip` and, since 2026-10-29, SWISH `edge`; 0 everywhere else: no click) |
| STICK CLICK band low, band high | `stick_bp_lo`, `stick_bp_hi` | 100–16000 Hz, 200–20000 Hz | yes | Edges of the click's band-pass. Default: the preset's values (2000 and 6000 Hz for NOVA RIDE and SWISH; Python's defaults 2500 and 9000 Hz for the 602, Bettis and Raven) |
| STICK CLICK length | `stick_len_ms` | 1–50 ms | yes | Length of the noise burst. Default: the preset's (NOVA RIDE 20, SWISH 8; Python's default 8) |
| STICK CLICK decay | `stick_decay_ms` | 0.5–50 ms | yes | Exponential decay of the burst. Default: the preset's (3; Python's default 3) |
| STICK CLICK bow weight, bell weight, edge weight | `stick_bow_weight`, `stick_bell_weight`, `stick_edge_weight` | 0–1 each | yes | The click's position weights, see below. Default: the articulation's own (NOVA RIDE and SWISH 1, 0, 0; SWISH `edge` 1, 0, 1 since 2026-10-29; Python's default 1, 1, 1 for the 602, Bettis, Raven and Konstan) |

The **velocity curve** is a level offset on top of the current level law. It is interpolated in log velocity through three points: 0 dB at velocity 1.0, the 0.6 offset at 0.6, and the 0.3 offset at 0.3. Below 0.3 it continues with the 0.6-to-0.3 slope (clamped to ±60 dB). It changes the level only. The spectral velocity behaviour stays as it is: velocity tables, the Hertz tilt and the NPZ captures. The velocity is the engine's: the MIDI velocity after the Velocity Sensitivity knob.

The **stick click** is the sound of the stick itself: a burst of noise at the strike, band-passed (order-2 Butterworth between the band edges) and decaying exponentially over the burst. Its level is the gain × the position weight × velocity^1.7, at the articulation's level (Python's `_add_transient`, `docs/PORT_MAP.md`, "Swish"). The position weight is bow fraction × bow weight + bell fraction × bell weight + edge fraction × edge weight, from the strike's position. The three weights are per articulation (STICK CLICK bow, bell and edge weight), starting at the articulation's own `stick_bow_weight`, `stick_bell_weight` and `stick_edge_weight` (Python's default 1 where it has none); NOVA RIDE and SWISH click only towards the bow, except SWISH's edge (below). The noise comes from the engine's random sequence. At gain 0 there is no click; every articulation but SWISH's `bow_tip` and `edge` starts there.

**SWISH's edge** (`edge`, set 2026-10-29, Kris's choice by ear). Its band envelopes start 4–10 dB under their peak in every band and swell to it over 120–480 ms, so a hard edge hit peaked about 200 ms in: a synth envelope rather than a China's edge, which explodes on impact. It now has a stick click (edge weight 1, gain 6, 1000–10000 Hz, 12 ms, decay 5 ms) and attack gains, the same in both layers: 0 dB from 89 to 356 Hz, +4 dB from 449 to 898 Hz, +8 dB from 1131 to 2263 Hz, +10 dB from 2851 to 11404 Hz (bands named by their lower edges), with an attack time of 25 ms. A hard edge hit now peaks at the strike, and the recorded swell follows unchanged. Setting the edge weight or the gain to 0 switches the click off.

**Brightness vs velocity** is how much brighter (positive) or darker (negative) a soft strike of the articulation is than a full one. The value is the spectral tilt, in dB per octave, at velocity 0.3 relative to velocity 1.0. It is interpolated in log velocity like the velocity curve: 0 at 1.0, the value at 0.3, and the same slope below 0.3 (within ±24 dB per octave). The tilt pivots at 1 kHz: at the strike, each mode's excitation is scaled by tilt × log2(f / 1 kHz) dB, so the level at 1 kHz is unchanged. It acts on the coupling, like the band gains, whatever path gives it. At 0 it is skipped.

#### Excitation band gains

Each articulation has a gain per one-third-octave band. It scales the energy that articulation's strikes put into the modes of that band. It is not an output EQ:

- **What it scales:** the strike's coupling into each mode, whatever path gives it: the velocity tables, the Hertz tilt, the NPZ velw captures and, through them, the edge band envelopes.
- **What it leaves alone:** the decay, the onset, the shared cymbal's state (energy already ringing, the build-up, the wander) and the other articulations. A change applies from the next strike.

The bands are the 22 one-third-octave bands of the onset data. Band j runs from 11404 × 2^((j − 21)/3) Hz to 11404 × 2^((j − 20)/3) Hz: 89.1–112.3 Hz, 112.3–141.4 Hz, …, 9051–11404 Hz, 11404–14368 Hz. A mode belongs to the band its frequency falls in; modes below 89.1 Hz go to the first band, modes above 14368 Hz to the last.

#### Onset shift

Each articulation also has, per band, an **onset delay** (ms) and an **onset speed** (above 1 is faster). They move that articulation's onset in time, band by band. They act from the next strike, on that articulation's strikes only. The decay after the onset, the levels and the band gains are unchanged. They are tuning departures from the SPEAR onset (`docs/PORT_MAP.md`, live-engine decision 8).

How they act depends on the onset the strike has. The articulation struck decides which values apply, even when a blend knob moves the strike.

- **Band envelope.** This covers 602, Bettis, Raven and Konstan edge strikes, and on those presets any strike towards the edge (edge fraction above 0.1, such as BOW with Blend 2 past 20 %). Band j's envelope is read at (t − delay) × speed. Before the delay ends it holds its first frame. A negative delay starts that far into the envelope; the ring length then scales that time after the kept onset ("Ring length"). The 20 ms in-frame decay and the envelope window (`edge_band_env_onset_s`) keep real time. A band slowed below ×1 may therefore still be inside its envelope when the window ends (the 602's envelopes run 9 s and its window 10 s; Bettis 16.2 s and 20 s; Raven 8.7 s and 9.2 s; Konstan 7.1 s and 7.6 s). The strike then rings on from its own decaying level, as at any window end; a longer window lets the envelope finish. The envelope bands are the NPZ bands of the same number: the same thirds, within 0.1 %.
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

Under the rows, **PEAK** shows when each band of the last strike of the chosen articulation peaked, in ms after the strike. Per mode this is the time its band envelope peaks (after the onset shift and the ring length's time scale) or the end of its onset ramp, and 0 for a mode with neither. The band shows the mean over its modes, weighted by their energy at the strike. A dash means the band had no energy, or the articulation has not been struck since the plugin loaded.

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
| | Edge capture curve | `edge_velocity_remap_exponent` | 0.25–2.0 | 602, Bettis, Swish, Raven, Konstan | Exponent of the NPZ velocity remap (0.5 = sqrt). Each capture still maps to itself; this shapes how the velocities between captures are reached |
| | Bow brightness vs velocity | `tau_ref_ms` | 0.05–8 ms | 602, Bettis, Swish, Raven, Konstan (no velocity tables) | `hertz_vel_scale` contact time at velocity 1 |
| | Ring length | `ring_length` | 0.5–2.0 × | all | Multiplier of the baked T60 of every mode (t60_scale), and of the length of an edge strike's band envelope after the kept onset; see "Ring length" below. Rebuilds the engine |
| | Kept onset | `edge_ring_keep_ms` | 0–300 ms | 602, Bettis, Swish, Raven, Konstan | How much of an edge strike's band envelope the ring length leaves as recorded; it scales the time after it ("Ring length"). Default 60 ms. Per strike |
| | Ring length per band | `ring_length_bands` | ×0.25–×4 per band | all | 22 values, see below. Default ×1 |
| Stereo | Pan per band | `stereo_pan_bands` | L100 to R100 per band | all | 22 values, −100 (L100) to 100 (R100). Default centre (0). See "Stereo" below |
| | Width per band | `stereo_width_bands` | 0–200 % per band | all | 22 values. Default 100 % |
| | Overall width | `stereo_width` | 0–200 % | all | Multiplies every band's width. Default 100 % |
| | Right-channel high-pass | `right_highpass` | on/off | all | The 2nd-order Butterworth high-pass on the right channel (Python's `_hp(out[:,1], 120)`). Default on |
| | Right-channel high-pass frequency | `right_highpass_hz` | 20–300 Hz | all | Default 120 Hz |
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
| Edge envelope | EDGE ONSET ENVELOPE | `edge_band_envelope` | on/off | 602, Bettis, Swish, Raven, Konstan | On (the bake; for Swish the bake switches it on, `docs/PORT_MAP.md`, "Swish"): an edge strike's level follows the NPZ band envelope for the window. Off: the strike rings from the start, with the onset ramp of its pk_t data, as Python plays Swish. Per strike |
| | EDGE VOICE FROM BOW | `edge_voice_from_bow` | 0–100 % | 602, Bettis, Swish, Raven, Konstan | Within each envelope band, how far the edge's long-ringing modes take the bow's mode pattern; see "Edge voice from bow" below. The bakes start at 100 % for Konstan and at 0 % for the 602, Bettis, Swish and Raven. Per strike |
| | Window | `edge_band_env_onset_s` | 0.1–30 s | 602, Bettis, Swish, Raven, Konstan | How long an edge strike follows the NPZ band envelope; per strike. A ring length above ×1 lengthens it, up to ×2 ("Ring length") |
| | 20 ms frame-decay restart | `edge_frame_decay_restart` | on/off | 602, Bettis, Swish, Raven, Konstan | On (the bake): the in-frame decay restarts every 20 ms (`docs/PORT_MAP.md`, "The in-frame decay is a 50 Hz sawtooth"). Off: no in-frame decay; the level follows the band envelope alone. Per strike |
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

#### Ring length

**Ring length** multiplies the baked T60 of every mode (`t60_scale`), so every strike rings that much longer or shorter. A change rebuilds the engine when the slider is released ("The values" above).

**Edge strikes with a band envelope.** On SMOKE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN and KONSTAN CRASH 18", an edge strike's level follows its recorded NPZ band envelope for the envelope window (EDGE ONSET ENVELOPE on), not its T60. The T60 alone changes only its 20 ms in-frame decay and its ring after the window. So the ring length also scales the envelope in time, after a kept onset:

- **The factor.** Each envelope band has a ring factor x = ring length × that band's **Ring length per band** (below). An envelope band takes the per-band value of the one-third-octave band its middle mode is in, as for the onset shift; on the 602, Bettis, Raven and Konstan that is the band of the same number. SWISH's 20 envelope bands are its own, so there the values of the 225–283 Hz, 898–1131 Hz, 7.2–9.1 kHz and 9.1–11.4 kHz bands reach no envelope (they still set those modes' T60).
- **The kept onset.** The envelope's first k ms play as recorded, so the attack keeps its timing. k is **Kept onset** (`edge_ring_keep_ms`, 0–300 ms, default 60 ms), in the CYMBAL group under Ring length.
- **The time scale.** After the kept onset k, the envelope is read x times slower: at time t after the strike it plays the recording's k + (t − k) / x. At ×2 everything after k takes twice as long: the bloom, the recorded flutter and the decay. At ×0.5 it takes half as long. The levels are the recording's; only their timing changes.
- **With the onset shift.** The envelope time is (t − delay) × speed, as before; the ring length then scales that time after k.
- **The window.** With a factor above 1 the envelope is stretched and may still be sounding when the window would end. The strike's window is then the **Window** times the largest factor of its bands, at most ×2: at ring length ×2, RAVEN's 9.2 s window becomes 18.4 s. With no factor above 1 the window is unchanged.
- **The 20 ms in-frame decay** is unchanged. It follows the modes' T60, which the ring length scales too. **20 ms frame-decay restart** switches it off.
- **When.** Each strike takes the factors and the kept onset when it is struck, like the onset shift. A change applies from the next strike.
- **At ×1** in every band nothing changes, whatever the kept onset: every preset plays exactly as before.

Where a band's envelope data end before the window, its last frame is held until the window ends, as before. The data end at k + (data length − k) × x after the strike. At ring length ×0.5 that is about halfway: RAVEN's 8.7 s of data end at 4.4 s and hold to 9.2 s. At ×2 they end at 17.3 s and hold to 18.4 s. Most last frames are 0 (`docs/PORT_MAP.md`, live-engine decision 20).

On every other strike, and on NOVA RIDE's edge, the ring length acts through the T60 alone.

#### Ring length per band

Each of the 22 one-third-octave bands of the band gains has a ring-length multiplier, ×0.25 to ×4 (default ×1). It multiplies the T60 of every mode in the band, on top of **Ring length**: T60 = baked T60 × ring length × the band's multiplier. Modes belong to bands by their frequency, as for the band gains. It acts on every articulation: the T60 belongs to the mode, not to the strike. On an edge strike with a band envelope it also scales the envelope's time after the kept onset, with the factor ring length × the band's multiplier ("Ring length" above).

In the window it is the **RING LENGTH PER BAND** row in the CYMBAL group: 22 vertical sliders on a logarithmic scale with ×1 in the middle, numeric entry, a per-band reset and FLAT (every band to ×1).

A change applies live, at the next control point (about 2.8 ms). The new decay coefficients are computed off the audio thread and handed over; there the ringing state is re-anchored, as a strike does without its contact, and every ringing strike decays at the new rates from that sample. Nothing steps. A band envelope keeps the factors of its strike; the change reaches the envelopes of the strikes after it. With every band at ×1 nothing changes and nothing is computed.

#### Stereo

How stereo works: every mode has a fixed left and right gain from the bake (the pick vectors `pick_l`, `pick_r`, random from the preset's seed, signed), and the right channel has a 120 Hz high-pass. The STEREO values change both.

- **Modes and bands.** Modes belong to the 22 bands by their frequency, as for the band gains.
- **A mode's baked gains** are a level, sqrt(L² + R²), and a position from −1 (hard left) to +1 (hard right): atan2(|R|, |L|) / (π/4) − 1. The baked positions lie within about L31 to R30.
- **Its new position** is the band's pan + its baked position × the band's width × the overall width, clamped to hard left / hard right.
- **Its new gains** are level × cos and level × sin of (new position + 1) × π/4 (constant power: the level stays), each with the sign of its baked gain.
- **Width 0 %** puts every mode of the band at the pan point; **100 %** at centre pan gives the baked gains exactly (the engine uses them as they are); **200 %** doubles the spread.
- The width scales each mode's whole baked position, the band's own mean offset with it (a band's mean baked position is near centre but not at it: mostly within L5 to R5, up to 9, and L24 for a band of Hammered Thin Ride that has one mode). So at centre and 100 % nothing changes, at 0 % every mode is at the pan point, and the spread around the band's mean doubles at 200 %.

**Live.** When a value changes, the Tuning window computes every mode's gains (off the audio thread) and sends them with the other values. The engine takes them at the next control point of the free phase and moves every ringing mode's gains to them linearly over that control block (about 3 ms), so nothing steps. A change that arrives while a strike's contact is running (a few ms) waits for its end. If a strike lands inside such a ramp, the gains stay where the ramp got to and the rest of the move follows the contact. While nothing rings, new gains apply at once. Outside a ramp block the engine renders exactly as before: no per-sample cost.

**The right-channel high-pass** can be switched off and moved from 20 to 300 Hz. A change moves the filter's coefficients linearly over 20 ms (a straight line between two stable 2nd-order high-passes stays stable, with its zeros at DC). Switching it on or off crossfades between the filtered and the unfiltered right channel over the same 20 ms. On at 120 Hz, the default, it is the filter as before.

In the window it is the **STEREO** group of WHOLE CYMBAL:
- a **PAN** row of 22 vertical sliders, L100 to R100. A value is typed as L30, R30, C or a signed number (− is left). It has a per-band reset and **CENTRE** (every band to the centre);
- a **WIDTH** row of 22 sliders, 0 to 200 %, with a per-band reset and **FLAT** (every band to 100 %);
- the **Overall width**;
- the **Right-channel high-pass** switch and its **frequency**.

**In the bake.** `tools/apply_tuning.py` stores the stereo values in the bake's tuning block. It also writes every mode's resulting left and right gains into a `stereo` block (`gain_l`, `gain_r`, checksummed like the other arrays). The plugin plays them at load as they are; a lean build (`CYMBALIS_WITH_TUNING` OFF) too. `pick_l` and `pick_r` stay as baked: the loader still regenerates them from the seed, and it checks that each stored gain pair keeps the level and signs of its pick vector entries. The high-pass settings go into the tuning block.

#### Edge voice from bow

On the presets with NPZ edge data, an edge strike's per-mode levels are the NPZ velw table, made by matching the recorded edge partials to the preset's modes. Those partials are dense and drift, so they land on other modes than the bow's. The edge's onset and band levels are right (Kris), but its tail rings on a different set of modes, so it sounds like a different cymbal. Of the 20 longest-ringing modes (coupling × √T60), the edge shares with the bow 8 on HAMMERED THIN RIDE (the closest by ear), 3 on KONSTAN (2 since its T60 refit), 2 on SMOKE and 0 on SWISH.

**EDGE VOICE FROM BOW** (`edge_voice_from_bow`, 0–100 %, in the EDGE ENVELOPE group below EDGE ONSET ENVELOPE; not a host parameter; approved by Kris, 2026-10-29) gives the long-ringing modes of each envelope band the bow's pattern and leaves the short-ringing ones the edge's, so the crash and the onset stay as recorded. Each band keeps its level at every NPZ velocity and the envelopes are untouched, so the velocity behaviour does not change. With a = value / 100:

1. **Weights per mode**, once when the preset loads (`edgeVoiceAlpha`, `plugin/dsp/Tuning.cpp`). For each envelope band, over its modes with `exc_norm` > 0 (at least 2): med = the median of their baked `t60_s` (of an even count, the mean of the middle two), and α = clamp((log2(T60 / med) + 1) / 2, 0, 1). A mode ringing half as long as the median or less has α = 0, twice as long or more α = 1. Modes with `exc_norm` 0, and bands with fewer than 2 such modes, have α = 0. The baked T60 is used, so the weights don't move with the ring length.
2. **Per NPZ velocity row and band** (`edgeVoiceVelw`): with ne = the norm of the row's velw over the band's modes and ns = that of their `exc_norm` (a band where either is 0 is left alone), each mode's new value is (1 − a·α) · velw / ne + a·α · exc_norm / ns, and the band is scaled back to ne.
3. **The engine** uses that table for the edge coupling from the next strike on; strikes already ringing keep their coupling. It is computed off the audio thread (when the preset loads, and by the Tuning window when the value changes) and handed over with the other values.

It acts wherever the NPZ coupling does: every strike with an edge fraction above 0.1, so also a bow or bell row blended towards the edge. At 0 % the table is the bake's, and every preset plays bit for bit as without the value. KONSTAN CRASH 18" starts at 100 % (its tuning block), with EDGE re-trimmed to its SPEAR target at 100 %; SMOKE FLAT RIDE, SWISH, HAMMERED THIN RIDE and RAVEN start at 0 %. SMOKE FLAT RIDE was tried at 100 % and set back to 0 % by Kris by ear (2026-10-31): he prefers its edge as it was.

The build checks these values at 100 % (`tools/pack_bake`), from the shipped bakes; the last column ranks the bow by `exc_norm` × √`t60_s` and the edge by the top velocity row of the new table × √`t60_s`:

| Preset | Modes with α = 1 | Modes with 0 < α < 1 | Of the 20 longest-ringing edge modes, shared with the bow |
|---|---|---|---|
| SMOKE FLAT RIDE | 206 | 469 | 14 (2 at 0 %) |
| SWISH | 226 | 378 | 6 (0 at 0 %) |
| KONSTAN CRASH 18" | 42 | 592 | 10 (2 at 0 %) |

No band has α = 1 for most of its modes: the most is 43 % (SMOKE's 566–713 Hz band, 15 of 35 modes).

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

- **Output, after the Volume and Pan knobs:** the peak (held for 1.5 s) and the RMS over about 300 ms, in dBFS. **CLIP** lights when a sample reaches 0 dBFS; click it to reset it.
- **Last hit, before the Volume knob:**
  - the row and articulation it played, and its velocity;
  - its peak, and the RMS of the first 500 ms after the strike, both channels.

  The 500 ms include whatever else is ringing. "(measuring)" shows until the 500 ms are over.

## Tuning files

**SAVE...** writes the current preset's tuning to a file (default name `<preset>.tuning.json`). **LOAD...** reads one. A file for another preset is refused with a message. So is a file for another bake format, an unknown value, band gains that are not 22 numbers from −24 to +12 dB, or a ring length, edge capture curve, wander rate or `tau_ref_ms` that is not above 0, an `edge_voice_from_bow` outside 0–100 % or a stick click weight outside 0–1. Cymbalis also saves each preset's tuning (if it differs from the bake) in its session state, and restores it with the session. A session without tuning, such as one saved by Cymbalis before it had the Tuning window, loads with every preset at its bake's values.

```json
{
  "format": "cymbalis-tuning",
  "version": 1,
  "preset": "nova_ride",
  "bake_format_version": 1,
  "date": "2026-10-16T14:02:11.000+02:00",
  "producer": "Cymbalis",
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
- `edge_voice_from_bow` is always written for the presets with NPZ edge data, even when it is the bake's. So a file without it is from before EDGE VOICE FROM BOW and was tuned without it: it loads as 0 %, in the window, from a session and in `tools/apply_tuning.py`.
- `effective` is a full copy of every value for reference; nothing reads it.
- `entry` names the bake entry (slot) of an articulation.
- `preset` is the preset id (`nova_ride`, `smoke_flat_ride`, `hammered_thin_ride`, `swish`, `raven`, `konstan`); `bake_format_version` is the bake's `format_version`.

## The round trip

1. Tune in the Tuning window, **SAVE...** the file, and bring it to the project session.
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
   - Each articulation's **brightness vs velocity** goes into the tuning block with its velocity curve, and so does its **stick click**, position weights included, where it differs from the slot's own values (its `stick_*` parameters, Python's defaults where it has none).
   - The **stereo** values go into the tuning block, and every mode's resulting left and right gains into the bake's `stereo` block (`gain_l`, `gain_r`). Centre and 100 % everywhere removes the block. The **right-channel high-pass** (on/off, frequency) goes into the tuning block.
   - **Every other value that differs from the bake's own** (the cymbal values and the velocity curves) goes into the bake's top-level `tuning` block: `{"format": "cymbalis-tuning", "version": 1, "applied_utc", "source", "cymbal": {...}, "articulations": {id: {"vel_0.6_db", "vel_0.3_db"}}}`. A per-band cymbal value is stored there as its 22 numbers.
   - **EDGE VOICE FROM BOW** goes into the tuning block like the other cymbal values (0 %, the neutral value, drops out). On a bake with NPZ edge data, a file without it gives 0 %.
   - A value equal to the bake's own drops out of the block, so applying the same file twice gives the same bake. Nothing else in the bake changes.
3. Rebuild the plugin. Its bake loader (`plugin/dsp/Bake.cpp`, `Tuning.cpp`) applies the block (`applyTuning`):
   - the parameters;
   - every mode's T60 times `ring_length` and its band's `ring_length_bands` factor;
   - the settings of the energy cascade, the loud-band damping and the re-hit damping (with its weights by articulation id), which the engine reads when it is built;
   - the LFO rates scaled to `wander_rate_hz`;
   - the LF and MF weights from `lf_cutoff_hz`, `mf_lo_hz` and `mf_hi_hz`;
   - the NPZ settings, the output gain and the velocity curves.

   The loader also reads each slot's band gains and turns them into one gain per mode for that articulation (`excitationModeGains`). The engine multiplies each strike's coupling by it (`docs/PORT_MAP.md`, live-engine decision 7). It reads each slot's onset shift, and the engine applies it to that articulation's band envelopes and onset peak times at each strike (decision 8). It reads each slot's attack band gains (from its attack data, else from its onset gains), which the engine applies at each strike (decision 15), and the brightness from the tuning block.

   The Tuning window applies the values live with the same functions, so the tuned bake plays what was heard while tuning.

Re-baking a preset (`tools/bake_preset.py`) drops its trims, its band gains, its onset shifts, its attack data and its tuning block. `Engine`, the retired Python-matching path, refuses a bake with a tuning block, band gains, onset shifts or attack data.
