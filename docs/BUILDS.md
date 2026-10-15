# Builds and installation

## Builds

Every push to `main` runs **Actions → Build macOS (VST3 + AU)**. It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11
- JUCE 7.0.12, built with Xcode 15
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta)
- An editor mouse test in an AU and a VST3 host (`tools/cymbalis_uitest`): real mouse events change the MIDI channel, notes, a stick and the preset, checked in the plugin's saved state
- Ad-hoc code signing

The run's **Artifacts** section has one artifact, `Cymbalis-macOS-<shortsha>`, with:

- `Cymbalis-VST3-macOS-<shortsha>.zip` and `Cymbalis-AU-macOS-<shortsha>.zip`, and nothing else

The job summary also shows the CPU cost of the engine on the runner for each preset (`cymbalis_bench`, every slot playing; it fails if the output is not finite). Pushing a `v*` tag also creates a GitHub Release with the zips attached.

## Playing it

The plugin is an instrument. Note-ons on MIDI channel 16 (selectable in the editor) strike the slots: BELL C3 (60), BELL2 C#3 (61), BOW D3 (62), BOW2 D#3 (63); EDGE has no note until one is assigned. Note-off and sustain are ignored. Click a note box to pick a note from the list, learn one from MIDI, reset it or unassign it; click a stick box to choose TIP or SHOULDER (NOVA RIDE; the other presets' sticks are fixed). `docs/UI_SPEC.md` describes the controls.

## The compiled-in presets

The plugin plays three presets, baked from the Python engine's data with NL off. Choose one in the CYMBAL box:

| Preset | Bake | Source |
|---|---|---|
| NOVA RIDE | `resources/presets/nova_ride.bake.json` | `presets/nova/` |
| SMOKE FLAT RIDE | `resources/presets/smoke_flat_ride.bake.json` | `presets/602/` |
| HAMMERED THIN RIDE | `resources/presets/hammered_thin_ride.bake.json` | `presets/bettis/` |

After a change to a preset's data, regenerate its bake (`--preset nova`, `602` or `bettis`):

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out resources/presets/smoke_flat_ride.bake.json
python tools/bake_preset.py --preset bettis --overrides '{"nl_amount": 0.0}' --out resources/presets/hammered_thin_ride.bake.json
```

Each bake also holds its slots' level trims, in `slots[].level_trim.gain_db`. The plugin uses only that number; edit it to adjust a slot's level by ear. Re-baking drops the trims. A trim is the gain that gives a velocity-0.9 hit at the slot's base position the same 1-second RMS as one on Nova's BOW (tip), so all presets sit at the same level at forte. To measure trims again with the plugin's engine and store them:

```
cmake --build build --target cymbalis_trims
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/smoke_flat_ride.bake.json --reference resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/smoke_flat_ride.bake.json trims.json
```

(and the same for `hammered_thin_ride`).

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

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Manufacturer code | `Dham` |
| Plugin code | `Cymb` |
| Bundle ID | `com.dhammers.cymbalis` |

These values are set in `CMakeLists.txt`. Changing a code changes the plugin ID that DAWs store.
