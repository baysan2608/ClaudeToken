#!/usr/bin/env python3
# Summarises an Unreal CSV profile (-csvCaptureFrames=N; files in ~/Library/Application Support/Epic/UnrealEngine/5.8/
# Saved/Profiling/CSV) around the "ff.fx showcase <cue>" events that ff.fx.Showcase writes: GPU time in the 1.5 s after
# each cue vs. the rest of the run. Run once with ff.fx.Niagara 1 and once with 0 to price the Niagara cue layer.
#   python3 unreal/Tools/vfx/csv_fx.py "<profile>.csv"
import csv, statistics, sys, collections
csv.field_size_limit(1 << 30)
def load(path):
    with open(path, newline='') as fh:
        rows = list(csv.reader(fh))
    head = rows[0]
    data = []
    for r in rows[1:]:
        if not r or r[0] in ('EVENTS',) or r[0].startswith('[HasHeaderRowAtEnd]') or len(r) < len(head) - 2:
            continue
        data.append(r)
    return head, data
def col(head, data, name):
    if name not in head:
        return None
    i = head.index(name)
    out = []
    for r in data:
        try: out.append(float(r[i]))
        except (ValueError, IndexError): out.append(float('nan'))
    return out
def main(path, window_ms=1500.0):
    head, data = load(path)
    ev = [r[head.index('EVENTS')] if 'EVENTS' in head else '' for r in data]
    ft = col(head, data, 'FrameTime'); gpu = col(head, data, 'GPUTime')
    extra = [c for c in head if c in ('GPU/Translucency', 'GPU/FXSystemPreRender', 'GPU/FXSystemPostRenderOpaque', 'GPU/FXSystemPostRenderFinal', 'Exclusive/RenderThread/Niagara')]
    cols = {c: col(head, data, c) for c in extra}
    inwin = [False] * len(data)
    per = collections.defaultdict(list)
    for i, e in enumerate(ev):
        if 'ff.fx showcase' not in e:
            continue
        name = e.split('ff.fx showcase')[-1].split(';')[0].strip()
        t = 0.0; j = i
        while j < len(data) and t < window_ms:
            inwin[j] = True; per[name].append(j); t += ft[j]; j += 1
    base = [gpu[i] for i in range(len(data)) if not inwin[i] and i > 60]
    print(f"{path.split('/')[-1]}: {len(data)} frames, median frame {statistics.median(ft):.1f} ms, median GPU (outside cue windows) {statistics.median(base):.2f} ms")
    for name, idx in sorted(per.items()):
        g = [gpu[i] for i in idx]
        line = f"  {name:16s} GPU mean {statistics.mean(g):6.2f}  max {max(g):6.2f}"
        for c, v in cols.items():
            line += f"  {c.split('/')[-1]} {statistics.mean([v[i] for i in idx]):.2f}"
        print(line)
if __name__ == '__main__':
    main(sys.argv[1])
