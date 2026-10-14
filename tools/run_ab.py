#!/usr/bin/env python3
"""
run_ab.py — the A/B pipeline: bake, render every sequence in Python and in C++,
compare, and write index.md.

    python tools/run_ab.py --cpp build-ab/tools/cymbalis_render/cymbalis_render --out ab-out

For each distinct set of overrides in the sequences, the preset is baked once.
Each sequence is rendered by tools/render_reference.py and by cymbalis_render
at every block size listed in the sequence ("cpp_block_sizes", default [512]).
Each render writes:

  * a 32-bit float WAV under <out>/wav/, for listening and for the artifact;
  * a 64-bit float WAV under --f64-dir, used for the PASS/FAIL gate.

The gate is max |diff| <= --threshold, applied to the float64 renders. The
threshold (1e-8) is finer than float32 resolution for any sample above 0.125,
whose float32 step is 1.5e-8. The float32 files are also compared, as
information only.

When a sequence is rendered at several block sizes and the C++ outputs are
bit-identical, only the first float32 WAV is kept in <out>. index.md names the
file each block size shares.

Sequences marked "benchmark": true are rendered once more, one at a time,
with `cymbalis_render --bench` (block size 512). This measures the real-time
factor of Engine::process.

An A/B FAIL or a C++ render error is reported, not fatal: the exit code is 0.
Pipeline errors exit non-zero: a bake failure, a Python reference failure, or
a missing binary.
"""
import argparse
import concurrent.futures as cf
import glob
import hashlib
import json
import os
import platform
import subprocess
import sys
import time

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ab_common as ab  # noqa: E402
import compare_ab  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))


def run(cmd, log_path=None):
    t0 = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    if log_path:
        with open(log_path, 'w') as f:
            f.write('$ ' + ' '.join(cmd) + '\n\n--- stdout\n' + p.stdout + '\n--- stderr\n' + p.stderr)
    return p, time.time() - t0


def git_sha():
    sha = os.environ.get('GITHUB_SHA')
    try:
        sha = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ab.REPO, capture_output=True, text=True,
                             check=True).stdout.strip()
    except Exception:
        pass
    return sha or 'unknown'


def fmt_e(x):
    return '—' if x is None else f'{x:.3e}'


def fmt_db(x):
    return '−∞' if x is None else f'{x:.1f}'


def rel(path, base):
    return os.path.relpath(path, base).replace(os.sep, '/')


# PORT_MAP section e stages, in report order.
STAGES = [
    ('0-2', 'linear core: data, PCG64, strike coupling, free ring, calibration, high-pass'),
    ('3', 'per-mode contact'),
    ('4', 'wander'),
    ('5', 'LF accumulator -> wander rate'),
    ('6', 'vel_len_tilt'),
    ('7', 'MF accumulator'),
    ('8', 'edge onset ramp + velocity tilt'),
    ('9', 'onset gains'),
    ('full', 'full engine, overrides {"nl_amount": 0.0} only: the plugin reference sound'),
]
STAGE_ORDER = {k: i for i, (k, _) in enumerate(STAGES)}


def stage_title(stage):
    return dict(STAGES).get(stage, '')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cpp', required=True, help='path to the cymbalis_render binary')
    ap.add_argument('--out', required=True, help='output directory (becomes the artifact)')
    ap.add_argument('--f64-dir', help='directory for float64 renders (default: <out>_f64, outside the artifact)')
    ap.add_argument('--sequences', default=os.path.join(ab.REPO, 'tests', 'sequences', '*.json'))
    ap.add_argument('--threshold', type=float, default=1e-8)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    f64_dir = os.path.abspath(args.f64_dir or out.rstrip('/') + '_f64')
    cpp = os.path.abspath(args.cpp)
    if not os.path.isfile(cpp):
        raise SystemExit(f'cymbalis_render not found: {cpp}')
    for d in ('bakes', 'wav', 'reports', 'logs', 'sequences'):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    os.makedirs(f64_dir, exist_ok=True)

    seq_paths = sorted(glob.glob(args.sequences))
    if not seq_paths:
        raise SystemExit(f'no sequences match {args.sequences}')
    seqs = [(p, ab.load_sequence(p)) for p in seq_paths]
    for p, s in seqs:
        with open(os.path.join(out, 'sequences', os.path.basename(p)), 'w') as f:
            json.dump(s, f, indent=2)

    # ---- 1. bake once per distinct overrides set (failure here is fatal)
    bakes = {}
    for p, s in seqs:
        key = json.dumps(s['overrides'], sort_keys=True)
        if key in bakes:
            continue
        bake_path = os.path.join(out, 'bakes', f'bake_{len(bakes)}_{ab.utc_stamp()}.json')
        print(f'baking overrides {key}', flush=True)
        proc, secs = run([sys.executable, os.path.join(TOOLS, 'bake_preset.py'), '--overrides-from', p,
                          '--out', bake_path], os.path.join(out, 'logs', f'bake_{len(bakes)}.log'))
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout + proc.stderr)
            raise SystemExit(f'bake failed for overrides {key}')
        print(proc.stdout.strip(), flush=True)
        bakes[key] = bake_path

    # ---- 2. renders (Python reference + C++ at each block size), in parallel
    jobs = []
    for p, s in seqs:
        name = s['name']
        os.makedirs(os.path.join(out, 'wav', name), exist_ok=True)
        os.makedirs(os.path.join(f64_dir, name), exist_ok=True)
        stamp = ab.utc_stamp()
        jobs.append(dict(kind='py', seq=s, seq_path=p, bs=None,
                         f32=os.path.join(out, 'wav', name, f'{name}_py_{stamp}.f32.wav'),
                         f64=os.path.join(f64_dir, name, f'{name}_py_{stamp}.f64.wav'),
                         log=os.path.join(out, 'logs', f'{name}_py.log')))
        for bs in s.get('cpp_block_sizes', [512]):
            stamp = ab.utc_stamp()
            jobs.append(dict(kind='cpp', seq=s, seq_path=p, bs=int(bs),
                             f32=os.path.join(out, 'wav', name, f'{name}_cpp_bs{bs}_{stamp}.f32.wav'),
                             f64=os.path.join(f64_dir, name, f'{name}_cpp_bs{bs}_{stamp}.f64.wav'),
                             log=os.path.join(out, 'logs', f'{name}_cpp_bs{bs}.log'),
                             bake=bakes[json.dumps(s['overrides'], sort_keys=True)]))

    def do(job):
        if job['kind'] == 'py':
            cmd = [sys.executable, os.path.join(TOOLS, 'render_reference.py'), job['seq_path'],
                   '--out', job['f32'], '--out64', job['f64']]
        else:
            cmd = [cpp, '--bake', job['bake'], '--sequence', job['seq_path'], '--block-size', str(job['bs']),
                   '--out', job['f32'], '--out64', job['f64']]
        proc, secs = run(cmd, job['log'])
        job['returncode'] = proc.returncode
        job['seconds'] = round(secs, 1)
        job['error'] = None if proc.returncode == 0 else (proc.stderr.strip().splitlines() or ['?'])[-1]
        tag = 'python' if job['kind'] == 'py' else f'c++ bs={job["bs"]}'
        print(f'  rendered {job["seq"]["name"]:22s} {tag:12s} {secs:6.1f}s'
              + ('' if proc.returncode == 0 else f'  ERROR: {job["error"]}'), flush=True)
        return job

    print(f'rendering {len(jobs)} files with {args.jobs} workers', flush=True)
    with cf.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        done = list(pool.map(do, jobs))

    py_failed = [j for j in done if j['kind'] == 'py' and j['returncode'] != 0]
    if py_failed:
        for j in py_failed:
            sys.stderr.write(open(j['log']).read())
        raise SystemExit('Python reference render failed: ' + ', '.join(j['seq']['name'] for j in py_failed))

    # ---- 3. comparisons
    py_by_name = {j['seq']['name']: j for j in done if j['kind'] == 'py'}
    rows = []
    for j in [j for j in done if j['kind'] == 'cpp']:
        s = j['seq']
        ref = py_by_name[s['name']]
        row = dict(sequence=s['name'], slot=s['slot'], stage=s.get('stage', '?'), block_size=j['bs'],
                   cpp_seconds=j['seconds'],
                   py_seconds=ref['seconds'], py_wav=ref['f32'], cpp_wav=j['f32'], error=j['error'],
                   cpp_log=j['log'])
        if j['returncode'] == 0:
            base = os.path.join(out, 'reports', f'{s["name"]}_bs{j["bs"]}')
            label = f'{s["name"]} (slot {s["slot"]}, C++ block size {j["bs"]})'
            r64 = compare_ab.compare(ref['f64'], j['f64'], args.threshold)
            r64['label'] = label + ', float64 renders'
            r32 = compare_ab.compare(ref['f32'], j['f32'], args.threshold)
            r32['label'] = label + ', float32 files'
            for suffix, r in (('f64', r64), ('f32', r32)):
                with open(f'{base}.{suffix}.json', 'w') as f:
                    json.dump(r, f, indent=1)
                with open(f'{base}.{suffix}.md', 'w') as f:
                    f.write(compare_ab.markdown(r, r['label']) + '\n')
            row.update(result='PASS' if r64['pass'] else 'FAIL', f64=r64, f32=r32,
                       report_md=f'{base}.f64.md', report_md32=f'{base}.f32.md')
        else:
            row.update(result='FAIL', f64=None, f32=None)
        rows.append(row)
    rows.sort(key=lambda r: (STAGE_ORDER.get(r['stage'], 99), r['sequence'], r['block_size']))

    # ---- 3b. speed: benchmark sequences, rendered alone, Engine::process time only
    bench = []
    for p, s in seqs:
        if not s.get('benchmark'):
            continue
        key = json.dumps(s['overrides'], sort_keys=True)
        proc, _ = run([cpp, '--bench', '--quiet', '--bake', bakes[key], '--sequence', p, '--block-size', '512'],
                      os.path.join(out, 'logs', f'{s["name"]}_bench.log'))
        if proc.returncode != 0:
            bench.append(dict(sequence=s['name'], slot=s['slot'], error=proc.stderr.strip()))
            continue
        info = json.loads(proc.stdout.strip().splitlines()[-1])
        bench.append(dict(sequence=s['name'], slot=s['slot'], audio_seconds=info['audio_seconds'],
                          render_seconds=info['render_seconds'], realtime_factor=info['realtime_factor']))
        print(f'  bench {s["name"]:22s} {info["render_seconds"]:7.2f}s for {info["audio_seconds"]:.1f}s audio '
              f'(RTF {info["realtime_factor"]:.3f})', flush=True)

    # ---- 4. block-size consistency of the C++ renders
    consistency = []
    by_seq = {}
    for j in done:
        if j['kind'] == 'cpp' and j['returncode'] == 0:
            by_seq.setdefault(j['seq']['name'], []).append(j)
    for name, js in sorted(by_seq.items()):
        if len(js) < 2:
            continue
        js.sort(key=lambda j: j['bs'])
        base, _ = sf.read(js[0]['f64'], dtype='float64')
        worst = 0.0
        for j in js[1:]:
            other, _ = sf.read(j['f64'], dtype='float64')
            worst = max(worst, float(np.max(np.abs(other - base))) if other.shape == base.shape else float('inf'))
        consistency.append(dict(sequence=name, block_sizes=[j['bs'] for j in js], max_abs_diff=worst))
        # Keep one float32 file when the renders are byte-identical.
        digests = {}
        for j in js:
            with open(j['f32'], 'rb') as f:
                d = hashlib.md5(f.read()).hexdigest()
            if d in digests:
                os.remove(j['f32'])
                for r in rows:
                    if r['sequence'] == name and r['block_size'] == j['bs']:
                        r['cpp_wav_shared_with'] = digests[d]['bs']
                        r['cpp_wav'] = digests[d]['f32']
            else:
                digests[d] = j

    # ---- 5. index.md and summary.json
    n_pass = sum(r['result'] == 'PASS' for r in rows)
    bake0 = json.load(open(next(iter(bakes.values()))))
    sha = git_sha()
    run_url = None
    if os.environ.get('GITHUB_RUN_ID'):
        run_url = (f'{os.environ.get("GITHUB_SERVER_URL", "https://github.com")}/'
                   f'{os.environ.get("GITHUB_REPOSITORY")}/actions/runs/{os.environ["GITHUB_RUN_ID"]}')
    overrides_sets = sorted(bakes.keys())

    def make_table(stage_rows):
        t = ['| Sequence | Slot | Block | max \\|diff\\| (f64) | Null peak dB | Null RMS dB | Offset | '
             'Max band Δ dB | Result | max \\|diff\\| (f32 files) |',
             '|---|---|---:|---:|---:|---:|---:|---:|:---:|---:|']
        for r in stage_rows:
            if r['f64'] is None:
                t.append(f'| {r["sequence"]} | {r["slot"]} | {r["block_size"]} | — | — | — | — | — | '
                         f'**FAIL** (C++ error: {r["error"]}) | — |')
                continue
            a, b = r['f64'], r['f32']
            t.append(
                f'| [{r["sequence"]}]({rel(r["report_md"], out)}) | {r["slot"]} | {r["block_size"]} | '
                f'{fmt_e(a["max_abs_diff"])} | {fmt_db(a["null_peak_db_re_ref_peak"])} | '
                f'{fmt_db(a["null_rms_db_re_ref_peak"])} | {a["sample_offset"]} | '
                f'{a["bands"]["max_abs_delta_db"]:.2g} | **{r["result"]}** | {fmt_e(b["max_abs_diff"])} |')
        return t

    stages_present = sorted({r['stage'] for r in rows}, key=lambda st: STAGE_ORDER.get(st, 99))
    table = []
    for st in stages_present:
        st_rows = [r for r in rows if r['stage'] == st]
        st_pass = sum(r['result'] == 'PASS' for r in st_rows)
        table += [f'### Stage {st}: {stage_title(st)} ({st_pass}/{len(st_rows)} PASS)', '',
                  *make_table(st_rows), '']

    idx = [
        '# Cymbalis A/B: Python V19 reference vs C++ engine', '',
        f'- Commit: `{sha}`',
        f'- Run: {run_url}' if run_url else '- Run: local',
        f'- Created: {ab.utc_stamp()}',
        f'- Platform: {platform.platform()}; Python {platform.python_version()}, numpy {np.__version__}',
        '- Stages compared: ' + ', '.join(stages_present) + ' (docs/PORT_MAP.md, section e)',
        f'- Bakes: {len(overrides_sets)} (one per distinct overrides set, listed under "Bake")',
        f'- Gate: max |diff| ≤ {args.threshold:g} on the float64 renders. **{n_pass}/{len(rows)} PASS.**',
        '',
        '## Results', '', *table,
        'Notes:',
        '',
        '- **Null peak / Null RMS:** peak and RMS of (C++ − Python), in dB relative to the Python peak.',
        '- **Offset:** cross-correlation lag of C++ against Python, in samples.',
        '- **Max band Δ dB:** largest 22-band 1/3-octave envelope difference at 20 ms resolution, over frames '
        'within 100 dB of the reference peak.',
        '- **WAVs in this artifact:** 32-bit float, as written by each renderer. The last column compares those '
        'files. Its value is for information only, because float32 steps are 1.5e-8 to 6e-8 for samples above '
        '0.125, which is coarser than the 1e-8 gate.',
        '',
    ]
    if bench:
        idx += ['## C++ render speed', '',
                'Full-engine 15 s sequences, block size 512, single thread, rendered one at a time on this machine. '
                'Real-time factor = time in `Engine::process` / audio duration (below 1 is faster than real time).',
                '', '| Slot | Sequence | Audio s | Render s | Real-time factor |', '|---|---|---:|---:|---:|']
        for b in bench:
            if 'error' in b:
                idx.append(f'| {b["slot"]} | {b["sequence"]} | — | — | error: {b["error"]} |')
            else:
                idx.append(f'| {b["slot"]} | {b["sequence"]} | {b["audio_seconds"]:.1f} | '
                           f'{b["render_seconds"]:.2f} | {b["realtime_factor"]:.3f} |')
        idx.append('')
    if consistency:
        idx += ['## C++ block-size consistency', '',
                '| Sequence | Block sizes | max \\|diff\\| between C++ renders |', '|---|---|---:|']
        idx += [f'| {c["sequence"]} | {", ".join(map(str, c["block_sizes"]))} | {fmt_e(c["max_abs_diff"])} |'
                for c in consistency]
        idx.append('')
    idx += ['## Bake', '']
    for key, path in bakes.items():
        b = json.load(open(path))
        idx.append(f'- [`{rel(path, out)}`]({rel(path, out)}): overrides `{key}`')
        idx += [f'  - {s["name"]}: ' + (f'overlay `{s["overlay"]}`' if s['overlay'] else 'no overlay')
                + f', position {s["position"]}, '
                f'calibration {s["calib_scalar"]!r}' for s in b['slots']]
    idx += ['', '## Files', '', '| Sequence | Python WAV | C++ WAV | Report (f64) | Report (f32) | C++ log |',
            '|---|---|---|---|---|---|']
    for r in rows:
        md64 = f'[md]({rel(r["report_md"], out)})' if r.get('report_md') else '—'
        md32 = f'[md]({rel(r["report_md32"], out)})' if r.get('report_md32') else '—'
        shared = (f' (bit-identical to bs {r["cpp_wav_shared_with"]})' if r.get('cpp_wav_shared_with') else '')
        idx.append(f'| {r["sequence"]} (bs {r["block_size"]}) | [{os.path.basename(r["py_wav"])}]'
                   f'({rel(r["py_wav"], out)}) | [{os.path.basename(r["cpp_wav"])}]({rel(r["cpp_wav"], out)})'
                   f'{shared} | {md64} | {md32} | [log]({rel(r["cpp_log"], out)}) |')
    idx.append('')
    with open(os.path.join(out, 'index.md'), 'w') as f:
        f.write('\n'.join(idx))

    summary = dict(commit=sha, run=run_url, threshold=args.threshold, passed=n_pass, total=len(rows),
                   python=dict(version=platform.python_version(), numpy=np.__version__),
                   bake_python=bake0['python'],
                   rows=[{k: (v if k not in ('f64', 'f32') else
                              (None if v is None else {kk: v[kk] for kk in
                               ('max_abs_diff', 'null_peak_db_re_ref_peak', 'null_rms_db_re_ref_peak',
                                'sample_offset', 'samples_differing', 'pass')} | {
                                   'max_band_delta_db': v['bands']['max_abs_delta_db']}))
                          for k, v in r.items()} for r in rows],
                   block_size_consistency=consistency, benchmark=bench)
    with open(os.path.join(out, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=1)

    print('\n'.join(table))
    print(f'\n{n_pass}/{len(rows)} PASS (gate: max |diff| <= {args.threshold:g}, float64 renders)')
    for c in consistency:
        print(f'block-size consistency {c["sequence"]}: {c["block_sizes"]} max |diff| {fmt_e(c["max_abs_diff"])}')

    gh_summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if gh_summary:
        speed = []
        if bench:
            speed = ['', '### C++ render speed (full engine, 15 s, block 512)', '',
                     '| Slot | Render s | Real-time factor |', '|---|---:|---:|']
            speed += [f'| {b["slot"]} | {b.get("render_seconds", float("nan")):.2f} | '
                      f'{b.get("realtime_factor", float("nan")):.3f} |' for b in bench]
        with open(gh_summary, 'a') as f:
            f.write('## Cymbalis A/B (Python V19 vs C++)\n\n'
                    f'{n_pass}/{len(rows)} PASS (max |diff| ≤ {args.threshold:g}, float64 renders)\n\n'
                    + '\n'.join(table + speed) + '\n')
    for r in rows:
        if r['result'] != 'PASS':
            detail = r['error'] or f'max |diff| {fmt_e(r["f64"]["max_abs_diff"])}'
            print(f'::warning title=A/B FAIL::{r["sequence"]} block {r["block_size"]}: {detail}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
