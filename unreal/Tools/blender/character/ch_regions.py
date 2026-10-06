"""Where things go on the fitted body: landmarks, hairline, garment boundaries (numpy only, rig space metres)."""
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
    return lm


def head_polar(V, lm):
    """Azimuth (deg: 0 = front, +90 = character left, 180 = back) and height of points around the skull axis."""
    c = lm["skull_c"]
    d = V - c
    az = np.degrees(np.arctan2(d[:, 0], -d[:, 1]))
    return az, V[:, 2]


# hairline: (|azimuth| deg, height above the brow line in m). Short, neat male hairline; sideburns to mid-ear;
# behind the ear it drops to the nape.
_HAIRLINE = [(0, 0.062), (18, 0.060), (32, 0.052), (48, 0.052), (62, 0.035), (72, 0.000), (80, -0.030),
             (86, -0.030), (92, 0.020), (104, 0.028), (118, 0.004), (135, -0.050), (155, -0.085),
             (180, -0.095)]


def hairline_z(az, lm):
    a = np.abs(az)
    xs, ys = zip(*_HAIRLINE)
    return lm["brow_z"] + np.interp(a, xs, ys)


def hair_mask(V, lm, soft=0.006):
    """0..1 coverage of the scalp by the hair cap (smooth at the hairline)."""
    az, z = head_polar(V, lm)
    h = hairline_z(az, lm)
    m = np.clip((z - h) / soft + 0.5, 0, 1)
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
