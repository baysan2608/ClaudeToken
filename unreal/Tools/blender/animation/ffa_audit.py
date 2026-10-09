"""Fourfold animation toolkit - motion-quality audit (numpy, no bpy).  Not a validator: it measures how a clip MOVES.

    python3 ffa_audit.py [--only "f_*,e_strike"] [--json out.json]

Per clip (all numbers from FK of the solved poses):
  * hip_yaw / sh_yaw     range (deg) of the hip line (thigh heads) and shoulder line (upper-arm heads) yaw, start..end
  * xfactor              largest hip-shoulder separation change (deg) - the torso coiling against the hips
  * drop_cm              deepest pelvis drop below the start pose (cm)
  * weight               weight transfer: COM position along the rear->front ankle line (0 = over the rear ankle,
                         1 = over the front ankle) at the start and at the contact, e.g. "0.42->0.71"
  * chain                frame of peak angular speed in the 12 frames before the first contact, for pelvis -> chest
                         -> upper arm -> forearm of the striking side (pelvis -> thigh -> shin for kicks); "ok" when
                         ordered proximal -> distal (1 frame tolerance) and the hips lead the arm
  * stops                key-like dead stops: interior frames where the whole body nearly stops (max bone speed
                         < 0.6 deg/f) while moving fast (> 4 deg/f) within 3 frames on both sides
  * static               longest run of frames in which nothing moves (max bone speed < 0.25 deg/f), excluding loops
  * settle               frames after the contact hold in which the free parts still move (> 0.5 deg/f) - 0 means
                         the body freezes after the strike
  * jerk                 the largest frame-to-frame change of angular speed (deg/f^2) of the trunk bones (pops in
                         timing, not in pose)
"""
import argparse
import fnmatch
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "common")))

import ffa_rig as rig  # noqa: E402
from ffa_math import angle_of  # noqa: E402

BODY = [b for b in rig.ANIM_BONES if not any(k in b for k in ("index", "middle", "ring", "pinky", "thumb"))]
TRUNK = ["pelvis", "spine_01", "spine_03", "spine_05", "neck_01", "head"]


def _yaw(v):
    # Blender armature space: x = character left, -y = forward
    return math.degrees(math.atan2(-float(v[1]), float(v[0])))


def _wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def speeds(fk):
    """(frames-1) x bones world angular speed (deg / frame)."""
    n = len(fk)
    out = {}
    for b in BODY:
        out[b] = np.array([angle_of(fk[f][0][b].T @ fk[f + 1][0][b]) for f in range(n - 1)])
    return out


def audit_frames(name, frames, loop, contacts, strike, poses):
    fk = [rig.fk(p) for p in poses]
    n = len(poses) - 1
    sp = speeds(fk)
    mx = np.max(np.stack([sp[b] for b in BODY]), axis=0)          # max bone speed per frame step
    hip = np.array([_yaw(Hd["thigh_l"] - Hd["thigh_r"]) for _, Hd, _ in fk])
    sh = np.array([_yaw(Hd["upperarm_l"] - Hd["upperarm_r"]) for _, Hd, _ in fk])
    hip = np.unwrap(np.radians(hip)) * 180 / math.pi
    sh = np.unwrap(np.radians(sh)) * 180 / math.pi
    xf = sh - hip
    pz = np.array([Hd["pelvis"][2] for _, Hd, _ in fk])
    r = {"frames": n, "loop": bool(loop)}
    r["hip_yaw"] = round(float(hip.max() - hip.min()), 1)
    r["sh_yaw"] = round(float(sh.max() - sh.min()), 1)
    r["xfactor"] = round(float(np.abs(xf - xf[0]).max()), 1)
    r["drop_cm"] = round(float((pz[0] - pz.min()) * 100), 1)
    # weight along the ankle line: front = the foot further forward (A-frame +y = Blender -y) at frame 0
    _, H0, _ = fk[0]
    al, ar = H0["foot_l"], H0["foot_r"]
    front, rear = ("l", "r") if -al[1] > -ar[1] else ("r", "l")

    def wfrac(f):
        _, Hd, _ = fk[f]
        a, b = Hd["foot_" + rear][:2], Hd["foot_" + front][:2]
        com = (0.5 * Hd["spine_01"] + 0.3 * Hd["pelvis"] + 0.2 * Hd["spine_05"])[:2]
        d = b - a
        L = float(np.dot(d, d))
        return float(np.dot(com - a, d) / L) if L > 1e-6 else 0.5
    c0 = contacts[0] if contacts else None
    if c0 is not None:
        r["weight"] = f"{wfrac(0):.2f}->{wfrac(c0):.2f}"
    # kinetic chain order before the first contact
    if c0 is not None and strike in ("hand_l", "hand_r", "foot_l", "foot_r", "hands"):
        s = strike[-1] if strike != "hands" else "r"
        if strike.startswith("foot"):
            chain = ["pelvis", "thigh_" + s, "calf_" + s]
        else:
            chain = ["pelvis", "spine_05", "upperarm_" + s, "lowerarm_" + s]
        lo = max(0, c0 - 12)
        peaks = []
        for b in chain:
            seg = sp[b][lo:c0]
            peaks.append(lo + int(np.argmax(seg)) + 1 if len(seg) else c0)
        # proximal -> distal: every link peaks no later than the next one (+1 frame tolerance: the elbow opens while
        # the shoulder finishes) and the hips clearly lead the arm
        ordered = all(peaks[i] <= peaks[i + 1] + 1 for i in range(len(peaks) - 1)) and peaks[0] < peaks[-1]
        r["chain"] = ("ok " if ordered else "BAD ") + "/".join(str(p) for p in peaks)
        r["chain_ok"] = ordered
        r["chain_spread"] = peaks[-1] - peaks[0]
    # dead stops in the middle of motion
    stops = 0
    for f in range(3, n - 4):
        if mx[f] < 0.6 and mx[max(0, f - 3):f].max() > 4.0 and mx[f + 1:f + 4].max() > 4.0:
            stops += 1
    r["stops"] = stops
    if not loop:
        run = best = 0
        for v in mx[1:-1]:
            run = run + 1 if v < 0.25 else 0
            best = max(best, run)
        r["static"] = best
    if c0 is not None:
        tail = mx[c0:min(n, c0 + 10)]
        r["settle"] = int(np.sum(tail > 0.5))
    tr = np.max(np.stack([sp[b] for b in TRUNK]), axis=0)
    r["jerk"] = round(float(np.abs(np.diff(tr)).max()) if len(tr) > 1 else 0.0, 1)
    return r


def audit(res):
    c = res.clip
    return audit_frames(res.name, res.frames, c.loop, c.contacts, c.strike, res.poses)


def sim_usage():
    """clip -> [(move key, role, startup f, active f, recovery f)] from anim_map.json + the sim's move_index.json."""
    import anim_map_gen as G
    moves = {m["id"]: m for m in G.load_moves()}
    amap = json.load(open(os.path.join(G.DATA, "anim_map.json")))
    use = {}

    def walk(key, e, m):
        for role in ("startup", "release"):
            c = e.get(role)
            if isinstance(c, str):
                use.setdefault(c, []).append((key, role, m["startup"] * 60, m["active"] * 60, m["recovery"] * 60))
        for sub in ("tiers", "modes"):
            for k, v in (e.get(sub) or {}).items():
                walk(key, v, m)
    for key, e in amap["moves"].items():
        m = moves.get(key.split("@")[0])
        if m:
            walk(key, e, m)
    return use


def suggest_timing(name, frames, contact, usage):
    """(contact, frames) that play close to rate 1 in the sim: the contact near the median startup (inside the
    0.6-1.6 rate clamp of every move that uses the clip), the length = contact + median active + median recovery
    (never shorter than authored - deliberate recoveries instead of a 0.6x slow-down and a frozen last frame)."""
    su = sorted(u[2] for u in usage if u[2] > 0.5)
    act = sorted(u[3] for u in usage)
    rec = sorted(u[4] for u in usage if u[4] > 0.5)
    c = contact
    if su:
        lo, hi = max(su) / 1.6, min(su) * 1.6
        med = su[len(su) // 2]
        if c / med > 1.3:
            c = max(int(math.ceil(lo)), int(round(med * 1.25)))
        elif c / med < 0.8:
            c = min(int(math.floor(hi)), int(round(med * 0.95)))
        c = int(min(max(c, math.ceil(lo)), math.floor(hi)))
    n = frames
    if rec:
        want = c + act[len(act) // 2] + rec[len(rec) // 2]
        n = max(frames - contact + c, int(round(want)))
    return c, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--json", default="")
    ap.add_argument("--timing", action="store_true", help="print sim-fitted (contact, frames) suggestions")
    a = ap.parse_args()
    if a.timing:
        clips = json.load(open(os.path.join(HERE, "..", "..", "..", "Content", "Fourfold", "Data", "clips.json")))["clips"]
        for n, us in sorted(sim_usage().items()):
            c = clips.get(n)
            if not c or c["loop"] or not c["contact"]:
                continue
            sc, sn = suggest_timing(n, c["frames"], c["contact"], us)
            flag = "" if (sc, sn) == (c["contact"], c["frames"]) else "  <-"
            print(f"{n:18s} now ({c['contact']:2d}, {c['frames']:2d})  fit ({sc:2d}, {sn:2d}){flag}   "
                  + " ".join(f"{u[0]}:{u[2]:.0f}/{u[3]:.0f}/{u[4]:.0f}" for u in us[:4]))
        return
    import clips as catalog
    cat = catalog.load()
    pats = [p.strip() for p in a.only.split(",")] if a.only else None
    out = {}
    for n in cat:
        if n.startswith("hand_") or (pats and not any(fnmatch.fnmatch(n, p) for p in pats)):
            continue
        res = cat[n]().build()
        r = audit(res)
        out[n] = r
        print(f"{n:18s} " + " ".join(f"{k}={v}" for k, v in r.items() if k not in ("frames", "loop", "chain_ok")))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1)


if __name__ == "__main__":
    with np.errstate(all="ignore"):
        main()
