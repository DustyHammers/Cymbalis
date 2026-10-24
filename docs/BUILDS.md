# Builds and installation

## Builds

Every push to `main` runs **Actions → Build macOS (VST3 + AU)**. It builds one plugin, **Cymbalis**, with everything: the editor, and the TUNING button and Tuning window (`docs/TUNING.md`). It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11
- JUCE 7.0.12, built with Xcode 15
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta)
- An editor mouse test in an AU and a VST3 host (`tools/cymbalis_uitest`): real mouse events change the MIDI channel, notes, a stick, the preset and a stick of that preset, checked in the plugin's saved state, and the scroll wheel over the knobs and boxes must change nothing; then a real click on TUNING must show the Tuning window, visible and in front of the editor and attached to its window, the wheel over its sliders and numeric boxes must change nothing, and a second click must hide it
- Ad-hoc code signing

The run's **Artifacts** section has one artifact, `Cymbalis-macOS-<shortsha>`, with two zips and nothing else: `Cymbalis-VST3-macOS-<shortsha>.zip` and `Cymbalis-AU-macOS-<shortsha>.zip`.

The job summary also shows the CPU cost of the engine on the runner for each preset (`cymbalis_bench`, every slot playing; it fails if the output is not finite). Pushing a `v*` tag also creates a GitHub Release with the two zips attached.

**Cymbalis Tuning** (plugin code `CymT`), the separate tuning build of earlier versions, is no longer built; Cymbalis now does everything it did. Its plugin ID differs from Cymbalis's, so a host does not swap one for the other: to carry tuning from a Cymbalis Tuning session over, **SAVE...** it there as a tuning file and **LOAD...** it in Cymbalis. The old Cymbalis Tuning bundles can then be deleted.

## Playing it

The plugin is an instrument. Note-ons on MIDI channel 16 (selectable in the editor) strike the slots: BELL C3 (60), BELL2 C#3 (61), BOW D3 (62), BOW2 D#3 (63), EDGE E3 (64). Note-off and sustain are ignored. Click a note box to pick a note from the list, learn one from MIDI, reset it or unassign it; click a stick box to choose TIP or SHOULDER (NOVA RIDE; the other presets' sticks are fixed). `docs/UI_SPEC.md` describes the controls.

## The compiled-in presets

The plugin plays four presets, baked from the Python engine's data with NL off. Choose one in the CYMBAL box:

| Preset | Bake | Source |
|---|---|---|
| NOVA RIDE | `resources/presets/nova_ride.bake.json` | `presets/nova/` |
| SMOKE FLAT RIDE | `resources/presets/smoke_flat_ride.bake.json` | `presets/602/` |
| HAMMERED THIN RIDE | `resources/presets/hammered_thin_ride.bake.json` | `presets/bettis/` |
| SWISH | `resources/presets/swish.bake.json` | `presets/swish/` |

The JSON bakes are the source: `tools/bake_preset.py`, `tools/apply_tuning.py` and the tuning files read and write them. The plugin embeds each bake in a packed form instead (in its binary, through BinaryData): when CMake builds the plugin's binary data, `tools/pack_bake` packs every bake into `<build>/presets/<preset>.bake.pack`. A packed bake (`plugin/PackedBake.h`) is the bake's JSON tree in binary, compressed with zlib: every number as its exact double (or int32 where it is exactly one), so the plugin loads exactly the values the JSON gives. It carries a format version and an FNV-1a checksum; a packed preset that does not match them fails to load with a message, like a bake with another format version. The tool checks each packed bake before writing it: it must unpack to the same JSON, bit for bit, and load as a bake, or the build stops.

After a change to a preset's data, regenerate its bake (`--preset nova`, `602`, `bettis` or `swish`):

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out resources/presets/smoke_flat_ride.bake.json
python tools/bake_preset.py --preset bettis --overrides '{"nl_amount": 0.0}' --out resources/presets/hammered_thin_ride.bake.json
python tools/bake_preset.py --preset swish --overrides '{"nl_amount": 0.0}' --out resources/presets/swish.bake.json
```

Each bake also holds its slots' level trims, in `slots[].level_trim.gain_db`. The plugin uses only that number; edit it to adjust a slot's level by ear. Re-baking drops the trims. Nova Ride's trims give a velocity-0.9 hit at each slot's base position the same 1-second RMS as one on BOW (tip). The 602, Bettis and Swish trims work at velocity 1.0 over the first 500 ms. BOW, the 602's CENTER (a copy of its BOW) and Swish's BOW_SHOULDER are matched to Nova's BOW (tip); Swish's BELL_SHOULDER takes BELL's SPEAR target. The shoulder copies of the 602 and Bettis, and Nova's EDGE (a copy of BOW2), start with their sources' trims. EDGE and BELL go to the level their SPEAR captures have relative to BOW, capped where one hit would peak above 0.9 before the Volume knob (`docs/PORT_MAP.md`, "602 and Bettis", decision 5). To measure trims again with the plugin's engine and store them:

```
cmake --build build --target cymbalis_trims
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/smoke_flat_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=2.8 --peak-limit 0.9 --json trims.json
python tools/set_level_trims.py resources/presets/smoke_flat_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/hammered_thin_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=2.7 --spear-target EDGE=14.2 --peak-limit 0.9 --json trims.json
python tools/set_level_trims.py resources/presets/hammered_thin_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/swish.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=5.3 --spear-target BELL_SHOULDER=5.3 --spear-target EDGE=4.7 --peak-limit 0.9 --json trims.json
python tools/set_level_trims.py resources/presets/swish.bake.json trims.json
```

## Installing a CI build

1. Unzip.
2. Move `Cymbalis.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `Cymbalis.component` to `~/Library/Audio/Plug-Ins/Components/`.
3. Clear the download quarantine flag once per build, because the builds are not notarized yet:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Cymbalis.vst3
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Cymbalis.component
   ```

4. Rescan plugins in your DAW. For Logic, you may need to run `killall -9 AudioComponentRegistrar` first.

## Local build (Mac)

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target Cymbalis_VST3 Cymbalis_AU
```

The CMake option `CYMBALIS_WITH_TUNING` (ON by default) builds the tuning code in: the TUNING button, the Tuning window, live tuning, tuning files and tuning in the session. `-DCYMBALIS_WITH_TUNING=OFF` builds a lean Cymbalis without any of it, under the same identity. It plays the same bakes the same way; tuning stored in a session is ignored when it loads one and is not saved with it. CI builds only the full plugin.

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Product name | Cymbalis |
| Manufacturer code | `Dham` |
| Plugin code | `Cymb` |
| Bundle ID | `com.dhammers.cymbalis` |

These values are set in `CMakeLists.txt`. Changing a code changes the plugin ID that DAWs store.
