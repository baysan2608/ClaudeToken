#!/usr/bin/env python3
# Summarises the GPU passes of an Unreal CSV profile (shot.sh ... -csvCaptureFrames=600 -csvGpuStats
# -ExecCmds="csvprofile start", or -FFExec="30:csvprofile frames=300|..."; files in unreal/Saved/Profiling/CSV or
# ~/Library/Application Support/Epic/UnrealEngine/5.8/Saved/Profiling/CSV).
#   python3 unreal/Tools/mac/csv_gpu.py [profile.csv]       (default: newest profile)
#   python3 unreal/Tools/mac/csv_gpu.py a.csv b.csv         (A/B: per-pass delta b - a)
# Skips the first 120 frames (30 in short captures) (shader / streaming warm-up). Prints median frame / GPU time and the per-pass means, biggest first.
import csv, glob, os, statistics, sys
csv.field_size_limit(1 << 30)
CSV_DIRS = [os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'Profiling', 'CSV'),
            os.path.expanduser('~/Library/Application Support/Epic/UnrealEngine/5.8/Saved/Profiling/CSV')]
SKIP = 120   # long captures; short (-FFExec "csvprofile frames=N") ones skip 30


def load(path):
    with open(path, newline='') as fh:
        rows = list(csv.reader(fh))
    head = rows[0]
    data = [r for r in rows[1:] if r and not r[0].startswith('[') and len(r) >= len(head) - 2]
    data = data[SKIP if len(data) > 2 * SKIP + 200 else 30:]
    out = {}
    for i, name in enumerate(head):
        vals = []
        for r in data:
            try:
                vals.append(float(r[i]))
            except (ValueError, IndexError):
                pass
        if vals:
            out[name] = vals
    return out, len(data)


def summary(path):
    cols, n = load(path)
    passes = {k[4:]: statistics.mean(v) for k, v in cols.items() if k.startswith('GPU/')}
    med = {k: statistics.median(cols[k]) for k in ('FrameTime', 'GPUTime', 'RenderThreadTime', 'GameThreadTime') if k in cols}
    return med, passes, n


def main(args):
    paths = args or [max([f for d in CSV_DIRS for f in glob.glob(os.path.join(d, '*.csv'))], key=os.path.getmtime)]
    sums = [summary(p) for p in paths]
    for p, (med, passes, n) in zip(paths, sums):
        print(f"{os.path.basename(p)}: {n} frames  " + '  '.join(f"{k} {v:.2f}" for k, v in med.items()))
    if len(sums) == 1:
        med, passes, _ = sums[0]
        for k, v in sorted(passes.items(), key=lambda kv: -kv[1]):
            if v >= 0.05:
                print(f"  {k:32s} {v:6.2f}")
        print(f"  {'(sum of passes)':32s} {sum(passes.values()):6.2f}")
    else:
        (_, a, _), (_, b, _) = sums[0], sums[1]
        keys = sorted(set(a) | set(b), key=lambda k: -max(a.get(k, 0), b.get(k, 0)))
        for k in keys:
            va, vb = a.get(k, 0.0), b.get(k, 0.0)
            if max(va, vb) >= 0.05:
                print(f"  {k:32s} {va:6.2f} -> {vb:6.2f}  ({vb - va:+.2f})")


if __name__ == '__main__':
    main(sys.argv[1:])
