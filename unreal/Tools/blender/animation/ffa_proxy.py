"""Fourfold animation toolkit - proxy body for previews: an articulated "artist mannequin" built from tapered
capsules, ball joints, a torso of stacked ellipsoids, a head with a nose and hair cap (reads facing), mitten palms with
finger / thumb tubes, and shoes.  Every part is rigidly bound to one bone of the frozen rig (rest armature space), so
the same mesh can be posed by FK in numpy (ffa_render) or skinned in Blender (ffa_export.proxy_object).

Colours: left limbs carry a cool tint, right limbs a warm tint, so lead / rear sides read in every view.
"""
import math

import numpy as np

import ffa_rig as rig
from ffa_math import norm

SKIN = (0.80, 0.60, 0.46)
CLOTH = (0.26, 0.30, 0.38)
CLOTH_L = (0.24, 0.33, 0.46)
CLOTH_R = (0.42, 0.28, 0.26)
SASH = (0.72, 0.56, 0.30)
TROUSER = (0.30, 0.31, 0.34)
TROUSER_L = (0.27, 0.32, 0.42)
TROUSER_R = (0.40, 0.30, 0.30)
SHOE = (0.16, 0.15, 0.14)
SOLE = (0.85, 0.82, 0.76)
HAIR = (0.10, 0.08, 0.07)
WRAP = (0.86, 0.80, 0.66)


class Part:
    __slots__ = ("bone", "verts", "faces", "color", "skin")

    def __init__(self, bone, verts, faces, color, skin=None):
        self.bone = bone
        self.verts = np.asarray(verts, float)
        self.faces = np.asarray(faces, int)
        self.color = color
        self.skin = skin          # optional per-vertex (bone_a, bone_b, weight_b) for a smoothly skinned part


# torso cross-sections at rest: (height, half width, half depth, centre y (Blender, - = forward), colour)
TORSO = [(0.79, 0.150, 0.100, 0.010), (0.84, 0.168, 0.110, 0.012), (0.91, 0.176, 0.114, 0.010),
         (0.99, 0.164, 0.106, 0.006), (1.07, 0.146, 0.098, 0.004), (1.15, 0.148, 0.100, 0.002),
         (1.23, 0.158, 0.106, -0.004), (1.31, 0.172, 0.112, -0.010), (1.38, 0.184, 0.114, -0.012),
         (1.44, 0.186, 0.106, -0.006), (1.485, 0.150, 0.090, 0.004), (1.515, 0.085, 0.065, 0.006),
         (1.53, 0.055, 0.050, 0.004)]
_TORSO_BONES = ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]


def _torso_weights(z):
    mids = [((rig.HEAD[b][2] + rig.TAIL[b][2]) / 2.0, b) for b in _TORSO_BONES]
    if z <= mids[0][0]:
        return mids[0][1], mids[0][1], 0.0
    if z >= mids[-1][0]:
        return mids[-1][1], mids[-1][1], 0.0
    for (z0, b0), (z1, b1) in zip(mids, mids[1:]):
        if z0 <= z <= z1:
            u = (z - z0) / (z1 - z0)
            return b0, b1, u * u * (3 - 2 * u)
    return mids[-1][1], mids[-1][1], 0.0


def torso_loft(seg=18):
    verts, skin, faces = [], [], []
    for (z, hw, hd, cy) in TORSO:
        b0, b1, w = _torso_weights(z)
        for j in range(seg):
            ph = 2 * math.pi * j / seg
            c, s_ = math.cos(ph), math.sin(ph)
            # superellipse-ish section: flatter front / back
            x = hw * math.copysign(abs(c) ** 0.85, c)
            y = hd * math.copysign(abs(s_) ** 0.9, s_)
            verts.append((x, cy + y, z))
            skin.append((b0, b1, w))
    nr = len(TORSO)
    for i in range(nr - 1):
        for j in range(seg):
            a0 = i * seg + j
            a1 = i * seg + (j + 1) % seg
            c0 = (i + 1) * seg + j
            c1 = (i + 1) * seg + (j + 1) % seg
            faces.append((a0, a1, c1))
            faces.append((a0, c1, c0))
    bot = len(verts)
    verts.append((0.0, TORSO[0][3], TORSO[0][0] - 0.02))
    skin.append(("pelvis", "pelvis", 0.0))
    top = len(verts)
    verts.append((0.0, TORSO[-1][3], TORSO[-1][0] + 0.01))
    skin.append(("spine_05", "spine_05", 0.0))
    for j in range(seg):
        faces.append((bot, (j + 1) % seg, j))
        faces.append((top, (nr - 1) * seg + j, (nr - 1) * seg + (j + 1) % seg))
    v = np.asarray(verts, float)
    centre = np.array([0.0, 0.0, 0.0])
    fx = []
    for f in faces:
        a, b, c = v[list(f)]
        n = np.cross(b - a, c - a)
        mid = (a + b + c) / 3.0
        centre = np.array([0.0, mid[1] * 0 + 0.0, mid[2]])
        fx.append((f[0], f[2], f[1]) if float(np.dot(n, mid - centre)) < 0 else tuple(f))
    return Part("pelvis", v, fx, CLOTH, skin=skin)


def _ring_frame(axis):
    axis = norm(axis)
    ref = np.array([0.0, 0.0, 1.0]) if abs(axis[2]) < 0.9 else np.array([0.0, -1.0, 0.0])
    u = norm(np.cross(axis, ref))
    v = np.cross(axis, u)
    return u, v


def capsule(a, b, ra, rb, seg=10, rings=3, cap=3, squash=1.0, up=None):
    """Tapered capsule from point a (radius ra) to b (radius rb); squash scales the cross-section along `up`."""
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    axis = norm(b - a)
    if up is None:
        u, v = _ring_frame(axis)
    else:
        v = norm(np.asarray(up, float) - axis * float(np.dot(up, axis)))
        u = np.cross(v, axis)
    verts = []
    profile = []
    # hemisphere at a
    for i in range(cap, 0, -1):
        th = (math.pi / 2) * i / cap
        profile.append((-ra * math.sin(th), ra * math.cos(th)))
    L = float(np.linalg.norm(b - a))
    for i in range(rings + 1):
        t = i / rings
        profile.append((t * L, ra + (rb - ra) * t))
    for i in range(1, cap + 1):
        th = (math.pi / 2) * i / cap
        profile.append((L + rb * math.sin(th), rb * math.cos(th)))
    verts.append(a - axis * ra)
    for (z, r) in profile:
        for j in range(seg):
            ph = 2 * math.pi * j / seg
            verts.append(a + axis * z + u * (r * math.cos(ph)) + v * (r * math.sin(ph) * squash))
    verts.append(b + axis * rb)
    faces = []
    nr = len(profile)
    for j in range(seg):
        faces.append((0, 1 + (j + 1) % seg, 1 + j))
    for i in range(nr - 1):
        for j in range(seg):
            a0 = 1 + i * seg + j
            a1 = 1 + i * seg + (j + 1) % seg
            b0 = 1 + (i + 1) * seg + j
            b1 = 1 + (i + 1) * seg + (j + 1) % seg
            faces.append((a0, a1, b1))
            faces.append((a0, b1, b0))
    last = len(verts) - 1
    base = 1 + (nr - 1) * seg
    for j in range(seg):
        faces.append((last, base + j, base + (j + 1) % seg))
    return verts, faces


def ellipsoid(c, rx, ry, rz, seg=12, rings=7, R=None):
    c = np.asarray(c, float)
    R = np.eye(3) if R is None else R
    verts = [c + R @ np.array([0.0, 0.0, -rz])]
    for i in range(1, rings):
        th = math.pi * i / rings - math.pi / 2
        for j in range(seg):
            ph = 2 * math.pi * j / seg
            p = np.array([rx * math.cos(th) * math.cos(ph), ry * math.cos(th) * math.sin(ph), rz * math.sin(th)])
            verts.append(c + R @ p)
    verts.append(c + R @ np.array([0.0, 0.0, rz]))
    faces = []
    for j in range(seg):
        faces.append((0, 1 + (j + 1) % seg, 1 + j))
    for i in range(rings - 2):
        for j in range(seg):
            a0 = 1 + i * seg + j
            a1 = 1 + i * seg + (j + 1) % seg
            b0 = 1 + (i + 1) * seg + j
            b1 = 1 + (i + 1) * seg + (j + 1) % seg
            faces.append((a0, a1, b1))
            faces.append((a0, b1, b0))
    last = len(verts) - 1
    base = 1 + (rings - 2) * seg
    for j in range(seg):
        faces.append((last, base + j, base + (j + 1) % seg))
    return verts, faces


def box(center, axes, half):
    """Oriented box: axes = 3 unit column vectors (3x3), half = half extents."""
    c = np.asarray(center, float)
    verts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                verts.append(c + axes[:, 0] * sx * half[0] + axes[:, 1] * sy * half[1] + axes[:, 2] * sz * half[2])
    f = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4),
         (1, 5, 7), (1, 7, 3)]
    return verts, f


def _fix_winding(verts, faces, inside):
    """Orient every triangle outward (away from `inside`)."""
    verts = np.asarray(verts, float)
    out = []
    for f in faces:
        a, b, c = verts[list(f)]
        n = np.cross(b - a, c - a)
        if float(np.dot(n, (a + b + c) / 3.0 - inside)) < 0:
            out.append((f[0], f[2], f[1]))
        else:
            out.append(tuple(f))
    return out


def _part(parts, bone, vf, color, inside=None):
    v, f = vf
    v = np.asarray(v, float)
    if inside is None:
        inside = v.mean(axis=0)
    parts.append(Part(bone, v, _fix_winding(v, f, inside), color))


def build():
    H, T, R = rig.HEAD, rig.TAIL, rig.REST
    parts = []
    # ---------------------------------------------------------------- torso
    parts.append(torso_loft())
    _part(parts, "pelvis", ellipsoid(H["pelvis"] + np.array([0, 0.002, 0.045]), 0.166, 0.116, 0.032, seg=16, rings=5), SASH)
    # ---------------------------------------------------------------- neck + head
    _part(parts, "neck_01", capsule(H["neck_01"], T["neck_02"], 0.05, 0.047, seg=10), SKIN)
    hc = H["head"] + np.array([0, 0.005, 0.095])
    _part(parts, "head", ellipsoid(hc, 0.083, 0.098, 0.112, seg=14, rings=9), SKIN)
    _part(parts, "head", ellipsoid(hc + np.array([0, 0.018, 0.03]), 0.088, 0.093, 0.095, seg=14, rings=8), HAIR)
    _part(parts, "head", ellipsoid(H["head"] + np.array([0, 0.06, 0.15]), 0.035, 0.035, 0.035, seg=8, rings=5), HAIR)
    nose = [hc + np.array([0, -0.095, 0.005]), hc + np.array([0.02, -0.088, -0.035]),
            hc + np.array([-0.02, -0.088, -0.035]), hc + np.array([0, -0.125, -0.03])]
    _part(parts, "head", (nose, [(0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2)]), (0.86, 0.55, 0.42),
          inside=hc)
    for sx in (1, -1):
        _part(parts, "head", ellipsoid(hc + np.array([0.034 * sx, -0.083, 0.012]), 0.014, 0.008, 0.008, seg=6, rings=4),
              (0.08, 0.08, 0.1))
    # ---------------------------------------------------------------- limbs
    for s in rig.SIDES:
        cl = CLOTH_L if s == "l" else CLOTH_R
        tr = TROUSER_L if s == "l" else TROUSER_R
        _part(parts, "clavicle_" + s, capsule(H["clavicle_" + s] + np.array([0, 0.01, 0.01]), T["clavicle_" + s],
                                              0.045, 0.06, seg=10), cl)
        _part(parts, "upperarm_" + s, capsule(H["upperarm_" + s], T["upperarm_" + s], 0.058, 0.045, seg=10), cl)
        _part(parts, "lowerarm_" + s, capsule(H["lowerarm_" + s], T["lowerarm_" + s] - norm(T["lowerarm_" + s] - H["lowerarm_" + s]) * 0.01,
                                              0.044, 0.032, seg=10), WRAP)
        # hand: mitten palm (on the hand bone), finger tubes per phalanx, thumb tubes
        hR = R["hand_" + s]
        y, z = hR[:, 1], hR[:, 2]
        x = hR[:, 0]
        pc = H["hand_" + s] + y * 0.052
        _part(parts, "hand_" + s, box(pc, np.stack([x, y, z], 1), (0.042, 0.05, 0.015)), WRAP)
        for f in ("index", "middle", "ring", "pinky"):
            for i, rr in ((1, 0.0105), (2, 0.0095), (3, 0.0085)):
                b = f"{f}_0{i}_{s}"
                _part(parts, b, capsule(H[b], T[b], rr, rr * 0.92, seg=6, rings=1, cap=2), SKIN)
            mb = f"{f}_metacarpal_{s}"
            _part(parts, mb, capsule(H[mb] + (T[mb] - H[mb]) * 0.55, T[mb], 0.011, 0.011, seg=6, rings=1, cap=2), SKIN)
        for i, rr in ((1, 0.014), (2, 0.012), (3, 0.0105)):
            b = f"thumb_0{i}_{s}"
            _part(parts, b, capsule(H[b], T[b], rr, rr * 0.9, seg=6, rings=1, cap=2), SKIN)
        # legs
        _part(parts, "thigh_" + s, capsule(H["thigh_" + s], T["thigh_" + s], 0.085, 0.06, seg=12), tr)
        _part(parts, "calf_" + s, capsule(H["calf_" + s], T["calf_" + s], 0.058, 0.04, seg=12), tr)
        # shoe: wedge from heel to ball on the foot bone, toe box on the ball bone, light sole
        fR = R["foot_" + s]
        ank = H["foot_" + s]
        heel = rig.side_point(rig.HEEL_L, s)
        ball = H["ball_" + s]
        fwd = norm(np.array([ball[0] - ank[0], ball[1] - ank[1], 0.0]))
        lat = norm(np.cross(fwd, np.array([0.0, 0.0, 1.0])))
        ax = np.stack([lat, fwd, np.array([0.0, 0.0, 1.0])], 1)
        mid = np.array([(heel[0] + ball[0]) / 2, (heel[1] + ball[1]) / 2, 0.045])
        L = float(np.linalg.norm((ball - heel)[:2]))
        _part(parts, "foot_" + s, box(mid, ax, (0.045, L / 2 + 0.01, 0.043)), SHOE)
        _part(parts, "foot_" + s, box(mid + np.array([0, 0, -0.038]), ax, (0.048, L / 2 + 0.014, 0.007)), SOLE)
        toe = T["ball_" + s]
        tm = np.array([(ball[0] + toe[0]) / 2, (ball[1] + toe[1]) / 2, 0.022])
        Lt = float(np.linalg.norm((toe - ball)[:2]))
        _part(parts, "ball_" + s, box(tm, ax, (0.044, Lt / 2 + 0.012, 0.022)), SHOE)
        _part(parts, "ball_" + s, box(tm + np.array([0, 0, -0.016]), ax, (0.047, Lt / 2 + 0.014, 0.007)), SOLE)
    # ---------------------------------------------------------------- ball joints
    for s in rig.SIDES:
        cl = CLOTH_L if s == "l" else CLOTH_R
        tr = TROUSER_L if s == "l" else TROUSER_R
        _part(parts, "upperarm_" + s, ellipsoid(H["upperarm_" + s], 0.062, 0.062, 0.062, seg=10, rings=6), cl)
        _part(parts, "lowerarm_" + s, ellipsoid(H["lowerarm_" + s], 0.046, 0.046, 0.046, seg=8, rings=5), cl)
        _part(parts, "calf_" + s, ellipsoid(H["calf_" + s], 0.062, 0.062, 0.062, seg=10, rings=6), tr)
        _part(parts, "thigh_" + s, ellipsoid(H["thigh_" + s], 0.085, 0.085, 0.085, seg=10, rings=6), tr)
    return parts


_CACHE = {}


def mesh():
    """Concatenated proxy: (verts (V,3) rest, faces (F,3), bone A / bone B index per vertex, weight of B, bone names,
    face colours (F,3))."""
    if "m" in _CACHE:
        return _CACHE["m"]
    parts = build()
    used = set()
    for p in parts:
        used.add(p.bone)
        if p.skin:
            for a, b, _ in p.skin:
                used.update((a, b))
    names = sorted(used, key=rig.BONES.index)
    bi = {n: i for i, n in enumerate(names)}
    V, Fc, B0, B1, Wb, C = [], [], [], [], [], []
    off = 0
    for p in parts:
        V.append(p.verts)
        Fc.append(p.faces + off)
        if p.skin:
            B0.append(np.array([bi[a] for a, _, _ in p.skin]))
            B1.append(np.array([bi[b] for _, b, _ in p.skin]))
            Wb.append(np.array([w for _, _, w in p.skin], float))
        else:
            B0.append(np.full(len(p.verts), bi[p.bone]))
            B1.append(np.full(len(p.verts), bi[p.bone]))
            Wb.append(np.zeros(len(p.verts)))
        C.append(np.tile(np.array(p.color, float), (len(p.faces), 1)))
        off += len(p.verts)
    m = (np.concatenate(V), np.concatenate(Fc), np.concatenate(B0), np.concatenate(B1), np.concatenate(Wb), names,
         np.concatenate(C))
    _CACHE["m"] = m
    return m


def posed_vertices(W, Hd):
    """World vertices of the proxy for one FK result (linear blend of at most two bones per vertex)."""
    V, F, B0, B1, Wb, names, C = mesh()
    Ms = np.stack([W[n] @ rig.REST[n].T for n in names])          # (nb,3,3)
    hs = np.stack([rig.HEAD[n] for n in names])
    Hs = np.stack([Hd[n] for n in names])
    pa = np.einsum("vij,vj->vi", Ms[B0], V - hs[B0]) + Hs[B0]
    pb = np.einsum("vij,vj->vi", Ms[B1], V - hs[B1]) + Hs[B1]
    return pa * (1.0 - Wb[:, None]) + pb * Wb[:, None]
