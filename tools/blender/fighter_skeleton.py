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

# --- added bones (v2): articulated hands, sash tails, topknot ------------------------------------------------------
# Hand bones are children of hand.X; they continue inside the (unchanged) hand bone, which still ends at the fingertip.
# Naming: thumb_1/2 = thumb (base, tip phalanx); finger_im_* = index+middle fingers (grouped); finger_rp_* = ring+pinky.
FINGER_BONES = [f"{n}.{s}" for s in SIDES for n in ("thumb_1", "thumb_2", "finger_im_1", "finger_im_2", "finger_rp_1", "finger_rp_2")]
SASH_BONES = [f"sash_tail.{s}.{k:03d}" for s in SIDES for k in (1, 2, 3)]
HAIR_BONES = ["hair_top.001", "hair_top.002"]
SECONDARY_BONES = SASH_BONES + HAIR_BONES          # simulated by the runtime (SpringBoneSimulator3D); never animated in clips
ALL_BONES = BONE_ORDER + FINGER_BONES + SASH_BONES + HAIR_BONES
ANIM_BONES = BONE_ORDER + FINGER_BONES              # bones that clips animate (rotation tracks)

# sash tail chains (A frame, left side; right is a mirror image except the length): knot on the lower back
SASH_L = [(0.030, -0.135, 1.040), (0.040, -0.140, 0.925), (0.047, -0.143, 0.810), (0.051, -0.143, 0.700)]
SASH_R = [(-0.030, -0.135, 1.040), (-0.036, -0.139, 0.950), (-0.041, -0.141, 0.865), (-0.044, -0.141, 0.785)]
HAIR_CHAIN = [(0.0, -0.032, 1.736), (0.0, -0.040, 1.778), (0.0, -0.072, 1.797)]


def hand_frame():
    """Left hand frame in the A frame: wrist W, unit direction hd (wrist -> fingertip), u (thumb side, forward),
    n (palm normal, medial), and hand length L."""
    W = Vector(_L["wrist"])
    T = Vector(_L["fingertip"])
    L = (T - W).length
    hd = (T - W).normalized()
    u = Vector((0, 1, 0))
    u = (u - hd * u.dot(hd)).normalized()
    n = -(hd.cross(u)).normalized()           # left hand: palm faces +x*-1 (medial)
    return W, hd, u, n, L


def hand_pts(side):
    """Finger bone joints (A frame) -> dict name -> (head, tail)."""
    W, hd, u, n, L = hand_frame()

    def P(s, uo=0.0, no=0.0):
        return W + hd * s + u * uo + n * no
    t1 = (hd * 0.65 + u * 0.5 + n * 0.25).normalized()
    t2 = (hd * 0.70 + u * 0.38 + n * 0.32).normalized()
    th = P(0.032, 0.030, 0.006)
    th2 = th + t1 * 0.040
    th3 = th2 + t2 * 0.036
    pts = {
        "thumb_1": (th, th2), "thumb_2": (th2, th3),
        "finger_im_1": (P(0.0915, 0.0175), P(0.1275, 0.0175)), "finger_im_2": (P(0.1275, 0.0175), P(0.1705, 0.0175)),
        "finger_rp_1": (P(0.0895, -0.0175), P(0.1235, -0.0175)), "finger_rp_2": (P(0.1235, -0.0175), P(0.1595, -0.0175)),
    }
    if side == "R":
        pts = {k: (Vector(mirror_x(a)), Vector(mirror_x(b))) for k, (a, b) in pts.items()}
    return pts


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
    # --- added bones
    for s in SIDES:
        for k, (h, tl) in hand_pts(s).items():
            nm = f"{k}.{s}"
            par = f"hand.{s}" if k.endswith("_1") else f"{k[:-1]}1.{s}"
            t[nm] = dict(parent=par, head=A(*h), tail=A(*tl), roll=_FWD, connect=k.endswith("_2"))
        chain = SASH_L if s == "L" else SASH_R
        for i in range(3):
            nm = f"sash_tail.{s}.{i + 1:03d}"
            t[nm] = dict(parent="hips" if i == 0 else f"sash_tail.{s}.{i:03d}", head=A(*chain[i]), tail=A(*chain[i + 1]),
                         roll=_FWD, connect=i > 0)
    for i in range(2):
        nm = f"hair_top.{i + 1:03d}"
        t[nm] = dict(parent="head" if i == 0 else "hair_top.001", head=A(*HAIR_CHAIN[i]), tail=A(*HAIR_CHAIN[i + 1]),
                     roll=_FWD, connect=i > 0)
    return t


def seg_len(a, b):
    return (Vector(a) - Vector(b)).length


LEFT_REST = _L
