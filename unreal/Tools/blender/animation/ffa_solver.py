"""Fourfold animation toolkit - body-space pose state -> local bone rotations of the frozen rig (numpy, no bpy).

POSE STATE (dict of channel -> tuple of floats; every value interpolates linearly, so states blend):
  pel    (x, y, z, pitch, side, yaw)   pelvis offset from rest (A-frame metres) + body rotation (deg, ffa_math.body_rot)
  spine  (pitch, side, yaw)            total bend of spine_01..05 relative to the pelvis, distributed 15/20/25/25/15 %
  neck   (pitch, side, yaw)            neck_01 / neck_02 / head relative to spine_05 (cumulative 25 / 50 / 100 %)
  gaze   (weight, yaw, pitch)          blend of the head toward a world look direction (A-frame yaw left+, pitch down+)
  clav_l, clav_r (lift, protract)      degrees on top of the automatic shoulder follow (scapulo-humeral rhythm)
  hand_l, hand_r (px,py,pz, fx,fy,fz, mx,my,mz, ex,ey,ez)
                                       CHEST SPACE (A-frame axes carried by spine_05): wrist position relative to the
                                       chest-posed rest shoulder joint, finger direction, palm normal, elbow pole
  foot_l, foot_r (px, py, pz, yaw, pitch, roll, pv, toe, kyaw, kup)
                                       world floor frame (A-frame): pivot point x / y, lift of the pivot (m),
                                       yaw (+ toes turn left), pitch (+ toes up / - heel up) about the pivot, roll,
                                       pv = pivot on the sole (-1 heel, 0 under the ankle, 1 ball joint, 2 toe tip),
                                       toe = extra toe flex (deg, + up), kyaw / kup = knee pole yaw offset / lift
  fing_l, fing_r (25)                  hand shape vectors (ffa_hands)

solve(state, ctx) -> (Pose, info).  ctx carries the twist unwrap between consecutive frames.
"""
import math

import numpy as np

import ffa_hands as hands
import ffa_rig as rig
from ffa_math import (A, DEG, body_rot, frame_from, mat_slerp, mat_to_quat, norm, perp, quat_to_mat, rot,
                      rot_between, rx, ry, rz, swing_twist_y)

SPINE = ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
SPINE_CUM = [0.15, 0.35, 0.60, 0.85, 1.00]
NECK = ["neck_01", "neck_02", "head"]
NECK_CUM = [0.25, 0.50, 1.00]

ARM_MAX_EXT = 0.996
LEG_MAX_EXT = 0.9985
WRIST_MAX_SWING = 76.0          # degrees (validator limit 80)
CLAV_AIM_W = 0.20               # fraction of the hand direction change the clavicle follows ...
CLAV_AIM_MAX = 24.0             # ... clamped to this many degrees
TWIST_LOWER = {"lowerarm_twist_02": 0.30, "lowerarm_twist_01": 0.60}     # share of the wrist roll (mid / near wrist)
TWIST_UPPER = {"upperarm_twist_01": -0.35, "upperarm_twist_02": -0.18}   # counter-roll near the shoulder / mid
TWIST_THIGH = {"thigh_twist_01": -0.40, "thigh_twist_02": -0.20}
TWIST_CALF = {"calf_twist_02": 0.30, "calf_twist_01": 0.60}

CHANNELS = ("pel", "spine", "neck", "gaze", "clav_l", "clav_r", "hand_l", "hand_r", "foot_l", "foot_r",
            "fing_l", "fing_r")

# rest landmarks -------------------------------------------------------------------------------------------------
_SH_REST = {s: rig.HEAD["upperarm_" + s] for s in rig.SIDES}
_CLAV_REST = {s: rig.HEAD["clavicle_" + s] for s in rig.SIDES}
_HAND_REST = {s: rig.HEAD["hand_" + s] for s in rig.SIDES}
_REST_AIM = {s: norm(_HAND_REST[s] - _CLAV_REST[s]) for s in rig.SIDES}


def sole_point(side, pv):
    """Rest-pose point on the sole for a pivot parameter pv (-1 heel, 0 under the ankle, 1 ball joint, 2 toe tip)."""
    heel = rig.side_point(rig.HEEL_L, side)
    ank = rig.side_point(rig.ANKLE_L, side)
    ank_g = np.array([ank[0], ank[1], 0.0])
    ball = rig.side_point(rig.BALL_L, side)
    toe = rig.side_point(rig.TOE_L, side)
    toe = np.array([toe[0], toe[1], 0.008])
    pts = [heel, ank_g, ball, toe]
    pv = min(max(pv, -1.0), 2.0)
    i = min(int(math.floor(pv + 1.0)), 2)
    u = (pv + 1.0) - i
    return pts[i] * (1.0 - u) + pts[i + 1] * u


def foot_delta(yaw, pitch, roll):
    """World delta rotation of the foot (rest -> posed)."""
    return rz(yaw) @ rx(-pitch) @ ry(roll)


def foot_pose(side, f):
    """Foot state -> (D_foot, ankle_world, pivot_world, pivot_rest)."""
    px, py, pz, yaw, pitch, roll, pv = f[:7]
    prest = sole_point(side, pv)
    D = foot_delta(yaw, pitch, roll)
    P = A(px, py, 0.0)
    P[2] = prest[2] + pz
    ankle_rest = rig.HEAD["foot_" + side]
    return D, P + D @ (ankle_rest - prest), P, prest


def foot_point_world(side, f, p_rest):
    D, _, P, prest = foot_pose(side, f)
    return P + D @ (np.asarray(p_rest, float) - prest)


def repivot(side, f, pv_new):
    """Same foot pose expressed with another pivot (exact): returns a new foot tuple."""
    f = list(f)
    if abs(f[6] - pv_new) < 1e-9:
        return tuple(f)
    D, _, P, prest = foot_pose(side, f)
    new_rest = sole_point(side, pv_new)
    Pn = P + D @ (new_rest - prest)
    a = Pn
    f[0], f[1] = a[0], -a[1]
    f[2] = Pn[2] - new_rest[2]
    f[6] = pv_new
    return tuple(f)


def footprint(side, x, y, yaw=0.0, pv=0.0, lift=0.0, pitch=0.0, roll=0.0, toe=0.0, kyaw=0.0, kup=0.0):
    """Foot tuple from a FLAT footprint: (x, y) = the point under the ankle (A-frame), yaw; the pivot is placed at pv
    along that footprint (so changing pv later keeps a flat foot where it is)."""
    ank_g = np.array([rig.side_point(rig.ANKLE_L, side)[0], rig.side_point(rig.ANKLE_L, side)[1], 0.0])
    prest = sole_point(side, pv)
    D = rz(yaw)
    off = D @ (prest - ank_g)
    w = A(x, y, 0.0) + off
    return (w[0], -w[1], lift, yaw, pitch, roll, pv, toe, kyaw, kup)


def rest_foot_x(side):
    return rig.side_point(rig.ANKLE_L, side)[0]


# ------------------------------------------------------------------------------------------------ the solve
def two_bone(a, c, l1, l2, pole, max_ext):
    """Two-bone IK.  Returns (b, c_actual, miss, vhat, dirv)."""
    to = c - a
    d = float(np.linalg.norm(to))
    dmax = (l1 + l2) * max_ext
    dmin = abs(l1 - l2) + 0.03
    dc = min(max(d, dmin), dmax)
    dirv = to / d if d > 1e-9 else np.array([0.0, 0.0, -1.0])
    aa = (l1 * l1 - l2 * l2 + dc * dc) / (2.0 * dc)
    h = math.sqrt(max(l1 * l1 - aa * aa, 0.0))
    v = perp(np.asarray(pole, float), dirv)
    if np.linalg.norm(v) < 1e-6:
        v = perp(np.array([0.0, -1.0, 0.0]), dirv)
    v = norm(v)
    b = a + dirv * aa + v * h
    return b, a + dirv * dc, max(0.0, d - dc), v, dirv


def _twist_unwrap(ctx, key, tw):
    """Keep a roll angle continuous between frames (no 360 jumps) but never beyond +-200 deg."""
    if ctx is None:
        return tw
    last = ctx.get(key)
    if last is not None:
        tw += 360.0 * round((last - tw) / 360.0)
        if tw > 200.0:
            tw -= 360.0
        elif tw < -200.0:
            tw += 360.0
    ctx[key] = tw
    return tw


def _axial_angle(neutral, target, axis):
    """Signed rotation about `axis` (deg) taking the neutral hand's palm normal to the target's, both projected
    perpendicular to the axis (falls back to the X axes when the palm normal runs along the axis)."""
    a = norm(axis)
    best = None
    for col in (2, 0):
        u = perp(neutral[:, col], a)
        v = perp(target[:, col], a)
        nu, nv = float(np.linalg.norm(u)), float(np.linalg.norm(v))
        w = min(nu, nv)
        if best is None or w > best[0]:
            best = (w, u / max(nu, 1e-9), v / max(nv, 1e-9))
        if w > 0.5:
            break
    _, u, v = best
    return math.degrees(math.atan2(float(np.dot(np.cross(u, v), a)), float(np.dot(u, v))))


def solve(st, ctx=None):
    pose = rig.Pose()
    W = {}
    H = {}
    info = {}
    # ---------------------------------------------------------------- pelvis + spine
    px, py, pz, pp, ps, pyw = st["pel"]
    Dp = body_rot(pp, ps, pyw)
    off = A(px, py, pz)
    W["pelvis"] = Dp @ rig.REST["pelvis"]
    H["pelvis"] = rig.HEAD["pelvis"] + off
    pose.pelvis_loc = rig.REST["pelvis"].T @ off
    pose.q["pelvis"] = rig.REST["pelvis"].T @ W["pelvis"]
    sp = st["spine"]
    prevD, prev = Dp, "pelvis"
    D = {"pelvis": Dp}
    for name, c in zip(SPINE, SPINE_CUM):
        Dk = Dp @ body_rot(sp[0] * c, sp[1] * c, sp[2] * c)
        D[name] = Dk
        W[name] = Dk @ rig.REST[name]
        H[name] = H[prev] + prevD @ (rig.HEAD[name] - rig.HEAD[prev])
        pose.q[name] = rig.local_from_world(name, W[prev], W[name])
        prevD, prev = Dk, name
    Dc = D["spine_05"]
    # ---------------------------------------------------------------- neck + head (+ gaze)
    nk = list(st["neck"])
    gw, gyaw, gpitch = st.get("gaze", (0.0, 0.0, 0.0))
    if gw > 1e-4:
        d = A(math.sin(gyaw * DEG) * math.cos(gpitch * DEG), math.cos(gyaw * DEG) * math.cos(gpitch * DEG),
              -math.sin(gpitch * DEG))
        dl = Dc.T @ d                       # in the chest's rest-aligned frame (Blender axes)
        dla = np.array([dl[0], -dl[1], dl[2]])
        yaw_c = math.degrees(math.atan2(dla[0], dla[1]))
        pitch_c = -math.degrees(math.asin(max(-1.0, min(1.0, dla[2]))))
        yaw_c = max(-80.0, min(80.0, yaw_c))
        pitch_c = max(-40.0, min(45.0, pitch_c))
        nk[0] = nk[0] + (pitch_c - nk[0]) * gw
        nk[2] = nk[2] + (yaw_c - nk[2]) * gw
        nk[1] = nk[1] * (1.0 - gw)
    prevD, prev = Dc, "spine_05"
    for name, c in zip(NECK, NECK_CUM):
        Dk = Dc @ body_rot(nk[0] * c, nk[1] * c, nk[2] * c)
        D[name] = Dk
        W[name] = Dk @ rig.REST[name]
        H[name] = H[prev] + prevD @ (rig.HEAD[name] - rig.HEAD[prev])
        pose.q[name] = rig.local_from_world(name, W[prev], W[name])
        prevD, prev = Dk, name
    # ---------------------------------------------------------------- arms
    h_s5 = rig.HEAD["spine_05"]
    for s in rig.SIDES:
        sx = 1.0 if s == "l" else -1.0
        hs = st["hand_" + s]
        S0 = H["spine_05"] + Dc @ (_SH_REST[s] - h_s5)
        T = S0 + Dc @ A(hs[0], hs[1], hs[2])
        fdir = Dc @ A(hs[3], hs[4], hs[5])
        mdir = Dc @ A(hs[6], hs[7], hs[8])
        pole = Dc @ A(hs[9], hs[10], hs[11])
        cn = "clavicle_" + s
        Hc = H["spine_05"] + Dc @ (_CLAV_REST[s] - h_s5)
        # automatic shoulder follow toward the hand (chest-local), then the manual lift / protraction
        dT = norm(Dc.T @ (T - Hc))
        Rt = rot_between(_REST_AIM[s], dT)
        ang = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(_REST_AIM[s], dT))))))
        w = CLAV_AIM_W
        if ang * w > CLAV_AIM_MAX:
            w = CLAV_AIM_MAX / max(ang, 1e-6)
        Raim = mat_slerp(np.eye(3), Rt, w)
        lift, prot = st["clav_" + s]
        Dm = rz(-sx * prot) @ rot(A(0.0, sx, 0.0), lift)
        Dcl = Dc @ Dm @ Raim
        W[cn] = Dcl @ rig.REST[cn]
        H[cn] = Hc
        pose.q[cn] = rig.local_from_world(cn, W["spine_05"], W[cn])
        S = Hc + Dcl @ (_SH_REST[s] - _CLAV_REST[s])
        ua, la, hn = "upperarm_" + s, "lowerarm_" + s, "hand_" + s
        l1 = float(np.linalg.norm(rig.HEAD[la] - rig.HEAD[ua]))
        l2 = float(np.linalg.norm(rig.HEAD[hn] - rig.HEAD[la]))
        B, Cact, miss, vhat, dirv = two_bone(S, T, l1, l2, pole, ARM_MAX_EXT)
        y1 = norm(B - S)
        y2 = norm(Cact - B)
        z1 = perp(y2, y1)
        z1 = norm(z1) if np.linalg.norm(z1) > 1e-6 else -vhat
        x1 = np.cross(y1, z1)
        W[ua] = np.stack([x1, y1, z1], axis=1)
        z2 = np.cross(x1, y2)
        W[la] = np.stack([x1, y2, z2], axis=1)
        H[ua], H[la] = S, B
        # hand orientation: the forearm roll (pronation / supination) is measured about the forearm axis between the
        # palm normals, given to the twist bones, and the remaining wrist swing is limited
        neutral = W[la] @ rig.REST[la].T @ rig.REST[hn]
        target = frame_from(fdir, mdir)
        tw = _axial_angle(neutral, target, y2)
        tw = _twist_unwrap(ctx, "tw_" + s, tw)
        Nt = rot(y2, tw) @ neutral
        swq = mat_to_quat(Nt.T @ target)
        sw_ang = math.degrees(2.0 * math.acos(max(-1.0, min(1.0, abs(swq[0])))))
        sw = quat_to_mat(swq)
        if sw_ang > WRIST_MAX_SWING:
            sw = mat_slerp(np.eye(3), sw, WRIST_MAX_SWING / sw_ang)
        W[hn] = Nt @ sw
        H[hn] = Cact
        pose.q[ua] = rig.local_from_world(ua, W[cn], W[ua])
        pose.q[la] = rig.local_from_world(la, W[ua], W[la])
        pose.q[hn] = rig.local_from_world(hn, W[la], W[hn])
        for tb, k in TWIST_LOWER.items():
            pose.q[tb + "_" + s] = ry(tw * k)
        utw, _ = swing_twist_y(mat_to_quat(pose.q[ua]))
        utw = max(-120.0, min(120.0, utw))
        for tb, k in TWIST_UPPER.items():
            pose.q[tb + "_" + s] = ry(utw * k)
        info["arm_miss_" + s] = miss
        info["wrist_swing_" + s] = min(sw_ang, WRIST_MAX_SWING)
        info["wrist_swing_raw_" + s] = sw_ang
    # ---------------------------------------------------------------- legs
    for s in rig.SIDES:
        f = st["foot_" + s]
        Df, ankle, P, prest = foot_pose(s, f)
        yaw, pitch, roll, pv, toe, kyaw, kup = f[3], f[4], f[5], f[6], f[7], f[8], f[9]
        th, ca, fo, ba = "thigh_" + s, "calf_" + s, "foot_" + s, "ball_" + s
        Hh = H["pelvis"] + Dp @ (rig.HEAD[th] - rig.HEAD["pelvis"])
        kdir = rz(yaw + kyaw) @ A(0.0, 1.0, 0.0) + np.array([0.0, 0.0, kup])
        B, Cact, miss, vhat, dirv = two_bone(Hh, ankle, rig.L_THIGH, rig.L_CALF, kdir, LEG_MAX_EXT)
        y1 = norm(B - Hh)
        y2 = norm(Cact - B)
        dev = perp(y2, y1)
        z1 = -norm(dev) if np.linalg.norm(dev) > 1e-6 else vhat
        x1 = np.cross(y1, z1)
        W[th] = np.stack([x1, y1, z1], axis=1)
        x2 = -x1
        z2 = np.cross(x2, y2)
        W[ca] = np.stack([x2, y2, z2], axis=1)
        W[fo] = Df @ rig.REST[fo]
        if pitch >= 0.0 or f[2] > 0.015 or pv < 1.0:
            tp = pitch          # toes up, airborne foot, or a heel / mid-foot pivot: the toes follow the foot
        else:
            tp = pitch * min(max(pv - 1.0, 0.0), 1.0)   # heel raised on the ball: toes stay flat (toe tip pivot: follow)
        # never push the toe tip through the floor: limit how far the toes point down from the ball joint
        ball_w = P + Df @ (rig.HEAD[ba] - prest)
        lt = rig.LENGTH[ba]
        if tp + toe < 0.0:
            lim = -math.degrees(math.asin(max(0.0, min(1.0, (ball_w[2] - 0.002) / lt))))
            if tp + toe < lim:
                toe = lim - tp
        Db = rz(yaw) @ rx(-(tp + toe)) @ ry(roll)
        W[ba] = Db @ rig.REST[ba]
        H[th], H[ca] = Hh, B
        pose.q[th] = rig.local_from_world(th, W["pelvis"], W[th])
        pose.q[ca] = rig.local_from_world(ca, W[th], W[ca])
        pose.q[fo] = rig.local_from_world(fo, W[ca], W[fo])
        pose.q[ba] = rig.local_from_world(ba, W[fo], W[ba])
        ttw, _ = swing_twist_y(mat_to_quat(pose.q[th]))
        ttw = max(-90.0, min(90.0, ttw))
        for tb, k in TWIST_THIGH.items():
            pose.q[tb + "_" + s] = ry(ttw * k)
        delta = W[fo] @ (W[ca] @ rig.REST[ca].T @ rig.REST[fo]).T      # foot vs calf-following, world
        ctw, _ = swing_twist_y(mat_to_quat(W[ca].T @ delta @ W[ca]))   # its axial part about the shin
        ctw = max(-60.0, min(60.0, ctw))
        for tb, k in TWIST_CALF.items():
            pose.q[tb + "_" + s] = ry(ctw * k)
        info["leg_miss_" + s] = miss
    # ---------------------------------------------------------------- fingers
    for s in rig.SIDES:
        for b, q in hands.finger_locals(s, st["fing_" + s]).items():
            pose.q[b] = q
    return pose, info


def chest_frame(st):
    """(Dc, origin_l, origin_r): the chest delta rotation and the chest-posed rest shoulder joints (the origins of the
    CHEST SPACE hand targets) for the body channels of a state."""
    px, py, pz, pp, ps, pyw = st["pel"]
    Dp = body_rot(pp, ps, pyw)
    Hp = rig.HEAD["pelvis"] + A(px, py, pz)
    sp = st["spine"]
    prevD, prev, Hprev = Dp, "pelvis", Hp
    for name, c in zip(SPINE, SPINE_CUM):
        Dk = Dp @ body_rot(sp[0] * c, sp[1] * c, sp[2] * c)
        Hk = Hprev + prevD @ (rig.HEAD[name] - rig.HEAD[prev])
        prevD, prev, Hprev = Dk, name, Hk
    Dc, Hs5 = prevD, Hprev
    h_s5 = rig.HEAD["spine_05"]
    return Dc, {s: Hs5 + Dc @ (_SH_REST[s] - h_s5) for s in rig.SIDES}


def world_to_chest(st, side, p=None, dirs=()):
    """Convert a world (A-frame, root origin) wrist position and directions into chest space for this state."""
    Dc, org = chest_frame(st)
    out = []
    if p is not None:
        v = Dc.T @ (A(*p) - org[side])
        out.append((v[0], -v[1], v[2]))
    for d in dirs:
        v = Dc.T @ A(*d)
        out.append((v[0], -v[1], v[2]))
    return out


def chest_to_world(st, side, p):
    Dc, org = chest_frame(st)
    w = org[side] + Dc @ A(*p)
    return (w[0], -w[1], w[2])
