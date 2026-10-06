"""Scenery outside the walls (pure numpy via meshkit): terrain, mountain layers, halls, pavilions, a tower, trees, rocks, sky dome, pennants.

All of it is merged into a handful of static meshes at world positions (pivot = origin) so the whole backdrop costs ~20 draw calls.
Everything is deterministic (fixed seeds).  The camera sits inside the 3.5 m walls, so what the player sees of it is: sky, mountain
ridges, roofs / tree crowns above the wall caps and the pennants - build effort goes into silhouettes, colour and atmospheric depth.

Original architecture: a quiet mountain training courtyard (plaster + timber + grey clay tile) - no franchise architecture.
"""
from __future__ import annotations

import math

import numpy as np

import env_spec as ES
from meshkit import Mesh, rot_y, normal_of

TAU = 2 * math.pi


def slots(*names):
    return [(n, ES.TILE[n]) for n in names]


# ------------------------------------------------------------------------------------------------------------------ noise
class Noise2:
    """Seeded value noise on a lattice (not periodic) with smooth interpolation, and a periodic 1-D version for ridge lines."""

    def __init__(self, seed, size=256):
        rng = np.random.default_rng(seed)
        self.n = size
        self.g = rng.random((size, size))
        self.p1 = rng.random(size)

    def v2(self, x, z):
        x = np.asarray(x, float)
        z = np.asarray(z, float)
        xi, zi = np.floor(x).astype(int), np.floor(z).astype(int)
        fx, fz = x - xi, z - zi
        fx, fz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
        n = self.n
        a = self.g[xi % n, zi % n]
        b = self.g[(xi + 1) % n, zi % n]
        c = self.g[xi % n, (zi + 1) % n]
        d = self.g[(xi + 1) % n, (zi + 1) % n]
        return (a * (1 - fx) + b * fx) * (1 - fz) + (c * (1 - fx) + d * fx) * fz

    def fbm(self, x, z, octaves=4, lac=2.0, gain=0.5):
        s = 0.0
        a = 1.0
        norm = 0.0
        for o in range(octaves):
            s = s + a * self.v2(x * (lac ** o) + 17.3 * o, z * (lac ** o) - 9.1 * o)
            norm += a
            a *= gain
        return s / norm

    def periodic(self, theta, freq, octaves=4, gain=0.55):
        """1-D noise periodic in theta (0..2pi): sum of cosines with seeded phases at integer multiples of freq."""
        rng = np.random.default_rng(int(self.p1[0] * 1e6))
        s = 0.0
        a = 1.0
        norm = 0.0
        for o in range(octaves):
            k = int(freq * (2 ** o))
            ph = rng.uniform(0, TAU, 3)
            for j in range(3):
                s = s + a * np.cos(theta * (k + j) + ph[j]) / 3.0
            norm += a
            a *= gain
        return 0.5 + 0.5 * s / norm * 1.4


NZ = Noise2(2024)


def ground_h(x, z):
    """Terrain height (m): flat courtyard surroundings, rolling foothills beyond ~90 m."""
    r = np.sqrt(np.asarray(x, float) ** 2 + np.asarray(z, float) ** 2)
    rise = np.clip((r - 90.0) / 260.0, 0.0, 1.0)
    rise = rise * rise * (3 - 2 * rise)
    n = NZ.fbm(np.asarray(x, float) / 55.0, np.asarray(z, float) / 55.0, 4)
    flat = np.clip((r - 30.0) / 40.0, 0.0, 1.0)
    return -0.06 + flat * 0.9 * (n - 0.5) + rise * (6.0 + 34.0 * n)


# ------------------------------------------------------------------------------------------------------------------ ground
def build_ground(A):
    m = Mesh("SM_Env_Ground", slots("Ground"))
    radii = [0, 20, 30, 40, 52, 66, 82, 100, 125, 155, 190, 230, 280, 340, 420]
    sectors = 72
    def vcol(P):
        r = np.sqrt(P[:, 0] ** 2 + P[:, 2] ** 2)
        grass = np.clip(NZ.fbm(P[:, 0] / 30.0, P[:, 2] / 30.0, 3) * 1.8 - 0.55, 0, 1)
        fade = np.clip((r - 60.0) / 300.0, 0, 1)
        return np.stack([grass, fade, np.zeros(len(P)), np.ones(len(P))], -1)
    def pt(i, k):
        r = radii[i]
        a = TAU * (k % sectors) / sectors
        x, z = r * math.cos(a), r * math.sin(a)
        return (x, float(ground_h(x, z)), z)
    for i in range(len(radii) - 1):
        for k in range(sectors):
            A_, B_, C_, D_ = pt(i, k), pt(i, k + 1), pt(i + 1, k + 1), pt(i + 1, k)
            q = [A_, D_, C_] if i == 0 else [A_, D_, C_, B_]
            if normal_of(np.array(q))[1] < 0:           # terrain faces up
                q = q[::-1]
            m.face(q, "Ground", col=vcol, smooth=1, tile=6.0)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ ridges
RIDGE_LAYERS = [
    # radius m, base y, height min, height max, colour (sRGB-ish), noise freq, haze
    dict(r=330.0, y0=-20.0, h0=34.0, h1=62.0, col=(0.20, 0.27, 0.25), freq=6, haze=0.42),
    dict(r=520.0, y0=-30.0, h0=70.0, h1=125.0, col=(0.36, 0.43, 0.48), freq=5, haze=0.60),
    dict(r=800.0, y0=-40.0, h0=130.0, h1=230.0, col=(0.58, 0.64, 0.72), freq=4, haze=0.78),
]
HAZE = np.array([0.86, 0.72, 0.60])         # warm horizon haze (matches the fog colour used by the level builder)


def build_ridges(A):
    m = Mesh("SM_Env_Ridges", slots("Ridge"))
    nz = Noise2(77)
    segs = 480
    for li, L in enumerate(RIDGE_LAYERS):
        theta = np.linspace(0, TAU, segs + 1)
        n1 = nz.periodic(theta + li * 1.3, L["freq"], 6, 0.62)
        n1 = (n1 - n1.min()) / (np.ptp(n1) + 1e-9)
        rid = 1.0 - np.abs(2.0 * n1 - 1.0)                          # ridged: sharp crests, wide saddles
        n1 = np.clip(0.30 * n1 + 0.70 * rid ** 1.25, 0, 1)
        # hero massif to the north (sim -z  <=>  theta = -pi/2)
        hero = np.exp(-(((theta + math.pi / 2 + 0.35 + 0.15 * li + math.pi) % TAU - math.pi) / 0.32) ** 2) if li in (1, 2) else 0
        H = L["h0"] + (L["h1"] - L["h0"]) * np.clip(n1 * 1.15, 0, 1) + (L["h1"] * 0.55 * hero if li else 0.0)
        for k in range(segs):
            ts = [theta[k], theta[k + 1]]
            Hs = [H[k], H[k + 1]]
            rows = [(L["y0"], 0.0), (0.0, 0.25), (1.0, 1.0)]       # (y fraction handled below), haze weight
            def vert(t, h, row):
                x, z = L["r"] * math.cos(t), L["r"] * math.sin(t)
                if row == 0:
                    y, haze = L["y0"], 1.0
                elif row == 1:
                    y, haze = 0.35 * h, 0.55 + 0.45 * L["haze"]
                else:
                    y, haze = h, L["haze"]
                shade = 0.82 + 0.34 * float(nz.v2(t * 13.0 + li * 5.0, row * 1.7))      # gullies / light faces
                base = np.array(L["col"]) * shade
                if li >= 1 and row >= 1:
                    snow = float(np.clip(((h - L["h0"]) / (L["h1"] * 1.45 - L["h0"]) - 0.5) * 3.2, 0, 1)) * (1.0 if row == 2 else 0.35)
                    base = base * (1 - 0.85 * snow) + np.array([0.93, 0.92, 0.9]) * 0.85 * snow
                c = base * (1 - haze) + HAZE * haze if row != 2 else base * (1 - 0.35 * L["haze"]) + HAZE * 0.35 * L["haze"]
                return (x, y, z), (c[0], c[1], c[2], 1.0)
            # strips: row0-row1 and row1-row2
            for (ra, rb) in ((0, 1), (1, 2)):
                (a0, c0), (a1, c1) = vert(ts[0], Hs[0], ra), vert(ts[1], Hs[1], ra)
                (b0, d0), (b1, d1) = vert(ts[0], Hs[0], rb), vert(ts[1], Hs[1], rb)
                # facing the centre: order so the normal points inward (toward the origin)
                pts = [a0, a1, b1, b0]
                cols = np.array([c0, c1, d1, d0])
                f = normal_of(np.array(pts))
                cen = np.mean(np.array(pts), 0)
                if np.dot(f, -cen) < 0:
                    pts, cols = pts[::-1], cols[::-1]
                uv = np.array([[p[0] / 2000.0 + 0.5, p[1] / 400.0] for p in pts])
                m.face(pts, "Ridge", uv=uv, col=cols)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ sky
def build_sky(A, radius=1500.0):
    m = Mesh("SM_Env_Sky", slots("Sky"))
    segs, rings = 32, 20
    def P(j, k):
        t = -math.pi / 2 + math.pi * j / rings
        a = TAU * (k % segs) / segs
        return (radius * math.cos(t) * math.cos(a), radius * math.sin(t), radius * math.cos(t) * math.sin(a))
    for j in range(rings):
        for k in range(segs):
            q = [P(j, k), P(j, k + 1), P(j + 1, k + 1), P(j + 1, k)]
            if j == 0:
                q = [q[0], q[2], q[3]]
            elif j == rings - 1:
                q = [q[0], q[1], q[3]]
            pts = np.array(q)
            f = normal_of(pts)
            if np.dot(f, -pts.mean(0)) < 0:
                q = q[::-1]
            m.face(q, "Sky", uv=np.zeros((len(q), 2)))
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ buildings
def hip_roof(m, cx, cz, yaw, w, d, y0, rise, overhang, mat="RoofTile", soffit="Timber", ridge=True):
    """Hip roof over a w (x) x d (z) plan, local x along the long side.  Faces point outward; also a soffit and a ridge cap."""
    W, D = w / 2 + overhang, d / 2 + overhang
    rl = max((W - D), 0.35)
    yr = y0 + rise
    with m.xf(rot_y(yaw), (cx, 0.0, cz)):
        c = [(-W, y0, -D), (W, y0, -D), (W, y0, D), (-W, y0, D)]
        m.face([c[3], c[2], (rl, yr, 0.0), (-rl, yr, 0.0)], mat)                 # +z slope
        m.face([c[1], c[0], (-rl, yr, 0.0), (rl, yr, 0.0)], mat)                 # -z slope
        m.face([c[2], c[1], (rl, yr, 0.0)], mat)                                 # +x hip
        m.face([c[0], c[3], (-rl, yr, 0.0)], mat)                                # -x hip
        m.face([c[0], c[1], c[2], c[3]], soffit)                                 # underside (faces down)
        if ridge:
            m.box((-rl - 0.25, yr - 0.02, -0.16), (rl + 0.25, yr + 0.2, 0.16), mat, skip=("bottom",))


def build_halls(A):
    m = Mesh("SM_Env_Halls", slots("Plaster", "RoofTile", "Timber", "Glow", "StoneWall"))
    rng = np.random.default_rng(4242)
    centers = []
    n = 15
    for i in range(n):
        for _try in range(30):
            ang = TAU * (i + rng.uniform(-0.25, 0.25)) / n
            dist = rng.uniform(34.0, 66.0)
            cx, cz = dist * math.cos(ang), dist * math.sin(ang)
            if abs(cx) < 22 and abs(cz) < 22:
                continue
            if all((cx - px) ** 2 + (cz - pz) ** 2 > 18.0 ** 2 for px, pz in centers):
                break
        centers.append((cx, cz))
        w = rng.uniform(10.0, 16.0)
        d = rng.uniform(6.5, 9.5)
        h = rng.uniform(4.2, 6.2)
        rise = rng.uniform(2.4, 3.6)
        # local +z is the front; rotate so +z points at the arena centre
        yaw = rng.uniform(-0.12, 0.12) + math.atan2(-cx, -cz)
        base_y = float(ground_h(cx, cz))
        gr = lambda P: np.stack([np.ones(len(P)), np.clip(0.8 - (P[:, 1] - base_y) / 3.5, 0, 0.8), np.zeros(len(P)), np.ones(len(P))], -1)
        with m.xf(rot_y(yaw), (cx, base_y, cz)):
            m.box((-w / 2, 0.0, -d / 2), (w / 2, 0.7, d / 2), "StoneWall", skip=("bottom",), col=gr)
            m.box((-w / 2 + 0.02, 0.7, -d / 2 + 0.02), (w / 2 - 0.02, h, d / 2 - 0.02), "Plaster", skip=("bottom",), col=gr)
            # timber corner posts + eaves beam
            for sx in (-1, 1):
                for sz in (-1, 1):
                    m.box((sx * (w / 2 - 0.2) - 0.18, 0.7, sz * (d / 2 - 0.2) - 0.18), (sx * (w / 2 - 0.2) + 0.18, h, sz * (d / 2 - 0.2) + 0.18), "Timber",
                          uvrot={k: True for k in ("px", "nx", "pz", "nz")}, skip=("bottom", "top"))
            m.box((-w / 2 - 0.05, h - 0.3, -d / 2 - 0.05), (w / 2 + 0.05, h, d / 2 + 0.05), "Timber", skip=("top", "bottom"))
            # lit windows (and door) on the front (+z) and back faces
            k = int(w // 3.0)
            for side in (1, -1):
                for wi in range(k):
                    wx = (wi - (k - 1) / 2) * (w / (k + 0.4))
                    glow = rng.uniform(0.0, 1.0) < 0.85
                    z0 = side * (d / 2 + 0.03)
                    pts = [(wx - 0.5, 1.8, z0), (wx + 0.5, 1.8, z0), (wx + 0.5, 3.4, z0), (wx - 0.5, 3.4, z0)]
                    if side < 0:
                        pts = pts[::-1]
                    m.face(pts, "Glow" if glow else "Timber")
        hip_roof(m, cx, cz, yaw, w, d, base_y + h, rise, 1.1)
    return m, (0.0, 0.0, 0.0)


def pavilion(m, cx, cz, yaw=0.0, size=8.0):
    """Open timber pavilion on a low stone platform with a hip roof and hanging lanterns."""
    by = float(ground_h(cx, cz))
    s = size / 2
    with m.xf(rot_y(yaw), (cx, by, cz)):
        m.box((-s - 0.8, 0.0, -s - 0.8), (s + 0.8, 0.45, s + 0.8), "StoneWall", skip=("bottom", "top"))
        m.chamfer_box((-s - 0.8, 0.45, -s - 0.8), (s + 0.8, 0.8, s + 0.8), 0.05, "StoneCap", skip=("bottom",))
        for sx in (-1, 1):
            for sz in (-1, 1):
                x, z = sx * (s - 0.2), sz * (s - 0.2)
                m.box((x - 0.2, 0.8, z - 0.2), (x + 0.2, 4.2, z + 0.2), "Timber", uvrot={k: True for k in ("px", "nx", "pz", "nz")}, skip=("bottom", "top"))
                m.box((x - 0.3, 0.8, z - 0.3), (x + 0.3, 1.1, z + 0.3), "StoneCap", skip=("bottom",))
        for sgn in (-1, 1):
            m.box((-s - 0.1, 4.0, sgn * (s - 0.2) - 0.18), (s + 0.1, 4.45, sgn * (s - 0.2) + 0.18), "Timber", skip=("top",))
            m.box((sgn * (s - 0.2) - 0.18, 4.0, -s - 0.1), (sgn * (s - 0.2) + 0.18, 4.45, s + 0.1), "Timber", skip=("top",))
        m.box((-s + 0.3, 3.0, -0.05), (s - 0.3, 3.1, 0.05), "Timber")
    hip_roof(m, cx, cz, yaw, size, size, by + 4.45, 2.8, 1.5)
    # lanterns
    with m.xf(rot_y(yaw), (cx, by, cz)):
        for sx in (-1, 1):
            for sz in (-1, 1):
                x, z = sx * (s - 0.2), sz * (s - 0.2)
                m.lathe([(0.0, 3.05), (0.12, 3.12), (0.16, 3.4), (0.12, 3.68), (0.0, 3.75)], (x * 0.8, 0, z * 0.8), "Glow", segs=8)
        m.lathe([(0.0, 4.45), (0.1, 4.46), (0.0, 4.5)], (0, 0, 0), "Timber", segs=6)
    # bronze finial
    m.lathe([(0.12, by + 7.2), (0.06, by + 7.6), (0.0, by + 8.1)], (cx, 0, cz), "Timber", segs=6)


def build_pavilions(A):
    m = Mesh("SM_Env_Pavilions", slots("Plaster", "RoofTile", "Timber", "Glow", "StoneWall", "StoneCap"))
    pavilion(m, -26.0, -44.0, 0.2, 9.0)
    pavilion(m, 31.0, 38.0, -0.3, 8.0)
    return m, (0.0, 0.0, 0.0)


def build_tower(A):
    """A three-tier timber and plaster tower (bell tower), square plan, tiled hip roofs, bronze finial."""
    m = Mesh("SM_Env_Tower", slots("Plaster", "RoofTile", "Timber", "Glow", "StoneWall", "StoneCap", "Bronze"))
    cx, cz = 34.0, -66.0
    by = float(ground_h(cx, cz))
    yaw = math.atan2(-cx, -cz)
    y = 0.0
    with m.xf(rot_y(yaw), (cx, by, cz)):
        m.chamfer_box((-4.4, 0.0, -4.4), (4.4, 2.4, 4.4), 0.1, "StoneWall", top_mat="StoneCap", chamfer_mat="StoneCap", skip=("bottom",))
    y = by + 2.4
    sizes = [3.6, 3.0, 2.4]
    for ti, hs in enumerate(sizes):
        wall_h = 3.6
        with m.xf(rot_y(yaw), (cx, 0.0, cz)):
            m.box((-hs, y, -hs), (hs, y + wall_h, hs), "Plaster", skip=("bottom",))
            for sx in (-1, 1):
                for sz in (-1, 1):
                    m.box((sx * hs - 0.2, y, sz * hs - 0.2), (sx * hs + 0.2, y + wall_h, sz * hs + 0.2), "Timber",
                          uvrot={k: True for k in ("px", "nx", "pz", "nz")}, skip=("bottom", "top"))
            m.box((-hs - 0.1, y + wall_h - 0.35, -hs - 0.1), (hs + 0.1, y + wall_h, hs + 0.1), "Timber", skip=("bottom", "top"))
            for (fx, fz, nx_, nz_) in ((0, hs + 0.04, 0, 1), (0, -hs - 0.04, 0, -1), (hs + 0.04, 0, 1, 0), (-hs - 0.04, 0, -1, 0)):
                if nz_ != 0:
                    pts = [(-0.7, y + 1.2, fz), (0.7, y + 1.2, fz), (0.7, y + 2.8, fz), (-0.7, y + 2.8, fz)]
                    if nz_ < 0:
                        pts = pts[::-1]
                else:
                    pts = [(fx, y + 1.2, 0.7), (fx, y + 1.2, -0.7), (fx, y + 2.8, -0.7), (fx, y + 2.8, 0.7)]
                    if nx_ < 0:
                        pts = pts[::-1]
                m.face(pts, "Glow")
        hip_roof(m, cx, cz, yaw, hs * 2, hs * 2, y + wall_h, 1.7 if ti < 2 else 2.4, 1.3, ridge=False)
        y += wall_h + 1.7
    with m.xf(rot_y(yaw), (cx, 0.0, cz)):
        m.lathe([(0.25, y - 1.0), (0.18, y + 0.8), (0.1, y + 2.2), (0.0, y + 3.4)], (0, 0, 0), "Bronze", segs=8)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ trees
ATLAS = {"broad": (0.0, 0.5), "pine": (0.5, 0.5), "cypress": (0.0, 0.0), "tuft": (0.5, 0.0)}      # (u0, v0) of each 0.5 x 0.5 cell


def _card(m, p0, p1, up, width_dir_len, atlas, col, flip=False):
    """A quad from p0 to p1 along its length, `up` = unit direction of the width axis; atlas cell UV (length along u)."""
    (a0, b0) = ATLAS[atlas]
    w = np.asarray(up) * width_dir_len * 0.5
    pts = [np.asarray(p0) - w, np.asarray(p1) - w, np.asarray(p1) + w, np.asarray(p0) + w]
    uv = np.array([[a0, b0], [a0 + 0.5, b0], [a0 + 0.5, b0 + 0.5], [a0, b0 + 0.5]])
    cols = np.array([col[0], col[1], col[2], col[3]])
    if flip:
        pts = pts[::-1]
        uv = uv[::-1]
        cols = cols[::-1]
    m.face([tuple(p) for p in pts], "Foliage", uv=uv, col=cols)


def build_trees(A):
    m = Mesh("SM_Env_Trees", slots("Bark", "Foliage"))
    rng = np.random.default_rng(777)
    buildings = [(-26.0, -44.0), (31.0, 38.0), (34.0, -66.0)]
    placed = []
    trees = []
    tries = 0
    while len(trees) < 90 and tries < 2000:
        tries += 1
        ang = rng.uniform(0, TAU)
        dist = rng.uniform(21.0, 82.0)
        x, z = dist * math.cos(ang), dist * math.sin(ang)
        if abs(x) < 20.5 and abs(z) < 20.5:
            continue
        if any((x - bx) ** 2 + (z - bz) ** 2 < 10.0 ** 2 for bx, bz in buildings):
            continue
        if any((x - px) ** 2 + (z - pz) ** 2 < 4.2 ** 2 for px, pz in placed):
            continue
        placed.append((x, z))
        kind = rng.choice(["pine", "broad", "cypress"], p=[0.45, 0.35, 0.20])
        trees.append((x, z, kind))
    for i, (x, z, kind) in enumerate(trees):
        by = float(ground_h(x, z))
        tint = rng.uniform(0.35, 0.65)
        yaw0 = rng.uniform(0, TAU)
        if kind == "pine":
            H = rng.uniform(9.0, 13.5)
            m.lathe([(0.34, by - 0.3), (0.24, by + H * 0.35), (0.12, by + H * 0.8), (0.0, by + H)], (x, 0, z), "Bark", segs=7, smooth=1)
            tiers = 10
            for t in range(tiers):
                f = t / (tiers - 1)
                ty = by + 2.2 + f * (H - 3.0)
                L = (1.0 - f) * 3.6 + 1.3
                ncards = 8 if t < tiers - 2 else 5
                for c in range(ncards):
                    a = yaw0 + TAU * (c + 0.5 * (t % 2)) / ncards + rng.uniform(-0.1, 0.1)
                    dirv = np.array([math.cos(a), 0.0, math.sin(a)])
                    side = np.array([-math.sin(a), 0.0, math.cos(a)])
                    droop = L * 0.28
                    p0 = np.array([x, ty, z]) + dirv * 0.25
                    p1 = np.array([x, ty - droop, z]) + dirv * L
                    wid = L * 1.05
                    g0, g1 = f, min(1.0, f + 0.12)
                    cols = [(tint, g0, 0.0 + f, 1), (tint, g0 - 0.05, f, 1), (tint, g1, f + 0.1, 1), (tint, g1, f + 0.1, 1)]
                    cols = [(tint, 0.15 + 0.7 * f, min(1.0, f * 0.8 + 0.1), 1.0)] * 4
                    cols[1] = (tint, 0.05 + 0.7 * f, min(1.0, f * 0.8 + 0.2), 1.0)
                    _card(m, p0, p1, side, wid, "pine", cols)
                    _card(m, p0, p1, side, wid, "pine", cols, flip=True)
        elif kind == "cypress":
            H = rng.uniform(7.0, 11.0)
            m.lathe([(0.22, by - 0.2), (0.12, by + 2.0), (0.04, by + 3.5)], (x, 0, z), "Bark", segs=6, smooth=1)
            for c in range(3):
                a = yaw0 + math.pi * c / 3
                dirv = np.array([math.cos(a), 0.0, math.sin(a)])
                p0 = np.array([x, by + 1.2, z])
                p1 = np.array([x, by + 1.2 + H, z])
                cols = [(tint, 0.05, 0.0, 1), (tint, 0.9, 1.0, 1), (tint, 0.9, 1.0, 1), (tint, 0.05, 0.0, 1)]
                # vertical card: width along dirv
                (a0, b0) = ATLAS["cypress"]
                w = dirv * (H * 0.2)
                pts = [p0 - w, p0 + w, p1 + w * 0.2, p1 - w * 0.2]
                uv = np.array([[a0, b0], [a0 + 0.5, b0], [a0 + 0.5 - 0.15, b0 + 0.5], [a0 + 0.15, b0 + 0.5]])
                for flip in (False, True):
                    P_ = pts[::-1] if flip else pts
                    U_ = uv[::-1] if flip else uv
                    C_ = cols[::-1] if flip else cols
                    m.face([tuple(p) for p in P_], "Foliage", uv=U_, col=np.array(C_))
        else:
            H = rng.uniform(6.0, 9.0)
            th = H * 0.42
            m.lathe([(0.3, by - 0.3), (0.2, by + th * 0.6), (0.14, by + th)], (x, 0, z), "Bark", segs=7, smooth=1)
            R = rng.uniform(3.0, 4.4)
            cy = by + th + R * 0.55
            (a0, b0) = ATLAS["broad"]
            for c in range(4):                                  # crossed vertical cards
                a = yaw0 + math.pi * c / 4
                dirv = np.array([math.cos(a), 0.0, math.sin(a)])
                p0 = np.array([x, cy - R * 0.9, z]) - dirv * R
                p1 = np.array([x, cy - R * 0.9, z]) + dirv * R
                up = np.array([0.0, 1.0, 0.0])
                cols = [(tint, 0.1, 0.4, 1), (tint, 0.1, 0.4, 1), (tint, 1.0, 1.0, 1), (tint, 1.0, 1.0, 1)]
                _card(m, p0, p1, up, R * 2.0 * 0.95, "broad", cols)
                _card(m, p0, p1, up, R * 2.0 * 0.95, "broad", cols, flip=True)
            for c in range(2):                                  # a pair of tilted crown discs
                a = yaw0 + 0.7 * c
                dirv = np.array([math.cos(a), 0.0, math.sin(a)])
                side = np.array([-math.sin(a), 0.0, math.cos(a)])
                p0 = np.array([x, cy + R * (0.2 + 0.25 * c), z]) - dirv * R * 0.95
                p1 = np.array([x, cy + R * (0.2 + 0.25 * c), z]) + dirv * R * 0.95
                cols = [(tint, 0.9, 1.0, 1)] * 4
                _card(m, p0, p1, side, R * 1.9, "broad", cols)
                _card(m, p0, p1, side, R * 1.9, "broad", cols, flip=True)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ rocks
def build_rocks(A):
    m = Mesh("SM_Env_Rocks", slots("Rock"))
    rng = np.random.default_rng(909)
    spots = []
    for i in range(26):
        ang = rng.uniform(0, TAU)
        dist = rng.uniform(24.0, 110.0)
        spots.append((dist * math.cos(ang), dist * math.sin(ang), rng.uniform(0.7, 2.4) * (2.4 if i % 7 == 0 else 1.0)))
    for (x, z, s) in spots:
        if abs(x) < 20 and abs(z) < 20:
            continue
        by = float(ground_h(x, z))
        n_r = int(rng.integers(2, 5))
        for k in range(n_r):
            ox, oz = rng.normal(0, s * 0.9, 2)
            rs = s * rng.uniform(0.5, 1.0)
            m.sphere((x + ox, by + rs * 0.25, z + oz), (rs * 1.3, rs * 0.8, rs * 1.0), "Rock", segs=9, rings=5, smooth=0,
                     seed=int(rng.integers(0, 1 << 30)), jitter=0.22, uvfn=None)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ pennants
def build_pennants(A):
    """Tall poles with long cloth pennants (sway via vertex colour R in the Banner material) + the pole itself."""
    m = Mesh("SM_Env_Pennants", slots("Timber", "Banner"))
    rng = np.random.default_rng(321)
    poles = [(-12.0, -24.5), (14.0, -26.0), (26.0, 4.0), (-28.0, 14.0), (8.0, 27.0), (-30.0, -12.0)]
    for pi, (x, z) in enumerate(poles):
        by = float(ground_h(x, z))
        H = 10.5 + (pi % 3) * 0.8
        m.lathe([(0.14, by - 0.2), (0.1, by + H * 0.7), (0.07, by + H)], (x, 0, z), "Timber", segs=7, smooth=1, uvfn=None)
        m.sphere((x, by + H + 0.1, z), (0.12, 0.12, 0.12), "Timber", segs=6, rings=4)
        # pennant: from the pole top, flying along +wind (a fixed bearing), 5.5 m long, tapering
        wind = np.array([math.cos(0.7), 0.0, math.sin(0.7)])
        side = np.array([0.0, 1.0, 0.0])
        L = 5.5
        nl, nw = 12, 1
        col_idx = pi % 4
        for i in range(nl):
            f0, f1 = i / nl, (i + 1) / nl
            def pt(f, t):
                p = np.array([x, by + H - 0.35, z]) + wind * (0.15 + f * L) - np.array([0.0, 0.0, 0.0])
                width = 0.85 * (1.0 - 0.85 * f)
                p = p + side * (0.5 - t) * width * 1.0
                p = p + np.array([0.0, -0.18 * f, 0.0])
                return tuple(p)
            q = [pt(f0, 1.0), pt(f0, 0.0), pt(f1, 0.0), pt(f1, 1.0)]
            n = normal_of(np.array(q))
            # plain cloth band of the atlas (between glyph and seal), colour per pole
            u0 = (col_idx + 0.18) / 4.0
            u1 = (col_idx + 0.82) / 4.0
            uv = np.array([[u0, 0.48], [u0, 0.52], [u1, 0.52], [u1, 0.48]])
            cols = np.array([[f0, 1, 0, 1], [f0, 1, 0, 1], [f1, 1, 0, 1], [f1, 1, 0, 1]])
            m.face(q, "Banner", uv=uv, col=cols)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------------ gate + yard
def build_gate(A):
    """A timber ceremonial gate (three bays, tiled roofs, stone drum bases, a plaque) standing in front of the north hills."""
    m = Mesh("SM_Env_Gate", slots("Timber", "RoofTile", "StoneWall", "StoneCap", "Bronze", "Plaster"))
    cx, cz = 12.0, -50.0
    by = float(ground_h(cx, cz))
    yaw = math.atan2(-cx, -cz)
    xs = [-4.8, -1.6, 1.6, 4.8]
    heights = [6.6, 7.9, 7.9, 6.6]
    with m.xf(rot_y(yaw), (cx, by, cz)):
        for x, h in zip(xs, heights):
            m.lathe([(0.55, 0.0), (0.55, 0.9), (0.5, 1.0), (0.0, 1.0)], (x, 0, 0), "StoneWall", segs=10, smooth=0)          # drum base
            m.box((x - 0.26, 1.0, -0.26), (x + 0.26, h, 0.26), "Timber", uvrot={k: True for k in ("px", "nx", "pz", "nz")}, skip=("bottom", "top"))
        # beams: main tie beam, lower beam, short rafters
        m.box((xs[0] - 0.6, heights[1] - 0.9, -0.34), (xs[3] + 0.6, heights[1] - 0.2, 0.34), "Timber", skip=("top",))
        m.box((xs[0] - 0.3, 5.1, -0.22), (xs[3] + 0.3, 5.6, 0.22), "Timber", skip=("top", "bottom"))
        # plaque
        m.box((-1.3, 5.7, -0.1), (1.3, 6.9, 0.1), "Timber")
        m.box((-1.2, 5.8, 0.1), (1.2, 6.8, 0.12), "Bronze")
        m.box((-1.1, 5.9, 0.12), (1.1, 6.7, 0.13), "Plaster")
    # roofs (tiles): wide central roof, two lower side roofs
    hip_roof(m, cx, cz, yaw, 5.6, 2.2, by + heights[1] - 0.2, 1.7, 1.2, ridge=True)
    for sx in (-1, 1):
        ox = sx * 4.8
        px = cx + math.cos(yaw) * ox
        pz = cz - math.sin(yaw) * ox
        hip_roof(m, px, pz, yaw, 3.4, 2.0, by + heights[0] - 0.2, 1.3, 1.0, ridge=True)
    return m, (0.0, 0.0, 0.0)


def build_yard(A):
    """Training yard props outside the east wall: wooden training posts with straw wraps, a stone-lock rack (no weapons), a bench."""
    m = Mesh("SM_Env_Yard", slots("Timber", "Bark", "StoneWall", "Iron", "Plaster"))
    rng = np.random.default_rng(1717)
    x0, z0 = 24.0, -9.0
    for i in range(6):
        px = x0 + (i % 3) * 2.2 + rng.uniform(-0.2, 0.2)
        pz = z0 + (i // 3) * 2.6 + rng.uniform(-0.2, 0.2)
        h = rng.uniform(1.5, 2.1)
        by = float(ground_h(px, pz))
        m.lathe([(0.17, by), (0.15, by + h), (0.0, by + h + 0.02)], (px, 0, pz), "Timber", segs=8, smooth=1)
        for k in range(3):
            y = by + 0.7 + 0.28 * k
            m.lathe([(0.19, y), (0.19, y + 0.2), (0.16, y + 0.22)], (px, 0, pz), "Bark", segs=8, smooth=1)
    # stone-lock rack: two A-frames, a crossbar, four ring locks hanging on it
    rx, rz = x0 + 1.0, z0 + 7.0
    by = float(ground_h(rx, rz))
    for sx in (-1.6, 1.6):
        m.box((rx + sx - 0.07, by, rz - 0.5), (rx + sx + 0.07, by + 1.9, rz - 0.38), "Timber", skip=("bottom",))
        m.box((rx + sx - 0.07, by, rz + 0.38), (rx + sx + 0.07, by + 1.9, rz + 0.5), "Timber", skip=("bottom",))
    m.box((rx - 1.8, by + 1.75, rz - 0.07), (rx + 1.8, by + 1.9, rz + 0.07), "Timber")
    for k in range(4):
        x = rx - 1.2 + 0.8 * k
        m.lathe([(0.2, by + 0.9), (0.2, by + 1.1), (0.05, by + 1.1)], (x, 0, rz), "StoneWall", segs=8, smooth=0)
        m.box((x - 0.01, by + 1.1, rz - 0.01), (x + 0.01, by + 1.75, rz + 0.01), "Iron")
    # bench
    bx, bz = x0 - 3.0, z0 + 7.0
    by = float(ground_h(bx, bz))
    m.box((bx - 1.0, by + 0.42, bz - 0.2), (bx + 1.0, by + 0.5, bz + 0.2), "Timber")
    for sx in (-0.8, 0.8):
        m.box((bx + sx - 0.05, by, bz - 0.15), (bx + sx + 0.05, by + 0.42, bz + 0.15), "Timber", skip=("bottom",))
    return m, (0.0, 0.0, 0.0)


SCENERY_BUILDERS = {
    "SM_Env_Ground": build_ground,
    "SM_Env_Ridges": build_ridges,
    "SM_Env_Sky": build_sky,
    "SM_Env_Halls": build_halls,
    "SM_Env_Pavilions": build_pavilions,
    "SM_Env_Tower": build_tower,
    "SM_Env_Trees": build_trees,
    "SM_Env_Rocks": build_rocks,
    "SM_Env_Pennants": build_pennants,
    "SM_Env_Gate": build_gate,
    "SM_Env_Yard": build_yard,
}
LIGHTMAP = {}          # scenery is Movable: no lightmaps (lit by the stationary sun + the sky light / volumetric lightmap)


def layout_entries(A, info, ent):
    out = []
    for name in SCENERY_BUILDERS:
        if name in info:
            out.append(ent(f"FFScenery_{name[7:]}", name, (0.0, 0.0, 0.0), group="scenery", mobility="movable",
                           extra=dict(hidden_in_shadow_pass=(name in ("SM_Env_Sky", "SM_Env_Ridges")))))
    return out


if __name__ == "__main__":
    A = ES.load_arena()
    tot = 0
    for name, fn in SCENERY_BUILDERS.items():
        mesh, pivot = fn(A)
        st = mesh.stats()
        tot += st["tris"]
        print(f"{name:20s} tris={st['tris']:6d} faces={st['faces']:5d} slots={st['slots']}")
    print("total", tot)
