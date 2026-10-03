"""Pose engine for the fighter rig: forward kinematics + analytic two-bone IK, keyed-state interpolation
and baking to Blender actions.

Everything animation-side is authored as *states* (dict of channel -> tuple of floats):

    hp      hips translation offset from rest (A frame, m)             (dx, dy, dz)
    hips, spine, chest, neck, head   rotation (deg) pitch/side/yaw      (pitch+ = forward, side+ = left, yaw+ = left)
    sl, sr  shoulder bone manual offset (deg)                           (lift, protract)
    hl, hr  hand target, in the chest-aligned "shoulder space"          (px,py,pz, fx,fy,fz, mx,my,mz, ex,ey,ez, ow)
              p  wrist position relative to the (chest-posed) rest shoulder joint, chest axes, A frame (m)
              f  finger direction, m palm-normal direction, e elbow pole direction (chest axes, A frame)
              ow weight of the f/m orientation versus a neutral wrist
    fl, fr  foot placement in the root frame                            (x, y, lift, yaw, pitch, roll, pv, toe, kyaw, kup)
              x,y,lift  ground position / height of the foot pivot point (pv = pivot offset ahead of the ankle, m)
              yaw (deg, + = toes left), pitch (deg, + = toes up i.e. heel strike, - = heel up on the ball)
              roll (deg), toe (extra toe flex-up, deg), kyaw (extra knee pole yaw), kup (knee pole elevation)
"""
import math

import bpy
from mathutils import Matrix, Quaternion, Vector

from fighter_skeleton import BONE_ORDER, A, LEFT_REST, SIDES

DEG = math.pi / 180.0
FPS = 30


def Rx(a):
    return Matrix.Rotation(a * DEG, 3, "X")


def Ry(a):
    return Matrix.Rotation(a * DEG, 3, "Y")


def Rz(a):
    return Matrix.Rotation(a * DEG, 3, "Z")


def sem(pitch, side, yaw):
    """Character-aligned rotation (Blender axes): pitch fwd+, side left+, yaw left+ (degrees)."""
    return Rz(yaw) @ Ry(side) @ Rx(pitch)


def blA(v):
    """A-frame vector -> Blender."""
    return Vector((v[0], -v[1], v[2]))


def frame_align(lp, wp, ls, ws):
    """Rotation mapping local primary lp -> world primary wp (exactly) and the local secondary
    towards the world secondary (as far as orthogonality allows)."""
    e1 = Vector(lp).normalized()
    e2 = Vector(ls) - e1 * Vector(ls).dot(e1)
    e2.normalize()
    e3 = e1.cross(e2)
    f1 = Vector(wp).normalized()
    f2 = Vector(ws) - f1 * Vector(ws).dot(f1)
    if f2.length < 1e-6:
        f2 = Vector((0, 0, 1)) if abs(f1.z) < 0.9 else Vector((1, 0, 0))
        f2 = f2 - f1 * f2.dot(f1)
    f2.normalize()
    f3 = f1.cross(f2)
    E = Matrix(((e1.x, e2.x, e3.x), (e1.y, e2.y, e3.y), (e1.z, e2.z, e3.z)))
    F = Matrix(((f1.x, f2.x, f3.x), (f1.y, f2.y, f3.y), (f1.z, f2.z, f3.z)))
    return F @ E.transposed()


def two_bone(Apos, Cpos, l1, l2, pole, max_ext=0.995):
    """Returns (B, C_actual, reach_error).  pole: world direction the joint should bend toward."""
    to = Cpos - Apos
    d = to.length
    dmax = (l1 + l2) * max_ext
    dmin = abs(l1 - l2) + 0.02
    dc = min(max(d, dmin), dmax)
    dirv = to / d if d > 1e-9 else Vector((0, 0, -1))
    a = (l1 * l1 - l2 * l2 + dc * dc) / (2 * dc)
    h = math.sqrt(max(l1 * l1 - a * a, 0.0))
    v = pole - dirv * pole.dot(dirv)
    if v.length < 1e-4:
        v = Vector((0, -1, 0)) - dirv * dirv.y * -1.0
    v.normalize()
    B = Apos + dirv * a + v * h
    Cact = Apos + dirv * dc
    return B, Cact, max(0.0, d - dc), v, dirv


# ---------------------------------------------------------------------------------------
class RigModel:
    """Rest-pose data read back from the Blender armature, plus the solver."""

    ARM_W = 0.30          # shoulder bone follows this fraction of the hand direction change
    ARM_LIM = 30.0        # ... clamped to this many degrees
    TWIST_FOREARM = 0.5   # fraction of wrist roll absorbed by the forearm bone
    MAX_EXT = 0.997

    def __init__(self, arm_ob):
        arm = arm_ob.data
        self.order = list(BONE_ORDER)
        self.parent, self.head, self.tail, self.R, self.Rinv, self.length, self.off = {}, {}, {}, {}, {}, {}, {}
        for name in self.order:
            b = arm.bones[name]
            self.parent[name] = b.parent.name if b.parent else None
            self.head[name] = Vector(b.head_local)
            self.tail[name] = Vector(b.tail_local)
            self.R[name] = b.matrix_local.to_3x3().copy()
            self.Rinv[name] = self.R[name].inverted()
            self.length[name] = (self.tail[name] - self.head[name]).length
        for name in self.order:
            p = self.parent[name]
            self.off[name] = self.Rinv[p] @ (self.head[name] - self.head[p]) if p else Vector()
        self.hinge = {}
        self.palm_local = {}
        for s in SIDES:
            for (a, b, c) in ((f"thigh.{s}", f"shin.{s}", f"foot.{s}"), (f"upper_arm.{s}", f"forearm.{s}", f"hand.{s}")):
                A0, B0, C0 = self.head[a], self.head[b], self.head[c]
                d0 = (C0 - A0).normalized()
                v0 = (B0 - A0) - d0 * (B0 - A0).dot(d0)
                v0.normalize()
                n0 = d0.cross(v0)
                self.hinge[a] = self.Rinv[a] @ n0
                self.hinge[b] = self.Rinv[b] @ n0
            sx = 1 if s == "L" else -1
            self.palm_local[f"hand.{s}"] = self.Rinv[f"hand.{s}"] @ Vector((-sx, 0, 0))
        self.ankle_h = LEFT_REST["ankle"][2]
        self.ball_h = LEFT_REST["ball"][2]
        sh = self.tail["shoulder.L"]
        # shoulder rest direction to the wrist (chest-local) for the auto-aim
        self.rest_aim = {}
        for s in SIDES:
            self.rest_aim[s] = (self.head[f"hand.{s}"] - self.head[f"shoulder.{s}"]).normalized()

    # ------------------------------------------------------------------ helpers
    def _world_to_local(self, name, Rp):
        p = self.parent[name]
        return (self.Rinv[name] @ self.R[p] @ Rp[p].inverted() @ Rp[name]).to_quaternion()

    # ------------------------------------------------------------------ body FK only (used by space conversion)
    def body_fk(self, st):
        Rp, Hp, ql = {}, {}, {}
        Rp["root"] = self.R["root"].copy()
        Hp["root"] = self.head["root"].copy()
        hp = blA(st["hp"])
        for name in ("hips", "spine", "chest", "neck", "head"):
            p = self.parent[name]
            D = sem(*st[name])
            Ql = self.Rinv[name] @ D @ self.R[name]
            Rp[name] = Rp[p] @ self.Rinv[p] @ self.R[name] @ Ql
            Hp[name] = Hp[p] + Rp[p] @ self.off[name]
            if name == "hips":
                Hp[name] = Hp[name] + hp
            ql[name] = Ql.to_quaternion()
        return Rp, Hp, ql

    def chest_delta(self, Rp):
        return Rp["chest"] @ self.Rinv["chest"]

    # ------------------------------------------------------------------ full solve
    def solve(self, st, ctx=None):
        """Returns ({bone: Quaternion}, hips_location_local Vector, diag dict).
        ctx: optional dict carried between consecutive frames (keeps the wrist roll continuous, no 360 deg wraps)."""
        R, Rinv, off = self.R, self.Rinv, self.off
        Rp, Hp, ql = self.body_fk(st)
        diag = {}
        Dc = self.chest_delta(Rp)
        hp = blA(st["hp"])
        hips_loc = Rinv["hips"] @ hp

        # shoulder reference (chest-attached rest shoulder joint)
        sh_rest_tail = {s: self.tail[f"shoulder.{s}"] for s in SIDES}

        for s in SIDES:
            sx = 1 if s == "L" else -1
            hk = st["hl" if s == "L" else "hr"]
            shn, uan, fan, han = f"shoulder.{s}", f"upper_arm.{s}", f"forearm.{s}", f"hand.{s}"
            S0 = Hp["chest"] + Rp["chest"] @ (Rinv["chest"] @ (sh_rest_tail[s] - self.head["chest"]))
            T = S0 + Dc @ blA(hk[0:3])
            # --- shoulder bone: partial aim toward the hand + manual offsets
            Hs = Hp["chest"] + Rp["chest"] @ off[shn]
            dT = (Dc.inverted() @ (T - Hs)).normalized()
            Rt = self.rest_aim[s].rotation_difference(dT)
            ang = Rt.angle
            w = self.ARM_W
            if ang * w > self.ARM_LIM * DEG:
                w = self.ARM_LIM * DEG / ang
            Rt_w = Quaternion().slerp(Rt, w).to_matrix()
            lift, prot = st["sl" if s == "L" else "sr"]
            Dm = Rz(-sx * prot) @ Ry(-sx * lift)
            Rp[shn] = Dm @ Dc @ Rt_w @ R[shn]
            Hp[shn] = Hs
            ql[shn] = self._world_to_local(shn, Rp)
            S1 = Hs + Rp[shn] @ Vector((0, self.length[shn], 0))      # shoulder joint after motion
            # --- arm IK
            pole = Dc @ blA(hk[9:12])
            l1, l2 = self.length[uan], self.length[fan]
            B, Cact, miss, vhat, dirv = two_bone(S1, T, l1, l2, pole, self.MAX_EXT)
            nrm = dirv.cross(vhat)
            y1 = (B - S1).normalized()
            y2 = (Cact - B).normalized()
            Rp[uan] = frame_align((0, 1, 0), y1, self.hinge[uan], nrm)
            Hp[uan] = S1
            ql[uan] = self._world_to_local(uan, Rp)
            Rp[fan] = frame_align((0, 1, 0), y2, self.hinge[fan], nrm)
            Hp[fan] = B
            # hand orientation
            neutral = Rp[fan] @ Rinv[fan] @ R[han]
            ow = hk[12]
            if ow > 1e-4:
                fvec = Dc @ blA(hk[3:6])
                mvec = Dc @ blA(hk[6:9])
                spec = frame_align((0, 1, 0), fvec, self.palm_local[han], mvec)
                qn, qs = neutral.to_quaternion(), spec.to_quaternion()
                if qn.dot(qs) < 0:
                    qs.negate()
                tgt = qn.slerp(qs, min(ow, 1.0)).to_matrix()
            else:
                tgt = neutral
            # share the roll between forearm and wrist
            rel = (neutral.inverted() @ tgt).to_quaternion()
            tw = 2.0 * math.atan2(rel.y, rel.w)
            if tw > math.pi:
                tw -= 2 * math.pi
            elif tw < -math.pi:
                tw += 2 * math.pi
            if ctx is not None:
                last = ctx.get(("tw", s))
                if last is not None:
                    tw += 2 * math.pi * round((last - tw) / (2 * math.pi))
                ctx[("tw", s)] = tw
            Rp[fan] = Rp[fan] @ Matrix.Rotation(tw * self.TWIST_FOREARM, 3, "Y")
            Hp[han] = Cact
            Rp[han] = tgt
            ql[fan] = self._world_to_local(fan, Rp)
            ql[han] = self._world_to_local(han, Rp)
            diag[f"reach_{s}_arm"] = miss

        # ------------------------------------------------------------------ legs
        for s in SIDES:
            sx = 1 if s == "L" else -1
            fk = st["fl" if s == "L" else "fr"]
            x, y, lift, yaw, pitch, roll, pv, toe, kyaw, kup = fk
            thn, shn, fon, ton = f"thigh.{s}", f"shin.{s}", f"foot.{s}", f"toe.{s}"
            Hhip = Hp["hips"] + Rp["hips"] @ off[thn]
            D = Rz(yaw) @ Ry(roll) @ Rx(-pitch)
            pivot = A(x, y, lift)
            # the pivot rolls on the sole: heel contact point (height 0) -> ball-of-foot joint (height of the toe joint)
            ph = self.ball_h * min(max((pv + 0.075) / 0.21, 0.0), 1.0)
            C = pivot + Vector((0, 0, ph)) + D @ Vector((0, pv, self.ankle_h - ph))
            kdir = Vector((math.sin((yaw + kyaw) * DEG), -math.cos((yaw + kyaw) * DEG), kup))   # blender coords
            l1, l2 = self.length[thn], self.length[shn]
            B, Cact, miss, vhat, dirv = two_bone(Hhip, C, l1, l2, kdir, self.MAX_EXT)
            nrm = dirv.cross(vhat)
            y1 = (B - Hhip).normalized()
            y2 = (Cact - B).normalized()
            Rp[thn] = frame_align((0, 1, 0), y1, self.hinge[thn], nrm)
            Hp[thn] = Hhip
            ql[thn] = self._world_to_local(thn, Rp)
            Rp[shn] = frame_align((0, 1, 0), y2, self.hinge[shn], nrm)
            Hp[shn] = B
            ql[shn] = self._world_to_local(shn, Rp)
            Rp[fon] = D @ R[fon]
            Hp[fon] = Cact
            ql[fon] = self._world_to_local(fon, Rp)
            # toe: stay flat on the ground when the heel is raised, plus manual flex
            toe_up = max(0.0, -pitch) + toe
            Dl = Rx(-toe_up)
            Qt = Rinv[ton] @ Dl @ R[ton]
            ql[ton] = Qt.to_quaternion()
            diag[f"reach_{s}_leg"] = miss
            diag[f"ankle_{s}"] = Cact.copy()
        return ql, hips_loc, diag


# ---------------------------------------------------------------------------------------
# state helpers
# ---------------------------------------------------------------------------------------
HAND_FIELDS = {"p": (0, 3), "f": (3, 6), "m": (6, 9), "e": (9, 12), "ow": (12, 13)}
FOOT_FIELDS = {"x": 0, "y": 1, "lift": 2, "yaw": 3, "pitch": 4, "roll": 5, "pv": 6, "toe": 7, "kyaw": 8, "kup": 9}
ROT_CHANNELS = ("hips", "spine", "chest", "neck", "head")
ALL_CHANNELS = ("hp",) + ROT_CHANNELS + ("sl", "sr", "hl", "hr", "fl", "fr")


def merge_hand(prev, upd):
    """upd: dict with any of p,f,m,e,ow (A-frame values) -> new 13-tuple.  (space conversion happens earlier.)"""
    t = list(prev)
    for k, v in upd.items():
        if k in HAND_FIELDS:
            a, b = HAND_FIELDS[k]
            if b - a == 1:
                t[a] = float(v)
            else:
                t[a:b] = [float(c) for c in v]
    return tuple(t)


def merge_foot(prev, upd):
    """Merge a partial foot spec.  Changing only `pv` keeps the *footprint* (the flat-foot ankle ground point) fixed,
    i.e. the pivot x,y slides along the foot by the pv difference, so planted feet never skate when they start to pivot."""
    t = list(prev)
    if "pv" in upd and "x" not in upd and "y" not in upd:
        x0, y0, yaw0, pv0 = prev[0], prev[1], prev[3], prev[6]
        fx = x0 - pv0 * math.sin(yaw0 * DEG)
        fy = y0 - pv0 * math.cos(yaw0 * DEG)
        yaw1 = float(upd.get("yaw", yaw0))
        pv1 = float(upd["pv"])
        t[0] = fx + pv1 * math.sin(yaw1 * DEG)
        t[1] = fy + pv1 * math.cos(yaw1 * DEG)
    for k, v in upd.items():
        if k in FOOT_FIELDS:
            t[FOOT_FIELDS[k]] = float(v)
    return tuple(t)


def mirror_state_value(ch, v):
    """Mirror a single channel's value across the sagittal plane (without swapping sides)."""
    if ch == "hp":
        return (-v[0], v[1], v[2])
    if ch in ROT_CHANNELS:
        return (v[0], -v[1], -v[2])
    if ch in ("sl", "sr"):
        return tuple(v)
    if ch in ("hl", "hr"):
        t = list(v)
        for a in (0, 3, 6, 9):
            t[a] = -t[a]
        return tuple(t)
    if ch in ("fl", "fr"):
        t = list(v)
        t[0] = -t[0]      # x
        t[3] = -t[3]      # yaw
        t[5] = -t[5]      # roll
        t[8] = -t[8]      # kyaw
        return tuple(t)
    raise KeyError(ch)


SWAP = {"hl": "hr", "hr": "hl", "fl": "fr", "fr": "fl", "sl": "sr", "sr": "sl"}


def mirror_state(st):
    out = {}
    for ch, v in st.items():
        out[SWAP.get(ch, ch)] = mirror_state_value(ch, v)
    return out


# ---------------------------------------------------------------------------------------
# easing / interpolation
# ---------------------------------------------------------------------------------------
def _ease(kind, u):
    if kind == "lin":
        return u
    if kind == "io":
        return u * u * (3 - 2 * u)
    if kind == "io3":
        return u * u * u * (u * (u * 6 - 15) + 10)
    if kind == "in":
        return u * u
    if kind == "in3":
        return u * u * u
    if kind == "out":
        return 1 - (1 - u) * (1 - u)
    if kind == "out3":
        return 1 - (1 - u) ** 3
    if kind == "ovs":       # ease-out with a little overshoot
        c = 1.2
        return 1 + (c + 1) * (u - 1) ** 3 + c * (u - 1) ** 2
    if kind == "hold":
        return 0.0 if u < 1.0 else 1.0
    raise ValueError(kind)


class Timeline:
    """Keyed full states.  keys: list of (frame, state, ease); `ease` describes the segment ENDING at that key.
    Loop clips are periodic (a copy of the first key is appended at frame n); non-loop clips hold the last key."""

    def __init__(self, keys, nframes, loop):
        keys = sorted(keys, key=lambda k: k[0])
        self.n = nframes
        self.loop = loop
        if loop:
            assert keys[0][0] == 0, "loop clips need a key at frame 0"
            if keys[-1][0] != nframes:
                keys.append((nframes, keys[0][1], keys[-1][2]))
            else:
                keys[-1] = (nframes, keys[0][1], keys[-1][2])
        elif keys[-1][0] < nframes:
            keys.append((nframes, keys[-1][1], "lin"))
        self.keys = keys
        self.t = [k[0] for k in keys]
        # Hermite tangents per key (for 'sp' segments)
        m = len(keys)
        self.tan = []
        for i in range(m):
            if self.loop:
                if i == 0:
                    prev_i, prev_t, next_i, next_t = m - 2, keys[m - 2][0] - nframes, 1, keys[1][0]
                elif i == m - 1:
                    prev_i, prev_t, next_i, next_t = m - 2, keys[m - 2][0], 1, keys[1][0] + nframes
                else:
                    prev_i, prev_t, next_i, next_t = i - 1, keys[i - 1][0], i + 1, keys[i + 1][0]
                span = next_t - prev_t
                pa, pb = keys[prev_i][1], keys[next_i][1]
                self.tan.append({ch: tuple((pb[ch][k] - pa[ch][k]) / span for k in range(len(pa[ch]))) for ch in pa})
            else:
                if i == 0 or i == m - 1:
                    st = keys[i][1]
                    self.tan.append({ch: tuple(0.0 for _ in st[ch]) for ch in st})
                else:
                    span = keys[i + 1][0] - keys[i - 1][0]
                    pa, pb = keys[i - 1][1], keys[i + 1][1]
                    self.tan.append({ch: tuple((pb[ch][k] - pa[ch][k]) / span for k in range(len(pa[ch]))) for ch in pa})

    def eval(self, f):
        if self.loop:
            f = f % self.n
        else:
            f = min(max(f, self.t[0]), self.t[-1])
        i = 0
        while i < len(self.t) - 2 and self.t[i + 1] <= f:
            i += 1
        t0, t1 = self.t[i], self.t[i + 1]
        s0, s1 = self.keys[i][1], self.keys[i + 1][1]
        ease = self.keys[i + 1][2]
        u = 0.0 if t1 == t0 else (f - t0) / (t1 - t0)
        out = {}
        if ease == "sp":
            dt = t1 - t0
            m0, m1 = self.tan[i], self.tan[i + 1]
            h00 = 2 * u ** 3 - 3 * u ** 2 + 1
            h10 = u ** 3 - 2 * u ** 2 + u
            h01 = -2 * u ** 3 + 3 * u ** 2
            h11 = u ** 3 - u ** 2
            for ch in s0:
                a, b = s0[ch], s1[ch]
                out[ch] = tuple(h00 * a[k] + h10 * dt * m0[ch][k] + h01 * b[k] + h11 * dt * m1[ch][k]
                                for k in range(len(a)))
        else:
            e = _ease(ease, u)
            for ch in s0:
                a, b = s0[ch], s1[ch]
                out[ch] = tuple(a[k] + (b[k] - a[k]) * e for k in range(len(a)))
        return out


def interp_states(a, b, u):
    return {ch: tuple(a[ch][k] + (b[ch][k] - a[ch][k]) * u for k in range(len(a[ch]))) for ch in a}


# ---------------------------------------------------------------------------------------
# baking
# ---------------------------------------------------------------------------------------
def apply_pose(arm_ob, ql, hips_loc):
    for name, q in ql.items():
        pb = arm_ob.pose.bones[name]
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = q
    arm_ob.pose.bones["hips"].location = hips_loc


def reset_pose(arm_ob):
    for pb in arm_ob.pose.bones:
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
        pb.scale = (1, 1, 1)


def bake_action(arm_ob, name, frames, fps=FPS):
    """frames: list (index = frame number) of (ql, hips_loc).  Creates a LINEAR-keyed action."""
    prefs = bpy.context.preferences.edit
    prefs.keyframe_new_interpolation_type = "LINEAR"
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    if arm_ob.animation_data is None:
        arm_ob.animation_data_create()
    arm_ob.animation_data.action = act
    prev = {}
    for f, (ql, hips_loc) in enumerate(frames):
        for bone in BONE_ORDER:
            if bone == "root":
                continue
            q = ql[bone].copy()
            pq = prev.get(bone)
            if pq is not None and q.dot(pq) < 0:
                q.negate()
            prev[bone] = q
            pb = arm_ob.pose.bones[bone]
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = q
            pb.keyframe_insert("rotation_quaternion", frame=f, group=bone)
        pbh = arm_ob.pose.bones["hips"]
        pbh.location = hips_loc
        pbh.keyframe_insert("location", frame=f, group="hips")
    for fc in act.fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "LINEAR"
        fc.update()
    arm_ob.animation_data.action = None
    reset_pose(arm_ob)
    return act
