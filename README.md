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
