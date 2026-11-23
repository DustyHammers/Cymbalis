# Builds and installation

## Builds

Every push to `main` runs **Actions → Build macOS (VST3 + AU)**. It builds one plugin, **Cymbalis**, with everything: the editor, and the Tuning window, opened by a click on the CYMBALIS title (`docs/TUNING.md`). It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11: the build fails if either slice of the VST3 or the AU has another minimum (`LC_BUILD_VERSION` minos)
- JUCE 7.0.12, built on GitHub's `macos-15` runner with Xcode 16.4 (macOS 15.5 SDK), pinned (see "The runner and Xcode" below)
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta; the step installs Rosetta if the runner lacks it)
- An editor mouse test in an AU and a VST3 host (`tools/cymbalis_uitest`): real mouse events change the MIDI channel, notes, a stick, the preset and a stick of that preset, checked in the plugin's saved state, and the scroll wheel over the knobs and boxes must change nothing; on HI-HAT, its Blend Position knobs must be hidden (and shown again on KONSTAN), MIDI notes 42 and 46 and the pedal CC played into the plugin through the host must sound (an open hit ringing on, a FOOT when the pedal closes), the hats and the pedal drawing must follow the pedal CC (their accessibility descriptions give their opening), also with MIDI CHANNEL 16 and the CC sent on channel 1, as a keyboard's mod wheel (the pedal CC is read on every channel), where a sweep from closed to open and back must play one FOOT, on the close; a BOW hit with the pedal closed and one with it open must swing the top cymbal at 15 % and all of its wobble, the bottom cymbal not at all (the hats' last frame, through their accessibility description), a pedal CC picked in the PEDAL box must be saved, and restored with the state, and its Tuning window must show the values that act on it and hide the others; dragging, double-clicking and automating each of the five knobs must move its parameter and the value the knob shows (its accessibility value), which its position arc is drawn from (`docs/UI_SPEC.md` section 6); then a real click on the CYMBALIS title must show the Tuning window, visible and in front of the editor and attached to its window, the wheel over its sliders and numeric boxes must change nothing, and a second click must hide it (on KONSTAN CRASH 18" and on HI-HAT). The Tuning window opens below the editor's top right; on a screen too narrow for it to stay clear of the editor's CYMBAL box (the `macos-15` runner's), it covers it, and JUCE ignores a click where another window is in front, as it would a user's. The test then moves the Tuning window aside before each such click, as a user would, puts it back after it, and says so in the log, which also gives the editor's and the screen's size
- A hit display timing (`CymbalisUiTest hits`): processBlock at 44.1 kHz, with 256- and 128-sample blocks and the audio thread at normal priority and real-time, with the editor closed, open and idle, and open with 16 hits moving the wireframe cymbal, each case three times, then the three cases once more with HAMMERED THIN RIDE selected, and once each with SWISH, RAVEN and KONSTAN CRASH 18" (the heaviest cymbals to draw: their dented meshes, with the size of each cymbal and how long it took to build) and HI-HAT (its two dented cymbals and its clutch and rod, with its notes 42 and 46 and its pedal CC sweeping from closed to open and back); with the CPU used besides, the editor's paint calls and the cymbal's OpenGL frames, and once a check that nothing is drawn after every hit is older than 6 s; printed to the log and the job summary. The runner's Mac has no GPU: it draws OpenGL with the Apple Software Renderer (the step prints the renderer), so its OpenGL frame times are a CPU's, not a GPU's
- Ad-hoc code signing

The run's **Artifacts** section has one artifact, `Cymbalis-macOS-<shortsha>`, with two zips and nothing else: `Cymbalis-VST3-macOS-<shortsha>.zip` and `Cymbalis-AU-macOS-<shortsha>.zip`.

The job summary also shows the CPU cost of the engine on the runner for each preset (`cymbalis_bench`, every slot playing, and on HI-HAT the pedal moving: a CC every 10 ms, open and closed every 1.5 s; it fails if the output is not finite), and its block times in dense playing (`cymbalis_bench --dense`: a strike every 24 ms, HI-HAT's pedal moving too, every preset at 44.1 and 96 kHz with 128- and 64-sample blocks on a real-time thread, each case three times; then `cymbalis_bench_profile`, the time of the engine's parts; docs/PORT_MAP.md, "Cost of a strike"). The runner is a virtual machine: a block that is slow in one run only is the machine's, not the engine's, which is why the dense mode reports the smallest of the three runs per block. Pushing a `v*` tag also creates a GitHub Release with the two zips attached.

If Configure or Build fails, its error lines also appear as the job's annotations (`.github/scripts/annotate_errors.sh`), and Configure prints the toolchain (CMake, Xcode, SDK, deployment target) as a notice, so a failure can be read without the job's log.

### The runner and Xcode

GitHub retires the `macos-14` runner images on 2026-11-02, with scheduled brownouts before that ([changelog](https://github.blog/changelog/2026-10-01-github-actions-macos-14-runner-image-retirement)), so the build runs on `macos-15` (2026-10-31). That image has no Xcode 15: the build selects `/Applications/Xcode_16.4.app` (`XCODE_VERSION` in the workflow) and fails if the image doesn't have it, rather than taking the image's default, which may move to Xcode 26.

JUCE 7.0.12 predates the macOS 15 SDK, and the plugin stays on it (JUCE 8 is a licence decision). Its helper program juceaide is built during configure in a CMake build of JUCE's own, which doesn't get the deployment target from `CMakeLists.txt`. Without one, juceaide targets the SDK's macOS 15.5, where `CGWindowListCreateImage` (in JUCE's macOS windowing code) is obsoleted and fails to compile; at macOS 11 it is only deprecated. The job therefore sets `MACOSX_DEPLOYMENT_TARGET=11.0`, which that build's CMake and clang take as their deployment target. The plugin's own targets get 11.0 from `CMakeLists.txt` as before. The JUCE cache (`.deps`, which holds juceaide's build) is keyed by the Xcode version, so a cache made with another Xcode isn't reused.

**Cymbalis Tuning** (plugin code `CymT`), the separate tuning build of earlier versions, is no longer built; Cymbalis now does everything it did. Its plugin ID differs from Cymbalis's, so a host does not swap one for the other: to carry tuning from a Cymbalis Tuning session over, **SAVE...** it there as a tuning file and **LOAD...** it in Cymbalis. The old Cymbalis Tuning bundles can then be deleted.

## Playing it

The plugin is an instrument. Note-ons on MIDI channel 16 (selectable in the editor) strike the slots: BELL C3 (60), BELL2 C#3 (61), BOW D3 (62), BOW2 D#3 (63), EDGE E3 (64). Note-off and sustain are ignored. Click a note box to pick a note from the list, learn one from MIDI, reset it or unassign it; click a stick box to choose TIP or SHOULDER (NOVA RIDE; the other presets' sticks are fixed). `docs/UI_SPEC.md` describes the controls.

## The compiled-in presets

The plugin plays nine presets: eight baked from the Python engine's data with NL off, and HI-HAT, baked from its captures by its own tool (it is not a V19 preset). Choose one in the CYMBAL box:

| Preset | Bake | Source |
|---|---|---|
| NOVA RIDE | `resources/presets/nova_ride.bake.json` | `presets/nova/` |
| SMOKE FLAT RIDE | `resources/presets/smoke_flat_ride.bake.json` | `presets/602/` |
| HAMMERED THIN RIDE | `resources/presets/hammered_thin_ride.bake.json` | `presets/bettis/` |
| SWISH | `resources/presets/swish.bake.json` | `presets/swish/` |
| RAVEN | `resources/presets/raven.bake.json` | `presets/raven/` |
| EXTRA PAPER THIN 24" | `resources/presets/paperthin.bake.json` | `presets/paperthin/` |
| KONSTAN CRASH 18" | `resources/presets/konstan.bake.json` | `presets/konstan/` |
| CRASH 16" | `resources/presets/azcrash.bake.json` | `presets/azcrash/` |
| HI-HAT | `resources/presets/hihat.bake.json` | `presets/hihat/` (`tools/bake_hihat.py`) |

The JSON bakes are the source: `tools/bake_preset.py`, `tools/apply_tuning.py` and the tuning files read and write them. The plugin embeds each bake in a packed form instead (in its binary, through BinaryData): when CMake builds the plugin's binary data, `tools/pack_bake` packs every bake into `<build>/presets/<preset>.bake.pack`. A packed bake (`plugin/PackedBake.h`) is the bake's JSON tree in binary, compressed with zlib: every number as its exact double (or int32 where it is exactly one), so the plugin loads exactly the values the JSON gives. It carries a format version and an FNV-1a checksum; a packed preset that does not match them fails to load with a message, like a bake with another format version. The tool checks each packed bake before writing it: it must unpack to the same JSON, bit for bit, and load as a bake, and SMOKE FLAT RIDE, SWISH and KONSTAN CRASH 18" must give EDGE VOICE FROM BOW's check values at 100 % (`docs/TUNING.md`), or the build stops. Re-baking one of them from new data calls for that table to be updated (in `tools/pack_bake/main.cpp` and the doc).

After a change to a preset's data, regenerate its bake (`--preset nova`, `602`, `bettis`, `swish`, `raven`, `paperthin`, `konstan` or `azcrash`). KONSTAN's, EXTRA PAPER THIN 24"'s and CRASH 16"'s T60s are baked without V19's T60 cap (`T60_UNCAPPED` in `tools/bake_preset.py`; `docs/PORT_MAP.md`, "Konstan Crash 18"", decision 5, "Extra Paper Thin Ride 24"" and "Crash 16""); the other presets keep it:

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out resources/presets/smoke_flat_ride.bake.json
python tools/bake_preset.py --preset bettis --overrides '{"nl_amount": 0.0}' --out resources/presets/hammered_thin_ride.bake.json
python tools/bake_preset.py --preset swish --overrides '{"nl_amount": 0.0}' --out resources/presets/swish.bake.json
python tools/bake_preset.py --preset raven --overrides '{"nl_amount": 0.0}' --out resources/presets/raven.bake.json
python tools/bake_preset.py --preset paperthin --overrides '{"nl_amount": 0.0}' --out resources/presets/paperthin.bake.json
python tools/bake_preset.py --preset konstan --overrides '{"nl_amount": 0.0}' --out resources/presets/konstan.bake.json
python tools/bake_preset.py --preset azcrash --overrides '{"nl_amount": 0.0}' --out resources/presets/azcrash.bake.json
```

**The hi-hat's bake** comes from `tools/bake_hihat.py` (numpy and scipy; the same environment): it runs the approved strike model's preparation (`docs/hihat_reference.py`: the stick envelopes, each knot's level per band, the band decay rates; `docs/PORT_MAP.md`, "Hi-hat") on `presets/hihat/`, checks those files against `docs/BASELINE_MANIFEST.md`, and writes the plugin's bake format with a `hihat` block, its entries TIP, EDGE and FOOT with their stick clicks, and its tuning block (output gain −11 dB, HIT VARIATION on), which it writes itself. The plugin's engine does the per-strike interpolation. Then its trims, as below:

```
python tools/bake_hihat.py --out resources/presets/hihat.bake.json
```

Each bake also holds its slots' level trims, in `slots[].level_trim.gain_db`. The plugin uses only that number; edit it to adjust a slot's level by ear. Re-baking drops the trims. Nova Ride's trims give a velocity-0.9 hit at each slot's base position the same 1-second RMS as one on BOW (tip). The 602, Bettis, Swish, Raven, Paper Thin, Konstan and Crash 16 trims work at velocity 1.0 over the first 500 ms. BOW, the 602's CENTER (a copy of its BOW) and Swish's BOW_SHOULDER are matched to Nova's BOW (tip); Swish's, Raven's, Paper Thin's, Konstan's and Crash 16's BELL_SHOULDER take BELL's SPEAR target. The shoulder copies of the 602, Bettis, Raven, Paper Thin, Konstan and Crash 16, and Nova's EDGE (a copy of BOW2), start with their sources' trims. Nova's EDGE plays an NPZ edge since 2026-11-23: its trim, +12.20 dB, gives a velocity-1.0 hit the RMS over 500 ms the copy of BOW2 had (`docs/PORT_MAP.md`, "Nova Ride's NPZ edge"). The Nova command below would replace it with a BOW match at velocity 0.9; keep +12.20 dB. EDGE and BELL go to the level their SPEAR captures have relative to BOW, capped where one hit would peak above 0.9 before the output gain and the Volume knob (`docs/PORT_MAP.md`, "602 and Bettis", decision 5). To measure trims again with the plugin's engine and store them (`--peak-limit 0.25365` is that 0.9 after the −11 dB output gain, see "Headroom" below):

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
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/paperthin.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=8.69 --spear-target BELL_SHOULDER=8.69 --spear-target EDGE=15.77 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/paperthin.bake.json trims.json --boost EDGE=4 --boost-reason 'Kris, 2026-11-21: louder at high velocity, without clipping. +4 dB on the capped trim (+5 dB asked for, lowered in 0.5 dB steps until one EDGE hit and a dense run peaked below 0.9 after the output gain); the EDGE velocity curve, -4 dB at 0.6 and 0.3, takes it off at 0.6 and below (docs/PORT_MAP.md, Extra Paper Thin, decision 6)'
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/konstan.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=4.72 --spear-target BELL_SHOULDER=4.72 --spear-target EDGE=13.5 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/konstan.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/azcrash.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target BELL=-0.7 --spear-target BELL_SHOULDER=-0.7 --spear-target EDGE=18.1 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/azcrash.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/hihat.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=0 --peak-limit 0.25365 --same-as TIP=EDGE --same-as FOOT=EDGE --json trims.json
python tools/set_level_trims.py resources/presets/hihat.bake.json trims.json
```

HI-HAT's trims are measured with the hats closed: EDGE matched to Nova's BOW (tip), capped at the 0.9 peak (−3.85 dB, 9.9 dB under BOW: the cap holds it), and TIP and FOOT take EDGE's trim (`--same-as`), so they keep their captured levels relative to EDGE (`docs/PORT_MAP.md`, "Hi-hat").

EXTRA PAPER THIN 24"'s EDGE is boosted after its trims are measured (`docs/PORT_MAP.md`, "Extra Paper Thin Ride 24"", decision 6; Kris, 2026-11-21): `--boost EDGE=4` stores its trim 4 dB above the capped one (+5.19 → +9.19 dB), and its tuning block gives it the velocity curve that takes the 4 dB back off at velocity 0.6 and below (`vel_0.6_db` and `vel_0.3_db` −4.0), so the extra level comes in between 0.6 and 1.0. `tools/set_level_trims.py` refuses the boost without that curve, so apply the tuning file first, as for every new preset (below). The boost is what keeps one EDGE hit at velocity 1.0 and a dense run of EDGE and BOW hits below a peak of 0.9 at the plugin's output (0.40 and 0.86, 99th percentiles; Kris asked for +5 dB, which took the dense run to 0.95). Its `level_trim` records the capped trim, the boost and why. Measuring the trims again drops the boost unless the command gives it again.

CRASH 16"'s BELL target, −0.7 dB against BOW, is not the level of its loudest bell capture against the loudest bow, as the others' are: its only bell capture is soft (Bell V1, −9.66 dB against Bow V2, about level with the soft Bow V1), so the target is the level the renders Kris heard had (`docs/BASELINE_MANIFEST.md`, Crash 16"). BELL reaches it uncapped (+16.81 dB); EDGE is capped at +1.02 dB, 2.56 dB under BOW (`docs/PORT_MAP.md`, "Crash 16"").

A new preset gets its tuning block (the output gain below, and any velocity curve) before its trims are measured: the trims tool measures the reference, Nova, with its −11 dB, so a bake without it would come out 11 dB too quiet once the block is added (Raven, `docs/PORT_MAP.md`, "Raven"; Konstan, "Konstan Crash 18""; Paper Thin, "Extra Paper Thin Ride 24""; Crash 16, "Crash 16"").

The tuning block also holds EDGE VOICE FROM BOW (`edge_voice_from_bow`, `docs/TUNING.md`), which changes an edge strike's level, and SWISH's edge click and attack, so EDGE is measured with them in place. KONSTAN CRASH 18"'s EDGE trim was measured with the value at 100 % (2026-10-29), with the commands above, and only EDGE's trim was stored; after its T60 refit (2026-10-31) every KONSTAN trim was measured again, with the tuning block in place. SMOKE FLAT RIDE is at 0 % (Kris, 2026-10-31), and its bake is main's, unchanged. SWISH's EDGE trim was left as it was (measured again it would be −3.88 dB instead of −3.44, under 1 dB). A new value in the block calls for EDGE to be measured again.

Every preset's tuning block also switches HIT VARIATION on (`hit_variation`, with the default spreads, 2026-10-31; `docs/TUNING.md`, "Hit variation"), which moves each strike's position and velocity a little. A trim is the level at the slot's own position, so `tools/cymbalis_trims` switches HIT VARIATION off while it measures, and the trims did not change. On average over many strikes the variation leaves the rows within 0.3 dB of their trims, except the bell rows, which it makes louder: KONSTAN's +3.9 dB, NOVA's BELL2 +1.8 dB and BELL +1.0 dB, RAVEN's +0.75 dB (`docs/PORT_MAP.md`, live-engine decision 22).

**Headroom (2026-10-25).** The trims match single hits to Nova's BOW (tip) with no headroom, and hits add up while the cymbal rings, so a host's master clipped in normal playing. Every preset therefore has the same cymbal output gain, −11 dB (`output_gain_db` in its tuning block, set with `tools/apply_tuning.py`); the trims, velocity curves and everything else are unchanged. RAVEN, KONSTAN CRASH 18", EXTRA PAPER THIN 24" and CRASH 16", added later (2026-10-27, 2026-10-28, 2026-11-21 and 2026-11-22), have the same −11 dB; they are not in the table below. Measured in memory through the plugin at 44.1 kHz, Volume 0 dB, velocity 1.0, with a stress pattern (8 s of BOW (tip) eighth notes at 120 BPM with EDGE on every beat 1, then 4 s of EDGE alone on every quarter note), first run after loading:

| Preset | Stress peak before | After −11 dB | Worst of 16 phase sequences, after | Loudest single hit (worst of 128), before → after |
|---|---|---|---|---|
| NOVA RIDE | −1.2 dBFS | −12.2 dBFS | −12.2 dBFS | +1.6 → −9.4 dBFS (BELL2) |
| SMOKE FLAT RIDE | +4.2 dBFS | −6.8 dBFS | −6.8 dBFS | +0.2 → −10.8 dBFS (BOW, CENTER and their shoulders) |
| HAMMERED THIN RIDE | +5.1 dBFS | −6.0 dBFS | −2.2 dBFS | +1.9 → −9.1 dBFS (EDGE) |
| SWISH | +7.4 dBFS | −3.6 dBFS | −3.0 dBFS | +3.6 → −7.4 dBFS (BOW tip: 1.51 → 0.43) |

−11 dB is the offset that puts the loudest stress peak (Swish, +7.35 dBFS) at −3 dBFS (−10.35 dB), rounded down to a whole dB. Each strike draws its own random phases, so a peak varies from run to run; Hammered Thin Ride's varies most. `tools/cymbalis_trims` measures with the bake's tuning block, output gain included. Its RMS matching is unaffected, since every preset has the same gain, but its peak limit now applies after the −11 dB, hence 0.25365 (0.9 × 10^(−11/20)) in the commands above, which gives the same trims as 0.9 did before.

## The compiled-in fonts

The plugin carries its fonts in its binary data (`CYMBALIS_RESOURCES` in `CMakeLists.txt`); each font's licence file stays next to it in `resources/ui/`.

| Font | File | Use | Licence |
|---|---|---|---|
| Carlito Regular | `resources/ui/Carlito-Regular.ttf` | All drawn text, standing in for Calibri Light (`docs/UI_SPEC.md` section 1) | SIL Open Font License 1.1 (`Carlito-OFL.txt`) |
| Pinyon Script Regular, Version 1.008 (Google Fonts release) | `resources/ui/PinyonScript-Regular.ttf` (148,956 bytes) | The DHammers logo (`docs/UI_SPEC.md` section 4), since 2026-11-17 | SIL Open Font License 1.1 (`PinyonScript-OFL.txt`) |

The OFL allows embedding the fonts in the plugin and distributing them with it; the fonts themselves may not be sold on their own. Pinyon Script adds about 150 KB to the plugin. Calibri Light (`resources/ui/calibril.ttf`) is compiled in too, and used for the text, when the file exists; it is not in the repository.

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
export MACOSX_DEPLOYMENT_TARGET=11.0
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --target Cymbalis_VST3 Cymbalis_AU
```

`MACOSX_DEPLOYMENT_TARGET=11.0` is needed with Xcode 16 or later (macOS 15 SDK or later) for JUCE's juceaide, as in CI (see "The runner and Xcode"); it makes no difference with Xcode 15. Start from a new build directory if one was configured without it.

The CMake option `CYMBALIS_WITH_TUNING` (ON by default) builds the tuning code in: the Tuning window (a click on the CYMBALIS title opens it), live tuning, tuning files and tuning in the session. `-DCYMBALIS_WITH_TUNING=OFF` builds a lean Cymbalis without any of it, under the same identity. It plays the same bakes the same way; tuning stored in a session is ignored when it loads one and is not saved with it. CI builds only the full plugin.

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Product name | Cymbalis |
| Manufacturer code | `Dham` |
| Plugin code | `Cymb` |
| Bundle ID | `com.dhammers.cymbalis` |

These values are set in `CMakeLists.txt`. Changing a code changes the plugin ID that DAWs store.
