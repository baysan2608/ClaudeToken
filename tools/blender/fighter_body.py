"""Head (with modelled face), ears, neck, hair (cap + locks + topknot) and torso skin for the fighter."""
import math
import random

from mathutils import Vector

from fighter_mesh import (Curve, HAIR, SKIN, ACC, MAIN, WRAPS, axial_weights, clamp01, frame_from_tangent, lerp,
                          ring_pts, sgnpow, sstep, to_a, torso_weights)
from fighter_skeleton import A

DEG = math.pi / 180.0


def G(x, z, cx, cz, sx, sz):
    return math.exp(-((x - cx) / sx) ** 2 - ((z - cz) / sz) ** 2)


# ---------------------------------------------------------------------------------------------------------------------
# head silhouette (superellipse rings, A frame)
# ---------------------------------------------------------------------------------------------------------------------
HEAD_Z = [1.514, 1.521, 1.528, 1.535, 1.541, 1.547, 1.552, 1.5565, 1.5605, 1.564, 1.5685, 1.573, 1.578, 1.584, 1.590, 1.597,
          1.604, 1.611, 1.619, 1.627, 1.634, 1.641, 1.648, 1.655, 1.663, 1.672, 1.683, 1.695, 1.708, 1.721, 1.733, 1.743]
_hk = [1.514, 1.524, 1.540, 1.560, 1.580, 1.600, 1.625, 1.650, 1.675, 1.700, 1.720, 1.735, 1.745]
HEAD_W = Curve(list(zip(_hk, [0.022, 0.034, 0.048, 0.058, 0.064, 0.068, 0.072, 0.075, 0.0765, 0.075, 0.068, 0.053, 0.034])))
_front = [0.080, 0.084, 0.091, 0.096, 0.099, 0.099, 0.099, 0.102, 0.103, 0.100, 0.090, 0.071, 0.042]
_back = [-0.008, 0.020, 0.046, 0.066, 0.080, 0.090, 0.096, 0.098, 0.097, 0.090, 0.078, 0.060, 0.035]
HEAD_D = Curve(list(zip(_hk, [(f + b) / 2 for f, b in zip(_front, _back)])))
HEAD_C = Curve(list(zip(_hk, [(f - b) / 2 for f, b in zip(_front, _back)])))
HEAD_P = 2.15
_P = [6, 12, 18, 24, 30, 38, 48, 60, 74, 90, 110, 135, 160]
THETAS = [180.0] + [-p for p in reversed(_P)] + [0.0] + [float(p) for p in _P]      # degrees from the front, + = left


def head_surface(z, theta_deg, off=0.0):
    """Skull point (Blender coords) at height z and angle from the front, pushed outward by `off` (metres), plus the
    outward unit normal (Blender xy)."""
    th = theta_deg * DEG
    w, d, c = HEAD_W(z), HEAD_D(z), HEAD_C(z)
    e = 2.0 / HEAD_P
    s, co = math.sin(th), math.cos(th)
    x = w * sgnpow(s, e)
    y = -c - d * sgnpow(co, e)
    gx = HEAD_P * abs(x / w) ** (HEAD_P - 1) * (1 if x >= 0 else -1) / w
    gy = HEAD_P * abs((y + c) / d) ** (HEAD_P - 1) * (1 if (y + c) >= 0 else -1) / d
    gl = math.hypot(gx, gy) or 1.0
    nx, ny = gx / gl, gy / gl
    return Vector((x + nx * off, y + ny * off, z)), Vector((nx, ny, 0.0))


def face_disp(x, z):
    """Outward displacement (m) of the face surface at (x = left+, z) in metres."""
    ax = abs(x)
    d = 0.0
    # brow ridge, glabella and forehead
    zb = 1.6555 - 0.004 * (ax / 0.06)
    d += 0.0085 * sstep(0.066, 0.040, ax) * math.exp(-((z - zb) / 0.0078) ** 2)
    d += 0.0035 * G(ax, z, 0.0, 1.690, 0.050, 0.022)
    d += -0.0030 * G(ax, z, 0.070, 1.650, 0.012, 0.022)                          # temples
    # eye sockets, lids
    ze = 1.6355
    d += -0.0070 * G(ax, z, 0.0325, ze, 0.0165, 0.0105)
    d += 0.0035 * G(ax, z, 0.0325, ze + 0.012, 0.0175, 0.0038)
    d += 0.0016 * G(ax, z, 0.0325, ze - 0.011, 0.015, 0.004)
    d += -0.0030 * G(ax, z, 0.011, ze, 0.007, 0.012)                             # inner corner / nasion dip
    # nose
    tip = 1.5915
    hb = 0.0040 + 0.0170 * sstep(1.642, tip, z)
    hb *= sstep(tip - 0.016, tip - 0.001, z) * sstep(1.658, 1.636, z)
    sxn = 0.0065 + 0.0105 * sstep(1.628, tip, z)
    d += hb * math.exp(-(x / sxn) ** 2)
    d += 0.0055 * G(ax, z, 0.0165, tip - 0.002, 0.0085, 0.0075)                   # alar wings
    d += 0.0030 * G(ax, z, 0.0, tip - 0.001, 0.0085, 0.0055)                      # tip
    d += -0.0050 * G(ax, z, 0.0095, tip - 0.012, 0.0050, 0.0036)                  # nostrils
    # cheeks, nasolabial, philtrum
    d += 0.0050 * G(ax, z, 0.050, 1.610, 0.024, 0.017)
    d += -0.0035 * G(ax, z, 0.052, 1.577, 0.022, 0.020)
    d += -0.0025 * G(ax, z, 0.030, 1.572, 0.005, 0.014)
    d += -0.0020 * G(ax, z, 0.0, 1.5745, 0.0045, 0.0050)
    # mouth
    d += 0.0043 * G(ax, z, 0.0, 1.5685, 0.0300, 0.0050)
    d += 0.0050 * G(ax, z, 0.0, 1.5535, 0.0270, 0.0060)
    d += -0.0048 * G(ax, z, 0.0, 1.5612, 0.0340, 0.0017)
    d += -0.0030 * G(ax, z, 0.0285, 1.5613, 0.0055, 0.0050)
    d += -0.0035 * G(ax, z, 0.0, 1.5425, 0.0230, 0.0040)
    d += 0.0075 * G(ax, z, 0.0, 1.5240, 0.0210, 0.0120)                           # chin
    d += 0.0030 * G(ax, z, 0.040, 1.530, 0.016, 0.014)                            # jaw muscle
    return d


def head_weights(z):
    nk = 1.0 - sstep(1.525, 1.575, z)
    return {"head": 1.0 - 0.55 * nk, "neck": 0.55 * nk} if nk > 0 else {"head": 1.0}


def build_head(mb):
    mb.begin("head", SKIN, density=1.7)
    rings = []
    for z in HEAD_Z:
        r = []
        for th in THETAS:
            p, nrm = head_surface(z, th)
            m = sstep(82, 48, abs(th))
            if m > 0:
                dn = face_disp(p.x, z) * m
                p = p + nrm * dn
            r.append(p)
        rings.append(r)
    C = 2 * math.pi * 0.085
    ucoord = lambda jj: (0.0 if jj == 0 else (THETAS[jj % len(THETAS)] + 180.0) / 360.0 * C) if jj < len(THETAS) else C
    vc = lambda i: HEAD_Z[i]
    mb.loft(rings, lambda i, j, p: head_weights(p.z), SKIN,
            cap_start=(Vector((0, -0.040, 1.508)), {"head": 0.55, "neck": 0.45}),
            cap_end=(Vector((0, 0.0, 1.748)), {"head": 1.0}), ucoord=ucoord, vcoord=vc)
    # ears
    for sx in (-1, 1):
        mb.begin(f"ear{sx}", SKIN, density=1.7)
        rr = []
        ax_c = Vector((sx * 0.0715, 0.004, 1.627))                           # blender coords (y back)
        tilt = Vector((0.0, 0.25, 0.97)).normalized()                         # major axis leans backward (top is back)
        sideways = Vector((0.0, -0.97, 0.25)).normalized()
        for dx, rs, sh in ((-0.002, 0.80, 0.0), (0.005, 1.0, 0.002), (0.011, 0.97, 0.004), (0.015, 0.78, 0.006)):
            c = ax_c + Vector((sx * dx, 0.0, 0.0)) + Vector((0, 0.002, 0)) * (dx / 0.015)
            pts = []
            for k in range(10):
                a = 2 * math.pi * k / 10
                r_up, r_fw = 0.0305 * rs, 0.0185 * rs * (1.0 + 0.18 * math.cos(a))
                pts.append(c + tilt * (r_up * math.sin(a)) + sideways * (r_fw * math.cos(a)) + Vector((sx * sh, 0, 0)))
            rr.append(pts)
        mb.loft(rr, lambda i, j, p: {"head": 1.0}, SKIN,
                cap_start=(ax_c + Vector((sx * -0.008, 0, 0)), {"head": 1.0}),
                cap_end=(ax_c + Vector((sx * 0.0085, 0.0015, 0.0)), {"head": 1.0}))


def build_neck(mb):
    mb.begin("neck", SKIN, density=1.2)
    nz = [1.415, 1.440, 1.470, 1.500, 1.530, 1.552]
    nr = Curve(list(zip(nz, [0.066, 0.058, 0.0525, 0.0500, 0.0495, 0.0495])))
    rings = []
    for z in nz:
        # slightly wider than deep, a touch forward at the throat
        rings.append(ring_pts(A(0, 0.006 - 0.0005 * (z - 1.43) * 10, z), Vector((1, 0, 0)), Vector((0, 1, 0)), nr(z) * 1.05,
                              nr(z) * 0.93, 12, 2.2, phase=math.pi * 0.5))
    mb.loft(rings, lambda i, j, p: axial_weights(p.z), SKIN)


# ---------------------------------------------------------------------------------------------------------------------
# hair
# ---------------------------------------------------------------------------------------------------------------------
_HL = Curve([(0, 1.706), (30, 1.702), (50, 1.690), (70, 1.676), (90, 1.668), (112, 1.652), (140, 1.628), (180, 1.605)])
HAIR_TOP = 1.746


def hairline(theta):
    return _HL(abs(theta))


def build_hair(mb, seed=11):
    rnd = random.Random(seed)
    # --- cap: rings from the hairline to the crown; per-meridian clump offsets give the volume -----------------------
    mb.begin("hair_cap", HAIR, density=1.0)
    n = 36
    ths = [(-180.0 + 360.0 * k / n) for k in range(n)]
    clump = [rnd.uniform(-0.0035, 0.0055) for _ in range(n)]
    clump = [0.25 * clump[(k - 1) % n] + 0.5 * clump[k] + 0.25 * clump[(k + 1) % n] for k in range(n)]
    S = [0.0, 0.12, 0.26, 0.42, 0.58, 0.72, 0.84, 0.93, 1.0]
    rings = []
    for si, s in enumerate(S):
        r = []
        for j, th in enumerate(ths):
            zb = hairline(th) - 0.002
            z = lerp(zb, HAIR_TOP - 0.004 * (1 - s), s ** 0.85)
            a = abs(th)
            vol = 1.0 - 0.5 * sstep(70, 120, a)                                       # fuller on top/front than at the nape
            off = 0.0055 + vol * 0.010 * math.sin(math.pi * min(1.0, s * 1.06)) + clump[j] * math.sin(math.pi * min(1.0, s * 1.1)) ** 0.8
            if si == 0:
                off = 0.0045 + 0.2 * clump[j]
            p, _ = head_surface(z, th, off=off)
            r.append(p)
        rings.append(r)
    C = 2 * math.pi * 0.09
    mb.loft(rings, lambda i, j, p: {"head": 1.0}, HAIR, cap_end=(Vector((0, 0.003, HAIR_TOP + 0.004)), {"head": 1.0}),
            ucoord=lambda jj: (jj / n) * C, vcoord=lambda i: (len(S) - 1 - i) * 0.02)

    # --- tufts: short flat shingles along the hairline give the jagged edge; a fringe falls over the forehead ---------
    def tuft(th0, z0, z1, width, lift, flare, steps=3, sweep=0.0, thick=0.0055, tip_taper=0.35, base_off=0.0075):
        mb.begin("tuft", HAIR, density=1.0, group="locks")
        pts = []
        for k in range(steps):
            t = k / (steps - 1)
            e = t * t * (3 - 2 * t) * 0.3 + t * 0.7
            th = th0 + sweep * e
            z = lerp(z0, z1, e)
            off = base_off + lift * math.sin(math.pi * min(1.0, 0.15 + 0.8 * t)) + flare * t ** 2
            p, nrm = head_surface(z, th, off=off)
            pts.append((p, nrm, width * 0.5 * (1.0 - (1.0 - tip_taper) * t ** 1.5)))
        rr = []
        for k, (p, nrm, hw) in enumerate(pts):
            tg = pts[min(k + 1, len(pts) - 1)][0] - pts[max(k - 1, 0)][0]
            u, v, t_ = frame_from_tangent(tg, nrm)
            rr.append(ring_pts(p, u, v, thick * 0.5, hw, 4, 2.0))
        tg = pts[-1][0] - pts[-2][0]
        tip = pts[-1][0] + tg.normalized() * 0.008 + pts[-1][1] * flare * 0.25
        mb.loft(rr, lambda i, j, p: {"head": 1.0}, HAIR, cap_end=(tip, {"head": 1.0}),
                ucoord=lambda jj: jj * 0.01, vcoord=lambda i: i * 0.03)

    n1 = 24
    for k in range(n1):
        th = -180 + 360 * (k + rnd.uniform(-0.25, 0.25)) / n1
        a = abs(th)
        front = 1.0 - sstep(25, 55, a)
        zh = hairline(th)
        tuft(th, zh + 0.040 + 0.01 * rnd.random(), zh - 0.016 - 0.012 * rnd.random() - 0.010 * sstep(110, 170, a), 0.040,
             0.003, 0.006 + 0.004 * front, steps=3, sweep=rnd.uniform(-5, 5), base_off=0.0065)
    for k in range(8):
        th = -34 + 68 * k / 7 + rnd.uniform(-2, 2)
        tuft(th, 1.722, hairline(th) - 0.024 - 0.006 * rnd.random(), 0.034, 0.014, 0.012, steps=4, sweep=-th * 0.08, base_off=0.009)
    for sgn in (-1, 1):
        tuft(sgn * 90, 1.668, 1.640, 0.016, 0.002, 0.003, steps=3, tip_taper=0.4, thick=0.005, base_off=0.0055)

    # --- topknot (bun + tail) on hair_top.001/.002 + tie band ---------------------------------------------------------
    mb.begin("topknot", HAIR, density=1.0)
    c = Vector((0.0, 0.036, 1.762))                     # blender coords (y back)

    def kw(z, y):
        t = clamp01((z - 1.742) / 0.03)
        w2 = clamp01((y - 0.04) / 0.045)
        return {"hair_top.001": (1 - w2) * t, "head": (1 - t) * 0.9, "hair_top.002": w2 * t}

    brings = []
    prof = [(0.0, 0.25), (0.35, 0.8), (0.7, 1.0), (1.0, 0.85), (1.35, 0.4)]
    for (zz, rs) in prof:
        cc = c + Vector((0, 0, (zz - 0.7) * 0.026))
        brings.append(ring_pts(cc, Vector((1, 0, 0)), Vector((0, 1, 0)), 0.0285 * rs, 0.0265 * rs, 10, 2.1))
    mb.loft(brings, lambda i, j, p: kw(p.z, p.y), HAIR,
            cap_start=(c + Vector((0, 0, -0.020)), kw(1.742, 0.04)), cap_end=(c + Vector((0, 0.002, 0.030)), kw(1.79, 0.04)))
    for k, (dx, dz) in enumerate(((-0.011, 0.0), (0.0, 0.004), (0.011, 0.0))):
        mb.begin("knot_tail", HAIR, density=1.0, group="locks")
        pts = [Vector((dx * 0.5, 0.062, 1.768 + dz)), Vector((dx * 0.9, 0.080, 1.779 + dz)), Vector((dx * 1.2, 0.096, 1.783 + dz)),
               Vector((dx * 1.5, 0.108, 1.768 + dz))]
        rr = []
        for q in range(len(pts)):
            t = q / (len(pts) - 1)
            nxt = pts[min(q + 1, len(pts) - 1)] - pts[max(q - 1, 0)]
            u, v, t_ = frame_from_tangent(nxt, Vector((1, 0, 0)))
            rr.append(ring_pts(pts[q], u, v, 0.0085 * (1 - 0.7 * t), 0.0075 * (1 - 0.7 * t), 4, 2.0))
        mb.loft(rr, lambda i, j, p: {"hair_top.002": 0.75, "hair_top.001": 0.25} if i < 2 else {"hair_top.002": 1.0}, HAIR,
                cap_end=(pts[-1] + Vector((0, 0.006, -0.004)), {"hair_top.002": 1.0}),
                ucoord=lambda jj: jj * 0.01, vcoord=lambda i: i * 0.03)
    mb.begin("hair_tie", ACC, density=1.0)
    tr = [ring_pts(c + Vector((0, 0, -0.012 + dz)), Vector((1, 0, 0)), Vector((0, 1, 0)), 0.0245 + dr, 0.0225 + dr, 10, 2.1)
          for dz, dr in ((0.0, 0.0), (0.004, 0.0035), (0.008, 0.0))]
    mb.loft(tr, lambda i, j, p: {"head": 0.4, "hair_top.001": 0.6}, ACC)


# ---------------------------------------------------------------------------------------------------------------------
# torso
# ---------------------------------------------------------------------------------------------------------------------
_TORSO_Z = [0.90, 0.97, 1.04, 1.12, 1.20, 1.27, 1.335, 1.385, 1.420, 1.447, 1.470]
TORSO_W = Curve(list(zip(_TORSO_Z, [0.165, 0.158, 0.148, 0.146, 0.154, 0.172, 0.180, 0.170, 0.128, 0.078, 0.058])))
TORSO_D = Curve(list(zip(_TORSO_Z, [0.108, 0.102, 0.094, 0.096, 0.104, 0.114, 0.116, 0.104, 0.086, 0.068, 0.058])))
TORSO_C = Curve(list(zip(_TORSO_Z, [-0.004, -0.004, -0.008, -0.004, 0.0, 0.004, 0.004, 0.0, -0.002, -0.004, -0.006])))
TORSO_P = 2.5


def torso_point(z, ang, off=0.0, sc=1.0, chest=True):
    """Point on the torso surface at height z and ring angle ang (rad; 0 = left side, 90 = back, 270 = front)."""
    w = TORSO_W(z) * sc + off
    d = TORSO_D(z) * sc + off
    c = TORSO_C(z)
    e = 2.0 / TORSO_P
    x = w * sgnpow(math.cos(ang), e)
    ybl = d * sgnpow(math.sin(ang), e)          # Blender +Y = backward
    if chest and math.sin(ang) < 0:
        # pectorals and the sternum groove; shallow, so cloth/skin stay smooth
        xa = abs(x)
        pec = 0.0085 * math.exp(-((xa - 0.075) / 0.045) ** 2 - ((z - 1.325) / 0.045) ** 2)
        groove = -0.0030 * math.exp(-(x / 0.012) ** 2 - ((z - 1.31) / 0.06) ** 2)
        ybl -= (pec + groove) * (-math.sin(ang)) ** 0.7
    return Vector((x, ybl - c, z))


def build_torso_skin(mb):
    mb.begin("torso", SKIN, density=0.9)
    NT = 24
    z_t = [0.955, 1.00, 1.06, 1.12, 1.18, 1.24, 1.30, 1.355, 1.40, 1.43, 1.452, 1.47]
    rings = []
    for z in z_t:
        rings.append([torso_point(z, 2 * math.pi * j / NT + math.pi * 0.5, off=-0.004) for j in range(NT)])
    mb.loft(rings, lambda i, j, p: torso_weights(to_a(p)), SKIN,
            cap_start=(Vector((0, 0.0, 0.93)), {"hips": 1.0}),
            cap_end=(Vector((0, 0.0, 1.478)), {"neck": 0.5, "chest": 0.5}))
