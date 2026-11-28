# Builds and installation

Cymbalis is proprietary: see `LICENSE` at the repo root, which also lists the third-party components and where their licences are (Kris, 2026-11-24).

## Builds

Every push to `main` and every pull request runs **Actions → Build macOS (VST3 + AU)**. Since 2026-11-26 the long tests in it run only on request or on a release tag ("What CI runs" below). It builds one plugin, **Cymbalis**, with everything: the editor, with its CYMBAL and TUNING pages (the TUNING tab shows the tuning panel, `docs/TUNING.md`). It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11: the build fails if either slice of the VST3 or the AU has another minimum (`LC_BUILD_VERSION` minos)
- JUCE 7.0.12, built on GitHub's `macos-15` runner with Xcode 16.4 (macOS 15.5 SDK), pinned (see "The runner and Xcode" below)
- No "Made with JUCE" splash screen: the plugin is built with `JUCE_DISPLAY_SPLASH_SCREEN=0` (`target_compile_definitions` of the plugin target in `CMakeLists.txt`, public, so it reaches the JUCE modules compiled into it, as `JUCE_WEB_BROWSER=0` does). JUCE 7 allows this only under a licence that permits it; Kris has confirmed his does (2026-11-23). Nothing else about JUCE changes: the version stays 7.0.12, and nothing reports app usage (JUCE 7.0.12 has no `JUCE_REPORT_APP_USAGE` setting; the build never set one). The splash hides itself from accessibility, so the mouse test cannot see it; instead the build fails unless every compile of the plugin's `juce_gui_basics` (the splash) and `juce_audio_processors` (the editor that puts it up) carries the flag
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta; the step installs Rosetta if the runner lacks it)
- An editor mouse test in an AU and a VST3 host (`tools/cymbalis_uitest`; on request only, "What CI runs" below): a new instance's editor must open at its opening size (1104×736, smaller on a small display such as the runner's, `docs/UI_SPEC.md` section 2); real mouse events change the MIDI channel, notes, a stick, the preset and a stick of that preset, checked in the plugin's saved state, and the scroll wheel over the knobs and boxes must change nothing; on HI-HAT, its Blend Position knobs must be hidden (and shown again on KONSTAN), MIDI notes 42 and 46 and the pedal CC played into the plugin through the host must sound (an open hit ringing on, a FOOT when the pedal closes), the hats and the pedal drawing must follow the pedal CC (their accessibility descriptions give their opening), also with MIDI CHANNEL 16 and the CC sent on channel 1, as a keyboard's mod wheel (the pedal CC is read on every channel), where a sweep from closed to open and back must play one FOOT, on the close; a BOW hit with the pedal closed and one with it open must swing the top cymbal at 15 % and all of its wobble, the bottom cymbal not at all (the hats' last frame, through their accessibility description), a pedal CC picked in the PEDAL box must be saved, and restored with the state, and its TUNING page must show the values that act on it and hide the others (its own values moved with the mouse, the foot splash's SPLASH WINDOW and SPLASH REOPEN among them, must be saved in a tuning file and loaded back); dragging, double-clicking and automating each of the five knobs must move its parameter and the value the knob shows (its accessibility value), which its position arc is drawn from (`docs/UI_SPEC.md` section 6); then a real click on the TUNING tab must show the TUNING page (its panel in the page area, the knobs hidden, the page saved in the session), the wheel over the panel's sliders and numeric boxes must change nothing, STINE FLAT RIDE chosen in the CYMBAL box on that page must show in the panel, a session saved on the page must reopen on it (VST3), the editor resized from the host must keep the panel's text size and fit it to the page area (VST3: in the AU host such a resize does not reach the plugin's editor), and a click on the CYMBAL tab must show the CYMBAL page again (on KONSTAN CRASH 18" and on HI-HAT); last, on the CYMBAL page, the editor's resizer dragged far in and far out must stop at 960×640 and 1536×1024, keeping 3:2 in between, and a session saved at another size must reopen at it (VST3; JUCE's AU wrapper hands back the old editor on a quick reopen). Until 2026-11-24 the panel was a Tuning window that opened below the editor's top right and, on a screen as narrow as the `macos-15` runner's, covered the editor's CYMBAL box; JUCE ignores a click where another window is in front, as it would a user's, so the test moved the window aside before each such click (it still moves any window of its own that covers a click). The log gives the editor's and the screen's size
- A hit display timing (`CymbalisUiTest hits`; on request only): processBlock at 44.1 kHz, with 256- and 128-sample blocks and the audio thread at normal priority and real-time, with the editor closed, open and idle, open with 16 hits moving the wireframe cymbal, and open with the same hits on the TUNING page (the cymbal hidden: it must draw no frame and take no hit), each case three times, then the three cases once more with HAMMERED THIN RIDE selected, and once each with SWISH, RAVEN and KONSTAN CRASH 18" (the heaviest cymbals to draw: their dented meshes, with the size of each cymbal and how long it took to build) and HI-HAT (its two dented cymbals and its clutch and rod, with its notes 42 and 46 and its pedal CC sweeping from closed to open and back); with the CPU used besides, the message thread's CPU, the editor's paint calls and the cymbal's OpenGL frames, and once a check that nothing is drawn after every hit is older than 6 s; printed to the log and the job summary. The runner's Mac has no GPU: it draws OpenGL with the Apple Software Renderer (the step prints the renderer), so its OpenGL frame times are a CPU's, not a GPU's
- Ad-hoc code signing

The run's **Artifacts** section has one artifact, `Cymbalis-macOS-<shortsha>`, with two zips and nothing else: `Cymbalis-VST3-macOS-<shortsha>.zip` and `Cymbalis-AU-macOS-<shortsha>.zip`.

The job summary also shows the CPU cost of the engine on the runner for each preset (`cymbalis_bench`, every slot playing, and on HI-HAT the pedal moving: a CC every 10 ms, open and closed every 1.5 s; it fails if the output is not finite), and, on request only, its block times in dense playing (`cymbalis_bench --dense`: a strike every 24 ms, HI-HAT's pedal moving too, every preset at 44.1 and 96 kHz with 128- and 64-sample blocks on a real-time thread, each case three times; then `cymbalis_bench_profile`, the time of the engine's parts; docs/PORT_MAP.md, "Cost of a strike"). The runner is a virtual machine: a block that is slow in one run only is the machine's, not the engine's, which is why the dense mode reports the smallest of the three runs per block. Pushing a `v*` tag also creates a GitHub Release with the two zips attached.

The job summary's **Kit benchmark (engine pool)** section (on request only; `cymbalis_bench --kit`; `docs/PORT_MAP.md`, "Multi-cymbal rendering (step 1)") shows several cymbals rendered together, on one thread and on several, through the engine pool the plugin will use for a kit (it doesn't use it yet). The kit is NOVA RIDE, HI-HAT (its pedal moving), CRASH 16", KONSTAN CRASH 18", STINE FLAT RIDE, RAVEN, SWISH, EXTRA PAPER THIN 24", HAMMERED THIN RIDE and SMOKE FLAT RIDE (from `resources/presets/archive/`, read as a file), the first N playing. It runs:

- **Two patterns.** In the groove, the ride plays 8ths at 140 bpm, the hi-hat 8ths, and the other cymbals crash in turn; on bar 1 of every 4, three cymbals land on the same sample. In the stress pattern, every cymbal is hit every 200 ms, and every fourth hit lands on the same sample for all of them.
- **The cases:** 1, 2, 4, 6 and 10 cymbals; 1 to 3 threads (the runner's performance cores); 44.1 and 96 kHz; 128- and 64-sample blocks.
- **How each case runs:** on a real-time thread, each block at its deadline, three times; the smallest of the three times per block is kept.

The tables:

- **One per pattern.** Each row gives the block time's mean, p99, p99.9 and worst as a percentage of the block's duration, and the blocks over 100 % and over 70 %. Then the speed-up against one thread. Then the workers' CPU per second of audio (all the workers, spinning included) and the part of it spent rendering. Then the wake-up delay (dispatch to a worker starting, median and p99) and how often a worker had to be woken from sleep. Last, since step 2, the caller's wait: in a shared block, the time from the hand-out running out to the last engine still on a worker finishing (median, p99, max).
- **Quiet cymbals.** 10 cymbals with only the ride playing: what a silent cymbal costs per block.
- **Workers spinning through.** The 10-cymbal cases again, at 44.1 kHz / 128 and 96 kHz / 64, with the workers spinning through each block instead of sleeping between blocks.

Each run is 2 s of audio, not the bench's default 60 s, to keep the job within its hour; the step takes about 15 minutes. The step fails only if a threaded mix differs from the one-thread mix by a single bit, if a one-thread mix differs from step 1's pool's (a copy of it is kept in the bench), or if a sample is not finite. It never fails on timing: the runner is a virtual machine.

Before the benchmarks, the **job pool self-test** step (on request only) runs `dhpool_selftest` (`shared/dhpool/README-pool.md`). It uses dummy jobs at 1 to 4 threads, and it fails the job unless:

- every job runs exactly once per run;
- the results are the same bits at every thread count, with flush to zero on and off;
- several runs per block work;
- a workgroup hook change reaches every worker;
- prepare / release cycles leave no thread behind.

It prints to the log only.

If Configure or Build fails, its error lines also appear as the job's annotations (`.github/scripts/annotate_errors.sh`), and Configure prints the toolchain (CMake, Xcode, SDK, deployment target) as a notice, so a failure can be read without the job's log.

### What CI runs

Since 2026-11-26 CI is lean while the presets are being developed (Kris: "we are still developing presets… right now we know the mouse works"; the dhpool self-test had been failing builds at random). Nothing was removed: the long tests are still in the workflow and run on request.

- **Every push to `main`, every pull request, and a manual run without "full tests":**
  - checkout, Xcode, Ninja, the caches, configure and build;
  - the edge attack check (`tools/cymbalis_edgeclick`, about 15 seconds; `docs/PORT_MAP.md`, "An envelope strike's contact plays at its envelope level");
  - the CPU benchmark (about 2 minutes; the 3× real-time check in `CLAUDE.md`);
  - the bundle checks: both architectures, minimum macOS 11.0, no AVX in the Intel slice, the JUCE splash screen off;
  - ad-hoc code signing;
  - pluginval and auval (native and Rosetta);
  - the two zips, packaged and uploaded as before.
- **The test tools are still built every time** (`dhpool_selftest`, `CymbalisUiTest`, the bench), so a broken one is still a build error. They compile alongside the plugin and finish before its link, so they add well under a minute to the build (2.3–2.8 minutes in all with the compiler cache).
- **On request only, or on a `v*` release tag:**
  - the job pool self-test (dhpool);
  - the engine block times in dense playing (about 11 minutes);
  - the kit benchmark (about 14 minutes);
  - the editor mouse test (about 7 minutes);
  - the hit display timing (about 9 minutes).

  Their steps and timeouts are as they were; only when they run has changed (`if:` on each: a manual run with `full_tests`, or a `v*` tag).
- **To run the full tests:** Actions → "Build macOS (VST3 + AU)" → Run workflow → pick the branch → tick "full tests".
- **A release tag** (`v*`) runs everything, then publishes the release.
- **Build time:** about 6 minutes for the lean build (6 min 6 s in this change's own run: the build 2.8 minutes, the CPU benchmark 2.3, the rest under a minute), against about 46 minutes with everything.
- **Before threads go on in the plugin** (step 3 of the kit plan), the dhpool self-test, the kit benchmark and the dense block times must run, and pass, on every pull request again. They are the multi-core checks the merge with Tympanis relies on. Tympanis needs the same change in its own workflow (done in the Tympanis session).

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
| STINE FLAT RIDE | `resources/presets/stine_flat_ride.bake.json` | `presets/stine/` |
| HAMMERED THIN RIDE | `resources/presets/hammered_thin_ride.bake.json` | `presets/bettis/` |
| SWISH | `resources/presets/swish.bake.json` | `presets/swish/` |
| RAVEN | `resources/presets/raven.bake.json` | `presets/raven/` |
| EXTRA PAPER THIN 24" | `resources/presets/paperthin.bake.json` | `presets/paperthin/` |
| KONSTAN CRASH 18" | `resources/presets/konstan.bake.json` | `presets/konstan/` |
| CRASH 16" | `resources/presets/azcrash.bake.json` | `presets/azcrash/` |
| HI-HAT | `resources/presets/hihat.bake.json` | `presets/hihat/` (`tools/bake_hihat.py`) |

The JSON bakes are the source: `tools/bake_preset.py`, `tools/apply_tuning.py` and the tuning files read and write them. The plugin embeds each bake in a packed form instead (in its binary, through BinaryData): when CMake builds the plugin's binary data, `tools/pack_bake` packs every bake into `<build>/presets/<preset>.bake.pack`. A packed bake (`plugin/PackedBake.h`) is the bake's JSON tree in binary, compressed with zlib: every number as its exact double (or int32 where it is exactly one), so the plugin loads exactly the values the JSON gives. It carries a format version and an FNV-1a checksum; a packed preset that does not match them fails to load with a message, like a bake with another format version. The tool checks each packed bake before writing it: it must unpack to the same JSON, bit for bit, and load as a bake, and STINE FLAT RIDE, SWISH and KONSTAN CRASH 18" must give EDGE VOICE FROM BOW's check values at 100 % (`docs/TUNING.md`), or the build stops. Re-baking one of them from new data calls for that table to be updated (in `tools/pack_bake/main.cpp` and the doc).

After a change to a preset's data, regenerate its bake (`--preset nova`, `stine`, `bettis`, `swish`, `raven`, `paperthin`, `konstan` or `azcrash`). KONSTAN's, EXTRA PAPER THIN 24"'s, CRASH 16"'s and STINE FLAT RIDE's T60s are baked without V19's T60 cap (`T60_UNCAPPED` in `tools/bake_preset.py`; `docs/PORT_MAP.md`, "Konstan Crash 18"", decision 5, "Extra Paper Thin Ride 24"", "Crash 16"" and "Stine Flat Ride"); the other presets keep it. `--preset 602` re-bakes SMOKE FLAT RIDE, which the plugin no longer plays: its bake is kept, unchanged, in `resources/presets/archive/`, which nothing builds, packs or tests (`docs/PORT_MAP.md`, "Stine Flat Ride"):

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/bake_preset.py --preset stine --overrides '{"nl_amount": 0.0}' --out resources/presets/stine_flat_ride.bake.json
python tools/bake_preset.py --preset bettis --overrides '{"nl_amount": 0.0}' --out resources/presets/hammered_thin_ride.bake.json
python tools/bake_preset.py --preset swish --overrides '{"nl_amount": 0.0}' --out resources/presets/swish.bake.json
python tools/bake_preset.py --preset raven --overrides '{"nl_amount": 0.0}' --out resources/presets/raven.bake.json
python tools/bake_preset.py --preset paperthin --overrides '{"nl_amount": 0.0}' --out resources/presets/paperthin.bake.json
python tools/bake_preset.py --preset konstan --overrides '{"nl_amount": 0.0}' --out resources/presets/konstan.bake.json
python tools/bake_preset.py --preset azcrash --overrides '{"nl_amount": 0.0}' --out resources/presets/azcrash.bake.json
python tools/bake_preset.py --preset 602 --overrides '{"nl_amount": 0.0}' --out resources/presets/archive/smoke_flat_ride.bake.json
```

**NOVA RIDE can no longer be re-baked from `presets/` alone** (since 2026-11-27, edge v2: `docs/PORT_MAP.md`, "NOVA RIDE edge v2"). Its real EDGE is a step after `tools/bake_preset.py`, like the trims and skip thresholds: `tools/apply_nova_edge_v2.py` inserts the 34 edge-only modes, the edge NPZ block, the edge settings, the EDGE trim and, last, the stereo picks of 575 modes from `presets/nova/edge_v2/` (checked against `docs/BASELINE_MANIFEST.md`), records that it ran, and refuses to run twice. The order is `bake_preset.py`, then edge v2, then the level trims keeping EDGE's (`--keep EDGE`: edge v2 sets it), then the skip thresholds:

```
python tools/bake_preset.py --preset nova --overrides '{"nl_amount": 0.0}' --out resources/presets/nova_ride.bake.json
python tools/apply_nova_edge_v2.py resources/presets/nova_ride.bake.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json --keep EDGE
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --skip-thresholds --json skip.json
python tools/set_skip_thresholds.py resources/presets/nova_ride.bake.json skip.json
```

The shipped bake had its trims and skip thresholds when edge v2 was applied to it; only EDGE's skip threshold was measured again (`set_skip_thresholds.py ... --only EDGE`). Its tuning block (`tools/apply_tuning.py`) works as on any bake, with the 2304 modes.

**The hi-hat's bake** comes from `tools/bake_hihat.py` (numpy and scipy; the same environment): it runs the approved strike model's preparation (`docs/hihat_reference.py`: the stick envelopes, each knot's level per band, the band decay rates; `docs/PORT_MAP.md`, "Hi-hat") on `presets/hihat/`, checks those files against `docs/BASELINE_MANIFEST.md`, and writes the plugin's bake format with a `hihat` block (the foot splash's knots from `presets/hihat/data/hihat_splash.json` among them, since 2026-11-26), its entries TIP, EDGE, FOOT and SPLASH with their stick clicks, and its tuning block (output gain −11 dB, HIT VARIATION on, the hi-hat's own values), which it writes itself. The plugin's engine does the per-strike interpolation. Then its trims, as below:

```
python tools/bake_hihat.py --out resources/presets/hihat.bake.json
```

Each bake also holds its slots' level trims, in `slots[].level_trim.gain_db`. The plugin uses only that number; edit it to adjust a slot's level by ear. Re-baking drops the trims. Nova Ride's trims give a velocity-0.9 hit at each slot's base position the same 1-second RMS as one on BOW (tip). The Stine, 602, Bettis, Swish, Raven, Paper Thin, Konstan and Crash 16 trims work at velocity 1.0 over the first 500 ms. BOW, Stine's and the 602's CENTER (a copy of BOW) and Swish's BOW_SHOULDER are matched to Nova's BOW (tip); Swish's, Raven's, Paper Thin's, Konstan's and Crash 16's BELL_SHOULDER take BELL's SPEAR target. The shoulder copies of Stine, the 602, Bettis, Raven, Paper Thin, Konstan and Crash 16 start with their sources' trims. Nova's EDGE has edge v2's trim (−2.11 dB, not measured: `--keep EDGE`). EDGE and BELL go to the level their SPEAR captures have relative to BOW, capped where one hit would peak above 0.9 before the output gain and the Volume knob (`docs/PORT_MAP.md`, "602 and Bettis", decision 5). To measure trims again with the plugin's engine and store them (`--peak-limit 0.25365` is that 0.9 after the −11 dB output gain, see "Headroom" below):

```
cmake --build build --target cymbalis_trims
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --json trims.json
python tools/set_level_trims.py resources/presets/nova_ride.bake.json trims.json --keep EDGE
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/stine_flat_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=19.25 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/stine_flat_ride.bake.json trims.json
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/archive/smoke_flat_ride.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=2.8 --peak-limit 0.25365 --json trims.json
python tools/set_level_trims.py resources/presets/archive/smoke_flat_ride.bake.json trims.json
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
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/hihat.bake.json --reference resources/presets/nova_ride.bake.json --velocity 1.0 --window 0.5 --spear-target EDGE=0 --peak-limit 0.25365 --same-as TIP=EDGE --same-as FOOT=EDGE --same-as SPLASH=EDGE --json trims.json
python tools/set_level_trims.py resources/presets/hihat.bake.json trims.json
```

HI-HAT's trims are measured with the hats closed: EDGE matched to Nova's BOW (tip), capped at the 0.9 peak (−3.85 dB, 9.9 dB under BOW: the cap holds it), and TIP, FOOT and SPLASH take EDGE's trim (`--same-as`), so they keep their captured levels relative to EDGE (`docs/PORT_MAP.md`, "Hi-hat"). SPLASH was added later (2026-11-26) with EDGE's trim as stored, and its skip threshold factor measured as below; the other three entries' trims and factors were kept as they were.

EXTRA PAPER THIN 24"'s EDGE is boosted after its trims are measured (`docs/PORT_MAP.md`, "Extra Paper Thin Ride 24"", decision 6; Kris, 2026-11-21): `--boost EDGE=4` stores its trim 4 dB above the capped one (+5.19 → +9.19 dB), and its tuning block gives it the velocity curve that takes the 4 dB back off at velocity 0.6 and below (`vel_0.6_db` and `vel_0.3_db` −4.0), so the extra level comes in between 0.6 and 1.0. `tools/set_level_trims.py` refuses the boost without that curve, so apply the tuning file first, as for every new preset (below). The boost is what keeps one EDGE hit at velocity 1.0 and a dense run of EDGE and BOW hits below a peak of 0.9 at the plugin's output (0.40 and 0.86, 99th percentiles; Kris asked for +5 dB, which took the dense run to 0.95). Its `level_trim` records the capped trim, the boost and why. Measuring the trims again drops the boost unless the command gives it again.

CRASH 16"'s BELL target, −0.7 dB against BOW, is not the level of its loudest bell capture against the loudest bow, as the others' are: its only bell capture is soft (Bell V1, −9.66 dB against Bow V2, about level with the soft Bow V1), so the target is the level the renders Kris heard had (`docs/BASELINE_MANIFEST.md`, Crash 16"). BELL reaches it uncapped (+16.81 dB); EDGE is capped at +1.02 dB, 2.56 dB under BOW (`docs/PORT_MAP.md`, "Crash 16"").

Every slot also carries its **skip threshold factor**, `skip_threshold.rel` (2026-11-25; `docs/PORT_MAP.md`, "One silence rule: 140 dB below a full hit").
- **What it does:** `FastEngine` stops rendering a mode or component once it is below `rel` × calibration × trim, 140 dB under a velocity-1.0 hit on that slot.
- **How it is measured:** with the same tool, from the median peak of 16 velocity-1.0 hits at the slot's base position, with HIT VARIATION off.
- **When to measure:** a trim scales the peak and the level alike, so the factor does not depend on the trims, and it can be measured before or after them. Measure it again after a tuning change that moves a slot's level against its calibration (its band gains, its stereo gains) by more than a few dB.

For every preset, archive included:

```
build/tools/cymbalis_trims/cymbalis_trims --bake resources/presets/nova_ride.bake.json --skip-thresholds --json skip.json
python tools/set_skip_thresholds.py resources/presets/nova_ride.bake.json skip.json
```

A new preset gets its tuning block (the output gain below, and any velocity curve) before its trims are measured: the trims tool measures the reference, Nova, with its −11 dB, so a bake without it would come out 11 dB too quiet once the block is added (Raven, `docs/PORT_MAP.md`, "Raven"; Konstan, "Konstan Crash 18""; Paper Thin, "Extra Paper Thin Ride 24""; Crash 16, "Crash 16""; Stine, "Stine Flat Ride").

The tuning block also holds EDGE VOICE FROM BOW (`edge_voice_from_bow`, `docs/TUNING.md`), which changes an edge strike's level, and SWISH's edge click and attack, so EDGE is measured with them in place. KONSTAN CRASH 18"'s EDGE trim was measured with the value at 100 % (2026-10-29), with the commands above, and only EDGE's trim was stored; after its T60 refit (2026-10-31) every KONSTAN trim was measured again, with the tuning block in place. SMOKE FLAT RIDE was at 0 % (Kris, 2026-10-31), and its bake was main's, unchanged. STINE FLAT RIDE's block does not hold the value, so it starts at 0 %, and its EDGE trim was measured that way. SWISH's EDGE trim was left as it was (measured again it would be −3.88 dB instead of −3.44, under 1 dB). A new value in the block calls for EDGE to be measured again.

Every preset's tuning block also switches HIT VARIATION on (`hit_variation`, with the default spreads, 2026-10-31; `docs/TUNING.md`, "Hit variation"), which moves each strike's position and velocity a little. A trim is the level at the slot's own position, so `tools/cymbalis_trims` switches HIT VARIATION off while it measures, and the trims did not change. On average over many strikes the variation leaves the rows within 0.3 dB of their trims, except the bell rows, which it makes louder: KONSTAN's +3.9 dB, NOVA's BELL2 +1.8 dB and BELL +1.0 dB, RAVEN's +0.75 dB (`docs/PORT_MAP.md`, live-engine decision 22).

**Headroom (2026-10-25).** The trims match single hits to Nova's BOW (tip) with no headroom, and hits add up while the cymbal rings, so a host's master clipped in normal playing. Every preset therefore has the same cymbal output gain, −11 dB (`output_gain_db` in its tuning block, set with `tools/apply_tuning.py`); the trims, velocity curves and everything else are unchanged. RAVEN, KONSTAN CRASH 18", EXTRA PAPER THIN 24" and CRASH 16", added later (2026-10-27, 2026-10-28, 2026-11-21 and 2026-11-22), have the same −11 dB; they are not in the table below. STINE FLAT RIDE (2026-11-23), which took SMOKE FLAT RIDE's place, has the same −11 dB; its row was measured the same way when it was added, with HIT VARIATION on as baked, the 16 phase sequences made by playing 0 to 15 silent strikes first. SMOKE FLAT RIDE's row stays as it was measured. Measured in memory through the plugin at 44.1 kHz, Volume 0 dB, velocity 1.0, with a stress pattern (8 s of BOW (tip) eighth notes at 120 BPM with EDGE on every beat 1, then 4 s of EDGE alone on every quarter note), first run after loading:

| Preset | Stress peak before | After −11 dB | Worst of 16 phase sequences, after | Loudest single hit (worst of 128), before → after |
|---|---|---|---|---|
| NOVA RIDE | −1.2 dBFS | −12.2 dBFS | −12.2 dBFS | +1.6 → −9.4 dBFS (BELL2) |
| SMOKE FLAT RIDE | +4.2 dBFS | −6.8 dBFS | −6.8 dBFS | +0.2 → −10.8 dBFS (BOW, CENTER and their shoulders) |
| STINE FLAT RIDE | +4.7 dBFS | −6.3 dBFS | −6.2 dBFS | −1.3 → −12.3 dBFS (BOW, CENTER and their shoulders) |
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

The CMake option `CYMBALIS_WITH_TUNING` (ON by default) builds the tuning code in: the TUNING page (its tab in the editor's left column), live tuning, tuning files and tuning in the session. `-DCYMBALIS_WITH_TUNING=OFF` builds a lean Cymbalis without any of it, under the same identity. It plays the same bakes the same way; tuning stored in a session is ignored when it loads one and is not saved with it. CI builds only the full plugin.

## Plugin identity

| Field | Value |
|---|---|
| Company | DHammers |
| Product name | Cymbalis |
| Manufacturer code | `Dham` |
| Plugin code | `Cymb` |
| Bundle ID | `com.dhammers.cymbalis` |

These values are set in `CMakeLists.txt`. Changing a code changes the plugin ID that DAWs store.
