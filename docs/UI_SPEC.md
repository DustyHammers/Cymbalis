# UI specification — Cymbalis

Approved by Kris, 2026-10-13; presets SMOKE FLAT RIDE and HAMMERED THIN RIDE added 2026-10-15, SWISH 2026-10-17. Layout v2 (mockup v2: a larger, higher photo area, MIDI CHANNEL under CYMBAL, Velocity Sensitivity and Volume in a bottom row) approved by Kris 2026-10-25; the Pan parameter and its knob added 2026-10-25 at Kris's request; the hit display (section 10) approved by Kris 2026-10-25. Layout v3 (mockup v3: the MIDI CHANNEL box and the TUNING button swapped) approved by Kris 2026-10-25. The wireframe cymbal (section 11), replacing the photos and the 2D hit marks, approved by Kris 2026-10-27. All coordinates are in 1536×1024 design units. `docs/ui/reference_mockup.png` shows the original artwork (layout v1).

## 1. Assets (`resources/ui/`)

| File | Use |
|---|---|
| `CYMBALIS_Background_logo.png` (1536×1024) | Background. The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of it; **do not draw them**. Everything else on this page is drawn by JUCE. |
| `Nova.png` (707×713, transparent) | NOVA RIDE photo: reference only |
| `SmokeFlatRide.png` (714×718) | SMOKE FLAT RIDE photo (602): reference only |
| `HammeredThinRide.png` (709×713) | HAMMERED THIN RIDE photo (Bettis): reference only |
| `Swish.png` (737×747) | SWISH photo: reference only |
| `Raven.png` (712×712, transparent) | RAVEN photo, a top-down view made from Kris's angled photo (`docs/BASELINE_MANIFEST.md`, Raven): reference only |
| `knob.png` (128×11648) | Knob filmstrip: 91 frames of 128×128, about 7 o'clock to 5 o'clock, frame 45 at 12 o'clock |
| `calibril.ttf` | Calibri Light, used for all drawn text. If it is missing, use Carlito Regular (SIL OFL, include its licence file) until Kris adds it |

Since 2026-10-27 the five photos are references only: they stay in `resources/ui/` (their MD5s in `docs/BASELINE_MANIFEST.md` are unchanged) but are not compiled into the plugin. The editor draws each preset's cymbal as a wireframe instead (section 11).

## 2. Window

- Resizable, aspect ratio fixed at 3:2. **Maximum 1536×1024 — never larger.** Minimum 960×640.
- Opens at the largest size that fits the screen, up to the maximum. Size is saved with the session.
- If a host gives the editor a size that is not 3:2, the whole design is scaled to fit and centred, so no control is cut off.
- Paint everything in design units and scale with one transform.

## 3. Layout

Layout v3, approved by Kris 2026-10-25 (mockup v3): layout v2 with the MIDI CHANNEL box and the TUNING button swapped. Left column: the CYMBAL box and, under it, the TUNING button. Top right: the MIDI CHANNEL box. Centre: the cymbal (the photo until 2026-10-27; now the wireframe cymbal, section 11). Bottom row: Velocity Sensitivity, Volume and Pan. Right column: the rows and the blend knobs, as in layout v1.

| Element | Position | Notes |
|---|---|---|
| Cymbal area | 400,40 to 1180,750 (780×710), inside the right column's divider (x 1188) and above the bottom row's (y 757) | The wireframe cymbal of the preset (section 11), drawn over the background and under every control; it replaces the photo. The photo was at 447,50, 680×680 (centre 787,390), each photo fitted into it with its aspect ratio kept; layout v1: 434,94, 707×713 |
| CYMBAL preset dropdown | box 61,371, 277×62 | Shows the preset name, e.g. **NOVA RIDE**. Changing preset changes the name, the cymbal and the sound. A name too wide to sit centred clear of the arrow is centred between the box's left edge and the arrow, with tighter letter spacing |
| MIDI CHANNEL dropdown | box 1404,30, 110×36 (centred on x 1459), top right, above the column headers | Channels 1–16, in a 4×4 grid that opens below the box (box bottom + 3), right-aligned with its right edge, so it stays inside the window. The channel number is left-aligned, its ink 12 units in from the box's left edge (where a long preset name starts), in the preset name's font, size and colour. Its label is on the same line, to the left (section 4). Layout v2: 61,501, 277×62 under the CYMBAL box, grid above it; layout v1: 632,936 |
| TUNING button | box 61,501, 277×62 (the CYMBAL box's size), under the CYMBAL box | Tuning code only (`CYMBALIS_WITH_TUNING`, `docs/TUNING.md`); in a lean build this place is empty. Outlined like the preset and channel boxes (section 5). **TUNING** centred, drawn like the preset name; no arrow and no label above it. Its Tuning window still opens where it did in layout v2: below 1404,30, 110×36 (the button's layout v2 place, now the MIDI CHANNEL box), right-aligned with it. Layout v2: 1404,30, 110×36 |
| Bottom row | x 39–1161 in thirds (39–413, 413–787, 787–1161), knob centres at y 881 (the height of Blend Position 2) | Each knob is centred in its third |
| Velocity Sensitivity knob | centre 226,881 (first third) | See section 9. Layout v1: 201,641 |
| Volume knob | centre 600,881 (second third) | See section 9. Layout v1: 199,881 |
| Pan knob | centre 974,881 (third third) | See section 9. Frame 45 (12 o'clock) at centre, 0 at full left, 90 at full right. End labels L and R (section 4) instead of −/+ marks |
| Hit display | on the wireframe cymbal, under every control | Each strike moves the cymbal and flashes where it struck, and the row that played it lights (sections 10 and 11) |
| Blend Position 1 knob (BELL ↔ BOW) | centre 1349,642 | |
| Blend Position 2 knob (BOW ↔ EDGE) | centre 1350,881 | |
| Articulation rows | box tops at y 140 / 220 / 300 / 380 / 460 (BELL, BOW, EDGE, BELL2, BOW2) | Each row has a stick box and a MIDI note box |
| Stick box (under STICK ARTICULATION) | x1265, 99×25, 2 px border `#463C30` | Shows **TIP** or **SHOULDER** (section 8); selectable where the preset has both (section 6) |
| MIDI note box (under MIDI NOTE) | x1424, 66×25, 2 px border `#463C30` | Note name + number, e.g. `C3 60` |
| Preset and channel boxes, TUNING button | 2 px border `#534739` | Dropdown arrow: 13×9 triangle `#999489` at 306,398 (CYMBAL) and 1484,44 (MIDI CHANNEL); the TUNING button has none |
| Divider lines (1 px, `#353231`) | horizontal 39→1161 at y757, above the bottom row; vertical x1188, y110→850 | Layout v1's lines at y869 (338→1161 and the short 39→58) are gone |
| −/+ marks (1 px, `#695E52`) | Under each bottom-row knob, at fixed offsets from its centre (cx, cy): − at cx−81, cy+81 (11 px); + centred at cx+73, cy+82 (10×9). Vel: − at 145,962; + centred 299,963. Vol: − at 519,962; + centred 673,963 | The offsets are layout v1's |

The artwork had the BELL note box at y138 and the BELL2/BOW2 stick boxes at x1266. These are aligned to y140 and x1265 in the drawn version.

All dropdowns, pop-up menus and value text are drawn by JUCE (custom LookAndFeel) in the palette below.

## 4. Drawn labels (Calibri Light)
(Calibri Light, design units)

Measured from Kris's original artwork. "Top-left" is the top-left corner of the ink. Sizes and spacing were derived with Carlito (same metrics as Calibri) and should be checked once in JUCE against the original artwork.

Layout v2 (2026-10-25) moved VELOCITY SENSITIVITY, VOLUME and MIDI CHANNEL, and layout v3 (2026-10-25) moved MIDI CHANNEL again, to the left of its box at the top right; their text, font, size, spacing and colour are unchanged. Each is centred horizontally (the editor centres it by its ink width, so it stays centred with Calibri Light too; the left edges given are Carlito's). PAN, L and R (2026-10-25) are centred the same way; PAN is drawn like VOLUME.

The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of the background image and are not drawn.

| Text | Ink top-left | Font size (px) | Letter spacing (px) | Colour |
|---|---|---|---|---|
| CYMBAL | 160, 348 | 17.1 | 3.45 | `#A7A7A4` |
| VELOCITY SENSITIVITY | centred on x 226 (left 120.1), top 784 | 17.1 | 3.09 | `#A09C99` |
| VOLUME | centred on x 600 (left 560.8), top 784 | 17.1 | 3.40 | `#A09C99` |
| PAN | centred on x 974, top 784 | 17.1 | 3.40 | `#A09C99` |
| L · R (under Pan) | centred on x 901 · 1047, top 958 | 15.6 | — | `#695E52` |
| MIDI CHANNEL | on its box's line, to its left: ink right edge at x 1384, centred vertically on the box (centre y 48). Layout v2: centred over its box at x 199.5, top 478 | 17.1 | 3.11 | `#A7A7A4` |
| STICK ARTICULATION | 1201, 98 | 17.1 | 3.29 | `#A7A7A4` |
| MIDI NOTE | 1424, 98 | 17.1 | 3.00 | `#A7A7A4` |
| BELL / BOW / EDGE / BELL2 / BOW2 (rows) | x1201; y 148 / 228 / 307 / 387 / 468 | 15.6 | 3.33 / 2.88 / 2.83 / 3.50 / 3.25 | `#695E52` |
| CENTER / CENTER2 (rows 1 and 4 on SMOKE FLAT RIDE, section 8) | x1201; y 148 / 387 | 15.6 | 1.5 / 0.0 | `#695E52` |
| BLEND POSITION 1 | 1269, 545 | 17.1 | 3.18 | `#A7A7A4` |
| BELL · BOW (under Blend 1; CENTER · BOW on SMOKE FLAT RIDE) | 1249, 720 · 1409, 720 | 15.6 | 3.33 · 2.88 | `#695E52` |
| BLEND POSITION 2 | 1270, 783 | 17.1 | 2.98 | `#A7A7A4` |
| BOW · EDGE (under Blend 2) | 1249, 958 · 1409, 958 | 15.6 | 2.88 · 2.83 | `#695E52` |

Values drawn inside boxes use the same font in `#A7A7A4`; secondary values (note number) use `#695E52`. The TUNING button's text is drawn like the preset name: 17.1, spacing 3.2, `#A7A7A4`, centred.

CENTER and CENTER2 are drawn like the other row labels but with tighter letter spacing. At the other labels' spacing (about 3), CENTER2 would reach x 1277, into the stick box at x 1265; at 1.5 and 0.0 both end about 7.5 units clear of it.

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
| Wireframe cymbal (section 11) | rose dark `#704A3A`, rose light `#DEB096`, lathe `#CDA06E`, gold `#ECC88C`, hot `#FFEED7`, flash `#FFF5E6` |
| Hit display (section 10) | a lit row's label `#A5A299`, its stick and note box borders `#76624C`. The 2D marks' colours (spot `#FFFFFF` → `#FFF1D6` → `#E9C88F` → `#C29A5B`, rings, edge arcs) went with the marks on 2026-10-27 |

Hover, open and selected states use only colours from this table.

## 6. Interactions
- **Knobs:** vertical drag; double-click resets to the default.
- **No mouse wheel:** the mouse wheel and trackpad scrolling change no value anywhere in the editor: not the knobs, the note boxes or the dropdowns. A value changes only by a deliberate click and drag, a menu choice or MIDI learn (and the host's automation of the five parameters). The wheel still scrolls the note list.
- **MIDI note box:** vertical drag changes the note. A click opens a menu: MIDI LEARN, RESET DEFAULT, NONE (unassign) and a scrollable list of every note, C-2 (0) to G8 (127), each with its number; the current note is outlined and in view. No separate Learn button is added.
- **Stick box:** where the preset has both stick versions, a click opens a TIP / SHOULDER popup. In every preset the bell and bow rows (BELL, BOW, BELL2, BOW2) have both; section 8 lists what each plays. EDGE is SHOULDER only in every preset: its box shows SHOULDER and opens nothing. Which rows offer the choice comes from the slot table (`StickDisplay::choice` in `plugin/SlotTable.h`). Each preset keeps its own choices: picking SHOULDER on a row in one preset leaves the other presets' rows as they are. The choices are saved with the session (section 9); they are not automatable parameters.
- **Dropdowns:** the preset dropdown is a list of NOVA RIDE, SMOKE FLAT RIDE, HAMMERED THIN RIDE, SWISH and RAVEN, below its box; the channel dropdown is a 4×4 grid below its box, right-aligned with it. A choice sets the preset or the channel.
- **TUNING button** (tuning code only): a click opens the Tuning window, a second click closes it; while it is open the button has the open outline `#A5A299`, and the bronze `#76624C` on hover otherwise. Cmd-click on the CYMBALIS logo opens it too (`docs/TUNING.md`).
- **Preset switch:** the chosen preset's data is loaded into the one cymbal engine and plays from the next audio block (PORT_MAP, "602 and Bettis", decision 6). If the old cymbal is still ringing it fades out over 30 ms; it does not carry over. Notes, the MIDI channel and the knobs stay as they are; the rows play the stick choices of the chosen preset.
- Knob value readout while dragging: Velocity "100%", Volume "+0.0 dB", Pan "C" (or "L 35", "R 35"), Blend 1 "BELL 100 · 0 BOW", Blend 2 "BOW 100 · 0 EDGE".
- MIDI LEARN: the next note-on on the selected channel is assigned; the box pulses "LEARN" until then; clicking elsewhere cancels.
- Popups: background `#141313`, 2 px border `#534739`; hover outline `#76624C`; open outline `#A5A299`.

## 7. Nova Ride defaults
| Setting | Default |
|---|---|
| MIDI channel | 16 |
| BELL | SHOULDER, C3 (60) |
| BOW | TIP, D3 (62) |
| EDGE | SHOULDER (fixed), E3 (64) |
| BELL2 | TIP, C#3 (61) |
| BOW2 | SHOULDER, D#3 (63) |
| Blend Position 1 | Full BELL |
| Blend Position 2 | Full BOW |

Note naming uses the Logic/Ableton convention (60 = C3). The note number is always shown alongside the name.

The default notes are the same in every preset (section 8). A new instance starts with them, and RESET DEFAULT in a note box's menu returns that row to its default. A saved session keeps the notes it stored: an EDGE saved unassigned, as every session from before 2026-10-24 has it unless a note was given, stays unassigned.

**Blend knobs have no MIDI notes of their own.** They change the character of the existing slots:
- Blend 1 moves the BELL notes from full bell toward a bell/bow mix.
- Blend 2 moves the BOW notes from full bow toward a bow/edge mix.

Blend 2 inherits the engine's hard bow-to-edge switch near position 0.45 (PORT_MAP, engine as-is).

## 8. Slots

The rows, their base positions, blends and default notes are the same in every preset; what each row plays depends on the preset. Notes are per row and are kept across presets.

A preset can name a row: SMOKE FLAT RIDE labels its bell rows CENTER and CENTER2 (`PresetSlot::label` in `plugin/SlotTable.h`). The name is shown only. The row keeps its notes, its MIDI behaviour and its session keys (`note_BELL`, `note_BELL2`).

| Slot | Base position | Blend | Default note |
|---|---|---|---|
| BELL | 1.0 | Blend 1: 1.0 → 0.5 | C3 (60) |
| BOW | 0.5 | Blend 2: 0.5 → 0.0 | D3 (62) |
| EDGE | 0.0 | — | E3 (64) |
| BELL2 | 1.0 | Blend 1: 1.0 → 0.5 | C#3 (61) |
| BOW2 | 0.5 | Blend 2: 0.5 → 0.0 | D#3 (63) |

**NOVA RIDE** (`resources/presets/nova_ride.bake.json`)

| Slot | Default (stick) | Other stick choice | Stick box |
|---|---|---|---|
| BELL | `bell_shoulder` (SHOULDER) | `bell_tip` (TIP) | TIP / SHOULDER popup |
| BOW | `bow_tip` (TIP) | `bow_shoulder` (SHOULDER) | TIP / SHOULDER popup |
| EDGE | `edge` (SHOULDER) | — | SHOULDER (fixed) |
| BELL2 | `bell_tip` (TIP) | `bell_shoulder` (SHOULDER) | TIP / SHOULDER popup |
| BOW2 | `bow_shoulder` (SHOULDER) | `bow_tip` (TIP) | TIP / SHOULDER popup |

The bell and bow articulations are the four overlays (`bell_shoulder.json` and so on), so any bell or bow row can play either stick. **edge** is a copy of `bow_shoulder` played at the edge position, 0.0 (bake entry `EDGE`, `"copy_of": "BOW2"`). It is tuned on its own (`docs/TUNING.md`) and starts with bow_shoulder's level trim. Nova has no edge captures; until 2026-10-17 its EDGE row played the base preset at the edge position, which is gone from the bake.

**SMOKE FLAT RIDE** (602, `resources/presets/smoke_flat_ride.bake.json`): no bell. Its bell rows play **center**, a copy of the 602 bow that is tuned on its own (`docs/TUNING.md`). The existing bow and center are TIP. Their SHOULDER versions, **bow_shoulder** and **center_shoulder**, are copies of them, tuned on their own; untuned they play exactly as their TIP versions.

| Slot | Label | TIP (default) | SHOULDER | Stick box |
|---|---|---|---|---|
| BELL | CENTER | center (`CENTER`: a copy of `BOW`) | center_shoulder (`CENTER_SHOULDER`: a copy of `CENTER`) | TIP / SHOULDER popup |
| BOW | BOW | bow (`BOW`: `bow.json`, empty overlay) | bow_shoulder (`BOW_SHOULDER`: a copy of `BOW`) | TIP / SHOULDER popup |
| EDGE | EDGE | — | edge (`EDGE`: `edge.json`, empty overlay) | SHOULDER (fixed) |
| BELL2 | CENTER2 | center | center_shoulder | TIP / SHOULDER popup |
| BOW2 | BOW2 | bow | bow_shoulder | TIP / SHOULDER popup |

Each copy has its own level trim, initialised to its source's.

CENTER and CENTER2 strike where the bell rows do, at 1.0 − 0.5·blend_1. The 602's position weights are flat, so position does not change its excitation. Untuned, CENTER sounds like the bow at any Blend 1 setting. The only effect of position is through the MF/LF build-up: a strike at 0.75 or above counts with the bell weights. The 602 has its build-up off.

**HAMMERED THIN RIDE** (Bettis, `resources/presets/hammered_thin_ride.bake.json`): no overlays. The existing bell and bow are TIP. Their SHOULDER versions, **bell_shoulder** and **bow_shoulder**, are copies of them, tuned on their own; untuned they play exactly as their TIP versions.

| Slot | TIP (default) | SHOULDER | Stick box |
|---|---|---|---|
| BELL | bell (`BELL`) | bell_shoulder (`BELL_SHOULDER`: a copy of `BELL`) | TIP / SHOULDER popup |
| BOW | bow (`BOW`) | bow_shoulder (`BOW_SHOULDER`: a copy of `BOW`) | TIP / SHOULDER popup |
| EDGE | — | edge (`EDGE`) | SHOULDER (fixed) |
| BELL2 | bell | bell_shoulder | TIP / SHOULDER popup |
| BOW2 | bow | bow_shoulder | TIP / SHOULDER popup |

Each copy has its own level trim, initialised to its source's. In both presets every row with a choice starts on TIP, the stick it played before the shoulder versions existed. The blend knobs move whichever rows play, with either stick. In SMOKE FLAT RIDE, Blend 1 moves CENTER and CENTER2, and its end labels read CENTER · BOW.

**SWISH** (22" China, `resources/presets/swish.bake.json`): the bell is the base preset at the bell position with the v11 bell ratios (TIP); its SHOULDER version, **bell_shoulder**, is a copy of it, tuned on its own. The bow has both captures as overlays: **bow_tip** (with the stick click) and **bow_shoulder**. The edge is the base preset at the edge position (SHOULDER).

| Slot | TIP (default) | SHOULDER | Stick box |
|---|---|---|---|
| BELL | bell (`BELL`: no overlay, position 1.0) | bell_shoulder (`BELL_SHOULDER`: a copy of `BELL`) | TIP / SHOULDER popup |
| BOW | bow_tip (`BOW`: `bow_tip.json`) | bow_shoulder (`BOW_SHOULDER`: `bow_shoulder.json`) | TIP / SHOULDER popup |
| EDGE | — | edge (`EDGE`: no overlay, position 0.0) | SHOULDER (fixed) |
| BELL2 | bell | bell_shoulder | TIP / SHOULDER popup |
| BOW2 | bow_tip | bow_shoulder | TIP / SHOULDER popup |

Every row with a choice starts on TIP. bell_shoulder has its own level trim, measured with BELL's target (`docs/PORT_MAP.md`, "Swish").

**RAVEN** (20" dry ride, `resources/presets/raven.bake.json`): as HAMMERED THIN RIDE, no overlays. The existing bell and bow are TIP. Their SHOULDER versions, **bell_shoulder** and **bow_shoulder**, are copies of them, tuned on their own; untuned they play exactly as their TIP versions.

| Slot | TIP (default) | SHOULDER | Stick box |
|---|---|---|---|
| BELL | bell (`BELL`) | bell_shoulder (`BELL_SHOULDER`: a copy of `BELL`) | TIP / SHOULDER popup |
| BOW | bow (`BOW`) | bow_shoulder (`BOW_SHOULDER`: a copy of `BOW`) | TIP / SHOULDER popup |
| EDGE | — | edge (`EDGE`) | SHOULDER (fixed) |
| BELL2 | bell | bell_shoulder | TIP / SHOULDER popup |
| BOW2 | bow | bow_shoulder | TIP / SHOULDER popup |

Every row with a choice starts on TIP. Each copy starts with its source's level trim (`docs/PORT_MAP.md`, "Raven").

## 9. Parameters — exactly these five, no others
Placeholder ranges until Kris revises them. At the defaults the plugin sounds the same as the Python engine.

| Parameter ID | Knob | Range | Default | Behaviour | Readout |
|---|---|---|---|---|---|
| `velocity_sensitivity` | Velocity Sensitivity | 0–1 (shown 0–100%) | **1.0 (100%)** | Engine velocity = vf + s·(v_in − vf), where v_in = MIDI velocity / 127, vf = 100/127 and s is this parameter. At 100% this is the engine's own velocity response; at 0% every hit plays at velocity 100 | `100%` |
| `volume_db` | Volume | −60 to +6 dB | **0 dB** | Output gain. Knob curve dB = 6 − 66·(1 − p)², p = knob position 0–1, so 0 dB sits at p ≈ 0.70. Smoothed (no zipper noise) | `+0.0 dB` |
| `blend_1` | Blend Position 1 (BELL → BOW) | 0–1 (shown 0–100%) | **0 (full BELL)** | BELL and BELL2 strike at position 1.0 − 0.5·blend_1 (1.0 = bell, 0.5 = bow). Read at strike time | `BELL 100 · 0 BOW` |
| `blend_2` | Blend Position 2 (BOW → EDGE) | 0–1 (shown 0–100%) | **0 (full BOW)** | BOW and BOW2 strike at position 0.5 − 0.5·blend_2 (0.5 = bow, 0.0 = edge). Read at strike time. The engine's bow-to-edge excitation switches abruptly near position 0.45 (engine as-is) | `BOW 100 · 0 EDGE` |
| `pan` | Pan | −1 to +1 | **0 (centre)** | Overall stereo pan of the cymbal, to place it in a mix (added 2026-10-25 at Kris's request). Applied after Volume, as the last stage, smoothed over 20 ms like the volume (linearly: it runs through 0). At centre the output is exactly as without it | `C`; else `L n` / `R n`, n = round(\|p\| × 100), e.g. `L 35` |

**Pan law.** Each input channel is placed at a position and panned at constant power; both move together:
- p ≤ 0: posL = −1, posR = 1 + 2p; p > 0: posL = −1 + 2p, posR = +1.
- A channel at pos goes to the left output with cos((pos + 1)·π/4) and to the right with sin((pos + 1)·π/4).
- outL = L·gL(posL) + R·gL(posR); outR = L·gR(posL) + R·gR(posR).

At p = 0 that is L to L and R to R, unchanged (the stage is skipped). At −1 everything goes to the left output and the right output is 0; at +1 everything goes to the right. A hard-panned output is the sum of both channels, so its peak can be up to twice a channel's. The output meter of the Tuning window reads after Pan.

EDGE always strikes at position 0.0. All five are automatable. No other parameters. Their host-visible names and value texts are the same in every preset; on SMOKE FLAT RIDE, Blend Position 1 still reads BELL → BOW in the host, although its knob labels read CENTER · BOW.

Pan is saved with the session like the other four; a session saved before it existed loads with pan at centre.

Non-automatable state saved with the session: MIDI channel, note per slot, stick per slot and preset (rows with a choice), current preset, editor size. A session without a row's note key gives the row its default note. NOVA RIDE's sticks keep the session keys they had before the other presets offered a choice (`stick_BELL`, …); the other presets' keys name the preset (`stick_smoke_flat_ride_BELL`, `stick_hammered_thin_ride_BOW2`, `stick_swish_BOW`, …). A session without a key gives the row its default stick.

## 10. Hit display

Approved by Kris, 2026-10-25. UI only: it changes nothing in the sound and adds no parameter. Each strike shows on the cymbal, where it landed, and the row that played it lights up for a moment. Until 2026-10-27 a strike was a short 2D mark drawn over the photo; since then it is drawn on the wireframe cymbal (section 11): the cymbal flexes and tilts and flashes where it was struck.

**From the audio thread to the editor.**
- At each strike that a note-on plays, the processor writes one event into a lock-free single-producer, single-consumer FIFO, preallocated with room for 64 events: the row, the strike position (0 edge, 0.5 bow, 1 bell, after the blend knobs), the engine velocity (after Velocity Sensitivity) and the stick (TIP or SHOULDER, as the row's stick box shows it). See `CymbalisProcessor::pushHit` and its call in `strikeSlots` (`plugin/PluginProcessor.cpp`).
- That write is all the audio thread does for the display. It never repaints, triggers an async update, posts a message, locks or allocates. When the FIFO is full the event is dropped. With no editor open nothing reads the FIFO, so it fills and later events are dropped; an editor that opens discards what waited.
- The editor reads the FIFO on its timer and keeps up to 16 hits (`HitTracker`, `plugin/ui/HitDisplay.h`); when full, the oldest is dropped, and a hit is dropped once it is older than 6 s. The tracker's own timer runs at 30 Hz only while it keeps a hit and stops when it keeps none; each tick hands the hits to the cymbal, which draws a frame. While it keeps none, the editor's existing 30 Hz timer (notes, MIDI learn) reads the FIFO, and a hit starts the tracker's timer.

**Where a strike lands.**
- Each preset has a bell (or centre) zone as a fraction of the cymbal's radius: its shape's bell radius (`CymbalShape::bell` in `plugin/ui/CymbalShapes.h`, the slot table's `PresetInfo::shape`): NOVA RIDE 0.40, SMOKE FLAT RIDE 0.16 (centre zone), HAMMERED THIN RIDE 0.19, SWISH 0.22, RAVEN 0.22. Until 2026-10-27 they were the slot table's `bellRadius`, measured on the photos: HAMMERED THIN RIDE 0.40 and RAVEN 0.29, the others as now.
- Radius fraction from strike position p, linear between p = 1.0 → 0.6 × bell radius, p = 0.5 → (bell radius + 0.95) / 2, and p = 0.0 → 0.97.
- Angle: 4 to 5 o'clock, 45° clockwise from 3 o'clock plus a random offset uniform in ±12°. The editor draws the random number, never the audio thread.
- Zone: bell when the radius fraction is below the bell radius, edge when it is 0.9 or more, bow otherwise. How the cymbal moves and flashes: section 11, "Motion on hits".

**How a strike looks.** The 2D marks (a spot with a ripple ring and a bell ring, or an arc on the rim, over the photo) were removed on 2026-10-27. A strike now moves the wireframe cymbal and flashes where it struck (section 11).

**Row highlight.** For T = 0.25 + 0.25·v seconds after a row's latest strike (v its engine velocity; as long as the mark lasted, and as the flash lasts now), the row's label is drawn in `#A5A299` and its stick and note box borders in `#76624C` (hover and open outlines still win). Then they return to their normal colours.

**Drawing.** The 2D marks' cached images (the part "Drawing cheaply") went with them. How the cymbal is drawn and kept clear of the audio: section 11, "Rendering".

## 11. Wireframe cymbal

Approved by Kris, 2026-10-27, from mockups made with `docs/ui/wireframe_reference.py` (Python, documentation only: not built, not run in CI). UI only: the sound is unchanged and there are no new parameters. It replaces the photos and the 2D hit marks: each preset's cymbal is drawn as a rose-bronze wireframe in three-quarter view. When struck, the cymbal tilts in a slow circle, its plate-mode patterns light up and flex, and a flash appears where it was hit. Each preset has its own shape, lathed zones and hammering.

The reference's numbers are the specification; its colours are OpenCV BGR, given here as RGB. Code: `plugin/ui/CymbalShapes.h` (the shapes), `plugin/ui/CymbalModel.*` (geometry, colour, motion; plain C++), `plugin/ui/CymbalView.*` (drawing), `plugin/ui/HitDisplay.*` (`HitTracker`: the hits and the lit rows).

**Where it is drawn.**
- The cymbal area: 400,40 to 1180,750 (section 3), over the background image and under every control.
- The cymbal's radius is the same on screen for every preset: SWISH (22") is drawn the size of the 20" cymbals, as its photo was. Heights are fractions of each cymbal's own R.
- Camera, as the reference's `project()`: elevation 38°, centre 787,400, scale 345 design units per cymbal radius, perspective distance 5 radii. A point X, Y, Z (radius 1) goes to yv = Y sin e − Z cos e, zv = Y cos e + Z sin e, p = 5 / (5 − zv), screen 787 + 345·X·p, 400 + 345·yv·p.

**Geometry per preset.** `plugin/ui/CymbalShapes.h` holds one entry per preset, copied from the reference's `CYMBALS` table: R in inches, the profile points (radius and height in inches), the bell radius fraction, the lathe and fine-lathe zones, the hammering style and zone, and RAVEN's bell grooves. The slot table's `PresetInfo::shape` points to it.

| Preset | Shape | R (in) | Bell | Hammering, zone | Lathe zones | Fine lathe |
|---|---|---|---|---|---|---|
| NOVA RIDE | `nova` | 10 | 0.40 | scale, 0.43–0.925 | 0.27–0.405, 0.935–1.0 | — |
| SMOKE FLAT RIDE | `smoke` | 10 | 0.16 | radial, 0.03–0.885 | 0.885–1.0 | 0.89–0.95 |
| HAMMERED THIN RIDE | `hammered` | 10 | 0.19 | dimple, 0.43–0.92 | 0.0–0.405, 0.92–1.0 | 0.03–0.40 |
| SWISH | `swish` | 11 | 0.22 | none | 0.0–1.0 | 0.03–0.21, 0.24–0.78, 0.80–1.0 |
| RAVEN | `raven` | 10 | 0.22 | spiral, 0.25–0.93 | 0.0–0.225, 0.935–1.0 | — (bell grooves 0.035–0.215) |

- **Height** between the profile points (r/R, z/R): monotone cubic, PCHIP (Fritsch–Carlson, as scipy's `PchipInterpolator`, its end slopes included); r is clipped to 0–1.
- **Mesh** (`Cymbal.__init__`): rings at `linspace(0.03, bell, max(4, int(bell/0.04)))`, then `linspace(bell + 0.035, 0.92, max(6, int((0.92 − bell)/0.035)))`, then 0.94, 0.96, 0.98 and 1.0; 80 samples around each ring, closed; a spoke on every second sample (40 spokes) from the first ring to the rim. Lines are 1 design unit wide; the ring nearest the bell radius and the rim are 2 units.
- **Surface lines** (`_build_texture`), on the profile's surface (+0.0015), from a fixed-seed generator (`std::mt19937_64`, seed 11; the pattern matches the mockups in size, spacing and spread, not mark for mark):
  - `scale`: rows of arcs every 0.03 R through the zone; each arc spans 0.82 of a step 0.05/r, steps of 0.92–1.08 × that, rising 0.014 R in the middle, jittered ±0.004 R.
  - `spiral` (RAVEN): scale-shaped marks along an Archimedean spiral of pitch 0.065 R, each 0.105 R wide (0.85 of it drawn), rising 0.032 R in the middle, jittered ±0.005 R per mark.
  - `dimple` (HAMMERED THIN RIDE): one irregular dent per 0.011 of the zone's area, at random, its half axes 0.012–0.026 R (× 0.35–0.9 across), growing 0.8 to 2.4 × towards the edge, wobbled by its 2nd and 3rd harmonics.
  - `radial` (SMOKE FLAT RIDE): one short radial stroke per 0.0025 of the zone's area (from r 0.05), 0.015–0.035 R long.
  - Fine lathe: full rings every 0.012 R through each fine-lathe zone.
  - RAVEN's bell grooves: full rings from 0.035 to 0.215, spaced 0.011–0.026 R at random, each with an intensity 0.55–1.0; 2 units wide.
  - Dents pressed into the mesh are a later step: the hammering is only drawn on the surface.

**Colour and light** (`draw()`).
- Normal from the height field (profile, flex and tilt); light direction (0, −0.55, 0.83); shade = clip(n·light, 0, 1).
- light = clip(((shade − 0.86)/0.14)·0.8 + 0.2·ytl, 0, 1), ytl being 1 at the cymbal's top on screen and 0 at its bottom.
- Base colour c = ROSE_DARK + (ROSE_LIGHT − ROSE_DARK)·(0.25 + 0.75·light)^1.4; in a lathe zone c·0.55 + LATHE·(0.30 + 0.35·light); then mixed toward HOT by the glow g: c + (HOT − c)·g. A mesh segment takes its ends' average shade, ytl and g, and the lathe test at its middle radius.
- Colours (RGB): ROSE_DARK `#704A3A`, ROSE_LIGHT `#DEB096`, LATHE `#CDA06E`, GOLD `#ECC88C`, HOT `#FFEED7`, flash `#FFF5E6`.
- Surface lines (shade 0.9, no glow): hammer marks the surface colour × 0.8 (RAVEN's spiral × 1.0); fine-lathe rings the surface colour × (0.30 + 0.25·ytl); bell grooves GOLD × (0.30 + 0.32·ytl) × the groove's intensity.
- Lines lighten the background (max). Glow: around every mesh line brighter than 150/255 (its brightest channel), a soft halo is added: Gaussian across the line, sigma 3 design units, out to 9, peak 0.0665 × the line's colour per unit of its width. That is the reference's 3-unit blur added at 0.5, drawn as a halo instead of blurring the image.

**Motion on hits**, from the hit FIFO's events (section 10); v is the event's engine velocity.
- Strike point: angle 45° + uniform ±12° (the editor draws it); radius fraction and zone as in section 10, with the bell radii above.
- Plate modes m = 0…6, n = 0…2, z the n-th zero of J_m:

  | m | n = 0 | n = 1 | n = 2 |
  |---|---|---|---|
  | 0 | 2.4048 | 5.5201 | 8.6537 |
  | 1 | 3.8317 | 7.0156 | 10.1735 |
  | 2 | 5.1356 | 8.4172 | 11.6198 |
  | 3 | 6.3802 | 9.7610 | 13.0152 |
  | 4 | 7.5883 | 11.0647 | 14.3725 |
  | 5 | 8.7715 | 12.3386 | 15.7002 |
  | 6 | 9.9361 | 13.5893 | 17.0038 |

- Weights, per hit (`set_hits`): |J_m(z r)| / N_k × the zone's preference (bell 1/(1 + m), bow 1/(1 + |m − 2|), edge (1 + m)/7) × the reach exp(−(m + 1.5n)/(2 + 6v)), then divided by the largest of the 21. N_k, the mode's normalisation, is the largest |J_m(z r)| for r in [0, 1] (201 points).
- Displacement (`modal`): for every mode with weight 0.05 or more, a = v·w·exp(−age/τ)·(1 − exp(−age/0.03)), skipped below 0.001, with τ = 0.25 + 1.2/(1 + m + 2n), the second term × 1.6 on the edge (τ = 0.25 + 1.92/(1 + m + 2n) there; the reference's code); it adds a·J_m(z r)/N_k·cos(m(θ − angle))·cos(2π(2 + 0.8(m + n))·age). The mesh height is the profile's + 0.010·disp·stiff, stiff = clip((r − (bell − 0.1))/0.15, 0.12, 1).
- Glow g (`draw()`): with d = |disp|·stiff and D its largest value over the mesh, g = (d/D)^1.8 × min(1, D/0.35)^0.5 (none when D is below 0.0001).
- J_m(x) = (1/π)∫₀^π cos(mτ − x sin τ) dτ, a 64-point midpoint sum (Apple's libc++ has no `std::cyl_bessel_j`). The radial part J_m(z r)/N_k is computed once per preset for every ring and mode; the angular part per hit and frame.
- Wobble (`wobble()`, the `MOT` table): per zone amplitude A, decay τ (s), frequency f (Hz) and spin S (degrees): bell 0.002, 0.3, 2.6, 0; bow 0.006, 0.5, 2.0, 0; edge 0.025, 1.3, 1.4, 0.15. ramp = (1 − exp(−age/0.02))·exp(−age/τ); with u the strike direction and p ⟂ u, T += A·v·ramp·(cos(2πf·age)·u + 0.85·sin(2πf·age)·p) and spin += S·v·ramp·cos(2πf·age), summed over the hits. The cymbal is spun by spin degrees and tilted: Z −= 2.2·(X·T₀ + Y·T₁).
- Flash at the strike point, at the cymbal's rest position: a soft round spot in `#FFF5E6`, added on top: as the reference, a filled disc of its radius blurred with a Gaussian of sigma 0.6 × its radius (0.75 of the opacity at its centre, half that at 1.0 radius, nothing left by 3). Since sigma follows the radius, its shape is the same at every radius: a 49-point table out to 3 radii (`wire::flashProfile`), computed once. Radius (10 + 22v) design units, × 1.3 for SHOULDER, shrinking linearly to 0.7 × over T = 0.25 + 0.25v; opacity (0.4 + 0.6v)·k, k = (1 − age/T)².
- Row highlight: unchanged (section 10). Up to 16 hits, the oldest dropped; a hit is dropped once it is older than 6 s.

**Rendering.**
- The audio thread is unchanged: it only writes the FIFO.
- `CymbalView` covers the cymbal area only. An `juce::OpenGLContext` (OpenGL 3.2 core, 4× multisampling, component painting off, no continuous repainting) draws it on the context's own thread: the area's part of the background image as a texture (with mipmaps), then the mesh and the surface lines, the glow and the flashes. JUCE's software renderer never draws the moving cymbal, and the message thread paints nothing per frame.
- Buffers: the static geometry is uploaded once per preset: the mesh's segments (each its two mesh vertices), the ring radii, and the surface lines (each segment its two ends and kind). Per frame only the mesh's heights, slopes and glow (4 floats per vertex, in a float texture of 80 × the rings: 2,240 to 2,320 vertices, 28 or 29 rings) and a few uniforms (tilt, spin, camera, the cymbal's screen extent) change. The surface lines follow the tilt and spin, not the flex.
- Lines: each segment is drawn as a thin quad (two triangles), 1 or 2 design units wide and scaled with the editor, built in the vertex shader from its two ends (the core profile has no wide lines); multisampling antialiases them. No geometry shader is used. Lines lighten with `GL_MAX`; the glow and the flashes are added.
- Frames: the hit tracker asks for one at 30 Hz while it keeps a hit (any hit younger than 6 s); otherwise none is drawn until the next hit, a resize or a preset change.
- Fallback: if no OpenGL context starts within 3 s of the view showing, or its shaders fail, the cymbal is drawn at rest (no motion, no glow) with JUCE's drawing, once per preset or size, as an image: the background and the lines at rest, lightening it. Hits then show only as the lit row. `tools/cymbalis_snapshot` (which cannot capture OpenGL) always draws this still cymbal.
- A preset that fails to load shows its message in the cymbal area, drawn with JUCE, as before.

**Where the plugin differs from the reference's code**, all within the specification above:
- Glow: the reference adds its blurred lines only on pixels where the lines are already brighter than 150, which brightens the bright lines rather than surrounding them; the plugin draws the halo the specification describes.
- Normalisation: the reference divides each mode's displacement by its largest value on the mesh, and the weights by the largest |J_m| at 50 points of [0, 1]; the plugin uses N_k for both.
- The slope around a ring wraps around (numpy's `gradient` is one-sided at the seam), and the mode zeros are the 4-decimal table above.
