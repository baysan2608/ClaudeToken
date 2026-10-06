"""Hair (cap combed back into a top knot + short tail on ff_hair_01..03), eyelash strips, eyeballs.
numpy + mathutils only."""
import numpy as np

import ch_garments as G
import ch_part as P
import ch_regions as R
import ch_weights as cw


def _n(a):
    return a / (np.linalg.norm(a, axis=-1, keepdims=True) + 1e-12)


def hair_field(v, lm):
    """> 0 on the scalp (above the hairline)."""
    az, z = R.head_polar(v, lm)
    return z - R.hairline_z(az, lm)


def build_hair_cap(st, W_all, lm):
    obj = st.obj
    fi = obj.group_faces(lambda g: g == "body")
    used = sorted({i for f in fi for i in obj.faces[f]})
    remap = {o: i for i, o in enumerate(used)}
    head = P.Part("hair_cap", st.v[used], [[remap[i] for i in obj.faces[f]] for f in fi], "hair", W_all[used])
    hf = hair_field(head.v, lm)
    sel = [all(hf[i] > -0.02 for i in f) and all(head.v[i, 2] > lm["jaw"][2] - 0.01 for i in f) for f in head.f]
    cap = head.subset(sel, "hair_cap")
    # thickness: 0 at the hairline -> 5 mm one centimetre in -> fuller toward the crown / back where it gathers
    hf = hair_field(cap.v, lm)
    nrm = cap.vertex_normals()
    c = lm["skull_c"]
    back = np.clip((cap.v[:, 1] - c[1]) / 0.08, 0, 1)
    top = np.clip((cap.v[:, 2] - (c[2] + 0.02)) / 0.06, 0, 1)
    thick = np.clip(hf / 0.012, 0, 1) ** 0.7 * (0.0045 + 0.0035 * top + 0.004 * back * top)
    cap.v = cap.v + nrm * thick[:, None]
    # comb smooth (lose the skull's small bumps), keep the hairline where it is
    pin = hf < 0.004
    G.taubin(cap, 10, mask=~pin)
    cap = P.clip(cap, -(hair_field(cap.v, lm) - 0.0005))     # exact hairline, a hair above the skin
    return cap


def keep_above(cap, head_part, lm, gap=0.0006):
    """No part of the cap may sink under the scalp (smoothing pulls it into concave spots behind the ears)."""
    bb = G.bvh_of(head_part)
    hf = hair_field(cap.v, lm)
    # at the hairline the cap tucks just under the skin surface (no visible gap / shadow line)
    thick = np.clip(hf / 0.012, 0, 1) * 0.004 + gap - 0.0012 * np.clip(1 - hf / 0.004, 0, 1)
    from mathutils import Vector
    for i in range(len(cap.v)):
        loc, nrm, _fi, _d = bb.find_nearest(Vector(cap.v[i]), 0.05)
        if loc is None:
            continue
        loc, nrm = np.array(loc), np.array(nrm)
        d = (cap.v[i] - loc) @ nrm
        if d < thick[i] or hf[i] < 0.004:
            cap.v[i] += nrm * (thick[i] - d)
    return cap


def lofted_tube(name, centers, radii_a, radii_b, up_hint, segs, mat, cap_end=True, twist=None):
    """Elliptic tube along a polyline. radii_a along the side axis, radii_b along the up axis."""
    C = np.asarray(centers, float)
    N = len(C)
    T = np.gradient(C, axis=0)
    T = _n(T)
    verts, faces = [], []
    prev_side = None
    for i in range(N):
        up = np.asarray(up_hint(i) if callable(up_hint) else up_hint, float)
        side = _n(np.cross(T[i], up))
        if prev_side is not None and side @ prev_side < 0:
            side = -side
        prev_side = side
        upv = _n(np.cross(side, T[i]))
        tw = 0.0 if twist is None else twist[i]
        for k in range(segs):
            a = 2 * np.pi * k / segs + tw
            verts.append(C[i] + side * np.cos(a) * radii_a[i] + upv * np.sin(a) * radii_b[i])
    for i in range(N - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append([i * segs + k, i * segs + k2, (i + 1) * segs + k2, (i + 1) * segs + k])
    sv = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))])
    circ = 2 * np.pi * float(np.mean(0.5 * (np.asarray(radii_a) + np.asarray(radii_b))))
    uvs = []
    for i in range(N - 1):
        for k in range(segs):
            u0, u1 = k / segs * circ, (k + 1) / segs * circ
            uvs.append([(u0, sv[i]), (u1, sv[i]), (u1, sv[i + 1]), (u0, sv[i + 1])])
    if cap_end:
        verts.append(C[-1] + T[-1] * 0.002)
        tip = len(verts) - 1
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append([(N - 1) * segs + k, (N - 1) * segs + k2, tip])
            uvs.append([(k / segs * circ, sv[-1]), ((k + 1) / segs * circ, sv[-1]), ((k + 0.5) / segs * circ, sv[-1] + 0.006)])
    p = P.Part(name, np.array(verts), faces, mat, uvs=uvs)
    # outward orientation check
    f0 = faces[0]
    e1 = p.v[f0[1]] - p.v[f0[0]]
    e2 = p.v[f0[3]] - p.v[f0[0]]
    if np.cross(e1, e2) @ (p.v[f0[0]] - C[0]) < 0:
        p.f = [list(reversed(f)) for f in p.f]
        p.uv = [list(reversed(u)) for u in p.uv]
    return p


def build_knot_and_tail(cap, lm):
    """Top knot (bun) at the crown-back on ff_hair_01, a short tail down the back of the head on ff_hair_02/03,
    and a cloth tie around the bun base (sash material)."""
    from mathutils import Vector
    bvh = G.bvh_of(cap)
    h1 = cw.bone_head("ff_hair_01")
    # bun seat: closest cap point to the first hair bone head
    loc, nrm, _fi, _d = bvh.find_nearest(Vector(h1), 1.0)
    seat = np.array(loc)
    n = _n(np.array(nrm))
    # bun: squashed sphere, axis along the scalp normal tilted back
    axis = _n(n + np.array([0.0, 0.35, 0.1]))
    r_bun = 0.026
    centers = [seat + axis * (r_bun * (0.15 + 1.6 * t)) for t in np.linspace(0, 1, 9)]
    prof = np.sin(np.linspace(0.12, np.pi - 0.15, 9)) * r_bun
    prof[0] = r_bun * 0.62
    bun = lofted_tube("hair_bun", centers, prof * 1.05, prof * 0.95, np.array([1.0, 0, 0]), 14, "hair",
                      cap_end=True)
    # close the bottom of the bun (sits on the cap)
    # tie: short ring around the bun base
    tie_c = [seat + axis * (r_bun * 0.38 + d) for d in np.linspace(-0.004, 0.006, 3)]
    tie = lofted_tube("hair_tie", tie_c, [r_bun * 0.80] * 3, [r_bun * 0.80] * 3, np.array([1.0, 0, 0]), 14, "sash",
                      cap_end=False)
    # tail: from the bun's back down the chain ff_hair_01 tail -> 03 tail, slight S curve, tapering
    chain = [cw.bone_tail("ff_hair_01"), cw.bone_tail("ff_hair_02"), cw.bone_tail("ff_hair_03")]
    bun_c = seat + axis * r_bun * 0.9
    start = bun_c + np.array([0, 0.016, -0.012])
    pts = np.vstack([start, chain])
    # Catmull-like resample
    tt = np.linspace(0, 1, 16)
    seg = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    s = seg / seg[-1]
    C = np.stack([np.interp(tt, s, pts[:, k]) for k in range(3)], axis=1)
    C = G.smooth_polyline(C, 3, False)
    C[:, 0] += 0.004 * np.sin(tt * np.pi * 1.5)                 # a little sway, not symmetric
    taper = (1 - tt) ** 0.8
    ra = 0.016 * taper ** 0.7 + 0.0015
    rb = 0.011 * taper ** 0.7 + 0.0012
    tail = lofted_tube("hair_tail", C, ra, rb, lambda i: np.array([0, 1.0, 0.2]), 10, "hair", cap_end=True,
                       twist=np.linspace(0, 0.5, len(C)))
    return bun, tie, tail, seat, axis


def chain_weight_tail(part, chain_bones, z_centers):
    """Weights along a vertical-ish chain by height (hats between the given centres)."""
    H = cw.hats(-part.v[:, 2], [-z for z in z_centers])
    W = np.zeros((len(part.v), cw.NB))
    for j, b in enumerate(chain_bones):
        W[:, cw.BI[b]] = H[:, j]
    return W


# ---------------------------------------------------------------------------------------------------- eyes
def build_eyes(lm, segs=20, rings=14):
    parts = []
    for s in ("l", "r"):
        c = lm[f"eye_c_{s}"]
        r = lm[f"eye_r_{s}"] * 0.96
        verts, faces, uvs = [], [], []
        # sphere around -Y (looking forward), pole 0 = front
        for i in range(rings + 1):
            th = np.pi * i / rings
            for k in range(segs):
                ph = 2 * np.pi * k / segs
                d = np.array([np.sin(th) * np.cos(ph), -np.cos(th), np.sin(th) * np.sin(ph)])
                bulge = 1.0 + 0.10 * max(0.0, np.cos(th) - 0.72) / 0.28   # cornea
                verts.append(c + d * r * bulge)
        verts = np.array(verts)
        idx = lambda i, k: i * segs + (k % segs)  # noqa: E731
        for i in range(rings):
            for k in range(segs):
                f = [idx(i, k), idx(i + 1, k), idx(i + 1, k + 1), idx(i, k + 1)]
                faces.append(f)
        # azimuthal map around the front pole (unique, no overlap): r = 0.5 * (theta / pi) ** 0.6 so the iris
        # (theta < ~32 deg) gets a big share of the texture; the back pole sits on the outer circle
        def uv_of(vi):
            d = (verts[vi] - c) / np.linalg.norm(verts[vi] - c)
            th = np.arccos(np.clip(-d[1], -1, 1))
            ph = np.arctan2(d[2], d[0] * (1 if s == "l" else -1))
            rr = 0.5 * (th / np.pi) ** 0.6 * 0.98
            return (float(0.5 + rr * np.cos(ph)), float(0.5 + rr * np.sin(ph)))
        uvs = [[uv_of(vi) for vi in f] for f in faces]
        p = P.Part("eye_" + s, verts, faces, "eyes", None, uvs)
        # outward winding
        f0 = faces[segs * 3]
        e1 = verts[f0[1]] - verts[f0[0]]
        e2 = verts[f0[3]] - verts[f0[0]]
        if np.cross(e1, e2) @ (verts[f0[0]] - c) < 0:
            p.f = [list(reversed(f)) for f in p.f]
            p.uv = [list(reversed(u)) for u in p.uv]
        p.W[:, cw.BI["head"]] = 1.0
        parts.append(p)
    return parts


def build_lashes(body, lm):
    """Opaque upper-lash strips along the top of each eye opening (hair material)."""
    loops = P.boundary_loops(body)
    out = []
    for s in ("l", "r"):
        c = lm[f"eye_c_{s}"]
        best = min(loops, key=lambda l: np.linalg.norm(body.v[l].mean(axis=0) - (c + np.array([0, -0.012, 0]))))
        L = body.v[best]
        if np.linalg.norm(L.mean(axis=0) - c) > 0.03:
            continue
        # upper half of the opening, ordered from inner to outer corner
        upper = [i for i in best if body.v[i, 2] > c[2] - 0.001]
        pts = body.v[upper]
        order = np.argsort(np.abs(pts[:, 0]))
        pts = pts[order]
        if len(pts) < 4:
            continue
        pts = G.resample_polyline(pts, 14, False)
        verts, faces = [], []
        n_out = len(pts)
        for i, p in enumerate(pts):
            t = i / (n_out - 1)
            ln = 0.0045 + 0.004 * np.sin(np.pi * min(1.0, t * 1.15))          # longer toward the outer third
            fwd = np.array([0, -1.0, 0])
            up = np.array([0, 0, 1.0])
            outward = _n(np.array([np.sign(c[0]) * 0.35, 0, 0]) + 0.2 * up)
            tip = p + fwd * ln * 0.55 + up * ln * 0.75 + outward * ln * 0.3 * t
            mid = p + fwd * ln * 0.45 + up * ln * 0.25
            base_in = p + np.array([0, 0.0008, -0.0004])
            verts += [base_in, p + fwd * 0.0006, mid, tip]
        for i in range(n_out - 1):
            for k in range(3):
                a, b = i * 4 + k, i * 4 + k + 1
                faces.append([a, b, b + 4, a + 4])
        luv = []
        for i in range(n_out - 1):
            for k in range(3):
                luv.append([(i * 0.003, k * 0.002), (i * 0.003, (k + 1) * 0.002), ((i + 1) * 0.003, (k + 1) * 0.002),
                            ((i + 1) * 0.003, k * 0.002)])
        lp = P.Part("lash_" + s, np.array(verts), faces, "hair", uvs=luv)
        lp.W[:, cw.BI["head"]] = 1.0
        out.append(lp)
    return out
