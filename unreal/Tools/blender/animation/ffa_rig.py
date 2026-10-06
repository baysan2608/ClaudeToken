"""Fourfold animation toolkit - rest data of the frozen rig and forward kinematics (numpy only, no bpy).

The rest frames are rebuilt from ff_rig_spec exactly the way Blender builds them (Y = head->tail, Z = the roll
target projected perpendicular to Y, X = Y x Z); test_toolkit.py checks them and the FK against Blender to 1e-5.

FK (Blender pose semantics, verified):
    W[b] = W[parent] @ R[parent]^T @ R[b] @ Q[b]          (Q = local pose rotation, R = rest frame)
    H[b] = H[parent] + W[parent] @ R[parent]^T @ (h[b] - h[parent])      (+ R[b] @ loc for the pelvis)
Only the pelvis carries a location key (ff_rig_spec.ANIM_BONES note).
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
COMMON = os.path.abspath(os.path.join(HERE, "..", "common"))
if COMMON not in sys.path:
    sys.path.insert(0, COMMON)

import ff_rig_spec as spec  # noqa: E402

from ffa_math import norm  # noqa: E402

BONES = [b["name"] for b in spec.BONES]
PARENT = {b["name"]: b["parent"] for b in spec.BONES}
HEAD = {b["name"]: np.array(b["head"], float) for b in spec.BONES}
TAIL = {b["name"]: np.array(b["tail"], float) for b in spec.BONES}
LENGTH = {n: float(np.linalg.norm(TAIL[n] - HEAD[n])) for n in BONES}
ANIM_BONES = list(spec.ANIM_BONES)          # bones a clip keys (rotation); pelvis also gets location
SIDES = ("l", "r")


def _rest_frame(b):
    h = np.array(b["head"], float)
    t = np.array(b["tail"], float)
    y = norm(t - h)
    z = np.array(b["z"], float)
    z = norm(z - y * float(np.dot(z, y)))
    x = np.cross(y, z)
    return np.stack([x, y, z], axis=1)


REST = {b["name"]: _rest_frame(b) for b in spec.BONES}
# order with parents before children (the spec table is already ordered that way; keep it explicit)
ORDER = []
_seen = set()
while len(ORDER) < len(BONES):
    for n in BONES:
        if n not in _seen and (PARENT[n] is None or PARENT[n] in _seen):
            ORDER.append(n)
            _seen.add(n)
CHILDREN = {n: [c for c in BONES if PARENT[c] == n] for n in BONES}

# ------------------------------------------------------------------------------------------------ landmarks
# Sole landmarks of the LEFT foot at rest (Blender coords); right = mirror x.  The sole spans ~7 cm behind the ankle
# to the toe tip.  'ball' is the ball (MTP) joint centre: the foot pivots about it when the heel rises while the toe
# bone stays flat on the floor.
ANKLE_L = HEAD["foot_l"].copy()             # (0.105, 0.030, 0.085)
BALL_L = HEAD["ball_l"].copy()              # (0.110, -0.100, 0.025)
TOE_L = TAIL["ball_l"].copy()               # (0.110, -0.170, 0.020)
HEEL_L = np.array([ANKLE_L[0] - 0.002, ANKLE_L[1] + 0.062, 0.0])
FOOT_FWD_L = norm(np.array([BALL_L[0] - ANKLE_L[0], BALL_L[1] - ANKLE_L[1], 0.0]))   # horizontal foot axis
ANKLE_H = float(ANKLE_L[2])
BALL_H = float(BALL_L[2])


def mirror_x(v):
    return np.array([-v[0], v[1], v[2]], float)


def side_point(p_left, side):
    return p_left.copy() if side == "l" else mirror_x(p_left)


# limb lengths
L_UPPERARM = LENGTH["upperarm_l"]
L_LOWERARM = LENGTH["lowerarm_l"]
L_THIGH = float(np.linalg.norm(HEAD["calf_l"] - HEAD["thigh_l"]))
L_CALF = float(np.linalg.norm(HEAD["foot_l"] - HEAD["calf_l"]))


class Pose:
    """Local rotations (3x3) per bone + pelvis location (bone-local, like Blender's pose_bone.location)."""

    __slots__ = ("q", "pelvis_loc")

    def __init__(self):
        self.q = {n: np.eye(3) for n in BONES}
        self.pelvis_loc = np.zeros(3)

    def copy(self):
        p = Pose()
        p.q = {k: v.copy() for k, v in self.q.items()}
        p.pelvis_loc = self.pelvis_loc.copy()
        return p


def fk(pose):
    """Returns (W, H, T): world rotation, head and tail per bone (armature space)."""
    W, H, T = {}, {}, {}
    for n in ORDER:
        p = PARENT[n]
        q = pose.q.get(n)
        if q is None:
            q = np.eye(3)
        if p is None:
            W[n] = REST[n] @ q
            H[n] = HEAD[n] + (REST[n] @ pose.pelvis_loc if n == "pelvis" else 0.0)
        else:
            Wp = W[p] @ REST[p].T
            W[n] = Wp @ REST[n] @ q
            H[n] = H[p] + Wp @ (HEAD[n] - HEAD[p])
        T[n] = H[n] + W[n][:, 1] * LENGTH[n]
    return W, H, T


def local_from_world(name, W_parent, W_world):
    """Local pose rotation Q for a bone given its parent's world rotation and its own desired world rotation."""
    p = PARENT[name]
    if p is None:
        return REST[name].T @ W_world
    return REST[name].T @ REST[p] @ W_parent.T @ W_world


def world_point(W, H, bone, p_rest):
    """Where a rest-pose point rigidly attached to `bone` is in the posed armature."""
    return H[bone] + W[bone] @ REST[bone].T @ (np.asarray(p_rest, float) - HEAD[bone])
