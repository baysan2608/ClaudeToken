"""Rest-pose skeleton definition for the Fourfold training fighter.

Authoring frame "A" (used for every hand written coordinate in this tool chain):
    x = character LEFT   (Blender +X)
    y = character FORWARD (Blender -Y)   <- the character faces Blender -Y, glTF/Godot +Z
    z = up
`A(x, y, z)` converts to Blender coordinates.  Rotation semantics (degrees):
    pitch  + = lean/rotate forward      (Blender rotation about +X)
    side   + = lean/rotate to the left  (Blender rotation about +Y)
    yaw    + = turn to the left         (Blender rotation about +Z)
"""
from mathutils import Vector

SIDES = ("L", "R")


def A(x, y, z):
    """Authoring frame -> Blender coordinates."""
    return Vector((x, -y, z))


def mirror_x(p):
    return (-p[0], p[1], p[2])


# --- key landmarks (metres, authoring frame, rest pose) ------------------------------
HEIGHT = 1.75
HIP_Z = 0.90
SHOULDER_Z = 1.39
SHOULDER_X = 0.175

# left-side joint positions (A frame); right side is mirrored in x
_L = {
    "shoulder_head": (0.035, 0.015, 1.415),
    "shoulder_tail": (SHOULDER_X, 0.0, SHOULDER_Z),   # = upper_arm head
    "elbow": (0.278, 0.010, 1.108),
    "wrist": (0.335, 0.088, 0.866),
    "fingertip": (0.366, 0.122, 0.703),
    "hip": (0.090, 0.0, HIP_Z),
    "knee": (0.090, 0.045, 0.495),
    "ankle": (0.090, -0.005, 0.085),
    "ball": (0.090, 0.130, 0.040),
    "toe_tip": (0.090, 0.215, 0.030),
}

# centre-line
SPINE = {
    "hips": ((0, 0, HIP_Z), (0, 0, 1.02)),
    "spine": ((0, 0, 1.02), (0, 0, 1.21)),
    "chest": ((0, 0, 1.21), (0, 0, 1.43)),
    "neck": ((0, 0, 1.43), (0, 0, 1.575)),
    "head": ((0, 0, 1.575), (0, 0, 1.75)),
}

# deform bones in export order (parents before children). Exactly the spec's list.
BONE_ORDER = [
    "root", "hips", "spine", "chest", "neck", "head",
    "shoulder.L", "upper_arm.L", "forearm.L", "hand.L",
    "shoulder.R", "upper_arm.R", "forearm.R", "hand.R",
    "thigh.L", "shin.L", "foot.L", "toe.L",
    "thigh.R", "shin.R", "foot.R", "toe.R",
]

# which world direction the Z axis of the bone should face (Blender coordinates)
_FWD = Vector((0, -1, 0))
_UP = Vector((0, 0, 1))


def _lr(side, key):
    p = _L[key]
    return p if side == "L" else mirror_x(p)


def bone_table():
    """Returns {name: dict(parent, head, tail, roll_vec, connected)} in BLENDER coordinates."""
    t = {}
    t["root"] = dict(parent=None, head=A(0, 0, 0), tail=A(0, 0.15, 0), roll=_UP, connect=False)
    for name, (h, tl) in SPINE.items():
        t[name] = dict(parent=None, head=A(*h), tail=A(*tl), roll=_FWD, connect=True)
    t["hips"]["parent"] = "root"
    t["hips"]["connect"] = False
    t["spine"]["parent"] = "hips"
    t["chest"]["parent"] = "spine"
    t["neck"]["parent"] = "chest"
    t["head"]["parent"] = "neck"
    for s in SIDES:
        t[f"shoulder.{s}"] = dict(parent="chest", head=A(*_lr(s, "shoulder_head")),
                                  tail=A(*_lr(s, "shoulder_tail")), roll=_FWD, connect=False)
        t[f"upper_arm.{s}"] = dict(parent=f"shoulder.{s}", head=A(*_lr(s, "shoulder_tail")),
                                   tail=A(*_lr(s, "elbow")), roll=_FWD, connect=True)
        t[f"forearm.{s}"] = dict(parent=f"upper_arm.{s}", head=A(*_lr(s, "elbow")),
                                 tail=A(*_lr(s, "wrist")), roll=_FWD, connect=True)
        t[f"hand.{s}"] = dict(parent=f"forearm.{s}", head=A(*_lr(s, "wrist")),
                              tail=A(*_lr(s, "fingertip")), roll=_FWD, connect=True)
        t[f"thigh.{s}"] = dict(parent="hips", head=A(*_lr(s, "hip")),
                               tail=A(*_lr(s, "knee")), roll=_FWD, connect=False)
        t[f"shin.{s}"] = dict(parent=f"thigh.{s}", head=A(*_lr(s, "knee")),
                              tail=A(*_lr(s, "ankle")), roll=_FWD, connect=True)
        t[f"foot.{s}"] = dict(parent=f"shin.{s}", head=A(*_lr(s, "ankle")),
                              tail=A(*_lr(s, "ball")), roll=_UP, connect=True)
        t[f"toe.{s}"] = dict(parent=f"foot.{s}", head=A(*_lr(s, "ball")),
                             tail=A(*_lr(s, "toe_tip")), roll=_UP, connect=True)
    return t


def seg_len(a, b):
    return (Vector(a) - Vector(b)).length


LEFT_REST = _L
