# Cymbalis

Physically modelled cymbal instrument (modal synthesis) by DHammers. macOS only, VST3 and AU.

**Status:** Phase 1, build skeleton. The plugin loads, accepts MIDI and outputs silence. The modal engine port starts in Phase 3.

## Builds

Every push to `main` runs **Actions → Build macOS (VST3 + AU)**. It produces:

- Universal binaries (Apple Silicon + Intel), minimum macOS 11
- JUCE 7.0.12, built with Xcode 15
- The Intel slice capped at SSE4.1, and the build fails if any AVX instructions appear
- Validation with pluginval (VST3) and auval (AU, native and under Rosetta)
- Ad-hoc code signing

Download the zips from the run's **Artifacts** section. Pushing a `v*` tag also creates a GitHub Release with the zips attached.

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

## A/B comparison: Python reference vs C++ engine

The C++ engine (`plugin/dsp/`, static library `cymbalis_engine`) is checked render by render against the Python V19 reference. See `docs/PORT_MAP.md` for the porting stages and the live-engine decisions.

```
python3 -m venv prototype/.venv && source prototype/.venv/bin/activate
pip install -r prototype/requirements.txt
cmake -B build-ab -G Ninja -DCMAKE_BUILD_TYPE=Release -DCYMBALIS_BUILD_PLUGIN=OFF
cmake --build build-ab --target cymbalis_render
python tools/run_ab.py --cpp build-ab/tools/cymbalis_render/cymbalis_render --out ab-out
```

`run_ab.py` does four things:

1. Bakes the preset with `tools/bake_preset.py`.
2. Renders every sequence in `tests/sequences/` twice: in Python with `tools/render_reference.py`, and in C++ with `cymbalis_render`.
3. Compares each pair with `tools/compare_ab.py`. A pair passes if the largest sample difference between the float64 renders is at most 1e-8.
4. Writes `ab-out/index.md`, which lists the results and links the float32 WAV pairs and the reports.

In CI, the `ab-renders` job runs this after the build and uploads `ab-out/` as the artifact `Cymbalis-AB-<shortsha>`.
