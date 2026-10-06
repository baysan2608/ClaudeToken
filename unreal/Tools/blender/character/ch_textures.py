"""Procedural textures for the 8 material slots (numpy): BaseColor (sRGB, alpha = tint mask or SSS mask),
Normal (tangent space, DirectX green), ORM (R occlusion = Cycles AO bake x detail cavity, G roughness, B metallic).

Cloth / wraps / sash / shoes BaseColor is a NEUTRAL value texture: the Unreal material multiplies it by the
palette tint (FF_Main / FF_Accent / FF_Trim) x 2 where alpha = 1 (alpha 0 = untinted, e.g. contrast stitching,
white shoe soles). Skin / hair / eyes are final colours (alpha = subsurface mask for skin, iris mask for eyes)."""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import ff_rig_spec as spec  # noqa: E402

import ch_noise as N  # noqa: E402
import ch_regions as R  # noqa: E402
import ch_texmaps as TM  # noqa: E402
import ch_weights as cw  # noqa: E402
from ch_assemble import SLOTS, TEX_SIZE  # noqa: E402


def _ss(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def _n(a):
    return a / (np.linalg.norm(a, axis=-1, keepdims=True) + 1e-12)


def bl(freq, mpt):
    """Amplitude factor that fades a pattern of `freq` cycles per metre out before it aliases on texels of `mpt`
    metres (full below 0.18 cycles / texel, gone at 0.45)."""
    return np.clip((0.45 - freq * mpt) / 0.27, 0.0, 1.0)


class Ctx:
    def __init__(self, maps, ao, part_names, lm):
        self.m = maps
        self.S = maps.S
        self.idx = np.nonzero(maps.mask)
        self.P = maps.pos[self.idx].astype(np.float64)
        self.Nn = maps.nrm[self.idx].astype(np.float64)
        self.U = maps.puv[self.idx].astype(np.float64)
        self.mpt = maps.mpt[self.idx].astype(np.float64)
        pid = maps.part[self.idx]
        self.part = np.array(part_names + ["?"])[np.where(pid >= 0, pid, len(part_names))]
        self.ao = ao[self.idx] if ao is not None else np.ones(len(self.P))
        self.lm = lm
        self.n = len(self.P)
        # distance to the island border (metres), for seams / stitches / edge wear
        from scipy import ndimage
        d = ndimage.distance_transform_edt(maps.mask)
        self.edge = d[self.idx] * self.mpt

    def is_part(self, *names):
        return np.isin(self.part, names)

    def startswith(self, prefix):
        return np.char.startswith(self.part.astype(str), prefix)


# ================================================================================================ skin
SKIN = np.array([0.33, 0.205, 0.145])          # linear base tone (warm medium tan)
LIP = np.array([0.245, 0.125, 0.105])
BROW = np.array([0.020, 0.014, 0.011])
HAIR_DARK = np.array([0.018, 0.013, 0.010])


def _blob(P, c, r):
    return np.exp(-np.sum((P - np.asarray(c)) ** 2, axis=1) / (r * r))


def gen_skin(c):
    P, lm = c.P, c.lm
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    ax = np.abs(x)
    col = np.tile(SKIN, (c.n, 1))
    h = np.zeros(c.n)
    rough = np.full(c.n, 0.52)
    alpha = np.full(c.n, 0.30)                     # subsurface / translucency mask
    lf = N.fbm3(P, 22.0, 4, seed=3)
    col *= (1.0 + 0.07 * lf)[:, None]
    mot = N.fbm3(P, 120.0, 2, seed=9)               # mottling
    col *= (1.0 + 0.035 * mot)[:, None]
    head = z > lm["jaw"][2] - 0.04
    face = head & (y < lm["skull_c"][1] - 0.02)
    eye_z = lm["eye_c_l"][2]
    # warmth zones: cheeks, nose, ears, chin; forehead a touch yellower; fingers' knuckles and tips redder
    red = np.zeros(c.n)
    for sx in (1, -1):
        red += 0.8 * _blob(P, (0.046 * sx, -0.128, 1.628), 0.022)
        red += 0.9 * _blob(P, (0.072 * sx, -0.03, 1.655), 0.028) * (ax > 0.06)
    red += 0.9 * _blob(P, (0.0, -0.168, 1.628), 0.013)
    red += 0.4 * _blob(P, (0.0, -0.150, 1.565), 0.016)
    knuckles = []
    for s in ("l", "r"):
        for f in ("index", "middle", "ring", "pinky"):
            for j in ("01", "02", "03"):
                knuckles.append(cw.bone_head(f"{f}_{j}_{s}"))
            knuckles.append(cw.bone_tail(f"{f}_03_{s}"))
        knuckles += [cw.bone_head(f"thumb_0{j}_{s}") for j in (2, 3)] + [cw.bone_tail(f"thumb_03_{s}")]
    K = np.array(knuckles)
    hand = z < 1.1
    if hand.any():
        from scipy.spatial import cKDTree
        d, _ = cKDTree(K).query(P[hand])
        red[hand] += 0.7 * np.exp(-(d / 0.008) ** 2)
    red = np.clip(red, 0, 1)
    col *= (1 + red[:, None] * np.array([0.10, -0.06, -0.05]))
    fore = head & (z > lm["brow_z"]) & face
    col[fore] *= np.array([1.02, 1.0, 0.95])
    # beard shadow (clean-shaven, subtle): jaw, chin, upper lip; never on the lips
    lips = lip_mask(P)
    beard = face & (z < 1.618) & (z > 1.52) & (ax < 0.068)
    beard_w = beard * _ss(0.068, 0.05, ax) * (1 - lips) * _ss(1.52, 1.545, z)
    speck = np.clip(N.noise2(P[:, 0] * 700 + P[:, 1] * 300, P[:, 2] * 700, seed=5), 0, 1) * bl(700, c.mpt)
    col *= (1 - beard_w[:, None] * (0.10 + 0.10 * speck[:, None]) * np.array([1.0, 0.92, 0.80]))
    # eyelids / under-eye
    for s, sx in (("l", 1), ("r", -1)):
        ec = lm[f"eye_c_{s}"]
        er = lm[f"eye_r_{s}"]
        rel = P - ec
        dd = np.linalg.norm(rel, axis=1) - er
        front = rel[:, 1] < -0.004
        lid_up = front & (rel[:, 2] > 0.002) & (dd < 0.012)
        col[lid_up] *= np.array([0.93, 0.86, 0.86]) ** (1 - np.clip(dd[lid_up] / 0.012, 0, 1))[:, None]
        under = front & (rel[:, 2] < -0.004) & (rel[:, 2] > -0.016) & (np.abs(rel[:, 0]) < 0.016)
        col[under] *= np.array([0.95, 0.93, 0.95])
        # lid margin: lash line on the top, a wet pink line under
        margin = front & (dd < 0.0024)
        top = margin & (rel[:, 2] > -0.001)
        col[top] *= (0.25 + 0.75 * _ss(0.0006, 0.0024, dd[top]))[:, None]
        bot = margin & ~top
        col[bot] = col[bot] * 0.7 + np.array([0.30, 0.12, 0.11]) * 0.3
        rough[margin] = 0.3
        # brows
        bm, along = brow_mask(P, ec, sx, lm)
        hairs = _brow_hairs(P, along, sx)
        a = np.clip(bm * (0.55 + 0.45 * hairs), 0, 1)
        col = col * (1 - a[:, None]) + BROW * a[:, None]
        h += a * 0.00012 * hairs
        rough = rough * (1 - a) + 0.65 * a
    # lips
    col = col * (1 - lips[:, None]) + (LIP * (1 + 0.06 * mot[:, None])) * lips[:, None]
    lower = lips * (z < 1.6035)
    col *= (1 + 0.05 * lower)[:, None]
    rough = rough * (1 - lips) + 0.44 * lips
    line = _ss(0.0011, 0.0, np.abs(z - (1.6035 + 0.06 * x * x / 0.024))) * _ss(0.025, 0.019, ax) * face * (y < -0.14)
    col *= (1 - 0.65 * line)[:, None]
    h -= 0.0003 * line
    # vertical lip lines
    h += lips * 0.00004 * np.sin(x * 2 * np.pi / 0.0016 + 3 * N.noise2(x * 400, z * 400, seed=11))
    # hairline: dark under the cap edge and fine strokes just below it
    hf = hair_field(P, lm)
    fuzz = head & (hf > -0.010)
    if fuzz.any():
        az, _zz = R.head_polar(P[fuzz], lm)
        strokes = 0.5 + 0.5 * N.noise2(az * 9.0, hf[fuzz] * 900, seed=13)
        dens = _ss(-0.010, 0.002, hf[fuzz])
        a = np.clip(dens * (0.35 + 0.65 * strokes ** 2), 0, 1)
        col[fuzz] = col[fuzz] * (1 - a[:, None]) + HAIR_DARK * a[:, None]
        rough[fuzz] = rough[fuzz] * (1 - a) + 0.5 * a
    # T-zone shine
    tz = face & (((ax < 0.022) & (z > 1.61)) | ((z > lm["brow_z"] + 0.005) & (ax < 0.04)))
    rough[tz] -= 0.08
    # nails + knuckle creases
    nails, crease = finger_details(P, c.Nn)
    col = col * (1 - nails[:, None]) + np.array([0.42, 0.28, 0.25]) * nails[:, None]
    rough = rough * (1 - nails) + 0.25 * nails
    h += nails * 0.00015 + crease * -0.00012
    col *= (1 - 0.18 * crease)[:, None]
    # pores + fine skin texture (face denser, hands finer)
    pore = N.cellular2(P[:, 0] * 900 + P[:, 2] * 150, P[:, 1] * 900 + P[:, 2] * 800, seed=2)
    h += np.where(face, 1.0, 0.6) * 0.00004 * _ss(0.0, 0.35, pore) * bl(900, c.mpt)
    h += 0.000012 * N.noise2(P[:, 0] * 400, P[:, 2] * 400 + P[:, 1] * 250, seed=4) * bl(400, c.mpt)
    rough += 0.04 * N.noise2(P[:, 0] * 300, P[:, 2] * 300, seed=7) * bl(300, c.mpt)
    # translucency: ears, nose, lips, fingers
    alpha += 0.7 * np.clip(_blob(P, (0.072, -0.03, 1.655), 0.03) * (x > 0.06) + _blob(P, (-0.072, -0.03, 1.655), 0.03) * (x < -0.06), 0, 1)
    alpha += 0.3 * _blob(P, (0.0, -0.165, 1.63), 0.015) + 0.4 * lips
    alpha[hand] += 0.25
    return col, np.clip(alpha, 0, 1), h, np.clip(rough, 0.2, 0.9), np.ones(c.n)


def hair_field(P, lm):
    az, z = R.head_polar(P, lm)
    return z - R.hairline_z(az, lm)


def lip_mask(P):
    """Soft lip shapes (rig space): lower lip ellipse + upper lip with a cupid's bow."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    ax = np.abs(x)
    front = y < -0.135
    lo = ((ax / 0.0228) ** 2 + ((z - 1.5958) / 0.0082) ** 2)
    bow = 0.0016 * np.exp(-(x / 0.004) ** 2) - 0.0006 * np.exp(-((ax - 0.0065) / 0.004) ** 2)
    top_edge = 1.6092 - bow - 0.0040 * (ax / 0.024) ** 2
    up = (ax < 0.0245) & (z >= 1.6030) & (z <= top_edge)
    up_soft = _ss(0.0245, 0.020, ax) * _ss(top_edge + 0.0006, top_edge - 0.0006, z) * (z >= 1.6025)
    m = np.maximum(_ss(1.15, 0.85, lo) * (z < 1.6040), up_soft)
    del up
    return m * front


def brow_mask(P, ec, sx, lm):
    """Brow coverage (0..1) and the normalised position along the brow (0 inner .. 1 outer)."""
    rel = P - ec
    xx = rel[:, 0] * sx
    zz = rel[:, 2]
    along = (xx + 0.018) / 0.040
    t = np.clip(along, 0, 1)
    centre = 0.0215 + 0.006 * np.sin(np.pi * np.clip(t * 1.1, 0, 1)) - 0.004 * t
    half = 0.0042 * (1 - t) + 0.0016 * t
    m = _ss(half + 0.001, half - 0.001, np.abs(zz - centre)) * _ss(-0.05, 0.02, along) * _ss(1.08, 0.95, along)
    m *= (rel[:, 1] < 0.0) & (P[:, 1] < -0.10)
    return m, t


def _brow_hairs(P, along, sx):
    # strokes: inner hairs point up, outer hairs point outward
    ang = np.radians(70 - 60 * np.clip(along, 0, 1))
    u = P[:, 0] * sx * np.cos(ang) + P[:, 2] * np.sin(ang)
    v = -P[:, 0] * sx * np.sin(ang) + P[:, 2] * np.cos(ang)
    return np.clip(0.5 + 0.8 * N.noise2(v * 1100, u * 160, seed=21), 0, 1)


def finger_details(P, Nn):
    nails = np.zeros(len(P))
    crease = np.zeros(len(P))
    for s in ("l", "r"):
        dorsal = -np.array(spec.PALM_N_L) * (np.array([1, 1, 1]) if s == "l" else np.array([-1, 1, 1]))
        for f in ("index", "middle", "ring", "pinky", "thumb"):
            b = f"{f}_03_{s}"
            h, t = cw.bone_head(b), cw.bone_tail(b)
            ax = _n(t - h)
            rel = P - h
            along = rel @ ax
            L = np.linalg.norm(t - h)
            radial = rel - np.outer(along, ax)
            rd = np.linalg.norm(radial, axis=1)
            facing = (radial @ dorsal) / (rd + 1e-9)
            nail = _ss(L * 0.30, L * 0.45, along) * _ss(L * 1.25, L * 1.05, along) * _ss(0.55, 0.8, facing) * (rd < 0.012)
            nails = np.maximum(nails, nail)
            for j in ("02", "03"):
                jh = cw.bone_head(f"{f}_{j}_{s}") if f != "thumb" else cw.bone_head(f"thumb_{j}_{s}")
                a2 = (P - jh) @ ax
                near = np.linalg.norm(P - jh, axis=1) < 0.014
                rings = np.exp(-((a2 + 0.0) / 0.0012) ** 2) + 0.6 * np.exp(-((a2 + 0.0025) / 0.0009) ** 2)
                crease += near * rings * _ss(0.2, 0.6, facing)
    return np.clip(nails, 0, 1), np.clip(crease, 0, 1)


# ================================================================================================ hair
def gen_hair(c):
    P, lm = c.P, c.lm
    col = np.tile(HAIR_DARK, (c.n, 1))
    h = np.zeros(c.n)
    rough = np.full(c.n, 0.50)
    cap = c.is_part("hair_cap")
    bun = c.is_part("hair_bun")
    tail = c.is_part("hair_tail")
    lash = c.startswith("lash_")
    # strand coordinate across strands (sx) and along (sy)
    sx = np.zeros(c.n)
    sy = np.zeros(c.n)
    if cap.any():
        ctr = lm["skull_c"]
        knot = cw.bone_head("ff_hair_01")
        axv = _n(knot - ctr)
        e1 = np.array([0, -1.0, 0.3])
        e1 = _n(e1 - axv * (e1 @ axv))
        e2 = np.cross(axv, e1)
        rel = _n(P[cap] - ctr)
        sx[cap] = np.arctan2(rel @ e2, rel @ e1) * 0.09
        sy[cap] = np.arccos(np.clip(rel @ axv, -1, 1)) * 0.09
    for m in (tail,):
        sx[m] = c.U[m, 0]
        sy[m] = c.U[m, 1]
    sx[bun] = c.U[bun, 1]          # strands wrap around the bun
    sy[bun] = c.U[bun, 0]
    clump = N.noise2(sx * 260, sy * 6, seed=31) * bl(260, c.mpt)
    fine = N.noise2(sx * 700, sy * 20, seed=32) * bl(700, c.mpt)
    clump_id = np.floor(sx * 180 + 0.6 * N.noise2(sy * 8, sx * 3, seed=33))
    tone = 0.75 + 0.5 * N.hash01(clump_id.astype(np.int64) + 1000, seed=3)
    col *= (tone * (1 + 0.25 * fine))[:, None]
    # soft highlight band (sheen) varying along the strands
    hl = np.clip(N.noise2(sx * 25, sy * 12, seed=34), 0, 1) ** 2
    col += hl[:, None] * np.array([0.020, 0.015, 0.011])
    h += 0.00030 * np.abs(clump) ** 0.5 + 0.00006 * fine
    rough += 0.06 * fine - 0.05 * hl
    # cap edge: thinner, a little lighter so the hairline reads soft against the skin
    if cap.any():
        hf = hair_field(P[cap], lm)
        edge = _ss(0.008, 0.0, hf)
        col[cap] = col[cap] * (1 + 0.5 * edge[:, None]) + edge[:, None] * np.array([0.01, 0.006, 0.004])
    col[lash] = np.array([0.012, 0.009, 0.008])
    rough[lash] = 0.5
    return col, np.ones(c.n), h, np.clip(rough, 0.25, 0.8), np.ones(c.n)


# ================================================================================================ eyes
def gen_eyes(c):
    P, lm = c.P, c.lm
    col = np.zeros((c.n, 3))
    h = np.zeros(c.n)
    rough = np.full(c.n, 0.16)
    iris_m = np.zeros(c.n)
    for s in ("l", "r"):
        ec = lm[f"eye_c_{s}"]
        rel = P - ec
        sel = (np.sign(P[:, 0]) == (1 if s == "l" else -1))
        d = _n(rel[sel])
        th = np.degrees(np.arccos(np.clip(-d[:, 1], -1, 1)))
        ph = np.arctan2(d[:, 2], d[:, 0])
        sclera = np.array([0.62, 0.58, 0.54]) * (1 + 0.04 * N.noise2(ph * 3, th * 0.2, seed=41))[:, None]
        corner = _ss(40, 75, th) * (0.5 + 0.5 * np.abs(np.cos(ph)))
        sclera = sclera * (1 - 0.15 * corner[:, None]) + np.array([0.45, 0.25, 0.22]) * 0.15 * corner[:, None]
        veins = np.clip(N.noise2(ph * 14, th * 0.5, seed=42) - 0.55, 0, 1) * _ss(35, 70, th)
        sclera = sclera * (1 - 0.3 * veins[:, None]) + np.array([0.5, 0.12, 0.1]) * 0.3 * veins[:, None]
        r = th / 28.0
        fib = 0.5 + 0.5 * N.noise2(ph * 22, r * 2.5, seed=43)
        crypt = np.clip(N.noise2(ph * 9, r * 6, seed=44), 0, 1)
        iris = np.array([0.050, 0.028, 0.014]) * (0.7 + 0.6 * fib)[:, None]
        collar = _ss(0.55, 0.38, r) * _ss(0.30, 0.40, r)
        iris = iris + collar[:, None] * np.array([0.07, 0.042, 0.016])
        iris *= (1 - 0.35 * crypt)[:, None]
        limbal = _ss(0.80, 1.0, r)
        iris *= (1 - 0.65 * limbal)[:, None]
        pupil = _ss(0.36, 0.31, r)
        iris = iris * (1 - pupil[:, None]) + np.array([0.004, 0.004, 0.005]) * pupil[:, None]
        im = _ss(1.04, 0.98, r)
        out = sclera * (1 - im[:, None]) + iris * im[:, None]
        back = _ss(100, 115, th)
        out = out * (1 - back[:, None]) + np.array([0.05, 0.03, 0.03]) * back[:, None]
        col[sel] = out
        iris_m[sel] = im
        h[sel] = -0.0002 * im * (1 - pupil) + 0.00002 * fib * im
        rough[sel] = 0.18 * (1 - im) + 0.06 * im
    return col, iris_m, h, rough, np.ones(c.n)


# ================================================================================================ cloth (shared)
def weave(U, mpt, scale=1.0, seed=0):
    """Neutral linen-like structure at texture scale (slubs: thick / thin threads; long along the grain).
    The thread-level weave itself is a tiling detail map in the Unreal material (T_Fighter_Detail_Weave_N)."""
    u, v = U[:, 0] * scale, U[:, 1] * scale
    warp = N.noise2(u * 240, v * 14, seed=seed) * bl(240 * scale, mpt)
    weft = N.noise2(u * 14, v * 240, seed=seed + 1) * bl(240 * scale, mpt)
    fine = N.noise2(u * 420, v * 420, seed=seed + 2) * bl(420 * scale, mpt)
    val = 0.55 * warp + 0.45 * weft + 0.3 * fine
    return val, 0.5 * (warp + weft) + 0.5 * fine


def stitches(c, dist=0.005, period=0.0042, width=0.0011):
    """Dashed stitch line at `dist` from every island border (seams, hems). Returns 0..1 mask, groove height."""
    e = c.edge
    line = _ss(width, width * 0.4, np.abs(e - dist))
    phase = (c.U[:, 0] + c.U[:, 1]) / period
    dash = _ss(0.15, 0.3, np.abs(np.sin(np.pi * phase)))
    seam = _ss(0.003, 0.0, e)                       # the seam allowance edge bulge
    return line * dash, line, seam


def folds_field(c, garment):
    """Large fold height (metres) by garment region, oriented in the grain-aligned pattern space."""
    P, U = c.P, c.U
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    u, v = U[:, 0], U[:, 1]
    h = np.zeros(c.n)
    wob = N.fbm2(u * 6, v * 6, 3, seed=51)
    if garment == "tunic":
        torso = np.abs(x) < 0.20
        drape = _ss(1.30, 1.08, z) * torso
        h += 0.0030 * drape * np.sin(2 * np.pi * u / 0.075 + 2.5 * wob)
        bunch = _ss(1.12, 1.04, z) * torso
        h += 0.0022 * bunch * np.sin(2 * np.pi * v / 0.016 + 4 * N.noise2(u * 18, v * 4, seed=52))
        # sleeves: rings near the elbow / cuff, diagonal pulls from the armpit
        sleeve = np.abs(x) > 0.20
        for s, sg in (("l", 1), ("r", -1)):
            m = sleeve & (x * sg > 0)
            el = cw.bone_head("lowerarm_" + s)
            sh = cw.bone_head("upperarm_" + s)
            ax = _n(el - sh)
            t = (P[m] - el) @ ax
            ring = np.exp(-(t / 0.07) ** 2)
            h[m] += 0.0025 * ring * np.sin(2 * np.pi * t / 0.022 + 3 * N.noise2(u[m] * 25, v[m] * 25, seed=53))
            pit = np.exp(-(((P[m] - sh) @ ax) / 0.06) ** 2)
            h[m] += 0.0016 * pit * np.sin(2 * np.pi * (u[m] + v[m]) / 0.04 + 2 * wob[m])
    elif garment == "hem":
        hang = _ss(0.95, 0.60, z)
        h += 0.0045 * hang * np.sin(2 * np.pi * u / 0.10 + 2.2 * N.noise2(u * 3, v * 2, seed=54) + 0.5 * wob)
        h += 0.0012 * np.sin(2 * np.pi * u / 0.031 + 3 * wob) * hang
    elif garment == "trousers":
        blouse = _ss(R.SHIN_WRAP_TOP - 0.005, R.SHIN_WRAP_TOP + 0.02, z) * _ss(0.52, 0.42, z)
        h += 0.0035 * blouse * np.sin(2 * np.pi * v / 0.018 + 5 * N.noise2(u * 14, v * 6, seed=55))
        for s in ("l", "r"):
            kn = cw.bone_head("calf_" + s)
            d = np.linalg.norm(P - kn, axis=1)
            back = (y - kn[1]) > 0.0
            h += 0.002 * np.exp(-(d / 0.07) ** 2) * np.sin(2 * np.pi * (z - kn[2]) / 0.02 + 2 * wob) * np.where(back, 1.0, 0.4)
        crotch = _ss(0.93, 0.80, z) * _ss(0.70, 0.80, z) * _ss(0.09, 0.03, np.abs(x))
        h += 0.0018 * crotch * np.sin(2 * np.pi * (u - v * np.sign(x)) / 0.035 + 2 * wob)
        h += 0.0015 * _ss(0.85, 0.5, z) * np.sin(2 * np.pi * u / 0.06 + 2.5 * wob)
    return h


def cavity_from_height(img_h, mask, sigma_px=6):
    from scipy import ndimage
    blur = ndimage.gaussian_filter(img_h, sigma_px)
    cav = np.clip((blur - img_h), 0, None)
    return cav


def gen_cloth_main(c):
    col = np.full((c.n, 3), 0.42)
    alpha = np.ones(c.n)
    tun = c.is_part("tunic")
    hem = c.is_part("hem")
    tro = c.is_part("trousers")
    col[tro] = 0.24
    w_val, w_h = weave(c.U, c.mpt)
    col *= (1 + 0.05 * w_val)[:, None]
    lf = N.fbm2(c.U[:, 0] * 8, c.U[:, 1] * 8, 3, seed=61)
    col *= (1 + 0.04 * lf)[:, None]
    h = 0.00006 * w_h
    for name, m in (("tunic", tun), ("hem", hem), ("trousers", tro)):
        if m.any():
            sub = _Sub(c, m)
            h[m] += folds_field(sub, name)
    st, line, seam = stitches(c)
    col = col * (1 - st[:, None]) + np.array([0.62, 0.60, 0.55]) * st[:, None]
    alpha = alpha * (1 - st)
    h += -0.0004 * line * (1 - st) + 0.0005 * seam + 0.0001 * st
    # wear: lighter at edges, elbows / knees; darker dust at the trouser bottoms
    wear = _ss(0.02, 0.0, c.edge) * np.clip(0.5 + N.noise2(c.U[:, 0] * 90, c.U[:, 1] * 90, seed=62), 0, 1)
    col *= (1 + 0.10 * wear)[:, None]
    dust = tro * _ss(0.50, 0.40, c.P[:, 2])
    col *= (1 - 0.10 * dust * np.clip(0.6 + N.noise2(c.U[:, 0] * 30, c.U[:, 1] * 30, seed=63), 0, 1))[:, None]
    rough = np.clip(0.80 + 0.05 * w_val - 0.05 * st, 0.5, 0.95)
    return col, alpha, h, rough, np.ones(c.n)


class _Sub:
    """A masked view of a Ctx (for per-garment helpers)."""

    def __init__(self, c, m):
        self.P, self.U, self.n = c.P[m], c.U[m], int(m.sum())


def gen_cloth_accent(c):
    col = np.full((c.n, 3), 0.44)
    alpha = np.ones(c.n)
    w_val, w_h = weave(c.U, c.mpt, 1.3, seed=70)
    col *= (1 + 0.05 * w_val)[:, None]
    h = 0.00005 * w_h
    band = c.startswith("collar") | c.startswith("cuff")
    # bands: two stitch rows along the visible face + soft padding between them
    v = c.U[:, 1]
    vb = v[band]
    vmin, vmax = 0.012, 0.012 + 0.036
    rows = _ss(0.0009, 0.0003, np.abs(vb - (vmin + 0.004))) + _ss(0.0009, 0.0003, np.abs(vb - (vmax - 0.004)))
    dash = _ss(0.15, 0.3, np.abs(np.sin(np.pi * c.U[band, 0] / 0.004)))
    st_b = np.clip(rows, 0, 1) * dash
    pad = np.sin(np.clip((vb - vmin - 0.004) / (vmax - vmin - 0.008), 0, 1) * np.pi)
    h[band] += 0.0006 * pad - 0.0003 * np.clip(rows, 0, 1)
    st = np.zeros(c.n)
    st[band] = st_b
    # hem border pieces: stitches along their borders
    hb = ~band
    if hb.any():
        s2, line, seam = stitches(c, dist=0.006)
        st[hb] = s2[hb]
        h[hb] += -0.0003 * line[hb] + 0.0004 * seam[hb]
    col = col * (1 - st[:, None]) + np.array([0.60, 0.58, 0.53]) * st[:, None]
    alpha *= (1 - st)
    wear = _ss(0.012, 0.0, c.edge) * 0.5
    col *= (1 + 0.08 * wear)[:, None]
    rough = np.clip(0.74 + 0.05 * w_val, 0.5, 0.95)
    return col, alpha, h, rough, np.ones(c.n)


def gen_wraps(c):
    P = c.P
    col = np.full((c.n, 3), 0.40)
    h = np.zeros(c.n)
    alpha = np.ones(c.n)
    # spiral bands around the limb axis (forearm-hand for z > 0.75, shin below)
    s_coord = np.zeros(c.n)
    W = 0.040
    pitch = W * 0.72
    for s, sg in (("l", 1), ("r", -1)):
        for kind in ("arm", "leg"):
            if kind == "arm":
                p0, p1 = cw.bone_head("lowerarm_" + s), cw.bone_tail("middle_03_" + s)
                m = (P[:, 0] * sg > 0) & (P[:, 2] > 0.75)
                ref = -np.array(spec.PALM_N_L) * np.array([sg, 1, 1])
            else:
                p0, p1 = cw.bone_head("calf_" + s), cw.bone_tail("calf_" + s)
                m = (P[:, 0] * sg > 0) & (P[:, 2] <= 0.75)
                ref = np.array([sg, 0, 0.0])
            if not m.any():
                continue
            ax = _n(p1 - p0)
            e1 = _n(ref - ax * (ref @ ax))
            e2 = np.cross(ax, e1)
            rel = P[m] - p0
            along = rel @ ax
            ang = np.arctan2(rel @ e2, rel @ e1) * sg
            s_coord[m] = along + (ang / (2 * np.pi)) * pitch
    b = s_coord / pitch
    jitter = 0.08 * N.noise2(c.U[:, 0] * 40, c.U[:, 1] * 40, seed=81)
    bf = b + jitter
    band_id = np.floor(bf)
    t = bf - band_id
    # each band overlaps the previous: a step at the band edge, slightly rounded crown
    h += 0.0007 * t + 0.0002 * np.sin(np.pi * t) - 0.0002 * _ss(0.06, 0.0, t)
    col *= (0.93 + 0.12 * N.hash01(band_id.astype(np.int64) + 500, seed=8))[:, None]
    col *= (1 - 0.35 * _ss(0.05, 0.0, t))[:, None]               # shadowed edge line
    gauze = np.sin(c.U[:, 0] * 2 * np.pi * 300) * np.sin(c.U[:, 1] * 2 * np.pi * 300) * bl(300, c.mpt)
    gn = N.noise2(c.U[:, 0] * 260, c.U[:, 1] * 260, seed=82) * bl(260, c.mpt)
    h += 0.00004 * gauze + 0.00003 * gn
    col *= (1 + 0.05 * gn)[:, None]
    # grime toward the knuckles / ankles
    grime = np.zeros(c.n)
    for s in ("l", "r"):
        k = cw.bone_tail("middle_metacarpal_" + s)
        grime += np.exp(-np.sum((P - k) ** 2, axis=1) / 0.03 ** 2)
        a = cw.bone_tail("calf_" + s)
        grime += np.exp(-np.sum((P - a) ** 2, axis=1) / 0.05 ** 2)
    col *= (1 - 0.12 * np.clip(grime, 0, 1) * (0.6 + 0.4 * gn))[:, None]
    rough = np.clip(0.86 + 0.04 * gn, 0.6, 0.95)
    return col, alpha, h, rough, np.ones(c.n)


def gen_sash(c):
    U = c.U
    col = np.full((c.n, 3), 0.45)
    alpha = np.ones(c.n)
    h = np.zeros(c.n)
    twill = np.sin((U[:, 0] + U[:, 1]) * 2 * np.pi * 160 + 2 * N.noise2(U[:, 0] * 100, U[:, 1] * 100, seed=91))
    twill *= bl(160 * 1.41, c.mpt)
    tn = N.noise2(U[:, 0] * 220, U[:, 1] * 220, seed=92) * bl(220, c.mpt)
    col *= (1 + 0.04 * twill + 0.04 * tn)[:, None]
    h += 0.00004 * twill
    band = c.is_part("sash_band")
    tails = c.startswith("sash_tail")
    knot = c.is_part("sash_knot")
    tie = c.is_part("hair_tie")
    wob = N.fbm2(U[:, 0] * 10, U[:, 1] * 10, 3, seed=93)
    h[band] += 0.0014 * np.sin(2 * np.pi * U[band, 1] / 0.016 + 3 * wob[band])
    h[band] += 0.0008 * np.sin(2 * np.pi * U[band, 0] / 0.05 + 2 * wob[band]) * np.sin(2 * np.pi * U[band, 1] / 0.09)
    h[tails] += 0.0020 * np.sin(2 * np.pi * U[tails, 0] / 0.030 + 2.5 * wob[tails]) * _ss(0.0, 0.08, U[tails, 1])
    h[knot] += 0.0025 * np.abs(N.fbm2(U[knot, 0] * 40, U[knot, 1] * 40, 3, seed=94))
    # fringe at the tail ends (last 2.5 cm): threads with gaps
    if tails.any():
        L = np.zeros(c.n)
        for nm in ("sash_tail_l", "sash_tail_r"):
            m = c.is_part(nm)
            if m.any():
                L[m] = U[m, 1].max()
        fr = tails & (U[:, 1] > L - 0.025)
        thread = 0.5 + 0.5 * np.sin(U[fr, 0] * 2 * np.pi / 0.0035)
        col[fr] *= (0.55 + 0.45 * thread)[:, None]
        h[fr] += 0.0003 * thread
    col[tie] *= 0.9
    h[tie] += 0.0004 * np.sin((U[tie, 0] * 2 + U[tie, 1] * 6) * 2 * np.pi / 0.01)
    edge = _ss(0.006, 0.0, c.edge)
    col *= (1 - 0.15 * edge)[:, None]
    rough = np.clip(0.62 + 0.05 * tn, 0.4, 0.9)
    return col, alpha, h, rough, np.ones(c.n)


def gen_shoes(c):
    P = c.P
    z = P[:, 2]
    col = np.full((c.n, 3), 0.22)
    alpha = np.ones(c.n)
    # sole: the bottom plus the first 1.3 cm of the side (layered white cloth sole, untinted)
    sole = (c.Nn[:, 2] < -0.6) | (z < 0.013)
    col[sole] = np.array([0.66, 0.64, 0.60])
    alpha[sole] = 0.0
    layers = 0.5 + 0.5 * np.sin(z * 2 * np.pi / 0.0026)
    h = np.zeros(c.n)
    side = sole & (c.Nn[:, 2] >= -0.6)
    h[side] += 0.00012 * layers[side]
    col[side] *= (0.92 + 0.08 * layers[side])[:, None]
    bottom = sole & ~side
    tread = N.cellular2(P[bottom, 0] * 900, P[bottom, 1] * 900, seed=101)
    h[bottom] += 0.0002 * tread
    col[bottom] *= 0.85
    # upper: canvas weave + stitched opening + centre seam over the toe
    up = ~sole
    w_val, w_h = weave(c.U, c.mpt, 1.2, seed=102)
    col[up] *= (1 + 0.06 * w_val[up])[:, None]
    h[up] += 0.00005 * w_h[up]
    st, line, seam = stitches(c, dist=0.004)
    s_up = st * up
    col = col * (1 - s_up[:, None]) + np.array([0.55, 0.53, 0.50]) * s_up[:, None]
    alpha = alpha * (1 - s_up)
    h += -0.0003 * line * up + 0.0003 * seam
    # sole stitch row
    srow = side & (np.abs(z - 0.0075) < 0.0008) & (np.abs(np.sin((P[:, 0] + P[:, 1]) * np.pi / 0.004)) > 0.4)
    col[srow] *= 0.55
    rough = np.where(sole, 0.88, 0.80) + 0.03 * w_val
    return col, alpha, h, np.clip(rough, 0.5, 0.95), np.ones(c.n)


GEN = {"skin": gen_skin, "hair": gen_hair, "eyes": gen_eyes, "cloth_main": gen_cloth_main,
       "cloth_accent": gen_cloth_accent, "wraps": gen_wraps, "sash": gen_sash, "shoes": gen_shoes}
NORMAL_STRENGTH = {"skin": 1.0, "hair": 1.0, "eyes": 1.0, "cloth_main": 1.0, "cloth_accent": 1.0, "wraps": 1.0,
                   "sash": 1.0, "shoes": 1.0}


def build_all(obj, parts, lm, out_dir, quick=False, slots=None):
    """Rasterise the LOD0 object, bake AO, generate and save every texture. Returns a report dict."""
    import time
    part_names = [p.name for p in parts]
    sizes = {k: (v // 4 if quick else v) for k, v in TEX_SIZE.items()}
    t0 = time.time()
    ao = TM.bake_ao(obj, sizes, samples=8 if quick else 32)
    rep = {"ao_bake_s": round(time.time() - t0, 1)}
    T = TM.mesh_tables(obj)
    for si, slot in enumerate(SLOTS):
        if slots and slot not in slots:
            continue
        S = sizes[slot]
        t1 = time.time()
        maps = TM.rasterize(T, si, S)
        if not maps.mask.any():
            continue
        ctx = Ctx(maps, ao.get(slot), part_names, lm)
        col, alpha, h, rough, aomul = GEN[slot](ctx)
        img_col = np.zeros((S, S, 3))
        img_a = np.ones((S, S))
        img_h = np.zeros((S, S))
        img_r = np.full((S, S), 0.8)
        img_ao = np.ones((S, S))
        img_mpt = np.full((S, S), float(np.median(ctx.mpt)))
        img_col[ctx.idx] = col
        img_a[ctx.idx] = alpha
        img_h[ctx.idx] = h
        img_r[ctx.idx] = rough
        img_ao[ctx.idx] = np.clip(ctx.ao, 0, 1) * aomul
        img_mpt[ctx.idx] = ctx.mpt
        ind = TM.fill_index(maps.mask)
        img_col, img_a, img_h, img_r, img_ao, img_mpt = (TM.pad(a, ind) for a in (img_col, img_a, img_h, img_r,
                                                                                 img_ao, img_mpt))
        # fold cavities darken the occlusion a little (cloth reads better on phone screens)
        cav = cavity_from_height(img_h, maps.mask, sigma_px=max(2, S // 340))
        img_ao = np.clip(img_ao * (1 - np.clip(cav / 0.002, 0, 0.35)), 0, 1)
        nrm = TM.height_to_normal(img_h, img_mpt, NORMAL_STRENGTH[slot])
        bc = np.dstack([TM.to_srgb(img_col), img_a])
        TM.save_png(os.path.join(out_dir, f"T_Fighter_{slot}_BC.png"), bc, "RGBA")
        TM.save_png(os.path.join(out_dir, f"T_Fighter_{slot}_N.png"), nrm, "RGB")
        orm = np.dstack([img_ao, img_r, np.zeros_like(img_r)])
        TM.save_png(os.path.join(out_dir, f"T_Fighter_{slot}_ORM.png"), orm, "RGB")
        rep[slot] = {"size": S, "texels": int(maps.mask.sum()), "seconds": round(time.time() - t1, 1)}
        print("[character] texture", slot, rep[slot])
    return rep
