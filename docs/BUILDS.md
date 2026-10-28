# Builds and installation

## Builds

Every push to `main` runs **Actions → Build macOS (VST3 + AU)**. It builds one plugin, **Cymbalis**, with everything: the editor, and the TUNING button and Tuning window (`docs/TUNING.md`). It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11
- JUCE 7.0.12, built with Xcode 15
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta)
- An editor mouse test in an AU and a VST3 host (`tools/cymbalis_uitest`): real mouse events change the MIDI channel, notes, a stick, the preset and a stick of that preset, checked in the plugin's saved state, and the scroll wheel over the knobs and boxes must change nothing; then a real click on TUNING must show the Tuning window, visible and in front of the editor and attached to its window, the wheel over its sliders and numeric boxes must change nothing, and a second click must hide it
- A hit display timing (`CymbalisUiTest hits`): processBlock at 44.1 kHz, with 256- and 128-sample blocks and the audio thread at normal priority and real-time, with the editor closed, open and idle, and open with 16 hits moving the wireframe cymbal, each case three times, then the three cases once more with HAMMERED THIN RIDE selected and once with SWISH (the heaviest cymbals to draw: their dented meshes, with the size of each cymbal and how long it took to build); with the CPU used besides, the editor's paint calls and the cymbal's OpenGL frames, and once a check that nothing is drawn after every hit is older than 6 s; printed to the log and the job summary. The runner's Mac has no GPU: it draws OpenGL with the Apple Software Renderer (the step prints the renderer), so its OpenGL frame times are a CPU's, not a GPU's
- Ad-hoc code signing

The run's **Artifacts** section has one artifact, `Cymbalis-macOS-<shortsha>`, with two zips and nothing else: `Cymbalis-VST3-macOS-<shortsha>.zip` and `Cymbalis-AU-macOS-<shortsha>.zip`.

The job summary also shows the CPU cost of the engine on the runner for each preset (`cymbalis_bench`, every slot playing; it fails if the output is not finite), and its block times in dense playing (`cymbalis_bench --dense`: a strike every 24 ms, every preset at 44.1 and 96 kHz with 128- and 64-sample blocks on a real-time thread, each case three times; then `cymbalis_bench_profile`, the time of the engine's parts; docs/PORT_MAP.md, "Cost of a strike"). The runner is a virtual machine: a block that is slow in one run only is the machine's, not the engine's, which is why the dense mode reports the smallest of the three runs per block. Pushing a `v*` tag also creates a GitHub Release with the two zips attached.

**Cymbalis Tuning** (plugin code `CymT`), the separate tuning build of earlier versions, is no longer built; Cymbalis now does everything it did. Its plugin ID differs from Cymbalis's, so a host does not swap one for the other: to carry tuning from a Cymbalis Tuning session over, **SAVE...** it there as a tuning file and **LOAD...** it in Cymbalis. The old Cymbalis Tuning bundles can then be deleted.

## Playing it

The plugin is an instrument. Note-ons on MIDI channel 16 (selectable in the editor) strike the slots: BELL C3 (60), BELL2 C#3 (61), BOW D3 (62), BOW2 D#3 (63), EDGE E3 (64). Note-off and sustain are ignored. Click a note box to pick a note from the list, learn one from MIDI, reset it or unassign it; click a stick box to choose TIP or SHOULDER (NOVA RIDE; the other presets' sticks are fixed). `docs/UI_SPEC.md` describes the controls.

## The compiled-in presets

The plugin plays five presets, baked from the Python engine's data with NL off. Choose one in the CYMBAL box:

| Preset | Bake | Source |
|---|---|---|
| NOVA RIDE | `resources/presets/nova_ride.bake.json` | `presets/nova/` |
| SMOKE FLAT RIDE | `resources/presets/smoke_flat_ride.bake.json` | `presets/602/` |
| HAMMERED THIN RIDE | `resources/presets/hammered_thin_ride.bake.json` | `presets/bettis/` |
| SWISH | `resources/presets/swish.bake.json` | `presets/swish/` |
| RAVEN | `resources/presets/raven.bake.json` | `presets/raven/` |

The JSON bakes are the source: `tools/bake_preset.py`, `tools/apply_tuning.py` and the tuning files read and write them. The plugin embeds each bake in a packed form instead (in its binary, through BinaryData): when CMake builds the plugin's binary data, `tools/pack_bake` packs every bake into `<build>/presets/<preset>.bake.pack`. A packed bake (`plugin/PackedBake.h`) is the bake's JSON tree in binary, compressed with zlib: every number as its exact double (or int32 where it is exactly one), so the plugin loads exactly the values the JSON gives. It carries a format version and an FNV-1a checksum; a packed preset that does not match them fails to load with a message, like a bake with another format version. The tool checks each packed bake before writing it: it must unpack to the same JSON, bit for bit, and load as a bake, or the build stops.

After a change to a preset's data, regenerate its bake (`--preset nova`, `602`, `bettis`, `swish` or `raven`):

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out resources/presets/smoke_flat_ride.bake.json
python tools/bake_preset.py --preset bettis --overrides '{"nl_amount": 0.0}' --out resources/presets/hammered_thin_ride.bake.json
python tools/bake_preset.py --preset swish --overrides '{"nl_amount": 0.0}' --out resources/presets/swish.bake.json
python tools/bake_preset.py --preset raven --overrides '{"nl_amount": 0.0}' --out resources/presets/raven.bake.json
```

Each bake also holds its slots' level trims, in `slots[].level_trim.gain_db`. The plugin uses only that number; edit it to adjust a slot's level by ear. Re-baking drops the trims. Nova Ride's trims give a velocity-0.9 hit at each slot's base position the same 1-second RMS as one on BOW (tip). The 602, Bettis, Swish and Raven trims work at velocity 1.0 over the first 500 ms. BOW, the 602's CENTER (a copy of its BOW) and Swish's BOW_SHOULDER are matched to Nova's BOW (tip); Swish's and Raven's BELL_SHOULDER take BELL's SPEAR target. The shoulder copies of the 602, Bettis and Raven, and Nova's EDGE (a copy of BOW2), start with their sources' trims. EDGE and BELL go to the level their SPEAR captures have relative to BOW, capped where one hit would peak above 0.9 before the output gain and the Volume knob (`docs/PORT_MAP.md`, "602 and Bettis", decision 5). To measure trims again with the plugin's engine and store them (`--peak-limit 0.25365` is that 0.9 after the −11 dB output gain, see "Headroom" below):

```
cmake --build build --target cymbalis_trims
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/smoke_flat_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=2.8 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/smoke_flat_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/hammered_thin_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=2.7 --spear-target EDGE=14.2 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/hammered_thin_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/swish.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=5.3 --spear-target BELL_SHOULDER=5.3 --spear-target EDGE=4.7 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/swish.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/raven.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=4.72 --spear-target BELL_SHOULDER=4.72 --spear-target EDGE=16.23 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/raven.bake.json trims.json
```

A new preset gets its tuning block (the output gain below, and any velocity curve) before its trims are measured: the trims tool measures the reference, Nova, with its −11 dB, so a bake without it would come out 11 dB too quiet once the block is added (Raven, `docs/PORT_MAP.md`, "Raven").

**Headroom (2026-10-25).** The trims match single hits to Nova's BOW (tip) with no headroom, and hits add up while the cymbal rings, so a host's master clipped in normal playing. Every preset therefore has the same cymbal output gain, −11 dB (`output_gain_db` in its tuning block, set with `tools/apply_tuning.py`); the trims, velocity curves and everything else are unchanged. RAVEN, added later (2026-10-27), has the same −11 dB; it is not in the table below. Measured in memory through the plugin at 44.1 kHz, Volume 0 dB, velocity 1.0, with a stress pattern (8 s of BOW (tip) eighth notes at 120 BPM with EDGE on every beat 1, then 4 s of EDGE alone on every quarter note), first run after loading:

| Preset | Stress peak before | After −11 dB | Worst of 16 phase sequences, after | Loudest single hit (worst of 128), before → after |
|---|---|---|---|---|
| NOVA RIDE | −1.2 dBFS | −12.2 dBFS | −12.2 dBFS | +1.6 → −9.4 dBFS (BELL2) |
| SMOKE FLAT RIDE | +4.2 dBFS | −6.8 dBFS | −6.8 dBFS | +0.2 → −10.8 dBFS (BOW, CENTER and their shoulders) |
| HAMMERED THIN RIDE | +5.1 dBFS | −6.0 dBFS | −2.2 dBFS | +1.9 → −9.1 dBFS (EDGE) |
| SWISH | +7.4 dBFS | −3.6 dBFS | −3.0 dBFS | +3.6 → −7.4 dBFS (BOW tip: 1.51 → 0.43) |

−11 dB is the offset that puts the loudest stress peak (Swish, +7.35 dBFS) at −3 dBFS (−10.35 dB), rounded down to a whole dB. Each strike draws its own random phases, so a peak varies from run to run; Hammered Thin Ride's varies most. `tools/cymbalis_trims` measures with the bake's tuning block, output gain included. Its RMS matching is unaffected, since every preset has the same gain, but its peak limit now applies after the −11 dB, hence 0.25365 (0.9 × 10^(−11/20)) in the commands above, which gives the same trims as 0.9 did before.

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
