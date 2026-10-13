#!/usr/bin/env python3
"""Turns the CC0 Unity Labs sims (fetch_cc0_flipbooks.py) into the house flipbook format the FX materials read
(flipbooks.json: "R light, G thickness, B temperature, A alpha", 8 x 8 frames) and replaces the numpy-solver atlases:

  fire_loop  <- Flame02         fire_burst <- FireBall01      explosion <- Explosion01
  smoke_puff <- WispySmoke02    steam_puff <- WispySmoke03    dust_puff <- Explosion01-nofire
  sand_burst <- Explosion01-nofire (warmer grade in the material)    water_splash / puff_atlas: unchanged

Frames are resampled in time to 64 (linear blend between neighbours). Channels from the straight-alpha colour:
  A = alpha, G = alpha (thickness), R = luminance / max-lum (lighting), B = black-body temperature estimate.
  python3 unreal/Tools/vfx/cc0_flipbooks_to_house.py [--tile 256]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FB = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "VFX", "Flipbooks"))
SRC = os.path.join(FB, "CC0")
MAP = {
    "fire_loop": ("Flame02", 256),
    "fire_burst": ("FireBall01", 256),
    "explosion": ("Explosion01", 256),
    "smoke_puff": ("WispySmoke02", 128),
    "steam_puff": ("WispySmoke03", 128),
    "dust_puff": ("Explosion01-nofire", 128),
    "sand_burst": ("Explosion01-nofire", 128),
}


def frames_of(path, cols, rows):
    im = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
    h, w = im.shape[:2]
    th, tw = h // rows, w // cols
    return [im[r * th:(r + 1) * th, c * tw:(c + 1) * tw] for r in range(rows) for c in range(cols)]


def resample(frames, n):
    out = []
    m = len(frames)
    for i in range(n):
        t = i * (m - 1) / max(n - 1, 1)
        a = int(np.floor(t))
        b = min(a + 1, m - 1)
        k = t - a
        out.append(frames[a] * (1 - k) + frames[b] * k)
    return out


def to_house(f, tile):
    img = Image.fromarray((np.clip(f, 0, 1) * 255).astype(np.uint8), "RGBA").resize((tile, tile), Image.LANCZOS)
    f = np.asarray(img, dtype=np.float32) / 255.0
    rgb, a = f[..., :3], f[..., 3]
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    # temperature: hot = bright and red-over-blue (flame / fireball cores); grey smoke stays cold
    warm = np.clip((rgb[..., 0] - rgb[..., 2]) * 1.6, 0, 1)
    temp = np.clip(warm * np.clip(lum * 1.4, 0, 1) + np.clip((lum - 0.75) * 2.0, 0, 1) * 0.6, 0, 1)
    return np.stack([lum, a, temp, a], axis=-1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tile", type=int, default=0, help="override tile px for every flipbook")
    a = ap.parse_args()
    man = json.load(open(os.path.join(SRC, "cc0_flipbooks.json")))
    meta = json.load(open(os.path.join(FB, "flipbooks.json")))
    for key, (name, tile) in MAP.items():
        s = man["sets"].get(name)
        if not s:
            print(f"{key}: {name} missing (run fetch_cc0_flipbooks.py)")
            continue
        tile = a.tile or tile
        frames = resample(frames_of(os.path.join(SRC, s["file"]), s["cols"], s["rows"]), 64)
        atlas = np.zeros((tile * 8, tile * 8, 4), dtype=np.float32)
        for i, f in enumerate(frames):
            r, c = divmod(i, 8)
            atlas[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile] = to_house(f, tile)
        dst = os.path.join(FB, f"T_FX_FB_{key}.png")
        Image.fromarray((np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(dst, optimize=True)
        e = meta["flipbooks"][key]
        e.update({"frames": 64, "grid": 8, "tile": tile, "source": f"CC0 Unity Labs VFX sequences: {name}",
                  "generator": "unreal/Tools/vfx/cc0_flipbooks_to_house.py"})
        print(f"{key:12s} <- {name:20s} {tile * 8}px")
    meta["license_notes"] = "fire / smoke / explosion atlases: CC0 1.0 (Unity Labs 2016 VFX image sequences); others: generated"
    json.dump(meta, open(os.path.join(FB, "flipbooks.json"), "w"), indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
