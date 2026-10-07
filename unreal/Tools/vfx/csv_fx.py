#!/usr/bin/env python3
# Summarises an Unreal CSV profile (-csvCaptureFrames=N; files in ~/Library/Application Support/Epic/UnrealEngine/5.8/
# Saved/Profiling/CSV) around the "ff.fx showcase <cue>" events that ff.fx.Showcase writes (and the "ff.fx prewarm"
# event): per cue window (1.5 s after the event) the GPU mean / max, the worst frame / render-thread time and
# the pipeline (PSO) misses + hitches, first use of each cue separately from its repeats, vs. the rest of the run.
# Run once with ff.fx.Niagara 1 and once with 0 to price the Niagara cue layer; with ff.fx.Prewarm 3 vs 0 to
# check the pre-warm (first-use PSO misses should move from the first cues to the prewarm window).
#   python3 unreal/Tools/vfx/csv_fx.py "<profile>.csv" [more profiles...]
import collections, csv, math, statistics, sys

csv.field_size_limit(1 << 30)
WINDOW_MS = 1500.0


def load(path):
    with open(path, newline='') as fh:
        rows = list(csv.reader(fh))
    head = rows[0]
    data = [r for r in rows[1:] if r and r[0] != 'EVENTS' and not r[0].startswith('[HasHeaderRowAtEnd]')
            and len(r) >= len(head) - 2]
    return head, data


def column(head, data, name):
    if name not in head:
        return [math.nan] * len(data)
    i = head.index(name)
    out = []
    for r in data:
        try:
            out.append(float(r[i]))
        except (ValueError, IndexError):
            out.append(math.nan)
    return out


def nz(v):
    return 0.0 if math.isnan(v) else v


def summarise(path):
    head, data = load(path)
    ev = [r[head.index('EVENTS')] if 'EVENTS' in head else '' for r in data]
    c = {k: column(head, data, k) for k in ('FrameTime', 'GPUTime', 'RenderThreadTime', 'GameThreadTime', 'PSO/PSOMisses',
                                             'PSO/PSOComputeMisses', 'PSO/GraphicsPSOHitch', 'PSO/GraphicsPSOHitchTime')}
    windows = []   # (name, frame indices)
    for i, e in enumerate(ev):
        for part in e.split(';'):
            part = part.split('##')[0].strip()
            if not part.startswith('ff.fx showcase') and not part.startswith('ff.fx prewarm'):
                continue
            name = part.replace('ff.fx showcase', '').strip() or part
            t, j, idx = 0.0, i, []
            while j < len(data) and t < WINDOW_MS:
                idx.append(j)
                t += nz(c['FrameTime'][j])
                j += 1
            windows.append((name, idx))
    inwin = set(j for _, idx in windows for j in idx)
    base = [c['GPUTime'][i] for i in range(len(data)) if i not in inwin and i > 60 and not math.isnan(c['GPUTime'][i])]
    name = path.split('/')[-1]
    print(f"{name}: {len(data)} frames, median frame {statistics.median([nz(v) for v in c['FrameTime']]):.1f} ms, "
          f"median GPU outside cue windows {statistics.median(base) if base else math.nan:.2f} ms")
    print(f"  {'cue':22s} {'use':>3s} {'GPUmean':>8s} {'GPUmax':>7s} {'frameMax':>8s} {'RTmax':>7s} {'PSOmiss':>7s} "
          f"{'cmpMiss':>7s} {'hitchMs':>7s}")
    seen = collections.Counter()
    first, later = collections.defaultdict(list), collections.defaultdict(list)
    for cue, idx in windows:
        seen[cue] += 1
        g = [c['GPUTime'][i] for i in idx if not math.isnan(c['GPUTime'][i])]
        row = (statistics.mean(g) if g else math.nan, max(g) if g else math.nan,
               max(nz(c['FrameTime'][i]) for i in idx), max(nz(c['RenderThreadTime'][i]) for i in idx),
               sum(nz(c['PSO/PSOMisses'][i]) for i in idx), sum(nz(c['PSO/PSOComputeMisses'][i]) for i in idx),
               sum(nz(c['PSO/GraphicsPSOHitchTime'][i]) for i in idx))
        (first if seen[cue] == 1 else later)[cue].append(row)
        print(f"  {cue:22s} {seen[cue]:3d} {row[0]:8.2f} {row[1]:7.2f} {row[2]:8.1f} {row[3]:7.1f} {row[4]:7.0f} "
              f"{row[5]:7.0f} {row[6]:7.1f}")
    tot_first = [sum(r[k] for rows in first.values() for r in rows) for k in (4, 5, 6)]
    tot_later = [sum(r[k] for rows in later.values() for r in rows) for k in (4, 5, 6)]
    print(f"  first uses: {sum(len(v) for v in first.values())} windows, PSO misses {tot_first[0]:.0f} graphics + "
          f"{tot_first[1]:.0f} compute, hitch {tot_first[2]:.1f} ms; repeats: {sum(len(v) for v in later.values())} "
          f"windows, PSO misses {tot_later[0]:.0f} + {tot_later[1]:.0f}, hitch {tot_later[2]:.1f} ms")


if __name__ == '__main__':
    for p in sys.argv[1:]:
        summarise(p)
