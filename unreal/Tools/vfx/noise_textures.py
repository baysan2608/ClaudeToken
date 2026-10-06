"""Tileable noise textures for the FX materials (stream `fx`), deterministic numpy.

    python3 unreal/Tools/vfx/noise_textures.py [--out unreal/SourceArt/VFX/Textures] [--raw <file.rgba8>] [--preview <png>]

T_FX_Noise.png  256 x 256 RGBA8, LINEAR data (import with sRGB off, compression Masks), every channel tiles seamlessly:
    R  fbm of periodic gradient noise (3 octaves, 8 cells per tile at the base octave) - soft billows, tonal variation
    G  ridged gradient noise (3 octaves, 6 cells) - sharp creases: flame streaks, wind streaks, crackle
    B  cellular distance F1 (14 cells, jittered) - puffy cells; 1 - |2B - 1| draws thin rings (static crackle)
    A  fine grain (2 octaves, 24 cells)
Each channel is percentile-stretched to 0..1 (0.5 % .. 99.5 %) so the shader thresholds see a stable range.
The contract is quoted in Shaders/Common/FFNoise.ush.
"""
import argparse
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.abspath(os.path.join(HERE, "..", ".."))
SIZE = 256


def _fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def periodic_gradient(size, cells, rng):
    """Perlin gradient noise on a `cells` x `cells` torus lattice, sampled at size x size. Range about -1..1."""
    ang = rng.uniform(0.0, 2.0 * np.pi, (cells, cells))
    gx, gy = np.cos(ang), np.sin(ang)
    coords = np.arange(size) * (cells / size)
    x, y = np.meshgrid(coords, coords)            # y rows, x columns
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    x0, y0 = x0 % cells, y0 % cells

    def dot(ix, iy, dx, dy):
        return gx[iy, ix] * dx + gy[iy, ix] * dy

    n00 = dot(x0, y0, fx, fy)
    n10 = dot(x1, y0, fx - 1.0, fy)
    n01 = dot(x0, y1, fx, fy - 1.0)
    n11 = dot(x1, y1, fx - 1.0, fy - 1.0)
    u, v = _fade(fx), _fade(fy)
    return 1.41421356 * ((n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v)


def fbm(size, cells, octaves, rng, gain=0.5):
    out = np.zeros((size, size))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        out += amp * periodic_gradient(size, cells * (2 ** o), rng)
        norm += amp
        amp *= gain
    return out / norm


def ridged(size, cells, octaves, rng):
    out = np.zeros((size, size))
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        r = 1.0 - np.abs(periodic_gradient(size, cells * (2 ** o), rng))
        out += amp * r * r
        norm += amp
        amp *= 0.5
    return out / norm


def cellular(size, cells, rng):
    """F1 distance to jittered feature points on a torus (in cell units)."""
    pts = (np.stack(np.meshgrid(np.arange(cells), np.arange(cells)), -1) + rng.uniform(0.1, 0.9, (cells, cells, 2)))
    pts = pts.reshape(-1, 2)
    coords = (np.arange(size) + 0.5) * (cells / size)
    x, y = np.meshgrid(coords, coords)
    best = np.full((size, size), 1e9)
    for px, py in pts:
        dx = np.abs(x - px)
        dy = np.abs(y - py)
        dx = np.minimum(dx, cells - dx)
        dy = np.minimum(dy, cells - dy)
        best = np.minimum(best, dx * dx + dy * dy)
    return np.sqrt(best)


def stretch(a, lo=0.5, hi=99.5):
    p0, p1 = np.percentile(a, [lo, hi])
    return np.clip((a - p0) / max(p1 - p0, 1e-9), 0.0, 1.0)


def make_noise(seed=7):
    rng = np.random.default_rng(seed)
    r = stretch(fbm(SIZE, 8, 3, rng))
    g = stretch(ridged(SIZE, 6, 3, rng))
    b = stretch(cellular(SIZE, 14, rng))
    a = stretch(fbm(SIZE, 24, 2, rng, gain=0.45))
    return np.stack([r, g, b, a], -1)


def save_png(rgba01, path):
    from PIL import Image
    img = Image.fromarray(np.clip(np.round(rgba01 * 255.0), 0, 255).astype(np.uint8), "RGBA")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)


def tile_error(rgba01):
    """Max jump across the wrap seam relative to the max jump inside (should be ~<= 1)."""
    d_in = np.abs(np.diff(rgba01, axis=1)).max(axis=(0, 1))
    d_wrap = np.abs(rgba01[:, 0] - rgba01[:, -1]).max(axis=0)
    return d_wrap / np.maximum(d_in, 1e-6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(UNREAL, "SourceArt", "VFX", "Textures"))
    ap.add_argument("--raw", help="also write raw RGBA8 bytes (for the C++ preview renderer)")
    ap.add_argument("--preview", help="write a 2 x 2 tiled contact sheet of the four channels")
    a = ap.parse_args()
    n = make_noise()
    save_png(n, os.path.join(a.out, "T_FX_Noise.png"))
    print("tile seam ratio per channel:", np.round(tile_error(n), 2))
    if a.raw:
        np.clip(np.round(n * 255.0), 0, 255).astype(np.uint8).tofile(a.raw)
    if a.preview:
        from PIL import Image
        tiles = []
        for c in range(4):
            ch = np.tile(n[..., c], (2, 2))
            tiles.append(ch)
        top = np.concatenate(tiles[:2], 1)
        bot = np.concatenate(tiles[2:], 1)
        sheet = np.concatenate([top, bot], 0)
        Image.fromarray((sheet * 255).astype(np.uint8), "L").save(a.preview)
    print("wrote", os.path.join(a.out, "T_FX_Noise.png"))


if __name__ == "__main__":
    main()
