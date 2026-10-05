"""Banner cloth atlas with the four original element glyphs (earth / water / fire / air).

    /home/user/tools/bpyenv/bin/python tools/textures/gen_banners.py [--out DIR]

Writes game/assets/textures/banners_albedo.png (RGBA, 1024 x 640: four 256 x 640 banners side by side,
alpha = swallow-tail cut-out) and its .import.  Glyphs are signed-distance drawn (anti-aliased) from
simple geometry: earth = nested diamonds with a keystone bar, water = three swells, fire = a flame
outline with an inner ember, air = a one-turn spiral with three drifting dots.  Cloth weave + thread
noise are procedural.  Deterministic.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texgen_common import *   # noqa: F401,F403
import texgen_common as tc

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "game", "assets", "textures")
CW, CH = 256, 640


def aa(d, px):
    """signed distance (in glyph units) -> coverage; px = glyph units per pixel."""
    return np.clip(0.5 - d / px, 0.0, 1.0)


def sd_seg(p, a, b):
    pa = p - np.array(a)[None, None, :]
    ba = np.array(b) - np.array(a)
    h = np.clip((pa @ ba) / (ba @ ba), 0, 1)
    return np.linalg.norm(pa - h[..., None] * ba[None, None, :], axis=-1)


def sd_tri(p, a, b, c):
    # distance to filled triangle (negative inside)
    def edge(u, v):
        return sd_seg(p, u, v)
    d = np.minimum(np.minimum(edge(a, b), edge(b, c)), edge(c, a))

    def side(u, v):
        return (p[..., 0] - u[0]) * (v[1] - u[1]) - (p[..., 1] - u[1]) * (v[0] - u[0])
    s1, s2, s3 = side(a, b), side(b, c), side(c, a)
    inside = ((s1 >= 0) & (s2 >= 0) & (s3 >= 0)) | ((s1 <= 0) & (s2 <= 0) & (s3 <= 0))
    return np.where(inside, -d, d)


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def glyph(kind, size=200):
    """Returns coverage 0..1 (size x size) for the glyph in [-1,1]^2 (y up)."""
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    x = (xs + 0.5) / size * 2 - 1
    y = -((ys + 0.5) / size * 2 - 1)
    p = np.stack([x, y], axis=-1)
    px = 2.0 / size
    w = 0.075
    if kind == "earth":
        dia = lambda r: (np.abs(x) + np.abs(y)) / np.sqrt(2) - r
        d = np.minimum(np.abs(dia(0.62)) - w, np.abs(dia(0.36)) - w * 0.9)
        d = np.minimum(d, np.maximum(np.abs(x) - w * 0.8, np.abs(y) - 0.2))       # keystone bar
        d = np.minimum(d, np.linalg.norm(p - np.array([0, -0.0])[None, None, :], axis=-1) - 0.08)
    elif kind == "water":
        d = np.full_like(x, 9.0)
        for i, y0 in enumerate((0.45, 0.0, -0.45)):
            f = 0.17 * np.sin(x * 3.6 + i * 0.9)
            fp = 0.17 * 3.6 * np.cos(x * 3.6 + i * 0.9)
            dd = np.abs(y - y0 - f) / np.sqrt(1 + fp * fp) - w * 0.85
            dd = np.maximum(dd, np.abs(x) - 0.82)
            d = np.minimum(d, dd)
    elif kind == "fire":
        circ = np.linalg.norm(p - np.array([0, -0.22])[None, None, :], axis=-1) - 0.46
        tri = sd_tri(p, (-0.4, -0.02), (0.4, -0.02), (0.06, 0.92))
        outer = smin(circ, tri, 0.25)
        d = np.abs(outer) - w
        inner = np.linalg.norm(p - np.array([0, -0.3])[None, None, :], axis=-1) - 0.17
        itri = sd_tri(p, (-0.14, -0.28), (0.14, -0.28), (0.0, 0.18))
        d = np.minimum(d, smin(inner, itri, 0.1))
    else:   # air
        r = np.sqrt(x * x + y * y)
        th = np.arctan2(y, x)
        b = 0.115
        # distance to Archimedean spiral r = b*(theta + 2 pi k) within r in [0.08, 0.8]
        k = np.round((r / b - th) / (2 * np.pi))
        ds = np.abs(r - b * (th + 2 * np.pi * k))
        # r < 0.08 and r > 0.8 clipped with round-ish caps
        d = np.maximum(ds - w * 0.9, np.maximum(0.08 - r, r - 0.8))
        for cx, cy, rr in ((0.55, -0.72, 0.075), (0.78, -0.55, 0.055), (-0.6, 0.72, 0.065)):
            d = np.minimum(d, np.linalg.norm(p - np.array([cx, cy])[None, None, :], axis=-1) - rr)
    return aa(d, px)


def make_banner(kind, seed):
    rng = np.random.default_rng(seed)
    pal = dict(earth="#7f5a30", water="#1f6070", fire="#922b22", air="#7f98ae")
    trim = dict(earth="#d9b45f", water="#d9c37a", fire="#e2b45a", air="#e6dfc8")
    ink = dict(earth="#efe3c2", water="#e8efe6", fire="#f3e2bb", air="#26384c")
    ys, xs = np.mgrid[0:CH, 0:CW].astype(np.float64)
    base = hexlin(pal[kind])[None, None, :] * np.ones((CH, CW, 1))
    # weave: warp/weft threads with slub noise
    weave = 0.5 + 0.5 * np.sin(xs * np.pi / 1.5) * np.sin(ys * np.pi / 1.5)
    slub_x = np.repeat(rng.normal(0, 1, (1, CW)), CH, 0)
    slub_y = np.repeat(rng.normal(0, 1, (CH, 1)), CW, 1)
    soft = ndi.gaussian_filter(rng.normal(0, 1, (CH, CW)), 18)
    soft /= soft.std()
    shade = 1.0 + 0.07 * (weave - 0.5) + 0.035 * slub_x + 0.03 * slub_y + 0.09 * soft
    # vertical fold shading
    shade *= 1.0 + 0.06 * np.sin(xs / CW * 2 * np.pi * 2.0 + seed)
    col = base * shade[..., None]
    # borders: trim line near the edge + darker hem
    edge = np.minimum(np.minimum(xs, CW - 1 - xs), ys)
    line = (np.abs(edge - 16) < 3.2) | (np.abs(edge - 26) < 1.4)
    col = np.where(line[..., None], hexlin(trim[kind])[None, None, :] * shade[..., None], col)
    # glyph
    g = glyph(kind, 200)
    gy, gx = 128, (CW - 200) // 2
    cov = np.zeros((CH, CW))
    cov[gy:gy + 200, gx:gx + 200] = g
    ink_c = hexlin(ink[kind])[None, None, :] * (shade * (0.94 + 0.06 * weave))[..., None]
    col = col * (1 - cov[..., None]) + ink_c * cov[..., None]
    # a second, smaller emblem band (three studs) below the glyph
    for i, cx in enumerate((CW // 2 - 40, CW // 2, CW // 2 + 40)):
        dd = np.sqrt((xs - cx) ** 2 + (ys - 400) ** 2) - 6
        c2 = np.clip(0.5 - dd, 0, 1)
        col = col * (1 - c2[..., None]) + hexlin(trim[kind])[None, None, :] * c2[..., None]
    # tail: hanging ribbon vertical stripe
    stripe = (np.abs(xs - CW / 2) < 9) & (ys > 440) & (ys < 560)
    col = np.where(stripe[..., None], hexlin(trim[kind])[None, None, :] * shade[..., None] * 0.9, col)
    # swallow-tail cut-out at the bottom
    tail = 70.0
    cut = ys > (CH - 1 - tail * (1 - np.abs(xs - CW / 2) / (CW / 2)))
    alpha = np.where(cut, 0.0, 1.0)
    alpha = np.clip(ndi.gaussian_filter(alpha, 0.7), 0, 1)
    return col, alpha


def main(out):
    img = np.zeros((CH, CW * 4, 4))
    for i, kind in enumerate(("earth", "water", "fire", "air")):
        c, a = make_banner(kind, 700 + i)
        img[:, i * CW:(i + 1) * CW, :3] = lin_to_srgb(c)
        img[:, i * CW:(i + 1) * CW, 3] = a
    path = os.path.join(out, "banners_albedo.png")
    write_png(path, to_u8(img))
    if os.path.abspath(out) == OUT:
        tc.write_import(path)
    print("wrote", path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    main(ap.parse_args().out)
