"""Clip catalogue for the fighter.  Pure data/choreography on top of fighter_dsl (see fighter_pose for the
channel semantics).  Time in seconds, 30 fps.  Left hand/foot are authored, the right side is the same
unless stated; `both()` / `both_feet()` mirror a spec onto the other side.

Authoring frame A: x = character left, y = forward, z = up.  Hand positions `p` are wrist positions
relative to the chest-posed rest shoulder joint (m, chest axes); f = finger direction, m = palm normal.
"""
import math

from fighter_dsl import (ClipDef, Hd, Fd, both, both_feet, merged, mirror_clipdef)

CLIPS = {}
ORDER = []


def clip(name, dur, loop, base, keys, contact=None, notes="", lag=None, **extra):
    cd = ClipDef(name, dur, loop, base, keys, contact, notes, lag, None, extra)
    CLIPS[name] = cd
    ORDER.append(name)
    return cd


def mirror(src, name, notes=None):
    cd = mirror_clipdef(CLIPS[src], name)
    if notes is not None:
        cd.notes = notes
    CLIPS[name] = cd
    ORDER.append(name)
    return cd


# ====================================================================================================
# BASE POSES (full specs; rest pose is the default underneath)
# ====================================================================================================
def _relaxed_hands():
    return both(Hd(p=(-0.06, 0.22, -0.40), f=(0.0, 1.0, -0.6), m=(-1, 0, 0.3), e=(0.7, -0.5, -1.0)))


B_IDLE = merged(
    dict(hp=(0, 0, -0.04), hips=(0, 0, 0), spine=(2, 0, 0), chest=(2, 0, 0), neck=(-1, 0, 0), head=(-3, 0, 0),
         sl=(-2, 0), sr=(-2, 0)),
    _relaxed_hands(),
    dict(fl=Fd(x=0.15, y=0.07, yaw=7), fr=Fd(x=-0.15, y=-0.06, yaw=-7)),
)

B_EARTH = merged(
    dict(hp=(0, 0.0, -0.20), hips=(0, 0, 0), spine=(0, 0, 0), chest=(0, 0, 0), neck=(-2, 0, 0), head=(-3, 0, 0),
         sl=(-2, 0), sr=(-2, 0)),
    both(Hd(p=(-0.04, 0.32, -0.30), f=(0, 1, 0), m=(0, 0, -1), e=(1.0, -0.3, -0.6))),
    dict(fl=Fd(x=0.34, y=0.03, yaw=30), fr=Fd(x=-0.34, y=0.03, yaw=-30)),
)

B_WATER = merged(
    dict(hp=(0, 0, -0.10), hips=(0, 0, -18), spine=(1, 0, -6), chest=(1, 0, -6), neck=(-1, 0, 10), head=(-2, 0, 18),
         sl=(-1, 0), sr=(-1, 0)),
    dict(hl=Hd(p=(0.04, 0.30, -0.06), f=(0.1, 1, 0.1), m=(-0.3, 0, -1), e=(0.9, -0.4, -0.5)),
         hr=Hd(p=(0.04, 0.16, -0.26), f=(0.0, 1, -0.3), m=(0.5, 0, -1), e=(-0.6, -0.5, -1.0))),
    dict(fl=Fd(x=0.18, y=0.20, yaw=10), fr=Fd(x=-0.16, y=-0.20, yaw=-40)),
)

B_FIRE = merged(
    dict(hp=(0, 0.02, -0.10), hips=(0, 0, -15), spine=(7, 0, -5), chest=(4, 0, -5), neck=(-5, 0, 11), head=(-4, 0, 14),
         sl=(0, 2), sr=(0, 2)),
    dict(hl=Hd(p=(0.0, 0.30, 0.00), f=(0, 0.6, 0.8), m=(-1, 0, 0), e=(0.3, 0.0, -1.0)),
         hr=Hd(p=(0.02, 0.20, -0.04), f=(0, 0.6, 0.8), m=(1, 0, 0), e=(-0.3, 0.0, -1.0))),
    dict(fl=Fd(x=0.13, y=0.30, yaw=-3, pitch=-8, pv=0.13), fr=Fd(x=-0.13, y=-0.24, yaw=-38, pitch=-10, pv=0.13)),
)

B_AIR = merged(
    dict(hp=(0, 0, -0.02), hips=(0, 0, 0), spine=(-2, 0, 0), chest=(-2, 0, 0), neck=(0, 0, 0), head=(-1, 0, 0),
         sl=(2, 0), sr=(2, 0)),
    both(Hd(p=(0.10, 0.26, -0.12), f=(0.15, 0.4, 1), m=(-0.2, 1, 0.0), e=(0.5, -0.5, -1.0))),
    dict(fl=Fd(x=0.12, y=0.08, yaw=6, pitch=-9, pv=0.13), fr=Fd(x=-0.12, y=-0.07, yaw=-6, pitch=-9, pv=0.13)),
)

BASES = {"idle": B_IDLE, "earth": B_EARTH, "water": B_WATER, "fire": B_FIRE, "air": B_AIR}


def F(n):
    """frame number -> seconds (30 fps)."""
    return n / 30.0


H_GUARD_L = Hd(p=(-0.08, 0.26, 0.02), f=(-0.25, 0.25, 1.0), m=(-0.4, -0.9, 0.0), e=(0.3, 0.3, -1.0))

B_GUARD = merged(
    dict(hp=(0, 0.0, -0.09), hips=(0, 0, -6), spine=(5, 0, 0), chest=(4, 0, -2), neck=(-5, 0, 2), head=(-4, 0, 4),
         sl=(3, 1), sr=(3, 1)),
    both(H_GUARD_L),
    dict(fl=Fd(x=0.15, y=0.14, yaw=5), fr=Fd(x=-0.15, y=-0.12, yaw=-12)),
)
BASES["guard"] = B_GUARD


# ====================================================================================================
# GAIT GENERATOR  (feet planted on the ground while stance, body moves; foot positions are in the root frame)
# ====================================================================================================
def _ss(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def gait(name, T, v, d, s, hip_z, lift, hs_pitch, to_pitch, lane=(0.13, 0.0, 0.0), lean=3.0, bob=0.02, sway=0.02,
         yaw_amp=5.0, arms="swing", foot_yaw=7.0, bob_phase=None, notes="", pv_heel=-0.075, pv_ball=0.135,
         arm_amp=0.13, lift_peak=0.5, stance_pitch_hold=(0.12, 0.45)):
    """Procedural in-place gait.  T cycle time (s), v ground speed (m/s), d travel direction (A-frame unit vector),
    s stance fraction.  Stance footprints move backwards at exactly v (no skating)."""
    N = int(round(T * 30))
    a = v * T * s / 2.0
    lane_x, ly_l, ly_r = lane
    keys = []
    bphi = (s / 2.0) if bob_phase is None else bob_phase
    for f in range(N):
        ph = f / N
        spec = {}
        for side in "LR":
            phi = ph if side == "L" else (ph + 0.5) % 1.0
            sx = 1 if side == "L" else -1
            yaw = foot_yaw * sx
            if phi < s:
                u = phi / s
                o = a * (1 - 2 * u)
                z = 0.0
                h0, h1 = stance_pitch_hold
                if u < h0:
                    pitch = hs_pitch * (1 - _ss(u / h0))
                elif u < h1:
                    pitch = 0.0
                else:
                    pitch = to_pitch * _ss((u - h1) / (1 - h1))
                if u < h0:
                    pv = pv_heel
                elif u < h1:
                    pv = pv_heel + (pv_ball - pv_heel) * _ss((u - h0) / (h1 - h0))
                else:
                    pv = pv_ball
            else:
                u = (phi - s) / (1 - s)
                o = -a + 2 * a * _ss(u)
                z = lift * math.sin(math.pi * min(max(u, 0.0), 1.0) ** (1.0 + (0.5 - lift_peak)))
                if u < 0.45:
                    pitch = to_pitch * (1 - _ss(u / 0.45))
                else:
                    pitch = hs_pitch * _ss((u - 0.45) / 0.55)
                pv = pv_ball + (pv_heel - pv_ball) * _ss((u - 0.3) / 0.5)
            lx = sx * lane_x
            ly = ly_l if side == "L" else ly_r
            fx, fy = lx + d[0] * o, ly + d[1] * o
            ya = math.radians(yaw)
            px, py = fx + pv * math.sin(ya), fy + pv * math.cos(ya)
            spec["f" + side.lower()] = Fd(x=px, y=py, lift=z, yaw=yaw, pitch=pitch, pv=pv)
        c = math.cos(2 * math.pi * ph)
        cs = math.cos(2 * math.pi * (ph - s / 2.0))
        perp = (d[1], -d[0])
        bz = hip_z + bob * math.cos(4 * math.pi * (ph - bphi))
        spec["hp"] = (sway * cs * perp[0], sway * cs * perp[1], bz)
        sh_yaw = 0.8 * yaw_amp * c
        hips_yaw = -yaw_amp * c
        spec["hips"] = (0, -2.5 * cs, hips_yaw)
        spec["spine"] = (lean * 0.5, 1.5 * cs, (sh_yaw - hips_yaw) * 0.5)
        spec["chest"] = (lean * 0.5, 1.5 * cs, (sh_yaw - hips_yaw) * 0.5)
        spec["neck"] = (-lean * 0.6, 0, -sh_yaw * 0.4)
        spec["head"] = (-lean * 0.4, 0, -sh_yaw * 0.5)
        if arms == "swing":
            hl = Hd(p=(0.10, 0.08 + arm_amp * math.cos(2 * math.pi * (ph - 0.5)), -0.46 + 0.035 * abs(math.cos(2 * math.pi * (ph - 0.5)))),
                    f=(0.05, 0.4, -1.0), m=(-1, 0, 0), e=(0.6, -0.6, -1.0))
            hr = Hd(p=(0.10, 0.08 + arm_amp * math.cos(2 * math.pi * ph), -0.46 + 0.035 * abs(math.cos(2 * math.pi * ph))),
                    f=(-0.05, 0.4, -1.0), m=(1, 0, 0), e=(-0.6, -0.6, -1.0))
            spec["hl"], spec["hr"] = hl, hr
        elif arms == "run":
            def rh(sgn, phase):
                th = 2 * math.pi * (ph - phase)
                return Hd(p=(-0.03 * sgn, 0.10 + 0.26 * math.cos(th), -0.30 + 0.10 * math.cos(th)),
                          f=(0.0, 1.0, -0.2), m=(-sgn, 0, 0.1), e=(sgn * 0.6, -1.0, -0.4))
            spec["hl"], spec["hr"] = rh(1, 0.5), rh(-1, 0.0)
        elif arms == "guard":
            g = 0.012 * math.sin(4 * math.pi * ph)
            hl = Hd(p=(-0.08, 0.26 + g, 0.02 - g), f=(-0.25, 0.25, 1.0), m=(-0.4, -0.9, 0.0), e=(0.3, 0.3, -1.0))
            hr = Hd(p=(-0.08, 0.26 - g, 0.02 + g), f=(0.25, 0.25, 1.0), m=(0.4, -0.9, 0.0), e=(-0.3, 0.3, -1.0))
            spec["hl"], spec["hr"] = hl, hr
        keys.append((f / 30.0, spec, "lin"))
    base = dict(keys[0][1])
    notes = (notes + " " if notes else "") + (
        f"In-place loop for ground speed {v:.1f} m/s: stride {v * T:.2f} m per cycle (two steps of {v * T / 2:.2f} m); "
        f"stance footprints move backwards at exactly {v:.1f} m/s, so feet do not skate when the root moves at that speed "
        f"(scale playback by actual_speed / {v:.1f}). Hips are lowered automatically wherever a planted leg would overstretch.")
    cd = ClipDef(name, T, True, {}, keys, None, notes, lag={"neck": 1.0, "head": 2.0}, extra=dict(auto_hips=True))
    cd.extra["stride"] = v * T
    cd.extra["speed"] = v
    cd.extra["dir"] = d
    return cd


def _reg(cd):
    CLIPS[cd.name] = cd
    ORDER.append(cd.name)
    return cd


# ====================================================================================================
# LOCOMOTION
# ====================================================================================================
clip("idle", 2.0, True, B_IDLE, [
    (0.0, {}, "sp"),
    (1.0, dict(chest=(3.2, 0, 0), spine=(3.0, 0, 0), neck=(-2, 0, 0), head=(-4.2, 0.6, 0.8), sl=(0.5, 0), sr=(0.5, 0),
               hp=(0.004, 0, -0.034)), "sp"),
], notes="Relaxed ready stance, breathing; feet planted.")

clip("stance_earth", 2.0, True, B_EARTH, [
    (0.0, {}, "sp"),
    (1.0, dict(hp=(0, 0, -0.213), chest=(1.5, 0, 0), spine=(0.5, 0, 0), sl=(0.5, 0), sr=(0.5, 0)), "sp"),
], notes="Wide low horse stance; slow heavy breathing, feet planted.")

clip("stance_water", 2.4, True, B_WATER, [
    (0.0, {}, "sp"),
    (0.6, dict(hp=(0.06, 0.03, -0.12), chest=(1, 3, -9), hips=(0, 3, -21)), "sp"),
    (1.2, dict(hp=(0.0, -0.03, -0.10), chest=(1, 0, -3), hips=(0, 0, -15)), "sp"),
    (1.8, dict(hp=(-0.06, 0.03, -0.12), chest=(1, -3, -6), hips=(0, -3, -18)), "sp"),
], notes="Fluid weight shift; feet planted.")

clip("stance_fire", 1.2, True, B_FIRE, [
    (0.0, {}, "sp"),
    (0.3, dict(hp=(0, 0.02, -0.12)), "sp"),
    (0.6, dict(), "sp"),
    (0.9, dict(hp=(0, 0.02, -0.12)), "sp"),
], notes="Forward pressure stance with light bounce.")

clip("stance_air", 2.0, True, B_AIR, [
    (0.0, {}, "sp"),
    (1.0, dict(hp=(0.02, 0, -0.03), head=(-1, 2, 2)), "sp"),
], notes="Light on the balls of the feet, open palms.")

_reg(gait("walk", 0.9, 1.4, (0, 1), 0.62, -0.05, 0.07, 20, -28, lean=3.0, bob=0.018, sway=0.022, yaw_amp=5.0,
          arms="swing", foot_yaw=6.0, arm_amp=0.13, notes="Natural forward walk, relaxed opposite arm swing."))
_reg(gait("run", 0.6, 5.5, (0, 1), 0.22, -0.10, 0.30, 5, -45, lean=12.0, bob=0.035, sway=0.015, yaw_amp=9.0,
          arms="run", foot_yaw=4.0, bob_phase=0.38, lane=(0.11, 0, 0), pv_heel=0.04, pv_ball=0.135,
          stance_pitch_hold=(0.15, 0.5), lift_peak=0.35, notes="Forward run, forefoot-ish contacts, 90-degree arm drive, 12 deg lean."))
_reg(gait("strafe_l", 0.9, 1.1, (1, 0), 0.52, -0.09, 0.07, 0, -12, lane=(0.15, 0.10, -0.10), lean=5.0, bob=0.012,
          sway=0.012, yaw_amp=2.5, arms="guard", foot_yaw=5.0, notes="Combat strafe to the character's left, guard up, facing forward."))
_reg(gait("strafe_r", 0.9, 1.1, (-1, 0), 0.52, -0.09, 0.07, 0, -12, lane=(0.15, 0.10, -0.10), lean=5.0, bob=0.012,
          sway=0.012, yaw_amp=2.5, arms="guard", foot_yaw=5.0, notes="Combat strafe to the character's right, guard up, facing forward."))
_reg(gait("walk_back", 0.9, 1.0, (0, -1), 0.60, -0.08, 0.06, -6, -22, lane=(0.15, 0.10, -0.08), lean=4.0, bob=0.015,
          sway=0.015, yaw_amp=3.0, arms="guard", foot_yaw=5.0, pv_heel=0.135, pv_ball=0.135, notes="Backpedal with guard up, toe-first contacts."))


# ====================================================================================================
# helpers for hand orientation presets (finger direction f, palm normal m), left-hand / chest axes
# ====================================================================================================
def H(p, o=None, e=None, sp="sh"):
    f, m = o if o else (None, None)
    return Hd(p=p, f=f, m=m, e=e, sp=sp)


PALM_FWD = ((0.0, 0.3, 1.0), (0.0, 1.0, -0.2))      # fingers up, palm facing forward
FIST_FWD = ((0.0, 1.0, 0.0), (-1.0, 0.0, 0.0))      # vertical fist, knuckles forward
PALM_UP = ((0.0, 1.0, 0.3), (0.0, 0.0, 1.0))
PALM_UPIN = ((0.0, 1.0, 0.3), (-0.8, 0.0, 0.6))      # palm up and turned in (halfway between palm-down and palm-up)
PALM_DOWN = ((0.0, 1.0, 0.0), (0.0, 0.0, -1.0))
PALM_IN = ((0.0, 1.0, 0.1), (-1.0, 0.0, 0.0))        # palm facing the body midline (left hand)


def mo(o):
    """mirror an orientation preset for the right hand."""
    f, m = o
    return ((-f[0], f[1], f[2]), (-m[0], m[1], m[2]))


# ====================================================================================================
# MOVEMENT ACTIONS (base: idle ready pose)
# ====================================================================================================
def evade(name, d, notes):
    dx, dy = d
    fl0, fr0 = (0.15, 0.07), (-0.15, -0.06)
    low_hands = both(H((0.10, 0.20, -0.30), PALM_IN, (0.8, -0.3, -1.0)))
    flight = dict(
        hp=(0.17 * dx, 0.15 * dy, -0.10),
        hips=(8 * dy - 3 * dx, 4 * dx, -6 * dx), spine=(6 * dy, 6 * dx, 0), chest=(8 * dy, 8 * dx, 0),
        neck=(-4 * dy, -5 * dx, 0), head=(-4 * dy, -5 * dx, 0),
        fl=Fd(x=fl0[0] + 0.06 * dx, y=fl0[1] + 0.07 * dy, lift=0.07, pitch=-18, pv=0.0),
        fr=Fd(x=fr0[0] + 0.06 * dx, y=fr0[1] + 0.07 * dy, lift=0.09, pitch=-18, pv=0.0),
    )
    cd = clip(name, 0.45, False, B_IDLE, [
        (0.0, {}, "lin"),
        (F(3), merged(dict(hp=(-0.05 * dx, -0.05 * dy, -0.17), hips=(10, 0, 0), spine=(8, -5 * dx, 0), chest=(6, -6 * dx, 0),
                           neck=(-8, 0, 0), head=(-6, 0, 0)), low_hands), "out"),
        (F(4), dict(hp=(0.02 * dx, 0.02 * dy, -0.14), fl=Fd(lift=0.035, pitch=-10, pv=0.0), fr=Fd(lift=0.035, pitch=-10, pv=0.0)), "lin"),
        (F(6), merged(flight, dict(
            hl=H((0.26 * dx + 0.04, 0.12, -0.12), PALM_IN, (0.9, -0.2, -0.6)),
            hr=mirror_h_spec((0.26 * dx + 0.04, 0.12, -0.12)))), "io"),
        (F(9), dict(hp=(0.12 * dx, 0.10 * dy, -0.20), hips=(10 * dy, 3 * dx, -3 * dx), spine=(10 * dy + 4, 6 * dx, 0),
                    chest=(8 * dy + 4, 8 * dx, 0), fl=Fd(x=fl0[0], y=fl0[1], lift=0, pitch=0, pv=0),
                    fr=Fd(x=fr0[0], y=fr0[1], lift=0, pitch=0, pv=0)), "in"),
        (F(13), dict(hp=(0.03 * dx, 0.02 * dy, -0.06), hips=(2, 0, 0), spine=(3, 1, 0), chest=(3, 2 * dx, 0)), "out"),
        (F(14), {"_base": True}, "io"),
    ], contact=F(6), notes=notes)
    return cd


def mirror_h_spec(p_left):
    """right-hand counterpart (mirror) of a left-hand low-guard spec."""
    return H((-p_left[0], p_left[1], p_left[2]), mo(PALM_IN), (-0.9, -0.2, -0.6))


evade("evade_l", (1, 0), "Quick low sidestep/hop to the character's left; feet leave the ground ~0.2-0.3 s, land on the stance spots. contact = apex of the dodge.")
evade("evade_r", (-1, 0), "Quick low sidestep/hop to the character's right. contact = apex of the dodge.")
evade("evade_back", (0, -1), "Backward hop with lean away, low silhouette. contact = apex of the dodge.")
evade("evade_fwd", (0, 1), "Forward dive-step, head low. contact = apex of the dodge.")

JUMP_ARMS_BACK = both(H((0.12, -0.22, -0.40), ((0, -0.3, -1.0), (-1, 0, 0)), (0.5, -1.0, -0.4)))
clip("jump", 0.3, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(3), merged(dict(hp=(0, 0, -0.21), hips=(10, 0, 0), spine=(14, 0, 0), chest=(10, 0, 0), neck=(-14, 0, 0), head=(-8, 0, 0)),
                  JUMP_ARMS_BACK), "out"),
    (F(5), merged(dict(hp=(0, 0.0, 0.06), hips=(-2, 0, 0), spine=(-5, 0, 0), chest=(-4, 0, 0), neck=(2, 0, 0), head=(4, 0, 0),
                       fl=Fd(pitch=-55, pv=0.135), fr=Fd(pitch=-55, pv=0.135)),
                  both(H((0.16, 0.24, 0.10), ((0, 1, 0.6), (-1, 0, 0)), (0.7, -0.4, -0.2)))), "in"),
    (F(9), dict(hp=(0, 0.0, 0.10), hips=(0, 0, 0), spine=(-3, 0, 0), chest=(-3, 0, 0), neck=(4, 0, 0), head=(4, 0, 0),
                fl=Fd(x=0.12, y=0.10, lift=0.20, pitch=-40, pv=0.135), fr=Fd(x=-0.12, y=-0.04, lift=0.16, pitch=-45, pv=0.135)), "out"),
], contact=F(5), notes="Crouch, explosive extension, feet leave ground at contact (0.17 s). Ends in rising pose; blend to fall/glide.")

FALL_POSE = merged(
    dict(hp=(0, 0, 0.05), hips=(6, 0, 0), spine=(4, 0, 0), chest=(2, 0, 0), neck=(-6, 0, 0), head=(-6, 0, 0), sl=(6, 0), sr=(6, 0)),
    both(H((0.30, 0.10, 0.10), ((0.5, 0.4, 0.8), (0, 1, 0.2)), (0.6, -0.5, -0.4))),
    dict(fl=Fd(x=0.13, y=0.20, lift=0.22, pitch=-30, pv=0.0, yaw=4), fr=Fd(x=-0.13, y=-0.14, lift=0.14, pitch=-35, pv=0.0, yaw=-4)),
)
BASES["fall"] = FALL_POSE
clip("fall", 0.8, True, FALL_POSE, [
    (0.0, {}, "sp"),
    (0.4, dict(hp=(0, 0, 0.065), chest=(4, 0, 0), sl=(8, 0), sr=(8, 0),
               hl=Hd(p=(0.30, 0.10, 0.17)), hr=Hd(p=(-0.30, 0.10, 0.17)),
               fl=Fd(lift=0.15, y=0.12), fr=Fd(lift=0.21, y=-0.06)), "sp"),
], notes="Airborne descent: arms up/out for balance, legs staggered and trailing. Loops.")

clip("land", 0.3, False, FALL_POSE, [
    (0.0, {}, "lin"),
    (F(2), dict(hp=(0, 0, 0.0), hips=(8, 0, 0), spine=(8, 0, 0), chest=(6, 0, 0),
                fl=Fd(x=0.15, y=0.07, lift=0, pitch=0, pv=0, yaw=7), fr=Fd(x=-0.15, y=-0.06, lift=0, pitch=0, pv=0, yaw=-7),
                hl=H((0.12, 0.26, -0.18), PALM_IN, (0.8, -0.3, -0.8)), hr=Hd(p=(-0.12, 0.26, -0.18), f=(0, 1, 0.1), m=(1, 0, 0), e=(-0.8, -0.3, -0.8))), "in"),
    (F(4), dict(hp=(0, 0.02, -0.25), hips=(12, 0, 0), spine=(20, 0, 0), chest=(10, 0, 0), neck=(-18, 0, 0), head=(-10, 0, 0)), "out"),
    (F(9), {"_base": True}, "io"),
], contact=F(2), notes="Touchdown at contact (0.07 s), knees absorb, rise to ready. Feet planted from contact on.")

GLIDE = merged(
    dict(hp=(0, -0.02, 0.14), hips=(18, 0, 0), spine=(12, 0, 0), chest=(8, 0, 0), neck=(-26, 0, 0), head=(-16, 0, 0), sl=(6, 0), sr=(6, 0)),
    both(H((0.52, 0.02, 0.04), ((1, 0, 0.05), (0, 0, -1)), (0.2, -1.0, -0.3))),
    dict(fl=Fd(x=0.10, y=-0.52, lift=0.64, pitch=-55, pv=0.0, yaw=0, kup=-0.6), fr=Fd(x=-0.10, y=-0.56, lift=0.60, pitch=-60, pv=0.0, yaw=0, kup=-0.6)),
)
BASES["glide"] = GLIDE
clip("glide", 1.6, True, GLIDE, [
    (0.0, {}, "sp"),
    (0.4, dict(hl=Hd(p=(0.52, 0.02, 0.09)), hr=Hd(p=(-0.52, 0.02, 0.02)), hips=(19, 3, 0), chest=(9, 0, 0)), "sp"),
    (0.8, dict(hp=(0, -0.02, 0.15)), "sp"),
    (1.2, dict(hl=Hd(p=(0.52, 0.02, 0.02)), hr=Hd(p=(-0.52, 0.02, 0.09)), hips=(19, -3, 0), chest=(9, 0, 0)), "sp"),
], notes="Arms spread, body pitched ~35 deg forward, legs trailing; gentle wing flutter. Loops.")

clip("air_dash", 0.3, False, FALL_POSE, [
    (0.0, {}, "lin"),
    (F(2), dict(hp=(0, -0.02, 0.05), hips=(18, 0, 0), spine=(14, 0, 0), chest=(8, 0, 0), neck=(-10, 0, 0),
                fl=Fd(lift=0.28, y=0.10, pitch=-20), fr=Fd(lift=0.25, y=-0.05),
                hl=H((0.15, -0.25, -0.25), PALM_DOWN, (0.5, -1, -0.4)), hr=Hd(p=(-0.15, -0.25, -0.25), f=(0, 1, 0), m=(0, 0, -1), e=(-0.5, -1, -0.4))), "in"),
    (F(5), dict(hp=(0, 0.05, 0.12), hips=(62, 0, 0), spine=(14, 0, 0), chest=(10, 0, 0), neck=(-42, 0, 0), head=(-28, 0, 0),
                hl=Hd(p=(0.10, 0.30, 0.45), f=(0, 0.2, 1), m=(-1, 0, 0), e=(0.8, -0.4, -0.2), sp="sh"),
                hr=Hd(p=(-0.10, 0.30, 0.45), f=(0, 0.2, 1), m=(1, 0, 0), e=(-0.8, -0.4, -0.2), sp="sh"),
                fl=Fd(x=0.09, y=-0.74, lift=0.78, pitch=-60, pv=0.0, yaw=0, kup=-0.6),
                fr=Fd(x=-0.09, y=-0.78, lift=0.74, pitch=-60, pv=0.0, yaw=0, kup=-0.6)), "ovs"),
    (F(9), dict(hp=(0, 0.05, 0.13), hips=(66, 0, 0)), "out"),
], contact=F(4), notes="Burst: tuck then streamline forward (superman). Hold last pose or blend to glide/fall.")

# ====================================================================================================
# DEFENSE
# ====================================================================================================
clip("guard", 1.6, True, B_GUARD, [
    (0.0, {}, "sp"),
    (0.8, dict(hp=(0, 0, -0.095), chest=(5.0, 0, -2), spine=(5.5, 0, 0),
               hl=Hd(p=(-0.075, 0.268, 0.028)), hr=Hd(p=(0.075, 0.268, 0.028))), "sp"),
], notes="Held block: forearms up in front of face/chest, knees bent; slight tension breathing.")

clip("deflect", 0.35, False, B_GUARD, [
    (0.0, {}, "lin"),
    (F(1), dict(hips=(0, 0, 4), chest=(4, 0, 6), hl=Hd(p=(-0.03, 0.20, 0.04))), "out"),
    (F(2), dict(hp=(-0.04, 0.03, -0.11), hips=(0, 0, -24), spine=(5, 0, -6), chest=(4, 0, -12), neck=(-5, 0, 24), head=(-4, 0, 14),
                hl=H((-0.26, 0.36, -0.04), ((0, 0.6, 1.0), (-1, 0.3, 0)), (0.2, 0.3, -1)),
                hr=H((0.05, 0.22, 0.00), ((0, 0.3, 1.0), (0.5, -0.8, 0)), (-0.3, 0.2, -1))), "in3"),
    (F(4), dict(hips=(0, 0, -30), chest=(4, 0, -14), hl=Hd(p=(-0.34, 0.34, -0.07))), "out"),
    (F(7), dict(hips=(0, 0, -14), chest=(4, 0, -6), hl=Hd(p=(-0.18, 0.28, -0.0))), "io"),
    (F(11), {"_base": True}, "io"),
], contact=F(2), notes="Sharp cross-body parry with hip turn; contact = hand meets the strike.")

clip("earth_wall", 0.4, False, B_EARTH, [
    (0.0, {}, "lin"),
    (F(2), dict(hp=(0, 0, -0.215), spine=(5, 0, 0), chest=(6, 0, 0),
                fr=Fd(lift=0.10, pitch=-8, pv=0.0),
                hl=H((0.08, 0.20, -0.42), PALM_UPIN, (0.9, -0.2, -0.8)), hr=Hd(p=(-0.08, 0.20, -0.42), f=(0, 1, 0.3), m=(0.8, 0, 0.6), e=(-0.9, -0.2, -0.8))), "out"),
    (F(4), dict(fr=Fd(lift=0.20, pitch=-14), hp=(0.0, 0, -0.18)), "io"),
    (F(5), dict(hp=(0, 0.02, -0.26), fr=Fd(lift=0.0, pitch=0, pv=0.0, x=-0.34, y=0.03), spine=(8, 0, 0), chest=(8, 0, 0)), "in3"),
    (F(8), dict(hp=(0, 0.0, -0.20), spine=(-2, 0, 0), chest=(-2, 0, 0),
                hl=H((0.06, 0.34, -0.10), PALM_UP, (0.9, -0.2, -0.8)), hr=Hd(p=(-0.06, 0.34, -0.10), f=(0, 1, 0.3), m=(0, 0, 1), e=(-0.9, -0.2, -0.8))), "out"),
    (F(10), dict(hp=(0, 0.0, -0.16), spine=(-5, 0, 0), chest=(-6, 0, 0), neck=(2, 0, 0),
                 hl=H((0.12, 0.30, 0.34), PALM_FWD, (0.9, 0.0, -0.8)), hr=Hd(p=(-0.12, 0.30, 0.34), f=(0, 0.3, 1), m=(0, 1, -0.2), e=(-0.9, 0.0, -0.8))), "ovs"),
    (F(12), {"_base": True}, "io"),
], contact=F(5), notes="Stomp (contact 0.17 s) then both palms rise to push up the wall. Starts/ends in earth stance.")

# ====================================================================================================
# REACTIONS (base: idle ready pose)
# ====================================================================================================
THROWN = both(H((0.26, 0.02, -0.28), ((0, 0.5, -1), (-1, 0, 0)), (0.6, -0.8, -0.5)))

clip("hit_front", 0.35, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(2), merged(dict(hp=(0, -0.07, -0.07), hips=(-6, 0, 3), spine=(-8, 0, 2), chest=(-14, 0, 4), neck=(-14, 0, 0), head=(-14, 0, 0),
                       sl=(6, -3), sr=(6, -3)), THROWN), "in3"),
    (F(4), dict(hp=(0, -0.09, -0.09), hips=(-8, 0, 4), chest=(-17, 0, 5), neck=(-8, 0, 0), head=(-6, 0, 0)), "out"),
    (F(7), dict(hp=(0, -0.03, -0.06), hips=(-2, 0, 1), spine=(0, 0, 0), chest=(-6, 0, 2), neck=(-3, 0, 0), head=(-4, 0, 0),
                sl=(2, 0), sr=(2, 0)), "io"),
    (F(11), {"_base": True}, "io"),
], contact=F(2), notes="Hit from the front: chest and head snap back, arms jerk up; feet planted. contact = impact frame.")

clip("hit_back", 0.35, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(2), dict(hp=(0, 0.08, -0.06), hips=(8, 0, -3), spine=(-6, 0, 0), chest=(-12, 0, -4), neck=(-18, 0, 0), head=(-16, 0, 0),
                sl=(5, 3), sr=(5, 3),
                hl=H((0.24, -0.26, -0.12), ((0.2, -0.5, -1), (-1, 0, 0)), (0.6, -1, -0.2)),
                hr=Hd(p=(-0.24, -0.26, -0.12), f=(-0.2, -0.5, -1), m=(1, 0, 0), e=(-0.6, -1, -0.2))), "in3"),
    (F(4), dict(hp=(0, 0.10, -0.08), hips=(10, 0, -4), chest=(-14, 0, -5), neck=(-12, 0, 0), head=(-8, 0, 0)), "out"),
    (F(7), dict(hp=(0, 0.04, -0.06), hips=(4, 0, -1), chest=(-5, 0, -2), neck=(-4, 0, 0), head=(-4, 0, 0), sl=(2, 1), sr=(2, 1)), "io"),
    (F(11), {"_base": True}, "io"),
], contact=F(2), notes="Hit from behind: arches back, head thrown up, arms swing back; feet planted.")

clip("hit_heavy", 0.6, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(3), dict(hp=(0, -0.10, -0.10), hips=(-10, 0, 4), spine=(-6, 0, 0), chest=(-18, 0, 4), neck=(-20, 0, 0), head=(-18, 0, 0),
                sl=(8, -2), sr=(8, -2), fl=Fd(pitch=0, lift=0.0),
                **both(H((0.40, -0.02, 0.12), ((1, 0.2, 0.3), (0, 1, 0)), (0.5, -0.8, 0)))), "in3"),
    (F(6), dict(hp=(0, -0.18, -0.13), hips=(-14, 0, 6), spine=(-6, 0, 0), chest=(-22, 0, 6), neck=(-12, 0, 0), head=(-10, 0, 0),
                fl=Fd(lift=0.10, pitch=25, pv=0.0, y=0.10),
                **both(H((0.38, -0.05, 0.30), ((0.6, 0.2, 1), (0, 1, 0.2)), (0.5, -0.8, 0)))), "out"),
    (F(10), dict(hp=(0, -0.04, -0.20), hips=(8, 0, 0), spine=(14, 0, 0), chest=(14, 0, 0), neck=(-8, 0, 0), head=(-6, 0, 0),
                 fl=Fd(lift=0.0, pitch=0, pv=0.0, y=0.07),
                 **both(H((0.16, 0.28, -0.26), PALM_DOWN, (0.8, -0.3, -0.8)))), "io"),
    (F(14), dict(hp=(0, 0.0, -0.12), hips=(3, 0, 0), spine=(7, 0, 0), chest=(5, 0, 0), neck=(-3, 0, 0), head=(-3, 0, 0),
                 **both(H((0.10, 0.22, -0.38), PALM_IN, (0.8, -0.4, -1)))), "io"),
    (F(18), {"_base": True}, "io"),
], contact=F(3), notes="Big balance loss: thrown back, front foot leaves ground, arms fling wide, stumbles forward and recovers.")

STAGGER_L = H((0.40, 0.12, 0.04), ((1, 0.3, 0.0), (0, 0, -1)), (0.6, -0.4, -0.5))
clip("stagger", 0.5, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(3), dict(hp=(0.07, -0.04, -0.09), hips=(0, 5, 8), spine=(6, -8, -6), chest=(8, -6, -6), neck=(-6, 6, 10), head=(-6, 6, 10),
                hl=STAGGER_L, hr=Hd(p=(-0.04, 0.22, -0.30), f=(0, 1, -0.3), m=(1, 0, 0), e=(-0.6, -0.5, -1)),
                fr=Fd(lift=0.07, pitch=-10, pv=0.0)), "out"),
    (F(6), dict(hp=(-0.05, -0.02, -0.12), hips=(0, -5, -6), spine=(10, 8, 4), chest=(10, 8, 4), neck=(-8, -6, -8), head=(-8, -6, -8),
                fr=Fd(lift=0.0, pitch=0, pv=0.0),
                hr=Hd(p=(-0.40, 0.12, 0.04), f=(-1, 0.3, 0.0), m=(0, 0, -1), e=(-0.6, -0.4, -0.5)),
                hl=Hd(p=(0.04, 0.22, -0.30), f=(0, 1, -0.3), m=(-1, 0, 0), e=(0.6, -0.5, -1))), "io"),
    (F(10), dict(hp=(0.02, 0.0, -0.08), hips=(0, 2, 2), spine=(5, -3, -2), chest=(4, -3, -2), neck=(-3, 2, 3), head=(-3, 2, 3),
                 hr=Hd(p=(-0.16, 0.22, -0.24), f=(0, 1, -0.3), m=(1, 0, 0), e=(-0.6, -0.5, -1)),
                 hl=Hd(p=(0.16, 0.22, -0.24), f=(0, 1, -0.3), m=(-1, 0, 0), e=(0.6, -0.5, -1))), "io"),
    (F(15), {"_base": True}, "io"),
], contact=F(3), notes="Off-balance wobble in place: arms out, knees give, foot re-plants. Light reaction.")

LYING = merged(
    dict(hp=(0, -0.30, -0.78), hips=(-90, 0, 0), spine=(0, 0, 0), chest=(0, 0, 0), neck=(8, 0, 0), head=(8, 0, 0), sl=(0, 0), sr=(0, 0)),
    both(H((0.22, -0.09, -0.50), ((0, 1, 0.1), (-1, 0, 0)), (0.8, -0.4, -0.5))),
    dict(fl=Fd(x=0.15, y=0.52, lift=0.0, yaw=12, pitch=70, pv=-0.075, kup=1.5),
         fr=Fd(x=-0.15, y=0.52, lift=0.0, yaw=-12, pitch=70, pv=-0.075, kup=1.5)),
)
BASES["lying"] = LYING

clip("knockdown", 0.7, False, B_IDLE, [
    (0.0, {}, "lin"),
    (F(2), merged(dict(hp=(0, -0.07, -0.07), hips=(-6, 0, 3), spine=(-6, 0, 0), chest=(-12, 0, 4), neck=(-14, 0, 0), head=(-12, 0, 0)),
                  both(H((0.28, 0.0, 0.0), ((0, 0.5, -0.6), (-1, 0, 0)), (0.6, -0.8, -0.5)))), "in3"),
    (F(6), merged(dict(hp=(0, -0.20, -0.30), hips=(-30, 0, 4), spine=(-4, 0, 0), chest=(-8, 0, 4), neck=(-14, 0, 0), head=(-12, 0, 0),
                       fl=Fd(lift=0.10, y=0.25, pitch=-20, pv=0.0), fr=Fd(pitch=-15, pv=0.135)),
                  both(H((0.40, -0.05, 0.12), ((1, 0.2, 0.3), (0, 1, 0)), (0.5, -0.8, 0)))), "in"),
    (F(11), merged(dict(hp=(0, -0.30, -0.55), hips=(-62, 0, 3), spine=(-3, 0, 0), chest=(-4, 0, 0), neck=(0, 0, 0), head=(0, 0, 0),
                        fl=Fd(lift=0.38, y=0.42, pitch=-10, pv=0.0, kup=1.0), fr=Fd(lift=0.22, y=0.34, pitch=-10, pv=0.0, kup=1.0)),
                   both(H((0.36, -0.10, 0.10), ((1, 0.2, 0.3), (0, 1, 0)), (0.5, -0.8, 0)))), "in"),
    (F(15), dict(hp=(0, -0.31, -0.75), hips=(-88, 0, 0), fl=Fd(lift=0.22, y=0.42, pitch=20, pv=-0.075, kup=1.2),
                 fr=Fd(lift=0.15, y=0.42, pitch=20, pv=-0.075, kup=1.2)), "in"),
    (F(17), dict(hp=(0, -0.31, -0.78), hips=(-90, 0, 0), chest=(6, 0, 0), neck=(6, 0, 0), head=(4, 0, 0),
                 fl=Fd(lift=0.04, pitch=60, y=0.52, pv=-0.075, kup=1.5), fr=Fd(lift=0.02, pitch=60, y=0.52, pv=-0.075, kup=1.5),
                 **both(H((0.28, -0.07, -0.46), ((0, 1, 0.1), (-1, 0, 0)), (0.8, -0.4, -0.5)))), "in"),
    (F(21), {**{"_base": True}, **LYING}, "out"),
], contact=F(2), notes="Struck, tumbles backward, slams the ground (~0.55 s) and settles lying on the back; ends on LYING pose. contact = first impact. Body extends ~0.9 m behind and ~0.7 m ahead of root.")

clip("getup", 0.8, False, LYING, [
    (0.0, {}, "lin"),
    (F(3), dict(neck=(20, 0, 0), head=(16, 0, 0), chest=(5, 0, 0),
                fl=Fd(x=0.15, y=0.30, lift=0.05, yaw=7, pitch=0, pv=0.0, kup=1.5), fr=Fd(x=-0.15, y=0.28, lift=0.05, yaw=-7, pitch=0, pv=0.0, kup=1.5),
                **both(H((0.30, -0.08, -0.34), ((0, 1, 0.1), (-1, 0, 0)), (0.8, -0.4, -0.5)))), "io"),
    (F(5), dict(neck=(28, 0, 0), head=(22, 0, 0), chest=(8, 0, 0),
                fl=Fd(x=0.15, y=0.07, lift=0.0, yaw=7, pitch=0, pv=0.0, kup=1.5), fr=Fd(x=-0.15, y=-0.06, lift=0.0, yaw=-7, pitch=0, pv=0.0, kup=1.5)), "io"),
    (F(9), dict(hp=(0, -0.28, -0.68), hips=(-72, 0, 0), spine=(30, 0, 0), chest=(25, 0, 0), neck=(-10, 0, 0), head=(-10, 0, 0),
                fl=Fd(kup=1.0), fr=Fd(kup=1.0),
                **both(H((0.16, 0.30, -0.16), PALM_DOWN, (0.9, -0.2, -0.8)))), "io"),
    (F(14), dict(hp=(0, -0.04, -0.40), hips=(-8, 0, 0), spine=(22, 0, 0), chest=(12, 0, 0), neck=(-18, 0, 0), head=(-8, 0, 0),
                 fl=Fd(kup=0.0), fr=Fd(kup=0.0),
                 **both(H((0.10, 0.30, -0.18), PALM_DOWN, (0.9, -0.2, -0.8)))), "io"),
    (F(19), dict(hp=(0, 0.0, -0.16), hips=(3, 0, 0), spine=(8, 0, 0), chest=(6, 0, 0), neck=(-6, 0, 0), head=(-4, 0, 0),
                 **both(H((0.06, 0.26, -0.30), PALM_IN, (0.8, -0.4, -1)))), "io"),
    (F(24), dict(B_IDLE), "io"),
], contact=None, notes="Lying on back -> heels pulled in -> sit-up -> squat -> stand into the idle ready pose. Feet planted on the idle spots from 0.17 s. contact = null.")

# ====================================================================================================
# EARTH  (grounded, low, heavy)  base: wide earth stance
# ====================================================================================================
EH_HOLD = ((0.0, 1.0, 0.1), (-1.0, 0.0, 0.0))

clip("earth_lift", 0.4, False, B_EARTH, [
    (0.0, {}, "lin"),
    (F(2), merged(dict(hp=(0, 0, -0.23), spine=(8, 0, 0), chest=(6, 0, 0), fr=Fd(lift=0.09, pitch=-6, pv=0.0)),
                  both(H((0.06, 0.16, -0.50), PALM_UPIN, (0.9, -0.3, -0.8)))), "out"),
    (F(3), dict(hp=(0, 0, -0.25), spine=(10, 0, 0), chest=(8, 0, 0), fr=Fd(lift=0.0, pitch=0, pv=0.0)), "in3"),
    (F(6), merged(dict(hp=(0, 0.01, -0.17), spine=(0, 0, 0), chest=(-2, 0, 0), neck=(0, 0, 0), head=(-4, 0, 0)),
                  both(H((0.04, 0.34, -0.12), PALM_UP, (0.9, -0.3, -0.8)))), "out"),
    (F(8), merged(dict(hp=(0, 0.01, -0.14), spine=(-3, 0, 0), chest=(-6, 0, 0)), both(H((0.04, 0.38, 0.02), PALM_UP, (0.9, -0.3, -0.8)))), "ovs"),
    (F(12), {"_base": True}, "io"),
], contact=F(6), notes="Stomp (0.1 s), hands drive up pulling a stone: contact = stone pops (0.2 s). Starts/ends in earth stance.")

clip("earth_throw", 0.45, False, B_EARTH, [
    (0.0, {}, "lin"),
    (F(3), dict(hp=(0, -0.04, -0.21), hips=(0, 0, -14), spine=(0, 0, -6), chest=(2, 0, -8), neck=(0, 0, 12), head=(0, 0, 10),
                hr=Hd(p=(0.14, 0.10, -0.30), f=(0, 1, 0), m=(1, 0, 0), e=(-0.8, -0.8, -0.4)),
                hl=Hd(p=(-0.05, 0.20, -0.14), f=(0, 0.7, 0.7), m=(-1, 0, 0), e=(0.6, -0.4, -0.8))), "out"),
    (F(5), dict(hp=(0, 0.10, -0.23), hips=(2, 0, 12), spine=(4, 0, 4), chest=(8, 0, 8), neck=(-4, 0, -12), head=(-4, 0, -10),
                sr=(0, 8), hr=Hd(p=(0.17, 0.56, 0.0), f=(0, 1, 0), m=(1, 0, 0), e=(-0.4, -0.8, -0.5))), "in3"),
    (F(7), dict(hp=(0, 0.12, -0.24), hips=(3, 0, 14), chest=(9, 0, 10), hr=Hd(p=(0.17, 0.58, 0.01))), "out"),
    (F(10), dict(hp=(0, 0.04, -0.21), hips=(0, 0, 5), spine=(2, 0, 2), chest=(4, 0, 3), sr=(0, 2),
                 hr=Hd(p=(0.13, 0.34, -0.08)), neck=(-2, 0, -4), head=(-2, 0, -4)), "io"),
    (F(14), {"_base": True}, "io"),
], contact=F(5), notes="Driving straight punch/push with the right hand; hips drive forward, feet planted. contact = fist lands (0.17 s).")

clip("earth_heavy", 0.8, False, B_EARTH, [
    (0.0, {}, "lin"),
    (F(5), merged(dict(hp=(0, 0, -0.27), hips=(8, 0, 0), spine=(12, 0, 0), chest=(12, 0, 0), neck=(-8, 0, 0)),
                  both(H((0.08, 0.18, -0.55), PALM_UPIN, (0.9, -0.3, -0.8)))), "io"),
    (F(10), merged(dict(hp=(0, -0.02, -0.08), hips=(-2, 0, 0), spine=(-8, 0, 0), chest=(-14, 0, 0), neck=(6, 0, 0), head=(0, 0, 0), sl=(6, 0), sr=(6, 0)),
                   both(H((0.06, 0.20, 0.40), ((0, 0.3, 1), (0, 1, -0.3)), (0.9, 0.0, -0.5)))), "io"),
    (F(12), dict(chest=(-16, 0, 0), hp=(0, -0.03, -0.07)), "out"),
    (F(14), merged(dict(hp=(0, 0.10, -0.29), hips=(6, 0, 0), spine=(16, 0, 0), chest=(20, 0, 0), neck=(-14, 0, 0), head=(-8, 0, 0), sl=(0, 6), sr=(0, 6)),
                   both(H((0.04, 0.50, 0.0), PALM_FWD, (0.9, -0.2, -0.8)))), "in3"),
    (F(17), merged(dict(hp=(0, 0.11, -0.28), chest=(22, 0, 0)), both(H((0.04, 0.52, -0.10), PALM_FWD, (0.9, -0.2, -0.8)))), "out"),
    (F(24), {"_base": True}, "io"),
], contact=F(14), notes="Scoop low, heave overhead, drive forward-down; contact at the end of the heave (0.47 s). Starts/ends in earth stance.")

EARTH_HOLD = merged(
    B_EARTH,
    dict(hp=(0, 0.0, -0.22), spine=(4, 0, 0), chest=(4, 0, 0), neck=(-4, 0, 0), head=(-6, 0, 0), sl=(5, 0), sr=(5, 0)),
    both(H((-0.04, 0.30, -0.02), EH_HOLD, (0.9, -0.1, -0.8))),
)
BASES["earth_hold"] = EARTH_HOLD
clip("earth_hold", 1.2, True, EARTH_HOLD, [
    (0.0, {}, "sp"),
    (F(9), dict(hp=(0, 0.0, -0.228), chest=(5.5, 0, 0), hl=Hd(p=(-0.044, 0.304, -0.016)), hr=Hd(p=(0.036, 0.296, -0.024)), sl=(6, 0), sr=(6, 0)), "sp"),
    (F(18), dict(hp=(0, 0.0, -0.215), chest=(4, 0, 0), hl=Hd(p=(-0.036, 0.296, -0.024)), hr=Hd(p=(0.044, 0.304, -0.016))), "sp"),
    (F(27), dict(hp=(0, 0.0, -0.228), chest=(5.5, 0, 0), hl=Hd(p=(-0.044, 0.304, -0.016)), hr=Hd(p=(0.036, 0.296, -0.024)), sl=(6, 0), sr=(6, 0)), "sp"),
], notes="Both hands cupped around an invisible mass at chest height (mass centre ~ (0, 0.30 fwd, 1.2 m)); strained tremble. Loops.")

# ====================================================================================================
# WATER  (continuous arcs)  base: water stance
# ====================================================================================================
WL = ((0.2, 1.0, 0.2), (-0.3, 0.2, -1.0))

clip("water_draw", 0.5, False, B_WATER, [
    (0.0, {}, "sp"),
    (F(3), dict(hp=(0.02, 0.02, -0.17), hips=(8, 0, -22), spine=(6, 4, -8), chest=(6, 6, -8), neck=(-4, 0, 8), head=(-4, 0, 8),
                hl=H((0.20, 0.22, -0.52), ((0.2, 1, 0.3), (-0.6, 0, 0.5)), (0.9, -0.5, -0.6)),
                hr=Hd(p=(-0.06, 0.14, -0.54), f=(-0.1, 1, 0.3), m=(0.4, 0, 0.6), e=(-0.7, -0.5, -1.0))), "sp"),
    (F(7), dict(hp=(0.0, 0.03, -0.12), hips=(2, 0, -18), spine=(1, 0, -6), chest=(0, -2, -6), neck=(-2, 0, 10), head=(-2, 0, 14),
                hl=Hd(p=(0.10, 0.34, -0.22), f=(0.1, 1, 0.4), m=(-0.2, 0.2, 1)),
                hr=Hd(p=(0.06, 0.30, -0.18), f=(0, 1, 0.4), m=(0.2, 0.2, 1))), "sp"),
    (F(11), dict(hp=(0.0, 0.05, -0.05), hips=(-2, 0, -16), spine=(-4, 0, -4), chest=(-8, -3, -4), neck=(-4, 0, 12), head=(-8, 0, 16), sl=(6, 0), sr=(6, 0),
                 hl=Hd(p=(0.00, 0.42, 0.12), f=(0.2, 0.6, 1), m=(-0.4, 1, -0.2)),
                 hr=Hd(p=(0.08, 0.38, 0.06), f=(-0.1, 0.6, 1), m=(0.4, 1, -0.2))), "sp"),
    (F(13), dict(hp=(0.0, 0.04, -0.06), spine=(-5, 0, -4), chest=(-10, -3, -4),
                 hl=Hd(p=(0.10, 0.40, 0.20)), hr=Hd(p=(0.0, 0.40, 0.14))), "sp"),
    (F(15), {"_base": True}, "sp"),
], contact=F(11), notes="Flowing S-curve pull from low-and-back to high-and-forward; contact = water released at the top (0.37 s).")


def _water_hold_keys():
    keys = []
    for k in range(8):
        th = 2 * math.pi * k / 8
        c, sn = math.cos(th), math.sin(th)
        # left hand circle in the chest x-z plane; right hand the mirror image half a turn later
        c2, s2 = math.cos(th + math.pi), math.sin(th + math.pi)
        hl = Hd(p=(0.02 + 0.11 * c, 0.34 + 0.04 * sn, -0.02 + 0.11 * sn), f=(0.15, 1, 0.2 * c2), m=(-1, 0, 0.3 * sn), e=(0.9, -0.3, -0.5))
        hr = Hd(p=(-(0.02 + 0.11 * c2), 0.34 + 0.04 * s2, -0.02 + 0.11 * s2), f=(-0.15, 1, 0.2 * c), m=(1, 0, 0.3 * s2), e=(-0.9, -0.3, -0.5))
        keys.append((F(6 * k), dict(hl=hl, hr=hr, hp=(0.03 * math.sin(th), 0.0, -0.11 - 0.012 * math.cos(2 * th)),
                                    hips=(0, 2.0 * math.sin(th), -18 + 4 * math.sin(th)), chest=(1, 2.5 * math.sin(th - 0.6), -6 + 3 * math.sin(th)),
                                    head=(-2, 0, 18 - 3 * math.sin(th))), "sp"))
    return keys


clip("water_hold", 1.6, True, B_WATER, _water_hold_keys(),
     notes="Both hands circling a sphere of water in front of the chest (opposite phase), weight swaying; planted feet. Loops.")

clip("water_whip", 0.5, False, B_WATER, [
    (0.0, {}, "sp"),
    (F(3), dict(hp=(-0.02, -0.02, -0.15), hips=(2, 0, -30), spine=(2, 0, -8), chest=(2, 0, -12), neck=(0, 0, 18), head=(0, 0, 20),
                hr=Hd(p=(-0.46, -0.18, -0.10), f=(-1, -0.3, 0), m=(0, 0, -1), e=(-0.3, -1, -0.4)),
                hl=Hd(p=(0.02, 0.26, -0.10), f=(0.1, 1, 0.3), m=(-0.5, 0, -1), e=(0.9, -0.3, -0.5))), "sp"),
    (F(5), dict(hp=(0, 0.03, -0.14), hips=(2, 0, -6), spine=(4, 0, 2), chest=(4, 0, 0), neck=(0, 0, 4), head=(0, 0, 6),
                hr=Hd(p=(-0.34, 0.40, -0.04), f=(-0.5, 1, 0), m=(0, 0, -1))), "sp"),
    (F(7), dict(hp=(0, 0.06, -0.13), hips=(2, 0, 10), spine=(4, 0, 6), chest=(5, 0, 8), neck=(0, 0, -6), head=(0, 0, -8),
                hr=Hd(p=(0.20, 0.50, 0.0), f=(0.5, 1, 0), m=(0, 0, -1), e=(-0.4, -0.5, -1)),
                hl=Hd(p=(0.22, 0.04, -0.14), f=(0.6, -0.3, -0.4), m=(0, 0, -1))), "sp"),
    (F(10), dict(hp=(0, 0.04, -0.14), hips=(2, 0, 22), spine=(3, 0, 8), chest=(3, 0, 10), neck=(0, 0, -12), head=(0, 0, -14),
                 hr=Hd(p=(0.42, 0.28, 0.06), f=(1, 0.2, 0.2)), hl=Hd(p=(0.40, -0.10, -0.06))), "sp"),
    (F(15), {"_base": True}, "sp"),
], contact=F(7), notes="Wide horizontal arm arc (right arm) with torso whip; contact = arm sweeps through the front (0.23 s).")

clip("water_freeze", 0.4, False, B_WATER, [
    (0.0, {}, "sp"),
    (F(3), dict(hp=(0, 0.02, -0.12), spine=(4, 0, 0), chest=(4, 0, 0), neck=(-4, 0, 0),
                hl=H((0.04, 0.44, -0.02), PALM_FWD, (0.9, -0.2, -0.6)), hr=Hd(p=(0.04, 0.44, -0.02), f=(0, 0.3, 1), m=(0, 1, -0.2), e=(-0.9, -0.2, -0.6))), "out"),
    (F(6), dict(hp=(0, 0.01, -0.13), hl=Hd(p=(0.06, 0.42, 0.0)), hr=Hd(p=(0.06, 0.42, 0.0))), "io"),
    (F(8), dict(hp=(0, 0.0, -0.15), spine=(6, 0, 0), chest=(7, 0, 0), neck=(2, 0, 0), head=(4, 0, 0), sl=(9, 0), sr=(9, 0),
                hl=Hd(p=(0.05, 0.36, -0.03), f=(0, 1, 0), m=(-0.7, 0, -0.7), e=(0.9, -0.2, -0.6)),
                hr=Hd(p=(0.05, 0.36, -0.03), f=(0, 1, 0), m=(0.7, 0, -0.7), e=(-0.9, -0.2, -0.6))), "in3"),
    (F(10), dict(hp=(0, 0.0, -0.16), hl=Hd(p=(0.045, 0.355, -0.035)), hr=Hd(p=(0.045, 0.355, -0.035))), "io"),
    (F(12), {"_base": True}, "io"),
], contact=F(8), notes="Reach out with open hands then clench both fists with a shoulder-tensed squeeze; contact = clench (0.27 s).")

WATER_SHIELD = merged(B_WATER, dict(hp=(0, 0, -0.14), spine=(-1, 0, 0), chest=(-2, 0, -4)))


def _shield_keys():
    keys = []
    for k in range(8):
        th = 2 * math.pi * k / 8
        c, sn = math.cos(th), math.sin(th)
        c2, s2 = math.cos(th + math.pi / 2), math.sin(th + math.pi / 2)
        hl = Hd(p=(0.0 + 0.06 * c, 0.38, 0.06 + 0.06 * sn), f=(0.4, 1, 0.2), m=(-1, 0.3, 0.2), e=(1.0, 0.0, 0.4))
        hr = Hd(p=(0.0 - 0.06 * c2, 0.38, 0.06 + 0.06 * s2), f=(-0.4, 1, 0.2), m=(1, 0.3, 0.2), e=(-1.0, 0.0, 0.4))
        keys.append((F(6 * k), dict(hl=hl, hr=hr, hp=(0, 0, -0.14 + 0.006 * math.sin(th))), "sp"))
    return keys


clip("water_shield", 1.6, True, WATER_SHIELD, _shield_keys(),
     notes="Arms rounded in front like a ring/shield, hands slowly orbiting; low planted stance. Loops.")

# ====================================================================================================
# FIRE  (sharp, forward)  base: fire stance
# ====================================================================================================
clip("fire_jab", 0.3, False, B_FIRE, [
    (0.0, {}, "lin"),
    (F(1), dict(hp=(0, -0.01, -0.11), hips=(0, 0, -19), spine=(7, 0, -6), chest=(4, 0, -7), neck=(-5, 0, 14), head=(-4, 0, 16),
                hl=Hd(p=(-0.04, 0.24, 0.0))), "out"),
    (F(3), dict(hp=(0, 0.07, -0.12), hips=(0, 0, -10), spine=(8, 0, -9), chest=(6, 0, -11), neck=(-6, 0, 18), head=(-5, 0, 20), sl=(2, 12),
                hl=Hd(p=(-0.02, 0.56, 0.04), f=(0, 1, 0), m=(-1, 0, 0), e=(0.5, -0.3, -1.0)),
                hr=Hd(p=(0.04, 0.18, 0.02), f=(0, 0.5, 0.9), m=(1, 0, 0))), "in3"),
    (F(5), dict(hp=(0, 0.05, -0.11), hips=(0, 0, -14), spine=(7, 0, -7), chest=(5, 0, -8), sl=(1, 6),
                hl=Hd(p=(-0.05, 0.40, 0.03))), "out"),
    (F(9), dict(B_FIRE, _base=True), "io"),
], contact=F(3), notes="Quick lead-hand jab, hips and shoulder snap forward; contact = fist extension (0.1 s).")

B_CHARGE = merged(
    B_FIRE,
    dict(hp=(0, 0.0, -0.14), spine=(11, 0, -5), chest=(7, 0, -5), neck=(-9, 0, 11), head=(-6, 0, 14), sl=(3, 0), sr=(3, 0)),
    both(H((0.02, 0.14, -0.46), ((0, 1, 0.1), (-1, 0, 0)), (0.6, -0.5, -1))),
)
BASES["charge"] = B_CHARGE
clip("fire_charge", 0.8, True, B_CHARGE, [
    (0.0, {}, "sp"),
    (F(4), dict(hp=(0, 0.0, -0.152), spine=(12, 0, -5), hl=Hd(p=(0.03, 0.13, -0.46)), hr=Hd(p=(0.01, 0.15, -0.46)), sl=(5, 0), sr=(4, 0)), "sp"),
    (F(8), dict(hp=(0, 0.0, -0.136), spine=(10.5, 0, -5), hl=Hd(p=(0.01, 0.15, -0.45)), hr=Hd(p=(0.03, 0.13, -0.45)), sl=(3, 0), sr=(5, 0)), "sp"),
    (F(12), dict(hp=(0, 0.0, -0.152), spine=(12, 0, -5), hl=Hd(p=(0.03, 0.13, -0.46)), hr=Hd(p=(0.01, 0.15, -0.46)), sl=(5, 0), sr=(4, 0)), "sp"),
    (F(16), dict(hp=(0, 0.0, -0.136), spine=(10.5, 0, -5), hl=Hd(p=(0.01, 0.15, -0.45)), hr=Hd(p=(0.03, 0.13, -0.45)), sl=(3, 0), sr=(5, 0)), "sp"),
    (F(20), dict(hp=(0, 0.0, -0.148), spine=(11.5, 0, -5), hl=Hd(p=(0.025, 0.135, -0.46)), hr=Hd(p=(0.015, 0.145, -0.46)), sl=(4, 0), sr=(4, 0)), "sp"),
], notes="Both fists chambered at the hips, torso coiled forward, tense vibration. Loops.")

clip("fire_release", 0.4, False, B_CHARGE, [
    (0.0, {}, "lin"),
    (F(1), dict(hp=(0, -0.04, -0.16), spine=(13, 0, -6), hl=Hd(p=(0.02, 0.06, -0.48)), hr=Hd(p=(0.02, 0.06, -0.48))), "out"),
    (F(3), dict(hp=(0, 0.12, -0.13), hips=(0, 0, -6), spine=(10, 0, -2), chest=(10, 0, -2), neck=(-14, 0, 6), head=(-8, 0, 8), sl=(0, 10), sr=(0, 10),
                fr=Fd(pitch=-16, pv=0.135),
                hl=H((-0.12, 0.54, -0.02), PALM_FWD, (0.5, 0.0, -1.0)),
                hr=Hd(p=(0.12, 0.54, -0.02), f=(0, 0.3, 1), m=(0, 1, -0.2), e=(-0.5, 0.0, -1.0))), "in3"),
    (F(5), dict(hp=(0, 0.13, -0.13), hl=Hd(p=(-0.12, 0.56, 0.0)), hr=Hd(p=(0.12, 0.56, 0.0))), "out"),
    (F(8), dict(hp=(0, 0.09, -0.12), hl=Hd(p=(-0.10, 0.50, -0.02)), hr=Hd(p=(0.10, 0.50, -0.02))), "io"),
    (F(12), dict(B_FIRE, _base=True), "io"),
], contact=F(3), notes="Double palm strike from the chambered pose; contact = palms land (0.1 s). Ends in fire stance.")

B_HEAT = merged(
    dict(hp=(0, 0.0, -0.15), hips=(0, 0, 0), spine=(2, 0, 0), chest=(2, 0, 0), neck=(-2, 0, 0), head=(-4, 0, 0), sl=(1, 0), sr=(1, 0)),
    both(H((0.12, 0.44, -0.04), PALM_FWD, (0.9, -0.2, -0.8))),
    dict(fl=Fd(x=0.24, y=0.06, yaw=18), fr=Fd(x=-0.24, y=0.0, yaw=-18)),
)
BASES["heat"] = B_HEAT
clip("heat_draw", 1.6, True, B_HEAT, [
    (0.0, {}, "sp"),
    (F(26), merged(dict(hp=(0, 0.0, -0.172), spine=(6, 0, 0), chest=(8, 0, 0), neck=(-5, 0, 0), head=(-6, 0, 0), sl=(6, 0), sr=(6, 0)),
                   both(H((0.0, 0.20, 0.04), ((0, 0.2, 1), (0, -1, 0.2)), (0.9, 0.0, -0.8)))), "sp"),
], notes="Rooted stance; open palms slowly pull toward the chest (extracting heat) then push out. Loops.")

B_MAGMA = merged(
    B_EARTH,
    dict(hp=(0, 0.02, -0.22), spine=(12, 0, 0), chest=(8, 0, 0), neck=(-10, 0, 0), head=(-8, 0, 0), sl=(6, 0), sr=(6, 0)),
    both(H((-0.02, 0.26, -0.14), ((0.1, 1, 0.3), (-1, 0, 0.5)), (0.9, -0.1, -0.8))),
)
BASES["magma"] = B_MAGMA


def _magma_keys():
    keys = []
    for k in range(6):
        sg = 1 if k % 2 == 0 else -1
        keys.append((F(4 * k), dict(hp=(0.004 * sg, 0.02, -0.22 + 0.007 * sg), chest=(8 + 1.0 * sg, 0, 0), sl=(6 + sg, 0), sr=(6 - sg, 0),
                                    hl=Hd(p=(-0.02 + 0.007 * sg, 0.26, -0.14 + 0.009 * sg)), hr=Hd(p=(0.02 - 0.007 * sg, 0.26, -0.14 - 0.009 * sg))), "sp"))
    return keys


clip("magma_hold", 0.8, True, B_MAGMA, _magma_keys(),
     notes="Cupped hands around a glowing mass (centre ~ (0, 0.26 fwd, 1.0 m)), hunched, intense fast tremble. Loops.")

clip("pour", 0.5, False, B_MAGMA, [
    (0.0, {}, "lin"),
    (F(3), dict(hp=(0, 0.0, -0.17), hips=(0, 0, -6), spine=(4, 0, 0), chest=(2, 0, 0), neck=(-4, 0, 0),
                hl=Hd(p=(-0.02, 0.30, 0.08)), hr=Hd(p=(0.02, 0.30, 0.08))), "out"),
    (F(8), dict(hp=(0, 0.18, -0.40), hips=(14, 0, -10), spine=(22, 0, 0), chest=(18, 0, -2), neck=(-28, 0, 8), head=(-14, 0, 8), sl=(0, 10), sr=(0, 10),
                hl=Hd(p=(0.16, 0.60, 0.42), f=(0, 1, 0), m=(0, 0, -1), e=(0.9, -0.2, -0.3), sp="rt"),
                hr=Hd(p=(-0.16, 0.60, 0.42), f=(0, 1, 0), m=(0, 0, -1), e=(-0.9, -0.2, -0.3), sp="rt")), "in3"),
    (F(11), dict(hp=(0, 0.24, -0.40), hl=Hd(p=(0.16, 0.80, 0.36), sp="rt"), hr=Hd(p=(-0.16, 0.80, 0.36), sp="rt")), "out"),
    (F(15), {"_base": True}, "io"),
], contact=F(8), notes="Raise, then push the mass down and sweep it forward along the ground; contact = mass hits the floor (0.27 s). Feet stay planted in the wide stance; starts/ends in the magma_hold stance.")

B_LIGHT = merged(
    B_FIRE,
    dict(hp=(0, 0.0, -0.09), spine=(4, 0, -5), chest=(0, 0, -5), neck=(-2, 0, 11), head=(-6, 0, 14), sl=(8, 0), sr=(8, 0)),
)


def _lightning_keys():
    keys = []
    n = 12
    r, cz = 0.25, 0.10
    for k in range(n):
        th = 2 * math.pi * k / n
        def hand(phi, side):
            cph, sph = math.cos(phi), math.sin(phi)
            wx = r * cph                         # world-x relative to the body centre line (left +)
            pz = cz + r * sph
            tan = (-sph, 0.35, cph)               # fingers follow the circle tangent
            inn = (-cph, 0.0, -sph)              # palm faces the circle centre
            if side == "L":
                return Hd(p=(wx - 0.175, 0.32, pz), f=tan, m=inn, e=(0.8, 0.0, -0.6))
            return Hd(p=(wx + 0.175, 0.32, pz), f=tan, m=inn, e=(-0.8, 0.0, -0.6))
        keys.append((F(3 * k), dict(hl=hand(th, "L"), hr=hand(th + math.pi, "R"),
                                    hp=(0.0, 0.0, -0.09 + 0.01 * math.sin(th * 2)), head=(-8, 0, 14)), "sp"))
    return keys


clip("lightning_charge", 1.2, True, B_LIGHT, _lightning_keys(),
     notes="Both arms trace a vertical ring (r=0.25 m) in front of the chest with extended fingers; hands half a turn apart. Loops.")

clip("lightning_release", 0.35, False, B_LIGHT, [
    (0.0, {}, "lin"),
    (F(1), dict(hp=(0, -0.02, -0.11), hips=(0, 0, -12), spine=(4, 0, -8),
                hr=Hd(p=(0.06, 0.16, 0.16), f=(0, 1, 0.3), m=(1, 0, 0), e=(-0.4, -0.8, -0.6)),
                hl=Hd(p=(-0.08, 0.20, 0.0), f=(0, 0.6, 0.8), m=(-1, 0, 0))), "out"),
    (F(2), dict(hp=(0, 0.11, -0.12), hips=(0, 0, 8), spine=(6, 0, 4), chest=(4, 0, 6), neck=(-6, 0, -8), head=(-6, 0, -10), sr=(0, 12),
                hr=Hd(p=(0.17, 0.56, 0.04), f=(0, 1, 0.0), m=(1, 0, 0), e=(-0.4, -0.8, -0.5)),
                hl=Hd(p=(-0.02, 0.10, 0.0), f=(0, 0.6, 0.8), m=(-1, 0, 0))), "in3"),
    (F(4), dict(hp=(0, 0.12, -0.12), hr=Hd(p=(0.17, 0.57, 0.05))), "out"),
    (F(7), dict(hp=(0, 0.08, -0.11), hr=Hd(p=(0.15, 0.50, 0.04))), "io"),
    (F(11), dict(B_LIGHT, _base=True), "io"),
], contact=F(2), notes="Right-hand two-finger thrust; contact = fingertips land (0.07 s). Ends in a ready pose.")

# ====================================================================================================
# AIR  (light, pivoting)  base: air stance
# ====================================================================================================
clip("air_push", 0.35, False, B_AIR, [
    (0.0, {}, "lin"),
    (F(2), dict(hp=(0, -0.03, -0.03), hips=(0, 0, 8), spine=(-3, 0, 4), chest=(-4, 0, 6), neck=(0, 0, -6), head=(0, 0, -6),
                hl=Hd(p=(-0.02, 0.14, -0.08)), hr=Hd(p=(0.02, 0.14, -0.08))), "out"),
    (F(4), dict(hp=(0, 0.06, -0.04), hips=(2, 0, -8), spine=(4, 0, -4), chest=(6, 0, -6), neck=(-4, 0, 6), head=(-4, 0, 6), sl=(0, 8), sr=(0, 8),
                fl=Fd(pitch=-14), fr=Fd(pitch=-14),
                hl=H((-0.10, 0.52, 0.0), PALM_FWD, (0.5, 0.0, -1)), hr=Hd(p=(0.10, 0.52, 0.0), f=(0, 0.3, 1), m=(0, 1, -0.2), e=(-0.5, 0.0, -1))), "in3"),
    (F(6), dict(hp=(0, 0.07, -0.04), hl=Hd(p=(-0.10, 0.55, 0.01)), hr=Hd(p=(0.10, 0.55, 0.01))), "out"),
    (F(8), dict(hp=(0, 0.04, -0.03), hl=Hd(p=(-0.08, 0.48, 0.0)), hr=Hd(p=(0.08, 0.48, 0.0))), "io"),
    (F(11), dict(B_AIR, _base=True), "io"),
], contact=F(4), notes="Two-hand open palm push with a small pivot of the hips; stays on the balls of the feet; contact = palms release (0.13 s).")


def _gust():
    # full 360-degree pirouette while airborne, then a landing palm thrust
    end_air = dict(B_AIR)
    wide = both(H((0.50, 0.06, 0.04), ((1, 0.2, 0.1), (0, 0, -1)), (0.2, -1, -0.2)))
    keys = [
        (0.0, {}, "lin"),
        (F(3), merged(dict(hp=(0, 0, -0.10), hips=(0, 0, 20), spine=(2, 0, 0), chest=(0, 0, 10), neck=(0, 0, -10), head=(0, 0, -10)),
                      both(H((0.20, 0.20, -0.10), PALM_IN, (0.9, -0.3, -0.8)))), "out"),
        (F(5), merged(dict(hp=(0, 0, 0.07), hips=(0, 0, -20), chest=(0, 0, 0), neck=(0, 0, 0), head=(0, 0, 0),
                           fl=Fd(x=0.06, y=0.05, lift=0.20, yaw=-20, pitch=-30, pv=0.0), fr=Fd(x=-0.06, y=-0.03, lift=0.20, yaw=-30, pitch=-30, pv=0.0)), wide), "in"),
        (F(8), merged(dict(hp=(0, 0, 0.10), hips=(0, 0, -200),
                           fl=Fd(x=0.06, y=0.05, lift=0.22, yaw=-200, pitch=-30, pv=0.0), fr=Fd(x=-0.06, y=-0.03, lift=0.22, yaw=-210, pitch=-30, pv=0.0)), wide), "lin"),
        (F(10), dict(hp=(0, 0, 0.05), hips=(0, 0, -340), spine=(0, 0, 0),
                     fl=Fd(x=0.12, y=0.12, lift=0.12, yaw=-340, pitch=-30, pv=0.0), fr=Fd(x=-0.12, y=-0.02, lift=0.12, yaw=-350, pitch=-30, pv=0.0)), "out"),
        (F(11), merged(dict(hp=(0, 0.08, -0.06), hips=(0, 0, -360), spine=(5, 0, 0), chest=(6, 0, 0), neck=(-4, 0, 0), head=(-4, 0, 0), sl=(0, 8), sr=(0, 8),
                            fl=Fd(x=0.12, y=0.08, lift=0.0, yaw=-354, pitch=-9, pv=0.13), fr=Fd(x=-0.12, y=-0.07, lift=0.0, yaw=-366, pitch=-9, pv=0.13)),
                       both(H((-0.10, 0.54, 0.0), PALM_FWD, (0.5, 0.0, -1)))), "in3"),
        (F(14), dict(hp=(0, 0.07, -0.05), hl=Hd(p=(-0.10, 0.58, 0.01)), hr=Hd(p=(0.10, 0.58, 0.01))), "out"),
    ]
    fin = dict(B_AIR)
    fin["hips"] = (0, 0, -360)
    fin["fl"] = Fd(x=0.12, y=0.08, yaw=6 - 360, pitch=-9, pv=0.13, lift=0.0)
    fin["fr"] = Fd(x=-0.12, y=-0.07, yaw=-6 - 360, pitch=-9, pv=0.13, lift=0.0)
    keys.append((F(18), fin, "io"))
    return keys


clip("air_gust", 0.6, False, B_AIR, _gust(), contact=F(11),
     notes="Wind-up, airborne 360-degree pirouette on the balls of the feet, lands with a two-palm thrust; contact = landing + thrust (0.37 s). Hips/foot yaw end at -360 (== rest orientation).")
