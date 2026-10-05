"""Procedural PBR bake for the fighter: albedo / tangent-space normal / ORM (R = ambient occlusion, G = roughness,
B = metallic) at 1024 x 1024 per material slot, computed with numpy straight from the mesh (no Cycles bake).

Pipeline
  1. per-vertex ambient occlusion (BVH ray casts on the rest pose mesh)
  2. every UV triangle is rasterised into per-texel attribute buffers (position, normal, vertex colour masks, AO, part id,
     pixels-per-metre)
  3. a material shader paints albedo / roughness / height from those attributes (position-based 3D noise, so there are no
     seams; weave and strand patterns live in texel space)
  4. height -> tangent-space normal (OpenGL / +Y convention, as used by glTF and Godot), margins are bled outwards

Albedo is *relative*: the game multiplies it by the slot's albedo_color (skin / cloth / hair tint), so cloth, wraps and
hair are near neutral and every value stays <= 1.
Vertex colours (see fighter_mesh): R = surface variant (1 = rubber sole), G = wear / dirt, B = nail mask on fingers.
"""
import math

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from scipy import ndimage

from fighter_mesh import ACC, HAIR, MAIN, MAT_NAMES, SKIN, WRAPS

SIZE = 1024
ROUGH_REF = {"skin": 0.6}          # fighter_view.gd._recolor roughness per slot (default 0.82)


# ---------------------------------------------------------------------------------------------------------------------
# noise
# ---------------------------------------------------------------------------------------------------------------------
def _hash3(ix, iy, iz, seed):
    u = np.uint32
    h = (ix.astype(np.int64).astype(np.uint32) * u(73856093)) ^ (iy.astype(np.int64).astype(np.uint32) * u(19349663)) \
        ^ (iz.astype(np.int64).astype(np.uint32) * u(83492791)) ^ u((seed * 2654435761) & 0xFFFFFFFF)
    h ^= h >> u(13)
    h *= u(1274126177)
    h ^= h >> u(16)
    return (h & u(0xFFFFFF)).astype(np.float32) / np.float32(16777215.0)


def vnoise3(x, y, z, seed=0):
    ix, iy, iz = np.floor(x), np.floor(y), np.floor(z)
    fx, fy, fz = (x - ix).astype(np.float32), (y - iy).astype(np.float32), (z - iz).astype(np.float32)
    fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (fx if dx else 1 - fx) * (fy if dy else 1 - fy) * (fz if dz else 1 - fz)
                out = out + w * _hash3(ix + dx, iy + dy, iz + dz, seed)
    return out


def vnoise2(x, y, seed=0):
    return vnoise3(x, y, np.zeros_like(x), seed)


def fbm3(x, y, z, octaves=4, seed=0, gain=0.5):
    tot, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        tot = tot + amp * vnoise3(x * 2 ** o, y * 2 ** o, z * 2 ** o, seed + o * 7)
        norm += amp
        amp *= gain
    return tot / norm


def fbm2(x, y, octaves=3, seed=0, gain=0.5):
    tot, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        tot = tot + amp * vnoise2(x * 2 ** o, y * 2 ** o, seed + o * 7)
        norm += amp
        amp *= gain
    return tot / norm


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0 if e1 != e0 else 1e-9), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def gauss(x, z, cx, cz, sx, sz):
    return np.exp(-((x - cx) / sx) ** 2 - ((z - cz) / sz) ** 2)


# ---------------------------------------------------------------------------------------------------------------------
# vertex AO
# ---------------------------------------------------------------------------------------------------------------------
def vertex_normals(mb):
    n = [Vector() for _ in mb.verts]
    for f in mb.faces:
        pts = [mb.verts[k] for k in f]
        nr = Vector()
        for i in range(1, len(f) - 1):
            nr += (pts[i] - pts[0]).cross(pts[i + 1] - pts[0])
        for k in f:
            n[k] += nr
    return [v.normalized() if v.length > 1e-12 else Vector((0, 0, 1)) for v in n]


def vertex_ao(mb, rays=40, max_dist=0.16):
    tree = BVHTree.FromPolygons([tuple(v) for v in mb.verts], [tuple(f) for f in mb.faces])
    nrm = vertex_normals(mb)
    # Fibonacci hemisphere
    dirs = []
    for i in range(rays):
        zc = (i + 0.5) / rays
        r = math.sqrt(1 - zc * zc)
        a = i * 2.399963
        dirs.append((r * math.cos(a), r * math.sin(a), zc))
    out = []
    for v, n in zip(mb.verts, nrm):
        t = Vector((1, 0, 0)) if abs(n.x) < 0.9 else Vector((0, 1, 0))
        t = (t - n * t.dot(n)).normalized()
        b = n.cross(t)
        occ = tot = 0.0
        o = v + n * 0.0015
        for (dx, dy, dz) in dirs:
            d = (t * dx + b * dy + n * dz)
            hit = tree.ray_cast(o, d, max_dist)
            tot += dz
            if hit[0] is not None:
                occ += dz * (1.0 - 0.55 * hit[3] / max_dist)
        out.append(1.0 - occ / tot)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# rasteriser
# ---------------------------------------------------------------------------------------------------------------------
class Buf:
    pass


def rasterise(mb, mesh, m, vao, size=SIZE):
    """Attribute buffers (flat arrays over valid texels) for material index m."""
    face_part = {}
    for pi, part in enumerate(mb.parts):
        for f in part["faces"]:
            face_part[f] = pi
    cn = np.empty(len(mesh.loops) * 3, dtype=np.float32)
    mesh.corner_normals.foreach_get("vector", cn)
    cn = cn.reshape(-1, 3)
    H = W = size
    mask = np.zeros((H, W), bool)
    pos = np.zeros((H, W, 3), np.float32)
    nrm = np.zeros((H, W, 3), np.float32)
    col = np.zeros((H, W, 3), np.float32)
    ao = np.ones((H, W), np.float32)
    part = np.full((H, W), -1, np.int32)
    ppm = np.zeros((H, W), np.float32)
    V = np.array([tuple(v) for v in mb.verts], np.float32)
    C = np.array(mb.cols, np.float32)
    for f, idx in enumerate(mb.faces):
        if mb.fmat[f] != m:
            continue
        poly = mesh.polygons[f]
        li = list(poly.loop_indices)
        uv = np.array(mb.fuv[f], np.float64) * size
        for k in range(1, len(idx) - 1):
            tri = (0, k, k + 1)
            uu = uv[list(tri)]
            x0, x1 = int(max(0, math.floor(uu[:, 0].min()))), int(min(W - 1, math.ceil(uu[:, 0].max())))
            y0, y1 = int(max(0, math.floor(uu[:, 1].min()))), int(min(H - 1, math.ceil(uu[:, 1].max())))
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            a, b, c = uu
            den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(den) < 1e-12:
                continue
            l1 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
            l2 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
            l3 = 1 - l1 - l2
            eps = -0.02
            ins = (l1 >= eps) & (l2 >= eps) & (l3 >= eps)
            if not ins.any():
                continue
            ids = [idx[t] for t in tri]
            P = V[ids]
            Nn = cn[[li[t] for t in tri]]
            Cc = C[ids]
            aoo = np.array([vao[i] for i in ids], np.float32)
            sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
            L = np.stack([l1, l2, l3], -1)[ins]
            ys, xs = np.nonzero(ins)
            ys, xs = ys + y0, xs + x0
            pos[ys, xs] = L @ P
            nrm[ys, xs] = L @ Nn
            col[ys, xs] = L @ Cc
            ao[ys, xs] = L @ aoo
            mask[ys, xs] = True
            part[ys, xs] = face_part[f]
            e3 = np.linalg.norm(P[1] - P[0]) + np.linalg.norm(P[2] - P[1]) + np.linalg.norm(P[0] - P[2])
            e2 = np.linalg.norm(uu[1] - uu[0]) + np.linalg.norm(uu[2] - uu[1]) + np.linalg.norm(uu[0] - uu[2])
            ppm[ys, xs] = e2 / max(e3, 1e-9)
    b = Buf()
    b.mask, b.pos, b.nrm, b.col, b.ao, b.part, b.ppm = mask, pos, nrm, col, ao, part, ppm
    return b


# ---------------------------------------------------------------------------------------------------------------------
# shaders.  Each gets flat arrays of the valid texels and returns albedo (N,3), rough (N), height (N, metres)
# ---------------------------------------------------------------------------------------------------------------------
def weave(px, py, period_px, seed=0, amp=1.0):
    """Plain-weave-ish height in [0, 1] with thread irregularity."""
    ph = 2 * math.pi / period_px
    n1 = vnoise2(px / (period_px * 3.0), py / (period_px * 0.7), seed)
    n2 = vnoise2(px / (period_px * 0.7), py / (period_px * 3.0), seed + 3)
    a = 0.5 + 0.5 * np.sin(px * ph + 3.0 * n1)
    b = 0.5 + 0.5 * np.sin(py * ph + 3.0 * n2)
    checker = np.sign(np.sin(px * ph * 0.5) * np.sin(py * ph * 0.5))
    return np.clip(0.5 * (a * (checker > 0) + b * (checker <= 0)) + 0.3 * (n1 + n2) * 0.5 + 0.2, 0, 1) * amp


def shade_skin(T):
    x, y, z = T.x, T.y, T.z
    n = len(x)
    base = 0.975 + 0.025 * fbm3(x * 9, y * 9, z * 9, 3, 1)
    alb = np.stack([base, base * 0.985, base * 0.975], -1)
    rough = 0.60 + 0.08 * fbm3(x * 40, y * 40, z * 40, 2, 5)
    height = 0.00006 * (vnoise3(x * 900, y * 900, z * 900, 2) - 0.5)       # pores
    wear = T.col[:, 1]
    # reddish scuffs / knuckles
    alb[:, 1] -= 0.16 * wear * (0.6 + 0.4 * fbm3(x * 60, y * 60, z * 60, 2, 9))
    alb[:, 2] -= 0.17 * wear * (0.6 + 0.4 * fbm3(x * 60, y * 60, z * 60, 2, 9))
    # nails
    nail = sstep(0.45, 0.6, T.col[:, 2])
    alb = alb * (1 - nail[:, None]) + nail[:, None] * np.array([1.0, 0.90, 0.88], np.float32)
    rough = rough * (1 - nail) + 0.30 * nail
    height += 0.00025 * nail
    # head details
    head = T.is_part(("head",)) & (y > 0.025) & (z > 1.50)
    ax = np.abs(x)
    if head.any():
        hz = z
        # cheeks / nose / socket shading
        blush = gauss(ax, hz, 0.050, 1.607, 0.022, 0.016)
        alb[:, 1] -= 0.07 * blush * head
        alb[:, 2] -= 0.06 * blush * head
        ez = 1.6355
        sock = gauss(ax, hz, 0.0325, ez, 0.020, 0.012)
        alb *= (1 - 0.07 * sock * head)[:, None]
        alb[:, 1] -= 0.04 * sock * head
        tipz = 1.5915
        nose = gauss(ax, hz, 0.0, tipz, 0.010, 0.008)
        alb[:, 1] -= 0.05 * nose * head
        alb[:, 2] -= 0.05 * nose * head
        # nostrils
        nos = gauss(ax, hz, 0.0098, tipz - 0.0115, 0.0042, 0.0030)
        k = np.clip(nos * 1.4, 0, 1) * head
        alb *= (1 - 0.40 * k)[:, None]
        # lips
        lip_c = 1.5600
        r2 = (x / 0.0275) ** 2 + ((hz - lip_c - 0.0010 * np.exp(-(x / 0.006) ** 2)) / 0.0078) ** 2
        lips = sstep(1.0, 0.72, r2) * head
        lipcol = np.array([0.99, 0.62, 0.60], np.float32)
        alb = alb * (1 - 0.85 * lips[:, None]) + 0.85 * lips[:, None] * lipcol * (0.92 + 0.08 * (1 - sstep(0.0, 1.0, np.abs(hz - 1.5568) / 0.008)))[:, None]
        rough = rough * (1 - lips) + 0.38 * lips
        line = np.exp(-((hz - (1.5612 + 0.0009 * (x / 0.0275) ** 2)) / 0.00085) ** 2) * sstep(0.030, 0.022, ax) * head
        alb *= (1 - 0.62 * line)[:, None]
        alb[:, 1:] -= 0.05 * line[:, None]
        height -= 0.0004 * line
        height += 0.00018 * lips * (0.5 + 0.5 * np.sin(x * 2200))          # fine vertical lip lines
        # eyebrows
        t = np.clip((ax - 0.010) / 0.052, 0, 1)
        zc = 1.6585 + 0.0062 * np.sin(math.pi * t ** 0.9) - 0.0018 * (1 - t)
        thick = 0.0043 * (1 - 0.55 * np.clip((ax - 0.012) / 0.045, 0, 1)) + 0.0004
        dist = np.abs(hz - zc) / thick
        bm = sstep(1.0, 0.45, dist) * sstep(0.007, 0.013, ax) * sstep(0.062, 0.050, ax) * head
        stroke = 0.78 + 0.22 * vnoise2(ax * 700 + hz * 150, hz * 260 - ax * 90, 21)
        bm = np.clip(bm * stroke * 1.15, 0, 1)
        brow = np.array([0.40, 0.36, 0.34], np.float32)
        alb = alb * (1 - 0.88 * bm[:, None]) + 0.88 * bm[:, None] * brow
        height += 0.00025 * bm * vnoise2(ax * 600 + hz * 200, hz * 500, 4)
        # eyelid lines: lash line + crease
        ex = ax - 0.0325
        lash_z = ez + 0.0072 - 0.0042 * (ex / 0.0155) ** 2
        lash = np.exp(-((hz - lash_z) / 0.00115) ** 2) * sstep(0.0185, 0.0150, np.abs(ex)) * head
        # thicken toward the outer corner
        lash *= 0.7 + 0.3 * sstep(-0.01, 0.014, ex)
        alb *= (1 - 0.72 * lash)[:, None]
        crease = np.exp(-((hz - (ez + 0.0125 - 0.003 * (ex / 0.02) ** 2)) / 0.0011) ** 2) * sstep(0.024, 0.016, np.abs(ex)) * head
        alb *= (1 - 0.14 * crease)[:, None]
        alb[:, 1:] -= 0.03 * crease[:, None]
        height -= 0.00022 * crease
        # a thin scar through the left brow
        sc = np.abs((x - 0.030) * 0.9 + (hz - 1.671) * 0.42 - 0.0)
        scar_t = np.clip((hz - 1.648) / 0.026, 0, 1)
        sx_ = 0.0445 - 0.0165 * scar_t
        scar = np.exp(-((x - sx_) / 0.0011) ** 2) * sstep(1.647, 1.650, hz) * sstep(1.675, 1.672, hz) * (x > 0) * head
        alb[:, 1:] -= 0.0 * scar[:, None]
        alb *= (1 - 0.0 * scar)[:, None]
        alb += 0.03 * scar[:, None]
        alb[:, 0] = np.minimum(alb[:, 0], 1.0)
        height += 0.00018 * scar
        # ear / neck
    ear = T.is_part(("ear-1", "ear1"))
    if ear.any():
        alb[:, 1] -= 0.10 * ear
        alb[:, 2] -= 0.10 * ear
    # AO into albedo (mild)
    alb *= (0.86 + 0.14 * T.ao)[:, None]
    return np.clip(alb, 0, 1), rough, height


def shade_eye(T):
    from fighter_body import eye_centres
    cs = [c for c in eye_centres()]
    n = len(T.x)
    alb = np.ones((n, 3), np.float32)
    rough = np.full(n, 0.28, np.float32)
    height = np.zeros(n, np.float32)
    for k in (0, 1):
        sel = T.is_part((f"eye{k}",))
        if not sel.any():
            continue
        c = cs[k]
        cx, cz = c.x, c.z
        dx, dz = T.x - cx, T.z - cz
        fwd = (T.y - (-c.y)) > 0.0
        rr = np.hypot(dx / 1.0, dz / 1.0)
        ang = np.arctan2(dz, dx)
        sclera = np.array([1.0, 0.96, 0.93], np.float32)
        a = np.tile(sclera, (n, 1))
        a *= (1 - 0.10 * sstep(0.004, 0.011, rr))[:, None]
        a[:, 1:] -= 0.03 * sstep(0.006, 0.011, rr)[:, None] * np.abs(dx / 0.011)[:, None] * 0.5
        iris = sstep(0.0058, 0.0050, rr) * fwd
        streak = 0.8 + 0.2 * vnoise2(ang * 6.0 + 20, rr * 700, 31)
        ic = np.array([0.44, 0.30, 0.17], np.float32)[None, :] * (0.6 + 0.4 * sstep(0.0020, 0.0046, rr))[:, None] * streak[:, None]
        limbal = sstep(0.0036, 0.0054, rr)
        ic = ic * (1 - 0.6 * limbal[:, None])
        a = a * (1 - iris[:, None]) + ic * iris[:, None]
        pupil = sstep(0.0024, 0.0019, rr) * fwd
        a = a * (1 - pupil[:, None]) + 0.03 * pupil[:, None]
        alb = np.where(sel[:, None], a, alb)
        rough = np.where(sel, 0.30 - 0.18 * iris, rough)
    alb *= (0.55 + 0.45 * T.ao)[:, None]
    return np.clip(alb, 0, 1), rough, height


def shade_hair(T):
    n = len(T.x)
    px, py = T.px, T.py
    strand = vnoise2(px / 2.6, py / 38.0, 1) * 0.55 + vnoise2(px / 1.3, py / 18.0, 2) * 0.45
    clump = vnoise2(px / 14.0, py / 70.0, 3)
    v = 0.52 + 0.30 * strand + 0.18 * clump
    gloss = vnoise2(px / 5.0, py / 90.0, 6)
    alb = np.stack([v, v * 0.97, v * 0.94], -1)
    # roots in the hair mass are darker, tips lighter
    alb *= (0.55 + 0.45 * T.ao)[:, None]
    rough = 0.42 + 0.22 * (1 - gloss)
    height = 0.00035 * (strand - 0.5)
    return np.clip(alb, 0, 1), rough.astype(np.float32), height


def shade_main(T):
    x, y, z = T.x, T.y, T.z
    n = len(x)
    px, py = T.px, T.py
    ppm = T.ppm_part
    per = np.maximum(0.0070 * ppm, 3.2)
    w = weave(px, py, per, 11)
    low = fbm3(x * 14, y * 14, z * 14, 3, 12)
    v = 0.955 + 0.04 * low + 0.04 * (w - 0.5)
    alb = np.stack([v, v * 0.995, v * 1.0], -1)
    height = 0.00022 * (w - 0.5)
    wear = T.col[:, 1]
    d = np.clip(wear * (0.55 + 0.9 * fbm3(x * 30, y * 30, z * 30, 3, 13)), 0, 1)
    alb[:, 0] *= (1 - 0.20 * d)
    alb[:, 1] *= (1 - 0.26 * d)
    alb[:, 2] *= (1 - 0.36 * d)
    rough = 0.86 + 0.06 * (w - 0.5) + 0.05 * d
    # ---- seams and stitching, painted by part --------------------------------------------------------------------------
    def dashes(coord, period=0.0042, duty=0.58):
        f = (coord / period) % 1.0
        return (f < duty).astype(np.float32)

    # trouser: outer seam (double stitched) along each leg, knee patch on the left
    for side, sx in (("L", 1), ("R", -1)):
        sel = T.is_part((f"trouser_{side}",))
        if not sel.any():
            continue
        legx = sx * 0.09 + sx * (0.045 - 0.09 * 0) * 0.0
        lat = (x - sx * 0.092) * sx                    # outward offset from the leg axis
        # the leg axis leans forward with height; use y relative to a thigh/shin centre line
        yc = np.where(z > 0.495, 0.0 + 0.045 * (0.9 - z) / 0.405, 0.045 - 0.05 * (0.495 - z) / 0.41)
        dy = y - yc
        seam = np.exp(-(dy / 0.0016) ** 2) * (lat > 0.02)
        st1 = np.exp(-((dy - 0.0065) / 0.0007) ** 2) * dashes(z) * (lat > 0.02)
        st2 = np.exp(-((dy + 0.0065) / 0.0007) ** 2) * dashes(z + 0.002) * (lat > 0.02)
        stitch = np.clip(seam * 0.8 + st1 + st2, 0, 1) * sel
        alb *= (1 - 0.30 * stitch)[:, None]
        height += (0.00045 * seam * (lat > 0.02)) * sel
        # cuff band and hem stitches at the ankle end
        hem = np.exp(-((z - 0.362) / 0.0007) ** 2) * dashes(x * 1.0 + y * 1.0) * sel
        alb *= (1 - 0.30 * hem)[:, None]
        if side == "L":
            kz, kx, ky = 0.512, sx * 0.090, 0.045 + 0.034
            inpatch = (np.abs(z - kz) < 0.040) & (np.abs(x - kx) < 0.036) & (y > 0.02) & sel
            e = np.maximum(np.abs(z - kz) / 0.040, np.abs(x - kx) / 0.036)
            edge = inpatch * np.exp(-((e - 0.86) / 0.035) ** 2)
            patch_dash = edge * np.clip(dashes(z + x, 0.0045, 0.6), 0, 1)
            alb = alb * (1 - 0.0 * inpatch)[:, None]
            alb[inpatch] *= np.array([0.93, 0.90, 0.85], np.float32)
            alb *= (1 - 0.32 * patch_dash)[:, None]
            height += 0.0006 * inpatch * sstep(0.98, 0.9, e)
    # top: centre-back and side seams, hem stitching, collar stitch lines
    sel = T.is_part(("top",))
    if sel.any():
        back = (y < -0.02) & sel
        cb = np.exp(-(x / 0.0016) ** 2) * back
        cbs = (np.exp(-((x - 0.0065) / 0.0007) ** 2) + np.exp(-((x + 0.0065) / 0.0007) ** 2)) * dashes(z) * back
        side = (np.exp(-((y + 0.004) / 0.0016) ** 2) * (np.abs(x) > 0.10)) * sel
        hemz = np.maximum(z - 0.0, 0)
        hs = np.exp(-((z - (0.962 + 0.04 * np.abs(x) / 0.17)) / 0.0008) ** 2) * dashes(x + y, 0.0045) * sel
        m = np.clip(cb * 0.8 + cbs + side * 0.8 + hs, 0, 1)
        alb *= (1 - 0.30 * m)[:, None]
        height += 0.0004 * (cb + side)
        # sweat / dust darkening toward the hem
        alb *= (1 - 0.06 * sstep(1.05, 0.95, z) * sel)[:, None]
    # pelvis seams
    sel = T.is_part(("pelvis",))
    if sel.any():
        cbp = np.exp(-(x / 0.0016) ** 2) * sel
        alb *= (1 - 0.26 * cbp)[:, None]
    # sleeves: hem stitch near the trim
    alb *= (0.84 + 0.16 * T.ao)[:, None]
    return np.clip(alb, 0, 1), rough.astype(np.float32), height.astype(np.float32)


def shade_accent(T):
    x, y, z = T.x, T.y, T.z
    px, py = T.px, T.py
    ppm = T.ppm_part
    per = np.maximum(0.0060 * ppm, 3.2)
    # twill: diagonal weave
    d = (px + py) / per
    tw = 0.5 + 0.5 * np.sin(2 * math.pi * d + 2.0 * vnoise2(px / 9.0, py / 9.0, 31))
    n1 = fbm3(x * 16, y * 16, z * 16, 3, 33)
    v = 0.95 + 0.05 * n1 + 0.04 * (tw - 0.5)
    alb = np.stack([v, v, v], -1)
    wear = T.col[:, 1]
    dirt = np.clip(wear * (0.5 + 0.9 * fbm3(x * 26, y * 26, z * 26, 3, 35)), 0, 1)
    alb *= (1 - 0.30 * dirt)[:, None]
    rough = 0.66 + 0.05 * (tw - 0.5) + 0.12 * dirt
    height = 0.00020 * (tw - 0.5)
    # shoes: leather grain, stitched welt line, scuffed toe
    shoe = T.is_part(("shoe_L", "shoe_R"))
    if shoe.any():
        grain = fbm3(x * 220, y * 220, z * 220, 2, 41)
        crack = np.abs(vnoise3(x * 70, y * 70, z * 70, 43) - 0.5)
        crackm = sstep(0.045, 0.0, crack)
        sv = 0.92 + 0.05 * grain - 0.06 * crackm
        alb[shoe] = np.stack([sv, sv, sv], -1)[shoe] * (1 - 0.30 * dirt[shoe])[:, None]
        rough = np.where(shoe, 0.48 + 0.2 * crackm + 0.15 * dirt, rough)
        height = np.where(shoe, 0.00018 * (grain - 0.5) - 0.00012 * crackm, height)
        welt = np.exp(-((z - 0.0295) / 0.0007) ** 2) * dashes_y(y)
        alb *= (1 - 0.35 * welt * shoe)[:, None]
        sole_line = np.exp(-((z - 0.0135) / 0.0011) ** 2) * shoe
        alb *= (1 - 0.40 * sole_line)[:, None]
    # trim / hair tie / collar piping: slightly darker stitching lines parallel to edges are provided by geometry
    alb *= (0.84 + 0.16 * T.ao)[:, None]
    return np.clip(alb, 0, 1), rough.astype(np.float32), height.astype(np.float32)


def dashes_y(y, period=0.0042, duty=0.58):
    return (((y / period) % 1.0) < duty).astype(np.float32)


def shade_wraps(T):
    x, y, z = T.x, T.y, T.z
    px, py = T.px, T.py
    ppm = T.ppm_part
    per = np.maximum(0.0050 * ppm, 3.4)
    w = weave(px, py, per, 51)
    fiber = vnoise2(px / 1.6, py / 14.0, 53)
    n1 = fbm3(x * 12, y * 12, z * 12, 3, 55)
    v = 0.97 + 0.05 * n1 + 0.04 * (w - 0.5) + 0.03 * (fiber - 0.5)
    alb = np.stack([v, v * 0.99, v * 0.97], -1)
    wear = T.col[:, 1]
    dirt = np.clip(wear * (0.55 + 0.9 * fbm3(x * 22, y * 22, z * 22, 3, 57)), 0, 1)
    tint = np.array([0.90, 0.84, 0.72], np.float32)
    alb = alb * (1 - 0.55 * dirt[:, None]) + 0.55 * dirt[:, None] * alb * tint * 0.85
    rough = 0.90 + 0.05 * (w - 0.5)
    height = 0.00030 * (w - 0.5) + 0.00012 * (fiber - 0.5)
    # rubber soles
    sole = sstep(0.5, 0.9, T.col[:, 0])
    if sole.any():
        tread = 0.5 + 0.5 * np.sin(y * 2 * math.pi / 0.012)
        rub = 0.30 + 0.04 * fbm3(x * 80, y * 80, z * 80, 2, 61)
        alb = alb * (1 - sole[:, None]) + sole[:, None] * np.stack([rub, rub * 0.97, rub * 0.93], -1)
        rough = rough * (1 - sole) + sole * (0.78 + 0.1 * fbm3(x * 120, y * 120, z * 120, 2, 63))
        height += sole * (0.0004 * tread * (z < 0.004))
    # eyes use this slot too (see shade_eye): handled by the caller
    alb *= (0.80 + 0.20 * T.ao)[:, None]
    return np.clip(alb, 0, 1), rough.astype(np.float32), height.astype(np.float32)


class Texels:
    """Flat per-texel view of the rasterised buffers for the shaders (A-frame coordinates: x left, y forward, z up)."""

    def __init__(self, buf, mb, size):
        ys, xs = np.nonzero(buf.mask)
        self.ys, self.xs = ys, xs
        p = buf.pos[ys, xs]
        self.x, self.y, self.z = p[:, 0].copy(), -p[:, 1].copy(), p[:, 2].copy()
        self.col = buf.col[ys, xs]
        self.ao = buf.ao[ys, xs]
        self.part = buf.part[ys, xs]
        self.px, self.py = xs.astype(np.float32) + 0.5, ys.astype(np.float32) + 0.5
        self.ppm = buf.ppm[ys, xs]
        self.names = [pt["name"] for pt in mb.parts]
        # median ppm per part: constant texel density for the patterns
        pp = np.zeros(len(self.names), np.float32)
        for i in range(len(self.names)):
            s = self.part == i
            if s.any():
                pp[i] = np.median(self.ppm[s])
        self.ppm_part = pp[np.maximum(self.part, 0)]

    def is_part(self, names):
        ids = [i for i, n in enumerate(self.names) if n in names]
        return np.isin(self.part, ids)


SHADERS = {SKIN: shade_skin, MAIN: shade_main, ACC: shade_accent, WRAPS: shade_wraps, HAIR: shade_hair}


def height_to_normal(h, ppm, strength=1.0):
    """h, ppm: (H, W) arrays (metres, pixels per metre), bleed already applied.  OpenGL tangent space (+Y = +v)."""
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * ppm
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * ppm              # row index increases with v (bottom-up image)
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n


def bleed(arr, mask, iters=None):
    """Fill every texel outside `mask` with its nearest valid texel's value (works for (H,W) and (H,W,C))."""
    _, (iy, ix) = ndimage.distance_transform_edt(~mask, return_indices=True)
    return arr[iy, ix]


def bake(mb, mesh_ob, size=SIZE, log=print):
    mesh = mesh_ob.data
    vao = vertex_ao(mb)
    log(f"[tex] vertex AO done (mean {sum(vao) / len(vao):.2f})")
    out = {}
    for m, name in enumerate(MAT_NAMES):
        buf = rasterise(mb, mesh, m, vao, size)
        if not buf.mask.any():
            continue
        T = Texels(buf, mb, size)
        alb, rough, hgt = SHADERS[m](T)
        if m == WRAPS:
            ea, er, eh = shade_eye(T)
            eye = T.is_part(("eye0", "eye1"))
            alb = np.where(eye[:, None], ea, alb)
            rough = np.where(eye, er, rough)
            hgt = np.where(eye, 0.0, hgt)
        H = W = size
        A = np.zeros((H, W, 3), np.float32)
        R = np.full((H, W), 0.8, np.float32)
        HG = np.zeros((H, W), np.float32)
        AO = np.ones((H, W), np.float32)
        PP = np.full((H, W), 800.0, np.float32)
        A[T.ys, T.xs] = alb
        R[T.ys, T.xs] = rough
        HG[T.ys, T.xs] = hgt
        AO[T.ys, T.xs] = T.ao
        PP[T.ys, T.xs] = T.ppm_part
        mask = buf.mask
        A, R, HG, AO, PP = (bleed(a, mask) for a in (A, R, HG, AO, PP))
        # light blur of the height before differentiation avoids aliasing sparkle
        HG = ndimage.gaussian_filter(HG, 0.5)
        N = height_to_normal(HG, PP, 1.0)
        # the game sets StandardMaterial3D.roughness per slot (0.6 skin, 0.82 otherwise) and Godot multiplies it with the
        # roughness texture, so the texture stores roughness *relative* to that reference (<= 1)
        ref = ROUGH_REF.get(name, 0.82)
        orm = np.stack([np.clip(0.55 + 0.45 * AO, 0, 1), np.clip(R / ref, 0, 1), np.zeros_like(R)], -1)
        out[name] = dict(albedo=np.clip(A, 0, 1), normal=N * 0.5 + 0.5, orm=orm)
        log(f"[tex] {name}: {int(mask.sum())} texels ({mask.mean() * 100:.0f}% of the atlas)")
    return out
