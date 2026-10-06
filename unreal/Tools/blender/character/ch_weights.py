"""Skin weights on the frozen rig (numpy only).

Weights are dense matrices (n_verts, n_bones) over DEFORM_BONES (every deforming bone of ff_rig_spec, ff_* included).
Body / garment weights start from the CC0 MakeHuman weights (hand-tuned for the base mesh), mapped onto our bones:
  * torso: MakeHuman spine bones are re-split over pelvis / spine_01..05 by height (hat functions between bone centres)
  * neck re-split over neck_01 / neck_02 by height; head, jaw and every facial bone -> head
  * arms / legs: each long bone's weight is distributed over the bone and its twist bones along the bone axis
    (UE5-Manny layout: upperarm_twist_01 near the shoulder, lowerarm_twist_01 near the wrist ...)
  * hands / feet: 1:1 (metacarpals, phalanges, thumb; toes -> ball)
Then: per-garment edits (spring chains), Laplacian smoothing on the mesh graph, <= 4 influences, normalisation and
left/right symmetry.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "common"))
import ff_rig_spec as spec  # noqa: E402

DEFORM_BONES = [b["name"] for b in spec.BONES if b["deform"]]
BI = {n: i for i, n in enumerate(DEFORM_BONES)}
NB = len(DEFORM_BONES)
_B = {b["name"]: b for b in spec.BONES}


def bone_head(n):
    return np.array(_B[n]["head"])


def bone_tail(n):
    return np.array(_B[n]["tail"])


def mirror_name(n):
    if n.endswith("_l"):
        return n[:-2] + "_r"
    if n.endswith("_r"):
        return n[:-2] + "_l"
    if "_l_" in n:
        return n.replace("_l_", "_r_")
    if "_r_" in n:
        return n.replace("_r_", "_l_")
    for a, b in (("_fl_", "_fr_"), ("_fr_", "_fl_"), ("_bl_", "_br_"), ("_br_", "_bl_")):
        if a in n:
            return n.replace(a, b)
    return n


MIRROR_PERM = np.array([BI[mirror_name(n)] for n in DEFORM_BONES])


def seg_param(p, a, b):
    """Projection parameter of points p on segment a->b (unclamped)."""
    d = b - a
    return ((p - a) @ d) / (d @ d)


def hats(t, centers):
    """Piecewise-linear partition of unity over sorted centres (clamped at both ends). Returns (n, k)."""
    t = np.asarray(t, float)
    c = np.asarray(centers, float)
    k = len(c)
    out = np.zeros((len(t), k))
    tc = np.clip(t, c[0], c[-1])
    idx = np.clip(np.searchsorted(c, tc, side="right") - 1, 0, k - 2)
    f = (tc - c[idx]) / (c[idx + 1] - c[idx])
    out[np.arange(len(t)), idx] = 1 - f
    out[np.arange(len(t)), idx + 1] += f
    return out


SPINE = ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
NECK = ["neck_01", "neck_02"]

# Twist layout along each long bone: (bone order, centres in t along the parent bone)
TWIST = {
    "upperarm": (["upperarm_twist_01_{s}", "upperarm_twist_02_{s}", "upperarm_{s}"], [0.12, 0.50, 0.88]),
    "lowerarm": (["lowerarm_{s}", "lowerarm_twist_02_{s}", "lowerarm_twist_01_{s}"], [0.12, 0.48, 0.86]),
    "thigh": (["thigh_twist_01_{s}", "thigh_twist_02_{s}", "thigh_{s}"], [0.12, 0.50, 0.88]),
    "calf": (["calf_{s}", "calf_twist_02_{s}", "calf_twist_01_{s}"], [0.12, 0.48, 0.86]),
}


def _mh_class(mh_bone):
    b, side = mh_bone, None
    if b.endswith(".L"):
        side, b = "l", b[:-2]
    elif b.endswith(".R"):
        side, b = "r", b[:-2]
    if b in ("root",) or b == "pelvis":
        return ("pelvis", None)
    if b.startswith("spine") or b == "breast":
        return ("spine", None)
    if b.startswith("neck"):
        return ("neck", None)
    if side is None:
        return ("head", None)
    fingers = {"2": "index", "3": "middle", "4": "ring", "5": "pinky"}
    if b in ("clavicle", "shoulder01"):
        return ("bone", "clavicle_" + side)
    if b.startswith("upperarm"):
        return ("twist", ("upperarm", side))
    if b.startswith("lowerarm"):
        return ("twist", ("lowerarm", side))
    if b == "wrist":
        return ("bone", "hand_" + side)
    if b.startswith("metacarpal"):
        return ("bone", f"{('index', 'middle', 'ring', 'pinky')[int(b[-1]) - 1]}_metacarpal_{side}")
    if b.startswith("finger"):
        f, j = b[6:].split("-")
        return ("bone", f"thumb_0{j}_{side}" if f == "1" else f"{fingers[f]}_0{j}_{side}")
    if b.startswith("upperleg"):
        return ("twist", ("thigh", side))
    if b.startswith("lowerleg"):
        return ("twist", ("calf", side))
    if b == "foot":
        return ("bone", "foot_" + side)
    if b.startswith("toe"):
        return ("bone", "ball_" + side)
    if b == "pelvis":
        return ("pelvis", None)
    if b in ("eye", "special05", "special06", "levator05", "levator06", "oculi01", "orbicularis03",
             "orbicularis04", "oris03", "oris07", "risorius03", "temporalis01", "temporalis02", "tongue05",
             "tongue06", "tongue07"):
        return ("head", None)
    return ("head", None)


def from_makehuman(verts, mh_weights, nverts=None):
    """verts: rig-space positions (n,3) of the OBJ vertices; returns dense (n, NB) weights."""
    n = len(verts) if nverts is None else nverts
    W = np.zeros((n, NB))
    spine_w = np.zeros(n)
    neck_w = np.zeros(n)
    twist_w = {}
    for mb, (ix, w) in mh_weights.items():
        if mb.endswith(".L") and mb[:-2] == "pelvis" or mb.endswith(".R") and mb[:-2] == "pelvis":
            W[ix, BI["pelvis"]] += w
            continue
        kind, arg = _mh_class(mb)
        if kind == "pelvis":
            W[ix, BI["pelvis"]] += w
        elif kind == "spine":
            spine_w[ix] += w
        elif kind == "neck":
            neck_w[ix] += w
        elif kind == "head":
            W[ix, BI["head"]] += w
        elif kind == "bone":
            W[ix, BI[arg]] += w
        else:
            twist_w.setdefault(arg, np.zeros(n))[ix] += w
    z = verts[:, 2]
    # spine: centres of pelvis / spine_01..05 (heights)
    cen = [0.5 * (bone_head(b)[2] + bone_tail(b)[2]) for b in SPINE]
    cen[0] = bone_head("spine_01")[2] - 0.02           # keep the lower belly on spine_01 / pelvis blend
    H = hats(z, cen)
    m = spine_w > 0
    for j, b in enumerate(SPINE):
        W[m, BI[b]] += spine_w[m] * H[m, j]
    cen = [0.5 * (bone_head(b)[2] + bone_tail(b)[2]) for b in NECK]
    H = hats(z, cen)
    m = neck_w > 0
    for j, b in enumerate(NECK):
        W[m, BI[b]] += neck_w[m] * H[m, j]
    for (kind, side), tw in twist_w.items():
        names, cen = TWIST[kind]
        names = [x.format(s=side) for x in names]
        base = names[2] if kind in ("upperarm", "thigh") else names[0]
        t = seg_param(verts, bone_head(base), bone_tail(base))
        H = hats(t, cen)
        m = tw > 0
        for j, b in enumerate(names):
            W[m, BI[b]] += tw[m] * H[m, j]
    return W


def normalize(W, fallback=None):
    s = W.sum(axis=1, keepdims=True)
    bad = s[:, 0] <= 1e-12
    if bad.any():
        if fallback is None:
            raise ValueError(f"{bad.sum()} unweighted vertices")
        W[bad] = fallback[bad]
        s = W.sum(axis=1, keepdims=True)
    return W / s


def limit(W, k=4, min_w=0.01):
    """Keep the k largest influences (and drop tiny ones), renormalise."""
    W = W.copy()
    W[W < min_w] = 0.0
    if W.shape[1] > k:
        idx = np.argsort(-W, axis=1)[:, k:]
        np.put_along_axis(W, idx, 0.0, axis=1)
    return normalize(W)


def adjacency(nverts, faces):
    """Sparse row-normalised adjacency (vertex neighbours over face edges) as a scipy CSR matrix."""
    import scipy.sparse as sp
    rows, cols = [], []
    for f in faces:
        k = len(f)
        for i in range(k):
            a, b = f[i], f[(i + 1) % k]
            rows += [a, b]
            cols += [b, a]
    A = sp.csr_matrix((np.ones(len(rows)), (rows, cols)), shape=(nverts, nverts))
    A.data[:] = 1.0
    d = np.asarray(A.sum(axis=1)).ravel()
    d[d == 0] = 1
    return sp.diags(1.0 / d) @ A


def smooth(W, A, iters=4, alpha=0.5, mask=None):
    """Laplacian smoothing of weight rows (mask: only these vertices change)."""
    W = W.copy()
    for _ in range(iters):
        Wn = A @ W
        if mask is None:
            W = (1 - alpha) * W + alpha * Wn
        else:
            W[mask] = (1 - alpha) * W[mask] + alpha * Wn[mask]
    return W


def mirror_index(verts, tol=2e-3):
    """Index of each vertex's mirror (x -> -x). -1 when none within tol."""
    from scipy.spatial import cKDTree
    tree = cKDTree(verts)
    m = verts * np.array([-1.0, 1.0, 1.0])
    d, ix = tree.query(m)
    ix[d > tol] = -1
    return ix


def symmetrize(W, mirror_ix):
    """Average each vertex with its mirror's (bone names swapped). Vertices without a mirror are untouched."""
    W2 = W.copy()
    ok = mirror_ix >= 0
    Wm = W[mirror_ix[ok]][:, MIRROR_PERM]
    W2[ok] = 0.5 * (W[ok] + Wm)
    return W2


def chain_weights(t, names):
    """Blend along a chain: t in [0, len(names)-1] -> (n, len(names)) partition of unity."""
    return hats(t, list(range(len(names))))


def to_vertex_groups(obj, W, eps=1e-4):
    import bpy  # noqa: F401
    for b in DEFORM_BONES:
        if b not in obj.vertex_groups:
            obj.vertex_groups.new(name=b)
    for j, b in enumerate(DEFORM_BONES):
        col = W[:, j]
        nz = np.nonzero(col > eps)[0]
        if len(nz) == 0:
            continue
        vg = obj.vertex_groups[b]
        # group by value for speed (add() takes one weight per call)
        vals = np.round(col[nz], 4)
        for val in np.unique(vals):
            ids = nz[vals == val].tolist()
            vg.add(ids, float(val), "REPLACE")


def from_vertex_groups(obj):
    n = len(obj.data.vertices)
    W = np.zeros((n, NB))
    gi = {g.index: g.name for g in obj.vertex_groups}
    for v in obj.data.vertices:
        for g in v.groups:
            nm = gi.get(g.group)
            if nm in BI:
                W[v.index, BI[nm]] = g.weight
    return W


ARM_FAMILY = {s: [f"upperarm_{s}", f"upperarm_twist_01_{s}", f"upperarm_twist_02_{s}"] for s in ("l", "r")}
TORSO_UPPER = ["spine_03", "spine_04", "spine_05", "neck_01"]


def _smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def rework_shoulders(verts, W, share=0.6, sigma=0.055):
    """Give the clavicle a real region (trapezius top, acromion, upper chest along the collar bone) by moving a
    share of the TORSO weights there (arm weights untouched, so the arm / torso blend keeps its smoothness).
    Clavicle elevation in the clips (scapular upward rotation when the arms go overhead) then lifts the shoulder."""
    W = W.copy()
    torso_ids = [BI[n] for n in ("spine_03", "spine_04", "spine_05", "neck_01")]
    for s in ("l", "r"):
        C0, C1 = bone_head("clavicle_" + s), bone_tail("clavicle_" + s)
        S = C1
        sgn = 1.0 if s == "l" else -1.0
        t = np.clip(seg_param(verts, C0, C1), 0.0, 1.1)
        d = np.linalg.norm(verts - (C0 + t[:, None] * (C1 - C0)), axis=1)
        side = _smoothstep(0.0, 0.035, verts[:, 0] * sgn)
        above = _smoothstep(S[2] - 0.10, S[2] - 0.02, verts[:, 2])
        g = np.exp(-(d / sigma) ** 2) * side * above * share
        torso = W[:, torso_ids].sum(axis=1)
        move = torso * g
        m = move > 1e-4
        if not m.any():
            continue
        scale = np.where(torso > 1e-9, 1 - move / np.maximum(torso, 1e-9), 1.0)
        for j in torso_ids:
            W[m, j] *= scale[m]
        W[m, BI["clavicle_" + s]] += move[m]
    return normalize(W)
