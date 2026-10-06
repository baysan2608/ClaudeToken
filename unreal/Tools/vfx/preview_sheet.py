"""Contact sheets from ffx_preview frames (stream `fx`): one row per shot (<shot>_<i>.ppm), frames left to right.

    python3 unreal/Tools/vfx/preview_sheet.py <frames dir> <out.png> [--filter text] [--scale 0.5] [--cols 4]
"""
import argparse
import glob
import os
import re

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("out")
    ap.add_argument("--filter", default="")
    ap.add_argument("--scale", type=float, default=0.5)
    a = ap.parse_args()
    shots = {}
    for p in sorted(glob.glob(os.path.join(a.frames, "*.ppm"))):
        m = re.match(r"(.+)_(\d+)\.ppm$", os.path.basename(p))
        if not m or a.filter not in m.group(1):
            continue
        shots.setdefault(m.group(1), []).append((int(m.group(2)), p))
    if not shots:
        raise SystemExit("no frames")
    rows = []
    for name in sorted(shots):
        imgs = [Image.open(p) for _, p in sorted(shots[name])]
        w, h = imgs[0].size
        sw, sh = int(w * a.scale), int(h * a.scale)
        row = Image.new("RGB", (sw * len(imgs), sh + 14), (20, 20, 20))
        for i, im in enumerate(imgs):
            row.paste(im.resize((sw, sh), Image.LANCZOS), (i * sw, 14))
        ImageDraw.Draw(row).text((4, 1), name, fill=(255, 255, 200))
        rows.append(row)
    W = max(r.size[0] for r in rows)
    H = sum(r.size[1] for r in rows)
    sheet = Image.new("RGB", (W, H), (0, 0, 0))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.size[1]
    sheet.save(a.out)
    print(a.out, sheet.size)


if __name__ == "__main__":
    main()
