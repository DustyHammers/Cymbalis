# UI specification — Cymbalis

Approved by Kris, 2026-10-13; presets SMOKE FLAT RIDE and HAMMERED THIN RIDE added 2026-10-15, SWISH 2026-10-17. Layout v2 (mockup v2: a larger, higher photo area, MIDI CHANNEL under CYMBAL, Velocity Sensitivity and Volume in a bottom row) approved by Kris 2026-10-25; the Pan parameter and its knob added 2026-10-25 at Kris's request; the hit display (section 10) approved by Kris 2026-10-25. All coordinates are in 1536×1024 design units. `docs/ui/reference_mockup.png` shows the original artwork (layout v1).

## 1. Assets (`resources/ui/`)

| File | Use |
|---|---|
| `CYMBALIS_Background_logo.png` (1536×1024) | Background. The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of it; **do not draw them**. Everything else on this page is drawn by JUCE. |
| `Nova.png` (707×713, transparent) | NOVA RIDE photo |
| `SmokeFlatRide.png` (714×718) | SMOKE FLAT RIDE photo (602) |
| `HammeredThinRide.png` (709×713) | HAMMERED THIN RIDE photo (Bettis) |
| `Swish.png` (737×747) | SWISH photo |
| `knob.png` (128×11648) | Knob filmstrip: 91 frames of 128×128, about 7 o'clock to 5 o'clock, frame 45 at 12 o'clock |
| `calibril.ttf` | Calibri Light, used for all drawn text. If it is missing, use Carlito Regular (SIL OFL, include its licence file) until Kris adds it |

## 2. Window

- Resizable, aspect ratio fixed at 3:2. **Maximum 1536×1024 — never larger.** Minimum 960×640.
- Opens at the largest size that fits the screen, up to the maximum. Size is saved with the session.
- If a host gives the editor a size that is not 3:2, the whole design is scaled to fit and centred, so no control is cut off.
- Paint everything in design units and scale with one transform.

## 3. Layout

Layout v2, approved by Kris 2026-10-25 (mockup v2). Left column: the CYMBAL and MIDI CHANNEL boxes. Centre: the photo. Bottom row: Velocity Sensitivity, Volume and Pan. Right column: the rows and the blend knobs, as in layout v1.

| Element | Position | Notes |
|---|---|---|
| Cymbal photo | top-left 447,50, size 680×680 (centre 787,390) | Changes per preset. Each photo is fitted into this area with its aspect ratio kept, centred (Nova 674×680, Smoke Flat Ride 676×680, Hammered Thin Ride 676×680, Swish 671×680). Layout v1: 434,94, 707×713 |
| CYMBAL preset dropdown | box 61,371, 277×62 | Shows the preset name, e.g. **NOVA RIDE**. Changing preset changes the name, the photo and the sound. A name too wide to sit centred clear of the arrow is centred between the box's left edge and the arrow, with tighter letter spacing |
| MIDI CHANNEL dropdown | box 61,501, 277×62, under the CYMBAL box | Channels 1–16, in a 4×4 grid that opens above the box, centred on it. Layout v1: 632,936 |
| Bottom row | x 39–1161 in thirds (39–413, 413–787, 787–1161), knob centres at y 881 (the height of Blend Position 2) | Each knob is centred in its third |
| Velocity Sensitivity knob | centre 226,881 (first third) | See section 9. Layout v1: 201,641 |
| Volume knob | centre 600,881 (second third) | See section 9. Layout v1: 199,881 |
| Pan knob | centre 974,881 (third third) | See section 9. Frame 45 (12 o'clock) at centre, 0 at full left, 90 at full right. End labels L and R (section 4) instead of −/+ marks |
| Hit display | over the photo, clipped to the cymbal disc, under every control | A mark for each strike and the row that played it lit (section 10) |
| Blend Position 1 knob (BELL ↔ BOW) | centre 1349,642 | |
| Blend Position 2 knob (BOW ↔ EDGE) | centre 1350,881 | |
| Articulation rows | box tops at y 140 / 220 / 300 / 380 / 460 (BELL, BOW, EDGE, BELL2, BOW2) | Each row has a stick box and a MIDI note box |
| Stick box (under STICK ARTICULATION) | x1265, 99×25, 2 px border `#463C30` | Shows **TIP** or **SHOULDER** (section 8); selectable where the preset has both (section 6) |
| MIDI note box (under MIDI NOTE) | x1424, 66×25, 2 px border `#463C30` | Note name + number, e.g. `C3 60` |
| Preset and channel boxes | 2 px border `#534739` | Dropdown arrow: 13×9 triangle `#999489` at 306,398 and 306,528 |
| Divider lines (1 px, `#353231`) | horizontal 39→1161 at y757, above the bottom row; vertical x1188, y110→850 | Layout v1's lines at y869 (338→1161 and the short 39→58) are gone |
| −/+ marks (1 px, `#695E52`) | Under each bottom-row knob, at fixed offsets from its centre (cx, cy): − at cx−81, cy+81 (11 px); + centred at cx+73, cy+82 (10×9). Vel: − at 145,962; + centred 299,963. Vol: − at 519,962; + centred 673,963 | The offsets are layout v1's |

The artwork had the BELL note box at y138 and the BELL2/BOW2 stick boxes at x1266. These are aligned to y140 and x1265 in the drawn version.

All dropdowns, pop-up menus and value text are drawn by JUCE (custom LookAndFeel) in the palette below.

## 4. Drawn labels (Calibri Light)
(Calibri Light, design units)

Measured from Kris's original artwork. "Top-left" is the top-left corner of the ink. Sizes and spacing were derived with Carlito (same metrics as Calibri) and should be checked once in JUCE against the original artwork.

Layout v2 (2026-10-25) moved VELOCITY SENSITIVITY, VOLUME and MIDI CHANNEL; their text, font, size, spacing and colour are unchanged. Each is centred horizontally (the editor centres it by its ink width, so it stays centred with Calibri Light too; the left edges given are Carlito's). PAN, L and R (2026-10-25) are centred the same way; PAN is drawn like VOLUME.

The CYMBALIS logo and "BRONZE MODELING INSTRUMENT" are part of the background image and are not drawn.

| Text | Ink top-left | Font size (px) | Letter spacing (px) | Colour |
|---|---|---|---|---|
| CYMBAL | 160, 348 | 17.1 | 3.45 | `#A7A7A4` |
| VELOCITY SENSITIVITY | centred on x 226 (left 120.1), top 784 | 17.1 | 3.09 | `#A09C99` |
| VOLUME | centred on x 600 (left 560.8), top 784 | 17.1 | 3.40 | `#A09C99` |
| PAN | centred on x 974, top 784 | 17.1 | 3.40 | `#A09C99` |
| L · R (under Pan) | centred on x 901 · 1047, top 958 | 15.6 | — | `#695E52` |
| MIDI CHANNEL | centred over its box, x 199.5 (left 130.8), top 478 | 17.1 | 3.11 | `#A7A7A4` |
| STICK ARTICULATION | 1201, 98 | 17.1 | 3.29 | `#A7A7A4` |
| MIDI NOTE | 1424, 98 | 17.1 | 3.00 | `#A7A7A4` |
| BELL / BOW / EDGE / BELL2 / BOW2 (rows) | x1201; y 148 / 228 / 307 / 387 / 468 | 15.6 | 3.33 / 2.88 / 2.83 / 3.50 / 3.25 | `#695E52` |
| CENTER / CENTER2 (rows 1 and 4 on SMOKE FLAT RIDE, section 8) | x1201; y 148 / 387 | 15.6 | 1.5 / 0.0 | `#695E52` |
| BLEND POSITION 1 | 1269, 545 | 17.1 | 3.18 | `#A7A7A4` |
| BELL · BOW (under Blend 1; CENTER · BOW on SMOKE FLAT RIDE) | 1249, 720 · 1409, 720 | 15.6 | 3.33 · 2.88 | `#695E52` |
| BLEND POSITION 2 | 1270, 783 | 17.1 | 2.98 | `#A7A7A4` |
| BOW · EDGE (under Blend 2) | 1249, 958 · 1409, 958 | 15.6 | 2.88 · 2.83 | `#695E52` |

Values drawn inside boxes use the same font in `#A7A7A4`; secondary values (note number) use `#695E52`.

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
| Hit display (section 10) | spot gradient `#FFFFFF` → `#FFF1D6` → `#E9C88F` → `#C29A5B`; ripple ring `#E9C88F`; bell ring `#FFF6E6`; edge arc `#E2BE85`, its inner arc `#FFF1D6`; a lit row's label `#A5A299`, its stick and note box borders `#76624C` |

Hover, open and selected states use only colours from this table.

## 6. Interactions
- **Knobs:** vertical drag; double-click resets to the default.
- **No mouse wheel:** the mouse wheel and trackpad scrolling change no value anywhere in the editor: not the knobs, the note boxes or the dropdowns. A value changes only by a deliberate click and drag, a menu choice or MIDI learn (and the host's automation of the five parameters). The wheel still scrolls the note list.
- **MIDI note box:** vertical drag changes the note. A click opens a menu: MIDI LEARN, RESET DEFAULT, NONE (unassign) and a scrollable list of every note, C-2 (0) to G8 (127), each with its number; the current note is outlined and in view. No separate Learn button is added.
- **Stick box:** where the preset has both stick versions, a click opens a TIP / SHOULDER popup. In every preset the bell and bow rows (BELL, BOW, BELL2, BOW2) have both; section 8 lists what each plays. EDGE is SHOULDER only in every preset: its box shows SHOULDER and opens nothing. Which rows offer the choice comes from the slot table (`StickDisplay::choice` in `plugin/SlotTable.h`). Each preset keeps its own choices: picking SHOULDER on a row in one preset leaves the other presets' rows as they are. The choices are saved with the session (section 9); they are not automatable parameters.
- **Dropdowns:** the preset dropdown is a list of NOVA RIDE, SMOKE FLAT RIDE, HAMMERED THIN RIDE and SWISH; the channel dropdown is a 4×4 grid.
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

Approved by Kris, 2026-10-25. UI only: it changes nothing in the sound and adds no parameter. Each strike shows as a short mark on the cymbal photo, where it landed, and the row that played it lights up while its mark lasts.

**From the audio thread to the editor.**
- At each strike that a note-on plays, the processor writes one event into a lock-free single-producer, single-consumer FIFO, preallocated with room for 64 events: the row, the strike position (0 edge, 0.5 bow, 1 bell, after the blend knobs), the engine velocity (after Velocity Sensitivity) and the stick (TIP or SHOULDER, as the row's stick box shows it). See `CymbalisProcessor::pushHit` and its call in `strikeSlots` (`plugin/PluginProcessor.cpp`).
- That write is all the audio thread does for the display. It never repaints, triggers an async update, posts a message, locks or allocates. When the FIFO is full the event is dropped. With no editor open nothing reads the FIFO, so it fills and later events are dropped; an editor that opens discards what waited.
- The editor reads the FIFO on its timer and keeps up to 16 active marks; when full, the oldest is dropped. The hit layer's own timer runs at 30 Hz only while a mark is active and stops when none is. While none is, the editor's existing 30 Hz timer (notes, MIDI learn) reads the FIFO, and a hit starts the layer's timer.

**Where a mark goes.**
- Each preset has a bell (or centre) zone as a fraction of the photo's radius, `bellRadius` in the slot table (`plugin/SlotTable.h`): NOVA RIDE 0.40, SMOKE FLAT RIDE 0.16 (centre zone), HAMMERED THIN RIDE 0.40, SWISH 0.22. Kris may adjust them.
- The photo's radius R = 0.495 × the fitted photo's width; the centre is the photo area's centre (787,390).
- Radius fraction from strike position p, linear between p = 1.0 → 0.6 × bellRadius, p = 0.5 → (bellRadius + 0.95) / 2, and p = 0.0 → 0.97.
- Angle: 4 to 5 o'clock, 45° clockwise from 3 o'clock plus a random offset uniform in ±12°. The editor draws the random number, never the audio thread.

**How a mark looks** (design units; v = engine velocity 0–1).
- Duration T = 0.25 + 0.25·v seconds. Intensity over age t: k = (1 − t/T)²; the mark is removed at t ≥ T.
- **Spot**, where the radius fraction is below 0.9:
  - Radial gradient: centre `#FFFFFF`, 12 % `#FFF1D6` at 0.95 alpha, 25 % `#E9C88F` at 0.75, 60 % `#C29A5B` at 0.28, edge `#C29A5B` at 0.
  - Radius r = (24 + 40·v) × (1.3 for SHOULDER, 1.0 for TIP), shrinking to 0.7·r at the end of T. Opacity (0.35 + 0.65·v)·k.
  - Ripple ring when v > 0.5: a circle around the spot, 2.5 px, `#E9C88F`, slightly soft (1.5 px edges), its radius growing from r to 2.6·r over T, opacity 0.35·v·k.
  - On the bell (radius fraction below bellRadius) also a crisp ring: 2.2 px, `#FFF6E6`, radius 0.45 × the spot's radius, opacity 0.85·k, so the mark shows on the bright bell.
- **Edge**, radius fraction 0.9 or more: an arc along the rim at the hit's angle and radius instead of a spot.
  - Span 20° + 28°·v, stroke 10 + 8·v px, `#E2BE85`, opacity (0.35 + 0.35·v)·k, soft edges of about 4 px.
  - When v > 0.5 also a 4 px inner arc along its middle, `#FFF1D6`, 0.6 of the span, opacity 0.5·k.
- Everything is clipped to the cymbal disc (centre, radius R) and drawn over the photo, under every control.

**Row highlight.** While a row's latest mark is active, the row's label is drawn in `#A5A299` and its stick and note box borders in `#76624C` (hover and open outlines still win). When the mark ends they return to their normal colours.

**Drawing cheaply.**
- No blur, gradient or path is computed per frame. Each mark shape is rendered once into small coloured images, at the current physical scale:
  - the spot, at radii 7 % apart covering the TIP and SHOULDER sizes;
  - the ripple ring and the bell ring, also at radii 7 % apart, each kept as the 32 px tiles it passes through;
  - the edge arc, at velocity 0, 0.2 … 1, with its inner arc.
- The images are rendered the first time a mark is drawn after the editor opens, is resized or moves to a screen with another scale (the arcs also after a preset change, since they follow R).
- A frame only places those images, with a position and an opacity:
  - spots and rings at the nearest radius, unscaled, on whole pixels;
  - arcs rotated about the disc's centre to the hit's angle and scaled to its radius fraction. The disc's edge is already in an arc's image, so arcs need no clip; spots and rings are clipped to the disc only when one reaches past it.
- Only the rectangle around each active mark is repainted (the spot and the ripple's current extent, with the previous frame's), plus a lit row's label area when it changes; never the whole editor or the whole photo.
- The photo area is its own opaque component, cached as an image, so a mark's repaint copies the photo from that image.
