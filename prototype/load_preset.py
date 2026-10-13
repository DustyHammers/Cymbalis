"""
load_preset.py — Shared preset loader for Cymbalis V4 engine.

Usage:
    from load_preset import load_engine
    eng, SE = load_engine('presets/nova/Nova_Baseline19.json')
    eng, SE = load_engine('presets/nova/Nova_Baseline19.json',
                          articulation='presets/nova/articulations/bow_tip.json')

Articulation overlays are small JSONs that patch specific parameters
on top of the base preset. Only keys present in the overlay are overridden.
"""
import os, sys, json, importlib.util

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, 'engine'))


def load_engine(preset_json, articulation=None, overrides=None):
    """
    Load engine from preset JSON with optional articulation overlay.

    Parameters
    ----------
    preset_json   : path to base preset JSON
    articulation  : optional path to articulation overlay JSON
    overrides     : optional dict of parameter overrides (applied last)

    Returns
    -------
    (engine_instance, StrikeEvent_class)
    """
    from spear_layer import SpearLayer
    spec = importlib.util.spec_from_file_location(
        'eng19', os.path.join(ROOT, 'engine', 'multi_strike_engine_v19.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    EC, SE = mod.MultiStrikeEngineV16, mod.StrikeEvent

    with open(preset_json) as f:
        p = json.load(f)

    base      = os.path.dirname(os.path.abspath(preset_json))
    df        = p['data_files']
    params    = dict(p['parameters'])

    # Apply articulation overlay
    if articulation:
        with open(articulation) as f:
            art = json.load(f)
        params.update(art.get('parameters', {}))
        # data_files overrides: articulation can redirect any data file
        if 'data_files' in art:
            df.update(art['data_files'])

    # Apply any runtime overrides
    if overrides:
        params.update(overrides)

    # Resolve relative data paths
    for k in ['band_env_onset_path', 'vel_tilt_path',
              'edge_band_env_path', 'edge_npz_path']:
        if k in params:
            params[k] = os.path.join(base, params[k])

    spear = SpearLayer(os.path.join(base, df['fingerprint']))

    kw = {}
    for key, attr in [('edge_ratios',  'edge_ratios_path'),
                      ('bell_ratios',  'bell_ratios_path'),
                      ('hf_bloom',     'hf_bloom_path'),
                      ('clusters',     'cluster_path'),
                      ('vel_participation',      'vel_participation_path'),
                      ('bell_vel_participation', 'bell_vel_participation_path')]:
        if key in df:
            kw[attr] = os.path.join(base, df[key])

    return EC(spear, **kw, **params), SE
