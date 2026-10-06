"""Test poses for the deformation sheet (rig convention: local +X = flexion on every joint; other axes are given in
world terms at rest and converted to the bone's local frame; right side = mirror of the left)."""
import math

from mathutils import Quaternion, Vector

FIST = {"index": (75, 95, 55), "middle": (80, 95, 55), "ring": (85, 95, 55), "pinky": (90, 95, 55)}
TIGER = {"index": (25, 75, 70), "middle": (25, 75, 70), "ring": (25, 75, 70), "pinky": (25, 75, 70)}
SWORD = {"index": (0, 3, 3), "middle": (0, 3, 3), "ring": (85, 100, 60), "pinky": (90, 100, 60)}


def _mirror_axis(a):
    return (a[0], -a[1], -a[2])


def world_rot(arm_obj, bone, axis_world, deg):
    """Quaternion (local) rotating `bone` about a world axis (rest frame)."""
    b = arm_obj.data.bones[bone]
    m = b.matrix_local.to_3x3()
    local_axis = m.inverted() @ Vector(axis_world)
    return Quaternion(local_axis.normalized(), math.radians(deg))


def local_x(deg):
    return Quaternion((1, 0, 0), math.radians(deg))


def local_y(deg):
    return Quaternion((0, 1, 0), math.radians(deg))


class Pose:
    def __init__(self, name):
        self.name = name
        self.ops = []           # (bone, kind, value)

    def flex(self, bone, deg, mirror=False):
        self.ops.append((bone, "x", deg))
        if mirror:
            self.ops.append((bone[:-2] + "_r", "x", deg))
        return self

    def twist(self, bone, deg, mirror=False):
        self.ops.append((bone, "y", deg))
        if mirror:
            self.ops.append((bone[:-2] + "_r", "y", -deg))
        return self

    def rot(self, bone, axis, deg, mirror=False):
        self.ops.append((bone, "w", (axis, deg)))
        if mirror:
            self.ops.append((bone[:-2] + "_r", "w", (_mirror_axis(axis), deg)))
        return self

    def hand(self, side, shape, thumb=(30, 30, 20)):
        for f, angs in shape.items():
            for j, a in enumerate(angs, start=1):
                self.ops.append((f"{f}_0{j}_{side}", "x", a))
        for j, a in enumerate(thumb, start=1):
            self.ops.append((f"thumb_0{j}_{side}", "x", a))
        return self

    def apply(self, arm_obj):
        for pb in arm_obj.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = Quaternion()
            pb.location = (0, 0, 0)
        acc = {}
        for bone, kind, val in self.ops:
            if kind == "x":
                q = local_x(val)
            elif kind == "y":
                q = local_y(val)
            else:
                q = world_rot(arm_obj, bone, val[0], val[1])
            acc[bone] = acc.get(bone, Quaternion()) @ q
        for bone, q in acc.items():
            arm_obj.pose.bones[bone].rotation_quaternion = q


def library():
    P = []
    P.append(Pose("rest"))
    horse = Pose("horse stance")
    horse.flex("thigh_l", 72, True).rot("thigh_l", (0, -1, 0), 32, True).flex("calf_l", 88, True)
    horse.flex("foot_l", 14, True)
    horse.flex("upperarm_l", 35, True).flex("lowerarm_l", 110, True).hand("l", FIST).hand("r", FIST)
    horse.rot("upperarm_l", (0, -1, 0), -30, True)
    P.append(horse)
    bow = Pose("bow stance + punch")
    bow.flex("thigh_l", 62).flex("calf_l", 62).flex("foot_l", 0)
    bow.flex("thigh_r", -28).flex("calf_r", 4).flex("foot_r", 18)
    bow.rot("spine_01", (0, 0, 1), 18).rot("spine_03", (0, 0, 1), 10)
    bow.rot("upperarm_r", (0, 1, 0), -40).flex("upperarm_r", 80).flex("lowerarm_r", 4)
    bow.twist("lowerarm_twist_01_r", -54).twist("lowerarm_twist_02_r", -27).twist("hand_r", -90)
    bow.hand("r", FIST).flex("upperarm_l", 20).flex("lowerarm_l", 115).hand("l", FIST)
    P.append(bow)
    kick = Pose("high front kick")
    kick.flex("thigh_r", 112).flex("calf_r", 6).flex("foot_r", -25)
    kick.flex("calf_l", 14).flex("spine_02", -8).flex("spine_04", -6)
    kick.rot("upperarm_l", (0, 1, 0), 30).rot("upperarm_r", (0, 1, 0), 30).flex("lowerarm_l", 60, True)
    P.append(kick)
    tornado = Pose("tornado kick")
    tornado.flex("thigh_l", 95).rot("thigh_l", (0, -1, 0), 25).flex("calf_l", 30)
    tornado.flex("thigh_r", 55).rot("thigh_r", (0, 1, 0), 15).flex("calf_r", 110)
    for b, a in (("spine_01", 10), ("spine_02", 10), ("spine_03", 10), ("spine_04", 8), ("spine_05", 6)):
        tornado.rot(b, (0, 0, 1), a)
    tornado.rot("upperarm_l", (0, -1, 0), 55, True).flex("lowerarm_l", 40, True)
    P.append(tornado)
    over = Pose("arms overhead")
    over.rot("upperarm_l", (0, -1, 0), 115, True).flex("upperarm_l", 15, True).flex("clavicle_l", 28, True)
    over.flex("lowerarm_l", 12, True).hand("l", SWORD).hand("r", TIGER)
    P.append(over)
    side = Pose("arms side + elbows deep")
    side.rot("upperarm_l", (0, -1, 0), 45, True).flex("upperarm_l", 20, True).flex("lowerarm_l", 145, True)
    side.flex("hand_l", 50, True).hand("l", TIGER).hand("r", FIST)
    P.append(side)
    twist = Pose("twist + look")
    for b, a in (("spine_02", 14), ("spine_03", 14), ("spine_04", 12), ("spine_05", 8)):
        twist.rot(b, (0, 0, 1), a)
    twist.rot("neck_01", (0, 0, 1), 25).rot("head", (0, 0, 1), 30).flex("head", 12)
    twist.rot("upperarm_l", (0, 1, 0), 60).flex("upperarm_r", 120).flex("lowerarm_r", 90)
    P.append(twist)
    return P
