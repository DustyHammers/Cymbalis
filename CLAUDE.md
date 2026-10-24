# Cymbalis — rules for Claude Code

Cymbalis is a physically modelled cymbal instrument (macOS VST3 + AU, JUCE 7.0.12). There is one plugin, Cymbalis, with the Tuning window built in (CMake option `CYMBALIS_WITH_TUNING`, ON by default; `docs/TUNING.md`). **The plugin is the reference.** Kris tunes the sound by ear, playing the plugin. The Python engine V19 in `prototype/` was the basis for the C++ engine and is the source of the preset data (bake); its audio output is no longer a target.

## Standing rules

- Never add or remove plugin parameters without explicit instruction from Kris. Tuning values are not host parameters.
- Never modify `prototype/engine/*.py` or anything under `presets/`. They are the locked source data (`docs/BASELINE_MANIFEST.md`). Sound changes go into the plugin's engine and its baked preset data (`resources/presets/`).
- One change per task. Do not bundle unrelated changes.
- SPEAR `.txt` files never enter this repo.
- **No regression or A/B renders.** Do not compare against Python, do not render audio for checking, and put no audio in CI or pull requests, unless Kris asks. Verify changes with the build, pluginval/auval and the CPU benchmark. Kris judges the sound by ear.
- **CI output is the VST3 and AU zips of Cymbalis only.** No editor snapshots, screenshots or other renders of the UI in CI, artifacts or pull requests, unless Kris asks.
- When a new preset needs engine features the C++ engine doesn't have yet, port them from the Python engine as a guide; no render comparison.
- Real-time requirement: the engine must stay at least 3× faster than real time (`cymbalis_bench`).
- Report anything in the engine that looks wrong; don't change sound behaviour unless the task asks for it.
- No UI code beyond what the task's approved spec describes.
- Read the relevant source and docs before changing anything; do not guess.
- Work on a branch and open a pull request for each task; do not push to `main` directly.
- There is no `README.md` for now. Do not create or edit one unless Kris asks. Put documentation in `docs/` instead.

## Repo map

| Path | Contents |
|---|---|
| `plugin/` | C++ plugin source (`plugin/tuning/`: the Tuning window, built with `CYMBALIS_WITH_TUNING`) |
| `prototype/` | Python engine V19 (`engine/`), `load_preset.py`, `render_nova.py` — reference only |
| `presets/nova/` | Nova Ride preset: `Nova_Baseline19.json`, `articulations/`, `data/` |
| `resources/ui/` | UI bitmaps (background with logo, Nova photo, knob filmstrip) |
| `docs/ui/reference_mockup.png` | Original UI artwork, reference only |
| `docs/history/` | Engine session summaries (background reading) |
| `docs/BASELINE_MANIFEST.md` | Locked MD5s and the baseline render check |
| `docs/PORT_MAP.md` | Python engine map, porting stages, live-engine decisions |
| `docs/AB_PIPELINE.md` | Retired Python vs C++ comparison (don't run unless Kris asks) |
| `docs/BUILDS.md` | CI builds, installing a build, plugin identity |
| `docs/UI_SPEC.md` | Approved UI spec, slots, the four parameters |
| `docs/TUNING.md` | Tuning window, tuning files, `tools/apply_tuning.py` |

## Python reference

From the repo root:

```
python3 -m venv prototype/.venv && source prototype/.venv/bin/activate
pip install -r prototype/requirements.txt
```

`load_engine` is imported from `prototype/` (add it to `sys.path`); preset paths are relative to the repo root, e.g. `presets/nova/Nova_Baseline19.json`.
