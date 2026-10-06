"""The visible body surface: skin (head, neck, chest V, fingers) + forearm / hand wraps; hidden skin removed."""
import numpy as np

import ch_part as P
import ch_regions as R
import ch_weights as cw


def _n(a):
    return a / (np.linalg.norm(a, axis=-1, keepdims=True) + 1e-12)


def wrap_field(v, side):
    """< 0 inside the forearm / hand wrap of one side (cuff under the sleeve -> knuckles, thumb base)."""
    s = side
    la = cw.seg_param(v, cw.bone_head("lowerarm_" + s), cw.bone_tail("lowerarm_" + s))
    f_arm = R.WRAP_ARM_T0 - la
    wr = cw.bone_head("hand_" + s)
    knuckles = np.array([cw.bone_tail(f"{n}_metacarpal_{s}") for n in ("index", "middle", "ring", "pinky")])
    kc = knuckles.mean(axis=0)
    hdir = _n(kc - wr)
    # wrap ends ~9 mm short of the knuckles on the back of the hand
    f_knuckle = (v - kc) @ hdir + 0.009
    t2 = cw.bone_head("thumb_02_" + s)
    t3 = cw.bone_head("thumb_03_" + s)
    tdir = _n(t3 - t2)
    rel = v - t2
    along = rel @ tdir
    radial = np.linalg.norm(rel - np.outer(along, tdir), axis=1)
    near = np.clip((0.028 - radial) / 0.010, 0, 1) * np.clip((along + 0.03) / 0.01, 0, 1)
    f_thumb = (along + 0.004) * near - 0.05 * (1 - near)
    return np.maximum(f_arm, np.maximum(f_knuckle, f_thumb))


def build_body(st, W_all, lm):
    obj = st.obj
    fi = obj.group_faces(lambda g: g == "body")
    used = sorted({i for f in fi for i in obj.faces[f]})
    remap = {o: i for i, o in enumerate(used)}
    uvs = [[tuple(obj.vt[t]) for t in obj.fuv[f]] for f in fi]
    body = P.Part("body", st.v[used], [[remap[i] for i in obj.faces[f]] for f in fi], "skin", W_all[used], uvs)
    # forearm / hand wraps: exact cut along a smooth field, per side
    dom = R.dominant_bones(body.W)
    armish = np.array([d.startswith(("upperarm", "lowerarm", "hand_")) or "metacarpal" in d or d.startswith(("thumb",
                       "index", "middle", "ring", "pinky")) for d in dom])
    f = np.ones(len(body.v))
    for s, sel in (("l", body.v[:, 0] > 0), ("r", body.v[:, 0] <= 0)):
        m = armish & sel
        f[m] = wrap_field(body.v[m], s)
    body, side = P.slice_assign(body, f, mat_neg="wraps", mat_pos="skin")
    v = body.v
    dom = R.dominant_bones(body.W)
    ap = R.arm_params(v)
    la = np.where(v[:, 0] > 0, ap["la_l"], ap["la_r"])
    deep_scalp = (R.hair_field(v, lm) > 0.016) & (v[:, 2] > lm["jaw"][2])
    torso = np.isin(dom, ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "clavicle_l",
                          "clavicle_r"])
    import ch_garments as G
    hidden_torso = torso & (G.neckline_field(v) > 0.05)
    arm = np.array([d.startswith(("upperarm", "lowerarm", "hand_")) or "metacarpal" in d or d.startswith("thumb_01")
                    for d in dom])
    hidden_arm = arm & (la < R.WRAP_ARM_T0 - 0.03)
    legs = np.array([d.startswith(("thigh", "calf", "foot", "ball")) for d in dom])
    hidden = hidden_torso | hidden_arm | legs | deep_scalp
    keep = [not all(hidden[i] for i in fc) for fc in body.f]
    return body.subset(keep, "body")


def wrap_layer(body, offset=0.0026):
    """Detach the wrap faces and raise them into a cloth layer with a lip at the knuckle / cuff edges."""
    sel = [m == "wraps" for m in body.m]
    return P.split_region_with_lip(body, sel, offset, lip=True)
