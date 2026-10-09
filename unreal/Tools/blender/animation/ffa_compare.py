"""Fourfold animation toolkit - before / after review (numpy + PIL; MP4 through ffmpeg or imageio-ffmpeg).

    # 1. snapshot the solved poses of a toolkit version (e.g. a copy of the toolkit from git HEAD)
    python3 ffa_compare.py dump --toolkit <dir> --only "f_cross,e_strike" --out before.pkl
    # 2. side-by-side MP4s / strips of that snapshot against the current catalogue (or a second snapshot)
    python3 ffa_compare.py video --before before.pkl [--after after.pkl] --out <dir> [--views game,side] [--slow 2]
    python3 ffa_compare.py strip --before before.pkl --out <dir> [--every 2]

The dump runs in its own process (the toolkit modules share names), so pass --toolkit for older versions.
"""
import argparse
import fnmatch
import os
import pickle
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


class Snap:
    """A solved clip without its builder: enough for the renderer and the audit."""

    class _C:
        pass

    def __init__(self, d):
        self.name = d["name"]
        self.frames = d["frames"]
        self.loop = d["loop"]
        self.plants = d["plants"]
        self.poses = d["poses"]
        self.clip = Snap._C()
        self.clip.contacts = d["contacts"]
        self.clip.loop = d["loop"]
        self.clip.strike = d.get("strike")
        self.clip.technique = d.get("technique", "")
        self.clip.priority = d.get("priority", "")


def _select(cat, only):
    pats = [p.strip() for p in only.split(",")] if only else None
    return [n for n in cat if not n.startswith("hand_") and (not pats or any(fnmatch.fnmatch(n, p) for p in pats))]


def dump(toolkit, only, out):
    toolkit = os.path.abspath(toolkit)
    sys.path.insert(0, toolkit)
    sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "common")))
    import clips as catalog  # noqa: E402  (from the given toolkit)
    cat = catalog.load()
    data = {}
    for n in _select(cat, only):
        r = cat[n]().build()
        c = r.clip
        data[n] = dict(name=n, frames=r.frames, loop=c.loop, plants=r.plants, poses=r.poses, contacts=list(c.contacts),
                       strike=c.strike, technique=c.technique, priority=c.priority)
    with open(out, "wb") as f:
        pickle.dump(data, f)
    print(f"dumped {len(data)} clips -> {out}")


def _load(path):
    with open(path, "rb") as f:
        return {k: Snap(v) for k, v in pickle.load(f).items()}


def _current(names):
    sys.path.insert(0, HERE)
    import clips as catalog
    cat = catalog.load()
    out = {}
    for n in names:
        if n in cat:
            r = cat[n]().build()
            out[n] = r
    return out


def ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:  # noqa: BLE001
        raise RuntimeError("no ffmpeg: install it or `pip3 install --user imageio-ffmpeg`")


def _pair_frame(b, a, f, views, pw, ph):
    from PIL import Image, ImageDraw
    import ffa_render as render
    img = Image.new("RGB", (pw * 2, ph * len(views)), (240, 240, 240))
    for side, res in enumerate((b, a)):
        if res is None:
            continue
        g = min(f, res.frames)
        sh = render.clip_shift(res)
        pl = render._planted_at(res, g)
        fl = ["contact"] if g in (res.clip.contacts or []) else None
        for r, view in enumerate(views):
            lab = f"{'BEFORE' if side == 0 else 'AFTER'}  {res.name}  f{g}/{res.frames}" if r == 0 else view
            im = render.frame_image(res.poses[g], view, (pw, ph), label=lab, plants=pl, flags=fl, shift=sh)
            img.paste(im, (side * pw, r * ph))
    d = ImageDraw.Draw(img)
    d.line([(pw, 0), (pw, ph * len(views))], fill=(90, 90, 90), width=2)
    return img


def video(before, after, outdir, views=("game", "side"), panel=(360, 300), slow=1, loops=2):
    os.makedirs(outdir, exist_ok=True)
    exe = ffmpeg()
    paths = []
    for n, b in before.items():
        a = after.get(n)
        n_fr = max(b.frames, a.frames if a else 0)
        loop = b.loop
        seq = list(range(n_fr)) * loops if loop else list(range(n_fr + 1)) + [n_fr] * 20
        tmp = tempfile.mkdtemp(prefix="ffa_cmp_")
        cache = {}
        k = 0
        for f in seq:
            if f not in cache:
                p = os.path.join(tmp, f"c{f:04d}.png")
                _pair_frame(b, a, f, views, *panel).save(p)
                cache[f] = p
            for _ in range(slow):
                shutil.copy(cache[f], os.path.join(tmp, f"f{k:05d}.png"))
                k += 1
        path = os.path.join(outdir, f"{n}_before_after.mp4")
        cmd = [exe, "-y", "-loglevel", "error", "-framerate", "60", "-i", os.path.join(tmp, "f%05d.png"),
               "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "28", "-preset", "slow", "-movflags", "+faststart", path]
        subprocess.run(cmd, check=True)
        shutil.rmtree(tmp, ignore_errors=True)
        paths.append(path)
        print(path, flush=True)
    return paths


def strip(before, after, outdir, every=2, panel=(150, 210), view="side"):
    """Two-row film strip per clip: BEFORE above, AFTER below (same frames)."""
    from PIL import Image, ImageDraw
    import ffa_render as render
    os.makedirs(outdir, exist_ok=True)
    for n, b in before.items():
        a = after.get(n)
        n_fr = max(b.frames, a.frames if a else 0)
        fr = list(range(0, n_fr + 1, every))
        if fr[-1] != n_fr:
            fr.append(n_fr)
        head = 22
        sheet = Image.new("RGB", (panel[0] * len(fr), panel[1] * 2 + head), (246, 246, 244))
        d = ImageDraw.Draw(sheet)
        d.text((6, 4), f"{n}: before (top) / after (bottom), {view} view, every {every} frames", fill=(20, 20, 20),
               font=render.font(13))
        for r, res in enumerate((b, a)):
            if res is None:
                continue
            sh = render.clip_shift(res)
            for c, f in enumerate(fr):
                g = min(f, res.frames)
                fl = ["contact"] if g in (res.clip.contacts or []) else None
                im = render.frame_image(res.poses[g], view, panel, label=f"f{g}", plants=render._planted_at(res, g),
                                        flags=fl, shift=sh)
                sheet.paste(im, (c * panel[0], head + r * panel[1]))
        p = os.path.join(outdir, f"{n}_before_after{'' if view == 'side' else '_' + view}.jpg")
        sheet.save(p, quality=82, optimize=True)
        print(p, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=("dump", "video", "strip"))
    ap.add_argument("--toolkit", default=HERE)
    ap.add_argument("--only", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--before", default="")
    ap.add_argument("--after", default="")
    ap.add_argument("--views", default="game,side")
    ap.add_argument("--slow", type=int, default=1)
    ap.add_argument("--every", type=int, default=2)
    a = ap.parse_args()
    if a.mode == "dump":
        return dump(a.toolkit, a.only, a.out)
    sys.path.insert(0, HERE)
    before = _load(a.before)
    if a.only:
        before = {n: v for n, v in before.items() if n in _select(before, a.only)}
    after = _load(a.after) if a.after else _current(list(before))
    if a.mode == "video":
        video(before, after, a.out, views=tuple(a.views.split(",")), slow=a.slow)
    else:
        for v in a.views.split(","):
            strip(before, after, a.out, every=a.every, view=v)


if __name__ == "__main__":
    import numpy as np
    with np.errstate(all="ignore"):
        main()
