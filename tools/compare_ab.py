#!/usr/bin/env python3
"""
compare_ab.py — compare two WAV renders (A = reference, B = test).

Reports, as JSON and Markdown:
  * max |B - A| and where it occurs, compared at lag 0;
  * the null residual's peak and RMS in dB relative to the reference peak;
  * the sample offset, estimated by cross-correlation;
  * 22-band 1/3-octave envelope differences at 20 ms resolution. The bands are
    base-10 centres from 125 Hz to 16 kHz, each an 8th-order Butterworth band
    pass; each frame is the RMS of 882 samples.

PASS when max |B - A| <= threshold (default 1e-8), and the sample rate,
channel count and length all match.

Usage:
    python tools/compare_ab.py ref.wav test.wav --json r.json --md r.md --label "edge 3 hits, bs 512"
"""
import argparse
import hashlib
import json
import math
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

N_BANDS = 22
BAND_K = range(-9, -9 + N_BANDS)   # fc = 1000 * 10**(k/10): 125.9 Hz ... 15.85 kHz
FRAME_S = 0.020
ACTIVE_DB = -100.0                 # envelope frames below ref peak - 100 dB are not scored


def db(x):
    return None if x <= 0 else 20.0 * math.log10(x)


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def read(path):
    info = sf.info(path)
    data, sr = sf.read(path, dtype='float64', always_2d=True)
    return data, sr, info.subtype


def sample_offset(a, b, max_lag):
    """Lag (samples) of b relative to a that maximises cross-correlation of the mono sums."""
    x = a.sum(axis=1)
    y = b.sum(axis=1)
    if not np.any(x) or not np.any(y):
        return None, None
    n = len(x) + len(y) - 1
    nfft = 1 << (n - 1).bit_length()
    xc = np.fft.irfft(np.fft.rfft(y, nfft) * np.conj(np.fft.rfft(x, nfft)), nfft)
    lags = np.concatenate([np.arange(0, max_lag + 1), np.arange(-max_lag, 0)])
    vals = np.concatenate([xc[:max_lag + 1], xc[-max_lag:]])
    i = int(np.argmax(vals))
    norm = math.sqrt(float(np.dot(x, x)) * float(np.dot(y, y)))
    return int(lags[i]), float(vals[i] / norm) if norm > 0 else None


def band_envelopes(x, sr, floor):
    frame = int(round(FRAME_S * sr))
    n_frames = x.shape[0] // frame
    centres = [1000.0 * 10 ** (k / 10.0) for k in BAND_K]
    env = np.zeros((N_BANDS, x.shape[1], n_frames))
    for bi, fc in enumerate(centres):
        lo, hi = fc * 10 ** -0.05, min(fc * 10 ** 0.05, 0.499 * sr)
        sos = butter(4, [lo, hi], btype='bandpass', fs=sr, output='sos')
        y = sosfilt(sos, x, axis=0)[:n_frames * frame]
        rms = np.sqrt(np.mean(y.reshape(n_frames, frame, x.shape[1]) ** 2, axis=1)).T
        env[bi] = 20.0 * np.log10(rms + floor)
    return centres, env


def compare(ref_path, test_path, threshold=1e-8, max_lag=4410):
    a, sra, sub_a = read(ref_path)
    b, srb, sub_b = read(test_path)
    res = {
        'reference': {'path': ref_path, 'md5': md5(ref_path), 'subtype': sub_a, 'frames': a.shape[0],
                      'channels': a.shape[1], 'sample_rate': sra},
        'test': {'path': test_path, 'md5': md5(test_path), 'subtype': sub_b, 'frames': b.shape[0],
                 'channels': b.shape[1], 'sample_rate': srb},
        'threshold': threshold,
    }
    problems = []
    if sra != srb:
        problems.append(f'sample rate {sra} vs {srb}')
    if a.shape[1] != b.shape[1]:
        problems.append(f'channels {a.shape[1]} vs {b.shape[1]}')
    if a.shape[0] != b.shape[0]:
        problems.append(f'length {a.shape[0]} vs {b.shape[0]} frames')
    ch = min(a.shape[1], b.shape[1])
    n = min(a.shape[0], b.shape[0])
    a, b = a[:n, :ch], b[:n, :ch]

    d = b - a
    ref_peak = float(np.max(np.abs(a))) if n else 0.0
    max_abs = float(np.max(np.abs(d))) if n else 0.0
    loc = np.unravel_index(int(np.argmax(np.abs(d))), d.shape) if n else (0, 0)
    rms = float(np.sqrt(np.mean(d ** 2))) if n else 0.0
    res.update({
        'reference_peak': ref_peak,
        'reference_peak_dbfs': db(ref_peak),
        'max_abs_diff': max_abs,
        'max_abs_diff_at': {'frame': int(loc[0]), 'channel': int(loc[1]), 'time_s': float(loc[0]) / sra},
        'samples_differing': int(np.count_nonzero(d)),
        'null_peak_db_re_ref_peak': db(max_abs / ref_peak) if ref_peak > 0 and max_abs > 0 else None,
        'null_rms_db_re_ref_peak': db(rms / ref_peak) if ref_peak > 0 and rms > 0 else None,
        'bit_identical': bool(max_abs == 0.0),
    })

    lag, corr = sample_offset(a, b, max_lag)
    res['sample_offset'] = lag
    res['sample_offset_correlation'] = corr

    floor = max(ref_peak, 1e-30) * 1e-12
    centres, env_a = band_envelopes(a, sra, floor)
    _, env_b = band_envelopes(b, sra, floor)
    delta = env_b - env_a                                  # (band, channel, frame)
    active = env_a >= (db(max(ref_peak, 1e-30)) + ACTIVE_DB)
    masked = np.where(active, np.abs(delta), 0.0)
    worst_ch = np.argmax(masked, axis=1)                   # (band, frame)
    per_frame = np.take_along_axis(np.where(active, delta, 0.0), worst_ch[:, None, :], axis=1)[:, 0, :]
    per_band_max = masked.max(axis=(1, 2)) if masked.size else np.zeros(N_BANDS)
    res['bands'] = {
        'centres_hz': [round(c, 2) for c in centres],
        'frame_s': FRAME_S,
        'active_threshold_db_re_ref_peak': ACTIVE_DB,
        'max_abs_delta_db_per_band': [float(x) for x in per_band_max],
        'max_abs_delta_db': float(per_band_max.max()) if per_band_max.size else 0.0,
        'delta_db_per_frame': [[float(f'{v:.4g}') for v in row] for row in per_frame],
    }

    res['problems'] = problems
    res['pass'] = (not problems) and max_abs <= threshold
    return res


def fmt_db(x):
    return '−∞' if x is None else f'{x:.1f}'


def markdown(res, label=None):
    r = res
    lines = [f'### {label or os.path.basename(r["test"]["path"])}', '',
             f'**{"PASS" if r["pass"] else "FAIL"}**: max |diff| = {r["max_abs_diff"]:.3e} '
             f'(threshold {r["threshold"]:.0e})', '',
             '| Metric | Value |', '|---|---|',
             f'| Reference | `{os.path.basename(r["reference"]["path"])}` ({r["reference"]["subtype"]}) |',
             f'| Test | `{os.path.basename(r["test"]["path"])}` ({r["test"]["subtype"]}) |',
             f'| Frames | {r["reference"]["frames"]} vs {r["test"]["frames"]} |',
             f'| Reference peak | {r["reference_peak"]:.6f} ({fmt_db(r["reference_peak_dbfs"])} dBFS) |',
             f'| Max abs diff | {r["max_abs_diff"]:.3e} at {r["max_abs_diff_at"]["time_s"]:.6f} s, '
             f'channel {r["max_abs_diff_at"]["channel"]} |',
             f'| Samples differing | {r["samples_differing"]} |',
             f'| Null residual peak | {fmt_db(r["null_peak_db_re_ref_peak"])} dB re ref peak |',
             f'| Null residual RMS | {fmt_db(r["null_rms_db_re_ref_peak"])} dB re ref peak |',
             f'| Sample offset | {r["sample_offset"]} |',
             f'| Max 1/3-octave envelope Δ (20 ms) | {r["bands"]["max_abs_delta_db"]:.3g} dB |']
    if r['problems']:
        lines.append(f'| Problems | {"; ".join(r["problems"])} |')
    lines += ['', '| Band (Hz) | ' + ' | '.join(f'{c:.0f}' for c in r['bands']['centres_hz']) + ' |',
              '|---' * (N_BANDS + 1) + '|',
              '| max \\|Δ\\| dB | ' + ' | '.join(f'{x:.2g}' for x in r['bands']['max_abs_delta_db_per_band']) + ' |',
              '']
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('reference')
    ap.add_argument('test')
    ap.add_argument('--json')
    ap.add_argument('--md')
    ap.add_argument('--label')
    ap.add_argument('--threshold', type=float, default=1e-8)
    ap.add_argument('--max-lag', type=int, default=4410)
    args = ap.parse_args()

    res = compare(args.reference, args.test, args.threshold, args.max_lag)
    res['label'] = args.label
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(res, f, indent=1)
    md = markdown(res, args.label)
    if args.md:
        with open(args.md, 'w') as f:
            f.write(md + '\n')
    print(md)
    return 0


if __name__ == '__main__':
    sys.exit(main())
