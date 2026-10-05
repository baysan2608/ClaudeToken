"""Reproducible tileable PBR texture sets for the Fourfold arena (1024^2, numpy/scipy only).

    /home/user/tools/bpyenv/bin/python tools/textures/gen_env_textures.py [--only flagstone,wall,...] [--preview DIR]

Per set it writes three PNGs into game/assets/textures/:
    <set>_albedo.png   RGB = base colour (sRGB), A = height (0.5 = stone top, lower = grout / cavities)
    <set>_normal.png   OpenGL-style tangent-space normal (green = image-up)
    <set>_orm.png      R = ambient occlusion, G = roughness, B = metallic
Deterministic (fixed seeds).  Physical tile sizes are recorded in docs/ASSET_MANIFEST.md and in the
shaders (tile_m uniforms): flagstone 4.0 m, wall 3.0 m, ledge_cap 2.4 m, pool_tile 1.6 m, metal_plate 2.0 m.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texgen_common import *   # noqa: F401,F403

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "game", "assets", "textures")

YY, XX = np.mgrid[0:N, 0:N].astype(np.float64)


def save_set(name, albedo_lin, height_mm, normal, ao, rough, metal, height_range_mm, out=OUT):
    """height_range_mm: (lo, hi) mapped to alpha 0..1 so the shader can height-blend repeats."""
    lo, hi = height_range_mm
    a = np.clip((height_mm - lo) / (hi - lo), 0, 1)
    rgb = lin_to_srgb(albedo_lin)
    write_png(os.path.join(out, name + "_albedo.png"), to_u8(np.dstack([rgb, a])))
    write_png(os.path.join(out, name + "_normal.png"), to_u8(normal * 0.5 + 0.5))
    write_png(os.path.join(out, name + "_orm.png"), to_u8(np.dstack([ao, rough, metal])))
    if os.path.abspath(out) == OUT:
        write_import(os.path.join(out, name + "_albedo.png"))
        write_import(os.path.join(out, name + "_normal.png"), normal_map=True)
        write_import(os.path.join(out, name + "_orm.png"))
    print("wrote", name)
    prev = os.environ.get("TEX_PREVIEW")
    if prev:
        # 2x2 tiled, lit by a low sun, downsampled to 1024 px: eyeball seams, scale and normals
        L = np.array([0.55, 0.45, 0.7])
        L /= np.linalg.norm(L)
        lit = np.clip(normal @ L, 0, 1)[..., None] * ao[..., None]
        shade = albedo_lin * (0.25 + 1.1 * lit)
        both = np.concatenate([np.tile(lin_to_srgb(albedo_lin), (2, 2, 1))[::2, ::2], np.tile(lin_to_srgb(shade), (2, 2, 1))[::2, ::2]], axis=1)
        write_png(os.path.join(prev, name + "_preview.png"), to_u8(both))


# ============================================================================ masonry family
def masonry(seed, rows, wmin, wmax, tile_m, p):
    """Generic hand-laid stone: flagstones, wall ashlar, ledge slabs.  p = parameter dict (see callers)."""
    px_mm = tile_m * 1000.0 / N
    rng = np.random.default_rng(seed)
    lay = Layout(rows, wmin, wmax, seed + 1, hjit=p.get("hjit", 0.15))
    wamp = p.get("warp_px", 7.0)
    wx = fbm(seed + 2, 2.2, 1, 14) * wamp
    wy = fbm(seed + 3, 2.2, 1, 14) * wamp * 0.7
    xw = (XX + wx) % N
    yw = (YY + wy) % N
    L = lay.label(xw, yw)
    ids = L["id"]
    nid = lay.count

    # per-block attributes
    t_col = rng.random(nid)
    bright = rng.uniform(0.82, 1.12, nid)
    h_off = rng.normal(0, p.get("h_jit_mm", 1.2), nid)
    tiltx = rng.normal(0, p.get("tilt", 0.0008), nid)       # mm per mm
    tilty = rng.normal(0, p.get("tilt", 0.0008), nid)
    theta = rng.uniform(0, np.pi, nid)
    cracked = rng.random(nid) < p.get("crack_frac", 0.12)
    mossy = rng.uniform(0.0, 1.0, nid)

    # ------------------------------------------------------------------ geometry
    d = L["d"]
    chip = np.clip(fbm(seed + 4, 1.3, 8, 160) * 0.5 + 0.2, 0, None)
    d_eff = d - chip * p.get("chip_px", 3.0) - fbm(seed + 5, 1.0, 30, 300) * 0.6
    gw = p["grout_px"]
    inside = smoothstep(gw * 0.5, gw * 1.1 + 1.0, d_eff)         # 0 = grout, 1 = stone
    bev = smoothstep(gw, gw + p["bevel_px"], d_eff)              # 0 = at the arris, 1 = flat top
    uu = (L["u"] - 0.5) * L["bw"] * px_mm
    vv = (L["v"] - 0.5) * L["bh"] * px_mm
    h = h_off[ids] + tiltx[ids] * uu + tilty[ids] * vv
    big = fbm(seed + 6, 2.4, 2, 40)
    mid = fbm(seed + 7, 1.8, 20, 200)
    fine = fbm(seed + 8, 1.0, 120, 512)
    h += big * p.get("big_mm", 0.9) + mid * p.get("mid_mm", 0.45) + fine * p.get("fine_mm", 0.12)
    # pitch-faced boss on ashlar: rough centre, calm margin
    if p.get("boss_mm", 0) > 0:
        marg = smoothstep(p.get("margin_px", 26), p.get("margin_px", 26) + 40, d_eff)
        boss = fbm(seed + 9, 1.6, 6, 120)
        h += marg * (boss * 0.5 + 0.5) * p["boss_mm"]
    # tooling marks (ledge caps): parallel pick strokes with per-slab direction
    if p.get("tool_mm", 0) > 0:
        cs, sn = np.cos(theta[ids]), np.sin(theta[ids])
        coord = XX * cs + YY * sn
        stroke = np.sin(coord / p.get("tool_period_px", 5.0) * 2 * np.pi + fbm(seed + 10, 1, 40, 400) * 3.0)
        h += stroke * p["tool_mm"] * (0.5 + 0.5 * fbm(seed + 11, 2, 2, 30))
    h -= p["edge_drop_mm"] * (1.0 - bev) ** 1.6
    h -= p["grout_mm"] * (1.0 - inside)
    # cracks
    crack_mask = np.zeros_like(h)
    if p.get("crack_frac", 0) > 0:
        cn = fbm(seed + 12, 2.0, 2, 90)
        cn = warp(cn, fbm(seed + 13, 2, 1, 20) * 12, fbm(seed + 14, 2, 1, 20) * 12)
        crack_mask = (1.0 - smoothstep(0.0, 0.07, np.abs(cn))) * cracked[ids] * inside
        h -= crack_mask * 3.2
    # spalls / pits
    pits = smoothstep(2.1, 2.7, fbm(seed + 15, 0.8, 60, 400)) * inside
    h -= pits * 1.4

    # ------------------------------------------------------------------ albedo
    pal = p["palette"]
    ta = t_col[ids]
    base = np.zeros((N, N, 3))
    k = len(pal)
    pos = ta * (k - 1)
    i0 = np.clip(pos.astype(int), 0, k - 2)
    f = (pos - i0)[..., None]
    pal_a = np.array(pal)
    base = pal_a[i0] * (1 - f) + pal_a[i0 + 1] * f
    base = base * bright[ids][..., None]
    blot = fbm(seed + 20, 2.2, 1, 12)
    blot2 = fbm(seed + 21, 1.6, 8, 120)
    grain = fbm(seed + 22, 0.9, 100, 512)
    mm = p.get("mott", 1.0)
    mott = 1.0 + mm * (0.16 * blot + 0.09 * blot2) + 0.05 * grain
    strat_c = XX * np.cos(theta[ids]) + YY * np.sin(theta[ids])
    strata = np.sin(strat_c / p.get("strata_px", 38.0) + fbm(seed + 23, 2, 1, 12) * 5.0)
    mott = mott + p.get("strata", 0.04) * strata
    alb = base * mott[..., None]
    # grime gathers at arrises and in cavities, dust lightens the highs
    grime_edge = np.exp(-d_eff / 26.0)
    alb *= (1.0 - p["grime"] * (0.35 * grime_edge + 0.16 * (blot2 * 0.5 + 0.5)))[..., None]
    hh = blur(h, 4)
    alb *= (1.0 + 0.06 * np.clip(h - hh, -2, 2) * 0.5)[..., None]
    alb += p.get("arris_light", 0.025) * ((1 - bev) * inside)[..., None]
    # stains (rain streaks etc.)
    if p.get("streak", 0) > 0:
        s = gnoise(seed + 24, 90, 5)
        s = smoothstep(0.4, 1.8, s) * (0.6 + 0.4 * fbm(seed + 25, 2, 1, 12))
        alb *= (1.0 - p["streak"] * s)[..., None]
    # lichen flecks
    if p.get("lichen", 0) > 0:
        lf = smoothstep(2.2, 2.9, fbm(seed + 26, 0.7, 50, 300)) * inside * p["lichen"]
        alb = alb * (1 - lf[..., None]) + np.array(p["lichen_col"]) * lf[..., None]
    # dark mineral veining
    vein = (1.0 - smoothstep(0.0, 0.05, np.abs(gnoise(seed + 27, 5, 5) + 0.15 * fbm(seed + 28, 1, 20, 100)))) * p.get("vein", 0.0)
    alb *= (1.0 - 0.28 * vein)[..., None]
    alb *= (1.0 - 0.55 * crack_mask)[..., None]
    alb *= (1.0 - 0.45 * pits)[..., None]

    # grout / mortar
    gcol = np.array(p["grout_col"])
    gn = 0.78 + 0.35 * (fbm(seed + 30, 0.9, 40, 512) * 0.5 + 0.5)
    gcolor = gcol[None, None, :] * gn[..., None]
    gcolor *= (1.0 - 0.35 * smoothstep(0.0, 1.0, fbm(seed + 31, 2, 2, 30)))[..., None]
    alb = alb * inside[..., None] + gcolor * (1 - inside[..., None])

    # moss: creeps out of the joints, favours damp low spots, a few stones carry more
    moss = np.zeros((N, N))
    if p.get("moss", 0) > 0:
        near = np.exp(-np.clip(d, 0, None) / p.get("moss_reach_px", 22.0))
        mf = fbm(seed + 32, 2.4, 2, 40)
        mfine = fbm(seed + 33, 1.0, 60, 400)
        score = mf * 0.5 + 0.75 * near + 0.3 * (mossy[ids] - 0.5) - p.get("moss_thr", 0.55) + mfine * 0.025
        moss = blur(smoothstep(0.0, 0.5, score), 1.6) * p["moss"]
        moss = np.clip(moss * (0.35 + 0.65 * smoothstep(-1.5, 0.5, -h + (hh - h) * 0)), 0, 1)
        mcol_a = np.array(p["moss_a"])
        mcol_b = np.array(p["moss_b"])
        mcol = mcol_a + (mcol_b - mcol_a) * (mfine * 0.5 + 0.5)[..., None]
        mcol = mcol * (0.85 + 0.3 * (fbm(seed + 34, 1.2, 30, 300) * 0.5 + 0.5))[..., None]
        alb = alb * (1 - moss[..., None]) + mcol * moss[..., None]
        h = h + moss * (1.0 + 0.8 * mfine) * 0.9

    # ------------------------------------------------------------------ roughness / ao
    pol = smoothstep(0.2, 0.9, 1 - np.clip(np.abs(L["u"] - 0.5) * 2, 0, 1)) * smoothstep(0.2, 0.9, 1 - np.clip(np.abs(L["v"] - 0.5) * 2, 0, 1))
    rough = p["rough"] + 0.10 * fbm(seed + 40, 1.5, 4, 200) - p.get("polish", 0.12) * pol * (0.5 + 0.5 * blot)
    rough = rough + 0.08 * grain
    rough = rough * inside + 0.97 * (1 - inside)
    rough = rough * (1 - moss) + 0.93 * moss
    rough = np.clip(rough, 0.25, 1.0)
    ao = cavity_ao(h, 6.0, 4.5, 0.2) * cavity_ao(h, 18.0, 14.0, 0.45)
    ao = ao * (1 - 0.45 * (1 - inside)) + 0.0
    ao = np.clip(ao, 0.15, 1.0)
    metal = np.zeros((N, N))
    nrm = normal_from_height(h, px_mm, p.get("nstrength", 1.0))
    return alb, h, nrm, ao, rough, metal


def gen_flagstone(out=OUT):
    p = dict(
        grout_px=5.0, bevel_px=10.0, edge_drop_mm=2.6, grout_mm=7.0, h_jit_mm=1.8, tilt=0.0011,
        big_mm=1.1, mid_mm=0.5, fine_mm=0.16, chip_px=4.5, crack_frac=0.14, grime=0.9, rough=0.72, polish=0.16,
        palette=[hexlin("#a58e6f"), hexlin("#b49a74"), hexlin("#8c8473"), hexlin("#7a7a76"), hexlin("#9a8268")],
        grout_col=hexlin("#5b5042"), moss=0.95, moss_a=hexlin("#38501f"), moss_b=hexlin("#5a6a2c"), moss_thr=0.5,
        moss_reach_px=26.0, strata=0.05, strata_px=44.0, vein=0.5, lichen=0.7, lichen_col=hexlin("#9aa27a"),
        warp_px=6.0, hjit=0.3, nstrength=1.0)
    r = masonry(101, 5, 0.2, 0.42, 4.0, p)
    save_set("flagstone", *_pack(r), (-9.0, 6.0), out=out)


def gen_wall(out=OUT):
    p = dict(
        grout_px=7.0, bevel_px=9.0, edge_drop_mm=3.0, grout_mm=11.0, h_jit_mm=2.6, tilt=0.0006,
        big_mm=1.4, mid_mm=0.7, fine_mm=0.2, chip_px=3.5, crack_frac=0.07, grime=0.9, rough=0.82, polish=0.04,
        palette=[hexlin("#9b8566"), hexlin("#a98f6a"), hexlin("#857a68"), hexlin("#74716b"), hexlin("#917a5e")],
        grout_col=hexlin("#7c6e5a"), moss=0.7, moss_a=hexlin("#3b5420"), moss_b=hexlin("#62702f"), moss_thr=0.62,
        moss_reach_px=16.0, strata=0.06, strata_px=30.0, vein=0.2, lichen=0.9, lichen_col=hexlin("#a6a98a"),
        boss_mm=5.5, margin_px=22, streak=0.3, warp_px=3.5, hjit=0.05, arris_light=0.03, nstrength=1.0)
    r = masonry(202, 10, 0.26, 0.44, 3.0, p)
    save_set("wall", *_pack(r), (-14.0, 10.0), out=out)


def gen_ledge_cap(out=OUT):
    p = dict(
        grout_px=4.0, bevel_px=16.0, edge_drop_mm=3.6, grout_mm=5.0, h_jit_mm=1.0, tilt=0.0006,
        big_mm=0.8, mid_mm=0.35, fine_mm=0.12, chip_px=3.0, crack_frac=0.08, grime=0.7, rough=0.66, polish=0.2,
        palette=[hexlin("#b09a78"), hexlin("#bba682"), hexlin("#a0967f"), hexlin("#948c7c")],
        grout_col=hexlin("#6a5e4d"), moss=0.5, moss_a=hexlin("#45602a"), moss_b=hexlin("#738338"), moss_thr=0.68,
        moss_reach_px=14.0, strata=0.035, strata_px=60.0, vein=0.25, lichen=0.55, lichen_col=hexlin("#b0b392"),
        tool_mm=0.35, tool_period_px=6.0, mott=0.5, warp_px=4.0, hjit=0.05, arris_light=0.05, nstrength=1.0)
    r = masonry(303, 2, 0.38, 0.62, 2.4, p)
    save_set("ledge_cap", *_pack(r), (-8.0, 4.0), out=out)


def _pack(r):
    alb, h, nrm, ao, rough, metal = r
    return alb, h, nrm, ao, rough, metal


# ============================================================================ pool tile
def gen_pool_tile(out=OUT):
    seed = 404
    rng = np.random.default_rng(seed)
    tile_m = 1.6
    px_mm = tile_m * 1000.0 / N
    T = 64                      # 16 x 16 tiles of 10 cm
    nt = N // T
    ix = (XX // T).astype(int)
    iy = (YY // T).astype(int)
    lx = XX % T
    ly = YY % T
    d = np.minimum(np.minimum(lx, T - 1 - lx), np.minimum(ly, T - 1 - ly)) + 0.5
    # slight wobble of the joints so the grid is hand-set rather than machine-perfect
    wob = fbm(seed + 1, 2.0, 1, 10) * 1.2
    d = d + wob * 0.0
    tid = iy * nt + ix
    hue = rng.random(nt * nt)
    lum = rng.uniform(0.82, 1.15, nt * nt)
    cols = [hexlin("#2f7f86"), hexlin("#3b8d93"), hexlin("#2a6e7e"), hexlin("#4d9a96"), hexlin("#c9c1a6")]
    probs = np.array([0.35, 0.29, 0.2, 0.155, 0.005])
    cls = np.searchsorted(np.cumsum(probs), hue)
    base = np.array(cols)[np.clip(cls, 0, 4)][tid] * lum[tid][..., None]
    gw = 2.8
    inside = smoothstep(gw * 0.5, gw + 1.2, d)
    bev = smoothstep(gw, gw + 6.0, d)
    crown = rng.normal(0, 0.25, nt * nt)
    h = crown[tid] + (1 - bev) * -0.9
    h += fbm(seed + 2, 1.6, 4, 100) * 0.12
    h -= 1.4 * (1 - inside)
    # glaze crazing and mottling
    craze = (1 - smoothstep(0.0, 0.05, np.abs(fbm(seed + 3, 1.5, 6, 120)))) * 0.5
    glaze = fbm(seed + 4, 2.0, 1, 30) * 0.5 + fbm(seed + 5, 1.0, 40, 400) * 0.25
    alb = base * (1.0 + 0.08 * glaze)[..., None] * (1 - 0.18 * craze)[..., None]
    alb *= (1.0 - 0.25 * np.exp(-d / 10.0))[..., None]            # glaze thins at edges
    grout = hexlin("#9a9a90") * (0.8 + 0.3 * (fbm(seed + 6, 1, 20, 300) * 0.5 + 0.5))[..., None]
    grime = smoothstep(0.1, 1.4, fbm(seed + 7, 2.0, 1, 10))
    grout = grout * (1 - 0.5 * grime[..., None]) + hexlin("#455a2c") * 0.5 * (grime[..., None] * 0.6)
    alb = alb * inside[..., None] + grout * (1 - inside[..., None])
    # chips exposing the pale body
    chips = smoothstep(2.3, 2.9, fbm(seed + 8, 0.8, 60, 400)) * inside * (1 - bev * 0.3)
    alb = alb * (1 - chips[..., None]) + hexlin("#c8bfa8") * chips[..., None]
    h -= chips * 0.5
    rough = 0.2 + 0.12 * fbm(seed + 9, 1.4, 4, 200) + 0.06 * craze
    rough = rough * inside + 0.85 * (1 - inside)
    rough = np.clip(rough + chips * 0.5, 0.08, 1.0)
    ao = cavity_ao(h, 3.0, 1.8, 0.35)
    ao *= 1 - 0.35 * (1 - inside)
    nrm = normal_from_height(h, px_mm, 1.4)
    save_set("pool_tile", alb, h, nrm, np.clip(ao, 0.2, 1), rough, np.zeros((N, N)), (-3.0, 1.0), out=out)


# ============================================================================ brushed metal deck plate
def _splat_scratches(seed, count, length, width_px=1.0):
    """Periodic random line scratches -> float image 0..1."""
    rng = np.random.default_rng(seed)
    img = np.zeros((N, N))
    for _ in range(count):
        x0, y0 = rng.uniform(0, N, 2)
        ang = rng.normal(0, 0.06) if rng.random() < 0.75 else rng.uniform(0, np.pi)
        ln = rng.uniform(0.2, 1.0) * length
        t = np.linspace(0, ln, int(ln * 2))
        xs = (x0 + t * np.cos(ang)).astype(int) % N
        ys = (y0 + t * np.sin(ang)).astype(int) % N
        a = rng.uniform(0.4, 1.0)
        img[ys, xs] = np.maximum(img[ys, xs], a)
    return np.clip(blur(img, width_px) * 3.0, 0, 1)


def gen_metal_plate(out=OUT):
    seed = 505
    rng = np.random.default_rng(seed)
    tile_m = 2.0
    px_mm = tile_m * 1000.0 / N
    P = N // 2                       # 2 x 2 plates of 1 m
    pid = (YY // P).astype(int) * 2 + (XX // P).astype(int)
    lx = XX % P
    ly = YY % P
    d = np.minimum(np.minimum(lx, P - 1 - lx), np.minimum(ly, P - 1 - ly)) + 0.5
    plate_b = rng.uniform(0.88, 1.12, 4)
    plate_phase = rng.random(4)
    # brushed grain: elongated along x
    g1 = gnoise(seed + 1, 7, 330)
    g2 = gnoise(seed + 2, 18, 460)
    g3 = gnoise(seed + 3, 2, 80)
    brush = g1 * 0.55 + g2 * 0.35
    scr = _splat_scratches(seed + 4, 520, 260, 0.8)
    scr2 = _splat_scratches(seed + 5, 90, 700, 1.0)
    gw = 5.0
    inside = smoothstep(gw * 0.5, gw + 1.0, d)
    bev = smoothstep(gw, gw + 8.0, d)
    h = brush * 0.012 + scr * -0.03 + scr2 * -0.05
    h += (1 - bev) * -0.55 - 0.7 * (1 - inside)
    h += fbm(seed + 6, 2.2, 1, 12) * 0.08 * 0
    # bolts: hex-ish domes at four corners per plate, plus a washer ring
    bolt = np.zeros((N, N))
    ring = np.zeros((N, N))
    dome = np.zeros((N, N))
    inset = 82.0
    for cy in (inset, P - inset):
        for cx in (inset, P - inset):
            for oy in (0, P):
                for ox in (0, P):
                    X0, Y0 = cx + ox, cy + oy
                    dx = np.abs(((XX - X0 + N / 2) % N) - N / 2)
                    dy = np.abs(((YY - Y0 + N / 2) % N) - N / 2)
                    rr = np.sqrt(dx * dx + dy * dy)
                    hexd = np.maximum(dx * 0.866 + dy * 0.5, dy)       # hexagon distance
                    bolt = np.maximum(bolt, 1 - smoothstep(15.0, 17.0, hexd))
                    ring = np.maximum(ring, (1 - smoothstep(26.0, 28.0, rr)) * smoothstep(21.0, 23.0, rr) * 0.0 + (1 - smoothstep(30.0, 32.0, rr)))
                    dome = np.maximum(dome, np.clip(1 - rr / 17.0, 0, 1))
    h += ring * 0.35 + bolt * 0.9 + dome * 0.2
    # rust: creeps from joints, bolts and a few random spots
    rn = fbm(seed + 7, 2.0, 2, 60)
    near_j = np.exp(-d / 38.0)
    near_b = np.clip(ring, 0, 1) * 0.8
    rust_m = smoothstep(0.55, 1.5, rn * 0.9 + near_j * 1.3 + near_b * 1.1 - 0.6 + fbm(seed + 8, 1.0, 40, 400) * 0.25) * 0.6
    rust_m *= inside * 0.8 + 0.2
    # colour
    steel = hexlin("#4a4e56")
    alb = steel[None, None, :] * (plate_b[pid] * (1 + 0.05 * brush) * (1 + 0.04 * g3))[..., None]
    alb *= (1 - 0.22 * scr)[..., None] * 0 + 1
    alb = alb * (1 + 0.55 * scr)[..., None]                   # scratches catch the light
    alb = alb * (1 - 0.35 * np.exp(-d / 9.0))[..., None] * 0 + alb
    alb *= (1 - 0.4 * np.exp(-d / 8.0))[..., None]
    alb = alb * (1 - 0.35 * (1 - inside))[..., None]
    alb = np.where((ring > 0.5)[..., None], alb * 0.55, alb)       # dark washer ring
    alb = np.where((bolt > 0.5)[..., None], steel[None, None, :] * 1.35 * (1 + 0.05 * brush[..., None]), alb)
    # grime pooling in the seams
    alb *= (1 - 0.5 * np.exp(-d / 5.0))[..., None]
    rcol = hexlin("#7a3a18") * (0.7 + 0.5 * (fbm(seed + 9, 1.3, 20, 300) * 0.5 + 0.5))[..., None]
    alb = alb * (1 - rust_m[..., None]) + rcol * rust_m[..., None]
    rough = 0.36 + 0.12 * (g1 * 0.5 + 0.5) + 0.2 * scr2 - 0.05 * np.clip(g3, -1, 1)
    rough = rough * (1 - rust_m) + 0.82 * rust_m
    rough = np.where(inside < 0.5, 0.9, rough)
    metal = np.clip(0.92 - 0.82 * rust_m - 0.06 * scr, 0.05, 1.0)
    metal = np.where(inside < 0.5, 0.35, metal)
    ao = np.clip(1.0 - 0.7 * np.exp(-d / 7.0) - 0.4 * np.clip(ring - bolt, 0, 1), 0.2, 1.0)
    nrm = normal_from_height(h * 3.0, px_mm, 1.0)
    save_set("metal_plate", alb, h * 3.0, nrm, ao, np.clip(rough, 0.12, 1), metal, (-6.0, 6.0), out=out)


GENS = dict(flagstone=gen_flagstone, wall=gen_wall, ledge_cap=gen_ledge_cap, pool_tile=gen_pool_tile, metal_plate=gen_metal_plate)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    names = [n for n in a.only.split(",") if n] or list(GENS)
    for n in names:
        GENS[n](a.out)
