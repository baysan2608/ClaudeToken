#!/usr/bin/env python3
"""Downloads Unity Labs' 2016 VFX image sequences / flipbooks (CC0, creativecommons.org/publicdomain/zero/1.0/;
https://unity.com/blog/engine-platform/free-vfx-image-sequences-flipbooks) - offline fluid sims of flames, fireballs,
explosions and smoke - and converts the TGA flipbook atlases to PNG for the Unreal import:
  unreal/SourceArt/VFX/Flipbooks/CC0/<Name>_<cols>x<rows>.png   (git-ignored) + cc0_flipbooks.json (grid, source, license)

  python3 unreal/Tools/vfx/fetch_cc0_flipbooks.py [--only Flame02,Explosion01]
"""
import argparse
import io
import json
import os
import re
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "VFX", "Flipbooks", "CC0"))
BASE = "https://unity3d.com/files/labs/downloads/vfx/assets01"
SETS = ["Flame02", "Flame03", "FireBall01", "FireBall02", "FireBall03", "Explosion00", "Explosion01", "Explosion02",
        "Explosion01-nofire", "WispySmoke01", "WispySmoke02", "WispySmoke03", "DiscSmoke01", "Cloud01", "Cloud02"]
UA = {"User-Agent": "Fourfold-asset-fetch/1.0"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {s for s in a.only.split(",") if s}
    os.makedirs(OUT, exist_ok=True)
    from PIL import Image
    man_path = os.path.join(OUT, "cc0_flipbooks.json")
    man = json.load(open(man_path)) if os.path.exists(man_path) else {
        "license": "CC0 1.0 (Unity Labs VFX image sequences, 2016)", "source": "https://unity.com/blog/engine-platform/free-vfx-image-sequences-flipbooks",
        "sets": {}}
    for name in SETS:
        if only and name not in only:
            continue
        url = f"{BASE}/{name}/{name}-flipbooks.zip"
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r:
            data = r.read()
        z = zipfile.ZipFile(io.BytesIO(data))
        tgas = [n for n in z.namelist() if n.lower().endswith(".tga")]
        if not tgas:
            print(f"{name}: no TGA in the zip ({z.namelist()})")
            continue
        src = tgas[0]
        m = re.search(r"_(\d+)x(\d+)\.tga$", src, re.I)
        cols, rows = (int(m.group(1)), int(m.group(2))) if m else (8, 8)
        img = Image.open(io.BytesIO(z.read(src))).convert("RGBA")
        dst = os.path.join(OUT, f"T_FX_CC0_{name.replace('-', '_')}.png")
        img.save(dst, optimize=True)
        man["sets"][name] = {"file": os.path.basename(dst), "cols": cols, "rows": rows, "size": list(img.size), "zip": url}
        print(f"{name:20s} {cols}x{rows} {img.size} -> {os.path.basename(dst)}")
    json.dump(man, open(man_path, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
