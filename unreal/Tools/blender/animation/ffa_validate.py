"""Fourfold animation toolkit - clip validation (numpy, no bpy).  Errors block the export; warnings are reported.

Checks (MARTIAL_ARTS.md §5, stream brief):
  * frame count / fps, no NaN
  * planted feet do not slide: the foot's anchor (the sole landmark - heel, ball contact or toe tip - on the floor in
    two consecutive planted frames that moves least, i.e. the pivot) moves <= 3 mm horizontally (in the treadmill
    frame for gait clips), so ball / heel pivots are allowed but any translation is caught; a planted foot
    touches the floor; nothing sinks below the floor by more than 5 mm; planted legs reach their ankle target
  * loops: first == last frame and matching per-frame velocity across the seam
  * joint limits: no knee / elbow hyperextension, wrist swing < 80 deg
  * contact frame = max extension of the striking limb (nothing after contact goes further)
  * start / end pose equals the base stance within 1 deg per bone (pelvis within 3 mm)
"""
import math

import numpy as np

import ffa_dsl as dsl
import ffa_rig as rig
import ffa_solver as solver
from ffa_math import A, angle_of, mat_to_quat, swing_twist_y

SLIDE_TOL = 0.003
GROUND_TOL = 0.006
BASE_TOL_DEG = 1.0
WRIST_LIMIT = 80.0


def landmarks(W, Hd, side):
    """World heel / ball-contact / toe-tip points of one foot (Blender coords) from FK."""
    heel = rig.world_point(W, Hd, "foot_" + side, rig.side_point(rig.HEEL_L, side))
    ball = rig.side_point(rig.BALL_L, side)
    ballc = rig.world_point(W, Hd, "ball_" + side, np.array([ball[0], ball[1], 0.0]))
    toe = rig.side_point(rig.TOE_L, side)
    toec = rig.world_point(W, Hd, "ball_" + side, np.array([toe[0], toe[1], 0.0]))
    return [heel, ballc, toec]


def _limb_ext(Hd, T, which):
    root = Hd["spine_03"]
    if which in ("hand_l", "hand_r"):
        return float(np.linalg.norm(Hd[which] - root))
    if which == "hands":
        return max(float(np.linalg.norm(Hd["hand_l"] - root)), float(np.linalg.norm(Hd["hand_r"] - root)))
    if which in ("foot_l", "foot_r"):
        return float(np.linalg.norm(Hd["ball_" + which[-1]] - Hd["pelvis"]))
    return None


def metric(clip, fk_frames):
    """Per-frame value of the clip's contact metric (bigger = more extended) or None."""
    m = clip.metric or clip.strike
    if not m:
        return None
    out = []
    for W, Hd, T in fk_frames:
        if m in ("hand_l", "hand_r", "hands", "foot_l", "foot_r"):
            out.append(_limb_ext(Hd, T, m))
        elif m.startswith("low_"):          # lowest hand / foot height at contact (ground slaps, stomps)
            b = m[4:]
            out.append(-float(Hd[b][2]) if b in Hd else None)
        elif m.startswith("high_"):         # highest point (rising palms, sky strike)
            b = m[5:]
            out.append(float(Hd[b][2]))
        elif m.startswith("fwd_"):          # furthest forward (A-frame +y) reach of a bone head
            b = m[4:]
            out.append(-float(Hd[b][1]))
        elif m == "apex":
            out.append(float(Hd["pelvis"][2]))
        else:
            return None
    return out


def validate(res):
    clip = res.clip
    errors, warnings, stats = [], [], {}
    n = res.frames
    if len(res.poses) != n + 1:
        errors.append(f"frame count {len(res.poses)} != frames + 1 ({n + 1})")
    fk = [rig.fk(p) for p in res.poses]
    # ---------------------------------------------------------------- NaN
    for f, p in enumerate(res.poses):
        bad = [b for b, q in p.q.items() if not np.all(np.isfinite(q))]
        if bad or not np.all(np.isfinite(p.pelvis_loc)):
            errors.append(f"NaN at frame {f}: {bad[:4]}")
            break
    # ---------------------------------------------------------------- feet
    vel = np.zeros(3) if clip.treadmill is None else A(clip.treadmill[0], clip.treadmill[1], 0.0) / dsl.FPS
    max_slide = 0.0
    for s in rig.SIDES:
        lms = [landmarks(W, Hd, s) for W, Hd, T in fk]
        lowest = min(min(p[2] for p in lm) for lm in lms)
        if lowest < -0.005:
            errors.append(f"foot_{s} sinks {lowest * 1000:.1f} mm below the floor")
        for a, b in res.plants.get(s, []):
            for f in range(a, min(b, n)):
                g = f + 1
                on = False
                best = None
                # the foot's anchor this frame = the floor contact (heel, ball or toe tip) that moves least: a foot
                # pivoting on its ball or heel is planted (its anchor stays put); a translating foot moves every point
                for j in range(3):
                    p0, p1 = lms[f][j], lms[g][j]
                    if p0[2] < GROUND_TOL:
                        on = True
                    if p0[2] < GROUND_TOL and p1[2] < GROUND_TOL:
                        d = p1 - p0 - vel
                        sl = math.hypot(d[0], d[1])
                        if best is None or sl < best[0]:
                            best = (sl, j)
                if best is not None:
                    max_slide = max(max_slide, best[0])
                    if best[0] > SLIDE_TOL:
                        errors.append(f"foot_{s} slides {best[0] * 1000:.1f} mm at frame {f}->{g} (anchor {best[1]})")
                if not on:
                    errors.append(f"foot_{s} planted at frame {f} but not on the floor")
                miss = res.infos[f]["leg_miss_" + s]
                if miss > 0.002:
                    errors.append(f"leg_{s} cannot reach its planted foot at frame {f} ({miss * 1000:.1f} mm)")
    stats["max_slide_mm"] = round(max_slide * 1000, 2)
    # ---------------------------------------------------------------- joint limits
    worst_wrist = 0.0
    for f, (p, info) in enumerate(zip(res.poses, res.infos)):
        W = fk[f][0]
        for s in rig.SIDES:
            # knee: the shin must deviate BACKWARD from the thigh line (thigh +Z = front), elbow: the forearm toward
            # the upper arm's +Z (flexion side); negative = hyperextension
            yt, yc, zt = W["thigh_" + s][:, 1], W["calf_" + s][:, 1], W["thigh_" + s][:, 2]
            kfl = math.degrees(math.atan2(-float(np.dot(yc, zt)), float(np.dot(yc, yt))))
            yu, yl, zu = W["upperarm_" + s][:, 1], W["lowerarm_" + s][:, 1], W["upperarm_" + s][:, 2]
            efl = math.degrees(math.atan2(float(np.dot(yl, zu)), float(np.dot(yl, yu))))
            for fl, lab in ((kfl, "knee"), (efl, "elbow")):
                if fl < -1.0:
                    errors.append(f"{lab}_{s} hyperextends {fl:.1f} deg at frame {f}")
            tw, sw = swing_twist_y(mat_to_quat(p.q["hand_" + s]))
            sa = math.degrees(2 * math.acos(min(1.0, abs(sw[0]))))
            worst_wrist = max(worst_wrist, sa)
            if sa >= WRIST_LIMIT:
                errors.append(f"wrist_{s} bends {sa:.1f} deg at frame {f}")
            if info.get("arm_miss_" + s, 0.0) > 0.03:
                warnings.append(f"arm_{s} short of its target by {info['arm_miss_' + s] * 100:.1f} cm at frame {f}")
    stats["max_wrist_deg"] = round(worst_wrist, 1)
    # ---------------------------------------------------------------- pops: no bone may jump > 40 deg in one frame
    pop, pop_b, pop_f = 0.0, "", 0
    body = [b for b in rig.ANIM_BONES if not any(k in b for k in ("index", "middle", "ring", "pinky", "thumb"))]
    for f in range(n):
        for b in body:
            a = angle_of(res.poses[f].q[b].T @ res.poses[f + 1].q[b])
            if a > pop:
                pop, pop_b, pop_f = a, b, f
    stats["max_step_deg"] = round(pop, 1)
    if pop > 40.0:
        errors.append(f"pop: {pop_b} turns {pop:.0f} deg between frames {pop_f} and {pop_f + 1}")
    # ---------------------------------------------------------------- loops
    if clip.loop:
        d0 = max(angle_of(res.poses[0].q[b].T @ res.poses[n].q[b]) for b in rig.ANIM_BONES)
        if d0 > 0.01 or np.abs(res.poses[0].pelvis_loc - res.poses[n].pelvis_loc).max() > 1e-5:
            errors.append(f"loop seam: last frame differs from the first ({d0:.3f} deg)")
        # velocity change across the seam must be no worse than the clip's own smoothest-possible interior
        # (the largest frame-to-frame velocity change inside the clip, e.g. a heel strike) and small in absolute terms
        worst, wb, ratio = 0.0, "", 0.0
        for b in rig.ANIM_BONES:
            va = res.poses[0].q[b].T @ res.poses[1].q[b]
            vb = res.poses[n - 1].q[b].T @ res.poses[n].q[b]
            seam = angle_of(va.T @ vb)
            if seam <= 0.25:
                continue
            inner = 0.0
            for f in range(1, n - 1):
                v0 = res.poses[f - 1].q[b].T @ res.poses[f].q[b]
                v1 = res.poses[f].q[b].T @ res.poses[f + 1].q[b]
                inner = max(inner, angle_of(v0.T @ v1))
            r = seam / max(inner, 0.25)
            if seam > worst:
                worst, wb = seam, b
            ratio = max(ratio, r)
            if seam > 1.0 and r > 1.5:
                errors.append(f"loop seam velocity pop on {b}: {seam:.2f} deg/frame (interior max {inner:.2f})")
        stats["seam_vel_deg"] = round(worst, 2)
        stats["seam_ratio"] = round(ratio, 2)
    # ---------------------------------------------------------------- contact = max extension
    vals = metric(clip, fk)
    if vals is not None and clip.contacts:
        c = clip.contacts[0]
        vc = vals[c]
        after = max(vals[c + 1:]) if c + 1 <= n else -1e9
        before = max(vals[:c]) if c > 0 else -1e9
        stats["contact_metric"] = round(vc, 3)
        if after > vc + 0.012:
            fa = c + 1 + int(np.argmax(vals[c + 1:]))
            errors.append(f"contact f{c}: frame {fa} extends further ({after:.3f} > {vc:.3f})")
        reach = (clip.metric or clip.strike) in ("hand_l", "hand_r", "hands", "foot_l", "foot_r")
        if reach and before > vc + 0.012:
            fb = int(np.argmax(vals[:c]))
            errors.append(f"contact f{c}: frame {fb} before contact extends further ({before:.3f} > {vc:.3f})")
    # ---------------------------------------------------------------- base pose
    for f, bname, label in ((0, clip.base, "start"), (n, clip.base_end, "end")):
        if not bname or bname not in dsl.BASES or (label == "end" and clip.end_pose):
            continue
        if (label == "start" and clip.start_pose_free) or not clip.base_check:
            continue
        bp, _ = solver.solve(dsl.BASES[bname], {})
        worst, wb = 0.0, ""
        for b in rig.ANIM_BONES:
            a = angle_of(bp.q[b].T @ res.poses[f].q[b])
            if a > worst:
                worst, wb = a, b
        dl = float(np.abs(bp.pelvis_loc - res.poses[f].pelvis_loc).max())
        stats[label + "_base_deg"] = round(worst, 3)
        if worst > BASE_TOL_DEG:
            errors.append(f"{label} pose differs from base '{bname}' by {worst:.2f} deg ({wb})")
        if dl > 0.003:
            errors.append(f"{label} pelvis differs from base '{bname}' by {dl * 1000:.1f} mm")
    # ---------------------------------------------------------------- balance (warning only)
    off = 0
    for f, ((W, Hd, T), st) in enumerate(zip(fk, res.states)):
        pl = [s for s in rig.SIDES if any(a <= f < b for a, b in res.plants.get(s, []))]
        if not pl or clip.no_balance:
            continue
        pts = []
        for s in pl:
            pts += [p[:2] for p in landmarks(W, Hd, s)]
        pts = np.array(pts)
        com = Hd["spine_01"][:2]
        lo, hi = pts.min(axis=0) - 0.08, pts.max(axis=0) + 0.08
        if not (lo[0] <= com[0] <= hi[0] and lo[1] <= com[1] <= hi[1]):
            off += 1
    if off:
        warnings.append(f"centre of mass outside the support box on {off} frames")
    return {"errors": errors[:20], "n_errors": len(errors), "warnings": warnings[:10], "stats": stats}
