# UI specification — Cymbalis

Approved by Kris, 2026-10-13; presets SMOKE FLAT RIDE and HAMMERED THIN RIDE added 2026-10-15, SWISH 2026-10-17. Layout v2 (mockup v2: a larger, higher photo area, MIDI CHANNEL under CYMBAL, Velocity Sensitivity and Volume in a bottom row) approved by Kris 2026-10-25; the Pan parameter and its knob added 2026-10-25 at Kris's request; the hit display (section 10) approved by Kris 2026-10-25. Layout v3 (mockup v3: the MIDI CHANNEL box and the TUNING button swapped) approved by Kris 2026-10-25. The wireframe cymbal (section 11), replacing the photos and the 2D hit marks, approved by Kris 2026-10-27; its second round (NOVA RIDE, HAMMERED THIN RIDE and SMOKE FLAT RIDE) 2026-10-28, and its third (SWISH) and fourth (RAVEN) the same day. All coordinates are in 1536×1024 design units. `docs/ui/reference_mockup.png` shows the original artwork (layout v1).

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
- Each preset has a bell (or centre) zone as a fraction of the cymbal's radius: its shape's bell radius (`CymbalShape::bell` in `plugin/ui/CymbalShapes.h`, the slot table's `PresetInfo::shape`): NOVA RIDE 0.22, SMOKE FLAT RIDE 0.16 (centre zone), HAMMERED THIN RIDE 0.19, SWISH 0.19, RAVEN 0.22. NOVA RIDE's was 0.40 until its new bell (2026-10-28, section 11); SWISH's was 0.22 until its new shape (round 3, 2026-10-28, section 11). Until 2026-10-27 they were the slot table's `bellRadius`, measured on the photos: HAMMERED THIN RIDE 0.40 and RAVEN 0.29, the others as then.
- Radius fraction from strike position p, linear between p = 1.0 → 0.6 × bell radius, p = 0.5 → (bell radius + 0.95) / 2, and p = 0.0 → 0.97.
- Angle: 4 to 5 o'clock, 45° clockwise from 3 o'clock plus a random offset uniform in ±12°. The editor draws the random number, never the audio thread.
- Zone: bell when the radius fraction is below the bell radius, edge when it is 0.9 or more, bow otherwise. How the cymbal moves and flashes: section 11, "Motion on hits".

**How a strike looks.** The 2D marks (a spot with a ripple ring and a bell ring, or an arc on the rim, over the photo) were removed on 2026-10-27. A strike now moves the wireframe cymbal and flashes where it struck (section 11).

**Row highlight.** For T = 0.25 + 0.25·v seconds after a row's latest strike (v its engine velocity; as long as the mark lasted, and as the flash lasts now), the row's label is drawn in `#A5A299` and its stick and note box borders in `#76624C` (hover and open outlines still win). Then they return to their normal colours.

**Drawing.** The 2D marks' cached images (the part "Drawing cheaply") went with them. How the cymbal is drawn and kept clear of the audio: section 11, "Rendering".

## 11. Wireframe cymbal

Approved by Kris, 2026-10-27, from mockups made with `docs/ui/wireframe_reference.py` (Python, documentation only: not built, not run in CI); its second round approved 2026-10-28 (below: NOVA RIDE's new bell and bow, gold lathing measured from the photos, HAMMERED THIN RIDE's dents, SMOKE FLAT RIDE's lighter gold body); its third round, SWISH, approved by Kris on 2026-10-28 from mockups made from new photos of the cymbal (below: its new bell and profile, gold lathing, a copper bell and band, dents in three zones, lathe lines that follow the dents); its fourth round, RAVEN, approved by Kris on 2026-10-28 (below: lathing measured from its photo, power-hammer dents along a spiral pressed into the mesh). UI only: the sound is unchanged and there are no new parameters. It replaces the photos and the 2D hit marks: each preset's cymbal is drawn as a rose-bronze wireframe in three-quarter view. When struck, the cymbal tilts in a slow circle, its plate-mode patterns light up and flex, and a flash appears where it was hit. Each preset has its own shape, lathed zones and hammering.

The reference's numbers are the specification; its colours are OpenCV BGR, given here as RGB. Code: `plugin/ui/CymbalShapes.h` (the shapes), `plugin/ui/CymbalModel.*` (geometry, colour, motion; plain C++), `plugin/ui/CymbalView.*` (drawing), `plugin/ui/HitDisplay.*` (`HitTracker`: the hits and the lit rows).

**Where it is drawn.**
- The cymbal area: 400,40 to 1180,750 (section 3), over the background image and under every control.
- The cymbal's radius is the same on screen for every preset: SWISH (22") is drawn the size of the 20" cymbals, as its photo was. Heights are fractions of each cymbal's own R.
- Camera, as the reference's `project()`: elevation 38°, centre 787,400, scale 345 design units per cymbal radius, perspective distance 5 radii. A point X, Y, Z (radius 1) goes to yv = Y sin e − Z cos e, zv = Y cos e + Z sin e, p = 5 / (5 − zv), screen 787 + 345·X·p, 400 + 345·yv·p.

**Geometry per preset.** `plugin/ui/CymbalShapes.h` holds one entry per preset, copied from the reference's `CYMBALS` table: R in inches, the profile points (radius and height in inches; up to 15), the bell radius fraction, the lathe and fine-lathe zones, the hammering style and zone, RAVEN's bell grooves, and the 2026-10-28 fields below. The slot table's `PresetInfo::shape` points to it.

| Preset | Shape | R (in) | Bell | Hammering, zone | Lathe zones | Fine lathe |
|---|---|---|---|---|---|---|
| NOVA RIDE | `nova` | 10 | 0.22 | scale, 0.43–0.925 (drawn) | — | — |
| SMOKE FLAT RIDE | `smoke` | 10 | 0.16 | radial, 0.03–0.855 (drawn) | — | — |
| HAMMERED THIN RIDE | `hammered` | 10 | 0.19 | dimple, 0.43–0.92 (dents in the mesh) | — | — |
| SWISH | `swish` | 11 | 0.19 | three dent zones (dents in the mesh) | — | — |
| RAVEN | `raven` | 10 | 0.22 | power-hammer dents along a spiral, 0.24–0.93 (dents in the mesh; hammer zone 0.235–0.935) | — | — (bell grooves 0.073–0.166) |

The 2026-10-28 fields (NOVA RIDE, HAMMERED THIN RIDE and SMOKE FLAT RIDE in round 2, SWISH in round 3, RAVEN in round 4; the defaults are no gold, no tint, no copper, edges on, gold step 0.012, no photo table, the bell ring 2 units, not dented):

| Field | NOVA RIDE | HAMMERED THIN RIDE | SMOKE FLAT RIDE | SWISH | RAVEN |
|---|---|---|---|---|---|
| `gold` zones (from, to, strength; default strength 1) | 0.025–0.355, 0.373–0.397, 0.958–1.0 | 0.025–0.405, 0.90–1.0 at 0.6 | 0.858–1.0 | 0.19–0.52, 0.745–1.0 | 0.039–0.073, 0.170–0.22, 0.945–0.965, 0.975–1.0 |
| `tint` (gold metal, no lathe lines) | — | — | 0.0–0.857 | — | — |
| `copper` (copper patina, round 3) | — | — | — | 0.0–0.19 (the bell), 0.52–0.745 (the band) | — |
| `goldLines` (extra edge lines) | — | — | 0.885, 0.965 | — | — |
| `zoneEdges` (edge lines at the gold zones' bounds) | yes | yes | no | yes | yes |
| `goldStep` (lathe lines in the gold zones) | 0.006 | 0.006 | 0.004 | 0.014 | 0.006 |
| photo brightness table, `lumSmooth` | `kNovaPhotoLum`, 9 | `kHammeredPhotoLum`, 9 | `kSmokePhotoLum`, 1 | `kSwishPhotoLum`, 9 | `kRavenPhotoLum`, 9 |
| `bellLine` | no: the bell ring is 1 unit | no | no | no | no |
| `dented` | no | yes (`Dents`, below) | no | yes (`Dents`, below: three dent zones, the lines following the dents) | yes (`Dents`, below: dents along a spiral) |

NOVA RIDE's profile (2026-10-28: a bell like RAVEN's, its edge at 2.2", and a longer, shallower bow down to the flat ring), in inches: 0.0 0.90, 0.5 0.88, 1.3 0.74, 1.8 0.53, 2.05 0.41, 2.25 0.35, 2.45 0.33, 4.1 0.33, 5.0 0.40, 6.0 0.55, 7.0 0.71, 7.85 0.80, 8.45 0.75, 9.2 0.45, 10.0 0.0. The gold zones are measured from the photos (rim radius: Nova 354 px, Hammered 354.5, Smoke 356.5 = 10"): NOVA one lathed area from the hole to 3.55", a narrow ring 3.73–3.97", a band at the rim from 9.58"; HAMMERED one area from the hole to 4.05" (the bell included) and a dimmer band at the rim from 9.0"; SMOKE the lighter gold body to 8.57", then the dark lathed edge band, with bright narrow rings at 8.85" and 9.65".

SWISH (round 3, 2026-10-28, measured by Kris from his new top-down photo of the cymbal, rim 238 px = 11"): an unlathed copper bell to 2.1", a gold lathed ring 2.1–5.7", a dark hammered copper band 5.7–8.2", and the outer flange gold and lathed from 8.2" to the rim, with hammer marks near the rim. Its brightness table `kSwishPhotoLum` is from that photo (left and right radii), not from `Swish.png`. Its profile, in inches (14 points, as the reference's `CYMBALS['swish']`): 0.0 1.85, 0.6 1.81, 1.2 1.63, 1.6 1.33, 1.9 1.02, 2.1 0.86, 2.3 0.79, 4.5 0.66, 7.0 0.42, 8.4 0.24, 8.75 0.22, 9.4 0.30, 10.2 0.50, 11.0 0.78: the bell's edge at 2.1" and its top at 1.85", a straight bow sloping down, the outer flange turned up. Until round 3 SWISH was lathed all over (fine lathe 0.03–0.21, 0.24–0.78, 0.80–1.0), its bell radius 0.22, its bell ring 2 units wide.

RAVEN (round 4, 2026-10-28, lathing measured from `Raven.png`, rim centre, rim 353.75 px = 10"; the photo's bell zones scaled onto the wireframe's smaller bell): a dark ring at the hole, a smooth gold lathed ring (0.039–0.073), deep wavy lathe grooves (the bell grooves, 0.073–0.166), a smooth lathed flange to the bell's edge (0.170–0.22); then the power-hammered bow (dents along a spiral, below); at the rim a narrow bright lathed ring (0.945–0.965), a dark groove and a lathed band (0.975–1.0). Its brightness table `kRavenPhotoLum` is the reference's `RAVEN_PHOTO_LUM`, already remapped to the wireframe's radii (the photo's bell, its edge at 2.85", scaled onto the wireframe's bell, its edge at 2.2"; the bow and edge kept), used as it is. Its profile and bell radius are unchanged. Until round 4 RAVEN was a ring mesh with lathe zones 0.0–0.225 and 0.935–1.0, bell grooves 0.035–0.215, scale-shaped `spiral` hammer marks drawn from 0.25 to 0.93, and its bell ring 2 units wide.

How the presets are hammered (Kris, 2026-10-28): RAVEN and NOVA RIDE are power-hammered, in regular patterns (RAVEN's dents along a spiral in the mesh; NOVA's drawn scale arcs); SMOKE FLAT RIDE and HAMMERED THIN RIDE are hand-hammered (SMOKE's drawn radial strokes; HAMMERED's random dents in the mesh).

- **Height** between the profile points (r/R, z/R): monotone cubic, PCHIP (Fritsch–Carlson, as scipy's `PchipInterpolator`, its end slopes included); r is clipped to 0–1.
- **Mesh** (`Cymbal.__init__`, NOVA RIDE and SMOKE FLAT RIDE; the dented meshes of HAMMERED THIN RIDE, SWISH and RAVEN below): rings at `linspace(0.03, bell, max(4, int(bell/0.04)))`, then `linspace(bell + 0.035, 0.92, max(6, int((0.92 − bell)/0.035)))`, then 0.94, 0.96, 0.98 and 1.0; 80 samples around each ring, closed; a spoke on every second sample (40 spokes) from the first ring to the rim. Lines are 1 design unit wide; the rim is 2 units, and so is the ring nearest the bell radius where `bellLine` (no preset since round 4; until then SWISH and RAVEN).
- **Surface lines** (`_build_texture`), on the profile's surface (+0.0015), from a fixed-seed generator (`std::mt19937_64`, seed 11; the pattern matches the mockups in size, spacing and spread, not mark for mark):
  - `scale`: rows of arcs every 0.03 R through the zone; each arc spans 0.82 of a step 0.05/r, steps of 0.92–1.08 × that, rising 0.014 R in the middle, jittered ±0.004 R.
  - `spiral` (RAVEN until round 4; no preset draws it now): scale-shaped marks along an Archimedean spiral of pitch 0.065 R, each 0.105 R wide (0.85 of it drawn), rising 0.032 R in the middle, jittered ±0.005 R per mark.
  - `dimple` (HAMMERED THIN RIDE): one irregular dent per 0.011 of the zone's area, at random, its half axes 0.012–0.026 R (× 0.35–0.9 across), growing 0.8 to 2.4 × towards the edge, wobbled by its 2nd and 3rd harmonics.
  - `radial` (SMOKE FLAT RIDE): one short radial stroke per 0.0025 of the zone's area (from r 0.05), 0.015–0.035 R long.
  - Fine lathe: full rings every 0.012 R through each fine-lathe zone.
  - RAVEN's bell grooves: full rings from 0.073 to 0.166 (0.035 to 0.215 until round 4), spaced 0.011–0.026 R at random, each with an intensity 0.55–1.0; 2 units wide.
  - Gold lathing (2026-10-28): full rings every `goldStep` across each gold zone (numpy's `arange`, value for value), 1 unit wide.
  - Edge lines (2026-10-28): full rings at each gold zone's bounds that lie between 0.02 and 0.999 (unless `zoneEdges` is off), and at each `goldLines` radius; 2 units wide.
  - HAMMERED THIN RIDE, SWISH and RAVEN draw no hammer marks: their dents are in the mesh (below). NOVA RIDE and SMOKE FLAT RIDE keep theirs drawn.

**Colour and light** (`draw()`).
- Normal from the height field (profile, flex and tilt); light direction (0, −0.55, 0.83); shade = clip(n·light, 0, 1).
- light = clip(((shade − 0.86)/0.14)·0.8 + 0.2·ytl, 0, 1), ytl being 1 at the cymbal's top on screen and 0 at its bottom.
- Base colour c = ROSE_DARK + (ROSE_LIGHT − ROSE_DARK)·(0.25 + 0.75·light)^1.4; then, in this order (the first zone that holds r):
  - copper zone (SWISH, round 3): min(COPPER·(0.95 + 0.55·light), 1) per channel (255 in the reference), in place of the bronze or gold colour. The reference applies it after the gold zones (`Cymbal2.draw()`), so where a copper and a gold zone meet (0.19, 0.52, 0.745) the copper wins;
  - tint zone: c·0.15 + GOLD·(0.62 + 0.38·light)·lum(r) (the lighter gold metal);
  - gold zone: c·0.25 + GOLD·(0.42 + 0.48·light)·lum(r)·strength;
  - lathe zone: c·0.55 + LATHE·(0.30 + 0.35·light);
  - then, on the dented meshes (HAMMERED THIN RIDE, SWISH, RAVEN), the dent light (below); then mixed toward HOT by the glow g: c + (HOT − c)·g.
- A mesh segment takes its ends' average shade, ytl and g, and the zone at its middle radius (the reference's `col()`); on the dented meshes each end's colour is computed at its own radius and the segment takes their average (`Cymbal2`).
- **Photo brightness lum(r)** (`Cymbal.lum`), once per preset: the 200-value table (the photo's mean brightness by radius, 0.005 R steps from the centre, measured 2026-10-28; `kNovaPhotoLum`, `kHammeredPhotoLum`, `kSmokePhotoLum`, `kSwishPhotoLum`, `kRavenPhotoLum` in `CymbalShapes.h`) smoothed with a moving average of `lumSmooth` samples (padded with the edge values), divided by the 98th percentile of entries 5 to 79 (numpy's linear interpolation). lum(r) is entry floor(r/0.005), at most 199, clipped to 0.3–1.15; 1 without a table.
- Colours (RGB): ROSE_DARK `#704A3A`, ROSE_LIGHT `#DEB096`, LATHE `#CDA06E`, GOLD `#ECC88C`, COPPER `#C46E42` (round 3; the reference's BGR 66, 110, 196), HOT `#FFEED7`, flash `#FFF5E6`.
- Surface lines (shade 0.9, no glow): hammer marks the surface colour (at the mark's mean radius) × 0.8 (the `spiral` marks × 1.0); fine-lathe rings the surface colour × (0.30 + 0.25·ytl); bell grooves GOLD × (0.30 + 0.32·ytl) × the groove's intensity; gold lathing the surface colour × (0.50 + 0.35·ytl) × lum(r) (on the dented meshes, as `Cymbal2`: GOLD × (0.50 + 0.35·ytl) × lum(r) × strength); edge lines GOLD × (0.55 + 0.40·ytl) × the zone's strength. On SWISH the lines then take the dent light (below, "Lines that follow the dents").
- The lines are combined as the reference's `draw()` combines them, in a layer of their own: the surface lines over one another in their drawing order (where two cross, the later one shows: SMOKE FLAT RIDE's dense lathe lines mostly cover its two edge lines, as in the mockup), and the mesh lightening them (max); on the dented meshes (`Cymbal2`) the surface lines are drawn over the mesh. The layer then lightens the background (max).
- Glow: around every mesh line brighter than 150/255 (its brightest channel), a soft halo is added: Gaussian across the line, sigma 3 design units, out to 9, peak 0.0665 × the line's colour per unit of its width. That is the reference's 3-unit blur added at 0.5, drawn as a halo instead of blurring the image.

**Dents in the mesh: HAMMERED THIN RIDE, SWISH and RAVEN** (2026-10-28, the reference's `Cymbal2`, `DENTED = {'hammered', 'swish', 'raven'}`; `CymbalShape::dents`; SWISH since round 3, RAVEN since round 4).
- Mesh: a fine grid of 197 radial samples from 0.02 to 1.0 (0.005 R apart) by 480 around. Rings are drawn on every `ringStep`-th radial sample (HAMMERED THIN RIDE 4: 51 rings; SWISH 5: 42 rings; RAVEN 4: 50 rings, its bell ring falling on every 4th), the bell ring and the rim; spokes on every 12th sample around (40), through every radial sample.
- Dents, from a fixed-seed generator (`std::mt19937_64`, seed 11; the pattern matches in count, sizes and spread, not dent for dent):
  - HAMMERED THIN RIDE (`_make_dents`, 'dimple'): large ones: int(area/0.011/2^1.5), area that of the hammer zone 0.43–0.92 (66); at r = sqrt(U(z0², z1²)), a random angle; a = U(0.012, 0.026)·(0.8 + 1.6·grow)·2, b = a·U(0.45, 0.9), a random rotation, depth 0.0042·(a/0.03)^0.7·U(0.6, 1.2); grow = (r − z0)/(z1 − z0);
  - then 110 small ones in the gaps: a = U(0.018, 0.028)·(0.85 + 0.5·grow), b = a·U(0.5, 0.9), depth 0.0042·(a/0.03)^0.7·U(0.6, 1.1); a candidate is rejected when its centre is closer than 0.9·(its a + the other's a) to any dent placed (at most 20,000 tries). 176 dents in all.
  - SWISH (`dent_zones`, round 3): three zones (`Dents::zones`), each packed at random without overlap, in this order:

    | Zone | From–to (R) | Number | a | b/a | Depth scale |
    |---|---|---|---|---|---|
    | the bell | 0.04–0.185 | 60 | 0.017–0.024 | 0.6–0.95 | 1.0 |
    | the copper band | 0.525–0.74 | 400 | 0.019–0.027 | 0.55–0.95 | 1.0 |
    | near the rim | 0.83–0.985 | 160 | 0.022–0.034 | 0.45–0.8 | 0.8 |

    In each zone: r = sqrt(U(from², to²)), an angle t uniform in 0–2π; a = U(a), b = a·U(b/a); rotation t + U(−0.6, 0.6); depth 0.0042·(a/0.03)^0.7·U(0.7, 1.1)·the depth scale. A candidate is rejected when its centre is closer than 0.85·(its a + the other's a) to any dent placed (in any zone); a zone stops at its number or after 40 × that many tries. All 620 fit, as in the reference. No 'dimple' hammering.
  - RAVEN (`dent_spiral`, round 4; `Dents::spiral`): power-hammer marks in a regular pattern, along a spiral through the zone 0.24–0.93. The size factor k runs linearly from 0.6 at the zone's inner radius to 1.5 at its outer radius. From θ = 0, r = 0.24, while r < 0.93: k from r; step = 0.05·k/max(r, 0.05); one dent (unless U < skip; skip is 0, so never skipped) at angle θ + U(−1, 1)·0.06·step and radius r + U(−1, 1)·0.04·0.065·k, its size g = U(0.95, 1.05), a = 0.032·k·g along the radius and b = 0.018·k·g across (its rotation its angle + U(−1, 1)·0.05, so its a axis runs along the radius), depth 0.0042·(a/0.03)^0.7·U(0.95, 1.05); then θ += step·U(0.98, 1.02) and r += 0.065·k·step/(2π) (a pitch of 0.065·k a turn). 716 dents, as in the reference; the spacing stays regular (a power hammer's pattern, not hand hammering). No 'dimple' hammering or zones.
  - each dent's height: dz = −depth·dent_depth·exp(−1.6·q), q = (u/a)² + (v/b)² in the dent's rotated frame, within 3a of its centre in x and y. No lip. dent_depth 2.4 (`Dents::depth`); SWISH 1.5 and RAVEN 1.2 (shallow: their deepest dents are about 0.07").
- Rest height: height(r) + dz. The flex, tilt, spin, glow and flashes work as on the other presets, on this mesh.
- Light: the base light uses the smooth surface (profile, flex and tilt; no dents). The dents add the dent light factor f = clip(1 + contrast·dshade, 0.25, 1.9), contrast 5 (`Dents::contrast`; SWISH 6, RAVEN 12), dshade = shade with the dents − shade without, computed once at rest per vertex: where f > 1, c + (ROSE_LIGHT·1.15 − c)·min(f − 1, 1); else c·f. After the copper and gold, before the glow mix.
- Fewer segments (`_simplify_lines`), once per preset: each drawn ring and spoke is simplified with Douglas–Peucker on its rest position in design units (345 per radius) plus 4·f (so a brightness step of 0.06 counts too), tolerance 0.25 design units: a sample splits a span when its largest coordinate difference from the span's chord, interpolated by sample index, exceeds it. A ring always keeps every 6th sample (80 around) and a spoke every 5th, so the flex stays smooth; rings are closed. This takes HAMMERED THIN RIDE from 32,320 segments to about 12,000 (the plugin: 12,071, from about 11,400 kept samples), with no visible change (Kris approved the comparison on 2026-10-28), SWISH from 28,000 to 12,501 (the reference: 12,585; from about 10,900 kept samples), and RAVEN from 31,840 to 19,785 (the reference: 19,726; from about 18,900 kept samples: its many regular dents keep more). Only the kept samples are vertices.
- Gold lathing as `Cymbal2` (above); its edge lines count the gold zones' bounds from 0.03, not 0.02 (HAMMERED THIN RIDE 0.405 and 0.90 at 0.6; SWISH 0.19, 0.52 and 0.745; RAVEN 0.039, 0.073, 0.17, 0.22, 0.945, 0.965 and 0.975). RAVEN's bell grooves are drawn here too, as `Cymbal2._lines` draws them (first, before the gold lathing).
- **Lines that follow the dents** (SWISH, `lines_follow_dents`, `Dents::linesFollowDents`, round 3): each gold lathe ring and edge line is sampled on the mesh's 480 angles at the nearest mesh radius, at height(r) + 0.0015 + that radius's dent height there, and simplified once as the mesh rings are (Douglas–Peucker at 0.25 design units on the position in design units plus 4·f, every 6th sample kept, closed); a radius with no dents keeps every 6th sample (80 around). Each segment, after its colour above, takes the dent light from its ends' average f: where f > 1, c + (HOT·0.95 − c)·min(f − 1, 1); else c·f. The zone edge lines follow the dents' height but stay whole (f = 1). Without `lines_follow_dents` (HAMMERED THIN RIDE, RAVEN, and the ring-mesh presets) the surface lines stay flat, as before.

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
- J_m(x) = (1/π)∫₀^π cos(mτ − x sin τ) dτ, a 64-point midpoint sum (Apple's libc++ has no `std::cyl_bessel_j`). The radial part J_m(z r)/N_k is computed once per preset for every ring and mode (on the dented meshes for every fine radius, with its slope z·J_m′(z r)/N_k, J_m′ = (J_(m−1) − J_(m+1))/2); the angular part per hit and frame.
- Wobble (`wobble()`, the `MOT` table): per zone amplitude A, decay τ (s), frequency f (Hz) and spin S (degrees): bell 0.002, 0.3, 2.6, 0; bow 0.006, 0.5, 2.0, 0; edge 0.025, 1.3, 1.4, 0.15. ramp = (1 − exp(−age/0.02))·exp(−age/τ); with u the strike direction and p ⟂ u, T += A·v·ramp·(cos(2πf·age)·u + 0.85·sin(2πf·age)·p) and spin += S·v·ramp·cos(2πf·age), summed over the hits. The cymbal is spun by spin degrees and tilted: Z −= 2.2·(X·T₀ + Y·T₁).
- Flash at the strike point, at the cymbal's rest position: a soft round spot in `#FFF5E6`, added on top: as the reference, a filled disc of its radius blurred with a Gaussian of sigma 0.6 × its radius (0.75 of the opacity at its centre, half that at 1.0 radius, nothing left by 3). Since sigma follows the radius, its shape is the same at every radius: a 49-point table out to 3 radii (`wire::flashProfile`), computed once. Radius (10 + 22v) design units, × 1.3 for SHOULDER, shrinking linearly to 0.7 × over T = 0.25 + 0.25v; opacity (0.4 + 0.6v)·k, k = (1 − age/T)².
- Row highlight: unchanged (section 10). Up to 16 hits, the oldest dropped; a hit is dropped once it is older than 6 s.

**Rendering.**
- The audio thread is unchanged: it only writes the FIFO.
- `CymbalView` covers the cymbal area only. An `juce::OpenGLContext` (OpenGL 3.2 core, 4× multisampling, component painting off, no continuous repainting) draws it on the context's own thread: the area's part of the background image as a texture (with mipmaps), then the mesh and the surface lines, the glow and the flashes. JUCE's software renderer never draws the moving cymbal, and the message thread paints nothing per frame.
- Buffers: the static geometry is uploaded once per preset: the mesh's segments (each its two mesh vertices and their metal), its vertices (radius, angle, dent light factor: a float texture), and the surface lines (each segment its two ends, kind and dent light factor). Per frame only the mesh's heights, the smooth surface's slopes and the glow (4 floats per vertex, in a second float texture: 2,320 vertices on the ring meshes, about 11,400 on HAMMERED THIN RIDE, 10,900 on SWISH and 18,900 on RAVEN) and a few uniforms (tilt, spin, camera, the cymbal's screen extent) change. On the dented meshes the flex is summed per frame from a per-radius table (Σ over m of A_m(r)·cos mθ + B_m(r)·sin mθ, and its slopes) at the kept vertices only. The surface lines follow the tilt and spin, not the flex (SWISH's carry its dents, fixed at rest).
- Size: about 3,440 mesh segments and 2,700 to 10,700 surface line segments on the ring-mesh presets (NOVA RIDE, SMOKE FLAT RIDE); on HAMMERED THIN RIDE about 12,000 mesh segments and 4,000 surface line segments; on SWISH about 12,500 and 8,400 (21,000 in all); on RAVEN about 19,800 and 1,700 (21,500 in all). The static geometry, dents included, is built once per preset.
- Lines: each segment is drawn as a thin quad (two triangles), 1 or 2 design units wide (the surface lines' widths per vertex, so they draw in one call, in order) and scaled with the editor, built in the vertex shader from its two ends (the core profile has no wide lines); multisampling antialiases them. No geometry shader is used.
- The lines' layer is a multisampled framebuffer the size of the view, cleared to black: the surface lines drawn over one another (no blending) and the mesh with `GL_MAX` (on the dented meshes the mesh first, then the surface lines over it). It is resolved into a texture, which is drawn over the background with `GL_MAX`; then the glow and the flashes are added. Where the driver refuses the framebuffer, all the lines lighten the background directly. The still drawing builds the same layer with JUCE's drawing.
- Frames: the hit tracker asks for one at 30 Hz while it keeps a hit (any hit younger than 6 s); otherwise none is drawn until the next hit, a resize or a preset change.
- Fallback: if no OpenGL context starts within 3 s of the view showing, or its shaders fail, the cymbal is drawn at rest (no motion, no glow) with JUCE's drawing, once per preset or size, as an image: the background and the lines at rest (HAMMERED THIN RIDE, SWISH and RAVEN dented, SWISH's surface lines following the dents), lightening it. Hits then show only as the lit row. `tools/cymbalis_snapshot` (which cannot capture OpenGL) always draws this still cymbal.
- A preset that fails to load shows its message in the cymbal area, drawn with JUCE, as before.

**Where the plugin differs from the reference's code**, all within the specification above:
- Glow: the reference adds its blurred layer only on pixels where the layer is already brighter than 150, which brightens the bright lines rather than surrounding them; the plugin draws the halo the specification describes, around the mesh lines only. Since 2026-10-28 some surface lines are that bright too (the gold lathing and edge lines at the top of NOVA RIDE and HAMMERED THIN RIDE): the reference brightens them by up to half again, the plugin does not, so the lit top of their gold zones is dimmer than in the mockups, and the halo of the bright mesh lines under HAMMERED THIN RIDE's lathing still shows near its bell. On SWISH (round 3) nearly every copper mesh line is brighter than 150 (its red channel), so its copper bell and band carry a faint copper haze between their lines, where the reference's stay dark between them. On RAVEN (round 4) the gold bell zones and the rim band are a little dimmer than in the mockups for the same reason, and the bright dent walls on the bow carry a faint halo.
- Normalisation: the reference divides each mode's displacement by its largest value on the mesh, and the weights by the largest |J_m| at 50 points of [0, 1]; the plugin uses N_k for both.
- The slope around a ring wraps around (numpy's `gradient` is one-sided at the seam), and the mode zeros are the 4-decimal table above.
- The dented meshes' flex slopes are exact (from J_m′) where the reference differentiates its fine grid numerically (0.005 R apart: the same to the eye), and its glow's largest value D is taken over the kept vertices, not the whole fine grid.
