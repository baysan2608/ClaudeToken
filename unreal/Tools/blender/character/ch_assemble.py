"""Builds every piece of the fighter in rig space with final skin weights (numpy + mathutils; no scene needed)."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "common"))
import ch_body
import ch_bodypart as BP
import ch_garments as G
import ch_hair as H
import ch_part as P
import ch_regions as R
import ch_weights as cw

SLOTS = ["skin", "hair", "eyes", "cloth_main", "cloth_accent", "wraps", "sash", "shoes"]


def _smooth_part_weights(part, iters=6, alpha=0.5, mask=None):
    A = part.adjacency()
    part.W = cw.normalize(cw.smooth(part.W, A, iters, alpha, mask))
    return part


def hem_weights(hem):
    """Front / back panels: pelvis at the top, then a mix of the leg below (front panels ride on the thighs when
    the knees come up) and the ff_hem_* chain of the quadrant (centre lines blended) for the runtime springs."""
    v = hem.v
    az = G.pelvis_azimuth(v)
    s = np.clip((R.SLIT_Z + 0.07 - v[:, 2]) / (R.SLIT_Z + 0.07 - R.HEM_Z), 0, 1)
    ss = s * s * (3 - 2 * s)
    front = np.abs(az) < 90
    left = np.clip(0.5 + az / 40.0, 0, 1)                              # front centre blend
    left_back = np.clip(0.5 + (180 - np.abs(az)) * np.sign(az) / 40.0, 0, 1)
    lw = np.where(front, left, left_back)
    Hs = cw.hats(s, [0.45, 1.0])
    Wc = np.zeros_like(hem.W)
    Wleg = np.zeros_like(hem.W)
    for quad_l, quad_r, sel in (("fl", "fr", front), ("bl", "br", ~front)):
        for j, k in enumerate(("01", "02")):
            Wc[sel, cw.BI[f"ff_hem_{quad_l}_{k}"]] += (lw * Hs[:, j])[sel]
            Wc[sel, cw.BI[f"ff_hem_{quad_r}_{k}"]] += ((1 - lw) * Hs[:, j])[sel]
    # leg follow: thigh + its twist bones, by side (blended across the centre line)
    for side, wside in (("l", lw), ("r", 1 - lw)):
        Wleg[:, cw.BI[f"thigh_{side}"]] += wside * 0.5
        Wleg[:, cw.BI[f"thigh_twist_02_{side}"]] += wside * 0.3
        Wleg[:, cw.BI[f"thigh_twist_01_{side}"]] += wside * 0.2
    ramp = np.clip(s / 0.55, 0, 1)
    ramp = ramp * ramp * (3 - 2 * ramp)
    leg_share = np.where(front, 0.42, 0.18) * ramp
    chain_share = np.where(front, 0.50, 0.68) * ramp
    keep = 1 - leg_share - chain_share
    hem.W = cw.normalize(keep[:, None] * hem.W + leg_share[:, None] * Wleg + chain_share[:, None] * Wc)
    # leg weights on the original skirt (MakeHuman) are already partly thighs: clean pelvis/spine on the top band
    return hem


def hair_weights(cap, bun, tie, tail):
    cap.W = np.zeros_like(cap.W)
    cap.W[:, cw.BI["head"]] = 1.0
    for p in (bun, tie):
        p.W = np.zeros((len(p.v), cw.NB))
        p.W[:, cw.BI["head"]] = 0.75
        p.W[:, cw.BI["ff_hair_01"]] = 0.25
    z01 = cw.bone_head("ff_hair_01")[2]
    zc = [z01, cw.bone_head("ff_hair_02")[2], cw.bone_head("ff_hair_03")[2] - 0.02]
    tail.W = H.chain_weight_tail(tail, ["ff_hair_01", "ff_hair_02", "ff_hair_03"], zc)
    top = tail.v[:, 2] > z01 - 0.005
    tail.W[top] = 0.0
    tail.W[top, cw.BI["head"]] = 0.6
    tail.W[top, cw.BI["ff_hair_01"]] = 0.4


def finalize(part, symmetric):
    part.W = cw.limit(cw.normalize(part.W), 4)
    if symmetric:
        mi = cw.mirror_index(part.v, tol=1.5e-3)
        part.W = cw.limit(cw.symmetrize(part.W, mi), 4)
    return part


def build_parts(verbose=False):
    st = ch_body.BodyState()
    lm = R.landmarks(st)
    W_all = cw.rework_shoulders(st.v, cw.normalize(cw.from_makehuman(st.v, st.mh_weights)))
    fullbody = P.Part("fullbody", st.v, [f for f, g in zip(st.obj.faces, st.obj.groups) if g == "body"], "skin",
                      W_all)
    body = BP.wrap_layer(BP.build_body(st, W_all, lm))
    tunic = G.build_tunic(st, W_all, fullbody)
    bands = G.build_collar_and_cuffs(tunic)
    trousers = G.build_trousers(st, W_all, fullbody)
    hem = G.build_hem(st, W_all, trousers)
    shoes = G.build_shoes(st, W_all, fullbody)
    sash = G.build_sash([tunic, hem, trousers], P.merge([tunic, hem], "under"))
    cap = H.build_hair_cap(st, W_all, lm)
    cap = H.keep_above(cap, fullbody, lm)
    bun, tie, tail, seat, _axis = H.build_knot_and_tail(cap, lm)
    cap = H.cut_under_bun(cap, lm, seat)
    eyes = H.build_eyes(lm)
    lashes = H.build_lashes(st, lm)
    # cloth weights: smoother than skin (cloth does not follow every muscle)
    _smooth_part_weights(tunic, 6)
    _smooth_part_weights(trousers, 4)
    _smooth_part_weights(shoes, 2)
    _smooth_part_weights(hem, 4)
    hem_weights(hem)
    hair_weights(cap, bun, tie, tail)
    parts = [body, tunic, trousers, hem, shoes] + bands + sash + [cap, bun, tie, tail] + eyes + lashes
    symmetric = {"body", "tunic", "trousers", "hem", "shoes", "hair_cap"}
    for p in parts:
        finalize(p, p.name in symmetric)
    if verbose:
        print({p.name: p.tri_count() for p in parts}, "total", sum(p.tri_count() for p in parts))
    return parts, st, lm


# ------------------------------------------------------------------------------------------------ UVs
TEX_SIZE = {"skin": 2048, "cloth_main": 2048, "cloth_accent": 1024, "hair": 1024, "eyes": 1024, "wraps": 1024,
            "sash": 1024, "shoes": 1024}


def assign_uvs(parts, lm):
    """Per-loop UVs in metres for every part (analytic where the texture pattern has a direction, Blender unwrap
    with rule-based seams for the garments)."""
    import ch_uv as U
    from ff_rig_spec import PALM_N_L
    by = {p.name: p for p in parts}
    # forearm / hand wraps: cylinder around elbow -> middle fingertip, seam on the palm / inner side
    body = by["body"]
    wm = np.array([m == "wraps" for m in body.m])
    fc = body.face_centers()
    for s, sg in (("l", 1.0), ("r", -1.0)):
        sel = wm & (fc[:, 0] * sg > 0)
        zero = -np.array(PALM_N_L) * np.array([sg, 1, 1])
        U.cylindrical_uv(body, sel, cw.bone_head("lowerarm_" + s), cw.bone_tail("middle_03_" + s), zero)
    # garments unwrapped in Blender: front / back panels
    for name in ("tunic", "hem"):
        p = by[name]
        U.blender_unwrap(p, U.front_back_classes(p, 0.0 if name == "tunic" else p.v[:, 1].mean()))
    tr = by["trousers"]
    trw = np.array([m == "wraps" for m in tr.m])
    cls = U.front_back_classes(tr, 0.0) + 2 * trw.astype(int)
    U.blender_unwrap(tr, cls)
    fc = tr.face_centers()
    for s, sg in (("l", 1.0), ("r", -1.0)):
        sel = trw & (fc[:, 0] * sg > 0)
        U.cylindrical_uv(tr, sel, cw.bone_head("calf_" + s), cw.bone_tail("calf_" + s), np.array([sg, 0, 0]))
    sh = by["shoes"]
    nrm = np.array([np.cross(sh.v[f[1]] - sh.v[f[0]], sh.v[f[2]] - sh.v[f[0]]) for f in sh.f])
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
    fc = sh.face_centers()
    ankle_x = np.where(fc[:, 0] > 0, cw.bone_head("foot_l")[0], cw.bone_head("foot_r")[0])
    cls = np.where(nrm[:, 2] < -0.6, 0, np.where(fc[:, 0] > ankle_x, 1, 2))
    U.blender_unwrap(sh, cls)
    # hair cap: polar map around the top knot (strands converge on it)
    cap = by["hair_cap"]
    c = lm["skull_c"]
    knot = cw.bone_head("ff_hair_01")
    U.polar_uv(cap, [True] * len(cap.f), c, knot - c, np.array([0, -1.0, 0.3]), 0.09)
    # eyes: left / right halves of the eye texture
    for s, ox in (("l", 0.0), ("r", 0.5)):
        e = by["eye_" + s]
        e.uv = [[(ox + u * 0.5, 0.25 + v * 0.5) for (u, v) in f] for f in e.uv]
    return parts


DENSITY = {"skin_head": 1.35, "skin_neck": 0.8, "hair_lash": 0.6}
ROTATABLE = {"skin", "shoes"}


def pack_uvs(parts):
    """Island scale to metres (x density), grain alignment for cloth, then shelf-pack per material."""
    import ch_uv as U
    report = {}
    oriented = {"tunic", "hem", "trousers", "shoes"}
    entries = {m: [] for m in SLOTS}
    for p in parts:
        if p.name.startswith("eye_"):
            continue
        for mat in sorted(set(p.m)):
            sel = [m == mat for m in p.m]
            for isl in U.uv_islands(p, sel):
                dens = 1.0
                if p.name == "body" and mat == "skin":
                    area = sum(0.5 * np.linalg.norm(np.cross(p.v[p.f[i][1]] - p.v[p.f[i][0]], p.v[p.f[i][2]] - p.v[p.f[i][0]]))
                               for i in isl)
                    zc = np.mean([p.v[p.f[i]].mean(axis=0)[2] for i in isl])
                    if area > 0.02 and zc > 1.5:
                        dens = DENSITY["skin_head"]
                    elif zc < 1.5 and zc > 1.2:
                        dens = DENSITY["skin_neck"]
                if p.name.startswith("lash_"):
                    dens = DENSITY["hair_lash"]
                orient = p.name in oriented and mat != "wraps"
                U.island_scale_and_orient(p, isl, orient=orient, density=dens)
                entries[mat].append((p, isl))
    for p in parts:
        # pattern coordinates (metres, grain-aligned) survive the packing as a second UV set
        p.puv = [list(u) for u in p.uv] if not p.name.startswith("eye_") else [[(0.0, 0.0)] * len(f) for f in p.f]
    for mat, ents in entries.items():
        if mat == "eyes" or not ents:
            continue
        s = U.pack(ents, margin_px=8, size=TEX_SIZE[mat], allow_rot=mat in ROTATABLE)
        report[mat] = {"islands": len(ents), "px_per_m": round(s * TEX_SIZE[mat], 1)}
    return report
