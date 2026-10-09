#!/usr/bin/env python3
"""Fourfold open world - generates the valley (terrain, regions, paths, encounter sites, shrines, props).

One script owns the whole world description so the sim and the renderer read the same data:
  Content/Fourfold/Data/openworld/world.json   regions, sites, shrines, solids, metal plates, props, water
  Content/Fourfold/Data/openworld/heights.f32  float32 LE heightfield (nx * nz, z rows), metres, world y
  Content/Fourfold/Data/openworld/splat.rgba   uint8 RGBA per sample: grass, rock, sand, ash weights
  docs/openworld/previews/map.png              shaded relief + regions / paths / sites (review image)

World space: metres, x / z in [-1024, 1024], +y up (sim space without the bubble origin).
Deterministic (fixed seeds). numpy + Pillow only. Never hand-edit the outputs; change this script and re-run:
  python3 unreal/Tools/openworld/gen_world.py
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT_DATA = os.path.join(UNREAL, "Content", "Fourfold", "Data", "openworld")
OUT_PREVIEW = os.path.join(UNREAL, "docs", "openworld", "previews")

HALF = 1024.0
CELL = 2.0
N = int(2 * HALF / CELL) + 1          # 1025 samples per side
WATER_LEVEL = 0.0
WADE_DEPTH = 0.85
SEED = 4417

xs = np.linspace(-HALF, HALF, N)
X, Z = np.meshgrid(xs, xs)            # Z rows (k), X columns (i)

REGIONS = {
    "hub":   {"name": "Temple of the Four Gates", "center": (0.0, 0.0), "radius": 210.0},
    "water": {"name": "Stillwater Shore", "center": (-500.0, 450.0), "radius": 420.0},
    "earth": {"name": "Redstone Quarry", "center": (-480.0, -470.0), "radius": 420.0},
    "fire":  {"name": "Ember Caldera", "center": (500.0, -480.0), "radius": 420.0},
    "air":   {"name": "Windspire Peaks", "center": (500.0, 470.0), "radius": 420.0},
}
ELEMENT = {"earth": 0, "water": 1, "fire": 2, "air": 3}


# ----------------------------------------------------------------------------- noise
def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def value_noise(cell_m, seed):
    """Bicubic-smoothed value noise in [0, 1] over the world grid, lattice spacing cell_m metres."""
    rng = np.random.default_rng(seed)
    n = int(math.ceil(2 * HALF / cell_m)) + 3
    lat = rng.random((n, n))
    fx = (X + HALF) / cell_m
    fz = (Z + HALF) / cell_m
    i = np.floor(fx).astype(int)
    k = np.floor(fz).astype(int)
    tx = fx - i
    tz = fz - k
    tx = tx * tx * (3 - 2 * tx)
    tz = tz * tz * (3 - 2 * tz)
    a = lat[k, i]
    b = lat[k, i + 1]
    c = lat[k + 1, i]
    d = lat[k + 1, i + 1]
    return (a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz


def fbm(cell_m, octaves, seed, gain=0.5):
    total = np.zeros_like(X)
    amp = 1.0
    norm = 0.0
    for o in range(octaves):
        total += amp * value_noise(cell_m / (2 ** o), seed + 101 * o)
        norm += amp
        amp *= gain
    return total / norm


def ridged(cell_m, octaves, seed):
    total = np.zeros_like(X)
    amp = 1.0
    norm = 0.0
    prev = np.ones_like(X)
    for o in range(octaves):
        n = 1.0 - np.abs(2.0 * value_noise(cell_m / (2 ** o), seed + 211 * o) - 1.0)
        n = n * n * prev
        prev = np.clip(n * 1.6, 0.0, 1.0)
        total += amp * n
        norm += amp
        amp *= 0.5
    return total / norm


def blur(a, radius_cells):
    """Separable box blur x3 (~gaussian), edge-clamped."""
    out = a.astype(np.float64)
    r = int(max(1, radius_cells))
    for _ in range(3):
        for axis in (0, 1):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (r, r)
            p = np.pad(out, pad, mode="edge")
            c = np.cumsum(p, axis=axis)
            c = np.insert(c, 0, 0.0, axis=axis)
            if axis == 0:
                out = (c[2 * r + 1:, :] - c[:-2 * r - 1, :]) / (2 * r + 1)
            else:
                out = (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)
    return out


_WARP = None


def warp():
    """Domain warp (metres) that makes region borders and the mountain ring organic."""
    global _WARP
    if _WARP is None:
        _WARP = (90.0 * (fbm(380.0, 3, SEED + 401) - 0.5), 90.0 * (fbm(380.0, 3, SEED + 409) - 0.5))
    return _WARP


def dist_to(cx, cz, warped=False):
    if warped:
        wx, wz = warp()
        return np.hypot(X + wx - cx, Z + wz - cz)
    return np.hypot(X - cx, Z - cz)


def region_weight(key, sharp=1.0):
    r = REGIONS[key]
    d = dist_to(*r["center"], warped=True)
    return smoothstep(r["radius"], r["radius"] * (0.45 / sharp), d)


# ----------------------------------------------------------------------------- terrain
def build_height():
    # Rolling meadow: broad swells + mid hills + small lumps (heights ~8..60 m before the regions).
    base = 8.0 + 34.0 * fbm(520.0, 3, SEED) ** 1.4 + 14.0 * fbm(170.0, 4, SEED + 3) + 3.0 * (fbm(40.0, 3, SEED + 7) - 0.5)

    # Water: a lake bowl (surface 0) with a gentle beach shelf; an inlet reaches toward the hub.
    wc = REGIONS["water"]["center"]
    rx = (X - wc[0]) / 300.0
    rz = (Z - wc[1]) / 230.0
    lake_warp = 0.18 * (fbm(140.0, 3, SEED + 31) - 0.5)
    lake_r = np.sqrt(rx * rx + rz * rz) + lake_warp
    lake = -6.5 * smoothstep(1.0, 0.55, lake_r) + 1.2 * smoothstep(1.25, 1.0, lake_r)
    shore = smoothstep(1.55, 1.0, lake_r)
    h = base * (1 - shore) + (lake + 2.5 * (lake_r - 0.8).clip(0, None)) * shore

    # Earth: terraced mesas cut by canyons (floor ~22 m, mesa tops 45 / 70 / 95 m).
    ew = region_weight("earth")
    m = ridged(220.0, 4, SEED + 53)
    steps = 3.0
    q = m * steps
    terr = (np.floor(q) + smoothstep(0.82, 1.0, q - np.floor(q))) / steps
    mesa = 22.0 + 75.0 * smoothstep(0.25, 0.95, terr)
    h = h * (1 - ew) + mesa * ew

    # Fire: a volcanic cone (crater rim ~190 m) over an ash plain.
    fc = REGIONS["fire"]["center"]
    rf = dist_to(*fc) / 340.0
    cone = 200.0 * np.clip(1.0 - rf, 0, 1) ** 1.6 + 6.0 * ridged(90.0, 3, SEED + 77) * np.clip(1.0 - rf, 0, 1)
    crater = 55.0 * smoothstep(0.17, 0.08, rf)
    fw = region_weight("fire", 0.8)
    plain = 14.0 + 4.0 * fbm(120.0, 3, SEED + 79)
    h = h * (1 - fw) + (np.maximum(plain, cone - crater)) * fw

    # Air: wind-carved peaks with three flat shrine terraces (60, 110, 160 m).
    ac = REGIONS["air"]["center"]
    aw = region_weight("air", 0.9)
    peaks = 30.0 + 260.0 * ridged(260.0, 5, SEED + 97) * smoothstep(420.0, 60.0, dist_to(*ac))
    for level, pos, rad in ((60.0, (380.0, 330.0), 70.0), (110.0, (560.0, 400.0), 55.0), (160.0, (520.0, 600.0), 45.0)):
        pw = smoothstep(rad * 1.6, rad, dist_to(*pos))
        peaks = peaks * (1 - pw) + level * pw
    h = h * (1 - aw) + peaks * aw

    # Hub: a temple plateau at 20 m.
    hw = smoothstep(200.0, 90.0, dist_to(0.0, 0.0))
    h = h * (1 - hw) + 20.0 * hw

    # Mountain ring closing the valley: ridged peaks on a warped, rounded border.
    wx, wz = warp()
    ex = np.abs(X + 1.4 * wx) / HALF
    ez = np.abs(Z + 1.4 * wz) / HALF
    edge = (1.0 - (ex ** 6 + ez ** 6) ** (1.0 / 6.0)) * HALF       # rounded-square distance to the border
    ring_w = smoothstep(330.0, 70.0, edge)
    ring = ring_w * (90.0 + 330.0 * ridged(230.0, 5, SEED + 131)) + smoothstep(90.0, 0.0, edge) * 120.0
    h = h + ring
    return h


# ----------------------------------------------------------------------------- paths
def catmull(points, per_seg=24):
    pts = [points[0]] + list(points) + [points[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = (np.array(pts[j], dtype=float) for j in (i - 1, i, i + 1, i + 2))
        for s in range(per_seg):
            t = s / per_seg
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                              (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    out.append(np.array(points[-1], dtype=float))
    return np.array(out)


def sample(h, x, z):
    fx = (x + HALF) / CELL
    fz = (z + HALF) / CELL
    i = int(np.clip(math.floor(fx), 0, N - 2))
    k = int(np.clip(math.floor(fz), 0, N - 2))
    tx = fx - i
    tz = fz - k
    return float((h[k, i] * (1 - tx) + h[k, i + 1] * tx) * (1 - tz) + (h[k + 1, i] * (1 - tx) + h[k + 1, i + 1] * tx) * tz)


PATHS = [
    # hub gates -> each region (control points, world x / z)
    [(0, 60), (-90, 170), (-230, 260), (-360, 330), (-430, 300)],          # water: down to the beach
    [(-60, -20), (-170, -110), (-300, -260), (-420, -380), (-470, -450)],   # earth: into the canyon floor
    [(60, -40), (170, -150), (290, -270), (380, -360), (430, -420)],       # fire: up to the ash plain
    [(50, 50), (160, 150), (260, 250), (340, 300), (380, 330)],            # air: to the first terrace
    [(380, 330), (470, 330), (540, 360), (560, 400)],                      # air: switchbacks to the second terrace
    # ring road between regions
    [(-430, 300), (-560, 60), (-520, -200), (-470, -450)],
    [(-470, -450), (-180, -600), (180, -620), (430, -420)],
    [(430, -420), (650, -150), (620, 120), (380, 330)],
]
PATH_MAX_SLOPE = 0.22


def carve_paths(h):
    polys = [catmull(p) for p in PATHS]
    for poly in polys:
        seg_len = np.hypot(np.diff(poly[:, 0]), np.diff(poly[:, 1]))
        along = np.concatenate([[0.0], np.cumsum(seg_len)])
        ph = np.array([sample(h, x, z) for x, z in poly])
        # smooth + clamp the grade both ways
        k = 9
        ph = np.convolve(np.pad(ph, (k, k), mode="edge"), np.ones(2 * k + 1) / (2 * k + 1), mode="valid")
        for _ in range(3):
            for i in range(1, len(ph)):
                ph[i] = np.clip(ph[i], ph[i - 1] - PATH_MAX_SLOPE * seg_len[i - 1], ph[i - 1] + PATH_MAX_SLOPE * seg_len[i - 1])
            for i in range(len(ph) - 2, -1, -1):
                ph[i] = np.clip(ph[i], ph[i + 1] - PATH_MAX_SLOPE * seg_len[i], ph[i + 1] + PATH_MAX_SLOPE * seg_len[i])
        ph = np.maximum(ph, WATER_LEVEL + 0.6)
        best_d = np.full(h.shape, 1e9)
        best_h = np.zeros(h.shape)
        for i in range(len(poly) - 1):
            a, b = poly[i], poly[i + 1]
            x0, x1 = sorted((a[0], b[0]))
            z0, z1 = sorted((a[1], b[1]))
            pad = 30.0
            i0 = max(0, int((x0 - pad + HALF) / CELL))
            i1 = min(N, int((x1 + pad + HALF) / CELL) + 2)
            k0 = max(0, int((z0 - pad + HALF) / CELL))
            k1 = min(N, int((z1 + pad + HALF) / CELL) + 2)
            sx = X[k0:k1, i0:i1]
            sz = Z[k0:k1, i0:i1]
            ab = b - a
            L2 = max(float(ab @ ab), 1e-6)
            t = np.clip(((sx - a[0]) * ab[0] + (sz - a[1]) * ab[1]) / L2, 0, 1)
            d = np.hypot(sx - (a[0] + t * ab[0]), sz - (a[1] + t * ab[1]))
            hh = ph[i] + (ph[i + 1] - ph[i]) * t
            sub_d = best_d[k0:k1, i0:i1]
            m = d < sub_d
            sub_d[m] = d[m]
            best_h[k0:k1, i0:i1][m] = hh[m]
        w = smoothstep(22.0, 5.0, best_d)
        h = h * (1 - w) + best_h * w
    return h, polys


# ----------------------------------------------------------------------------- sites
SITE_PLAN = [
    # region, nominal (x, z), name, sub, preset
    ("hub", (0.0, -40.0), "Gatekeeper Sen", -1, "master"),
    ("water", (-360.0, 300.0), "Tide Adept Lin", 0, "novice"),
    ("water", (-610.0, 170.0), "Mist Walker Oru", 2, "adept"),
    ("water", (-300.0, 520.0), "Frost Warden Kai", 1, "adept"),
    ("earth", (-430.0, -430.0), "Quarry Hand Bo", 0, "novice"),
    ("earth", (-620.0, -330.0), "Sand Reader Tau", 2, "adept"),
    ("earth", (-300.0, -620.0), "Iron Warden Mei", 3, "master"),
    ("fire", (400.0, -400.0), "Cinder Novice Ra", 0, "novice"),
    ("fire", (640.0, -300.0), "Spark Monk Jin", 2, "adept"),
    ("fire", (520.0, -610.0), "Ash Warden Hal", 1, "master"),
    ("air", (380.0, 330.0), "Gale Pupil Yu", 0, "novice"),
    ("air", (560.0, 400.0), "Echo Hermit Sol", 2, "adept"),
    ("air", (520.0, 600.0), "Sky Warden Ilo", 3, "master"),
]
PAD_R = 13.0


def flatten_pad(h, cx, cz, r, level=None):
    d = dist_to(cx, cz)
    core = d < r
    if level is None:
        level = float(np.median(h[core]))
    w = smoothstep(r * 1.9, r, d)
    return h * (1 - w) + level * w, level


def place_sites(h):
    sites = []
    for idx, (reg, (nx, nz), name, sub, preset) in enumerate(SITE_PLAN):
        # flattest spot within 40 m of the nominal position (slope from a blurred field)
        gz, gx = np.gradient(h, CELL)
        slope = blur(np.hypot(gx, gz), 4)
        k0 = int((nz - 40 + HALF) / CELL)
        k1 = int((nz + 40 + HALF) / CELL)
        i0 = int((nx - 40 + HALF) / CELL)
        i1 = int((nx + 40 + HALF) / CELL)
        win = slope[k0:k1, i0:i1] + 0.002 * dist_to(nx, nz)[k0:k1, i0:i1]
        kk, ii = np.unravel_index(np.argmin(win), win.shape)
        sx = float(xs[i0 + ii])
        sz = float(xs[k0 + kk])
        level = None
        if reg == "water":
            level = 0.45                 # a shore pad: the lake is inside the fight bubble
        h, level = flatten_pad(h, sx, sz, PAD_R, level)
        facing = math.atan2(-sx, -sz)    # toward the hub
        sites.append({
            "id": f"site_{reg}_{idx:02d}", "name": name, "region": reg,
            "pos": [round(sx, 2), round(float(level), 3), round(sz, 2)], "facing": round(facing, 4),
            "element": ELEMENT.get(reg, 0) if reg != "hub" else 0, "sub": sub, "preset": preset,
            "aggro_radius": 10.0 if preset != "master" else 12.0,
        })
    # hub master fights with every element: earth by default, the planner mixes (sub -1)
    return h, sites


SHRINES = [
    ("shrine_hub", "Temple Gate Shrine", (30.0, 40.0)),
    ("shrine_water", "Shore Shrine", (-400.0, 250.0)),
    ("shrine_earth", "Quarry Shrine", (-380.0, -360.0)),
    ("shrine_fire", "Ash Shrine", (360.0, -330.0)),
    ("shrine_air", "Wind Shrine", (330.0, 280.0)),
]


# ----------------------------------------------------------------------------- solids / metal
def solids_for(h, sites):
    solids = []

    def box(cx, cz, hx, hz, height, kind, name, sink=1.0):
        g = min(sample(h, cx + ox, cz + oz) for ox in (-hx, hx) for oz in (-hz, hz))
        solids.append({"min": [round(cx - hx, 2), round(g - sink, 2), round(cz - hz, 2)],
                       "max": [round(cx + hx, 2), round(g + height, 2), round(cz + hz, 2)], "kind": kind,
                       "surface": "stone", "name": name})

    # hub: eight pillars around the plaza + two low walls
    for i in range(8):
        a = i * math.pi / 4 + math.pi / 8
        box(46 * math.cos(a), 46 * math.sin(a), 0.6, 0.6, 4.0, "pillar", f"hub_pillar_{i}")
    box(-18.0, -70.0, 4.0, 0.4, 1.6, "wall", "hub_wall_s")
    box(18.0, 70.0, 4.0, 0.4, 1.6, "wall", "hub_wall_n")
    # each site: one cover wall + one ledge to fight around
    for s in sites:
        x, _, z = s["pos"]
        f = s["facing"]
        cx = x + 6.0 * math.cos(f)
        cz = z - 6.0 * math.sin(f)
        box(cx, cz, 1.6, 0.35, 1.5, "wall", s["id"] + "_cover")
        lx = x - 7.0 * math.sin(f + 0.9)
        lz = z - 7.0 * math.cos(f + 0.9)
        box(lx, lz, 2.0, 2.0, 0.6 if s["preset"] == "novice" else 1.5, "ledge", s["id"] + "_ledge", sink=0.4)
    return solids


def metal_rects(sites):
    out = []
    for s in sites:
        if s["region"] == "earth":
            x, _, z = s["pos"]
            out.append({"min": [round(x - 9.0, 2), round(z + 3.0, 2)], "max": [round(x - 4.0, 2), round(z + 8.0, 2)]})
    return out


# ----------------------------------------------------------------------------- splat + props
def build_splat(h, polys_mask):
    gz, gx = np.gradient(h, CELL)
    slope = np.hypot(gx, gz)
    ew = region_weight("earth")
    fw = region_weight("fire", 0.8)
    wat = region_weight("water")
    rock = smoothstep(0.55, 0.95, slope)
    sand = np.maximum(smoothstep(3.0, 0.8, h) * (h > -20), ew * 0.85 * smoothstep(40.0, 24.0, h))
    sand = np.maximum(sand, 0.55 * polys_mask)
    fc = REGIONS["fire"]["center"]
    ash = fw * smoothstep(0.2, 0.6, fw) * (0.55 + 0.45 * smoothstep(20.0, 70.0, h)) * smoothstep(0.25, 0.6, fbm(70.0, 3, SEED + 503) + 0.3 * smoothstep(300.0, 120.0, dist_to(*fc)))
    grass = np.clip(1.0 - rock - sand - ash, 0.0, 1.0)
    del wat
    w = np.stack([grass, rock, sand, ash], axis=-1)
    w = w / np.maximum(w.sum(axis=-1, keepdims=True), 1e-6)
    return (np.clip(w, 0, 1) * 255 + 0.5).astype(np.uint8), slope


TREES = {"broad": ["broadleaf_a", "broadleaf_b"], "fir": ["fir_a", "fir_b", "fir_c"], "slim": ["firslim_a", "firslim_b", "firslim_c"]}
ROCKS = ["rock_granite", "rock_strata", "rock_cliff"]


def scatter_props(h, splat, slope, sites, path_dist):
    rng = np.random.default_rng(SEED + 999)
    props = []
    spacing = 9.0
    g = np.arange(-HALF + 30, HALF - 30, spacing)
    density = {"hub": 0.22, "water": 0.55, "earth": 0.07, "fire": 0.035, "air": 0.25}
    centers = {k: v["center"] for k, v in REGIONS.items()}
    site_xz = np.array([[s["pos"][0], s["pos"][2]] for s in sites])
    for gz_ in g:
        for gx_ in g:
            x = gx_ + rng.uniform(-3.8, 3.8)
            z = gz_ + rng.uniform(-3.8, 3.8)
            i = int(round((x + HALF) / CELL))
            k = int(round((z + HALF) / CELL))
            hh = float(h[k, i])
            if hh < 1.5 or slope[k, i] > 0.38 or path_dist[k, i] < 12.0:
                continue
            if np.min(np.hypot(site_xz[:, 0] - x, site_xz[:, 1] - z)) < 28.0:
                continue
            if math.hypot(x, z) < 110.0:
                continue
            reg = min(centers, key=lambda r: math.hypot(x - centers[r][0], z - centers[r][1]))
            grass = splat[k, i, 0] / 255.0
            if rng.random() > density[reg] * grass:
                continue
            if reg == "air" or hh > 120.0:
                kind = rng.choice(TREES["fir"] + TREES["slim"])
            elif reg == "water":
                kind = rng.choice(TREES["broad"] * 2 + TREES["slim"])
            else:
                kind = rng.choice(TREES["broad"] + TREES["fir"])
            props.append({"mesh": f"tree_{kind}", "pos": [round(x, 2), round(hh - 0.15, 2), round(z, 2)],
                          "yaw": round(rng.uniform(0, 360), 1), "scale": round(rng.uniform(0.8, 1.25), 3)})
    # rocks: everywhere off-path, more in earth / fire / on slopes
    rock_density = {"hub": 0.02, "water": 0.03, "earth": 0.12, "fire": 0.1, "air": 0.06}
    g2 = np.arange(-HALF + 20, HALF - 20, 14.0)
    for gz_ in g2:
        for gx_ in g2:
            x = gx_ + rng.uniform(-6, 6)
            z = gz_ + rng.uniform(-6, 6)
            i = int(round((x + HALF) / CELL))
            k = int(round((z + HALF) / CELL))
            if path_dist[k, i] < 10.0 or h[k, i] < -0.5:
                continue
            if np.min(np.hypot(site_xz[:, 0] - x, site_xz[:, 1] - z)) < 22.0:
                continue
            reg = min(centers, key=lambda r: math.hypot(x - centers[r][0], z - centers[r][1]))
            if rng.random() > rock_density[reg] * (1.0 + 2.0 * min(slope[k, i], 1.0)):
                continue
            props.append({"mesh": rng.choice(ROCKS), "pos": [round(x, 2), round(float(h[k, i]) - 0.4, 2), round(z, 2)],
                          "yaw": round(rng.uniform(0, 360), 1), "scale": round(rng.uniform(0.35, 1.1), 3)})
    return props


def path_distance(polys):
    best = np.full(X.shape, 1e9)
    for poly in polys:
        for i in range(len(poly) - 1):
            a, b = poly[i], poly[i + 1]
            x0, x1 = sorted((a[0], b[0]))
            z0, z1 = sorted((a[1], b[1]))
            pad = 30.0
            i0 = max(0, int((x0 - pad + HALF) / CELL))
            i1 = min(N, int((x1 + pad + HALF) / CELL) + 2)
            k0 = max(0, int((z0 - pad + HALF) / CELL))
            k1 = min(N, int((z1 + pad + HALF) / CELL) + 2)
            sx = X[k0:k1, i0:i1]
            sz = Z[k0:k1, i0:i1]
            ab = b - a
            L2 = max(float(ab @ ab), 1e-6)
            t = np.clip(((sx - a[0]) * ab[0] + (sz - a[1]) * ab[1]) / L2, 0, 1)
            d = np.hypot(sx - (a[0] + t * ab[0]), sz - (a[1] + t * ab[1]))
            sub = best[k0:k1, i0:i1]
            np.minimum(sub, d, out=sub)
    return best


# ----------------------------------------------------------------------------- preview
def write_preview(h, splat, sites, props, polys, path):
    from PIL import Image, ImageDraw
    gz, gx = np.gradient(h, CELL)
    light = np.array([-0.5, 0.7, -0.5])
    light /= np.linalg.norm(light)
    nrm = np.stack([-gx, np.ones_like(h), -gz], axis=-1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    shade = np.clip(nrm @ light, 0, 1) * 0.75 + 0.25
    cols = np.array([[0.36, 0.52, 0.25], [0.48, 0.45, 0.42], [0.78, 0.68, 0.48], [0.22, 0.2, 0.2]])
    base = (splat.astype(float) / 255.0) @ cols
    snow = smoothstep(190.0, 240.0, h)[..., None]
    base = base * (1 - snow) + snow * 0.92
    water = (h < WATER_LEVEL)[..., None]
    rgb = base * shade[..., None]
    rgb = np.where(water, np.array([0.18, 0.36, 0.5]) * (0.7 + 0.3 * smoothstep(-7, 0, h))[..., None], rgb)
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1, :, :])   # +z up in the image
    d = ImageDraw.Draw(img)

    def px(x, z):
        return ((x + HALF) / CELL, (HALF - z) / CELL)

    for p in props:
        if p["mesh"].startswith("tree"):
            x, _, z = p["pos"]
            u, v = px(x, z)
            d.point((u, v), fill=(20, 60, 20))
    for poly in polys:
        d.line([px(x, z) for x, z in poly], fill=(200, 180, 140), width=2)
    for s in sites:
        u, v = px(s["pos"][0], s["pos"][2])
        c = {"hub": (255, 255, 255), "water": (80, 160, 255), "earth": (200, 140, 60), "fire": (255, 70, 40), "air": (220, 240, 255)}[s["region"]]
        d.ellipse((u - 6, v - 6, u + 6, v + 6), outline=(0, 0, 0), fill=c)
    for sid, _, (x, z) in SHRINES:
        u, v = px(x, z)
        d.rectangle((u - 4, v - 4, u + 4, v + 4), outline=(0, 0, 0), fill=(255, 215, 0))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


# ----------------------------------------------------------------------------- main
def main():
    h = build_height()
    h, polys = carve_paths(h)
    h, sites = place_sites(h)
    h = h.astype(np.float32)
    pd = path_distance(polys)
    splat, slope = build_splat(h, smoothstep(6.0, 2.0, pd))
    props = scatter_props(h, splat, slope, sites, pd)
    shrines = []
    for sid, name, (x, z) in SHRINES:
        shrines.append({"id": sid, "name": name, "pos": [x, round(sample(h, x, z), 3), z]})
    solids = solids_for(h, sites)
    world = {
        "version": 1,
        "generator": "unreal/Tools/openworld/gen_world.py",
        "terrain": {"nx": N, "nz": N, "x0": -HALF, "z0": -HALF, "cell": CELL, "heights": "heights.f32", "splat": "splat.rgba"},
        "water": {"level": WATER_LEVEL, "wade_depth": WADE_DEPTH},
        "regions": [{"id": k, "name": v["name"], "center": list(v["center"]), "radius": v["radius"]} for k, v in REGIONS.items()],
        "spawn": {"pos": [0.0, 20.0, 30.0], "facing": math.pi},
        "sites": sites,
        "shrines": shrines,
        "solids": solids,
        "metal": metal_rects(sites),
        "paths": [[[round(float(x), 1), round(float(z), 1)] for x, z in poly[::4]] for poly in polys],
        "props": props,
    }
    os.makedirs(OUT_DATA, exist_ok=True)
    h.astype("<f4").tofile(os.path.join(OUT_DATA, "heights.f32"))
    splat.tofile(os.path.join(OUT_DATA, "splat.rgba"))
    with open(os.path.join(OUT_DATA, "world.json"), "w") as f:
        json.dump(world, f, separators=(",", ":"))
    write_preview(h, splat, sites, props, polys, os.path.join(OUT_PREVIEW, "map.png"))
    trees = sum(1 for p in props if p["mesh"].startswith("tree"))
    print(f"world {N}x{N} @ {CELL} m: h {h.min():.1f}..{h.max():.1f} m, {len(sites)} sites, {len(shrines)} shrines, "
          f"{len(solids)} solids, {trees} trees, {len(props) - trees} rocks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
