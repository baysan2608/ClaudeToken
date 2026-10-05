"""Arms (bare shoulders/upper arms, short cap sleeves), wrapped forearms and articulated hands (palm, wrap, four fingers in
two grouped chains plus a two-bone thumb).  Everything is authored on the left side in the A frame and mirrored.

Vertex colour (r, g): r = surface variant (0 default), g = wear / dirt mask (see fighter_textures.py).
Bones used: shoulder.X, upper_arm.X, forearm.X, hand.X, thumb_1/2.X, finger_im_1/2.X, finger_rp_1/2.X.
"""
import math

from mathutils import Vector

from fighter_mesh import (ACC, Curve, MAIN, SKIN, WRAPS, clamp01, frame_from_tangent, lerp, ring_pts, sgnpow, sstep)
from fighter_skeleton import A, LEFT_REST, SIDES, hand_frame, hand_pts

FWD_B = Vector((0, -1, 0))                 # forward in Blender coordinates


def _mirror(v, sx):
    return Vector((v.x * sx, v.y, v.z))


def to_b(v):
    """A-frame vector -> Blender coordinates."""
    return Vector((v.x, -v.y, v.z))


def to_a_v(p):
    return Vector((p.x, -p.y, p.z))


class ArmGeo:
    """Rest geometry of one arm (A frame)."""

    def __init__(self, side):
        self.side = side
        self.sx = 1 if side == "L" else -1
        L = LEFT_REST
        self.S = _mirror(Vector(L["shoulder_tail"]), self.sx)
        self.E = _mirror(Vector(L["elbow"]), self.sx)
        self.W = _mirror(Vector(L["wrist"]), self.sx)
        W, hd, u, n, hl = hand_frame()
        self.hd = _mirror(hd, self.sx)
        self.u = _mirror(u, self.sx)
        self.n = _mirror(n, self.sx)
        self.hl = hl
        self.ua_len = (self.E - self.S).length
        self.fa_len = (self.W - self.E).length

    def upper(self, t):
        """point at fraction t along the upper arm (t<0 above the joint, extrapolated along the same line)"""
        return self.S + (self.E - self.S) * t

    def fore(self, t):
        return self.E + (self.W - self.E) * t

    def hp(self, s, uo=0.0, no=0.0):
        """hand point: s along the hand axis from the wrist, uo thumb-side offset, no palm-normal offset"""
        return self.W + self.hd * s + self.u * uo + self.n * no


def _along(a, b, p):
    d = b - a
    return (p - a).dot(d) / d.dot(d)


def arm_weights(g, pa):
    """Weights of an arm-surface point (A frame) over shoulder / chest / upper_arm / forearm / hand."""
    s = g.side
    du = _along(g.S, g.E, pa) * g.ua_len               # metres down the upper arm from the shoulder joint
    tf = _along(g.E, g.W, pa)
    df = tf * g.fa_len                                 # metres down the forearm from the elbow
    sh = sstep(-0.075, 0.07, du)                       # shoulder bone -> upper arm, smooth across the deltoid
    signed = df if tf > 0 else du - g.ua_len
    ke = sstep(-0.05, 0.05, signed)                    # upper arm -> forearm across the elbow
    kw = sstep(-0.03, 0.03, df - g.fa_len)             # forearm -> hand at the wrist
    w = {f"shoulder.{s}": (1 - sh) * 0.75, "chest": (1 - sh) * 0.25}
    w[f"upper_arm.{s}"] = sh * (1 - ke)
    w[f"forearm.{s}"] = sh * ke * (1 - kw)
    w[f"hand.{s}"] = sh * ke * kw
    return w


# ---------------------------------------------------------------------------------------------------------------------
# loft helper that works in the A frame
# ---------------------------------------------------------------------------------------------------------------------
def loft_a(mb, rings_a, wfn_a, mat, cap_start=None, cap_end=None, close=True, colfn=None, flip=False, ucoord=None, vcoord=None):
    rings = [[to_b(p) for p in r] for r in rings_a]
    cs = None if cap_start is None else (to_b(cap_start[0]), cap_start[1])
    ce = None if cap_end is None else (to_b(cap_end[0]), cap_end[1])
    return mb.loft(rings, lambda i, j, p: wfn_a(to_a_v(p)), mat, cap_start=cs, cap_end=ce, close=close,
                   colfn=(None if colfn is None else (lambda i, j, p: colfn(i, j, to_a_v(p)))), flip=flip,
                   ucoord=ucoord, vcoord=vcoord)


def ellipse_ring(c, t, ref, ru, rv, n, p=2.0, phase=0.0, shift=(0.0, 0.0)):
    u, v, _ = frame_from_tangent(t, ref)
    return ring_pts(c + u * shift[0] + v * shift[1], u, v, ru, rv, n, p, phase)


# ---------------------------------------------------------------------------------------------------------------------
# arm
# ---------------------------------------------------------------------------------------------------------------------
UA_R = Curve([(-0.30, 0.036), (-0.20, 0.046), (-0.12, 0.053), (0.0, 0.054), (0.18, 0.0535), (0.40, 0.0475), (0.62, 0.0435), (0.82, 0.0385),
              (1.0, 0.0365), (1.14, 0.0385)])
FA_R = Curve([(0.0, 0.0385), (0.12, 0.0425), (0.30, 0.0405), (0.55, 0.0345), (0.80, 0.0290), (1.0, 0.0265)])


def build_arm(mb, side):
    g = ArmGeo(side)
    sx = g.sx
    NT = 10
    wfn = lambda p: arm_weights(g, p)
    wear = lambda i, j, p: (0.0, 0.05, 0.0)
    # ---- upper arm skin: from inside the shoulder to the elbow, then the forearm to the wrist -----------------------------
    mb.begin(f"arm_{side}", SKIN, density=1.25)
    rings = []
    ts_u = [0.04, 0.12, 0.24, 0.38, 0.52, 0.68, 0.84, 1.0]
    for t in ts_u:
        c = g.upper(t)
        tg = (g.E - g.S).normalized()
        # muscle shape: the biceps/deltoid bulge is a little larger front-to-back than side-to-side
        r = UA_R(t)
        rr = ellipse_ring(c, tg, Vector((0, 1, 0)), r * 1.02, r * 0.97, NT, 2.2, 0.0, shift=(0.0, 0.0))
        rings.append(rr)
    ts_f = [0.12, 0.30, 0.50, 0.70, 0.88, 1.0]
    for t in ts_f:
        c = g.fore(t)
        tg = (g.W - g.E).normalized()
        r = FA_R(t)
        rings.append(ellipse_ring(c, tg, Vector((0, 1, 0)), r * 1.06, r * 0.94, NT, 2.2))
    top = g.upper(-0.02) + Vector((-0.01 * sx, 0, -0.004))
    loft_a(mb, rings, wfn, SKIN, cap_start=(top, {f"shoulder.{side}": 0.5, "chest": 0.5}),
           colfn=lambda i, j, p: (0.0, 0.05 + 0.2 * sstep(0.3, 0.9, _along(g.E, g.W, p)), 0.0))

    # ---- short cap sleeve --------------------------------------------------------------------------------------------------
    mb.begin(f"sleeve_{side}", MAIN, density=1.0)
    rings = []
    ts = [-0.02, 0.03, 0.09, 0.15, 0.22, 0.30, 0.37, 0.44]
    taper = [0.80, 0.94, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    tg = (g.E - g.S).normalized()
    for k, t in enumerate(ts):
        c = g.upper(t)
        r = (UA_R(max(t, -0.05)) + 0.012 + 0.006 * sstep(-0.1, 0.4, t)) * taper[k]
        # soft folds along the sleeve and a slight flare at the hem
        rr = ellipse_ring(c, tg, Vector((0, 1, 0)), r * 1.04, r * 0.98, 14, 2.2)
        u, v, _ = frame_from_tangent(tg, Vector((0, 1, 0)))
        out = []
        for j, p in enumerate(rr):
            a = 2 * math.pi * j / 14
            f = 0.0028 * math.sin(3 * a + 5 * t + (0.0 if sx > 0 else 1.7)) * sstep(-0.1, 0.1, t) + 0.0035 * math.sin(2 * a + 1.0) * sstep(0.2, 0.44, t)
            out.append(p + (p - c).normalized() * f)
        rings.append(out)
    loft_a(mb, rings, wfn, MAIN, cap_start=(g.upper(-0.06) + Vector((-0.012 * sx, 0.0, -0.006)), wfn(g.upper(-0.05))),
           colfn=lambda i, j, p: (0.0, 0.18 + 0.4 * sstep(0.3, 0.44, _along(g.S, g.E, p)), 0.0))
    # trim band (accent) at the hem
    mb.begin(f"sleeve_trim_{side}", ACC, density=1.2)
    rings = []
    for t, d in ((0.405, 0.0), (0.425, 0.0035), (0.452, 0.0035), (0.462, 0.0)):
        c = g.upper(t)
        r = UA_R(t) + 0.012 + 0.006 * sstep(-0.1, 0.4, t) + d
        rings.append(ellipse_ring(c, tg, Vector((0, 1, 0)), r * 1.04, r * 0.98, 14, 2.2))
    loft_a(mb, rings, wfn, ACC, colfn=lambda i, j, p: (0.0, 0.25, 0.0))

    # ---- forearm wraps: overlapping tilted bands from 22 percent down the forearm to the wrist ------------------------------
    mb.begin(f"forearm_wrap_{side}", WRAPS, density=1.0)
    nb = 7
    t0, t1 = 0.30, 1.04
    pitch = (t1 - t0) / nb
    rings = []
    tgf = (g.W - g.E).normalized()
    for b in range(nb):
        for (zf, rid) in ((0.0, 1.0), (0.94, 0.0)):
            tb = t0 + (b + zf) * pitch
            r_ = []
            for j in range(14):
                a = 2 * math.pi * j / 14
                tt = tb + 0.045 * math.cos(a - (0.7 if sx > 0 else 2.4))
                tt = min(max(tt, t0 - 0.01), t1 + 0.02)
                R = FA_R(min(tt, 1.0)) + 0.0040 + 0.0018 * rid + 0.0012 * math.sin(3 * a + b)
                u, v, _ = frame_from_tangent(tgf, Vector((0, 1, 0)))
                c = g.fore(tt)
                r_.append(c + u * (R * 1.06 * math.cos(a)) + v * (R * 0.94 * math.sin(a)))
            rings.append(r_)
    loft_a(mb, rings, wfn, WRAPS, cap_start=(g.fore(t0 - 0.03), wfn(g.fore(t0 - 0.03))),
           colfn=lambda i, j, p: (0.0, 0.2 + 0.5 * sstep(0.7, 1.0, _along(g.E, g.W, p)) + 0.25 * (1 - sstep(0.3, 0.5, _along(g.E, g.W, p))), 0.0))


# ---------------------------------------------------------------------------------------------------------------------
# hand
# ---------------------------------------------------------------------------------------------------------------------
PALM_W = Curve([(0.0, 0.0285), (0.025, 0.0355), (0.055, 0.0405), (0.085, 0.0430), (0.100, 0.0415), (0.110, 0.0300), (0.114, 0.0150)])
PALM_T = Curve([(0.0, 0.0200), (0.025, 0.0190), (0.055, 0.0170), (0.085, 0.0160), (0.100, 0.0150), (0.110, 0.0115), (0.114, 0.0070)])
PALM_U0 = Curve([(0.0, 0.0), (0.05, 0.002), (0.114, 0.0015)])         # palm centre offset towards the thumb side

# finger layout: (name, bone group, lateral offset (thumb side +), knuckle s, lengths of 3 phalanges, radius)
FINGERS = [
    ("index", "im", 0.0275, 0.0915, (0.040, 0.0225, 0.0175), 0.0092),
    ("middle", "im", 0.0095, 0.0915, (0.043, 0.0245, 0.0185), 0.0095),
    ("ring", "rp", -0.0095, 0.0895, (0.040, 0.0225, 0.0175), 0.0090),
    ("pinky", "rp", -0.0280, 0.0870, (0.032, 0.0185, 0.0150), 0.0080),
]


def hand_w(g, p_a, s_bone_blend=None):
    return {f"hand.{g.side}": 1.0}


def finger_ring(c, t, n_dir, ru, rv, k=6):
    """ring around axis t; n_dir is the palm normal reference (rv along it)."""
    v = n_dir - t * n_dir.dot(t)
    v.normalize()
    u = t.cross(v)
    return ring_pts(c, u, v, ru, rv, k, 2.0)


def build_hand(mb, side):
    g = ArmGeo(side)
    sx = g.sx
    ps = hand_pts(side)
    Wp = g.W
    hd, u, n = g.hd, g.u, g.n
    hb = f"hand.{side}"
    # ---- palm ---------------------------------------------------------------------------------------------------------------
    mb.begin(f"palm_{side}", SKIN, density=1.6)
    rings = []
    ss = [0.0, 0.020, 0.045, 0.070, 0.092, 0.104, 0.112, 0.116]
    for s in ss:
        c = g.hp(s, PALM_U0(s), 0.0)
        # back of the hand is a touch flatter than the palm (thenar/hypothenar mass on the palm side)
        ring = []
        for j in range(10):
            a = 2 * math.pi * j / 10
            cu, sn_ = math.cos(a), math.sin(a)
            wu = PALM_W(s) * sgnpow(cu, 0.85)
            wn = PALM_T(s) * sgnpow(sn_, 0.85) * (1.08 if sn_ > 0 else 0.92)
            bump = 0.0045 * math.exp(-((s - 0.04) / 0.03) ** 2) * max(0.0, cu) * max(0.0, sn_)       # thenar pad on the palm side
            ring.append(c + u * wu + n * (wn + bump))
        rings.append(ring)
    wfn_h = lambda p: {hb: 1.0}
    loft_a(mb, rings, wfn_h, SKIN, cap_start=(g.hp(-0.012, 0, 0), {f"forearm.{side}": 0.5, hb: 0.5}),
           cap_end=(g.hp(0.120, 0.0, 0.0), {hb: 1.0}),
           colfn=lambda i, j, p: (0.0, 0.18 + 0.2 * sstep(0.05, 0.11, _along(g.W, g.hp(0.17), p)), 0.0))

    # ---- fingers ------------------------------------------------------------------------------------------------------------
    for (nm, grp, uo, s0, lens, rad) in FINGERS:
        mb.begin(f"{nm}_{side}", SKIN, density=2.0, group=f"fingers_{side}")
        b1, b2 = f"finger_{grp}_1.{side}", f"finger_{grp}_2.{side}"
        base = g.hp(s0 - 0.012, uo, 0.0)
        pts = [base]
        L1, L2, L3 = lens
        # the bone joint (PIP) in this chain is where bone 1 ends
        j_im = 0.1275 if grp == "im" else 0.1235
        # polyline of knuckles along the hand axis
        sk = [s0 - 0.012, s0, s0 + L1 * 0.5, s0 + L1, s0 + L1 + L2 * 0.5, s0 + L1 + L2, s0 + L1 + L2 + L3]
        rr = [rad * 0.98, rad * 1.04, rad * 1.0, rad * 1.04, rad * 0.94, rad * 0.97, rad * 0.88]
        rings = []
        for s_, r_ in zip(sk, rr):
            c = g.hp(s_, uo, 0.0)
            rings.append(finger_ring(c, hd, n, r_ * 1.0, r_ * 0.93, 6))

        def fw(p, s0=s0, b1=b1, b2=b2, j=j_im):
            sp = (p - Wp).dot(hd)
            k = sstep(j - 0.007, j + 0.007, sp)
            kb = sstep(s0 - 0.020, s0 - 0.004, sp)
            return {hb: 1 - kb, b1: kb * (1 - k), b2: k * kb}
        tip = g.hp(sk[-1] + rad * 0.55, uo, -0.0003)
        send = sk[-1]
        nail = lambda i, j, p, uo=uo, send=send: (0.0, 0.15, clamp01(((p - Wp).dot(hd) - (send - 0.0145)) / 0.004)
                                                  * (1.0 if (p - g.hp((p - Wp).dot(hd), uo, 0.0)).dot(n) < -0.2 * 0.0085 else 0.0))
        loft_a(mb, rings, fw, SKIN, cap_end=(tip, {b2: 1.0}), colfn=nail)

    # ---- thumb --------------------------------------------------------------------------------------------------------------
    mb.begin(f"thumb_{side}", SKIN, density=2.0, group=f"fingers_{side}")
    t1b, t2b = f"thumb_1.{side}", f"thumb_2.{side}"
    th_a, th_b = ps["thumb_1"]
    th_c = ps["thumb_2"][1]
    th_a, th_b, th_c = Vector(th_a), Vector(th_b), Vector(th_c)
    d1 = (th_b - th_a).normalized()
    d2 = (th_c - th_b).normalized()
    root = th_a - d1 * 0.016 - u * 0.0 + n * (-0.004)
    nodes = [(root, d1, 0.0172, 0.0150), (th_a, d1, 0.0150, 0.0135), (th_a.lerp(th_b, 0.55), d1, 0.0128, 0.0118),
             (th_b, (d1 + d2).normalized(), 0.0130, 0.0120), (th_b.lerp(th_c, 0.5), d2, 0.0115, 0.0108), (th_c, d2, 0.0100, 0.0092)]
    rings = []
    for c, t, ru_, rv_ in nodes:
        rings.append(finger_ring(c, t, n, ru_, rv_, 7))

    def tw(p):
        sp = (p - th_a).dot(d1)
        k = sstep(0.034, 0.046, sp)
        kb = sstep(-0.014, 0.0, sp)
        return {hb: 1 - kb, t1b: kb * (1 - k), t2b: k * kb}
    tnail = lambda i, j, p: (0.0, 0.15, clamp01(((p - th_b).dot(d2) - 0.014) / 0.004)
                                 * (1.0 if (p - (th_b + d2 * (p - th_b).dot(d2))).dot(n) < -0.0018 else 0.0))
    loft_a(mb, rings, tw, SKIN, cap_end=(th_c + d2 * 0.008, {t2b: 1.0}), colfn=tnail)

    # ---- hand wrap: bands over the palm and the wrist; knuckles and the thumb root stay free ----------------------------------
    mb.begin(f"hand_wrap_{side}", WRAPS, density=1.0)
    rings = []
    nb = 4
    s_lo, s_hi = -0.030, 0.092
    pitch = (s_hi - s_lo) / nb
    for b in range(nb):
        for (zf, rid) in ((0.0, 1.0), (0.95, 0.0)):
            sb = s_lo + (b + zf) * pitch
            ring = []
            for j in range(12):
                a = 2 * math.pi * j / 12
                ss_ = sb + 0.014 * math.cos(a - (0.5 if sx > 0 else 2.6))
                ss_ = min(max(ss_, s_lo - 0.004), s_hi + 0.004)
                sc = max(ss_, 0.0)
                c = g.hp(ss_, PALM_U0(sc), 0.0)
                off = 0.0040 + 0.0016 * rid
                wu = (PALM_W(sc) + off) * sgnpow(math.cos(a), 0.80)
                wn = (PALM_T(sc) * (1.08 if math.sin(a) > 0 else 0.92) + off) * sgnpow(math.sin(a), 0.80)
                ring.append(c + u * wu + n * wn)
            rings.append(ring)
    loft_a(mb, rings, lambda p: ({f"forearm.{side}": 1 - sstep(0.0, 0.03, (p - Wp).dot(hd)) * 1.0,
                                  hb: sstep(0.0, 0.03, (p - Wp).dot(hd))} if (p - Wp).dot(hd) < 0.03 else {hb: 1.0}),
           WRAPS, cap_start=(g.hp(s_lo - 0.012, 0, 0), {f"forearm.{side}": 1.0}),
           colfn=lambda i, j, p: (0.0, 0.35 + 0.45 * sstep(0.04, 0.09, (p - Wp).dot(hd)), 0.0))


def build_arms_and_hands(mb):
    for side in SIDES:
        build_arm(mb, side)
        build_hand(mb, side)
