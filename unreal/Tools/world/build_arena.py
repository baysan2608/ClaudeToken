"""Courtyard arena meshes: exact sim solids dressed as masonry / plaster / timber (pure numpy via meshkit; no Blender needed).

Every function returns a meshkit.Mesh in SIM coordinates (x, y-up, z).  Solids are authored at their real size (from sim.json), their
pivot is the box centre, and nothing of a solid's own geometry leaves its sim box except above-head-height dressing (wall caps, pillar
braziers), exactly like the Godot build ("<= 14 cm relief on faces, bigger things above head height").

    SM_Env_Floor         32 x 32 m flagstone floor with the pool opening (top faces only), world-aligned UV
    SM_Env_Wall{N,S,E,W} boundary walls: stone plinth, plaster body, timber pilasters + tie beam, gabled tile cap (symmetrical faces)
    SM_Env_CoverWall / Terrace / StepBlock / HighLedge / Pillar   the other sim solids (pillar has a brazier)
    SM_Env_PoolBasin     tiled basin floor + lining + coping (water plane is SM_Env_PoolWater)
    SM_Env_MetalPlate    deck plate rectangle
    SM_Env_Banners       12 cloth banners (sway driven by vertex colour R in the material)
    SM_Env_Lanterns      hanging lanterns at the pilasters + corner finials
    SM_Env_Rings         bronze inlay rings (start circles + a thin sparring circle)
"""
from __future__ import annotations

import math

import numpy as np

import env_spec as ES
from meshkit import Mesh, rot_y, normal_of, uv_box

PIER_U = [-12.0, -4.0, 4.0, 12.0]          # pilaster centres along each wall (m from the wall's middle)
BANNER_U = [-8.0, 0.0, 8.0]
BANNER_W, BANNER_H = 0.9, 1.9
BANNER_TOP = 3.06


def slots(*names):
    return [(n, ES.TILE[n]) for n in names]


def grime_col(y0, g0, y1, g1, ao=1.0):
    """Vertex colour: G = grime / damp amount, linearly from (y0 -> g0) to (y1 -> g1) over the world height of each vertex."""
    def f(P):
        t = np.clip((P[:, 1] - y0) / (y1 - y0), 0.0, 1.0)
        g = g0 + (g1 - g0) * t
        return np.stack([np.full(len(P), ao), g, np.zeros(len(P)), np.ones(len(P))], -1)
    return f


def extrude_profile(m, prof, u0, u1, mats, uvrot=False, col=None):
    """Extrude a (d, y) profile along local x (u) from u0 to u1.  prof: points (d, y) in any orientation; mats: one slot name or a list
    per edge.  Faces point outward (decided from the polygon's orientation).  No end caps."""
    prof = [tuple(p) for p in prof]
    area = 0.0
    for i in range(len(prof)):
        (a, b), (c, d) = prof[i], prof[(i + 1) % len(prof)]
        area += a * d - c * b
    if area < 0:
        prof = prof[::-1]
        if isinstance(mats, (list, tuple)):
            mats = list(mats)[::-1]
            mats = mats[1:] + mats[:1]        # edge i of the reversed polygon was edge (n-2-i) of the original
    n = len(prof)
    for i in range(n):
        (d0, y0), (d1, y1) = prof[i], prof[(i + 1) % n]
        e = np.array([d1 - d0, y1 - y0])
        if np.linalg.norm(e) < 1e-9:
            continue
        n2 = np.array([e[1], -e[0]])
        n2 /= np.linalg.norm(n2)
        n3 = np.array([0.0, n2[1], n2[0]])               # local (u, y, d)
        A, B, C, D = (u0, y0, d0), (u1, y0, d0), (u1, y1, d1), (u0, y1, d1)
        pts = [A, B, C, D]
        # orient: transform-independent test in local space
        if np.dot(normal_of(np.array(pts, float)), n3) < 0:
            pts = pts[::-1]
        mat = mats[i] if isinstance(mats, (list, tuple)) else mats
        m.face(pts, mat, rot=uvrot, col=col)


# --------------------------------------------------------------------------------------------------------------- walls
WALL_XF = {
    "N": (np.eye(3), np.array([0.0, 0.0, -16.5])),
    "S": (rot_y(math.pi), np.array([0.0, 0.0, 16.5])),
    "W": (rot_y(math.pi / 2), np.array([-16.5, 0.0, 0.0])),
    "E": (rot_y(-math.pi / 2), np.array([16.5, 0.0, 0.0])),
}


def wall_local_frame(face):
    return WALL_XF[face]


def build_wall(face, half=16.0, top=3.5):
    """Dressed boundary wall.  Local frame (u along the wall, y up, d inward); 1 m thick, symmetrical on both faces."""
    L = 2 * (half + 1.0) if face in "NS" else 2 * half
    h = L / 2
    m = Mesh(f"SM_Env_Wall{face}", slots("StoneWall", "StoneCap", "Plaster", "Timber", "RoofTile", "Iron"))
    R, t = wall_local_frame(face)
    with m.xf(R, t):
        # plinth: dressed stone with a chamfered shoulder
        plinth = [(-0.54, 0.0), (0.54, 0.0), (0.54, 1.0), (0.50, 1.08), (-0.50, 1.08), (-0.54, 1.0)]
        extrude_profile(m, plinth, -h, h, ["StoneWall", "StoneWall", "StoneCap", "StoneCap", "StoneCap", "StoneWall"],
                        col=grime_col(0.0, 1.0, 1.08, 0.55))
        # plaster body
        body = [(-0.5, 1.08), (0.5, 1.08), (0.5, 3.3), (-0.5, 3.3)]
        extrude_profile(m, body, -h, h, ["Plaster", "Plaster", "Plaster", "Plaster"], col=grime_col(1.08, 0.75, 3.3, 0.05))
        # tie beam
        beam = [(-0.56, 3.3), (0.56, 3.3), (0.56, 3.5), (-0.56, 3.5)]
        extrude_profile(m, beam, -h, h, ["Timber"] * 4, uvrot=False)
        # gabled tile cap (eaves overhang 0.34 m each side, above head height)
        cap = [(-0.9, 3.5), (0.9, 3.5), (0.9, 3.58), (0.0, 3.98), (-0.9, 3.58)]
        extrude_profile(m, cap, -h, h, ["Timber", "RoofTile", "RoofTile", "RoofTile", "RoofTile"], col=grime_col(3.5, 0.2, 3.98, 0.0))
        ridge = [(-0.13, 3.94), (0.13, 3.94), (0.10, 4.06), (0.0, 4.12), (-0.10, 4.06)]
        extrude_profile(m, ridge, -h, h, ["RoofTile"] * 5)
        # pilasters (both faces), stone bases, timber capitals
        for u in PIER_U:
            if abs(u) > h - 0.6:
                continue
            for s in (1.0, -1.0):
                d0, d1 = sorted([s * 0.5, s * 0.61])
                m.box((u - 0.15, 1.08, d0), (u + 0.15, 3.3, d1), "Timber", uvrot={k: True for k in ("px", "nx", "pz", "nz")},
                      skip=("bottom", "top"), col=grime_col(1.08, 0.5, 3.3, 0.0))
                bd0, bd1 = sorted([s * 0.5, s * 0.64])
                m.box((u - 0.22, 1.08, bd0), (u + 0.22, 1.32, bd1), "StoneCap", skip=("bottom",), col=grime_col(1.08, 0.6, 1.32, 0.3))
                cd0, cd1 = sorted([s * 0.5, s * 0.70])
                m.box((u - 0.24, 3.04, cd0), (u + 0.24, 3.3, cd1), "Timber", skip=("top",))
        # banner rods + brackets (inner face only; the camera never sees the outside)
        for u in BANNER_U:
            if abs(u) > h - 0.8:
                continue
            m.box((u - BANNER_W * 0.5 - 0.04, BANNER_TOP - 0.02, 0.52), (u + BANNER_W * 0.5 + 0.04, BANNER_TOP + 0.02, 0.57), "Timber")
            for sx in (-1.0, 1.0):
                m.box((u + sx * 0.44 - 0.015, BANNER_TOP - 0.02, 0.5), (u + sx * 0.44 + 0.015, BANNER_TOP + 0.02, 0.57), "Iron")
    # pivot = box centre (sim): wall solids are boxes with y 0..3.5
    cx = 0.0 if face in "NS" else (-16.5 if face == "W" else 16.5)
    cz = -16.5 if face == "N" else (16.5 if face == "S" else 0.0)
    return m, (cx, top / 2, cz)


# ---------------------------------------------------------------------------------------------------------------- floor
def build_floor(A):
    h = A["half"] + 1.0                   # runs under the walls
    p0, p1 = A["pool_min"], A["pool_max"]
    m = Mesh("SM_Env_Floor", slots("Floor"))
    rects = [(-h, -h, h, p0[1]), (-h, p1[1], h, h), (-h, p0[1], p0[0], p1[1]), (p1[0], p0[1], h, p1[1])]     # x0 z0 x1 z1
    for x0, z0, x1, z1 in rects:
        if x1 - x0 < 0.01 or z1 - z0 < 0.01:
            continue
        m.face([(x0, 0.0, z1), (x1, 0.0, z1), (x1, 0.0, z0), (x0, 0.0, z0)], "Floor")
    return m, (0.0, 0.0, 0.0)


def build_solid_cover(A):
    s = A["solids"]["cover_wall"]
    mn, mx = s["min"], s["max"]
    m = Mesh("SM_Env_CoverWall", slots("StoneWall", "StoneCap"))
    gr = grime_col(0.0, 0.8, mx[1], 0.1)
    cap_h = 0.14
    m.box((mn[0], mn[1], mn[2]), (mx[0], mx[1] - cap_h, mx[2]), "StoneWall", skip=("top", "bottom"), col=gr)
    m.chamfer_box((mn[0], mx[1] - cap_h, mn[2]), (mx[0], mx[1], mx[2]), 0.03, "StoneCap", skip=("bottom",), col=grime_col(0.0, 0.0, 1.0, 0.0))
    return m, tuple((np.array(mn) + np.array(mx)) / 2)


def build_solid_terrace(A):
    s = A["solids"]["terrace"]
    mn, mx = s["min"], s["max"]
    m = Mesh("SM_Env_Terrace", slots("StoneWall", "StoneCap"))
    gr = grime_col(0.0, 0.8, mx[1], 0.15)
    cap_h = 0.14
    m.box(mn, (mx[0], mx[1] - cap_h, mx[2]), "StoneWall", skip=("top", "bottom"), col=gr)
    m.chamfer_box((mn[0], mx[1] - cap_h, mn[2]), mx, 0.035, "StoneCap", skip=("bottom",), col=grime_col(0.0, 0.0, 1.0, 0.0))
    return m, tuple((np.array(mn) + np.array(mx)) / 2)


def build_solid_step(A):
    s = A["solids"]["step_block"]
    mn, mx = s["min"], s["max"]
    m = Mesh("SM_Env_StepBlock", slots("StoneWall", "StoneCap"))
    gr = grime_col(0.0, 0.8, mx[1], 0.2)
    m.box(mn, (mx[0], mx[1] - 0.17, mx[2]), "StoneWall", skip=("top", "bottom"), col=gr)
    m.chamfer_box((mn[0], mx[1] - 0.17, mn[2]), mx, 0.04, "StoneCap", skip=("bottom",), col=grime_col(0.0, 0.0, 1.0, 0.0))
    return m, tuple((np.array(mn) + np.array(mx)) / 2)


def build_solid_ledge(A):
    s = A["solids"]["high_ledge"]
    mn, mx = s["min"], s["max"]
    m = Mesh("SM_Env_HighLedge", slots("StoneWall", "StoneCap"))
    gr = grime_col(0.0, 0.8, mx[1], 0.1)
    cap_h = 0.16
    m.box(mn, (mx[0], mx[1] - cap_h, mx[2]), "StoneWall", skip=("top", "bottom"), col=gr)
    m.chamfer_box((mn[0], mx[1] - cap_h, mn[2]), mx, 0.04, "StoneCap", skip=("bottom",), col=grime_col(0.0, 0.0, 1.0, 0.0))
    # stringcourse band flush with the faces (a different stone course reads as relief in the baked light)
    return m, tuple((np.array(mn) + np.array(mx)) / 2)


def build_solid_pillar(A, name="pillar_ne"):
    s = A["solids"][name]
    mn, mx = s["min"], s["max"]
    cx, cz = (mn[0] + mx[0]) / 2, (mn[2] + mx[2]) / 2
    top = mx[1]
    w = (mx[0] - mn[0]) / 2
    m = Mesh("SM_Env_Pillar", slots("StoneWall", "StoneCap", "Bronze", "Glow", "Timber"))
    gr = grime_col(0.0, 0.9, top, 0.0)
    # base block, shaft with chamfered edges, capital block (all inside the sim box)
    m.chamfer_box((cx - w, 0.0, cz - w), (cx + w, 0.4, cz + w), 0.03, "StoneWall", top_mat="StoneCap", chamfer_mat="StoneCap", skip=("bottom",), col=gr)
    sw = w - 0.07
    m.chamfer_box((cx - sw, 0.4, cz - sw), (cx + sw, top - 0.4, cz + sw), 0.07, "StoneWall", skip=("bottom", "top"), col=gr)
    m.chamfer_box((cx - w, top - 0.4, cz - w), (cx + w, top, cz + w), 0.04, "StoneCap", skip=("bottom",), col=grime_col(0.0, 0.0, 1.0, 0.0))
    # timber binding bands on the shaft
    for y in (1.0, 2.2):
        bw = sw + 0.012
        m.box((cx - bw, y, cz - bw), (cx + bw, y + 0.07, cz + bw), "Timber", skip=("bottom", "top"))
    # bronze brazier bowl above head height + glowing coals
    bowl = [(0.0, top), (0.20, top), (0.30, top + 0.04), (0.40, top + 0.16), (0.44, top + 0.28), (0.41, top + 0.30), (0.36, top + 0.18),
            (0.0, top + 0.12)]
    m.lathe(bowl[:6], (cx, 0, cz), "Bronze", segs=12)                                  # outer shell, outward normals
    m.lathe([(0.0, top + 0.12), (0.36, top + 0.18), (0.41, top + 0.30)], (cx, 0, cz), "Bronze", segs=12)    # inner shell (profile goes up: faces out; flipped below)
    m.disc((cx, top + 0.285, cz), 0.37, "Glow", segs=12)
    rng = np.random.default_rng(31)
    for i in range(7):
        a = rng.uniform(0, 6.28)
        r = rng.uniform(0.0, 0.26)
        m.sphere((cx + r * math.cos(a), top + 0.3, cz + r * math.sin(a)), (0.08, 0.06, 0.08), "Glow", segs=6, rings=3, smooth=1, seed=i, jitter=0.15)
    return m, ((mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2)


# ----------------------------------------------------------------------------------------------------------------- pool
def build_pool(A):
    p0, p1 = A["pool_min"], A["pool_max"]
    fy = A["pool_floor"]
    m = Mesh("SM_Env_PoolBasin", slots("PoolTile", "StoneCap"))
    gr = grime_col(fy, 1.0, 0.0, 0.2)
    # basin floor (faces up) and the four lining walls (face into the pool)
    m.face([(p0[0], fy, p1[1]), (p1[0], fy, p1[1]), (p1[0], fy, p0[1]), (p0[0], fy, p0[1])], "PoolTile", col=gr)
    m.face([(p0[0], fy, p0[1]), (p1[0], fy, p0[1]), (p1[0], 0.0, p0[1]), (p0[0], 0.0, p0[1])], "PoolTile", col=gr)       # north lining, faces +z
    m.face([(p1[0], fy, p1[1]), (p0[0], fy, p1[1]), (p0[0], 0.0, p1[1]), (p1[0], 0.0, p1[1])], "PoolTile", col=gr)       # south lining, faces -z
    m.face([(p0[0], fy, p1[1]), (p0[0], fy, p0[1]), (p0[0], 0.0, p0[1]), (p0[0], 0.0, p1[1])], "PoolTile", col=gr)       # west lining, faces +x
    m.face([(p1[0], fy, p0[1]), (p1[0], fy, p1[1]), (p1[0], 0.0, p1[1]), (p1[0], 0.0, p0[1])], "PoolTile", col=gr)       # east lining, faces -x
    # coping stones (raised 3 cm above the floor, rounded outer edge)
    c = 0.28
    ytop = 0.035
    cg = grime_col(0.0, 0.0, 1.0, 0.0)
    for (a0, b0, a1, b1) in ((p0[0] - c, p0[1] - c, p1[0] + c, p0[1]), (p0[0] - c, p1[1], p1[0] + c, p1[1] + c),
                             (p0[0] - c, p0[1], p0[0], p1[1]), (p1[0], p0[1], p1[0] + c, p1[1])):
        m.chamfer_box((a0, -0.02, b0), (a1, ytop, b1), 0.012, "StoneCap", skip=("bottom",), col=cg)
    return m, (0.0, 0.0, 0.0)


def build_pool_water(A):
    p0, p1 = A["pool_min"], A["pool_max"]
    y = A["pool_level"]
    m = Mesh("SM_Env_PoolWater", slots("Water"))
    nx, nz = 12, 16
    for i in range(nx):
        for j in range(nz):
            x0 = p0[0] + (p1[0] - p0[0]) * i / nx
            x1 = p0[0] + (p1[0] - p0[0]) * (i + 1) / nx
            z0 = p0[1] + (p1[1] - p0[1]) * j / nz
            z1 = p0[1] + (p1[1] - p0[1]) * (j + 1) / nz
            uvf = lambda P, n: np.stack([(P[:, 0] - p0[0]) / (p1[0] - p0[0]), (P[:, 2] - p0[1]) / (p1[1] - p0[1])], -1)
            m.face([(x0, y, z1), (x1, y, z1), (x1, y, z0), (x0, y, z0)], "Water", uv=uvf)
    return m, ((p0[0] + p1[0]) / 2, y, (p0[1] + p1[1]) / 2)


def build_plate(A):
    p0, p1 = A["metal_min"], A["metal_max"]
    top = A["metal_top"]
    m = Mesh("SM_Env_MetalPlate", slots("Metal"))
    m.chamfer_box((p0[0], -0.03, p0[1]), (p1[0], top, p1[1]), 0.012, "Metal", skip=("bottom",))
    return m, ((p0[0] + p1[0]) / 2, (top - 0.03) / 2, (p0[1] + p1[1]) / 2)


# -------------------------------------------------------------------------------------------------------------- banners
BANNER_NX, BANNER_NY = 6, 12


def build_banners(A):
    m = Mesh("SM_Env_Banners", slots("Banner"))
    half = A["half"]
    for fi, face in enumerate("NSWE"):
        L = 2 * (half + 1.0) if face in "NS" else 2 * half
        R, t = wall_local_frame(face)
        for bi, u in enumerate(BANNER_U):
            if abs(u) > L / 2 - 0.8:
                continue
            col_idx = (bi + fi) % 4
            with m.xf(R, t):
                for j in range(BANNER_NY):
                    for i in range(BANNER_NX):
                        def pt(ii, jj):
                            fu = ii / BANNER_NX
                            fv = jj / BANNER_NY
                            ux = u + (fu - 0.5) * BANNER_W
                            y = BANNER_TOP - 0.02 - fv * BANNER_H
                            d = 0.62 + 0.035 * math.sin(math.pi * fu) * (0.3 + 0.7 * fv)       # a gentle outward bow
                            return (ux, y, d)
                        quad = [pt(i, j + 1), pt(i + 1, j + 1), pt(i + 1, j), pt(i, j)]     # faces +d (toward the arena)
                        uvs = []
                        for (ii, jj) in ((i, j + 1), (i + 1, j + 1), (i + 1, j), (i, j)):
                            uvs.append(((col_idx + ii / BANNER_NX) / 4.0, 1.0 - jj / BANNER_NY))
                        cols = [(jj / BANNER_NY, 0.0, 0.0, 1.0) for (ii, jj) in ((i, j + 1), (i + 1, j + 1), (i + 1, j), (i, j))]
                        m.face(quad, "Banner", uv=np.array(uvs), col=np.array(cols), smooth=1)
    return m, (0.0, 0.0, 0.0)


# ------------------------------------------------------------------------------------------------------------- lanterns
def build_lanterns(A):
    m = Mesh("SM_Env_Lanterns", slots("Timber", "Glow", "Iron", "StoneCap"))
    half = A["half"]
    for face in "NSWE":
        L = 2 * (half + 1.0) if face in "NS" else 2 * half
        R, t = wall_local_frame(face)
        for u in PIER_U:
            if abs(u) > L / 2 - 0.6:
                continue
            with m.xf(R, t):
                d = 0.98
                # bracket arm from the pilaster capital
                m.box((u - 0.03, 3.12, 0.62), (u + 0.03, 3.18, d + 0.04), "Timber")
                # chain
                m.box((u - 0.008, 2.98, d - 0.008), (u + 0.008, 3.12, d + 0.008), "Iron")
                # lantern body: glowing paper drum with a timber frame, cap and base
                body = [(0.0, 2.52), (0.10, 2.52), (0.15, 2.60), (0.17, 2.74), (0.15, 2.88), (0.10, 2.96), (0.0, 2.96)]
                m.lathe(body, (u, 0, d), "Glow", segs=8, smooth=1)
                for k in range(8):
                    a = 2 * math.pi * k / 8
                    r = 0.172
                    m.box((u + r * math.cos(a) - 0.008, 2.60, d + r * math.sin(a) - 0.008), (u + r * math.cos(a) + 0.008, 2.88, d + r * math.sin(a) + 0.008), "Timber")
                m.lathe([(0.0, 2.96), (0.19, 2.96), (0.22, 3.0), (0.15, 3.06), (0.0, 3.1)], (u, 0, d), "Timber", segs=8, smooth=0)
                m.lathe([(0.0, 2.48), (0.09, 2.5), (0.11, 2.52), (0.0, 2.52)][::1], (u, 0, d), "Timber", segs=8, smooth=0)
    # corner finials above the wall caps (stone ball + bronze point)
    for sx in (-1, 1):
        for sz in (-1, 1):
            cx, cz = sx * (half + 0.5), sz * (half + 0.5)
            m.chamfer_box((cx - 0.3, 4.0, cz - 0.3), (cx + 0.3, 4.28, cz + 0.3), 0.04, "StoneCap", skip=("bottom",))
            m.sphere((cx, 4.5, cz), (0.2, 0.2, 0.2), "StoneCap", segs=8, rings=5)
            m.lathe([(0.05, 4.68), (0.02, 4.9), (0.0, 5.0)], (cx, 0, cz), "Iron", segs=6, smooth=0)
    return m, (0.0, 0.0, 0.0)


# ---------------------------------------------------------------------------------------------------------------- rings
def build_rings(A):
    m = Mesh("SM_Env_Rings", slots("Bronze"))
    def ring(c, r0, r1, y, segs):
        for k in range(segs):
            a0 = 2 * math.pi * k / segs
            a1 = 2 * math.pi * (k + 1) / segs
            P = lambda r, a: (c[0] + r * math.cos(a), y, c[1] + r * math.sin(a))
            m.face([P(r0, a0), P(r0, a1), P(r1, a1), P(r1, a0)][::-1], "Bronze", smooth=1)
    for sp in (A["player"], A["opponent"]):
        ring((sp[0], sp[2]), 0.93, 1.03, 0.006, 56)
        ring((sp[0], sp[2]), 0.30, 0.33, 0.006, 40)
    ring((0.0, 0.0), 8.0, 8.05, 0.004, 120)
    return m, (0.0, 0.0, 0.0)


ARENA_BUILDERS = {
    "SM_Env_Floor": lambda A: build_floor(A),
    "SM_Env_WallN": lambda A: build_wall("N", A["half"]),
    "SM_Env_WallS": lambda A: build_wall("S", A["half"]),
    "SM_Env_WallW": lambda A: build_wall("W", A["half"]),
    "SM_Env_WallE": lambda A: build_wall("E", A["half"]),
    "SM_Env_CoverWall": build_solid_cover,
    "SM_Env_Terrace": build_solid_terrace,
    "SM_Env_StepBlock": build_solid_step,
    "SM_Env_HighLedge": build_solid_ledge,
    "SM_Env_Pillar": lambda A: build_solid_pillar(A, "pillar_ne"),
    "SM_Env_PoolBasin": build_pool,
    "SM_Env_PoolWater": build_pool_water,
    "SM_Env_MetalPlate": build_plate,
    "SM_Env_Banners": build_banners,
    "SM_Env_Lanterns": build_lanterns,
    "SM_Env_Rings": build_rings,
}

# lightmap texel size (m) and resolution per mesh (None = no lightmap: dynamic / movable)
LIGHTMAP = {
    "SM_Env_Floor": (0.09, 1024), "SM_Env_WallN": (0.12, 512), "SM_Env_WallS": (0.12, 512), "SM_Env_WallW": (0.12, 512),
    "SM_Env_WallE": (0.12, 512), "SM_Env_CoverWall": (0.05, 256), "SM_Env_Terrace": (0.06, 256), "SM_Env_StepBlock": (0.05, 128),
    "SM_Env_HighLedge": (0.06, 256), "SM_Env_Pillar": (0.04, 256), "SM_Env_PoolBasin": (0.06, 256), "SM_Env_MetalPlate": (0.05, 256),
}

if __name__ == "__main__":
    A = ES.load_arena()
    tot = 0
    for name, fn in ARENA_BUILDERS.items():
        mesh, pivot = fn(A)
        st = mesh.stats()
        tot += st["tris"]
        print(f"{name:20s} tris={st['tris']:6d} faces={st['faces']:5d} size={np.round(np.array(st['max']) - np.array(st['min']), 3)}")
    print("total tris", tot)
