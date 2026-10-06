"""Fourfold UE port - the ONE shared skeleton (frozen contract; architect-owned, read-only for every stream).

UE5-Mannequin-compatible: the 89 bones of SK_Mannequin (UE5 Manny / Quinn) with the same names and the same
hierarchy, plus 15 optional `ff_*` secondary bones (hair / sash / hem) that UE's IK Retargeter ignores.
The `character` stream skins its mesh to this armature; the `animation` stream keys clips on it. Both build it with
build_armature() so the rest pose is bit-identical. Never edit positions here without a new spec version: every
exported FBX would have to be regenerated.

Coordinates (Blender, metres): Z up, the character FACES -Y, character LEFT = +X (bones *_l are at +X).
Rest pose: "A-pose" - arms 45 deg below horizontal, palms toward the thighs, fingers straight, legs straight.
Height: head bone tail 1.80 m (skull top ~1.79 m); an athletic 1.78-1.80 m adult (close to UE5 Manny's build).

Local bone axes (Blender convention: +Y runs along the bone). The roll of every bone is set so that the primary
flexion of its joint is a POSITIVE rotation about local +X:
  pelvis/spine_*/neck_*/head   Z = forward (-Y)   +X: bend forward / nod down
  clavicle_*                   Z = up (+Z)        +X: shrug up
  upperarm/lowerarm (+twists)  Z = forward        +X: swing the bone forward (shoulder flexion / elbow flexion)
  hand, metacarpals, fingers   Z = palm normal    +X: curl toward the palm (fist)
  thumb_*                      Z = palm normal    +X: curl the thumb into the palm
  thigh (+twists)              Z = forward        +X: hip flexion (knee comes up)
  calf  (+twists)              Z = backward (+Y)  +X: knee flexion (heel goes back)
  foot, ball                   Z = up             +X: toes up (dorsiflexion / toe extension)
  ik_*, interaction, center_of_mass, root-level helpers: Y up, Z forward (ik_foot_*/ik_hand_* copy foot/hand frames)
  ff_* secondary chains        Z = forward (hair: Z = up)
Roll targets are projected onto the plane perpendicular to the bone (Blender align_roll), so "Z = up" on a sloped foot
means "the perpendicular closest to up".
Twist bones are children of the bone they twist and lie on its axis (UE5 Manny layout).

FBX: the armature OBJECT is named "root" and becomes UE's `root` bone (there is NO bone called "root" in Blender);
pelvis, ik_foot_root, ik_hand_root, interaction and center_of_mass are top-level bones (children of root in UE).
Export settings live in ff_fbx_export.py (also frozen).
"""
import math

SPEC_VERSION = "ff-manny-1.0"
ARMATURE_OBJECT_NAME = "root"

# ---------------------------------------------------------------- geometry helpers (no bpy needed)

def _v(*a):
    return tuple(float(x) for x in a)


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def _norm(a):
    l = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-12 else (0.0, 0.0, 1.0)


def _mirror(p):
    return (-p[0], p[1], p[2])


FWD = (0.0, -1.0, 0.0)
BACK = (0.0, 1.0, 0.0)
UP = (0.0, 0.0, 1.0)

# ---------------------------------------------------------------- landmarks (left side; right = mirror in x)
ARM_DOWN_DEG = 45.0
_c, _s = math.cos(math.radians(ARM_DOWN_DEG)), math.sin(math.radians(ARM_DOWN_DEG))
ARM_DIR_L = _norm((_c, 0.0, -_s))                 # along the left arm, out and down
PALM_N_L = _norm((-_s, 0.0, -_c))                 # left palm normal: toward the thigh (in and down)

SHOULDER_L = _v(0.175, 0.005, 1.440)              # upperarm_l head
UPPERARM_LEN, LOWERARM_LEN, HAND_LEN = 0.290, 0.255, 0.090
ELBOW_L = _add(SHOULDER_L, _mul(ARM_DIR_L, UPPERARM_LEN))
WRIST_L = _add(ELBOW_L, _mul(ARM_DIR_L, LOWERARM_LEN))
HAND_TIP_L = _add(WRIST_L, _mul(ARM_DIR_L, HAND_LEN))

HIP_L = _v(0.095, 0.000, 0.920)
KNEE_L = _v(0.100, -0.010, 0.500)
ANKLE_L = _v(0.105, 0.030, 0.085)
BALL_L = _v(0.110, -0.100, 0.025)
TOE_L = _v(0.110, -0.170, 0.020)

# fingers: name -> (offset across the hand along -Y (forward = index), metacarpal len, phalanx lengths)
_FINGERS = {
    "index": (-0.026, 0.080, (0.040, 0.025, 0.020)),
    "middle": (-0.008, 0.082, (0.045, 0.028, 0.022)),
    "ring": (0.010, 0.078, (0.042, 0.026, 0.020)),
    "pinky": (0.027, 0.072, (0.032, 0.020, 0.018)),
}
_THUMB_LENS = (0.040, 0.032, 0.027)

# ---------------------------------------------------------------- bone table
# Each entry: name, parent (None = top level, i.e. child of the UE root), head, tail, z_axis (roll target), deform.
BONES = []


def _bone(name, parent, head, tail, z, deform=True):
    BONES.append({"name": name, "parent": parent, "head": tuple(head), "tail": tuple(tail), "z": tuple(_norm(z)),
                  "deform": deform})


def _build_table():
    BONES.clear()
    # centre line ------------------------------------------------------------------------------------------
    spine = [("pelvis", None, _v(0, 0.000, 0.960), _v(0, 0.005, 1.030)),
             ("spine_01", "pelvis", _v(0, 0.005, 1.030), _v(0, 0.012, 1.105)),
             ("spine_02", "spine_01", _v(0, 0.012, 1.105), _v(0, 0.014, 1.180)),
             ("spine_03", "spine_02", _v(0, 0.014, 1.180), _v(0, 0.012, 1.260)),
             ("spine_04", "spine_03", _v(0, 0.012, 1.260), _v(0, 0.006, 1.350)),
             ("spine_05", "spine_04", _v(0, 0.006, 1.350), _v(0, -0.002, 1.495)),
             ("neck_01", "spine_05", _v(0, -0.002, 1.495), _v(0, -0.010, 1.555)),
             ("neck_02", "neck_01", _v(0, -0.010, 1.555), _v(0, -0.018, 1.615)),
             ("head", "neck_02", _v(0, -0.018, 1.615), _v(0, -0.018, 1.800))]
    for n, p, h, t in spine:
        _bone(n, p, h, t, FWD)

    for side, mir in (("l", lambda p: p), ("r", _mirror)):
        s = side
        sgn = 1.0 if side == "l" else -1.0
        arm = mir(ARM_DIR_L) if side == "l" else _mirror(ARM_DIR_L)
        palm = PALM_N_L if side == "l" else _mirror(PALM_N_L)
        sh, el, wr, ht = (mir(SHOULDER_L), mir(ELBOW_L), mir(WRIST_L), mir(HAND_TIP_L)) if side == "l" else \
            (_mirror(SHOULDER_L), _mirror(ELBOW_L), _mirror(WRIST_L), _mirror(HAND_TIP_L))
        clav_head = _v(0.025 * sgn, -0.030, 1.430)
        _bone("clavicle_" + s, "spine_05", clav_head, sh, UP)
        _bone("upperarm_" + s, "clavicle_" + s, sh, el, FWD)
        _bone("lowerarm_" + s, "upperarm_" + s, el, wr, FWD)
        _bone("hand_" + s, "lowerarm_" + s, wr, ht, palm)
        # twist bones on the parent axis (UE5 Manny layout): upperarm 01 near the shoulder, 02 mid;
        # lowerarm 01 near the wrist, 02 mid
        ua = _norm(_sub(el, sh))
        la = _norm(_sub(wr, el))
        for nm, par, a, b, frac, ax in (("upperarm_twist_01_", "upperarm_", sh, el, 0.20, ua),
                                        ("upperarm_twist_02_", "upperarm_", sh, el, 0.55, ua),
                                        ("lowerarm_twist_01_", "lowerarm_", el, wr, 0.80, la),
                                        ("lowerarm_twist_02_", "lowerarm_", el, wr, 0.45, la)):
            p0 = _add(a, _mul(_sub(b, a), frac))
            _bone(nm + s, par + s, p0, _add(p0, _mul(ax, 0.06)), FWD)
        # fingers: metacarpals start just past the wrist, spread across the hand along Y (index forward)
        for fname, (off, mlen, (l1, l2, l3)) in _FINGERS.items():
            base = _add(_add(wr, _mul(arm, 0.012)), (0.0, off, 0.0))
            knuckle = _add(base, _mul(arm, mlen))
            _bone(f"{fname}_metacarpal_{s}", "hand_" + s, base, knuckle, palm)
            p = knuckle
            parent = f"{fname}_metacarpal_{s}"
            for i, ln in enumerate((l1, l2, l3), start=1):
                q = _add(p, _mul(arm, ln))
                _bone(f"{fname}_0{i}_{s}", parent, p, q, palm)
                parent = f"{fname}_0{i}_{s}"
                p = q
        # thumb: from the wrist toward the front and across the palm
        tdir = _norm(_add(_add(_mul(arm, 0.55), _mul(FWD, 0.75)), _mul(palm, 0.30)))
        p = _add(_add(_add(wr, _mul(arm, 0.018)), (0.0, -0.022, 0.0)), _mul(palm, 0.012))
        parent = "hand_" + s
        for i, ln in enumerate(_THUMB_LENS, start=1):
            q = _add(p, _mul(tdir, ln))
            _bone(f"thumb_0{i}_{s}", parent, p, q, palm)
            parent = f"thumb_0{i}_{s}"
            p = q
        # leg
        hip, knee, ankle, ball, toe = [(x if side == "l" else _mirror(x)) for x in (HIP_L, KNEE_L, ANKLE_L, BALL_L, TOE_L)]
        _bone("thigh_" + s, "pelvis", hip, knee, FWD)
        _bone("calf_" + s, "thigh_" + s, knee, ankle, BACK)
        _bone("foot_" + s, "calf_" + s, ankle, ball, UP)
        _bone("ball_" + s, "foot_" + s, ball, toe, UP)
        ta = _norm(_sub(knee, hip))
        ca = _norm(_sub(ankle, knee))
        for nm, par, a, b, frac, ax, z in (("thigh_twist_01_", "thigh_", hip, knee, 0.20, ta, FWD),
                                           ("thigh_twist_02_", "thigh_", hip, knee, 0.55, ta, FWD),
                                           ("calf_twist_01_", "calf_", knee, ankle, 0.80, ca, BACK),
                                           ("calf_twist_02_", "calf_", knee, ankle, 0.45, ca, BACK)):
            p0 = _add(a, _mul(_sub(b, a), frac))
            _bone(nm + s, par + s, p0, _add(p0, _mul(ax, 0.06)), z)

    # helper bones (UE5 Manny): IK targets and markers. Non-deforming but exported (they are part of the skeleton).
    o = _v(0, 0, 0)
    _bone("ik_foot_root", None, o, _v(0, 0, 0.10), FWD, deform=False)
    for s in ("l", "r"):
        f = next(b for b in BONES if b["name"] == "foot_" + s)
        _bone("ik_foot_" + s, "ik_foot_root", f["head"], f["tail"], f["z"], deform=False)
    _bone("ik_hand_root", None, o, _v(0, 0, 0.10), FWD, deform=False)
    hr = next(b for b in BONES if b["name"] == "hand_r")
    _bone("ik_hand_gun", "ik_hand_root", hr["head"], hr["tail"], hr["z"], deform=False)
    for s in ("l", "r"):
        h = next(b for b in BONES if b["name"] == "hand_" + s)
        _bone("ik_hand_" + s, "ik_hand_gun", h["head"], h["tail"], h["z"], deform=False)
    _bone("interaction", None, o, _v(0, 0, 0.10), FWD, deform=False)
    _bone("center_of_mass", None, _v(0, 0.0, 0.960), _v(0, 0.0, 1.060), FWD, deform=False)

    # Fourfold secondary bones (optional to skin; driven by runtime springs, never keyed in clips) -------------
    hair = [_v(0, 0.075, 1.745), _v(0, 0.115, 1.715), _v(0, 0.145, 1.655), _v(0, 0.160, 1.590)]
    for i in range(3):
        _bone(f"ff_hair_0{i + 1}", "head" if i == 0 else f"ff_hair_0{i}", hair[i], hair[i + 1], UP)
    for s, sg in (("l", 1.0), ("r", -1.0)):
        sash = [_v(0.120 * sg, -0.080, 1.000), _v(0.140 * sg, -0.085, 0.860), _v(0.150 * sg, -0.085, 0.720)]
        for i in range(2):
            _bone(f"ff_sash_{s}_0{i + 1}", "pelvis" if i == 0 else f"ff_sash_{s}_0{i}", sash[i], sash[i + 1], FWD)
    for nm, x, y in (("fl", 0.085, -0.125), ("fr", -0.085, -0.125), ("bl", 0.085, 0.125), ("br", -0.085, 0.125)):
        pts = [_v(x, y, 0.940), _v(x * 1.08, y * 1.06, 0.745), _v(x * 1.12, y * 1.10, 0.550)]
        for i in range(2):
            _bone(f"ff_hem_{nm}_0{i + 1}", "pelvis" if i == 0 else f"ff_hem_{nm}_0{i}", pts[i], pts[i + 1], FWD)


_build_table()

# The 89 bone names of UE5 SK_Mannequin (root + 88), for validation. "root" is the armature object.
UE5_MANNY_BONES = ["root", "pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02",
                   "head"] + [f"{b}_{s}" for s in ("l", "r") for b in (
                       "clavicle", "upperarm", "lowerarm", "hand", "upperarm_twist_01", "upperarm_twist_02",
                       "lowerarm_twist_01", "lowerarm_twist_02",
                       "index_metacarpal", "index_01", "index_02", "index_03",
                       "middle_metacarpal", "middle_01", "middle_02", "middle_03",
                       "ring_metacarpal", "ring_01", "ring_02", "ring_03",
                       "pinky_metacarpal", "pinky_01", "pinky_02", "pinky_03",
                       "thumb_01", "thumb_02", "thumb_03",
                       "thigh", "calf", "foot", "ball", "thigh_twist_01", "thigh_twist_02", "calf_twist_01",
                       "calf_twist_02")] + [
                   "ik_foot_root", "ik_foot_l", "ik_foot_r", "ik_hand_root", "ik_hand_gun", "ik_hand_l", "ik_hand_r",
                   "interaction", "center_of_mass"]
FF_SECONDARY_BONES = [b["name"] for b in BONES if b["name"].startswith("ff_")]
HELPER_BONES = ["ik_foot_root", "ik_foot_l", "ik_foot_r", "ik_hand_root", "ik_hand_gun", "ik_hand_l", "ik_hand_r",
                "interaction", "center_of_mass"]
# Bones a clip may key (rotation on all of them, location on pelvis only). Helpers / ff_* are never keyed.
ANIM_BONES = [b["name"] for b in BONES if b["deform"] and not b["name"].startswith("ff_")]


def bone(name):
    for b in BONES:
        if b["name"] == name:
            return b
    raise KeyError(name)


def parent_map():
    """UE view of the hierarchy: top-level Blender bones are children of 'root'."""
    return {b["name"]: (b["parent"] or "root") for b in BONES}


def self_check():
    names = [b["name"] for b in BONES]
    assert len(names) == len(set(names)), "duplicate bone names"
    manny = set(UE5_MANNY_BONES) - {"root"}
    have = set(names)
    missing = sorted(manny - have)
    assert not missing, f"missing UE5 Manny bones: {missing}"
    assert len(UE5_MANNY_BONES) == 89, len(UE5_MANNY_BONES)
    pm = parent_map()
    for n, p in pm.items():
        assert p == "root" or p in have, (n, p)
    extra = sorted(have - manny)
    assert all(e.startswith("ff_") for e in extra), extra
    return {"bones": len(names), "manny": len(manny) + 1, "extras": extra}


# ---------------------------------------------------------------- bpy side

def build_armature(collection=None, name=ARMATURE_OBJECT_NAME, show_in_front=True):
    """Creates the armature object (named 'root') with every bone of BONES. Returns the object.
    The object stays at the origin with identity transform (required by the FBX convention)."""
    import bpy
    from mathutils import Vector
    arm = bpy.data.armatures.new(name + "_data")
    obj = bpy.data.objects.new(name, arm)
    (collection or bpy.context.scene.collection).objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones
    for b in BONES:
        e = eb.new(b["name"])
        e.head = Vector(b["head"])
        e.tail = Vector(b["tail"])
        e.align_roll(Vector(b["z"]))
        e.use_deform = bool(b["deform"])
        e.use_connect = False
    for b in BONES:
        if b["parent"]:
            eb[b["name"]].parent = eb[b["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.display_type = "STICK"
    obj.show_in_front = show_in_front
    for pb in obj.pose.bones:
        pb.rotation_mode = "QUATERNION"
    return obj


def validate_armature(obj, tol=1e-4):
    """Checks an armature object against the spec (names, parents, rest heads/tails, roll axes). Returns a list of
    problems (empty = OK)."""
    from mathutils import Vector
    problems = []
    if obj.name != ARMATURE_OBJECT_NAME:
        problems.append(f"armature object must be named '{ARMATURE_OBJECT_NAME}', got '{obj.name}'")
    if any(abs(x) > 1e-6 for x in obj.location) or any(abs(x) > 1e-6 for x in obj.rotation_euler) or \
            any(abs(x - 1.0) > 1e-6 for x in obj.scale):
        problems.append("armature object transform must be identity")
    bones = obj.data.bones
    if "root" in bones:
        problems.append("there must be no bone called 'root' (the object is the UE root)")
    for b in BONES:
        bb = bones.get(b["name"])
        if bb is None:
            problems.append("missing bone " + b["name"])
            continue
        par = bb.parent.name if bb.parent else None
        if par != b["parent"]:
            problems.append(f"{b['name']}: parent {par} != {b['parent']}")
        if (bb.head_local - Vector(b["head"])).length > tol or (bb.tail_local - Vector(b["tail"])).length > tol:
            problems.append(f"{b['name']}: rest head/tail moved")
        m3 = bb.matrix_local.to_3x3()
        y, z = m3.col[1], m3.col[2]
        want = Vector(b["z"]) - y * Vector(b["z"]).dot(y)
        if want.length > 1e-6 and z.dot(want.normalized()) < 0.995:
            problems.append(f"{b['name']}: roll differs (local Z {tuple(round(c, 3) for c in z)})")
    extra = [bb.name for bb in bones if bb.name not in {b['name'] for b in BONES}]
    if extra:
        problems.append("bones not in the spec (export armature must be exact; use a separate control rig): " + ", ".join(extra))
    return problems


if __name__ == "__main__":
    print(self_check())
