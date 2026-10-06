"""Garments built on the CC0 helper geometry of the base mesh (tights / skirt shells share its topology and weights):
wrap-front tunic (+ collar band, cuffs), split hem panels, loose trousers gathered into shin wraps, soft shoes, sash.
numpy + mathutils (BVH) only."""
import math

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

import ch_part as P
import ch_regions as R
import ch_weights as cw

TORSO_BONES = {"pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "clavicle_l", "clavicle_r",
               "neck_01", "neck_02"}


def bvh_of(part):
    return BVHTree.FromPolygons([tuple(p) for p in part.v], [tuple(f) for f in part.f])


def enforce_clearance(v, bvh, clear, mask=None, max_dist=0.25):
    """Push vertices out so they sit at least `clear` in front of the reference surface (along its normal)."""
    v = v.copy()
    idx = range(len(v)) if mask is None else np.nonzero(mask)[0]
    for i in idx:
        loc, nrm, _fi, dist = bvh.find_nearest(Vector(v[i]), max_dist)
        if loc is None:
            continue
        loc = np.array(loc)
        nrm = np.array(nrm)
        d = (v[i] - loc) @ nrm
        if d < clear:
            v[i] = v[i] + nrm * (clear - d)
    return v


def taubin(part, iters, lam=0.5, mu=-0.53, mask=None, pin=None):
    A = part.adjacency()
    v = part.v
    for _ in range(iters):
        for k in (lam, mu):
            d = A @ v - v
            if mask is not None:
                d[~mask] = 0
            if pin is not None:
                d[pin] = 0
            v = v + k * d
    part.v = v
    return part


def bridge_hollows(part, mask, iters=60, step=0.7):
    """Outward-only Laplacian: a vertex lying below the mean of its neighbours (a hollow) moves out along its
    normal; convex areas never move. Converges to a membrane stretched over the bumps, like cloth under light
    tension. The mask fades over two rings of neighbours so the bridged area blends into the rest."""
    A = part.adjacency()
    w = mask.astype(float)
    for _ in range(2):
        w = np.maximum(w, 0.5 * (A @ w))
    v = part.v.copy()
    for _ in range(iters):
        part.v = v
        n = part.vertex_normals()
        dn = ((A @ v - v) * n).sum(axis=1)
        v = v + n * (np.maximum(dn, 0.0) * step * w)[:, None]
    part.v = v
    return part


def boundary_vertex_mask(part):
    m = np.zeros(len(part.v), dtype=bool)
    for a, b in part.edges_boundary():
        m[a] = m[b] = True
    return m


def fold_under(part, loops_sel, depth=0.008, inset=0.004, mat=None):
    """Turn the edge of the cloth under (one strip of quads) so free edges read as a thick hem from outside.
    loops_sel: list of boundary loops (vertex lists in face winding)."""
    nrm = part.vertex_normals()
    v = list(part.v)
    W = list(part.W)
    faces = list(part.f)
    mats = list(part.m)
    uvs = list(part.uv) if part.uv is not None else None
    cen = part.v.mean(axis=0)
    for loop in loops_sel:
        n = len(loop)
        new = []
        for k, a in enumerate(loop):
            prev, nxt = part.v[loop[k - 1]], part.v[loop[(k + 1) % n]]
            tang = nxt - prev
            tang /= (np.linalg.norm(tang) + 1e-12)
            # direction pointing back into the cloth (in the surface, perpendicular to the edge)
            inward = np.cross(nrm[a], tang)
            # the face winding puts the cloth on the left of a->b when seen from outside: cross(n, t)
            v.append(part.v[a] - nrm[a] * inset + inward * depth)
            W.append(part.W[a].copy())
            new.append(len(v) - 1)
        for k in range(n):
            a, b = loop[k], loop[(k + 1) % n]
            na, nb = new[k], new[(k + 1) % n]
            faces.append([b, a, na, nb])
            mats.append(mat or part.m[0])
            if uvs is not None:
                uvs.append([(0, 0)] * 4)
    out = P.Part(part.name, np.array(v), faces, mats, np.array(W), uvs)
    return out


# ------------------------------------------------------------------------------------------------ helper parts
def helper_part(st, group, W_all, name, mat):
    obj = st.obj
    fi = obj.group_faces(lambda g: g == group)
    used = sorted({i for f in fi for i in obj.faces[f]})
    remap = {o: i for i, o in enumerate(used)}
    return P.Part(name, st.v[used], [[remap[i] for i in obj.faces[f]] for f in fi], mat, W_all[used],
                  src_index=np.array(used))


# ------------------------------------------------------------------------------------------------ tunic
def build_tunic(st, W_all, body):
    """Upper tunic: tights torso + 3/4 sleeves, V wrap neckline, loose drape. Returns (tunic, collar_loop_info)."""
    t = helper_part(st, "helper-tights", W_all, "tunic", "cloth_main")
    dom = R.dominant_bones(t.W)
    ap = R.arm_params(t.v)
    side_l = t.v[:, 0] > 0
    la = np.where(side_l, ap["la_l"], ap["la_r"])
    is_torso = np.isin(dom, list(TORSO_BONES))
    is_arm = np.array([d.startswith(("upperarm", "lowerarm")) for d in dom])
    fc_keep = []
    for f in t.f:
        k = all((is_torso[i] or is_arm[i]) for i in f) and all(t.v[i, 2] > R.WAIST_Z - 0.08 for i in f)
        fc_keep.append(k)
    t = t.subset(fc_keep, "tunic")
    # loosen: offset grows from chest to waist and along the sleeve
    nrm = t.vertex_normals()
    ap = R.arm_params(t.v)
    la = np.where(t.v[:, 0] > 0, ap["la_l"], ap["la_r"])
    ua = np.where(t.v[:, 0] > 0, ap["ua_l"], ap["ua_r"])
    dom = R.dominant_bones(t.W)
    armish = np.array([d.startswith(("upperarm", "lowerarm")) for d in dom])
    off = np.full(len(t.v), 0.012)
    zf = np.clip((1.30 - t.v[:, 2]) / 0.25, 0, 1)          # looser toward the waist (blouses over the sash)
    off += 0.012 * zf
    sleeve_t = np.where(la > 0, 1.0 + la, ua)              # 0 shoulder .. 1 elbow .. 1.22 cuff
    off = np.where(armish, 0.010 + 0.016 * np.clip(sleeve_t - 0.3, 0, 1.0), off)
    t.v = t.v + nrm * off[:, None]
    # drape first (fills the body's concavities like a loose garment, keeps clear of the body) ...
    bb = bvh_of(body)
    for _ in range(4):
        taubin(t, 6)
        t.v = enforce_clearance(t.v, bb, 0.007)
    # cloth spans hollows instead of following them: bridge the sternum cleft between the pectorals and the spine
    # groove (a shrink-wrapped cleft reads as a bust under the wrap front)
    dom = R.dominant_bones(t.W)
    torso = np.isin(dom, list(TORSO_BONES)) & (np.abs(t.v[:, 0]) < 0.15) & (t.v[:, 2] > R.WAIST_Z + 0.01)
    bridge_hollows(t, torso, iters=60)
    # ... then cut exact edges on the smooth shell: waist (under the sash), sleeve ends, wrap neckline
    t = P.clip(t, (R.WAIST_Z - 0.035) - t.v[:, 2])
    ap = R.arm_params(t.v)
    la = np.where(t.v[:, 0] > 0, ap["la_l"], ap["la_r"])
    t = P.clip(t, la - R.SLEEVE_T)
    t = clip_neckline(t)
    return t


def neckline_field(v):
    """> 0 where the tunic stays (below the neckline), < 0 above it. Front: wrap V; back: a straight neckline."""
    x, y, z = v[:, 0], v[:, 1], v[:, 2]
    f_front = (R.NECK_V_Z + R.NECK_V_SLOPE * np.abs(x)) - z
    f_back = R.COLLAR_BACK_Z - z
    w = np.clip((0.045 - y) / 0.07, 0, 1)        # 1 at the front of the chest, 0 at the back
    w = w * w * (3 - 2 * w)                        # smooth blend: rounded corners on the shoulders
    return w * f_front + (1 - w) * f_back


def clip_neckline(t):
    return P.clip(t, -neckline_field(t.v))


# ------------------------------------------------------------------------------------------------ sampling utils
def sample_weights(points, part, bvh):
    """Weights at arbitrary points: inverse-distance blend of the vertices of the nearest face of `part`."""
    out = np.zeros((len(points), cw.NB))
    for i, p in enumerate(points):
        loc, _n, fi, _d = bvh.find_nearest(Vector(p), 1.0)
        if fi is None:
            continue
        ids = part.f[fi]
        d = np.linalg.norm(part.v[ids] - np.asarray(p), axis=1) + 1e-4
        w = 1.0 / d ** 2
        out[i] = (w[:, None] * part.W[ids]).sum(axis=0) / w.sum()
    return out


def surface_frame(points, bvh):
    locs, nrms = [], []
    for p in points:
        loc, n, _fi, _d = bvh.find_nearest(Vector(p), 1.0)
        locs.append(np.array(loc))
        nrms.append(np.array(n))
    return np.array(locs), np.array(nrms)


def resample_polyline(pts, n, closed):
    pts = np.asarray(pts, float)
    if closed:
        pts = np.vstack([pts, pts[:1]])
    seg = np.linalg.norm(np.diff(pts, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    L = s[-1]
    tt = np.linspace(0, L, n, endpoint=not closed)
    return np.stack([np.interp(tt, s, pts[:, k]) for k in range(3)], axis=1)


def smooth_polyline(pts, iters, closed):
    p = pts.copy()
    for _ in range(iters):
        if closed:
            p = 0.25 * np.roll(p, 1, 0) + 0.5 * p + 0.25 * np.roll(p, -1, 0)
        else:
            q = p.copy()
            q[1:-1] = 0.25 * p[:-2] + 0.5 * p[1:-1] + 0.25 * p[2:]
            p = q
    return p


def smooth_normals_at(points, part, sigma=0.015):
    """Gaussian-weighted average of the part's vertex normals around each point (smooth frames for sweeps)."""
    from scipy.spatial import cKDTree
    vn = part.vertex_normals()
    tree = cKDTree(part.v)
    out = np.zeros((len(points), 3))
    for i, p in enumerate(points):
        ids = tree.query_ball_point(p, 3 * sigma)
        if not ids:
            ids = [tree.query(p)[1]]
        d = np.linalg.norm(part.v[ids] - p, axis=1)
        w = np.exp(-0.5 * (d / sigma) ** 2)
        out[i] = (w[:, None] * vn[ids]).sum(axis=0)
    return out / (np.linalg.norm(out, axis=1, keepdims=True) + 1e-12)


def band(name, curve, bvh, src_part, d_hint, profile, closed, mat, n_override=None, sigma=0.015, conform=True):
    """Sweep a 2D profile [(d, n), ...] along a curve lying on a surface. d = in-surface direction (hint function
    gives the side), n = surface normal (smoothed). Returns a Part (weights sampled from src_part)."""
    c, _fn = surface_frame(curve, bvh)
    nrm = smooth_normals_at(c, src_part, sigma)
    for _ in range(4):
        nrm = smooth_polyline(nrm, 1, closed)
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    if n_override is not None:
        nrm = n_override(c, nrm)
    N = len(c)
    if closed:
        tang = np.roll(c, -1, 0) - np.roll(c, 1, 0)
    else:
        tang = np.gradient(c, axis=0)
    tang /= np.linalg.norm(tang, axis=1, keepdims=True) + 1e-12
    nrm = nrm - tang * np.einsum("ij,ij->i", nrm, tang)[:, None]
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
    d = np.cross(nrm, tang)
    d /= np.linalg.norm(d, axis=1, keepdims=True) + 1e-12
    # one consistent side for the whole curve (majority vote against the hint)
    if np.einsum("ij,ij->i", d, d_hint(c)).sum() < 0:
        d = -d
    K = len(profile)
    verts = []
    for i in range(N):
        for a, b in profile:
            verts.append(c[i] + d[i] * a + nrm[i] * b)
    verts = np.array(verts)
    if conform:
        # the flat part of the band follows the garment surface (a straight sweep in the tangent plane dips under
        # it where the surface is concave, e.g. where the shoulder meets the neck -> ragged intersection line)
        pa = np.array([a for a, _b in profile])
        pb = np.array([b for _a, b in profile])
        tk = np.clip((pa - 0.002) / 0.008, 0, 1)
        tk = tk * tk * (3 - 2 * tk)
        tk[: int(np.argmin(pa)) + 1] = 0.0          # only the outer face (after the fold), not the tucked flap
        if tk.max() > 0:
            ks = np.nonzero(tk > 0)[0]
            q = np.array([c[i] + d[i] * pa[k] for i in range(N) for k in ks])
            loc = np.array([bvh.find_nearest(Vector(x), 0.1)[0] or Vector(x) for x in q])
            ns = smooth_normals_at(loc, src_part, 0.008)
            conf = loc + ns * np.tile(pb[ks], N)[:, None]
            rows = np.array([i * K + k for i in range(N) for k in ks])
            w = np.tile(tk[ks], N)[:, None]
            verts[rows] = (1 - w) * verts[rows] + w * conf
    faces = []
    rng = range(N) if closed else range(N - 1)
    for i in rng:
        j = (i + 1) % N
        for k in range(K - 1):
            faces.append([i * K + k, i * K + k + 1, j * K + k + 1, j * K + k])
    # UVs in metres: u along the curve, v across the profile
    seg = np.linalg.norm(np.diff(np.vstack([c, c[:1]]) if closed else c, axis=0), axis=1)
    su = np.concatenate([[0.0], np.cumsum(seg)])
    pk = np.concatenate([[0.0], np.cumsum([np.hypot(profile[k + 1][0] - profile[k][0], profile[k + 1][1] - profile[k][1])
                                           for k in range(K - 1)])])
    uvs = []
    for i in rng:
        j = i + 1
        for k in range(K - 1):
            uvs.append([(su[i], pk[k]), (su[i], pk[k + 1]), (su[j], pk[k + 1]), (su[j], pk[k])])
    p = P.Part(name, verts, faces, mat, uvs=uvs)
    # orientation: the outer face (profile segment with the largest n) must face along +n
    kk = int(np.argmax([0.5 * (profile[k][1] + profile[k + 1][1]) for k in range(K - 1)]))
    votes = 0.0
    for i in list(rng)[:: max(1, N // 16)]:
        fc = faces[i * (K - 1) + kk]
        e1 = verts[fc[1]] - verts[fc[0]]
        e2 = verts[fc[3]] - verts[fc[0]]
        votes += np.cross(e1, e2) @ nrm[i]
    if votes < 0:
        p.f = [list(reversed(f)) for f in p.f]
        p.uv = [list(reversed(u)) for u in p.uv]
    p.W = sample_weights(np.repeat(c, K, axis=0), src_part, bvh)
    p.W = cw.normalize(p.W)
    return p


def loops_of(part):
    return [np.array(l) for l in P.boundary_loops(part)]


# ------------------------------------------------------------------------------------------------ collar + cuffs
COLLAR_W = 0.034
COLLAR_PROFILE = [(0.010, -0.012), (0.004, -0.006), (-0.0015, -0.0012), (-0.003, 0.0012), (-0.0005, 0.0036), (COLLAR_W * 0.5, 0.0040),
                  (COLLAR_W, 0.0034), (COLLAR_W + 0.0025, 0.0007)]
CUFF_W = 0.045
CUFF_PROFILE = [(0.008, -0.004), (-0.001, -0.0018), (-0.0035, 0.0015), (-0.001, 0.0045), (CUFF_W * 0.5, 0.0050),
                (CUFF_W, 0.0042), (CUFF_W + 0.003, 0.0008)]


def build_collar_and_cuffs(tunic):
    bvh = bvh_of(tunic)
    loops = loops_of(tunic)
    neck = max(loops, key=lambda l: tunic.v[l, 2].mean())
    cpts = smooth_polyline(resample_polyline(tunic.v[neck], 84, True), 5, True)
    cen = cpts.mean(axis=0)
    collar = band("collar", cpts, bvh, tunic, lambda c: c - cen, COLLAR_PROFILE, True, "cloth_accent")
    # diagonal edge of the outer (left) panel, from the crossing point down to the right hip (under the sash)
    xs = np.linspace(0.004, -0.150, 40)
    pts = []
    for x in xs:
        z = R.NECK_V_Z + 2.4 * x
        hit = bvh.ray_cast(Vector((x, -0.6, z)), Vector((0, 1, 0)), 2.0)
        if hit[0] is not None:
            pts.append(np.array(hit[0]))
    pts = smooth_polyline(resample_polyline(np.array(pts), 30, False), 2, False)
    diag = band("collar_diag", pts, bvh, tunic, lambda c: np.tile([0.6, 0.0, 0.8], (len(c), 1)), COLLAR_PROFILE,
                False, "cloth_accent")
    parts = [collar, diag]
    # sleeve cuffs: the two loops farthest from the body axis
    arm_loops = sorted(loops, key=lambda l: -np.abs(tunic.v[l, 0]).mean())[:2]
    for l in arm_loops:
        s = "l" if tunic.v[l, 0].mean() > 0 else "r"
        pts = smooth_polyline(resample_polyline(tunic.v[l], 40, True), 1, True)
        sh = cw.bone_head("upperarm_" + s)
        parts.append(band("cuff_" + s, pts, bvh, tunic, lambda c, sh=sh: sh - c, CUFF_PROFILE, True, "cloth_accent"))
    return parts


# ------------------------------------------------------------------------------------------------ hem panels
def pelvis_azimuth(v):
    """0 = front (-Y), +90 = character left, 180 = back."""
    return np.degrees(np.arctan2(v[:, 0], -(v[:, 1] - 0.0)))


def hang(sk, flare=0.10, win=10.0):
    """Cloth hangs straight down from its widest point: per azimuth, the radius below a point is at least the
    largest radius above it (+ a slight A-line flare)."""
    cy = sk.v[:, 1].mean()
    d = sk.v[:, :2] - np.array([0.0, cy])
    r = np.linalg.norm(d, axis=1)
    az = np.degrees(np.arctan2(d[:, 0], -d[:, 1]))
    z = sk.v[:, 2]
    ztop = z.max()
    rn = r.copy()
    for i in range(len(r)):
        da = np.abs((az - az[i] + 180) % 360 - 180)
        m = (da < win) & (z >= z[i])
        rn[i] = max(r[i], r[m].max()) + flare * (ztop - z[i]) * 0.25
    s = rn / np.maximum(r, 1e-6)
    sk.v[:, 0] = d[:, 0] * s
    sk.v[:, 1] = cy + d[:, 1] * s
    return sk


def build_hem(st, W_all, trousers):
    sk = helper_part(st, "helper-skirt", W_all, "hem", "cloth_main")
    sk = P.clip(sk, sk.v[:, 2] - (R.WAIST_Z + 0.035))          # keep below the sash top
    sk = P.clip(sk, R.HEM_Z - sk.v[:, 2])                      # keep above the hem line
    hang(sk)
    # side slits: wedge around azimuth +-90 deg, opening toward the hem
    az = np.abs(pelvis_azimuth(sk.v))
    half = np.interp(sk.v[:, 2], [R.HEM_Z, R.SLIT_Z - 0.06, R.SLIT_Z], [7.5, 2.5, -1.0])
    sk = P.clip(sk, half - np.abs(az - 90.0))
    # drape: hang a little looser than the trousers, smooth, keep clear of them
    nrm = sk.vertex_normals()
    sk.v = sk.v + nrm * 0.004
    bt = bvh_of(trousers)
    for _ in range(3):
        taubin(sk, 4)
        sk.v = enforce_clearance(sk.v, bt, 0.010, max_dist=0.2)
    # contrasting border: bottom band + slit edges (cloth_accent)
    az = np.abs(pelvis_azimuth(sk.v))
    edge = np.minimum(sk.v[:, 2] - (R.HEM_Z + 0.032),
                      np.where(sk.v[:, 2] < R.SLIT_Z, np.abs(az - 90.0) - (np.interp(sk.v[:, 2], [R.HEM_Z, R.SLIT_Z - 0.06,
                               R.SLIT_Z], [7.5, 2.5, -1.0]) + 5.0), 1.0) * 0.006)
    sk, _side = P.slice_assign(sk, edge, mat_neg="cloth_accent", mat_pos="cloth_main")
    return sk


# ------------------------------------------------------------------------------------------------ trousers
def build_trousers(st, W_all, body):
    t = helper_part(st, "helper-tights", W_all, "trousers", "cloth_main")
    dom = R.dominant_bones(t.W)
    armish = np.array([d.startswith(("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"))
                       or "metacarpal" in d for d in dom])
    keep = [all((t.v[i, 2] < R.WAIST_Z + 0.06) and not armish[i] for i in f) for f in t.f]
    t = t.subset(keep, "trousers")
    nrm = t.vertex_normals()
    z = t.v[:, 2]
    x = t.v[:, 0]
    # looseness by height (thigh 2.4 cm, knee 2.0, calf 1.6), blousing above the wraps, tight under them
    off = np.interp(z, [0.0, R.SHIN_WRAP_TOP - 0.01, R.SHIN_WRAP_TOP + 0.03, R.SHIN_WRAP_TOP + 0.08, 0.50, 0.70, 0.90, 1.10],
                    [0.004, 0.004, 0.036, 0.034, 0.028, 0.027, 0.022, 0.010])
    # inner leg surfaces (normal toward the mid-line) stay closer so the legs don't merge
    inward = -np.sign(x) * nrm[:, 0]
    off *= np.where(z < 0.86, 1.0 - 0.6 * np.clip(inward, 0, 1), 1.0)
    t.v = t.v + nrm * off[:, None]
    t = P.clip(t, (R.SHOE_TOP - 0.025) - t.v[:, 2])
    t = P.clip(t, t.v[:, 2] - (R.WAIST_Z + 0.04))
    bb = bvh_of(body)
    under_wrap = t.v[:, 2] < R.SHIN_WRAP_TOP - 0.015
    for _ in range(3):
        taubin(t, 4, mask=~under_wrap)
        t.v = enforce_clearance(t.v, bb, 0.004, max_dist=0.2)
    # shin wraps: the lower leg becomes the wrap layer
    t, _s = P.slice_assign(t, t.v[:, 2] - R.SHIN_WRAP_TOP, mat_neg="wraps", mat_pos="cloth_main")
    return t


# ------------------------------------------------------------------------------------------------ shoes
def build_shoes(st, W_all, body):
    t = helper_part(st, "helper-tights", W_all, "shoes", "shoes")
    keep = [all(t.v[i, 2] < R.SHOE_TOP + 0.03 for i in f) for f in t.f]
    t = t.subset(keep, "shoes")
    nrm = t.vertex_normals()
    t.v = t.v + nrm * 0.0035
    # merge the toes into a soft rounded toe box
    toe = t.v[:, 2] < 0.06
    taubin(t, 30, mask=toe)
    bb = bvh_of(body)
    t.v = enforce_clearance(t.v, bb, 0.003, max_dist=0.1)
    t = P.clip(t, (R.SHOE_TOP) - t.v[:, 2], keep_negative=False)
    # flat sole
    low = t.v[:, 2] < 0.012
    t.v[low, 2] = np.minimum(t.v[low, 2], 0.0) * 0.0 + np.clip(t.v[low, 2] - 0.012, -0.012, 0) * 0.15
    return t


def build_sole_rim(shoes):
    nrm = shoes.vertex_normals()
    sel = [all(shoes.v[i, 2] < 0.006 for i in f) for f in shoes.f]
    out = P.split_region_with_lip(shoes, sel, 0.0, lip=False)
    return out


# ------------------------------------------------------------------------------------------------ sash
SASH_Z0, SASH_Z1 = 0.982, 1.078
KNOT_AZ = 52.0          # front-left (character left = +x)
TUCK_AZ = -50.0


def _radius_field(parts, cy):
    allv = np.vstack([p.v for p in parts])
    d = allv[:, :2] - np.array([0.0, cy])
    r = np.linalg.norm(d, axis=1)
    az = np.degrees(np.arctan2(d[:, 0], -d[:, 1]))
    return allv[:, 2], az, r


def build_sash(under_parts, src_for_weights):
    """Two-turn waist sash: band with rounded edges and a crease between the turns, a knot at the front-left hip
    with a long tail (ff_sash_l), and the tucked end hanging at the front-right (ff_sash_r)."""
    allv = np.vstack([p.v for p in under_parts])
    band_m = (allv[:, 2] > SASH_Z0 - 0.03) & (allv[:, 2] < SASH_Z1 + 0.03)
    cy = 0.5 * (allv[band_m, 1].min() + allv[band_m, 1].max())
    z, az, r = _radius_field(under_parts, cy)
    NA = 72
    zs = np.array([SASH_Z0, SASH_Z0 + 0.005, SASH_Z0 + 0.016, 1.030, 1.044, SASH_Z1 - 0.016, SASH_Z1 - 0.005, SASH_Z1])
    lift = np.array([-0.007, 0.0040, 0.0062, 0.0040, 0.0062, 0.0062, 0.0040, -0.007])
    angs = np.linspace(-180, 180, NA, endpoint=False)
    R0 = np.zeros((len(zs), NA))
    for j, a in enumerate(angs):
        da = np.abs((az - a + 180) % 360 - 180)
        m = (da < 9) & (z > SASH_Z0 - 0.02) & (z < SASH_Z1 + 0.02)
        R0[:, j] = r[m].max() if m.any() else np.nan
    # one radius per azimuth (cinched cloth), smoothed around the waist
    rr = np.nanmax(R0, axis=0)
    for _ in range(3):
        rr = 0.25 * np.roll(rr, 1) + 0.5 * rr + 0.25 * np.roll(rr, -1)
    verts = []
    for k, zk in enumerate(zs):
        for j, a in enumerate(angs):
            ar = np.radians(a)
            rad = rr[j] + 0.003 + lift[k]
            verts.append([np.sin(ar) * rad, cy - np.cos(ar) * rad, zk])
    verts = np.array(verts)
    faces = []
    for k in range(len(zs) - 1):
        for j in range(NA):
            j2 = (j + 1) % NA
            faces.append([k * NA + j, k * NA + j2, (k + 1) * NA + j2, (k + 1) * NA + j])
    rmean = float(np.mean(rr)) + 0.006
    zpro = np.concatenate([[0.0], np.cumsum(np.hypot(np.diff(zs), np.diff(lift)))])
    uvs = []
    for k in range(len(zs) - 1):
        for j in range(NA):
            u0 = j / NA * 2 * np.pi * rmean
            u1 = (j + 1) / NA * 2 * np.pi * rmean
            uvs.append([(u0, zpro[k]), (u1, zpro[k]), (u1, zpro[k + 1]), (u0, zpro[k + 1])])
    band_p = P.Part("sash_band", verts, faces, "sash", uvs=uvs)
    f0 = faces[NA * 3]
    e1 = verts[f0[1]] - verts[f0[0]]
    e2 = verts[f0[3]] - verts[f0[0]]
    outward = verts[f0[0]] - np.array([0, cy, verts[f0[0], 2]])
    if np.cross(e1, e2) @ outward < 0:
        band_p.f = [list(reversed(f)) for f in band_p.f]
        band_p.uv = [list(reversed(u)) for u in band_p.uv]
    bvh_src = bvh_of(src_for_weights)
    band_p.W = cw.normalize(sample_weights(band_p.v, src_for_weights, bvh_src))

    def surf_point(a_deg, zq, extra):
        ar = np.radians(a_deg)
        j = int(round((a_deg + 180) / 360 * NA)) % NA
        rad = rr[j] + 0.009 + extra
        return np.array([np.sin(ar) * rad, cy - np.cos(ar) * rad, zq]), np.array([np.sin(ar), -np.cos(ar), 0.0])

    parts = [band_p]
    # knot: a plump rounded lump with a pinched middle, facing out from the hip
    kc, kn = surf_point(KNOT_AZ, 1.024, 0.010)
    side = _n2(np.cross(np.array([0, 0, 1.0]), kn))
    up = np.array([0, 0, 1.0])
    kv, kf = [], []
    nu, nv = 12, 9
    for i in range(nv + 1):
        th = np.pi * i / nv
        for k in range(nu):
            ph = 2 * np.pi * k / nu
            x = np.sin(th) * np.cos(ph)
            y = np.sin(th) * np.sin(ph)
            zz = np.cos(th)
            pinch = 1.0 - 0.18 * np.exp(-((x) / 0.30) ** 2)
            lobe = 1.0 + 0.10 * np.cos(2 * ph)                    # two rounded lobes left / right of the knot
            p = kc + side * x * 0.046 * lobe + up * zz * 0.034 * pinch + kn * (y * 0.019 + 0.012) * pinch
            kv.append(p)
    for i in range(nv):
        for k in range(nu):
            k2 = (k + 1) % nu
            kf.append([i * nu + k, i * nu + k2, (i + 1) * nu + k2, (i + 1) * nu + k])
    kuv = []
    for i in range(nv):
        for k in range(nu):
            kuv.append([(k / nu * 0.20, i / nv * 0.09), ((k + 1) / nu * 0.20, i / nv * 0.09),
                        ((k + 1) / nu * 0.20, (i + 1) / nv * 0.09), (k / nu * 0.20, (i + 1) / nv * 0.09)])
    knot = P.Part("sash_knot", np.array(kv), kf, "sash", uvs=kuv)
    # orient outward
    cen = np.array(kv).mean(axis=0)
    fq = kf[nu * 4]
    e1 = knot.v[fq[1]] - knot.v[fq[0]]
    e2 = knot.v[fq[3]] - knot.v[fq[0]]
    if np.cross(e1, e2) @ (knot.v[fq[0]] - cen) < 0:
        knot.f = [list(reversed(f)) for f in knot.f]
        knot.uv = [list(reversed(u)) for u in knot.uv]
    knot.W = cw.normalize(sample_weights(knot.v, src_for_weights, bvh_src))
    parts.append(knot)
    # tails
    parts.append(_sash_tail("sash_tail_l", KNOT_AZ - 3, 1.000, 0.700, 0.070, 0.060, surf_point, under_parts, cy,
                            ["pelvis", "ff_sash_l_01", "ff_sash_l_02"], swing=+1))
    parts.append(_sash_tail("sash_tail_r", TUCK_AZ, 1.010, 0.770, 0.062, 0.054, surf_point, under_parts, cy,
                            ["pelvis", "ff_sash_r_01", "ff_sash_r_02"], swing=-1))
    return parts


def _n2(a):
    return a / (np.linalg.norm(a) + 1e-12)


def _sash_tail(name, az0, z_top, z_bot, w_top, w_bot, surf_point, under_parts, cy, chain, swing):
    """Hanging double-layer ribbon following the hem surface; slanted cut end."""
    nz = 14
    zs = np.linspace(z_top, z_bot, nz)
    z_all, az_all, r_all = _radius_field(under_parts, cy)
    cols = []
    for i, zq in enumerate(zs):
        t = i / (nz - 1)
        a = az0 + swing * 4.0 * t
        da = np.abs((az_all - a + 180) % 360 - 180)
        m = (da < 10) & (np.abs(z_all - zq) < 0.03)
        rad = (r_all[m].max() if m.any() else 0.16) + 0.012 + 0.010 * t
        ar = np.radians(a)
        c = np.array([np.sin(ar) * rad, cy - np.cos(ar) * rad, zq])
        nrm = np.array([np.sin(ar), -np.cos(ar), 0.0])
        cols.append((c, nrm))
    C = np.array([c for c, _ in cols])
    C = smooth_polyline(C, 2, False)
    verts, faces = [], []
    th = 0.0022
    prof = [(-0.5, th), (0.5, th), (0.5, -th), (-0.5, -th)]    # closed thin box (front, edge, back, edge)
    for i in range(nz):
        t = i / (nz - 1)
        w = w_top + (w_bot - w_top) * t
        nrm = cols[i][1]
        side = _n2(np.cross(np.array([0, 0, 1.0]), nrm))
        twist = np.radians(8.0 * swing * t)
        sd = side * np.cos(twist) + nrm * np.sin(twist)
        nd = nrm * np.cos(twist) - side * np.sin(twist)
        cz = C[i].copy()
        for a, b in prof:
            p = cz + sd * a * w + nd * b
            if i == nz - 1:                          # slanted cut
                p[2] -= 0.03 * (a + 0.5)
            verts.append(p)
    K = len(prof)
    for i in range(nz - 1):
        for k in range(K):
            k2 = (k + 1) % K
            faces.append([i * K + k, i * K + k2, (i + 1) * K + k2, (i + 1) * K + k])
    faces.append([(nz - 1) * K + k for k in range(K)][::-1])
    V_ = np.array(verts)
    seg = np.linalg.norm(np.diff(C, axis=0), axis=1)
    sv = np.concatenate([[0.0], np.cumsum(seg)])
    per = [0.0, w_top, w_top + 2 * th, 2 * w_top + 2 * th, 2 * w_top + 4 * th]
    uvs = []
    for i in range(nz - 1):
        for k in range(K):
            uvs.append([(per[k], sv[i]), (per[k + 1], sv[i]), (per[k + 1], sv[i + 1]), (per[k], sv[i + 1])])
    uvs.append([(0.01 * k, sv[-1] + 0.01) for k in range(K)][::-1])
    p = P.Part(name, V_, faces, "sash", uvs=uvs)
    # orientation (front face outward)
    f0 = faces[0]
    e1 = p.v[f0[1]] - p.v[f0[0]]
    e2 = p.v[f0[3]] - p.v[f0[0]]
    if np.cross(e1, e2) @ cols[0][1] < 0:
        p.f = [list(reversed(f)) for f in p.f]
        p.uv = [list(reversed(u)) for u in p.uv]
    # chain weights by height: pelvis at the top, the two spring bones below
    zc = [z_top - 0.005, z_top - 0.10, z_top - 0.22]
    H = cw.hats(-p.v[:, 2], [-z for z in zc])
    p.W = np.zeros((len(p.v), cw.NB))
    for j, b in enumerate(chain):
        p.W[:, cw.BI[b]] = H[:, j]
    return p
