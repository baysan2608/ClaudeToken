"""Garments: wrap top with V collar and lapel seam, sash with back knot and tails, loose trousers, shin wraps, shoes.
Vertex colour (r, g): r = surface variant (1 = rubber sole), g = wear / dirt mask (see fighter_textures.py)."""
import math

from mathutils import Vector

from fighter_body import TORSO_C, torso_point
from fighter_mesh import (ACC, Curve, MAIN, WRAPS, clamp01, frame_from_tangent, lerp, ring_pts, sgnpow, sstep, to_a,
                          torso_weights)
from fighter_skeleton import A, LEFT_REST, SASH_L, SASH_R, SIDES

FLIP = Vector((1, -1, 1))


def _fold(z, ang, amp=1.0):
    """Cloth drape: low-frequency creases that follow the body, strongest just above the sash and near the hem."""
    e1 = sstep(1.04, 1.12, z) * (1 - sstep(1.24, 1.36, z))
    e2 = sstep(0.93, 1.0, 1.0 - z * 0 if False else z) * (1 - sstep(1.10, 1.2, z))
    return amp * (0.0030 * math.sin(4 * ang + 9 * z) * e1 + 0.0022 * math.sin(7 * ang - 14 * z + 1.3) * (e1 + 0.3 * e2)
                  + 0.0035 * math.sin(6 * ang + 3.0) * (1 - sstep(0.94, 1.03, z)))


def top_edge_z(ang):
    """Neckline height at ring angle ang: V at the front, neck base elsewhere."""
    d = abs(((ang - 3 * math.pi / 2) + math.pi) % (2 * math.pi) - math.pi)
    v = max(0.0, 1.0 - d / math.radians(54))
    return 1.458 - 0.175 * v ** 1.15


def hem_z(ang):
    return 0.940 + 0.048 * abs(math.cos(ang)) ** 2.5


def build_top(mb):
    NT = 32
    ang0 = math.pi * 0.5                   # ring start = centre back (seam)
    angs = [ang0 + 2 * math.pi * j / NT for j in range(NT)]
    mb.begin("top", MAIN, density=1.0)
    NS = 11
    rings = []
    for i in range(NS):
        s = i / (NS - 1)
        r = []
        for ang in angs:
            zb = hem_z(ang)
            zt = top_edge_z(ang)
            z = lerp(zb, zt, s)
            # loose at the waist, hugging the chest; flares at the hem
            off = 0.011 + 0.006 * (1 - sstep(0.0, 0.5, s)) + 0.020 * (1 - sstep(0.0, 0.16, s)) ** 1.5
            off += _fold(z, ang) * (0.4 + 0.6 * (1 - s))
            if s > 0.9:
                off *= 1 - 0.5 * sstep(0.9, 1.0, s)
            r.append(torso_point(z, ang, off=off, chest=True))
        rings.append(r)
    wear = lambda i, j, p: (0.0, 0.55 * (1 - sstep(0.0, 0.18, i / (NS - 1))) + 0.15, 0.0)
    ids = mb.loft(rings, lambda i, j, p: torso_weights(to_a(p)), MAIN, colfn=wear)
    # hem roll
    hr = []
    for k, (ds, doff) in enumerate(((0.0, 0.0), (0.0, 0.0035), (0.012, 0.0035), (0.012, 0.0))):
        r = []
        for ang in angs:
            zb = hem_z(ang)
            zt = top_edge_z(ang)
            z = lerp(zb, zt, 0.0) + ds * 0.0
            off = 0.011 + 0.006 + 0.020 + _fold(z, ang) * 0.4 + doff
            r.append(torso_point(z + (0.012 if k >= 2 else 0.0) * 0.0, ang, off=off, chest=True))
        hr.append(r)
    # collar: rolled band along the V neckline (accent colour)
    mb.begin("collar", ACC, density=1.0)
    prof = [(0.80, 0.0030), (0.88, 0.0058), (0.96, 0.0068), (1.0, 0.0050), (1.012, 0.0020)]
    cr = []
    for s, doff in prof:
        r = []
        for ang in angs:
            zb = hem_z(ang)
            zt = top_edge_z(ang)
            z = lerp(zb, zt, s)
            off = 0.011 + 0.006 * (1 - sstep(0.0, 0.5, s)) + _fold(z, ang) * 0.1 + doff
            if s >= 1.0:
                off = 0.011 * (1 - 0.5 * sstep(0.9, 1.0, s)) + doff - 0.001
            r.append(torso_point(z, ang, off=off, chest=True))
        cr.append(r)
    mb.loft(cr, lambda i, j, p: torso_weights(to_a(p)), ACC, colfn=lambda i, j, p: (0.0, 0.2, 0.0))
    # lapel crossing: raised piping from the V apex diagonally over the chest to the left of the sash
    mb.begin("lapel", ACC, density=1.0)
    rr = []
    NSEG = 9
    for k in range(NSEG):
        t = k / (NSEG - 1)
        a_c = math.radians(270 + 10 + 62 * t)             # towards the character's left
        z_c = lerp(1.292, 1.050, t) - 0.012 * math.sin(math.pi * t)
        row = []
        for dz, doff in ((0.011, 0.0), (0.0035, 0.0032), (-0.0035, 0.0032), (-0.011, 0.0)):
            zz = z_c + dz
            off = 0.011 + 0.006 * (1 - sstep(0.0, 0.5, (zz - 1.0) / 0.45)) + doff + _fold(zz, a_c) * 0.4
            row.append(torso_point(zz, a_c, off=off, chest=True))
        rr.append(row)
    mb.loft(rr, lambda i, j, p: torso_weights(to_a(p)), ACC, close=False)


def build_sash(mb):
    NT = 32
    angs = [math.pi * 0.5 + 2 * math.pi * j / NT for j in range(NT)]
    mb.begin("sash", ACC, density=1.0)
    levels = [(1.000, 0.010), (1.014, 0.026), (1.05, 0.032), (1.095, 0.030), (1.118, 0.020), (1.128, 0.009)]
    rings = []
    for k, (z, off) in enumerate(levels):
        r = []
        for ang in angs:
            tilt = 0.012 * math.cos(ang - math.radians(340))
            ripple = 0.0025 * math.sin(5 * ang + 2.0 * k) * (1 if 0 < k < 5 else 0.3)
            r.append(torso_point(z + tilt, ang, off=off + ripple, chest=False))
        rings.append(r)
    mb.loft(rings, lambda i, j, p: torso_weights(to_a(p)), ACC, colfn=lambda i, j, p: (0.0, 0.1, 0.0))
    # second wrap, slightly diagonal, partial (front half)
    mb.begin("sash2", ACC, density=1.0)
    rr = []
    for k, (zo, off) in enumerate(((-0.020, 0.012), (-0.010, 0.0345), (0.010, 0.0365), (0.020, 0.0125))):
        r = []
        for q in range(15):
            ang = math.radians(150 + 240 * q / 14)
            tilt = 0.022 * math.sin(ang - math.radians(200)) * 0.8
            r.append(torso_point(1.06 + zo + tilt, ang, off=off + 0.0018 * math.sin(7 * ang + k), chest=False))
        rr.append(r)
    mb.loft(rr, lambda i, j, p: torso_weights(to_a(p)), ACC, close=False, colfn=lambda i, j, p: (0.0, 0.1, 0.0))
    # knot on the lower back
    mb.begin("knot", ACC, density=1.2)
    kz = 1.058
    ky = 0.1255 + 0.016
    krings = []
    for dy, rs in ((-0.012, 0.55), (0.0, 1.0), (0.013, 0.95), (0.025, 0.45)):
        pts = []
        for q in range(10):
            a = 2 * math.pi * q / 10
            pts.append(Vector((0.034 * rs * math.cos(a) * (1.0 + 0.2 * abs(math.sin(a))), ky + dy, kz + 0.030 * rs * math.sin(a))))
        krings.append(pts)
    wk = lambda i, j, p: {"hips": 0.5, "spine": 0.5}
    mb.loft(krings, wk, ACC, cap_start=(Vector((0, ky - 0.020, kz)), {"hips": 0.5, "spine": 0.5}),
            cap_end=(Vector((0, ky + 0.034, kz)), {"hips": 0.5, "spine": 0.5}))
    # tails (flat ribbons hanging from the knot, weighted along the tail bones)
    for side, chain, width in (("L", SASH_L, 0.050), ("R", SASH_R, 0.046)):
        mb.begin(f"sash_tail_{side}", ACC, density=1.2)
        b = [Vector(A(*p)) for p in chain]
        zs = [p.z for p in b]
        rows = 9
        rings = []
        names = [f"sash_tail.{side}.{k:03d}" for k in (1, 2, 3)]

        def wfn(i, j, p, zs=zs, names=names):
            z = p.z
            t1 = sstep(zs[1] + 0.035, zs[1] - 0.035, z)         # 1 above joint 1
            t2 = sstep(zs[2] + 0.035, zs[2] - 0.035, z)
            w = {}
            top = sstep(zs[0] - 0.02, zs[0] + 0.015, z)       # attached to the knot / hips
            w["hips"] = top
            w[names[0]] = (1 - top) * t1
            w[names[1]] = (1 - top) * (1 - t1) * t2
            w[names[2]] = (1 - top) * (1 - t1) * (1 - t2)
            return w
        for r_ in range(rows):
            s_ = r_ / (rows - 1)
            z = lerp(b[0].z - 0.002, b[3].z, s_)
            # centre line along the chain
            if z >= b[1].z:
                t = (b[0].z - z) / (b[0].z - b[1].z)
                c = b[0].lerp(b[1], clamp01(t))
            elif z >= b[2].z:
                c = b[1].lerp(b[2], (b[1].z - z) / (b[1].z - b[2].z))
            else:
                c = b[2].lerp(b[3], (b[2].z - z) / (b[2].z - b[3].z))
            wv = width * (0.8 + 0.28 * s_) * 0.5
            rip = 0.006 * s_ * math.sin(9 * s_ + (1 if side == "L" else 2))
            drop = 0.016 * s_ ** 4
            th = 0.0022
            rings.append([Vector((c.x - wv + rip, c.y - th, z - drop * 0)), Vector((c.x + wv + rip, c.y - th, z - drop)),
                          Vector((c.x + wv + rip, c.y + th, z - drop)), Vector((c.x - wv + rip, c.y + th, z - drop * 0))])
        mb.loft(rings, wfn, ACC, cap_end=(Vector(((b[3].x + rings[-1][0].x) * 0.5 + 0.0, b[3].y, b[3].z + 0.020)), wfn(0, 0, Vector((0, 0, b[3].z)))),
                colfn=lambda i, j, p: (0.0, 0.1 + 0.4 * (1 - sstep(0.75, 1.05, p.z)), 0.0))


# ---------------------------------------------------------------------------------------------------------------------
# legs
# ---------------------------------------------------------------------------------------------------------------------
def leg_geometry(side):
    sx = 1 if side == "L" else -1
    L = LEFT_REST
    hip, knee, ank = Vector(L["hip"]), Vector(L["knee"]), Vector(L["ankle"])

    def leg_pt(z):
        if z >= knee.z:
            t = (z - hip.z) / (knee.z - hip.z)
            p = hip + (knee - hip) * t
        else:
            t = (z - knee.z) / (ank.z - knee.z)
            p = knee + (ank - knee) * t
        return Vector((p.x * sx, p.y, p.z))
    return sx, knee, leg_pt


def build_legs(mb):
    NT = 28
    # --- pelvis / trouser top (one piece spanning both thighs) ------------------------------------------------------
    mb.begin("pelvis", MAIN, density=1.0)
    levels = [(1.068, 0.158, 0.103), (1.02, 0.164, 0.106), (0.97, 0.176, 0.111), (0.92, 0.187, 0.117), (0.865, 0.189, 0.118),
              (0.82, 0.175, 0.111)]
    rings = []
    for z, hw_, hd_ in levels:
        r = []
        for j in range(NT):
            ang = math.pi * 0.5 + 2 * math.pi * j / NT
            e = 2.0 / 2.5
            fl = 0.0025 * math.sin(5 * ang + 6 * z) * (1 if z < 1.0 else 0.3)
            r.append(Vector(((hw_ + fl) * sgnpow(math.cos(ang), e), (hd_ + fl) * sgnpow(math.sin(ang), e) - TORSO_C(min(z, 1.0)), z)))
        rings.append(r)

    def band_w(i, j, p):
        z = p.z
        t_leg = sstep(0.945, 0.805, z)
        sf = sstep(-0.035, 0.035, p.x)
        w = {"hips": 1.0 - t_leg}
        w["thigh.L"] = t_leg * sf
        w["thigh.R"] = t_leg * (1 - sf)
        return w
    mb.loft(rings, band_w, MAIN, cap_end=(Vector((0, 0.0, 0.80)), {"thigh.L": 0.5, "thigh.R": 0.5}))

    for side in SIDES:
        sx, knee, leg_pt = leg_geometry(side)
        thigh_b, shin_b, foot_b = f"thigh.{side}", f"shin.{side}", f"foot.{side}"
        # ---- trousers ----
        mb.begin(f"trouser_{side}", MAIN, density=1.0)
        tz = [0.985, 0.95, 0.90, 0.84, 0.78, 0.72, 0.66, 0.60, 0.555, 0.52, 0.49, 0.455, 0.42, 0.39, 0.366, 0.345]
        tr = Curve([(0.985, 0.088), (0.93, 0.094), (0.85, 0.094), (0.75, 0.089), (0.65, 0.082), (0.565, 0.0765), (0.515, 0.075),
                    (0.475, 0.0765), (0.425, 0.0785), (0.385, 0.0775), (0.358, 0.0715), (0.345, 0.0665)])
        n_l = 14
        rings = []
        pts = [leg_pt(z) for z in tz]
        for k, z in enumerate(tz):
            t_ = (pts[min(k + 1, len(tz) - 1)] - pts[max(k - 1, 0)]) * FLIP
            u, v, t = frame_from_tangent(-t_, Vector((0, -1, 0)))
            c = pts[k] * FLIP
            r = []
            for j in range(n_l):
                a = math.pi + 2 * math.pi * j / n_l                     # j = 0 at the back
                front = max(0.0, -math.cos(a - math.pi)) if False else max(0.0, math.cos(a))
                # drape: thigh pleats, knee crease at the front, bunching at the cuff
                f = 0.0032 * math.sin(3 * a + 22 * z + (0 if sx > 0 else 1.7)) * sstep(0.45, 0.62, z) * (1 - sstep(0.93, 0.99, z))
                f += 0.0045 * math.exp(-((z - 0.505) / 0.032) ** 2) * (0.4 + 0.6 * math.cos(a)) * (1 if math.cos(a) > -0.2 else 0.3)
                f += 0.0035 * math.sin(4 * a + 2.0 * (1 if sx > 0 else -1)) * sstep(0.40, 0.355, z)
                f += 0.0030 * math.sin(2 * a + 12 * z) * sstep(0.50, 0.40, z) * 0.0
                rr = (tr(z) + f)
                r.append(c + u * (rr * sgnpow(math.cos(a), 0.95)) + v * (rr * 0.97 * sgnpow(math.sin(a), 0.95)))
            rings.append(r)

        def trw(i, j, p, thigh_b=thigh_b, shin_b=shin_b, knee=knee):
            z = p.z
            hipw = sstep(0.84, 0.96, z)
            kneet = sstep(knee.z - 0.06, knee.z + 0.06, z)
            return {"hips": hipw, thigh_b: (1 - hipw) * kneet, shin_b: (1 - hipw) * (1 - kneet)}
        wear = lambda i, j, p: (0.0, clamp01(0.7 * math.exp(-((p.z - 0.5) / 0.05) ** 2) + 0.5 * (1 - sstep(0.36, 0.5, p.z)) * 0.8
                                              + 0.3 * (1 - sstep(0.93, 0.8, p.z)) * 0), 0.0)
        mb.loft(rings, trw, MAIN, cap_end=(Vector((rings[-1][0].x * 0 + sum((q.x for q in rings[-1])) / n_l,
                                                  sum((q.y for q in rings[-1])) / n_l, tz[-1] - 0.014)), {shin_b: 1.0}), colfn=wear)

        # ---- shin wraps: overlapping bands, each ring pair tilted so the seams run diagonally ----
        mb.begin(f"shinwrap_{side}", WRAPS, density=1.0)
        nb = 10
        z_lo, z_hi = 0.090, 0.372
        pitch = (z_hi - z_lo) / nb
        wr = Curve([(0.09, 0.0405), (0.13, 0.0440), (0.19, 0.0505), (0.25, 0.0575), (0.30, 0.0615), (0.372, 0.0660)])
        rings = []
        n_w = 14
        for b in range(nb):
            for (zf, rid) in ((0.0, 1.0), (0.92, 0.0)):
                z0 = z_lo + (b + zf) * pitch
                r = []
                for j in range(n_w):
                    a = math.pi + 2 * math.pi * j / n_w
                    zz = z0 + 0.0075 * math.cos(a - 0.6) * (1 if sx > 0 else -1) * 0 + 0.0075 * math.cos(a - (0.6 if sx > 0 else 2.5))
                    zz = min(max(zz, z_lo - 0.002), z_hi + 0.004)
                    R = wr(zz) * (1.0 + 0.06 * rid) + 0.0012 * math.sin(3 * a + b)
                    t_ = (leg_pt(zz + 0.02) - leg_pt(zz - 0.02)) * FLIP
                    u, v, t = frame_from_tangent(t_, Vector((0, -1, 0)))
                    c = leg_pt(zz) * FLIP
                    r.append(c + u * (R * math.cos(a)) + v * (R * 0.97 * math.sin(a)))
                rings.append(r)

        def ww(i, j, p, shin_b=shin_b, foot_b=foot_b):
            f = 0.55 * sstep(0.135, 0.085, p.z)
            return {shin_b: 1 - f, foot_b: f}
        wcol = lambda i, j, p: (0.0, 0.25 + 0.5 * (1 - sstep(0.09, 0.30, p.z)), 0.0)
        mb.loft(rings, ww, WRAPS, cap_start=(Vector((sx * 0.09, 0.005, 0.085)), {foot_b: 1.0}), colfn=wcol)
        build_shoe(mb, side, sx)


def build_shoe(mb, side, sx):
    foot_b, toe_b = f"foot.{side}", f"toe.{side}"
    ys = [-0.090, -0.082, -0.062, -0.032, 0.0, 0.032, 0.064, 0.096, 0.126, 0.156, 0.184, 0.208, 0.228]
    top_h = Curve([(-0.090, 0.050), (-0.082, 0.082), (-0.062, 0.108), (-0.032, 0.122), (0.0, 0.124), (0.032, 0.100), (0.064, 0.080),
                   (0.096, 0.064), (0.126, 0.056), (0.156, 0.050), (0.184, 0.044), (0.208, 0.036), (0.228, 0.026)])
    wid = Curve([(-0.090, 0.026), (-0.082, 0.036), (-0.062, 0.043), (-0.032, 0.0455), (0.0, 0.0465), (0.032, 0.048), (0.064, 0.0505),
                 (0.096, 0.0525), (0.126, 0.0525), (0.156, 0.0505), (0.184, 0.046), (0.208, 0.039), (0.228, 0.026)])
    soleh = 0.013
    shw = lambda i, j, p, foot_b=foot_b, toe_b=toe_b: ({foot_b: 1 - sstep(0.105, 0.155, -p.y), toe_b: sstep(0.105, 0.155, -p.y)})
    # upper
    mb.begin(f"shoe_{side}", ACC, density=1.0)
    rings = []
    for k, y in enumerate(ys):
        h = top_h(y)
        r = []
        n = 14
        for q in range(n):
            a = 2 * math.pi * q / n
            # toe-box bump and heel counter seam
            x = wid(y) * sgnpow(math.cos(a), 0.78)
            zc = soleh + (h - soleh) * 0.5
            hz = (h - soleh) * 0.5
            zz = zc + hz * sgnpow(math.sin(a), 0.78)
            if math.sin(a) > 0.2 and 0.09 < y < 0.17:
                zz += 0.003 * math.sin(math.pi * (y - 0.09) / 0.08)         # toe box
            r.append(Vector((sx * 0.09 + x * (1 if True else 1), -y, zz)))
        rings.append(r)
    heel_c = Vector((sx * 0.09, 0.094, 0.034))
    tip_c = Vector((sx * 0.09, -0.238, 0.022))
    wcol = lambda i, j, p: (0.0, 0.2 + 0.7 * (sstep(0.18, 0.23, -p.y) + sstep(-0.07, -0.09, -p.y)) * 0.6, 0.0)
    mb.loft(rings, shw, ACC, cap_start=(heel_c, {foot_b: 1.0}), cap_end=(tip_c, {toe_b: 1.0}), colfn=wcol)
    # sole: rubber slab, slightly proud of the upper, with a toe spring
    mb.begin(f"sole_{side}", WRAPS, density=0.8)
    rings = []
    for k, y in enumerate(ys):
        n = 14
        r = []
        spring = 0.011 * sstep(0.15, 0.232, y) ** 1.5
        for q in range(n):
            a = 2 * math.pi * q / n
            x = (wid(y) + 0.0035) * sgnpow(math.cos(a), 0.62)
            zz = soleh * 0.5 + spring + (soleh * 0.5 + 0.0005) * sgnpow(math.sin(a), 0.62)
            r.append(Vector((sx * 0.09 + x, -y, max(zz, 0.0))))
        rings.append(r)
    mb.loft(rings, shw, WRAPS, cap_start=(Vector((sx * 0.09, 0.095, soleh * 0.5)), {foot_b: 1.0}),
            cap_end=(Vector((sx * 0.09, -0.2395, soleh * 0.5 + 0.011)), {toe_b: 1.0}), colfn=lambda i, j, p: (1.0, 0.5, 0.0))


def build_garments(mb):
    build_top(mb)
    build_sash(mb)
    build_legs(mb)
