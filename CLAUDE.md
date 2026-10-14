# Cymbalis — rules for Claude Code

Cymbalis is a physically modelled cymbal instrument (macOS VST3 + AU, JUCE 7.0.12). The Python engine V19 in `prototype/` is the reference; the plugin's C++ engine must reproduce it.

## Standing rules

- Never add or remove plugin parameters without explicit instruction from Kris.
- Never modify `prototype/engine/*.py` or anything under `presets/`. These are MD5-locked baselines (`docs/BASELINE_MANIFEST.md`). Python-side experiments use `load_engine(..., overrides=...)` only.
- One change per task. Do not bundle unrelated changes.
- SPEAR `.txt` files never enter this repo.
- The C++ engine reproduces Python V19 as it is, including its known bugs. Do not "fix" behaviour while porting; report anything that looks wrong.
- When a C++ vs Python comparison fails, diagnose and report. Never tune C++ values to make numbers match.
- No UI code beyond what the task's approved spec describes.
- Use unique timestamped filenames for every audio render.
- Read the relevant source and docs before changing anything; do not guess.
- Work on a branch and open a pull request for each task; do not push to `main` directly.
- There is no `README.md` for now. Do not create or edit one unless Kris asks. Put documentation in `docs/` instead.

## Repo map

| Path | Contents |
|---|---|
| `plugin/` | C++ plugin source |
| `prototype/` | Python engine V19 (`engine/`), `load_preset.py`, `render_nova.py` — reference only |
| `presets/nova/` | Nova Ride preset: `Nova_Baseline19.json`, `articulations/`, `data/` |
| `resources/ui/` | UI bitmaps (background with logo, Nova photo, knob filmstrip) |
| `docs/ui/reference_mockup.png` | Original UI artwork, reference only |
| `docs/history/` | Engine session summaries (background reading) |
| `docs/BASELINE_MANIFEST.md` | Locked MD5s and the baseline render check |
| `docs/PORT_MAP.md` | Python engine map, porting stages, live-engine decisions |
| `docs/AB_PIPELINE.md` | How to run the Python vs C++ comparison |
| `docs/BUILDS.md` | CI builds, installing a build, plugin identity |
| `docs/UI_SPEC.md` | Approved UI spec, slots, the four parameters |

## Python reference

From the repo root:

```
python3 -m venv prototype/.venv && source prototype/.venv/bin/activate
pip install -r prototype/requirements.txt
```

`load_engine` is imported from `prototype/` (add it to `sys.path`); preset paths are relative to the repo root, e.g. `presets/nova/Nova_Baseline19.json`.
