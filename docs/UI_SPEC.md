# UI specification — Cymbalis (Nova Ride)

Approved by Kris, 2026-10-13. All coordinates are in 1536×1024 design units. `docs/ui/reference_mockup.png` shows the original artwork.

## 1. Assets (`resources/ui/`)

| File | Use |
|---|---|
| `CYMBALIS_Background_logo.png` (1536×1024) | Background. The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of it; **do not draw them**. Everything else on this page is drawn by JUCE. |
| `Nova.png` (707×713, transparent) | Nova Ride photo |
| `knob.png` (128×11648) | Knob filmstrip: 91 frames of 128×128, about 7 o'clock to 5 o'clock, frame 45 at 12 o'clock |
| `calibril.ttf` | Calibri Light, used for all drawn text. If it is missing, use Carlito Regular (SIL OFL, include its licence file) until Kris adds it |

## 2. Window

- Resizable, aspect ratio fixed at 3:2. **Maximum 1536×1024 — never larger.** Minimum 960×640.
- Opens at the largest size that fits the screen, up to the maximum. Size is saved with the session.
- If a host gives the editor a size that is not 3:2, the whole design is scaled to fit and centred, so no control is cut off.
- Paint everything in design units and scale with one transform.

## 3. Layout


| Element | Position | Notes |
|---|---|---|
| Cymbal photo | top-left 434,94, size 707×713 (centre 787,450) | Changes per preset |
| CYMBAL preset dropdown | box 61,371, 277×62 | Shows the preset name, e.g. **NOVA RIDE**. Changing preset changes the name and the photo |
| MIDI CHANNEL dropdown | box 632,936, 277×62 | Channels 1–16 |
| Velocity Sensitivity knob | centre 201,641 | See section 9 |
| Volume knob | centre 199,881 | See section 9 |
| Blend Position 1 knob (BELL ↔ BOW) | centre 1349,642 | |
| Blend Position 2 knob (BOW ↔ EDGE) | centre 1350,881 | |
| Articulation rows | box tops at y 140 / 220 / 300 / 380 / 460 (BELL, BOW, EDGE, BELL2, BOW2) | Each row has a stick box and a MIDI note box |
| Stick box (under STICK ARTICULATION) | x1265, 99×25, 2 px border `#463C30` | Shows **TIP** or **SHOULDER**; selectable where the preset has both (section 6) |
| MIDI note box (under MIDI NOTE) | x1424, 66×25, 2 px border `#463C30` | Note name + number, e.g. `C3 60` |
| Preset and channel boxes | 2 px border `#534739` | Dropdown arrow: 13×9 triangle `#999489` at 306,398 and 877,963 |
| Divider lines (1 px, `#353231`) | horizontal 338→1161 at y869; short 39→58 at y869; vertical x1188, y110→850 | |
| −/+ marks (1 px, `#695E52`) | Vel: − at 120,722 (11 px); + centred 274,723 (10×9). Vol: − at 118,962; + centred 272,963 | |

The artwork had the BELL note box at y138 and the BELL2/BOW2 stick boxes at x1266. These are aligned to y140 and x1265 in the drawn version.

All dropdowns, pop-up menus and value text are drawn by JUCE (custom LookAndFeel) in the palette below.

## 4. Drawn labels (Calibri Light)
(Calibri Light, design units)

Measured from Kris's original artwork. "Top-left" is the top-left corner of the ink. Sizes and spacing were derived with Carlito (same metrics as Calibri) and should be checked once in JUCE against the original artwork.

The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of the background image and are not drawn.

| Text | Ink top-left | Font size (px) | Letter spacing (px) | Colour |
|---|---|---|---|---|
| CYMBAL | 160, 348 | 17.1 | 3.45 | `#A7A7A4` |
| VELOCITY SENSITIVITY | 99, 544 | 17.1 | 3.09 | `#A09C99` |
| VOLUME | 164, 784 | 17.1 | 3.40 | `#A09C99` |
| MIDI CHANNEL | 704, 913 | 17.1 | 3.11 | `#A7A7A4` |
| STICK ARTICULATION | 1201, 98 | 17.1 | 3.29 | `#A7A7A4` |
| MIDI NOTE | 1424, 98 | 17.1 | 3.00 | `#A7A7A4` |
| BELL / BOW / EDGE / BELL2 / BOW2 (rows) | x1201; y 148 / 228 / 307 / 387 / 468 | 15.6 | 3.33 / 2.88 / 2.83 / 3.50 / 3.25 | `#695E52` |
| BLEND POSITION 1 | 1269, 545 | 17.1 | 3.18 | `#A7A7A4` |
| BELL · BOW (under Blend 1) | 1249, 720 · 1409, 720 | 15.6 | 3.33 · 2.88 | `#695E52` |
| BLEND POSITION 2 | 1270, 783 | 17.1 | 2.98 | `#A7A7A4` |
| BOW · EDGE (under Blend 2) | 1249, 958 · 1409, 958 | 15.6 | 2.88 · 2.83 | `#695E52` |

Values drawn inside boxes use the same font in `#A7A7A4`; secondary values (note number) use `#695E52`.

## 5. Palette
(sampled from the background artwork)

| Use | Colour |
|---|---|
| Background | `#0F0E0F` |
| Main labels, headers, values | `#A7A7A4` (labels `#A8A8A5`) |
| Knob labels (Velocity Sensitivity, Volume) | `#A09C99` |
| Row labels, knob end labels, secondary text | `#695E52` / `#62574C` / `#60554A` |
| Bronze accent | `#76624C` |
| Box outlines | `#534739` (preset and channel boxes), `#463C30` (row boxes) |
| Dropdown arrow | `#999489` |
| Divider lines | `#353231` |
| Title gradient | `#665E53` → `#A5A299` |
| "BRONZE" subtitle gradient | `#4C433B` → `#76624C` |
| Subtitle "MODELING INSTRUMENT" | `#5C5F5F` |

Hover, open and selected states use only colours from this table.

## 6. Interactions
- **Knobs:** vertical drag; double-click resets to the default.
- **MIDI note box:** vertical drag changes the note. A click opens a menu: MIDI LEARN, RESET DEFAULT, NONE (unassign) and a scrollable list of every note, C-2 (0) to G8 (127), each with its number; the current note is outlined and in view. No separate Learn button is added.
- **Stick box:** where the preset has both stick versions, a click opens a TIP / SHOULDER popup. For Nova Ride, BELL and BELL2 choose TIP (`bell_tip`) or SHOULDER (`bell_shoulder`); BOW and BOW2 choose TIP (`bow_tip`) or SHOULDER (`bow_shoulder`). EDGE has no choice and shows a dash. The choice is saved with the session; it is not an automatable parameter.
- **Dropdowns:** the preset dropdown is a list; the channel dropdown is a 4×4 grid.
- Knob value readout while dragging: Velocity "100%", Volume "+0.0 dB", Blend 1 "BELL 100 · 0 BOW", Blend 2 "BOW 100 · 0 EDGE".
- MIDI LEARN: the next note-on on the selected channel is assigned; the box pulses "LEARN" until then; clicking elsewhere cancels.
- Popups: background `#141313`, 2 px border `#534739`; hover outline `#76624C`; open outline `#A5A299`.

## 7. Nova Ride defaults
| Setting | Default |
|---|---|
| MIDI channel | 16 |
| BELL | SHOULDER, C3 (60) |
| BOW | TIP, D3 (62) |
| EDGE | Unassigned |
| BELL2 | TIP, C#3 (61) |
| BOW2 | SHOULDER, D#3 (63) |
| Blend Position 1 | Full BELL |
| Blend Position 2 | Full BOW |

Note naming uses the Logic/Ableton convention (60 = C3). The note number is always shown alongside the name.

**Blend knobs have no MIDI notes of their own.** They change the character of the existing slots:
- Blend 1 moves the BELL notes from full bell toward a bell/bow mix.
- Blend 2 moves the BOW notes from full bow toward a bow/edge mix.

Blend 2 inherits the engine's hard bow-to-edge switch near position 0.45 (PORT_MAP, engine as-is).

## 8. Slots

| Slot | Default overlay (stick) | Other stick choice | Base position | Blend | Default note |
|---|---|---|---|---|---|
| BELL | `bell_shoulder.json` (SHOULDER) | `bell_tip.json` (TIP) | 1.0 | Blend 1: 1.0 → 0.5 | C3 (60) |
| BOW | `bow_tip.json` (TIP) | `bow_shoulder.json` (SHOULDER) | 0.5 | Blend 2: 0.5 → 0.0 | D3 (62) |
| EDGE | none (base preset) | — | 0.0 | — | unassigned |
| BELL2 | `bell_tip.json` (TIP) | `bell_shoulder.json` (SHOULDER) | 1.0 | Blend 1: 1.0 → 0.5 | C#3 (61) |
| BOW2 | `bow_shoulder.json` (SHOULDER) | `bow_tip.json` (TIP) | 0.5 | Blend 2: 0.5 → 0.0 | D#3 (63) |

The baked preset holds all four overlays and the no-overlay edge data, so any bell or bow row can play either stick.

## 9. Parameters — exactly these four, no others
Placeholder ranges until Kris revises them. At the defaults the plugin sounds the same as the Python engine.

| Parameter ID | Knob | Range | Default | Behaviour | Readout |
|---|---|---|---|---|---|
| `velocity_sensitivity` | Velocity Sensitivity | 0–1 (shown 0–100%) | **1.0 (100%)** | Engine velocity = vf + s·(v_in − vf), where v_in = MIDI velocity / 127, vf = 100/127 and s is this parameter. At 100% this is the engine's own velocity response; at 0% every hit plays at velocity 100 | `100%` |
| `volume_db` | Volume | −60 to +6 dB | **0 dB** | Output gain. Knob curve dB = 6 − 66·(1 − p)², p = knob position 0–1, so 0 dB sits at p ≈ 0.70. Smoothed (no zipper noise) | `+0.0 dB` |
| `blend_1` | Blend Position 1 (BELL → BOW) | 0–1 (shown 0–100%) | **0 (full BELL)** | BELL and BELL2 strike at position 1.0 − 0.5·blend_1 (1.0 = bell, 0.5 = bow). Read at strike time | `BELL 100 · 0 BOW` |
| `blend_2` | Blend Position 2 (BOW → EDGE) | 0–1 (shown 0–100%) | **0 (full BOW)** | BOW and BOW2 strike at position 0.5 − 0.5·blend_2 (0.5 = bow, 0.0 = edge). Read at strike time. The engine's bow-to-edge excitation switches abruptly near position 0.45 (engine as-is) | `BOW 100 · 0 EDGE` |

EDGE always strikes at position 0.0. All four are automatable. No other parameters.

Non-automatable state saved with the session: MIDI channel, note and stick per slot, current preset, editor size.
