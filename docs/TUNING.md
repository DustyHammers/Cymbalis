# Tuning by ear: the TUNING page

Kris tunes each cymbal by ear on Cymbalis's TUNING page, saves the settings as a tuning file (JSON), and brings the file to the project session. There, `tools/apply_tuning.py` writes it into the preset's bake in `resources/presets/`, and the plugin plays the tuned values from the bake. This document is the approved spec, 2026-10-16. The tuning adds no host parameters; the plugin parameters (`docs/UI_SPEC.md` section 9) stay as they are.

## One plugin, with the tuning code

Cymbalis (plugin code `Cymb`, bundle ID `com.dhammers.cymbalis`) has the TUNING page (its tab in the editor's left column), live tuning values, tuning files and tuning in its session state. The CMake option `CYMBALIS_WITH_TUNING` (ON by default) builds this tuning code in; the code tests the macro `CYMBALIS_TUNING`. `-DCYMBALIS_WITH_TUNING=OFF` builds a lean Cymbalis under the same identity, with no TUNING page (no tabs: its editor shows the CYMBAL page), no live values, no tuning files and no tuning in its session state. Both read a bake's `tuning` block when they load the bake (below), so both play what was tuned. CI builds only the full plugin (`docs/BUILDS.md`).

Cymbalis Tuning (plugin code `CymT`), the separate tuning build of earlier versions, is no longer built. Its tuning files load in Cymbalis (**LOAD...**).

## The TUNING page

The editor has two pages, **CYMBAL** and **TUNING**, chosen by the two tabs in its left column, under the subtitle (`docs/UI_SPEC.md` sections 3 and 6); the pointer is a pointing hand over them. Click **TUNING** to show the tuning panel in the editor, and **CYMBAL** to go back to the cymbal. Both pages keep the background with its title and subtitle, the tabs, the CYMBAL label and box, the MIDI CHANNEL label and box and the DHammers logo; the CYMBAL page has everything else (the cymbal, the rows, the knobs and, on HI-HAT, the pedal), and the TUNING page hides those and shows the panel instead. The preset can be changed on the TUNING page too, with the CYMBAL box: the panel then shows the new preset's values. A lean build (above) has no tabs and shows only the CYMBAL page.

- **Where:** the panel fills the page area, from x 460 to 1500 and y 100 to 1000 in the editor's design units (1040 × 900), clear of the subtitle.
- **Text size:** the panel keeps the size it had in the window: it is not scaled with the editor, but laid out and drawn at one pixel per pixel in the page area's rectangle on screen (Kris chose this from the project session's mockups, 2026-11-24). At 1536 × 1024 that rectangle is 1040 × 900 pixels, at the opening size 1104 × 736 about 748 × 647, at the smallest size, 960 × 640, about 650 × 563. The list scrolls, and its layout follows the rectangle's width, as the window's did when it was resized. On a Retina display JUCE draws it at the display's scale, so it is sharp.
- **The open page** is kept while the plugin instance lives (a new editor opens on it) and saved in the session as `editorPage` (0 CYMBAL, 1 TUNING). A session without it opens on CYMBAL; a lean build ignores it.
- **The hidden page rests:** while the TUNING page shows, the wireframe cymbal is not drawn and its OpenGL context does not render; hits keep being taken from the processor, so its queue never fills, but they are not drawn, and back on the CYMBAL page the cymbal shows as it is then (the hits it missed are not replayed); the rows' lights follow the same rule. While the CYMBAL page shows, the panel's timer (its meters and refresh) stops; back on the TUNING page it shows the current preset (rebuilt if it changed), its values and its meters at once. The meters update 30 times a second; their text is made only when the readings have changed.
- **The panel** is made the first time the TUNING page opens and kept until the editor closes. Every value, the hand-over to the engine, tuning files and the session's tuning work as they did in the window.

Until 2026-11-24 (approved by Kris that day, from the project session's mockups) the panel was a separate **Tuning window**: a click on the CYMBALIS title, at the editor's top left (within 6 units of its letters), opened it and a second click closed it. It was attached to the editor's window (on macOS a child window of it, in front of the editor and the host's windows, on the editor's Space, hidden while the host was in the background; elsewhere kept on top), opened 860 pixels wide below the MIDI CHANNEL box, right-aligned with it, and its close button hid it. Until 2026-11-20 the **TUNING** box under the CYMBAL box opened it (and Cmd-click on the logo opened it too).

The panel has two parts, one below the other, each under a filled title band with a bronze rule (on STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN and EXTRA PAPER THIN 24", the SIZZLE CHAIN group comes first, above them: "Sizzle chain" below):

- **ARTICULATIONS** ("affects only this articulation"): each articulation's level and velocity curve, and the band rows (excitation gain, onset delay and speed) of the articulation chosen in its **Articulation** box. A value here changes only the strikes of that articulation.
- **WHOLE CYMBAL** ("affects every articulation"): the values of the preset as a whole, by group. Some of them act only on strikes of some articulations, such as the edge wander (edge strikes) or the build-up weights (strikes at one place). Their labels name those articulations, for example "Bow brightness vs velocity (tau_ref): BOW, CENTER" on STINE FLAT RIDE.

The panel uses plain JUCE controls in the editor's palette and font:

- Each value is a slider with a numeric box. Type a number into the box to enter it exactly.
- The mouse wheel and trackpad scrolling change no value. Over a slider or a numeric box, the band sliders included, they scroll the panel's list instead. A value changes only by a click and drag, a typed number or a button (reset, FLAT, RESET ALL, LOAD...).
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

One set per bake entry: NOVA RIDE `bell_shoulder`, `bow_tip`, `edge`, `bell_tip`, `bow_shoulder`; STINE FLAT RIDE `bow`, `edge`, `center`, `bow_shoulder`, `center_shoulder`; HAMMERED THIN RIDE `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; SWISH `bell`, `bow_tip`, `bow_shoulder`, `edge`, `bell_shoulder`; RAVEN `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; EXTRA PAPER THIN 24" `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; KONSTAN CRASH 18" `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; CRASH 16" `bell`, `bow`, `edge`, `bell_shoulder`, `bow_shoulder`; HI-HAT `tip`, `edge`, `foot` (see "HI-HAT" below). The articulation id is the entry's `articulation` key if it has one, else its overlay, else its name in lower case. Two entries of a bake may not share an id. Which rows and sticks play each articulation: `docs/UI_SPEC.md` section 8.

**Copies.** Some articulations are copies of another entry's data under their own id (`"articulation"`, `"copy_of"`, made by `tools/bake_preset.py`). Each is a full articulation with every per-articulation value of its own: level, velocity curve, band gains, onset shift and the re-hit damping weight. Each starts as an exact copy of its source, its level trim included, so untuned it plays exactly as its source. Changing a copy leaves its source alone, and the reverse.

| Preset | Articulation | Copy of | Played by |
|---|---|---|---|
| STINE FLAT RIDE | `center` | `bow` (at the bell position) | CENTER and CENTER2, TIP |
| STINE FLAT RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| STINE FLAT RIDE | `center_shoulder` | `center` | CENTER and CENTER2, SHOULDER |
| HAMMERED THIN RIDE | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| HAMMERED THIN RIDE | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| SWISH | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| RAVEN | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| RAVEN | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| EXTRA PAPER THIN 24" | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| EXTRA PAPER THIN 24" | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| KONSTAN CRASH 18" | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| KONSTAN CRASH 18" | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| CRASH 16" | `bell_shoulder` | `bell` | BELL and BELL2, SHOULDER |
| CRASH 16" | `bow_shoulder` | `bow` | BOW and BOW2, SHOULDER |
| NOVA RIDE | `edge` | `bow_shoulder` (at the edge position, 0.0) | EDGE |

#### HI-HAT

HI-HAT has four articulations (`docs/PORT_MAP.md`, "Hi-hat"): `tip` (the BOW row, its stick fixed on TIP), `edge` (the EDGE row, SHOULDER), `foot` (no row: the pedal closing plays it) and `splash` (no row: the pedal reopening quickly after a FOOT plays it, since 2026-11-26; "HI-HAT: the foot splash" below). Their strikes follow band envelopes interpolated from the hats' captures at the pedal's openness and the strike's velocity, so not every value acts on them:

- **Act, as on every preset:** level, the velocity curve (a level at velocities 0.6 and 0.3, on top of the captures' own velocity response), brightness vs velocity, the excitation band gains, the attack band gains and time, the onset delay and speed (each band's envelope read at (t − delay) × speed, as on an NPZ edge strike), and the stick click's gain, band, length and decay. FOOT and SPLASH have no click (gain 0); TIP's and EDGE's start at 0.1398 and 0.4189 (3 dB under the hit's first 3 ms). SPLASH's values show and act as FOOT's do.
- **Hidden:** the stick click's bow, bell and edge weights (the hi-hat's strikes have no position on the cymbal; they stay 1).

All four start with the same level trim, EDGE's, so TIP, FOOT and SPLASH keep their captured levels relative to EDGE. There is no NOVA SHOULDER PROFILE button on the hi-hat.

Nova Ride's `edge` replaced the base preset played at the edge (Nova has no edge captures). It is the one articulation that does not play as it did before: the EDGE row now sounds like the bow shoulder struck at the edge. A tuning file with `edge` values from before applies them to the new `edge`.

#### NOVA SHOULDER PROFILE

Every shoulder articulation of every preset but NOVA RIDE has a **NOVA SHOULDER PROFILE** button below its values: `bell_shoulder`, `bow_shoulder` and `center_shoulder` of STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN, EXTRA PAPER THIN 24", KONSTAN CRASH 18" and CRASH 16". Most are copies of their tip. SWISH's `bow_shoulder` is an overlay entry, not a copy, but not a shoulder capture either: its overlay (`presets/swish/articulations/bow_shoulder.json`) only sets `stick_gain` 0.0, and `bow_tip.json` only sets `stick_gain` 6.0, so SWISH's BOW and BOW_SHOULDER entries differ only in the stick click and the level trim (+8.86 and +8.96 dB): its bow shoulder is the bow tip without the click, like the copies. It has the button too since 2026-10-31 (Kris), with the bow profile. NOVA RIDE has none: its shoulders are the profiles' source. It sets that articulation's band gains, attack gains and attack time to Nova Ride's shoulder-minus-tip difference: the bow profile for `bow_shoulder` and `center_shoulder`, the bell profile for `bell_shoulder`. It is a starting point to tune from. The button only fills those values, which then work as any others (reset, FLAT, save).

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
| STICK CLICK band low, band high | `stick_bp_lo`, `stick_bp_hi` | 100–16000 Hz, 200–20000 Hz | yes | Edges of the click's band-pass. Default: the preset's values (2000 and 6000 Hz for NOVA RIDE and SWISH; Python's defaults 2500 and 9000 Hz for Stine, Bettis, Raven, Paper Thin and Crash 16) |
| STICK CLICK length | `stick_len_ms` | 1–50 ms | yes | Length of the noise burst. Default: the preset's (NOVA RIDE 20, SWISH 8; Python's default 8) |
| STICK CLICK decay | `stick_decay_ms` | 0.5–50 ms | yes | Exponential decay of the burst. Default: the preset's (3; Python's default 3) |
| STICK CLICK bow weight, bell weight, edge weight | `stick_bow_weight`, `stick_bell_weight`, `stick_edge_weight` | 0–1 each | yes | The click's position weights, see below. Default: the articulation's own (NOVA RIDE and SWISH 1, 0, 0; SWISH `edge` 1, 0, 1 since 2026-10-29; Python's default 1, 1, 1 for Stine, Bettis, Raven, Paper Thin, Konstan and Crash 16) |

The **velocity curve** is a level offset on top of the current level law. It is interpolated in log velocity through three points: 0 dB at velocity 1.0, the 0.6 offset at 0.6, and the 0.3 offset at 0.3. Below 0.3 it continues with the 0.6-to-0.3 slope (clamped to ±60 dB). It changes the level only. The spectral velocity behaviour stays as it is: velocity tables, the Hertz tilt and the NPZ captures. The velocity is the engine's: the MIDI velocity after the Velocity Sensitivity knob.

**EXTRA PAPER THIN 24"'s edge** (`edge`, Kris, 2026-11-21) starts with a curve of −4 dB at 0.6 and at 0.3, so flat below 0.6, and a level 4 dB above its capped trim: it plays as the capped trim alone at velocity 0.6 and below and gets louder only between 0.6 and 1.0, up to +4 dB (`docs/PORT_MAP.md`, "Extra Paper Thin Ride 24"", decision 6). Raising its level, or its curve towards 0 dB, by ear raises its loudest hits: at the start, a dense run of EDGE hits at velocity 1.0 with BOW hits at 0.8 peaks at about 0.86 at the plugin's output (99th percentile).

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

- **Band envelope.** This covers Stine, Bettis, Raven, Paper Thin, Konstan and Crash 16 edge strikes, and on those presets any strike towards the edge (edge fraction above 0.1, such as BOW with Blend 2 past 20 %). Band j's envelope is read at (t − delay) × speed. Before the delay ends it holds its first frame. A negative delay starts that far into the envelope; the ring length then scales that time after the kept onset ("Ring length"). The 20 ms in-frame decay and the envelope window (`edge_band_env_onset_s`) keep real time. A band slowed below ×1 may therefore still be inside its envelope when the window ends (Stine's envelopes run 30 s and its window 30 s; Bettis 16.2 s and 20 s; Raven 8.7 s and 9.2 s; Paper Thin 13.7 s and 14.2 s; Konstan 7.1 s and 7.6 s; Crash 16 8.4 s and 8.9 s). The strike then rings on from its own decaying level, as at any window end; a longer window lets the envelope finish. The envelope bands are the NPZ bands of the same number: the same thirds, within 0.1 %.
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

In the panel, a bar above the attack gain row chooses the layer that the **ATTACK GAIN**, **ONSET DELAY** and **ONSET SPEED** rows show and edit:

- **SOFT (0.3)** / **HARD (1.0)**: the layer shown. It starts on HARD.
- **LINK** (on by default): an edit, a band's reset or FLAT sets both layers to the value. Off: only the layer shown changes.
- **COPY SOFT -> HARD**: the SOFT layer's attack gains, onset delays and onset speeds become the HARD layer's.

Under the rows, **PEAK** shows when each band of the last strike of the chosen articulation peaked, in ms after the strike. Per mode this is the time its band envelope peaks (after the onset shift and the ring length's time scale) or the end of its onset ramp, and 0 for a mode with neither. The band shows the mean over its modes, weighted by their energy at the strike. A dash means the band had no energy, or the articulation has not been struck since the plugin loaded.

#### Attack band gains

Each articulation has a gain per band at the strike, **ATTACK GAIN** (dB), that fades back to 0 dB with the **ATTACK TIME**: the gain at time t after the strike is 1 + (g − 1) · exp(−t / time), g the band's gain at the strike. It applies to that articulation's strikes only, from the next strike, and changes the onset only; the decay and levels after it are unchanged. It is how Nova's onset band gains (`bow_onset_band_gains`, `bow_onset_tau_ms`) always worked, now for every articulation of every preset.

- **Nova's onset gains are these.** `bell_shoulder`, `bow_tip` and `bell_tip` have onset gains in 9 octave bands (50–89 Hz, 89–178 Hz, … 11404–22050 Hz). Each of the 22 bands takes the value of the octave band that contains its centre, in dB, with the same time (300, 80 and 120 ms). Every Nova mode gets the gain it had: the only mode that falls in a different octave band (81 Hz) has the same gain in both. So NOVA RIDE plays exactly as before. The other articulations start at 0 dB with 80 ms.
- **Where.** As in Python, the attack gains do not apply to a bow or bell strike blended all the way to the edge (edge fraction 0.9 or more, Blend 2 above 90 %). An edge articulation's apply at the edge.
- **Gains within 0.01 dB of 0** (|g − 1| below 0.001) count as 0 dB, as Python's do.

In the panel, the **BANDS** section below the articulations works like a graphic EQ:

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
| | Edge capture curve | `edge_velocity_remap_exponent` | 0.25–2.0 | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 | Exponent of the NPZ velocity remap (0.5 = sqrt). Each capture still maps to itself; this shapes how the velocities between captures are reached |
| | Bow brightness vs velocity | `tau_ref_ms` | 0.05–8 ms | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 (no velocity tables) | `hertz_vel_scale` contact time at velocity 1 |
| | Ring length | `ring_length` | 0.5–2.0 × | all | Multiplier of the baked T60 of every mode (t60_scale), and of the length of an edge strike's band envelope after the kept onset (on HI-HAT every strike's); see "Ring length" below. Rebuilds the engine |
| | Kept onset | `edge_ring_keep_ms` | 0–300 ms | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16, HI-HAT | How much of an edge strike's band envelope (on HI-HAT every strike's) the ring length leaves as recorded; it scales the time after it ("Ring length"). Default 60 ms. Per strike |
| | Ring length per band | `ring_length_bands` | ×0.25–×4 per band | all | 22 values, see below. Default ×1 |
| | TIP OPEN LEVEL | `hh_tip_open_level` | 0–100 % | HI-HAT | How much of the closed tip/edge level difference (per band, dB, smoothed to its straight-line fit over the bands) an open TIP strike drops at full opening: at openness o, (1 − value × o) × the difference. 0 %: as before. The bake's: 60 %. Per strike. See "HI-HAT: the open tip and the ring floor" below |
| | TIP OPEN TICK | `hh_tip_open_tick_db` | 0 to +12 dB | HI-HAT | The TIP's stick click raised by value × o dB at the strike's openness o. 0: as before. The bake's: +4 dB. Per strike |
| | RING FLOOR | `hh_ring_floor` | 0–200 % | HI-HAT | Each band's decay rate at least value × the median of the 22 at that openness (TIP and EDGE). 100 %: as PR #58 set it; 0 %: the raw rates; above 100 % shorter tails. The bake's: 100 %. Per strike |
| | FREE RING | `hh_free_ring` | on/off | HI-HAT | The fully open knot (1.0) plays as a single free cymbal: its mode weights from `presets/hihat/data/hihat_weights_free.json` and a smoothed envelope. Off: as before. The bake's: off. Per strike. See "HI-HAT: the open-hat options" below |
| | TIP MODES | `hh_tip_modes_db` | 0–20 dB | HI-HAT | An open TIP knot's mode weights lean towards the modes a tip strike favours, within ±value dB per mode; each band keeps its total. 0 dB: as before. The bake's: 0 dB. Per strike |
| | MAX OPEN | `hh_max_open` | 75–100 % | HI-HAT | The openness the pedal reaches at CC 127: o = CC / 127 × value. The hats' and the pedal's drawing and every openness rule use it. 100 %: as before. The bake's: 100 % |
| | SPLASH WINDOW | `hh_splash_window_ms` | 50–400 ms | HI-HAT | After a FOOT, how long the pedal has to reopen to SPLASH REOPEN for a foot splash. The bake's: 150 ms. From the next FOOT. See "HI-HAT: the foot splash" below |
| | SPLASH REOPEN | `hh_splash_reopen` | 5–80 % | HI-HAT | The smoothed openness at which the reopening pedal plays the splash. The bake's: 25 %. From the next FOOT |
| Hit variation | HIT VARIATION | `hit_variation` | on/off | all | Switches both spreads below. Off: every strike plays exactly as without it. Default off; every bake starts on (2026-10-31). See "Hit variation" below |
| | POSITION SPREAD | `hit_position_spread` | 0–0.15 (position units: 0 edge, 0.5 bow, 1 bell) | all but HI-HAT | Each strike's position moves by a uniform random amount in ±spread. Default 0.04. The hi-hat's strikes have no position |
| | VELOCITY SPREAD | `hit_velocity_spread_pct` | 0–15 % | all | Each strike's velocity is multiplied by 1 + a uniform random amount in ±spread. Default 3 % |
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
| Edge envelope | EDGE ONSET ENVELOPE | `edge_band_envelope` | on/off | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 | On (the bake; for Swish the bake switches it on, `docs/PORT_MAP.md`, "Swish"): an edge strike's level follows the NPZ band envelope for the window. Off: the strike rings from the start, with the onset ramp of its pk_t data, as Python plays Swish. Per strike |
| | EDGE VOICE FROM BOW | `edge_voice_from_bow` | 0–100 % | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 | Within each envelope band, how far the edge's long-ringing modes take the bow's mode pattern; see "Edge voice from bow" below. The bakes start at 100 % for Konstan and at 0 % for Stine, Bettis, Swish and Raven. Per strike |
| | Window | `edge_band_env_onset_s` | 0.1–30 s | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 | How long an edge strike follows the NPZ band envelope; per strike. A ring length above ×1 lengthens it, up to ×2 ("Ring length") |
| | 20 ms frame-decay restart | `edge_frame_decay_restart` | on/off | Stine, Bettis, Swish, Raven, Paper Thin, Konstan, Crash 16 | On (the bake): the in-frame decay restarts every 20 ms (`docs/PORT_MAP.md`, "The in-frame decay is a 50 Hz sawtooth"). Off: no in-frame decay; the level follows the band envelope alone. Per strike |
| Energy cascade | Amount | `cascade_amount` | 0–1 | all | 0 (default) switches it off. See "Energy cascade" below |
| | Threshold | `cascade_threshold_db` | −40 to 0 dB | all | Per band, relative to that band's energy after a velocity-1.0 BOW strike (on HI-HAT a closed velocity-1.0 TIP strike at its peak). Default −12 dB |
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

On HI-HAT (`docs/PORT_MAP.md`, "Hi-hat") every per-cymbal value in the table acts as on the other presets, except those listed for other presets only and POSITION SPREAD: the ring length and ring length per band scale each band's envelope time after the kept onset (its strikes follow envelopes, not T60s), the MF and LF build-up and the edge wander count TIP as a bow strike and EDGE, FOOT and SPLASH as edge strikes (their positions 0.5 and 0.0), and the cascade and loud-band damping thresholds are relative to a closed TIP strike. The pedal and its choke are not tuning values: the openness comes from the pedal CC (`docs/UI_SPEC.md` section 9). TIP OPEN LEVEL, TIP OPEN TICK, RING FLOOR, FREE RING, TIP MODES, MAX OPEN, SPLASH WINDOW and SPLASH REOPEN are the hi-hat's own (below).

#### HI-HAT: the open tip and the ring floor

Three cymbal-wide values in the CYMBAL group, shown for HI-HAT only (approved by Kris, 2026-11-21; tuning values, not host parameters), so Kris can set by ear the open tip's level and stick attack against the edge, and how long the hats' quieter bands ring. Each takes effect for strikes struck after it changes, and a strike's openness is the pedal's at its note-on.

- **TIP OPEN LEVEL** (`hh_tip_open_level`, 0–100 %, the bake's 60 %). With the hats mostly or fully open the tip on the bow sounded weak and out of proportion to the edge (shoulder) at the same opening (Kris, 2026-11-21). The tip at an opening takes the EDGE's envelope there times the per-band level difference between the closed tip and the closed edge at that velocity (`docs/hihat_reference.py`, `HiHat.envelope`); in the captures that gap grows with velocity (the closed tip is about 4.5 dB under the closed edge at velocity 0.6 and 10.9 dB at 1.0: a tip on clamped hats barely gets louder), so on open hats a hard tip sat 7–8 dB under the edge at every opening. Each open knot's tip, at the knot's openness x, now uses (1 − value × x) × the difference; between the knots the interpolation makes it smooth in o. A closed strike (o = 0) is unchanged at any setting. Since the open modes change (2026-11-21, `docs/PORT_MAP.md`, "Hi-hat") the difference is smoothed first: per band it jumped by up to 13 dB, so the open tip takes its straight-line fit over the 22 bands (least squares on the band index), and TIP OPEN LEVEL scales that. The open tip also rings its open knot's own modes now, not the closed tip's.
- **TIP OPEN TICK** (`hh_tip_open_tick_db`, 0 to +12 dB, the bake's +4 dB). The TIP's stick click did not grow with the opening while the cymbal behind it got louder. It is raised by value × o dB.
- **RING FLOOR** (`hh_ring_floor`, 0–200 %, the bake's 100 %). PR #58 floored each band's decay rate at the median of the 22 band rates at that openness (`HiHat.rate`): that ended the long low tail, but it also shortened open hits, by up to about 3.5 dB at the PR #56 check points. Each band's rate is now at least value × the median, wherever the floored rates are used: TIP and EDGE's ring-on past their captured envelope, the opening's target, the choke's target and reference. 100 % is PR #58's floor; 0 % turns it off (the raw rates; PR #58's floor rule and choke from the highest opening stay); above 100 % shortens the tails further. FOOT keeps the raw rates.

#### HI-HAT: the open-hat options

Three more cymbal-wide values in the CYMBAL group, shown for HI-HAT only (approved by Kris, 2026-11-21; tuning values, not host parameters), so Kris can try them by ear: all three start off, and with them off the hi-hat plays bit for bit as before them. With the hats mostly or fully open (CC about 100–127), above all on TIP and at high velocities, the sound was phasey and unnatural; open hats should sound like a single cymbal ringing free (pedal up, the clutch clear of the bottom cymbal).

- **FREE RING** (`hh_free_ring`, on/off, the bake's off). The fully open capture (V7, the 1.0 knot) still has the bottom cymbal in it: its level pulses at about 1 and 4 Hz, its partials are short-lived (about 7,200 SPEAR tracks in 1.5 s, against about 1,700 for a closed hit), and the plugin rang all of that as one cymbal; only about 240 of its modes come from partials lasting 0.4 s or more, the top cymbal ringing. On, the 1.0 knot plays as that single free cymbal: its mode weights from `presets/hihat/data/hihat_weights_free.json` (`edge_open_100_free`: from the partials lasting at least 0.4 s, unit norm per band; Kris's data branch `presets-hihat-free`, its MD5 in `docs/BASELINE_MANIFEST.md`) and its stick envelope smoothed in the bake (`HiHat.free_envelope`): per band, in log amplitude, a 75th-percentile filter over 31 frames (centred, the edges repeated); the first 5 frames kept as they are; from frame 5 on each frame at most the one before it (a running minimum, frame 4 included), so the band never rises; values at the floor (under 1.5e-9) set to 0. Both versions are in the bake, so the switch acts from the next strike. Only strikes that use the 1.0 knot change (openness above 0.75); the shared per-mode T60s and `exc_norm` stay as they were. This is "version C" of the project session's renders.
- **TIP MODES** (`hh_tip_modes_db`, 0–20 dB, the bake's 0). Since the open modes change, an open TIP uses its open knot's modes scaled as a whole, so it favours no modes of its own and sounds like a quieter EDGE. Above 0, an open TIP knot's weights are multiplied per mode by r = (w_tip + 0.001) / (w_edge + 0.001), w_tip and w_edge the closed TIP's and closed EDGE's weights at the strike's velocity (interpolated over velocity as the closed knots are), r clamped to ±value dB; then each band is scaled back to the knot's own weights' norm in it. So the band levels stay and only which modes ring inside each band changes. Above about 1 kHz, where the closed and open hats share most modes, it brings over the tip's own mode emphasis; in the low bands the ratio is mostly near 1. With FREE RING on, it acts on the free weights at the 1.0 knot.
- **MAX OPEN** (`hh_max_open`, 75–100 %, the bake's 100 %). The openness the pedal reaches at CC 127: o = CC / 127 × value, before the pedal's smoothing. Everything that uses the openness uses the scaled one: the knots, the choke, the opening, FOOT's thresholds, the hats' and the pedal's drawing. A change moves the pedal to where it asks for at the new scale.

Measured in memory (2026-11-21): at the defaults the hi-hat plays bit for bit as before (open and closed hits, a fast close firing FOOT, a closed hit opened and closed, random fast playing with the pedal moving); closed hits and FOOT are bit for bit at any setting (FOOT fired by a close from the top changes with MAX OPEN only because the pedal starts lower); FREE RING changes TIP and EDGE strikes above openness 0.75 only; TIP MODES leaves every band's norm as it was (to 1e-15) and EDGE untouched; at MAX OPEN 80 % CC 127 gives openness 0.800.

Starting points heard in the project session's prototype: 60 % with +4 dB brings a fully open tip to about 4 dB under the edge (5 dB at 60 % open), 80 % with +6 dB to about 3 dB under. Measured in memory (2026-11-21; a fully open TIP against a fully open EDGE at velocity 0.8, stick clicks on, the first 300 ms): −8.5 dB at 0 % and 0 dB, −4.2 dB at the bake's 60 % and +4 dB (−5.5 dB at 60 % open), −2.7 dB at 80 % and +6 dB. The click itself rises by exactly value × o (+4.0 dB fully open, +2.0 dB half open); over the first 300 ms that adds less than 0.1 dB. At 0 %, 0 dB and 100 % the hi-hat plays bit for bit as before them.

#### HI-HAT: the foot splash

Two cymbal-wide values in the CYMBAL group, shown for HI-HAT only (approved by Kris by ear on 2026-11-25, from the project session's renders of a prototype in a copy of `FastEngine`, "B sounds right"; tuning values, not host parameters). A foot splash is the pedal pressed down quickly and let up: the hats close briefly, open, and ring with the foot up. The pedal alone plays it, with no note or row (Kris: "pedal only"). After a FOOT, the splash is armed until the FOOT's time + SPLASH WINDOW; if the pedal's smoothed openness reaches SPLASH REOPEN within that time, a SPLASH strike is struck there, at the FOOT's velocity (`docs/PORT_MAP.md`, "Hi-hat", "The foot splash").

- **SPLASH WINDOW** (`hh_splash_window_ms`, 50–400 ms, the bake's 150 ms). Why 150 ms: at 250 ms, open/closed 8ths at 120 bpm (EDGE open on the "and", the pedal closing on the beat and reopening for the next "and") fired splashes Kris wouldn't want; at 150 ms and 100 ms they played identically to before the splash.
- **SPLASH REOPEN** (`hh_splash_reopen`, 5–80 %, the bake's 25 %). The smoothed openness that plays the splash.

Both take effect from the next FOOT (a splash already armed keeps the values it was armed with). They are saved in tuning files and `tools/apply_tuning.py` writes them. SPLASH's own articulation values (level, velocity curve, brightness vs velocity, band gains, attack, onset shift; no stick click) show and act as FOOT's do.

#### Ring length

**Ring length** multiplies the baked T60 of every mode (`t60_scale`), so every strike rings that much longer or shorter. A change rebuilds the engine when the slider is released ("The values" above).

**Edge strikes with a band envelope.** On STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN, EXTRA PAPER THIN 24", KONSTAN CRASH 18" and CRASH 16", an edge strike's level follows its recorded NPZ band envelope for the envelope window (EDGE ONSET ENVELOPE on), not its T60. The T60 alone changes only its 20 ms in-frame decay and its ring after the window. So the ring length also scales the envelope in time, after a kept onset:

- **The factor.** Each envelope band has a ring factor x = ring length × that band's **Ring length per band** (below). An envelope band takes the per-band value of the one-third-octave band its middle mode is in, as for the onset shift; on Stine, Bettis, Raven, Paper Thin, Konstan and Crash 16 that is the band of the same number. SWISH's 20 envelope bands are its own, so there the values of the 225–283 Hz, 898–1131 Hz, 7.2–9.1 kHz and 9.1–11.4 kHz bands reach no envelope (they still set those modes' T60).
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

In the panel it is the **RING LENGTH PER BAND** row in the CYMBAL group: 22 vertical sliders on a logarithmic scale with ×1 in the middle, numeric entry, a per-band reset and FLAT (every band to ×1).

A change applies live, at the next control point (about 2.8 ms). The new decay coefficients are computed off the audio thread and handed over; there the ringing state is re-anchored, as a strike does without its contact, and every ringing strike decays at the new rates from that sample. Nothing steps. A band envelope keeps the factors of its strike; the change reaches the envelopes of the strikes after it. With every band at ×1 nothing changes and nothing is computed.

#### Hit variation

Every strike draws fresh random mode phases, so the beating of close modes and the attack's fine texture change from hit to hit, but a mode's level is fixed by the row, the velocity, the stick and the BLEND POSITION knobs: eight identical RAVEN bow tip hits differ by 0.56 dB in level and 0.07 dB in a typical band, and repeated hits sound like copies. On a real cymbal most of the difference between hits comes from where the stick lands, which the engine models through the strike position. **HIT VARIATION** (the HIT VARIATION group, below CYMBAL; not host parameters; approved by Kris, 2026-10-31, who chose the amounts by ear) moves each strike's position and velocity a little: with the defaults (position ±0.04, velocity ±3 %, version B of three RAVEN renders) the eight hits differ by 1.2 dB in level and 0.33 dB in a typical band.

It is one cymbal-wide setting and applies to every articulation of every preset (bell, bow, edge, center and the shoulder copies) and to every row of the editor. When it is on, each strike draws two numbers, before anything else in the strike:

- **Position**: p′ = p + U(−s, s), with p the strike's position after the BLEND POSITION knobs and s the POSITION SPREAD.
  - **Mirrored at the ends**: below 0, p′ = −p′; above 1, p′ = 2 − p′. The EDGE rows (0.0) and bell rows (1.0) vary inward instead of sticking at the end.
  - **Never crosses 0.45**: a strike at p ≥ 0.45 stays at 0.45 or above, one below 0.45 stays below it (at most the double just under 0.45). So it stays on the same side of the bow ↔ edge switch, the edge fraction's 0.1 (`docs/PORT_MAP.md`, live-engine decision 22): a bow hit never becomes an NPZ or envelope edge hit, nor the reverse.
  - p′ replaces p wherever the strike uses its position: the position weights, the edge and bell fractions, the NPZ coupling and band envelope, the onset ramp, the stick click weights and the contact.
- **Velocity**: v′ = v · (1 + U(−d, d)), with v the engine's velocity (after the Velocity Sensitivity knob) and d the VELOCITY SPREAD, at most 1. v′ replaces v wherever the strike uses its velocity: its level, the velocity tables, the NPZ velocity, the envelopes, the onset layers, the stick click and the velocity curve.

The two numbers come from a random sequence of their own, not the engine's: the mode phases and the stick click's noise draw what they drew before. It is seeded from the bake's strike-phase seed and a constant, so a preset plays the same variation each time it is loaded, and offline renders repeat; presets whose bakes share a seed (SWISH and NOVA RIDE; STINE FLAT RIDE, RAVEN, KONSTAN CRASH 18", EXTRA PAPER THIN 24" and CRASH 16") get the same sequence. Nothing is allocated or locked on the audio thread. A change of the switch or a spread applies from the next strike. The hit display shows the row's position and velocity before the variation; the LAST HIT meter gives the velocity before the variation, with the level the varied strike played.

Two consequences of the rules, measured in memory (2026-10-31; trims unchanged):

- **The bell rows get louder on average.** A strike at an end can only move inward, so the bell rows (1.0) take in some of the bow's modes. Over 48 strikes at velocity 1.0 (NOVA 0.9), each against the same strike without the variation: KONSTAN's BELL and BELL_SHOULDER +3.9 dB (single strikes +0.1 to +6.9 dB; its bell is trimmed +26 dB), NOVA's BELL2 at BLEND POSITION 1 = 0 +1.8 dB and BELL +1.0 dB, RAVEN's BELL and BELL_SHOULDER +0.75 dB, HAMMERED's BELL +0.4 dB. Every other row moves by 0.3 dB or less (BOW −0.1 to −0.3 dB, partly because a velocity of 1.0 can only go down).
- **The stick click can come in.** Where an articulation's click weight is 0 at its position, a varied strike can get a quiet click: on SWISH, BOW2 at BLEND POSITION 2 = 100 % strikes the bow articulation (bow weight 1, edge weight 0) at 0.0, and moved inward to at most 0.04 it clicks with a weight of at most 0.08. That click's noise comes from the engine's random sequence, so every later strike's phases are drawn further along it than without the variation.

With HIT VARIATION off, every preset plays bit for bit as it did before it existed.

#### Stereo

How stereo works: every mode has a fixed left and right gain from the bake (the pick vectors `pick_l`, `pick_r`, random from the preset's seed, signed), and the right channel has a 120 Hz high-pass. The STEREO values change both.

- **Modes and bands.** Modes belong to the 22 bands by their frequency, as for the band gains.
- **A mode's baked gains** are a level, sqrt(L² + R²), and a position from −1 (hard left) to +1 (hard right): atan2(|R|, |L|) / (π/4) − 1. The baked positions lie within about L31 to R30.
- **Its new position** is the band's pan + its baked position × the band's width × the overall width, clamped to hard left / hard right.
- **Its new gains** are level × cos and level × sin of (new position + 1) × π/4 (constant power: the level stays), each with the sign of its baked gain.
- **Width 0 %** puts every mode of the band at the pan point; **100 %** at centre pan gives the baked gains exactly (the engine uses them as they are); **200 %** doubles the spread.
- The width scales each mode's whole baked position, the band's own mean offset with it (a band's mean baked position is near centre but not at it: mostly within L5 to R5, up to 9, and L24 for a band of Hammered Thin Ride that has one mode). So at centre and 100 % nothing changes, at 0 % every mode is at the pan point, and the spread around the band's mean doubles at 200 %.

**Live.** When a value changes, the TUNING page computes every mode's gains (off the audio thread) and sends them with the other values. The engine takes them at the next control point of the free phase and moves every ringing mode's gains to them linearly over that control block (about 3 ms), so nothing steps. A change that arrives while a strike's contact is running (a few ms) waits for its end. If a strike lands inside such a ramp, the gains stay where the ramp got to and the rest of the move follows the contact. While nothing rings, new gains apply at once. Outside a ramp block the engine renders exactly as before: no per-sample cost.

**The right-channel high-pass** can be switched off and moved from 20 to 300 Hz. A change moves the filter's coefficients linearly over 20 ms (a straight line between two stable 2nd-order high-passes stays stable, with its zeros at DC). Switching it on or off crossfades between the filtered and the unfiltered right channel over the same 20 ms. On at 120 Hz, the default, it is the filter as before.

In the panel it is the **STEREO** group of WHOLE CYMBAL:
- a **PAN** row of 22 vertical sliders, L100 to R100. A value is typed as L30, R30, C or a signed number (− is left). It has a per-band reset and **CENTRE** (every band to the centre);
- a **WIDTH** row of 22 sliders, 0 to 200 %, with a per-band reset and **FLAT** (every band to 100 %);
- the **Overall width**;
- the **Right-channel high-pass** switch and its **frequency**.

**In the bake.** `tools/apply_tuning.py` stores the stereo values in the bake's tuning block. It also writes every mode's resulting left and right gains into a `stereo` block (`gain_l`, `gain_r`, checksummed like the other arrays). The plugin plays them at load as they are; a lean build (`CYMBALIS_WITH_TUNING` OFF) too. `pick_l` and `pick_r` stay as baked: the loader still regenerates them from the seed, and it checks that each stored gain pair keeps the level and signs of its pick vector entries. The high-pass settings go into the tuning block.

#### Edge voice from bow

On the presets with NPZ edge data, an edge strike's per-mode levels are the NPZ velw table, made by matching the recorded edge partials to the preset's modes. Those partials are dense and drift, so they land on other modes than the bow's. The edge's onset and band levels are right (Kris), but its tail rings on a different set of modes, so it sounds like a different cymbal. Of the 20 longest-ringing modes (coupling × √T60), the edge shares with the bow 8 on HAMMERED THIN RIDE (the closest by ear), 3 on KONSTAN (2 since its T60 refit), 2 on STINE and 0 on SWISH.

**EDGE VOICE FROM BOW** (`edge_voice_from_bow`, 0–100 %, in the EDGE ENVELOPE group below EDGE ONSET ENVELOPE; not a host parameter; approved by Kris, 2026-10-29) gives the long-ringing modes of each envelope band the bow's pattern and leaves the short-ringing ones the edge's, so the crash and the onset stay as recorded. Each band keeps its level at every NPZ velocity and the envelopes are untouched, so the velocity behaviour does not change. With a = value / 100:

1. **Weights per mode**, once when the preset loads (`edgeVoiceAlpha`, `plugin/dsp/Tuning.cpp`). For each envelope band, over its modes with `exc_norm` > 0 (at least 2): med = the median of their baked `t60_s` (of an even count, the mean of the middle two), and α = clamp((log2(T60 / med) + 1) / 2, 0, 1). A mode ringing half as long as the median or less has α = 0, twice as long or more α = 1. Modes with `exc_norm` 0, and bands with fewer than 2 such modes, have α = 0. The baked T60 is used, so the weights don't move with the ring length.
2. **Per NPZ velocity row and band** (`edgeVoiceVelw`): with ne = the norm of the row's velw over the band's modes and ns = that of their `exc_norm` (a band where either is 0 is left alone), each mode's new value is (1 − a·α) · velw / ne + a·α · exc_norm / ns, and the band is scaled back to ne.
3. **The engine** uses that table for the edge coupling from the next strike on; strikes already ringing keep their coupling. It is computed off the audio thread (when the preset loads, and by the TUNING page when the value changes) and handed over with the other values.

It acts wherever the NPZ coupling does: every strike with an edge fraction above 0.1, so also a bow or bell row blended towards the edge. At 0 % the table is the bake's, and every preset plays bit for bit as without the value. KONSTAN CRASH 18" starts at 100 % (its tuning block), with EDGE re-trimmed to its SPEAR target at 100 %; STINE FLAT RIDE, SWISH, HAMMERED THIN RIDE, RAVEN, EXTRA PAPER THIN 24" and CRASH 16" start at 0 %. SMOKE FLAT RIDE (in the plugin until STINE FLAT RIDE took its place, 2026-11-23) was tried at 100 % and set back to 0 % by Kris by ear (2026-10-31): he prefers its edge as it was.

The build checks these values at 100 % (`tools/pack_bake`), from the shipped bakes; the last column ranks the bow by `exc_norm` × √`t60_s` and the edge by the top velocity row of the new table × √`t60_s`:

| Preset | Modes with α = 1 | Modes with 0 < α < 1 | Of the 20 longest-ringing edge modes, shared with the bow |
|---|---|---|---|
| STINE FLAT RIDE | 52 | 590 | 8 (2 at 0 %) |
| SWISH | 226 | 378 | 6 (0 at 0 %) |
| KONSTAN CRASH 18" | 42 | 592 | 10 (2 at 0 %) |

No band has α = 1 for most of its modes: the most is 42 % (SWISH's 898–1131 Hz band, 17 of 40 modes).

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

With amount 0 the cascade is skipped entirely: nothing is measured or computed, and every preset plays exactly as before. In the panel it is the **ENERGY CASCADE** group of the whole-cymbal part.

Limits:
- Energy goes only to modes that are ringing. Silent modes (below the skip threshold, 140 dB under a full hit since 2026-11-25; `docs/PORT_MAP.md`, "One silence rule") receive none, and a band with no ringing mode takes no energy.
- The moved energy is held by one strike component of the engine's pool of 24. When the pool holds 24 strikes and none of them has settled (dense playing, every strike still in its onset), nothing moves for that control point.
- During a strike's contact (NOVA RIDE only, about 0.3 ms) nothing moves.

#### Loud-band damping

While a band rings above its threshold, its modes decay faster. It is a plugin mechanism (`docs/PORT_MAP.md`, plugin mechanism 11) and acts on the whole cymbal.

At every control point the band energies are measured as for the energy cascade (every strike together, before the LF and MF build-up scales). For each band:

- **How far above** the threshold it is: 0 at the threshold, 1 at 20 dB above it and beyond (capped), linear in dB in between. It is smoothed over 20 ms, so the damping comes and goes without steps.
- **Tilt** weights the bands: 1 − tilt for the lowest band, 1 + tilt for the highest, linear in between. At −1 the low bands get up to twice the effect and the highest none; at +1 the reverse.
- **The decay rate** of the band's modes becomes rate × (1 + amount × tilt weight × how far above). The rate is the mode's own, from its T60. At amount 4, a band 20 dB or more above its threshold (tilt 0) decays five times as fast.

The extra decay acts on every strike ringing in the band alike. It is applied smoothly across each control block, with no step. With amount 0 it is skipped entirely. In the panel it is the **LOUD-BAND DAMPING** group of the whole-cymbal part.

When the energy cascade is also on, the damping acts first in each control block, and the cascade moves part of what is left.

#### Re-hit damping

A new strike first damps what is already ringing, then adds its own energy. It is a plugin mechanism (`docs/PORT_MAP.md`, plugin mechanism 12) and acts on the whole cymbal.

- **How much:** at a strike, everything already ringing is scaled down to 1 − amount × velocity × the struck articulation's weight (never below 0). The velocity is the engine's: after the Velocity Sensitivity knob. For example, at amount 0.5, a velocity-0.8 BOW strike with weight 1 leaves 60 % of the ringing amplitude.
- **How fast:** the scaling runs over **Time** (1–50 ms) as a smooth S-curve, from the moment the strike's contact ends (NOVA RIDE: about 0.3 ms; the other presets have no contact phase). It is evaluated at the control points (about 2.8 ms apart) and interpolated in between. So a Time shorter than one control block still takes about one block, smoothly, with no click.
- **What it acts on:** only the energy that was already ringing, every strike of it. The new strike's own energy is not scaled.
- **Weights:** each articulation of the preset has its own weight (0–2, default 1). A weight of 0 means that articulation's strikes do not damp; 2 doubles the effect.
- **Strikes in quick succession:** a strike that lands while an earlier damping is still running carries what is left of it into its own: the earlier damping completes within the new strike's Time.

With amount 0 nothing is computed and every preset plays exactly as before (a damping already running when the amount is set to 0 completes). In the panel it is the **RE-HIT DAMPING** group of the whole-cymbal part, with one weight row per articulation.

### Sizzle chain

Added 2026-11-26 (Kris; `docs/PORT_MAP.md`, "Sizzle chain"). A bead chain resting on the ride, on STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN and EXTRA PAPER THIN 24" only (their bakes have a `chain` block). In the panel it is the **SIZZLE CHAIN** group at the top, above ARTICULATIONS, on those five presets only; its header, drawn like the part headers, reads "STINE, HAMMERED THIN, SWISH, RAVEN and EXTRA PAPER THIN only" and has **CHAIN** with an **OFF | ON** pair of boxes at its right (`docs/UI_SPEC.md` section 6).

- **The switch** (CHAIN OFF | ON) is session state, not a tuning value and not a host parameter (no automation): it acts at once (the sizzle fades in or out over 50 ms; the damping applies from the next strike), each preset keeps its own, and it is saved with the session as `chain_<preset id>` (1 or 0; a session without it: off). No tuning file holds it, RESET ALL leaves it, and **MODIFIED** does not count it. Off, the cymbal plays bit for bit as without the chain.
  - **What stays after switching off:** the strikes made while it was on ring out with their damping (at most their own ring), and the sizzle fades over 50 ms, so a strike within those 50 ms still gets some of it. Nothing else, apart from one effect far under the playing: in dense playing, while all 24 of the engine's strike slots are full, merging two ringing strikes can pass a little of the damping of a strike made while on to one made after it (`docs/PORT_MAP.md`, "Sizzle chain", "Off is off"). Checked on every pull request (`cymbalis_chain_off`).
- **The eight values** are tuning values (the bake's tuning block, tuning files, the session's tuning, `tools/apply_tuning.py`), acting from the next block; the same defaults on all five presets:

| Key | Shown as | Default | Range |
|---|---|---|---|
| `chain_level_db` | SIZZLE LEVEL | 0.0 dB | −20 to +12 dB |
| `chain_edge_db` | EDGE DRIVE | +6.0 dB | −30 to +12 dB |
| `chain_bow_db` | BOW DRIVE | 0.0 dB | −30 to +12 dB |
| `chain_bell_db` | BELL DRIVE | −10.0 dB | −30 to +12 dB |
| `chain_velocity` | VELOCITY EFFECT (0 = none) | 1.00 | 0 to 2 |
| `chain_stop_db` | STOPS AT (below a full hit) | −28 dB | −50 to −10 dB |
| `chain_swing` | SWING (with the cymbal's motion) | 1.20 | 0 to 3 |
| `chain_damping` | DAMPING (mostly high frequencies) | 0.35 | 0 to 1 |

  - **SIZZLE LEVEL** adds to every band of the sizzle.
  - **EDGE, BOW and BELL DRIVE** weigh a strike's drive by where it lands: at positions 0.0, 0.5 and 1.0, linear in dB between them (so the blend knobs fall in between; STINE's CENTER rows are at 1.0).
  - **VELOCITY EFFECT:** the drive × (velocity / 0.9) to this power.
  - **STOPS AT:** the gate closes over the 6 dB under this, in dB below a full hit's drive 0.5 s after it (BOW at 0.9 stops about 4.2 s after the hit on HAMMERED THIN RIDE and 6.9 s on STINE FLAT RIDE).
  - **SWING:** how much the sizzle follows the plate's motion (0: a steady sizzle). It changes the flicker, not the level: the swing is scaled to keep the sizzle's power (`docs/PORT_MAP.md`, "Sizzle chain").
  - **DAMPING:** with the chain on, every mode's t60 / (1 + this × (f / 4 kHz)^1.5), from the next strike.

  The values live in one small struct with its own keys (`plugin/dsp/ChainSettings.h`), apart from the TUNING page's code, so that they can move as they are to the combined kit plugin.

### Meters

- **Output, after the Volume and Pan knobs:** the peak (held for 1.5 s) and the RMS over about 300 ms, in dBFS. **CLIP** lights when a sample reaches 0 dBFS; click it to reset it.
- **Last hit, before the Volume knob:**
  - the row and articulation it played, and its velocity;
  - its peak, and the RMS of the first 500 ms after the strike, both channels.

  The 500 ms include whatever else is ringing. "(measuring)" shows until the 500 ms are over.

## Tuning files

**SAVE...** writes the current preset's tuning to a file (default name `<preset>.tuning.json`). **LOAD...** reads one. A file for another preset is refused with a message. So is a file for another bake format, an unknown value, band gains that are not 22 numbers from −24 to +12 dB, or a ring length, edge capture curve, wander rate or `tau_ref_ms` that is not above 0, an `edge_voice_from_bow` outside 0–100 %, a stick click weight outside 0–1, a `hit_position_spread` outside 0–0.15, a `hit_velocity_spread_pct` outside 0–15 %, or a `hh_tip_open_level` outside 0–100 %, `hh_tip_open_tick_db` outside 0–12 dB, `hh_ring_floor` outside 0–200 %, `hh_tip_modes_db` outside 0–20 dB, `hh_max_open` outside 75–100 %, `hh_splash_window_ms` outside 50–400 ms or `hh_splash_reopen` outside 5–80 %, a `chain_` value outside its range (above) or on a preset without a sizzle chain. Cymbalis also saves each preset's tuning (if it differs from the bake) in its session state, and restores it with the session. A session without tuning, such as one saved by Cymbalis before it had the Tuning window, loads with every preset at its bake's values.

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
- `edge_voice_from_bow` is always written for the presets with NPZ edge data, even when it is the bake's. So a file without it is from before EDGE VOICE FROM BOW and was tuned without it: it loads as 0 %, on the TUNING page, from a session and in `tools/apply_tuning.py`.
- `hit_variation` (true or false), `hit_position_spread` and `hit_velocity_spread_pct` are written when they differ from the bake. A file without them, such as one from before HIT VARIATION, keeps the bake's values, on the TUNING page, from a session and in `tools/apply_tuning.py`.
- `hh_tip_open_level`, `hh_tip_open_tick_db`, `hh_ring_floor`, `hh_free_ring` (true or false), `hh_tip_modes_db`, `hh_max_open`, `hh_splash_window_ms` and `hh_splash_reopen` (HI-HAT only) are written when they differ from the bake. A file without them, such as one from before them, keeps the bake's values (60 %, +4 dB, 100 %, off, 0 dB, 100 %, 150 ms, 25 %), on the TUNING page, from a session and in `tools/apply_tuning.py`, which refuses them for any other preset.
- `chain_level_db`, `chain_edge_db`, `chain_bow_db`, `chain_bell_db`, `chain_velocity`, `chain_stop_db`, `chain_swing` and `chain_damping` (the five presets with a sizzle chain only) are written when they differ from the bake. A file without them keeps the bake's values (0 dB, +6 dB, 0 dB, −10 dB, 1.0, −28 dB, 1.2, 0.35), on the TUNING page, from a session and in `tools/apply_tuning.py`, which refuses them for any other preset. The chain's switch is not in tuning files (session state).
- `effective` is a full copy of every value for reference; nothing reads it.
- `entry` names the bake entry (slot) of an articulation.
- `preset` is the preset id (`nova_ride`, `stine_flat_ride`, `hammered_thin_ride`, `swish`, `raven`, `konstan`); `bake_format_version` is the bake's `format_version`.

## The round trip

1. Tune on the TUNING page, **SAVE...** the file, and bring it to the project session.
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
   - **TIP OPEN LEVEL**, **TIP OPEN TICK**, **RING FLOOR**, **FREE RING**, **TIP MODES**, **MAX OPEN**, **SPLASH WINDOW** and **SPLASH REOPEN** go into the tuning block like the other cymbal values (0 %, 0 dB, 100 %, off, 0 dB, 100 %, 150 ms and 25 %, the values of a bake without them, drop out). They are refused for any bake but the hi-hat's, whose tuning block starts at 60 %, +4 dB, 100 %, off, 0 dB, 100 %, 150 ms and 25 % (`tools/bake_hihat.py`).
   - The **sizzle chain's** eight values go into the tuning block like the other cymbal values (their defaults, the values of a bake without them, drop out). They are refused for a bake without a `chain` block.
   - A value equal to the bake's own drops out of the block, so applying the same file twice gives the same bake. Nothing else in the bake changes.
3. Rebuild the plugin. Its bake loader (`plugin/dsp/Bake.cpp`, `Tuning.cpp`) applies the block (`applyTuning`):
   - the parameters;
   - every mode's T60 times `ring_length` and its band's `ring_length_bands` factor;
   - the settings of the energy cascade, the loud-band damping and the re-hit damping (with its weights by articulation id), which the engine reads when it is built;
   - the LFO rates scaled to `wander_rate_hz`;
   - the LF and MF weights from `lf_cutoff_hz`, `mf_lo_hz` and `mf_hi_hz`;
   - the NPZ settings, the output gain and the velocity curves.

   The loader also reads each slot's band gains and turns them into one gain per mode for that articulation (`excitationModeGains`). The engine multiplies each strike's coupling by it (`docs/PORT_MAP.md`, live-engine decision 7). It reads each slot's onset shift, and the engine applies it to that articulation's band envelopes and onset peak times at each strike (decision 8). It reads each slot's attack band gains (from its attack data, else from its onset gains), which the engine applies at each strike (decision 15), and the brightness from the tuning block.

   The TUNING page applies the values live with the same functions, so the tuned bake plays what was heard while tuning.

Re-baking a preset (`tools/bake_preset.py`) drops its trims, its band gains, its onset shifts, its attack data and its tuning block. `Engine`, the retired Python-matching path, refuses a bake with a tuning block, band gains, onset shifts or attack data.
