"""Where things go on the fitted body: landmarks, hairline, garment boundaries (numpy only, rig space metres)."""
import os

import numpy as np

import ch_mh
import ch_weights as cw

# ---------------------------------------------------------------------------------------------- design constants
NECK_V_Z = 1.285          # crossing point of the wrap-front collar (mid sternum)
NECK_V_SLOPE = 3.1        # dz/dx of the collar lines up to the neck
COLLAR_BACK_Z = 1.500     # neckline height at the back of the neck
SLEEVE_T = 0.22           # sleeve ends just below the elbow (t along lowerarm)
WRAP_ARM_T0 = 0.10        # forearm wraps start under the cuff
WAIST_Z = 1.035           # tunic / hem seam (hidden under the sash)
HEM_Z = 0.56              # hem bottom (knee) - ff_hem_*_02 tails end at 0.55
SLIT_Z = 0.86             # side slits open from the hem up to here
SHIN_WRAP_TOP = 0.39      # shin wraps from here down to the shoe
SHOE_TOP = 0.105
# CC0 MakeHuman ear-translate targets (they move the ear rigidly): only read as a vertex mask (which base-mesh
# vertices belong to the ears)
EAR_TARGETS = ("targets/ears/l-ear-trans-backward.target", "targets/ears/r-ear-trans-backward.target")
EAR_R0, EAR_R1 = 0.003, 0.011   # hair stays this far from the ears (full / fading)
EAR_DROP = 0.04                 # how strongly the ears push the hairline field negative


def landmarks(st):
    obj, V = st.obj, st.v

    def grp(name):
        ix = obj.group_verts(lambda g: g == name)
        return V[ix].mean(axis=0)

    lm = {k: grp("joint-" + k) for k in ("l-eye", "r-eye", "mouth", "jaw", "head", "head-2", "neck",
                                           "l-upperlid", "l-lowerlid", "r-upperlid", "r-lowerlid")}
    body = obj.group_verts(lambda g: g == "body")
    hv = body[V[body, 2] > 1.55]
    lm["head_min"] = V[hv].min(axis=0)
    lm["head_max"] = V[hv].max(axis=0)
    face = body[(V[body, 2] > 1.55) & (np.abs(V[body, 0]) < 0.006)]
    lm["nose_tip"] = V[face[np.argmin(V[face, 1])]]
    # skull centre: centre of the head above the eyes
    top = hv[V[hv, 2] > lm["l-eye"][2]]
    c = 0.5 * (V[top].min(axis=0) + V[top].max(axis=0))
    c[0] = 0.0
    lm["skull_c"] = c
    for s in ("l", "r"):
        eix = obj.group_verts(lambda g, s=s: g == f"helper-{s}-eye")
        ec = V[eix].mean(axis=0)
        lm[f"eye_c_{s}"] = ec
        lm[f"eye_r_{s}"] = float(np.linalg.norm(V[eix] - ec, axis=1).mean())
    # brow height: a bit above the upper lid
    lm["brow_z"] = lm["l-upperlid"][2] + 0.017
    # lips from the mid-line profile: upper / lower lip bulges and the contact line between them
    # (front-most y of the mid-sagittal slice of the mesh at each height; the slice is cut through the faces, so
    # the inner lip surfaces between two vertex rows never masquerade as the front contour)
    nz = lm["nose_tip"][2]
    segs = []
    xc = 0.0007
    for f, g in zip(obj.faces, obj.groups):
        if g != "body":
            continue
        P = V[f]
        if P[:, 2].max() < nz - 0.07 or P[:, 2].min() > nz or P[:, 1].min() > lm["nose_tip"][1] + 0.03:
            continue
        pts = []
        for k in range(len(f)):
            a, b = P[k], P[(k + 1) % len(f)]
            if (a[0] - xc) * (b[0] - xc) < 0:
                t = (xc - a[0]) / (b[0] - a[0])
                pts.append(a + t * (b - a))
        if len(pts) == 2:
            segs.append((pts[0][1:], pts[1][1:]))
    zs = np.arange(nz - 0.060, nz - 0.012, 0.00025)
    front = np.full(len(zs), np.nan)
    for (y0, z0), (y1, z1) in segs:
        lo_, hi_ = min(z0, z1), max(z0, z1)
        ii = np.nonzero((zs >= lo_) & (zs <= hi_))[0]
        if len(ii) == 0 or hi_ - lo_ < 1e-9:
            continue
        yy = y0 + (zs[ii] - z0) / (z1 - z0) * (y1 - y0)
        front[ii] = np.fmin(front[ii], yy)
    ok = ~np.isnan(front)
    front = np.interp(zs, zs[ok], front[ok])

    def most_forward(z0, z1):
        m = (zs >= z0) & (zs <= z1)
        return zs[m][np.argmin(front[m])]
    up = most_forward(nz - 0.036, nz - 0.019)
    lo = most_forward(nz - 0.056, up - 0.008)
    m = (zs > lo) & (zs < up)
    lm["mouth_z"] = float(zs[m][np.argmax(front[m])]) if m.any() else 0.5 * (up + lo)
    lm["upper_lip_z"] = float(up)
    lm["lower_lip_z"] = float(lo)
    # ears: base-mesh vertices the ear-translate targets move fully (the skin around them only partly)
    em = np.zeros(len(V))
    for rel in EAR_TARGETS:
        d = ch_mh.read_target(os.path.join(ch_mh.MH_DIR, rel), len(V))
        em = np.maximum(em, np.linalg.norm(d, axis=1))
    inb = np.zeros(len(V), dtype=bool)
    inb[body] = True
    em[~inb] = 0.0
    lm["ear_pts"] = V[em > 0.85 * em.max()]
    return lm


def head_polar(V, lm):
    """Azimuth (deg: 0 = front, +90 = character left, 180 = back) and height of points around the skull axis."""
    c = lm["skull_c"]
    d = V - c
    az = np.degrees(np.arctan2(d[:, 0], -d[:, 1]))
    return az, V[:, 2]


# hairline: (|azimuth| deg, height above the brow line in m). Short, neat male hairline; sideburns to mid-ear;
# behind the ear it drops to the nape. The ears themselves are carved out by ear_weight() (hair_field).
_HAIRLINE = [(0, 0.063), (15, 0.061), (30, 0.054), (45, 0.050), (58, 0.040), (68, 0.018), (76, -0.010),
             (84, -0.024), (92, -0.016), (102, -0.008), (115, -0.018), (130, -0.040), (150, -0.070),
             (180, -0.086)]


def hairline_z(az, lm):
    a = np.abs(az)
    xs, ys = zip(*_HAIRLINE)
    return lm["brow_z"] + np.interp(a, xs, ys)


_EAR_TREES = {}


def ear_weight(V, lm):
    """1 on / right next to the ears, fading to 0 at EAR_R1 (smooth)."""
    pts = lm.get("ear_pts")
    if pts is None or len(pts) == 0:
        return np.zeros(len(V))
    key = (len(pts), float(pts.sum()))
    if key not in _EAR_TREES:
        from scipy.spatial import cKDTree
        _EAR_TREES[key] = cKDTree(pts)
    d, _ = _EAR_TREES[key].query(np.asarray(V, float))
    t = np.clip((EAR_R1 - d) / (EAR_R1 - EAR_R0), 0, 1)
    return t * t * (3 - 2 * t)


def hair_field(V, lm):
    """> 0 on the scalp under the hair cap (m, ~ height above the hairline), < 0 on the face, neck and ears."""
    V = np.asarray(V, float)
    az, z = head_polar(V, lm)
    return z - hairline_z(az, lm) - EAR_DROP * ear_weight(V, lm)


def hair_mask(V, lm, soft=0.006):
    """0..1 coverage of the scalp by the hair cap (smooth at the hairline)."""
    m = np.clip(hair_field(V, lm) / soft + 0.5, 0, 1)
    # only on the head (not the neck / shoulders): above the jaw line at the back
    m[V[:, 2] < lm["jaw"][2] - 0.03] = 0.0
    return m


def dominant_bones(W):
    return np.array(cw.DEFORM_BONES)[np.argmax(W, axis=1)]


def arm_params(V):
    """t along upper arm / lower arm for both sides (left side uses x>0)."""
    out = {}
    for s in ("l", "r"):
        out["ua_" + s] = cw.seg_param(V, cw.bone_head("upperarm_" + s), cw.bone_tail("upperarm_" + s))
        out["la_" + s] = cw.seg_param(V, cw.bone_head("lowerarm_" + s), cw.bone_tail("lowerarm_" + s))
    return out


def neckline_visible(V, margin=0.0):
    """True where the chest / neck skin shows above the wrap-front neckline (margin > 0 grows the region)."""
    x, y, z = V[:, 0], V[:, 1], V[:, 2]
    front = z > NECK_V_Z + NECK_V_SLOPE * np.abs(x) - margin
    back = z > COLLAR_BACK_Z - margin
    # front half uses the V, back half the straight neckline; blend around the side of the neck
    fwd = np.clip((0.02 - y) / 0.04, 0, 1)
    return np.where(fwd > 0.5, front, back)
