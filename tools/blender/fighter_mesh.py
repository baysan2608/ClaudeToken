"""Procedural low-poly mesh for the Fourfold training fighter (lofted rings, explicit skin weights).

Everything is authored in the authoring frame A (x left, y forward, z up) and converted to Blender
coordinates on the way in.  Parts are built as lofts of cross-section rings; each vertex receives an
explicit bone-weight dict.  No modifiers are used (geometry is final).
"""
import math
from mathutils import Vector

from fighter_skeleton import A, LEFT_REST, SPINE, SIDES, SHOULDER_Z, HIP_Z

MAT_NAMES = ["skin", "cloth_main", "cloth_accent", "wraps", "hair"]
SKIN, MAIN, ACC, WRAPS, HAIR = range(5)
MAT_COLORS = {
    "skin": (0.78, 0.55, 0.40, 1.0),
    "cloth_main": (0.72, 0.74, 0.78, 1.0),
    "cloth_accent": (0.78, 0.22, 0.16, 1.0),
    "wraps": (0.86, 0.80, 0.66, 1.0),
    "hair": (0.09, 0.07, 0.06, 1.0),
}
DOUBLE_SIDED = {"cloth_accent"}


# ---------------------------------------------------------------------------------------
# small math helpers
# ---------------------------------------------------------------------------------------
def clamp01(x):
    return 0.0 if x < 0 else 1.0 if x > 1 else x


def sstep(e0, e1, x):
    """smoothstep from e0 to e1 (works for e0 > e1 as a falling step)."""
    if e0 == e1:
        return 0.0 if x < e0 else 1.0
    t = clamp01((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


class Curve:
    """Monotone cubic (PCHIP-like) interpolation through (x, y) knots, clamped outside."""

    def __init__(self, pts):
        pts = sorted(pts)
        self.x = [p[0] for p in pts]
        self.y = [p[1] for p in pts]
        n = len(pts)
        d = [(self.y[i + 1] - self.y[i]) / (self.x[i + 1] - self.x[i]) for i in range(n - 1)]
        m = [0.0] * n
        m[0], m[-1] = d[0], d[-1]
        for i in range(1, n - 1):
            m[i] = 0.0 if d[i - 1] * d[i] <= 0 else 2 * d[i - 1] * d[i] / (d[i - 1] + d[i])
        self.m = m

    def __call__(self, x):
        xs, ys, m = self.x, self.y, self.m
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = 0
        while xs[i + 1] < x:
            i += 1
        h = xs[i + 1] - xs[i]
        t = (x - xs[i]) / h
        h00 = 2 * t ** 3 - 3 * t ** 2 + 1
        h10 = t ** 3 - 2 * t ** 2 + t
        h01 = -2 * t ** 3 + 3 * t ** 2
        h11 = t ** 3 - t ** 2
        return h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1]


def sgnpow(v, e):
    return math.copysign(abs(v) ** e, v)


def ring_pts(c, u, v, ru, rv, n, p=2.0, phase=0.0):
    out = []
    e = 2.0 / p
    for k in range(n):
        a = phase + 2 * math.pi * k / n
        out.append(c + u * (ru * sgnpow(math.cos(a), e)) + v * (rv * sgnpow(math.sin(a), e)))
    return out


def frame_from_tangent(t, ref):
    t = t.normalized()
    u = ref - t * ref.dot(t)
    if u.length < 1e-6:
        u = Vector((1, 0, 0)) - t * t.x
    u.normalize()
    v = t.cross(u)
    return u, v, t


def norm_weights(w, keep=4):
    items = [(k, v) for k, v in w.items() if v > 0.012]
    items.sort(key=lambda kv: -kv[1])
    items = items[:keep]
    s = sum(v for _, v in items) or 1.0
    return {k: v / s for k, v in items}


def side_name(base, side):
    return f"{base}.{side}"


# ---------------------------------------------------------------------------------------
# mesh container
# ---------------------------------------------------------------------------------------
class MeshBuilder:
    def __init__(self):
        self.verts = []
        self.weights = []
        self.faces = []
        self.fmat = []

    def vert(self, p, w):
        self.verts.append(Vector(p))
        self.weights.append(norm_weights(w))
        return len(self.verts) - 1

    def face(self, idx, mat):
        self.faces.append(tuple(idx))
        self.fmat.append(mat)

    # -- loft ------------------------------------------------------------------------
    def loft(self, rings, wfn, mat, cap_start=None, cap_end=None, mat_seg=None, close=True,
             check_center=None):
        """rings: list of equal-length lists of Vectors.  wfn(i, j, p) -> weight dict.
        cap_start / cap_end: None, or (apex_point, weight_dict) -> triangle fan to a pole.
        Winding is auto-corrected so faces point away from ring centroids."""
        n = len(rings[0])
        ids = [[self.vert(p, wfn(i, j, p)) for j, p in enumerate(r)] for i, r in enumerate(rings)]
        new_faces = []
        for i in range(len(rings) - 1):
            m = mat if mat_seg is None else mat_seg(i)
            if m is None:
                continue
            for j in range(n):
                j2 = (j + 1) % n
                if not close and j2 == 0:
                    continue
                new_faces.append(([ids[i][j], ids[i][j2], ids[i + 1][j2], ids[i + 1][j]], m))
        if cap_start is not None:
            ap, wd = cap_start
            a = self.vert(ap, wd)
            for j in range(n):
                new_faces.append(([ids[0][(j + 1) % n], ids[0][j], a], mat if mat_seg is None else mat_seg(0)))
        if cap_end is not None:
            ap, wd = cap_end
            a = self.vert(ap, wd)
            last = len(rings) - 1
            for j in range(n):
                new_faces.append(([ids[last][j], ids[last][(j + 1) % n], a],
                                  mat if mat_seg is None else mat_seg(last - 1)))
        # winding check on the first quad
        if new_faces:
            fi, _ = new_faces[0]
            pts = [self.verts[k] for k in fi]
            nrm = (pts[1] - pts[0]).cross(pts[2] - pts[1])
            cen = check_center if check_center is not None else sum(rings[0], Vector()) / n
            if len(rings) > 1 and check_center is None:
                cen = sum(rings[0], Vector()) / n * 0.5 + sum(rings[1], Vector()) / n * 0.5
            fc = sum(pts, Vector()) / len(pts)
            if nrm.dot(fc - cen) < 0:
                new_faces = [(list(reversed(f)), m) for f, m in new_faces]
        for f, m in new_faces:
            self.face(f, m)
        return ids

    def tri_fan_ring(self, ring_ids, apex_id, mat, flip=False):
        n = len(ring_ids)
        for j in range(n):
            a, b = ring_ids[j], ring_ids[(j + 1) % n]
            self.face([b, a, apex_id] if flip else [a, b, apex_id], mat)


# ---------------------------------------------------------------------------------------
# weights helpers
# ---------------------------------------------------------------------------------------
# axial chain along the spine: (joint z, half width of blend zone, bone below, bone above)
_AXIAL = [
    (1.02, 0.065, "hips", "spine"),
    (1.21, 0.065, "spine", "chest"),
    (1.43, 0.045, "chest", "neck"),
    (1.575, 0.04, "neck", "head"),
]


def axial_weights(z, hw_scale=1.0):
    """Weights across hips/spine/chest/neck/head for a point at height z (sequential smooth blends)."""
    bones = ["hips", "spine", "chest", "neck", "head"]
    w = {}
    rem = 1.0
    for k, (zj, hw, _, _) in enumerate(_AXIAL):
        t = sstep(zj - hw * hw_scale, zj + hw * hw_scale, z)
        share = rem * (1 - t)
        w[bones[k]] = share
        rem -= share
    w[bones[-1]] = rem
    return w


def torso_weights(p_a):
    """p_a: point in A frame.  Spine chain + shoulder share near the shoulder tips."""
    x, y, z = p_a
    w = axial_weights(z)
    sh = 0.55 * sstep(0.10, 0.17, abs(x)) * sstep(1.30, 1.38, z) * (1 - sstep(1.43, 1.47, z))
    if sh > 0:
        side = "L" if x > 0 else "R"
        w["chest"] = w.get("chest", 0.0) * (1 - sh)
        w["spine"] = w.get("spine", 0.0) * (1 - sh * 0.5)
        w[f"shoulder.{side}"] = sh
    return w


def to_a(p_blender):
    return (p_blender.x, -p_blender.y, p_blender.z)


# ---------------------------------------------------------------------------------------
# the character
# ---------------------------------------------------------------------------------------
_TORSO_Z = [0.90, 0.97, 1.04, 1.12, 1.20, 1.27, 1.335, 1.385, 1.420, 1.447, 1.470]
_TORSO_W = Curve(list(zip(_TORSO_Z, [0.165, 0.158, 0.148, 0.146, 0.154, 0.172, 0.180, 0.170, 0.128, 0.078, 0.058])))
_TORSO_D = Curve(list(zip(_TORSO_Z, [0.108, 0.102, 0.094, 0.096, 0.104, 0.114, 0.116, 0.104, 0.086, 0.068, 0.058])))
_TORSO_C = Curve(list(zip(_TORSO_Z, [-0.004, -0.004, -0.008, -0.004, 0.0, 0.004, 0.004, 0.0, -0.002, -0.004, -0.006])))
TORSO_P = 2.5


def torso_point(z, ang, off=0.0, sc=1.0):
    """Point on the torso surface at height z and ring angle ang (0 = left side, 90 = back, 270 = front)."""
    w = _TORSO_W(z) * sc + off
    d = _TORSO_D(z) * sc + off
    c = _TORSO_C(z)
    e = 2.0 / TORSO_P
    x = w * sgnpow(math.cos(ang), e)
    ybl = d * sgnpow(math.sin(ang), e)          # Blender +Y = backward
    return Vector((x, ybl - c, z))


def build_character(n_limb=12):
    mb = MeshBuilder()
    Z = Vector((0, 0, 1))
    fwdB = Vector((0, -1, 0))

    # ===================================================================================
    # HEAD, NECK, HAIR
    # ===================================================================================
    head_z = [1.517, 1.540, 1.585, 1.635, 1.685, 1.716, 1.735]
    head_w = Curve(list(zip(head_z, [0.032, 0.058, 0.072, 0.076, 0.073, 0.063, 0.046])))
    head_d = Curve(list(zip(head_z, [0.050, 0.082, 0.098, 0.104, 0.100, 0.087, 0.064])))
    head_c = Curve(list(zip(head_z, [0.050, 0.022, 0.012, 0.008, 0.006, 0.000, -0.004])))   # forward offset (A)
    NH = 14
    ux, vy = Vector((1, 0, 0)), Vector((0, 1, 0))
    rings = []
    for z in head_z:
        c = Vector((0, -head_c(z), z))
        rings.append(ring_pts(c, ux, vy, head_w(z), head_d(z), NH, 2.2))
    mb.loft(rings, lambda i, j, p: {"head": 1.0}, SKIN,
            cap_start=(Vector((0, -0.045, 1.508)), {"head": 0.7, "neck": 0.3}),
            cap_end=(Vector((0, 0.004, 1.744)), {"head": 1.0}))

    # nose (tetra wedge), brow bar, ears
    def tet(pts_a, mat, wd):
        ids = [mb.vert(A(*p), wd) for p in pts_a]
        top, bl, br, tip = ids
        for f in ([top, tip, bl], [top, br, tip], [bl, tip, br], [top, bl, br]):
            mb.face(f, mat)
    tet([(0, 0.108, 1.642), (0.016, 0.106, 1.588), (-0.016, 0.106, 1.588), (0, 0.140, 1.598)], SKIN, {"head": 1.0})
    # make sure nose faces outward: top,tip,bl order checked visually; fix by recomputation below
    brow_rings = []
    for xk in (-0.058, -0.03, 0.0, 0.03, 0.058):
        zc = 1.664
        yc = 0.0 + 0.1 - 0.006 * (abs(xk) / 0.058) ** 2 * 3
        brow_rings.append(ring_pts(A(xk, yc, zc), Vector((0, 0, 1)), Vector((0, -1, 0)), 0.0085, 0.011, 6, 2.0))
    # ring axis is along x: make rings with u=Z, v=-Y... (u x v = Z x (-Y) = X) -> t = +X ok
    mb.loft(brow_rings, lambda i, j, p: {"head": 1.0}, SKIN,
            cap_start=(A(-0.066, 0.092, 1.664), {"head": 1.0}), cap_end=(A(0.066, 0.092, 1.664), {"head": 1.0}))
    for sx in (-1, 1):
        ear_rings = []
        for k, (dx, rs) in enumerate(((0.0, 0.55), (0.007, 1.0), (0.014, 0.55))):
            xk = sx * (0.072 + dx)
            ear_rings.append(ring_pts(A(xk, -0.002, 1.622), Vector((0, 0, 1)), Vector((0, -1, 0)),
                                      0.026 * rs, 0.017 * rs, 6, 2.0))
        mb.loft(ear_rings, lambda i, j, p: {"head": 1.0}, SKIN,
                cap_start=(A(sx * 0.068, -0.002, 1.622), {"head": 1.0}),
                cap_end=(A(sx * 0.092, -0.002, 1.622), {"head": 1.0}))

    # neck
    neck_z = [1.425, 1.470, 1.510, 1.545]
    neck_r = Curve(list(zip(neck_z, [0.058, 0.054, 0.050, 0.050])))
    rings = [ring_pts(A(0, 0.004, z), ux, vy, neck_r(z), neck_r(z) * 0.95, 10, 2.0) for z in neck_z]
    mb.loft(rings, lambda i, j, p: axial_weights(p.z), SKIN)

    # hair cap (shell over the skull; lower edge follows a hairline: high at the forehead, low at the nape)
    NHR = 14
    hair_rings = []
    for k, s_ in enumerate([0.0, 0.30, 0.58, 0.80, 0.93, 1.0]):
        r = []
        for j in range(NHR):
            ang = 2 * math.pi * j / NHR
            zb = 1.645 + 0.058 * (-math.sin(ang))          # front (270 deg) high, back (90 deg) low
            ztop = 1.737
            z = lerp(zb, ztop, s_)
            wv = head_w(z) + 0.011 * (1.0 - 0.3 * s_)
            dv = head_d(z) + 0.013 * (1.0 - 0.3 * s_)
            e = 2.0 / 2.2
            x = wv * sgnpow(math.cos(ang), e)
            yb = dv * sgnpow(math.sin(ang), e)
            r.append(Vector((x, yb - head_c(z), z)))
        hair_rings.append(r)
    mb.loft(hair_rings, lambda i, j, p: {"head": 1.0}, HAIR, cap_end=(Vector((0, 0.004, 1.758)), {"head": 1.0}))

    # ===================================================================================
    # TORSO skin, wrap top, collar trim, sash
    # ===================================================================================
    NT = 20                                      # ring points, front centre = index 15 (270 deg)
    z_t = [0.955, 1.00, 1.06, 1.12, 1.18, 1.24, 1.30, 1.355, 1.40, 1.43, 1.452, 1.47]
    rings = []
    for z in z_t:
        rings.append([torso_point(z, 2 * math.pi * j / NT, off=-0.004) for j in range(NT)])
    mb.loft(rings, lambda i, j, p: torso_weights(to_a(p)), SKIN,
            cap_start=(Vector((0, 0.0, 0.93)), {"hips": 1.0}),
            cap_end=(Vector((0, 0.0, 1.478)), {"neck": 0.5, "chest": 0.5}))

    # wrap top: lower edge z=1.03, upper edge follows a V-neckline at the front
    NS = 7
    top_rings = []
    top_bottom = 1.025
    def top_edge_z(ang):
        # front centre ang=270deg -> deep V; sides/back -> neck base
        d = abs(((ang - 3 * math.pi / 2) + math.pi) % (2 * math.pi) - math.pi)   # angular distance to front
        v = max(0.0, 1.0 - d / math.radians(52))
        return 1.458 - 0.17 * v ** 1.15
    for i in range(NS):
        s = i / (NS - 1)
        r = []
        for j in range(NT):
            ang = 2 * math.pi * j / NT
            zt_ = top_edge_z(ang)
            z = lerp(top_bottom, zt_, s)
            r.append(torso_point(z, ang, off=0.011 * (1 - 0.75 * sstep(0.7, 1.0, s))))
        top_rings.append(r)
    mb.loft(top_rings, lambda i, j, p: torso_weights(to_a(p)), MAIN)
    # collar trim: strip along the upper edge following the V
    trim_rings = []
    for s in (1.0, 0.90):
        r = []
        for j in range(NT):
            ang = 2 * math.pi * j / NT
            zt_ = top_edge_z(ang)
            z = lerp(top_bottom, zt_, s)
            r.append(torso_point(z, ang, off=0.011 * (1 - 0.75 * sstep(0.7, 1.0, s)) + 0.0035))
        trim_rings.append(r)
    mb.loft(trim_rings, lambda i, j, p: torso_weights(to_a(p)), ACC)

    # sash
    sash_levels = [(1.000, 0.012), (1.016, 0.024), (1.06, 0.029), (1.104, 0.024), (1.12, 0.013)]
    rings = []
    for z, off in sash_levels:
        rings.append([torso_point(z, 2 * math.pi * j / NT, off=off) for j in range(NT)])
    mb.loft(rings, lambda i, j, p: torso_weights(to_a(p)), ACC)
    # knot + tails on the left front hip
    kn_ang = math.radians(305)
    kp = torso_point(1.06, kn_ang, off=0.040)
    knot_rings = []
    kt = Vector((0.45, -0.9, 0.0)).normalized()          # outward-ish tangent
    ku, kv, kt_ = frame_from_tangent(kt, Vector((0, 0, 1)))
    for k, rs in enumerate((0.6, 1.0, 0.6)):
        knot_rings.append(ring_pts(kp + kt_ * (k - 1) * 0.022, ku, kv, 0.040 * rs, 0.026 * rs, 8, 2.0))
    mb.loft(knot_rings, lambda i, j, p: torso_weights(to_a(p)), ACC,
            cap_start=(kp - kt_ * 0.04, torso_weights(to_a(kp))), cap_end=(kp + kt_ * 0.04, torso_weights(to_a(kp))))
    # tails: thin double-sheet strips hanging down the left thigh front
    for (dx, length, wdt, drift) in ((0.014, 0.30, 0.034, 0.015), (-0.020, 0.21, 0.030, -0.01)):
        rows = 5
        top = kp + Vector((dx, -0.012, -0.014))
        sheets = []
        for sheet_off in (-0.0022, 0.0022):
            ids = []
            for r_ in range(rows):
                s_ = r_ / (rows - 1)
                cx = top.x + drift * s_ + 0.02 * s_
                cyb = top.y - 0.012 * s_ - 0.004 * s_ ** 2 + sheet_off
                cz = top.z - length * s_
                half = wdt / 2 * (1 + 0.15 * s_)
                wd = {"hips": 1.0 - 0.6 * s_, "thigh.L": 0.6 * s_}
                ids.append((mb.vert(Vector((cx - half, cyb, cz)), wd), mb.vert(Vector((cx + half, cyb, cz)), wd)))
            sheets.append(ids)
        # front sheet faces -Y (forward), back sheet faces +Y
        for r_ in range(rows - 1):
            a_, b_ = sheets[0][r_]
            c_, d_ = sheets[0][r_ + 1]
            mb.face([a_, c_, d_, b_], ACC)
            a_, b_ = sheets[1][r_]
            c_, d_ = sheets[1][r_ + 1]
            mb.face([a_, b_, d_, c_], ACC)

    # ===================================================================================
    # PELVIS BAND + TROUSER LEGS
    # ===================================================================================
    band_levels = [(1.065, 0.158, 0.103), (1.02, 0.162, 0.104), (0.97, 0.174, 0.110), (0.92, 0.185, 0.115),
                   (0.865, 0.186, 0.115), (0.82, 0.170, 0.108)]
    rings = []
    for z, hw_, hd_ in band_levels:
        r = []
        for j in range(NT):
            ang = 2 * math.pi * j / NT
            e = 2.0 / TORSO_P
            r.append(Vector((hw_ * sgnpow(math.cos(ang), e), hd_ * sgnpow(math.sin(ang), e) - _TORSO_C(min(z, 1.0)), z)))
        rings.append(r)

    def band_w(i, j, p):
        z = p.z
        t_leg = sstep(0.945, 0.805, z)
        sf = sstep(-0.035, 0.035, p.x)
        w = {"hips": 1.0 - t_leg}
        w["thigh.L"] = t_leg * sf
        w["thigh.R"] = t_leg * (1 - sf)
        return w
    # winding: rings go top -> bottom, auto-correct handles it
    mb.loft(rings, band_w, MAIN, cap_end=(Vector((0, 0.0, 0.80)), {"thigh.L": 0.5, "thigh.R": 0.5}))

    for side in SIDES:
        sx = 1 if side == "L" else -1
        L = LEFT_REST
        hip = Vector(L["hip"]); knee = Vector(L["knee"]); ank = Vector(L["ankle"])
        def leg_pt(z):
            if z >= knee.z:
                t = (z - hip.z) / (knee.z - hip.z)
                p = hip + (knee - hip) * t
            else:
                t = (z - knee.z) / (ank.z - knee.z)
                p = knee + (ank - knee) * t
            return Vector((p.x * sx, p.y, p.z))
        # ---- trousers
        tz = [0.985, 0.93, 0.85, 0.75, 0.65, 0.565, 0.515, 0.475, 0.425, 0.385, 0.35, 0.335]
        tr = Curve([(0.985, 0.088), (0.93, 0.092), (0.85, 0.092), (0.75, 0.088), (0.65, 0.082),
                    (0.565, 0.075), (0.515, 0.073), (0.475, 0.074), (0.425, 0.077), (0.385, 0.079),
                    (0.35, 0.073), (0.335, 0.066)])
        rings = []
        pts = [leg_pt(z) for z in tz]
        for k, z in enumerate(tz):
            t_ = (pts[min(k + 1, len(tz) - 1)] - pts[max(k - 1, 0)]) * Vector((1, -1, 1))
            u, v, t = frame_from_tangent(-t_, Vector((0, -1, 0)))
            c = pts[k] * Vector((1, -1, 1))        # A -> blender (y flip)
            rings.append(ring_pts(c, u, v, tr(z), tr(z) * 0.98, n_limb, 2.0))
        thigh_b, shin_b = f"thigh.{side}", f"shin.{side}"
        def trw(i, j, p, thigh_b=thigh_b, shin_b=shin_b):
            z = p.z
            w = {}
            hipw = sstep(0.84, 0.96, z)              # hips share (1 at top)
            kneet = sstep(knee.z - 0.06, knee.z + 0.06, z)   # 1 above knee
            w["hips"] = hipw
            w[thigh_b] = (1 - hipw) * kneet
            w[shin_b] = (1 - hipw) * (1 - kneet)
            return w
        mb.loft(rings, trw, MAIN, cap_end=(Vector((c.x, c.y, tz[-1] - 0.012)), {shin_b: 1.0}))
        # fix: loft start at top is open (hidden inside the band)

        # ---- shin wraps
        wz = [0.092, 0.118, 0.148, 0.18, 0.212, 0.244, 0.276, 0.308, 0.34, 0.362]
        wr = Curve([(0.092, 0.040), (0.13, 0.043), (0.19, 0.050), (0.25, 0.057), (0.30, 0.061), (0.362, 0.063)])
        rings = []
        for k, z in enumerate(wz):
            c = leg_pt(z) * Vector((1, -1, 1))
            ridge = 1.0 + (0.08 if k % 2 else 0.0)
            t_ = (leg_pt(z + 0.02) - leg_pt(z - 0.02)) * Vector((1, -1, 1))
            u, v, t = frame_from_tangent(t_, Vector((0, -1, 0)))
            rings.append(ring_pts(c, u, v, wr(z) * ridge, wr(z) * ridge * 0.97, 10, 2.0))
        def ww(i, j, p, shin_b=shin_b, foot_b=f"foot.{side}"):
            f = 0.55 * sstep(0.135, 0.085, p.z)
            return {shin_b: 1 - f, foot_b: f}
        mb.loft(rings, ww, WRAPS, cap_start=(Vector((sx * 0.09, 0.005, 0.085)), {f"foot.{side}": 1.0}))

        # ---- shoe
        shoe_y = [-0.085, -0.075, -0.035, 0.010, 0.060, 0.110, 0.150, 0.195, 0.228]
        top_h = Curve([(-0.085, 0.060), (-0.075, 0.080), (-0.035, 0.112), (0.010, 0.118), (0.060, 0.082),
                       (0.110, 0.060), (0.150, 0.052), (0.195, 0.044), (0.228, 0.030)])
        wid = Curve([(-0.085, 0.026), (-0.075, 0.036), (-0.035, 0.043), (0.010, 0.046), (0.060, 0.048),
                     (0.110, 0.052), (0.150, 0.051), (0.195, 0.044), (0.228, 0.026)])
        shu = Vector((1, 0, 0)); shv = Vector((0, 0, 1))
        rings = []
        for y in shoe_y:
            h = top_h(y)
            c = Vector((sx * 0.09, -y, h / 2))
            rings.append(ring_pts(c, shu, shv, wid(y), h / 2, 10, 2.6))
        foot_b, toe_b = f"foot.{side}", f"toe.{side}"
        def shw(i, j, p, foot_b=foot_b, toe_b=toe_b):
            ya = -p.y
            t = sstep(0.105, 0.155, ya)
            return {foot_b: 1 - t, toe_b: t}
        tip_c = Vector((sx * 0.09, -0.238, 0.014))
        heel_c = Vector((sx * 0.09, 0.090, 0.034))
        # loft goes along -Y (forward): ring axis t = -Y; (u=X, v=Z): u x v = -Y ok
        mb.loft(rings, shw, ACC, cap_start=(heel_c, {foot_b: 1.0}), cap_end=(tip_c, {toe_b: 1.0}))

    # ===================================================================================
    # ARMS: skin tube, sleeve, forearm wrap, hand
    # ===================================================================================
    for side in SIDES:
        sx = 1 if side == "L" else -1
        L = LEFT_REST

        def M(p):
            return Vector((p[0] * sx, p[1], p[2]))
        sho = M(L["shoulder_tail"]); elb = M(L["elbow"]); wri = M(L["wrist"]); tip = M(L["fingertip"])
        l1 = (elb - sho).length
        l2 = (wri - elb).length
        d1 = (elb - sho).normalized()
        d2 = (wri - elb).normalized()

        def arm_pt(s):
            if s <= l1:
                return sho + d1 * s
            return elb + d2 * (s - l1)

        def arm_tan(s, eps=0.012):
            a = arm_pt(max(s - eps, -0.1))
            b = arm_pt(s + eps)
            return (b - a)

        ub, fb, shb, hb = f"upper_arm.{side}", f"forearm.{side}", f"shoulder.{side}", f"hand.{side}"

        def arm_w(s):
            """weights for a point at arc position s along the arm path."""
            w = {}
            sh_t = sstep(-0.01, 0.075, s)            # 0 = shoulder/chest influence, 1 = upper arm
            el_t = sstep(l1 - 0.055, l1 + 0.055, s)  # 0 = upper arm, 1 = forearm
            wr_t = sstep(l1 + l2 - 0.03, l1 + l2 + 0.02, s)
            w[shb] = (1 - sh_t) * 0.55
            w["chest"] = (1 - sh_t) * 0.45
            w[ub] = sh_t * (1 - el_t)
            w[fb] = sh_t * el_t * (1 - wr_t)
            w[hb] = wr_t * el_t
            return w

        FLIP = Vector((1, -1, 1))

        def arm_ring(s, r, aspect=0.94, ridge=1.0, n=n_limb, shift=Vector()):
            tn = arm_tan(max(s, 0.0) if s < 0 else s) * FLIP
            u, v, t = frame_from_tangent(tn, Vector((0, -1, 0)))
            c = arm_pt(s) * FLIP + shift
            return ring_pts(c, u, v, r * ridge, r * ridge * aspect, n, 2.0), c, t

        arm_s = [-0.03, 0.0, 0.045, 0.10, 0.17, 0.235, l1, l1 + 0.045, l1 + 0.10, l1 + 0.16, l1 + 0.21, l1 + l2 + 0.004]
        arm_r = Curve([(-0.03, 0.050), (0.0, 0.057), (0.05, 0.058), (0.12, 0.051), (0.2, 0.046), (l1, 0.039),
                       (l1 + 0.05, 0.043), (l1 + 0.12, 0.040), (l1 + 0.2, 0.033), (l1 + l2 + 0.004, 0.029)])
        rr = [arm_ring(s_, arm_r(s_)) for s_ in arm_s]
        mb.loft([r_[0] for r_ in rr], lambda i, j, p, arm_s=arm_s: arm_w(arm_s[i]), SKIN,
                cap_start=(rr[0][1] - rr[0][2] * 0.03, arm_w(-0.03)))

        # sleeve over the shoulder
        sl_s = [-0.035, 0.0, 0.05, 0.10, 0.135]
        sl_r = Curve([(-0.035, 0.060), (0.0, 0.071), (0.05, 0.074), (0.10, 0.071), (0.135, 0.073)])
        rr = [arm_ring(s_, sl_r(s_), 0.95, shift=Vector((sx * 0.004 * sstep(0.04, 0.13, s_), 0, 0))) for s_ in sl_s]
        mb.loft([r_[0] for r_ in rr], lambda i, j, p, sl_s=sl_s: arm_w(sl_s[i]), MAIN,
                cap_start=(rr[0][1] - rr[0][2] * 0.045, arm_w(-0.035)))

        # forearm wrap
        fw_s = [l1 + 0.10, l1 + 0.13, l1 + 0.16, l1 + 0.19, l1 + 0.22, l1 + 0.25, l1 + l2 + 0.006]
        rr = [arm_ring(s_, arm_r(s_) + 0.0055, 0.94, ridge=1.0 + (0.08 if k % 2 else 0.0)) for k, s_ in enumerate(fw_s)]
        mb.loft([r_[0] for r_ in rr], lambda i, j, p, fw_s=fw_s: arm_w(fw_s[i]), WRAPS)

        # hand: mitten (wraps) + fingers (skin) + thumb
        ht = (tip - wri)
        hl = ht.length
        ht_b = ht * Vector((1, -1, 1))
        hu, hv, hT = frame_from_tangent(ht_b, Vector((0, -1, 0)))      # u: thumb side (forward)
        hand_s = [(-0.005, 0.030, 0.021), (0.35, 0.040, 0.022), (0.62, 0.043, 0.022), (0.80, 0.041, 0.019), (1.0, 0.034, 0.015)]
        rings = []
        for s_, wv, tv in hand_s:
            c = (wri * Vector((1, -1, 1))) + hT * (s_ * hl)
            rings.append((ring_pts(c, hu, hv, wv, tv, 10, 2.6), s_))
        def hw(i, j, p, hb=hb, fb=fb):
            s_ = rings[i][1]
            f = 0.5 * (1 - sstep(0.0, 0.2, s_))
            return {hb: 1 - f, fb: f}
        mb.loft([r_[0] for r_ in rings], hw, WRAPS, mat_seg=lambda i: (WRAPS if i < 2 else SKIN),
                cap_end=((wri * Vector((1, -1, 1))) + hT * (hl * 1.06) , {hb: 1.0}))
        # thumb
        th_base = (wri * Vector((1, -1, 1))) + hT * (0.22 * hl) + hu * 0.030 + hv * 0.0
        th_dir = (hT * 0.78 + hu * 0.45).normalized()
        thu, thv, tht = frame_from_tangent(th_dir, hv)
        rings2 = []
        for k, rs in enumerate((1.0, 0.9, 0.7)):
            rings2.append(ring_pts(th_base + tht * (0.026 * k), thu, thv, 0.016 * rs, 0.014 * rs, 6, 2.0))
        mb.loft(rings2, lambda i, j, p, hb=hb: {hb: 1.0}, SKIN, mat_seg=lambda i: (WRAPS if i == 0 else SKIN),
                cap_end=(th_base + tht * 0.062, {hb: 1.0}))
    return mb
