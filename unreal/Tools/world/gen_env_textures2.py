"""Additional courtyard texture sets (new for the Unreal build; 1024^2, numpy / scipy / PIL, deterministic):

    plaster    lime-washed wall plaster with trowel marks, rain streaks, hairline cracks and chipped patches   (3.0 m repeat)
    timber     weathered dark timber boards, grain along U (the long axis of beams / posts)                    (2.0 m repeat)
    roof_tile  grey barrel (pan + cover) clay roof tiles with moss in the pans                                (1.2 m repeat)
    ground     packed earth, gravel and dry grass for the area outside the walls                              (6.0 m repeat)
    rock       craggy grey-brown rock with strata, cracks, moss and lichen                                    (3.0 m repeat)
    water_n    tileable ripple normal map (DirectX), 4.0 m repeat                                             (T_Env_WaterN.png)
    noise      RGBA utility noise: R macro tint, G fine grain, B cellular, A large-scale clouds              (T_Env_Noise.png)
    clouds     R cumulus density, G cirrus streaks, B detail                                                  (T_Env_Clouds.png)
    foliage    RGBA 2x2 atlas of leaf / pine / cypress / shrub cards (alpha = coverage)                       (T_Env_Foliage_BC.png)
    banners    RGBA atlas of four cloth banners with the original four-part emblem                            (T_Env_Banners_BC.png)
    mask       arena contact-AO / wall dirt / pool wetness mask from sim.json                                 (T_Env_ArenaMask.png)

Run: /home/user/tools/bpyenv/bin/python Tools/world/gen_env_textures2.py [--only plaster,timber,...] [--out DIR]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import texgen_common as tc                                  # noqa: E402
from texgen_common import (N, fbm, gnoise, blur, warp, smoothstep, normal_from_height, cavity_ao, hexlin,   # noqa: E402
                           lin_to_srgb, srgb_to_lin, to_u8, write_png, ndi)
import gen_env_textures as ge                                # noqa: E402
from gen_env_textures import save_set, OUT, YY, XX           # noqa: E402

ROOT = ge.ROOT
SIM_JSON = os.path.join(ROOT, "Source", "FourfoldCore", "Data", "sim.json")
TILE_M = dict(plaster=3.0, timber=2.0, roof_tile=1.2, ground=6.0, rock=3.0, water_n=4.0)


def mix(a, b, t):
    return a * (1 - t[..., None]) + b * t[..., None]


def periodic_voronoi(seed, cells, n=N):
    """F1 / F2 distances (in pixels) and the id of the nearest of `cells` x `cells` jittered periodic points."""
    from scipy.spatial import cKDTree
    rng = np.random.default_rng(seed)
    gx, gy = np.meshgrid(np.arange(cells), np.arange(cells))
    pts = np.stack([gx.ravel(), gy.ravel()], -1) + rng.uniform(0.1, 0.9, (cells * cells, 2))
    pts = pts * (n / cells)
    tree = cKDTree(pts, boxsize=n)
    qy, qx = np.mgrid[0:n, 0:n]
    q = np.stack([qx.ravel() + 0.5, qy.ravel() + 0.5], -1)
    d, i = tree.query(q, k=2)
    return d[:, 0].reshape(n, n), d[:, 1].reshape(n, n), i[:, 0].reshape(n, n)


# ============================================================================ plaster
def gen_plaster(out=OUT):
    seed = 606
    px_mm = TILE_M["plaster"] * 1000.0 / N
    rng = np.random.default_rng(seed)
    big = fbm(seed + 1, 2.2, 1, 10)
    mid = fbm(seed + 2, 1.8, 10, 90)
    grain = fbm(seed + 3, 0.8, 120, 512)
    # trowel: broad sweeping strokes (anisotropic, random-ish orientation via two overlapping fields)
    tro = fbm(seed + 4, 2.6, 2, 40, ax=1.0, ay=0.35) * 0.6 + fbm(seed + 5, 2.6, 2, 40, ax=0.35, ay=1.0) * 0.4
    h = tro * 0.55 + mid * 0.18 + grain * 0.07                    # mm
    # hairline cracks
    cn = fbm(seed + 6, 2.0, 2, 70)
    cn = warp(cn, fbm(seed + 7, 2, 1, 20) * 14, fbm(seed + 8, 2, 1, 20) * 14)
    crack = (1.0 - smoothstep(0.0, 0.035, np.abs(cn))) * smoothstep(0.15, 0.9, fbm(seed + 9, 2.0, 1, 8) * 0.5 + 0.5)
    h -= crack * 1.4
    # fallen patches exposing the rubble masonry underneath
    pn = fbm(seed + 10, 2.6, 2, 7) + 0.12 * fbm(seed + 11, 1.5, 12, 80)
    patch = smoothstep(2.05, 2.15, pn)
    lip = smoothstep(1.9, 2.05, pn) * (1 - patch)                   # chipped plaster edge
    # under-layer: coarse stone / brick
    under_h = fbm(seed + 12, 1.4, 8, 200) * 1.6 - 4.5
    h = h * (1 - patch) + under_h * patch - lip * 0.8
    # colour
    base = hexlin("#dcd2bb")
    alb = base[None, None, :] * np.ones((N, N, 1))
    alb = alb * (1.0 + 0.045 * big + 0.03 * mid + 0.025 * grain)[..., None]
    # warm lime wash: faint ochre drift
    alb = mix(alb, alb * np.array([1.03, 0.98, 0.9]), smoothstep(-0.5, 1.5, fbm(seed + 13, 2.2, 1, 6)))
    # rain streaks from the top: long vertical stains
    st = gnoise(seed + 14, 70, 3.5)
    st = smoothstep(0.35, 2.0, st) * (0.55 + 0.45 * fbm(seed + 15, 2.0, 1, 10) * 0.5 + 0.2)
    alb = alb * (1.0 - 0.28 * st)[..., None]
    alb = mix(alb, alb * np.array([0.82, 0.88, 0.8]), st * 0.35)
    # damp / mould blotches
    damp = smoothstep(1.0, 2.0, fbm(seed + 16, 2.2, 1, 9))
    alb = mix(alb, alb * np.array([0.78, 0.86, 0.72]), damp * 0.5)
    mould = smoothstep(2.0, 2.6, fbm(seed + 17, 0.9, 30, 300)) * damp
    alb = mix(alb, hexlin("#3f4a2a")[None, None, :] * np.ones((N, N, 1)), mould * 0.5)
    alb = alb * (1.0 - 0.55 * crack)[..., None]
    ucol = mix(hexlin("#a39079")[None, None, :] * np.ones((N, N, 1)), hexlin("#86735f")[None, None, :] * np.ones((N, N, 1)),
               smoothstep(-1, 1.5, fbm(seed + 18, 1.5, 6, 120)))
    ucol = ucol * (0.85 + 0.25 * (fbm(seed + 19, 1.0, 30, 300) * 0.5 + 0.5))[..., None]
    alb = mix(alb, ucol, patch)
    alb = alb * (1.0 + 0.25 * lip)[..., None]                       # pale chipped edge
    rough = 0.9 + 0.05 * grain - 0.04 * np.clip(big, -1, 1)
    rough = np.clip(rough * (1 - patch) + 0.97 * patch, 0.6, 1.0)
    ao = cavity_ao(h, 5.0, 4.0, 0.3) * cavity_ao(h, 18.0, 12.0, 0.5)
    ao = np.clip(ao * (1 - 0.3 * crack), 0.2, 1.0)
    nrm = normal_from_height(h, px_mm, 1.0)
    save_set("plaster", alb, h, nrm, ao, rough, np.zeros((N, N)), out=out)


# ============================================================================ timber
def gen_timber(out=OUT):
    seed = 707
    px_mm = TILE_M["timber"] * 1000.0 / N
    rng = np.random.default_rng(seed)
    P = 6                                                          # boards across V
    bh = N / P
    pid = np.minimum((YY // bh).astype(int), P - 1)
    ly = YY - pid * bh
    gap = np.minimum(ly, bh - 1 - ly)
    inside = smoothstep(2.0, 5.0, gap)
    # grain: flat-sawn rings, warped, with knots
    warpf = fbm(seed + 1, 2.4, 1, 14, ax=0.2, ay=1.0) * 22.0 + fbm(seed + 2, 1.6, 6, 80, ax=0.15, ay=1.0) * 4.0
    phase = rng.uniform(0, 100, P)[pid]
    knots = np.zeros((N, N))
    for _ in range(9):
        kx, ky = rng.uniform(0, N), rng.uniform(0, N)
        r = rng.uniform(14, 30)
        dx = (XX - kx + N / 2) % N - N / 2
        dy = (YY - ky + N / 2) % N - N / 2
        dd = np.sqrt((dx / 1.7) ** 2 + dy ** 2)
        knots += np.exp(-(dd / r) ** 2) * rng.uniform(26, 46)
        knots += (dd < r * 0.28) * 1.0
    ringc = (ly + warpf + knots * np.sign(ly - bh / 2 + 1e-3) + phase * 3.0) / rng.uniform(7.0, 9.0)
    rings = 0.5 + 0.5 * np.sin(ringc * 2 * np.pi * 0.35)
    late = np.clip(rings, 0, 1) ** 2.0
    fine = fbm(seed + 3, 1.2, 4, 300, ax=0.06, ay=1.0)
    checks = (1 - smoothstep(0.0, 0.05, np.abs(fbm(seed + 4, 1.8, 3, 90, ax=0.05, ay=1.0)))) * smoothstep(0.7, 1.6, fbm(seed + 5, 2, 1, 6))
    h = -late * 0.35 + fine * 0.14 - checks * 1.8 - 3.2 * (1 - inside)
    h -= (knots > 20) * 0.5
    tone = rng.uniform(0.82, 1.15, P)[pid]
    weather = smoothstep(-0.2, 1.8, fbm(seed + 6, 2.4, 1, 10, ax=0.12, ay=1.0))            # silvered streaks along the grain
    dark = hexlin("#4b3828")
    warm = hexlin("#6c523a")
    silver = hexlin("#8c8676")
    alb = mix(np.ones((N, N, 3)) * dark, np.ones((N, N, 3)) * warm, 0.35 + 0.65 * (1 - late))
    alb = mix(alb, np.ones((N, N, 3)) * silver, weather * 0.55)
    streak = fbm(seed + 7, 1.1, 20, 512, ax=0.04, ay=1.0)
    alb = alb * (tone * (1.0 + 0.16 * fine + 0.10 * streak))[..., None]
    alb = alb * (1 - 0.65 * checks)[..., None]
    alb = alb * (0.35 + 0.65 * inside)[..., None]
    rough = np.clip(0.78 + 0.1 * fine + 0.08 * weather - 0.05 * late, 0.5, 1.0)
    ao = np.clip(cavity_ao(h, 4.0, 2.5, 0.3) * (0.4 + 0.6 * inside), 0.2, 1)
    nrm = normal_from_height(h, px_mm, 1.2)
    save_set("timber", alb, h, nrm, ao, rough, np.zeros((N, N)), out=out)


# ============================================================================ roof tile
def gen_roof_tile(out=OUT):
    """Barrel tiles: long convex cover tiles alternate with concave pans; each course overlaps the one below."""
    seed = 808
    px_mm = TILE_M["roof_tile"] * 1000.0 / N
    rng = np.random.default_rng(seed)
    pairs = 3
    ncol = pairs * 2
    colw = N / ncol
    col = (XX // colw).astype(int)
    lx = (XX % colw) / colw
    is_cover = (col % 2 == 0)
    prof = np.sin(lx * np.pi)
    rows = 4
    rh = N / rows
    row = (YY // rh).astype(int)
    ly = (YY % rh) / rh                                             # 0 at the tile's upper end, 1 at its lower (exposed) end
    tid = row * ncol + col
    nt = rows * ncol
    # each tile slopes down toward its exposed lower edge and drops sharply onto the course below
    ramp = 1.0 - ly
    h = np.where(is_cover, prof * 9.0, -prof * 6.0) + ramp * 7.0
    h += rng.normal(0, 0.5, nt)[tid]
    h += fbm(seed + 1, 2.0, 2, 60) * 0.6 + fbm(seed + 2, 1.0, 60, 400) * 0.25
    shade_gap = smoothstep(0.0, 9.0, (1 - ly) * rh)                 # dark joint under the lower edge of the course above
    bright = rng.uniform(0.9, 1.1, nt)[tid]
    base = hexlin("#4b5156")
    warmc = hexlin("#585049")
    alb = mix(np.ones((N, N, 3)) * base, np.ones((N, N, 3)) * warmc, np.clip(rng.random(nt)[tid], 0, 1) * 0.5)
    alb = alb * (bright * (1.0 + 0.05 * fbm(seed + 3, 2.0, 1, 12) + 0.04 * fbm(seed + 4, 1.0, 80, 400)))[..., None]
    alb = alb * np.where(is_cover, 1.0 + 0.16 * prof, 0.82 + 0.1 * prof)[..., None]
    # soot-dark channels and pale worn crowns
    alb = alb * (0.62 + 0.38 * shade_gap)[..., None]
    mscore = (~is_cover) * 0.35 + (ly) * 0.3 + fbm(seed + 5, 2.6, 2, 30) * 0.55
    moss = smoothstep(1.05, 1.5, mscore) * 0.75
    mcol = mix(np.ones((N, N, 3)) * hexlin("#2c3f1c"), np.ones((N, N, 3)) * hexlin("#4d5e27"), np.clip(fbm(seed + 7, 1.2, 10, 200) * 0.5 + 0.5, 0, 1))
    alb = mix(alb, mcol, moss)
    lich = smoothstep(2.4, 3.0, fbm(seed + 8, 0.8, 50, 300)) * 0.7
    alb = mix(alb, np.ones((N, N, 3)) * hexlin("#a4a68f"), lich)
    h += moss * 0.8
    rough = np.clip(0.6 + 0.15 * fbm(seed + 9, 1.4, 4, 200) + 0.3 * moss, 0.35, 1.0)
    ao = np.clip(cavity_ao(h, 6.0, 6.0, 0.25) * (0.45 + 0.55 * shade_gap), 0.2, 1.0)
    nrm = normal_from_height(h, px_mm, 1.5)
    save_set("roof_tile", alb, h, nrm, ao, rough, np.zeros((N, N)), out=out)


# ============================================================================ ground (outside the walls)
def gen_ground(out=OUT):
    seed = 909
    px_mm = TILE_M["ground"] * 1000.0 / N
    rng = np.random.default_rng(seed)
    big = fbm(seed + 1, 2.2, 1, 12)
    mid = fbm(seed + 2, 1.8, 12, 100)
    grain = fbm(seed + 3, 0.8, 100, 512)
    h = big * 3.0 + mid * 1.2 + grain * 0.5
    soil = mix(np.ones((N, N, 3)) * hexlin("#74654a"), np.ones((N, N, 3)) * hexlin("#8c7b5a"), np.clip(big * 0.25 + 0.5, 0, 1))
    soil = soil * (1.0 + 0.1 * mid + 0.05 * grain)[..., None]
    # pebbles: splats of small stones
    peb = np.zeros((N, N))
    pcol = np.zeros((N, N, 3))
    cnt = 1500
    px = rng.uniform(0, N, cnt)
    py = rng.uniform(0, N, cnt)
    pr = rng.uniform(1.8, 6.5, cnt)
    cols = np.array([hexlin("#8c8a82"), hexlin("#a39b8b"), hexlin("#6f6a62"), hexlin("#9a8a72")])
    for k in range(cnt):
        r = pr[k]
        x0, y0 = int(px[k]), int(py[k])
        rr = int(r + 2)
        ys = (np.arange(y0 - rr, y0 + rr + 1)) % N
        xs = (np.arange(x0 - rr, x0 + rr + 1)) % N
        gy, gx = np.meshgrid(np.arange(-rr, rr + 1), np.arange(-rr, rr + 1), indexing="ij")
        d = np.sqrt((gx / 1.25) ** 2 + gy ** 2) / r
        hh = np.clip(1 - d * d, 0, None) * r * 0.9
        sl = np.ix_(ys, xs)
        better = hh > peb[sl]
        peb[sl] = np.where(better, hh, peb[sl])
        c = cols[k % 4] * rng.uniform(0.8, 1.15)
        pcol[sl] = np.where(better[..., None], c, pcol[sl])
    has_peb = (peb > 0.25)[..., None]
    soil = np.where(has_peb, pcol * (0.75 + 0.25 * np.clip(peb / 5, 0, 1))[..., None], soil)
    h += peb * 0.8
    # dry grass: blade-like anisotropic noise in a patchy mask
    gmask = smoothstep(0.1, 1.2, fbm(seed + 4, 2.2, 2, 30) + 0.25 * mid)
    blades = fbm(seed + 5, 0.9, 80, 512, ax=0.5, ay=1.0)
    gcol = mix(np.ones((N, N, 3)) * hexlin("#5f6a31"), np.ones((N, N, 3)) * hexlin("#a29a58"), np.clip(blades * 0.35 + 0.5, 0, 1))
    gcol = gcol * (0.8 + 0.3 * np.clip(blades * 0.5 + 0.5, 0, 1))[..., None]
    gm = (gmask * (1 - np.clip(peb * 0.35, 0, 1)))
    alb = mix(soil, gcol, gm)
    h += gm * blades * 0.9
    # wheel / foot track: darker packed strip (kept periodic)
    rough = np.clip(0.9 - 0.15 * gm + 0.03 * grain, 0.5, 1.0)
    ao = np.clip(cavity_ao(h, 4.0, 3.0, 0.4), 0.3, 1)
    nrm = normal_from_height(h, px_mm, 1.0)
    save_set("ground", alb, h, nrm, ao, rough, np.zeros((N, N)), out=out)


# ============================================================================ rock
def gen_rock(out=OUT):
    """Faceted crag: Voronoi facets with random tilts (sharp arrises), low-frequency undulation, sheared strata, moss in hollows."""
    from scipy.spatial import cKDTree
    seed = 111
    px_mm = TILE_M["rock"] * 1000.0 / N
    rng = np.random.default_rng(seed)
    cells = 4
    gx, gy = np.meshgrid(np.arange(cells), np.arange(cells))
    pts = (np.stack([gx.ravel(), gy.ravel()], -1) + rng.uniform(0.15, 0.85, (cells * cells, 2))) * (N / cells)
    tree = cKDTree(pts, boxsize=N)
    qy, qx = np.mgrid[0:N, 0:N]
    q = np.stack([qx.ravel() + 0.5, qy.ravel() + 0.5], -1)
    dd, ii = tree.query(q, k=2)
    d1 = dd[:, 0].reshape(N, N)
    d2 = dd[:, 1].reshape(N, N)
    cid = ii[:, 0].reshape(N, N)
    cen = pts[cid]
    rx = (XX + 0.5 - cen[..., 0] + N / 2) % N - N / 2
    ry = (YY + 0.5 - cen[..., 1] + N / 2) % N - N / 2
    tx = rng.normal(0, 0.16, cells * cells)[cid]
    ty = rng.normal(0, 0.16, cells * cells)[cid]
    off = rng.normal(0, 3.0, cells * cells)[cid]
    facet = tx * rx + ty * ry + off
    edge = d2 - d1
    arris = smoothstep(0.0, 34.0, edge)
    h = facet * 2.2 - (1 - arris) ** 1.5 * 10.0
    h += fbm(seed + 1, 2.4, 1, 10) * 7.0 + fbm(seed + 2, 1.8, 8, 60) * 2.5 + fbm(seed + 3, 1.2, 40, 512) * 0.5
    strata = np.sin((YY * 0.3 + XX * 0.05 + fbm(seed + 4, 2.2, 1, 6) * 50.0) / 8.0) * 0.5 + 0.5
    h += strata * 0.8
    shade = np.clip((blur(h, 3) - h) * 0.0 + (h - blur(h, 20)) / 12.0, -1, 1)
    base = mix(np.ones((N, N, 3)) * hexlin("#7b776f"), np.ones((N, N, 3)) * hexlin("#8a7d6b"), np.clip(rng.random(cells * cells)[cid], 0, 1))
    alb = base * (1.0 + 0.2 * shade + 0.06 * fbm(seed + 8, 1.0, 40, 400) - 0.08 * strata)[..., None]
    alb = mix(alb, alb * np.array([1.12, 0.9, 0.72]), smoothstep(0.4, 2.0, gnoise(seed + 9, 30, 4)) * 0.5)
    alb = alb * (0.6 + 0.4 * arris)[..., None]
    low = np.clip((blur(h, 12) - h) / 5.0, 0, 1)
    moss = smoothstep(0.5, 1.0, low * 0.8 + fbm(seed + 10, 2.4, 2, 30) * 0.3 + (1 - arris) * 0.35) * 0.75
    mcol = mix(np.ones((N, N, 3)) * hexlin("#2f411d"), np.ones((N, N, 3)) * hexlin("#586a2a"), np.clip(fbm(seed + 11, 1.2, 12, 200) * 0.5 + 0.5, 0, 1))
    alb = mix(alb, mcol, moss)
    h += moss * 0.9
    lich = smoothstep(2.3, 3.0, fbm(seed + 12, 0.8, 40, 300)) * (1 - moss) * 0.75
    alb = mix(alb, np.ones((N, N, 3)) * hexlin("#aeb092"), lich)
    ao = np.clip(cavity_ao(h, 6.0, 9.0, 0.2) * cavity_ao(h, 24.0, 26.0, 0.4) * (0.45 + 0.55 * arris), 0.15, 1)
    rough = np.clip(0.86 + 0.1 * fbm(seed + 13, 1.4, 4, 200) - 0.06 * strata + 0.08 * moss, 0.5, 1.0)
    nrm = normal_from_height(h, px_mm, 1.0)
    save_set("rock", alb, h, nrm, ao, rough, np.zeros((N, N)), out=out)


# ============================================================================ water normal / noise / clouds
def _save_normal_file(name, normal, out):
    n = np.array(normal, dtype=np.float64)
    n[..., 1] = -n[..., 1]
    write_png(os.path.join(out, name), to_u8(n * 0.5 + 0.5))


def gen_water_n(out=OUT):
    seed = 1212
    px_mm = TILE_M["water_n"] * 1000.0 / N
    h = (fbm(seed + 1, 2.6, 3, 120, ax=1.0, ay=0.65) * 1.0 + fbm(seed + 2, 2.0, 12, 200, ax=0.7, ay=1.0) * 0.5
         + fbm(seed + 3, 1.3, 60, 512) * 0.12)
    # soften the very fine part so ripples read as water, not as sand
    nrm = normal_from_height(h * 8.0, px_mm, 1.0)
    _save_normal_file("T_Env_WaterN.png", nrm, out)
    print("wrote water_n")


def gen_noise(out=OUT):
    seed = 1313
    r = fbm(seed + 1, 2.4, 1, 8)
    g = fbm(seed + 2, 1.0, 40, 512)
    d1, d2, cid = periodic_voronoi(seed + 3, 24)
    b = np.clip(d1 / 20.0, 0, 1) * 0.6 + 0.4 * fbm(seed + 4, 2.0, 1, 30) * 0.2 + 0.2
    a = fbm(seed + 5, 2.8, 1, 24)
    def norm01(x):
        return np.clip(0.5 + 0.28 * x, 0, 1)
    img = np.dstack([norm01(r), norm01(g), np.clip(b, 0, 1), norm01(a)])
    write_png(os.path.join(out, "T_Env_Noise.png"), to_u8(img))
    print("wrote noise")


def gen_clouds(out=OUT):
    seed = 1414
    cum = fbm(seed + 1, 2.7, 2, 26) * 0.7 + fbm(seed + 2, 2.0, 8, 64) * 0.3
    cum = np.clip(0.5 + 0.30 * cum, 0, 1)
    cir = fbm(seed + 3, 2.2, 1, 40, ax=1.0, ay=0.12)
    cir = np.clip(0.5 + 0.32 * cir, 0, 1)
    det = fbm(seed + 4, 1.6, 12, 200)
    det = np.clip(0.5 + 0.3 * det, 0, 1)
    write_png(os.path.join(out, "T_Env_Clouds.png"), to_u8(np.dstack([cum, cir, det])))
    print("wrote clouds")


# ============================================================================ foliage atlas (PIL, 2x supersampled)
def _leaf_poly(cx, cy, length, width, ang, n=10):
    t = np.linspace(0, 1, n)
    w = np.sin(t * np.pi) ** 0.8 * (1 - 0.25 * t)
    xs = np.concatenate([t * length, (t * length)[::-1]])
    ys = np.concatenate([w * width / 2, -(w * width / 2)[::-1]])
    c, s = np.cos(ang), np.sin(ang)
    px = cx + xs * c - ys * s
    py = cy + xs * s + ys * c
    return list(zip(px.tolist(), py.tolist()))


def gen_foliage(out=OUT):
    S = 1024
    cell = S // 2
    rng = np.random.default_rng(1515)
    ss = 2
    img = Image.new("RGBA", (S * ss, S * ss), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)

    def col(c, jit=0.08):
        c = np.array(c, float) * (1 + rng.uniform(-jit, jit))
        return tuple(int(np.clip(v, 0, 255)) for v in c) + (255,)

    # cell 0: broadleaf clump (round, many leaves; lighter on top, darker inside)
    ox, oy = 0, 0
    ccx, ccy = cell / 2, cell / 2
    leaves = []
    for _ in range(520):
        r = np.sqrt(rng.uniform(0, 1)) * cell * 0.44
        a = rng.uniform(0, 2 * np.pi)
        x = ccx + np.cos(a) * r * 1.0
        y = ccy + np.sin(a) * r * 0.9
        leaves.append((y, x))
    leaves.sort()                               # back to front (top first = lit on top drawn last? keep stable)
    for y, x in leaves:
        ang = rng.uniform(0, 2 * np.pi)
        L = rng.uniform(34, 58)
        shade = 0.55 + 0.7 * (1 - (y / cell)) * rng.uniform(0.8, 1.1)
        c = col((70 * shade, 108 * shade, 38 * shade), 0.1)
        poly = _leaf_poly((ox + x) * ss, (oy + y) * ss, L * ss, L * 0.5 * ss, ang)
        dr.polygon(poly, fill=c)
        # midrib
        x2 = (ox + x + np.cos(ang) * L) * ss
        y2 = (oy + y + np.sin(ang) * L) * ss
        dr.line([((ox + x) * ss, (oy + y) * ss), (x2, y2)], fill=col((50 * shade, 80 * shade, 28 * shade), 0.05), width=ss)

    # cell 1: pine bough: a feathery frond made of many tapered needle clumps leaning toward the tip (dense, ragged outline)
    ox, oy = cell, 0
    cy0 = cell * 0.5
    clumps = []
    for t in np.linspace(0.03, 0.97, 150):
        x = ox + t * cell
        y = oy + cy0 + 5 * np.sin(t * 4.0)
        full = (0.50 + 0.50 * np.sin(np.clip(t * 1.06, 0, 1) * np.pi)) * cell * 0.34
        for side in (-1, 1):
            for k in range(2):
                lean = rng.uniform(0.35, 0.95)
                length = full * rng.uniform(0.7, 1.1)
                clumps.append((t, x, y, side, lean, length))
    clumps.sort(key=lambda c: c[3] * 0 + abs(c[2]))
    for t, x, y, side, lean, length in clumps:
        ang = side * lean                                  # direction relative to +x (the tip)
        shade = rng.uniform(0.7, 1.2) * (1.0 + 0.12 * (side == -1))
        poly = _leaf_poly(x * ss, y * ss, length * ss, length * 0.16 * ss, ang, n=8)
        dr.polygon(poly, fill=col((32 * shade, 70 * shade, 36 * shade), 0.1))
        x2 = (x + np.cos(ang) * length * 0.9) * ss
        y2 = (y + np.sin(ang) * length * 0.9) * ss
        dr.line([(x * ss, y * ss), (x2, y2)], fill=col((58 * shade, 100 * shade, 54 * shade), 0.05), width=max(1, int(1.5 * ss)))
    dr.line([((ox + 4) * ss, (oy + cy0) * ss), ((ox + cell - 4) * ss, (oy + cy0 + 5 * np.sin(4.0)) * ss)], fill=col((70, 52, 36)), width=int(3.5 * ss))

    # cell 2: cypress spray (vertical, flame shaped, tight overlapping scales)
    ox, oy = 0, cell
    for i in range(240):
        t = rng.uniform(0.02, 0.98)
        half = np.sin(np.clip(t, 0, 1) ** 0.7 * np.pi * 0.5 + 0.05) ** 1.0 * cell * 0.32 * (1 - t * 0.9) + 6
        x = ox + cell / 2 + rng.uniform(-1, 1) * half
        y = oy + cell * (1 - t) * 0.96 + 8
        a = np.pi / 2 + rng.normal(0, 0.5)
        L = rng.uniform(28, 46)
        shade = 0.6 + 0.6 * (t) * rng.uniform(0.85, 1.1)
        dr.polygon(_leaf_poly(x * ss, y * ss, L * ss, L * 0.55 * ss, a + np.pi), fill=col((36 * shade, 68 * shade, 36 * shade), 0.1))

    # cell 3: shrub / grass tuft: arching blades
    ox, oy = cell, cell
    for i in range(90):
        x0 = ox + cell / 2 + rng.normal(0, cell * 0.1)
        y0 = oy + cell - 8
        h_ = rng.uniform(0.5, 0.95) * cell
        bend = rng.normal(0, 0.55)
        pts = []
        for t in np.linspace(0, 1, 12):
            x = x0 + bend * h_ * t * t + (t * 0.0)
            y = y0 - h_ * t
            pts.append((x * ss, y * ss))
        shade = rng.uniform(0.7, 1.2)
        dr.line(pts, fill=col((84 * shade, 118 * shade, 44 * shade), 0.1), width=int(rng.uniform(3, 6) * ss))
    img = img.resize((S, S), Image.LANCZOS)
    arr = np.array(img)
    # bleed colour into transparent texels (avoids dark fringes with mips)
    a = arr[..., 3] > 0
    rgb = arr[..., :3].astype(np.float64)
    idx = ndi.distance_transform_edt(~a, return_distances=False, return_indices=True)
    rgb = rgb[idx[0], idx[1]]
    arr[..., :3] = rgb.astype(np.uint8)
    write_png(os.path.join(out, "T_Env_Foliage_BC.png"), arr)
    print("wrote foliage")


# ============================================================================ banners with the four-part emblem
CW, CH = 256, 640


def _aa(d, px):
    return np.clip(0.5 - d / px, 0.0, 1.0)


def _sd_seg(p, a, b):
    pa = p - np.array(a)[None, None, :]
    ba = np.array(b) - np.array(a)
    h = np.clip((pa @ ba) / (ba @ ba), 0, 1)
    return np.linalg.norm(pa - h[..., None] * ba[None, None, :], axis=-1)


def _smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def _glyph_field(kind, x, y):
    """Signed distance field of one element glyph in [-1,1]^2 (y up); negative inside the stroke."""
    p = np.stack([x, y], -1)
    w = 0.1
    if kind == "earth":        # stacked courses: three offset bars
        d = np.full_like(x, 9.0)
        for i, (yy, ww) in enumerate(((0.52, 0.8), (0.0, 0.62), (-0.52, 0.8))):
            off = 0.14 if i == 1 else 0.0
            dd = np.maximum(np.abs(x - off) - ww, np.abs(y - yy) - 0.17)
            d = np.minimum(d, dd)
        return d
    if kind == "water":        # three swells
        d = np.full_like(x, 9.0)
        for i, y0 in enumerate((0.5, 0.0, -0.5)):
            f = 0.15 * np.sin(x * 3.6 + i * 0.9)
            fp = 0.15 * 3.6 * np.cos(x * 3.6 + i * 0.9)
            dd = np.abs(y - y0 - f) / np.sqrt(1 + fp * fp) - w * 0.8
            dd = np.maximum(dd, np.abs(x) - 0.85)
            d = np.minimum(d, dd)
        return d
    if kind == "fire":         # a leaning tongue (lens) with an inner cut
        c = np.array([0.0, -0.05])
        q = p - c[None, None, :]
        lens = np.maximum(np.linalg.norm(q - np.array([0.55, 0.0])[None, None, :], axis=-1) - 0.95,
                          np.linalg.norm(q + np.array([0.55, 0.0])[None, None, :], axis=-1) - 0.95)
        # rotate the lens upright and skew its tip
        qq = np.stack([q[..., 1], -q[..., 0] - 0.25 * q[..., 1] ** 2], -1)
        lens = np.maximum(np.linalg.norm(qq - np.array([0.0, 0.62])[None, None, :], axis=-1) - 0.96,
                          np.linalg.norm(qq + np.array([0.0, 0.62])[None, None, :], axis=-1) - 0.96)
        return np.minimum(lens, 9.0)
    # air: one-turn spiral
    r = np.sqrt(x * x + y * y)
    th = np.arctan2(y, x)
    b = 0.12
    k = np.round((r / b - th) / (2 * np.pi))
    ds = np.abs(r - b * (th + 2 * np.pi * k))
    return np.maximum(ds - w * 0.8, np.maximum(0.06 - r, r - 0.82))


def _glyph(kind, size):
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    x = (xs + 0.5) / size * 2 - 1
    y = -((ys + 0.5) / size * 2 - 1)
    return _aa(_glyph_field(kind, x, y), 2.0 / size)


def _emblem(size):
    """The four-part emblem: a rounded square quartered by a cross, each quarter carrying one element glyph (small)."""
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64)
    x = (xs + 0.5) / size * 2 - 1
    y = -((ys + 0.5) / size * 2 - 1)
    px = 2.0 / size
    sq = np.maximum(np.abs(x), np.abs(y)) - 0.92
    ring = np.abs(sq) - 0.045
    cross = np.minimum(np.abs(x) - 0.03, np.abs(y) - 0.03)
    cross = np.maximum(cross, sq + 0.02)
    cov = np.maximum(_aa(ring, px), _aa(cross, px))
    for kind, sx, sy in (("earth", -1, 1), ("water", 1, 1), ("fire", -1, -1), ("air", 1, -1)):
        gx = (x - sx * 0.47) / 0.36
        gy = (y - sy * 0.47) / 0.36
        d = _glyph_field(kind, gx, gy) * 0.36
        cov = np.maximum(cov, _aa(d, px) * (np.abs(gx) < 1.1) * (np.abs(gy) < 1.1))
    return cov


def _make_banner(kind, seed):
    rng = np.random.default_rng(seed)
    pal = dict(earth="#7b5a34", water="#1f5e70", fire="#8c3326", air="#6f8aa3")
    trim = dict(earth="#d7b66c", water="#d8c27c", fire="#e0b25c", air="#e6dfc8")
    ink = dict(earth="#efe2c0", water="#e6efe6", fire="#f2e0b8", air="#22364a")
    ys, xs = np.mgrid[0:CH, 0:CW].astype(np.float64)
    base = hexlin(pal[kind])[None, None, :] * np.ones((CH, CW, 1))
    weave = 0.5 + 0.5 * np.sin(xs * np.pi / 1.5) * np.sin(ys * np.pi / 1.5)
    slub_x = np.repeat(rng.normal(0, 1, (1, CW)), CH, 0)
    slub_y = np.repeat(rng.normal(0, 1, (CH, 1)), CW, 1)
    soft = ndi.gaussian_filter(rng.normal(0, 1, (CH, CW)), 18)
    soft /= soft.std()
    shade = 1.0 + 0.07 * (weave - 0.5) + 0.035 * slub_x + 0.03 * slub_y + 0.09 * soft
    # sun fade toward the hem and weathering at the folds
    shade *= 1.0 - 0.12 * (ys / CH) ** 2
    col = base * shade[..., None]
    edge = np.minimum(np.minimum(xs, CW - 1 - xs), ys)
    line = (np.abs(edge - 16) < 3.2) | (np.abs(edge - 26) < 1.4)
    col = np.where(line[..., None], hexlin(trim[kind])[None, None, :] * shade[..., None], col)
    g = _glyph(kind, 180)
    cov = np.zeros((CH, CW))
    cov[104:104 + 180, (CW - 180) // 2:(CW - 180) // 2 + 180] = g
    ink_c = hexlin(ink[kind])[None, None, :] * (shade * (0.94 + 0.06 * weave))[..., None]
    col = col * (1 - cov[..., None]) + ink_c * cov[..., None]
    # the four-part emblem seal
    e = _emblem(112)
    cov2 = np.zeros((CH, CW))
    cov2[352:352 + 112, (CW - 112) // 2:(CW - 112) // 2 + 112] = e
    tc_ = hexlin(trim[kind])[None, None, :] * shade[..., None]
    col = col * (1 - cov2[..., None]) + tc_ * cov2[..., None]
    stripe = (np.abs(xs - CW / 2) < 8) & (ys > 490) & (ys < 570)
    col = np.where(stripe[..., None], hexlin(trim[kind])[None, None, :] * shade[..., None] * 0.9, col)
    tail = 70.0
    cut = ys > (CH - 1 - tail * (1 - np.abs(xs - CW / 2) / (CW / 2)))
    alpha = np.where(cut, 0.0, 1.0)
    alpha = np.clip(ndi.gaussian_filter(alpha, 0.7), 0, 1)
    return col, alpha


def gen_banners(out=OUT):
    img = np.zeros((CH, CW * 4, 4))
    for i, kind in enumerate(("earth", "water", "fire", "air")):
        c, a = _make_banner(kind, 700 + i)
        img[:, i * CW:(i + 1) * CW, :3] = lin_to_srgb(c)
        img[:, i * CW:(i + 1) * CW, 3] = a
    write_png(os.path.join(out, "T_Env_Banners_BC.png"), to_u8(img))
    print("wrote banners")


# ============================================================================ arena mask
MASK_RES = 512
MASK_RECT = (-18.0, -18.0, 36.0, 36.0)       # x0, z0, size x, size z in sim metres


def load_arena():
    def v3(d):
        return d["$v3"] if isinstance(d, dict) else d

    def v2(d):
        return d["$v2"] if isinstance(d, dict) else d

    with open(SIM_JSON, encoding="utf-8") as f:
        a = json.load(f)["arena_lab"]
    solids = [dict(name=s["name"], kind=s["kind"], min=v3(s["min"]), max=v3(s["max"])) for s in a["solids"]]
    return dict(half=a["half_size"], solids=solids, pool_min=v2(a["pool_min"]), pool_max=v2(a["pool_max"]),
                pool_floor=a["pool_floor"], pool_level=a["pool_level"], metal_min=v2(a["metal_min"]),
                metal_max=v2(a["metal_max"]), metal_top=a["metal_top"], player=v3(a["player_spawn"]),
                opponent=v3(a["opponent_spawn"]))


def gen_mask(out=OUT):
    A = load_arena()
    x0, z0, sx, sz = MASK_RECT
    step = sx / MASK_RES
    xs = x0 + (np.arange(MASK_RES) + 0.5) * step
    zs = z0 + (np.arange(MASK_RES) + 0.5) * step
    X, Z = np.meshgrid(xs, zs)
    ao = np.ones_like(X)
    for s in A["solids"]:
        mn, mx = s["min"], s["max"]
        dx = np.maximum(np.maximum(mn[0] - X, X - mx[0]), 0.0)
        dz = np.maximum(np.maximum(mn[2] - Z, Z - mx[2]), 0.0)
        d = np.sqrt(dx * dx + dz * dz)
        hf = np.clip(mx[1] / 1.4, 0.25, 1.0)
        r = 0.55 + 0.9 * hf
        t = 1.0 - smoothstep(0.0, r, d)
        occ = (d > 0.0) & (d < r)
        ao = np.where(occ, ao * (1.0 - 0.62 * hf * t * t), ao)
    h = A["half"]
    edge = h - np.maximum(np.abs(X), np.abs(Z))                    # distance to the inner wall faces
    dirt = 1.0 - smoothstep(0.0, 2.2, edge)
    p0, p1 = A["pool_min"], A["pool_max"]
    dxp = np.maximum(np.maximum(p0[0] - X, X - p1[0]), 0.0)
    dzp = np.maximum(np.maximum(p0[1] - Z, Z - p1[1]), 0.0)
    dp = np.sqrt(dxp * dxp + dzp * dzp)
    wet = 1.0 - smoothstep(0.0, 1.1, dp)                           # splash band around the pool
    m0, m1 = A["metal_min"], A["metal_max"]
    dxm = np.maximum(np.maximum(m0[0] - X, X - m1[0]), 0.0)
    dzm = np.maximum(np.maximum(m0[1] - Z, Z - m1[1]), 0.0)
    dm = np.sqrt(dxm * dxm + dzm * dzm)
    rust = 1.0 - smoothstep(0.0, 0.5, dm)                          # stain halo around the metal plate
    img = np.dstack([ao, dirt, wet, rust])
    write_png(os.path.join(out, "T_Env_ArenaMask.png"), to_u8(img))
    meta = dict(rect=list(MASK_RECT), res=MASK_RES, channels=dict(R="contact AO", G="wall-edge dirt", B="pool splash band", A="metal plate halo"))
    with open(os.path.join(out, "T_Env_ArenaMask.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
    print("wrote mask")


GENS = dict(plaster=gen_plaster, timber=gen_timber, roof_tile=gen_roof_tile, ground=gen_ground, rock=gen_rock,
            water_n=gen_water_n, noise=gen_noise, clouds=gen_clouds, foliage=gen_foliage, banners=gen_banners, mask=gen_mask)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    names = [n for n in a.only.split(",") if n] or list(GENS)
    for n in names:
        GENS[n](a.out)
