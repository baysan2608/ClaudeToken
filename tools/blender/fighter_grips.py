"""Hand shapes (finger poses) per clip.  A grip is a 5-tuple (thumb tuck, thumb tip curl, index+middle curl,
ring+pinky curl, spread) that fighter_pose.RigModel.finger_quats turns into rotations of the 12 finger bones.

GRIPS[clip] = either a preset name (both hands, constant) or {"L": [(seconds, preset|tuple), ...], "R": [...]}
(right defaults to the left list).  Keys are blended with a smoothstep; loop clips wrap, other clips hold the last key.
"""
PRESETS = {
    "relaxed":     (0.25, 0.20, 0.28, 0.34, 0.10),
    "open":        (0.00, 0.00, 0.03, 0.05, 0.30),
    "open_spread": (0.00, 0.00, 0.00, 0.00, 1.00),
    "blade":       (0.80, 0.30, 0.00, 0.00, -0.30),     # fingers together and straight, thumb tucked (knife hand / palm)
    "fist":        (1.00, 0.80, 1.00, 1.00, 0.00),
    "fist_loose":  (0.60, 0.50, 0.72, 0.78, 0.00),
    "claw":        (0.10, 0.30, 0.55, 0.55, 0.80),
    "flow":        (0.12, 0.10, 0.14, 0.20, 0.40),      # soft, slightly splayed (water)
    "cup":         (0.30, 0.30, 0.50, 0.55, 0.40),
    "cup_claw":    (0.20, 0.25, 0.50, 0.55, 0.80),
    "two_finger":  (0.90, 0.60, 0.00, 1.00, -0.15),     # index + middle extended together, others curled (lightning)
}


def f(n):
    return n / 30.0


GRIPS = {
    "idle": "relaxed", "walk": "relaxed", "run": "fist_loose", "land": "relaxed", "fall": "claw",
    "stance_earth": "fist_loose", "stance_water": "flow", "stance_fire": "fist", "stance_air": "open",
    "strafe_l": "fist", "strafe_r": "fist", "walk_back": "fist", "guard": "fist",
    "glide": "blade", "air_dash": "blade",
    "earth_hold": "cup", "water_hold": "flow", "water_shield": "flow", "heat_draw": "open", "magma_hold": "cup_claw",
    "fire_charge": "fist", "fire_jab": "fist", "lightning_charge": "two_finger",
    "air_push": "open", "air_gust": "open",
    "evade_l": {"L": [(0, "relaxed"), (f(3), "fist_loose"), (f(10), "fist_loose"), (f(14), "relaxed")]},
    "jump": {"L": [(0, "relaxed"), (f(3), "fist_loose"), (f(6), "open")]},
    "deflect": {"L": [(0, "fist"), (f(1), "fist"), (f(2), "open"), (f(5), "open"), (f(8), "fist")],
                "R": [(0, "fist"), (f(2), "fist_loose"), (f(8), "fist")]},
    "earth_wall": {"L": [(0, "fist_loose"), (f(2), "fist"), (f(5), "fist"), (f(8), "open"), (f(10), "open_spread"), (f(12), "fist_loose")]},
    "hit_front": {"L": [(0, "relaxed"), (f(2), "claw"), (f(7), "relaxed")]},
    "hit_heavy": {"L": [(0, "relaxed"), (f(3), "claw"), (f(10), "open"), (f(18), "relaxed")]},
    "stagger": {"L": [(0, "relaxed"), (f(3), "open_spread"), (f(10), "relaxed")]},
    "knockdown": {"L": [(0, "relaxed"), (f(6), "claw"), (f(17), "relaxed")]},
    "getup": {"L": [(0, "relaxed"), (f(3), "open"), (f(14), "open"), (f(19), "relaxed"), (f(24), "relaxed")]},
    "earth_lift": {"L": [(0, "fist_loose"), (f(2), "open"), (f(3), "claw"), (f(6), "fist"), (f(12), "fist_loose")]},
    "earth_throw": {"L": [(0, "fist_loose")],
                    "R": [(0, "fist_loose"), (f(3), "fist"), (f(7), "fist"), (f(10), "fist_loose")]},
    "earth_heavy": {"L": [(0, "fist_loose"), (f(5), "claw"), (f(10), "claw"), (f(13), "fist"), (f(17), "open"), (f(24), "fist_loose")]},
    "water_draw": {"L": [(0, "flow"), (f(7), "flow"), (f(11), "open_spread"), (f(15), "flow")]},
    "water_whip": {"L": [(0, "flow")],
                   "R": [(0, "flow"), (f(3), "flow"), (f(5), "open"), (f(7), "open_spread"), (f(10), "open_spread"), (f(15), "flow")]},
    "water_freeze": {"L": [(0, "flow"), (f(3), "open"), (f(6), "open_spread"), (f(8), "fist"), (f(10), "fist"), (f(12), "flow")]},
    "fire_release": {"L": [(0, "fist"), (f(2), "fist"), (f(3), "open"), (f(8), "open"), (f(12), "fist")]},
    "pour": {"L": [(0, "cup_claw"), (f(3), "cup_claw"), (f(8), "open"), (f(15), "cup_claw")]},
    "lightning_release": {"L": [(0, "two_finger"), (f(1), "fist"), (f(11), "fist")],
                          "R": [(0, "two_finger")]},
}
def _breathe(preset, period, d=0.07, phase=0.0):
    """loop grip that tightens and relaxes slightly (fingers follow the breathing / tension of the clip)"""
    a = PRESETS[preset]
    b = tuple(min(1.0, max(-1.0, v + d)) if i in (1, 2, 3) else v for i, v in enumerate(a))
    return {"L": [(0.0, a), (period * 0.5, b), (period, a)], "R": [(0.0, a), (period * (0.5 + phase), b), (period, a)]}


GRIPS["idle"] = _breathe("relaxed", 2.0, 0.08, 0.1)
GRIPS["stance_earth"] = _breathe("fist_loose", 2.0, 0.08)
GRIPS["stance_water"] = {"L": [(0.0, "flow"), (0.6, (0.2, 0.15, 0.2, 0.28, 0.5)), (1.2, "flow"), (1.8, (0.1, 0.08, 0.1, 0.15, 0.3)), (2.4, "flow")],
                         "R": [(0.0, (0.1, 0.08, 0.1, 0.15, 0.3)), (0.6, "flow"), (1.2, (0.2, 0.15, 0.2, 0.28, 0.5)), (1.8, "flow"), (2.4, (0.1, 0.08, 0.1, 0.15, 0.3))]}
GRIPS["stance_air"] = _breathe("open", 2.0, 0.05, 0.15)
GRIPS["guard"] = _breathe("fist", 1.6, -0.05)
GRIPS["fire_charge"] = {"L": [(f(0), "fist"), (f(4), (1.0, 0.8, 0.92, 0.95, 0.0)), (f(8), "fist"), (f(12), (1.0, 0.8, 0.92, 0.95, 0.0)), (f(16), "fist"), (f(20), (1.0, 0.8, 0.96, 0.97, 0.0))]}
GRIPS["evade_r"] = GRIPS["evade_back"] = GRIPS["evade_fwd"] = GRIPS["evade_l"]
GRIPS["hit_back"] = GRIPS["hit_front"]


def _tuple(v):
    return PRESETS[v] if isinstance(v, str) else tuple(v)


def _blend(a, b, u):
    u = u * u * (3 - 2 * u)
    return tuple(x + (y - x) * u for x, y in zip(a, b))


def _eval(keys, t, period, loop):
    keys = [(float(tt), _tuple(v)) for tt, v in keys]
    if loop:
        t = t % period
        if keys[0][0] > 0.0:
            keys = [(0.0, keys[0][1])] + keys
        if keys[-1][0] < period:
            keys = keys + [(period, keys[0][1])]
    if t <= keys[0][0]:
        return keys[0][1]
    for i in range(len(keys) - 1):
        t0, v0 = keys[i]
        t1, v1 = keys[i + 1]
        if t <= t1:
            return v0 if t1 <= t0 else _blend(v0, v1, (t - t0) / (t1 - t0))
    return keys[-1][1]


def grip_at(clip, t, period, loop, default="relaxed"):
    """(left grip, right grip) at time t seconds for the clip."""
    spec = GRIPS.get(clip, default)
    if isinstance(spec, str):
        g = _tuple(spec)
        return g, g
    kl = spec["L"]
    kr = spec.get("R", kl)
    return _eval(kl, t, period, loop), _eval(kr, t, period, loop)
