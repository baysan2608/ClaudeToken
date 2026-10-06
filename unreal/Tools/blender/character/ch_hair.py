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
    """> 0 on the scalp (above the hairline, away from the ears)."""
    return R.hair_field(v, lm)


CAP_EDGE = 0.012      # the cap thickens from 0 at its edge to full over this distance


def _cap_thickness(cap, lm, edge_d):
    """0 at the cap edge -> ~5 mm one centimetre in -> fuller toward the crown / back where it gathers."""
    c = lm["skull_c"]
    back = np.clip((cap.v[:, 1] - c[1]) / 0.08, 0, 1)
    top = np.clip((cap.v[:, 2] - (c[2] + 0.02)) / 0.06, 0, 1)
    return np.clip(edge_d / CAP_EDGE, 0, 1) ** 0.7 * (0.0045 + 0.0035 * top + 0.004 * back * top)


def build_hair_cap(st, W_all, lm):
    obj = st.obj
    fi = obj.group_faces(lambda g: g == "body")
    used = sorted({i for f in fi for i in obj.faces[f]})
    remap = {o: i for i, o in enumerate(used)}
    head = P.Part("hair_cap", st.v[used], [[remap[i] for i in obj.faces[f]] for f in fi], "hair", W_all[used])
    hf = hair_field(head.v, lm)
    sel = [all(hf[i] > -0.02 for i in f) and all(head.v[i, 2] > lm["jaw"][2] - 0.01 for i in f) for f in head.f]
    cap = head.subset(sel, "hair_cap")
    # the base mesh's scalp is coarse (~1.5 cm quads): one subdivision step so the hairline cut and the cap's
    # silhouette are smooth, then the exact hairline (on the skin surface, away from the ears)
    cap = P.subdivide(cap)
    cap = P.clip(cap, -(hair_field(cap.v, lm) - 0.0005))
    # the field bends sharply around the ears: linear cuts on 7 mm edges zig-zag there. Relax the cut line along
    # itself and put it back on the skin.
    _relax_boundary(cap, G.bvh_of(head), 8)
    # thickness from the distance to the cap edge (the edge stays on the skin; works along the ears too)
    edge_d = P.boundary_distance(cap)
    nrm = cap.vertex_normals()
    cap.v = cap.v + nrm * _cap_thickness(cap, lm, edge_d)[:, None]
    # comb smooth (lose the skull's small bumps), keep the hairline where it is
    G.taubin(cap, 12, mask=edge_d > 0.004)
    return cap


def _relax_boundary(part, bvh, iters):
    from mathutils import Vector
    for loop in P.boundary_loops(part):
        if len(loop) < 6:
            continue
        pts = part.v[loop].copy()
        closed = loop[0] in {b for a, b in part.edges_boundary() if a == loop[-1]}
        for _ in range(iters):
            sm = 0.5 * pts + 0.25 * (np.roll(pts, 1, axis=0) + np.roll(pts, -1, axis=0))
            if not closed:
                sm[0], sm[-1] = pts[0], pts[-1]
            pts = sm
        for k, q in enumerate(pts):
            loc = bvh.find_nearest(Vector(q), 0.02)[0]
            part.v[loop[k]] = np.array(loc) if loc is not None else q
    return part


def keep_above(cap, head_part, lm, gap=0.0006):
    """No part of the cap may sink under the scalp (smoothing pulls it into concave spots behind the ears); at the
    edge the cap tucks just under the skin surface (no visible gap / shadow line)."""
    bb = G.bvh_of(head_part)
    edge_d = P.boundary_distance(cap)
    thick = np.clip(edge_d / CAP_EDGE, 0, 1) * 0.004 + gap - 0.0012 * np.clip(1 - edge_d / 0.004, 0, 1)
    thick_max = _cap_thickness(cap, lm, edge_d) + 0.0015 + 0.004 * np.clip((edge_d - 0.02) / 0.03, 0, 1)
    from mathutils import Vector
    for i in range(len(cap.v)):
        loc, nrm, _fi, _d = bb.find_nearest(Vector(cap.v[i]), 0.05)
        if loc is None:
            continue
        loc, nrm = np.array(loc), np.array(nrm)
        d = (cap.v[i] - loc) @ nrm
        if d < thick[i] or edge_d[i] < 0.004:
            cap.v[i] += nrm * (thick[i] - d)
        elif d > thick_max[i]:                    # no flaps standing off concave spots (behind the ears)
            cap.v[i] += nrm * (thick_max[i] - d)
    return cap


def cut_under_bun(cap, lm, seat, hole=0.09):
    """Open the cap under the top knot (the bun hides it): removes the pole of the cap's polar UV map, where
    triangles around the pole would fold in UV space."""
    c = lm["skull_c"]
    ax = _n(cw.bone_head("ff_hair_01") - c)
    pol = np.arccos(np.clip(_n(cap.v - c) @ ax, -1, 1))
    pole_pt = cap.v[np.argmin(pol)]
    if np.linalg.norm(pole_pt - seat) > 0.012:       # pole not under the bun: keep the cap closed
        return cap
    return P.clip(cap, hole - pol)


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
    # the first ring sits a few millimetres inside the cap so the bun's base meets the hair without a gap
    centers = [seat + axis * (r_bun * (-0.15 + 1.9 * t)) for t in np.linspace(0, 1, 9)]
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
    tail = _tail_clumps(C)
    return bun, tie, tail, seat, axis


def _tail_clumps(C, segs=16, lobes=4):
    """The tail as one broad, flat lock of hair (wide across the back, thin from the side - a round tapering tube
    reads as a horn): its cross-section has `lobes` strand clumps (shallow grooves along the length) that part
    toward the end, where each clump runs out into its own point (brush-like end, not one needle tip).
    UVs as lofted_tube (u around, v along, metres). The centre line is the ff_hair chain, so the height-based chain
    weights stay valid."""
    C = np.asarray(C, float)
    N = len(C)
    tt = np.linspace(0, 1, N)
    T = _n(np.gradient(C, axis=0))
    up = np.array([0, 1.0, 0.2])
    swell = 1.0 + 0.25 * np.sin(np.clip(tt / 0.6, 0, 1) * np.pi)           # spreads below the tie
    taper = np.clip(1 - tt, 0, 1) ** 0.6
    half_w = 0.0150 * swell * (0.45 + 0.55 * taper)                       # across the back
    half_t = 0.0062 * (0.55 + 0.45 * taper)                               # front-to-back thickness
    part_t = np.clip((tt - 0.55) / 0.45, 0, 1) ** 1.5                     # clumps part toward the end
    twist = np.linspace(0.0, 0.35, N)
    verts, faces, uvs = [], [], []
    th = 2 * np.pi * np.arange(segs) / segs
    prev_side = None
    ring_len = []
    for i in range(N):
        side = _n(np.cross(T[i], up))
        if prev_side is not None and side @ prev_side < 0:
            side = -side
        prev_side = side
        upv = _n(np.cross(side, T[i]))
        a = th + twist[i]
        lobe = np.cos(lobes * th)                                          # +1 clump crest .. -1 groove
        depth = 0.10 + 0.45 * part_t[i]
        r = 1.0 + depth * 0.5 * (lobe - 1.0)                               # grooves cut in, crests keep the hull
        along = np.zeros(segs)
        if i == N - 1:
            along = 0.016 * np.clip(lobe, 0, 1) ** 2                       # crests run out into points
        elif i == N - 2:
            along = 0.006 * np.clip(lobe, 0, 1) ** 2
        for k in range(segs):
            verts.append(C[i] + side * np.cos(a[k]) * half_w[i] * r[k] + upv * np.sin(a[k]) * half_t[i] * r[k]
                         + T[i] * along[k])
        ring_len.append(along)
    sv = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))])
    circ = 2 * np.pi * float(np.mean(0.5 * (half_w + half_t)))
    for i in range(N - 1):
        for k in range(segs):
            k2 = (k + 1) % segs
            faces.append([i * segs + k, i * segs + k2, (i + 1) * segs + k2, (i + 1) * segs + k])
            u0, u1 = k / segs * circ, (k + 1) / segs * circ
            uvs.append([(u0, sv[i] + ring_len[i][k]), (u1, sv[i] + ring_len[i][k2]),
                        (u1, sv[i + 1] + ring_len[i + 1][k2]), (u0, sv[i + 1] + ring_len[i + 1][k])])
    # close the end: centre vertex a little behind the crest points (the points stay free)
    verts.append(C[-1] + T[-1] * 0.003)
    tip = len(verts) - 1
    for k in range(segs):
        k2 = (k + 1) % segs
        faces.append([(N - 1) * segs + k, (N - 1) * segs + k2, tip])
        uvs.append([(k / segs * circ, sv[-1] + ring_len[-1][k]), ((k + 1) / segs * circ, sv[-1] + ring_len[-1][k2]),
                    ((k + 0.5) / segs * circ, sv[-1] + 0.004)])
    p = P.Part("hair_tail", np.array(verts), faces, "hair", uvs=uvs)
    f0 = faces[0]
    e1 = p.v[f0[1]] - p.v[f0[0]]
    e2 = p.v[f0[3]] - p.v[f0[0]]
    if np.cross(e1, e2) @ (p.v[f0[0]] - C[0]) < 0:                       # outward winding
        p.f = [list(reversed(f)) for f in p.f]
        p.uv = [list(reversed(u)) for u in p.uv]
    return p


def chain_weight_tail(part, chain_bones, z_centers):
    """Weights along a vertical-ish chain by height (hats between the given centres)."""
    H = cw.hats(-part.v[:, 2], [-z for z in z_centers])
    W = np.zeros((len(part.v), cw.NB))
    for j, b in enumerate(chain_bones):
        W[:, cw.BI[b]] = H[:, j]
    return W


# ---------------------------------------------------------------------------------------------------- eyes
def build_eyes(lm, segs=18, rings=12):
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
        # (theta < ~32 deg) gets a big share of the texture; the back pole sits on the outer circle. UVs come from the
        # grid indices (not the positions) so the pole corners keep their own azimuth.
        sx = 1 if s == "l" else -1

        def uv_ik(i, k):
            th = np.pi * i / rings
            ph = 2 * np.pi * k / segs
            ph = np.arctan2(np.sin(ph), np.cos(ph) * sx)
            rr = 0.5 * (th / np.pi) ** 0.6 * 0.98
            return (float(0.5 + rr * np.cos(ph)), float(0.5 + rr * np.sin(ph)))
        uvs = [[uv_ik(i, k), uv_ik(i + 1, k), uv_ik(i + 1, k + 1), uv_ik(i, k + 1)]
               for i in range(rings) for k in range(segs)]
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


def build_lashes(st, lm):
    """Opaque upper-lash strips along the upper lid margins (hair material, both faces). The lid margin comes from
    the base mesh's CC0 upper-lash helper strip (its root row), which the fit moves with the eyelids."""
    out = []
    for s in ("l", "r"):
        c = lm[f"eye_c_{s}"]
        fs = [f for f, g in zip(st.obj.faces, st.obj.groups) if g == f"helper-{s}-eyelashes-2"]
        ids = sorted({i for f in fs for i in f})
        if len(ids) < 8:
            continue
        hv = st.v[ids]
        d = np.linalg.norm(hv - c, axis=1)
        root = hv[d <= np.percentile(d, 40)]
        # one point per x slot, inner -> outer corner
        order = np.argsort(np.abs(root[:, 0]))
        root = root[order]
        if len(root) < 4:
            continue
        pts = G.smooth_polyline(G.resample_polyline(root, 14, False), 2, False)
        # sit just on the lid margin (a hair outside the eyeball)
        rel = pts - c
        rr = np.linalg.norm(rel, axis=1, keepdims=True)
        pts = c + rel / rr * np.maximum(rr, lm[f"eye_r_{s}"] + 0.0012)
        verts, faces = [], []
        n_out = len(pts)
        for i, p in enumerate(pts):
            t = i / (n_out - 1)
            # short and mostly forward: from the front they read as a fine dark lash line, not a slab
            ln = 0.0022 + 0.0016 * np.sin(np.pi * min(1.0, t * 1.15))        # longer toward the outer third
            fwd = np.array([0, -1.0, 0])
            up = np.array([0, 0, 1.0])
            outward = _n(np.array([np.sign(c[0]) * 0.35, 0, 0]) + 0.2 * up)
            tip = p + fwd * ln * 0.85 + up * ln * 0.40 + outward * ln * 0.25 * t
            mid = p + fwd * ln * 0.50 + up * ln * 0.08
            base_in = p + np.array([0, 0.0006, -0.0003])
            verts += [base_in, p + fwd * 0.0004, mid, tip]
        for i in range(n_out - 1):
            for k in range(3):
                a, b = i * 4 + k, i * 4 + k + 1
                faces.append([a, b, b + 4, a + 4])
        luv = []
        for i in range(n_out - 1):
            for k in range(3):
                luv.append([(i * 0.003, k * 0.002), (i * 0.003, (k + 1) * 0.002), ((i + 1) * 0.003, (k + 1) * 0.002),
                            ((i + 1) * 0.003, k * 0.002)])
        # second face (reversed, a quarter millimetre behind) so the strip shows from above as well
        verts = np.array(verts)
        nv = len(verts)
        lp0 = P.Part("tmp", verts, faces, "hair")
        back = verts - lp0.vertex_normals() * 0.00025
        faces2 = faces + [[v + nv for v in reversed(f)] for f in faces]
        luv2 = luv + [list(reversed(u)) for u in luv]
        lp = P.Part("lash_" + s, np.vstack([verts, back]), faces2, "hair", uvs=luv2)
        lp.W[:, cw.BI["head"]] = 1.0
        out.append(lp)
    return out
