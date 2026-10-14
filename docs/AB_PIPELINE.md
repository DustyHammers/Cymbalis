# A/B comparison: Python reference vs C++ engine

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
