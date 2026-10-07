"""Motion-capture clips from Epic's Game Animation Sample, retargeted onto the Fourfold fighter.

    import fourfold.animation.mocap as mm; mm.build(force=False)   -> report dict

Inputs: the GASP content copied into this project by Tools/gasp/migrate_from_gasp.py (UEFN mannequin mesh, its IK rig
and the locomotion / jump / ragdoll get-up animations under /Game/Characters/UEFN_Mannequin).

Outputs:
  * /Game/Fourfold/Characters/Fighter/Rigs/IK_Fighter            IK rig of SK_Fighter (auto humanoid retarget chains)
  * /Game/Fourfold/Characters/Fighter/Rigs/RTG_UEFN_to_Fighter    retargeter (default op stack, fuzzy chain map,
                                                                  target retarget pose auto-aligned to the source)
  * /Game/Fourfold/Characters/Fighter/Anims/Mocap/A_mm_<clip>     retargeted sequences (root locked, no root motion)
  * Content/Fourfold/Data/clips_mocap.json   clip rows for the native runtime: frames, loop, gait speed and the
                                             left-foot touchdown phase measured from the root motion, foot plants
  * Content/Fourfold/Data/anim_map_mocap.json  locomotion / reaction / air slots pointed at the mocap clips
Both JSON files are overlays: UFourfoldAnimLibrarySubsystem merges them over clips.json / anim_map.json when present.
"""
import json
import math
import os
import traceback

import unreal

SRC_ROOT = "/Game/Characters/UEFN_Mannequin"
SRC_MESH = SRC_ROOT + "/Meshes/SKM_UEFN_Mannequin"
SRC_RIG = SRC_ROOT + "/Rigs/IK_UEFN_Mannequin"
FIGHTER = "/Game/Fourfold/Characters/Fighter"
TGT_MESH = FIGHTER + "/SK_Fighter"
RIG_DIR = FIGHTER + "/Rigs"
IK_FIGHTER = RIG_DIR + "/IK_Fighter"
RTG = RIG_DIR + "/RTG_UEFN_to_Fighter"
OUT_DIR = FIGHTER + "/Anims/Mocap"
A = SRC_ROOT + "/Animations"

# our clip name -> (GASP sequence, loop, kind). kind: gait (stride-matched loop), loop, oneshot
CLIPS = {
    "mm_idle": (A + "/Idle/M_Neutral_Stand_Idle_Loop", True, "loop"),
    "mm_walk": (A + "/Walk/M_Neutral_Walk_Loop_F", True, "gait"),
    "mm_run": (A + "/Run/M_Neutral_Run_Loop_F", True, "gait"),
    "mm_strafe_l": (A + "/Walk/M_Neutral_Walk_Loop_LL", True, "gait"),
    "mm_strafe_r": (A + "/Walk/M_Neutral_Walk_Loop_RR", True, "gait"),
    "mm_walk_back": (A + "/Walk/M_Neutral_Walk_Loop_B", True, "gait"),
    "mm_getup_f": (A + "/Ragdoll/M_ragdoll_getup_stand_F", False, "getup"),
    "mm_getup_b": (A + "/Ragdoll/M_ragdoll_getup_stand_B", False, "getup"),
}
# slots of anim_map.json that the overlay re-points (only for clips that were produced)
MAP = {
    "locomotion": {"walk": "mm_walk", "run": "mm_run", "strafe_l": "mm_strafe_l", "strafe_r": "mm_strafe_r",
                   "back": "mm_walk_back"},
    "reactions": {"getup": "mm_getup_f", "getup_back": "mm_getup_b"},
}
FALLBACK = {"mm_idle": "idle", "mm_walk": "walk", "mm_run": "run", "mm_strafe_l": "strafe_l", "mm_strafe_r": "strafe_r",
            "mm_walk_back": "walk_back", "mm_getup_f": "getup", "mm_getup_b": "getup"}
EAL = unreal.EditorAssetLibrary


def _log(msg):
    unreal.log(f"[Fourfold][mocap] {msg}")


def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _data_dir():
    return os.path.join(unreal.Paths.project_content_dir(), "Fourfold", "Data")


def _load(path):
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else None


def _ik_rig_for_fighter(force, rep):
    rig = _load(IK_FIGHTER)
    if rig is not None and not force:
        rep["skipped"].append(IK_FIGHTER)
        return rig
    if rig is None:
        EAL.make_directory(RIG_DIR)
        rig = _tools().create_asset("IK_Fighter", RIG_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    ctrl = unreal.IKRigController.get_controller(rig)
    if not ctrl.set_skeletal_mesh(_load(TGT_MESH)):
        raise RuntimeError("IK_Fighter: set_skeletal_mesh(SK_Fighter) failed")
    if not ctrl.apply_auto_generated_retarget_definition():
        rep["notes"].append("IK_Fighter: auto retarget definition reported failure (chains may be incomplete)")
    EAL.save_loaded_asset(rig)
    rep["created"].append(IK_FIGHTER)
    return rig


def _retargeter(src_rig, tgt_rig, force, rep):
    rtg = _load(RTG)
    if rtg is not None and not force:
        rep["skipped"].append(RTG)
        return rtg
    if rtg is None:
        rtg = _tools().create_asset("RTG_UEFN_to_Fighter", RIG_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(S, src_rig)
    c.set_ik_rig(T, tgt_rig)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    c.assign_ik_rig_to_all_ops(S, src_rig)
    c.assign_ik_rig_to_all_ops(T, tgt_rig)
    c.set_preview_mesh(S, _load(SRC_MESH))
    c.set_preview_mesh(T, _load(TGT_MESH))
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # match the fighter's retarget pose to the mannequin's A-pose chain by chain (rotations copy 1:1 afterwards)
    c.auto_align_all_bones(T, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    EAL.save_loaded_asset(rtg)
    rep["created"].append(RTG)
    ops = [str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())]
    rep["notes"].append(f"RTG ops: {ops}")
    return rtg


def _foot_positions(seq, n, dt):
    """World-space (component) ball positions per frame for both feet, root motion kept in the pose (cm)."""
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("extract_root_motion", False)
    opts.set_editor_property("incorporate_root_motion_into_pose", True)
    opts.set_editor_property("optional_skeletal_mesh", _load(TGT_MESH))
    out = {"l": [], "r": []}
    for i in range(n):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, i * dt, opts)
        for s in ("l", "r"):
            out[s].append(unreal.AnimPoseExtensions.get_bone_pose(pose, f"ball_{s}", unreal.AnimPoseSpaces.WORLD).translation)
    return out


def _analyse(seq, frames, length, loop):
    """Gait speed (m/s), left-foot touchdown phase and planted frame ranges from the root motion."""
    n = max(2, frames)
    dt = length / (n - 1) if n > 1 else 1.0 / 60.0
    root0 = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "root", 0.0, False).translation
    root1 = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "root", length, False).translation
    speed = math.hypot(root1.x - root0.x, root1.y - root0.y) / 100.0 / max(length, 1e-3)
    pos = _foot_positions(seq, n, dt)
    plants, touch, cycles = {}, 0.0, 1
    for s, ps in pos.items():
        zmin = min(p.z for p in ps)
        planted = []
        for i in range(n):
            a, b = ps[max(0, i - 1)], ps[min(n - 1, i + 1)]
            v = math.sqrt((b.x - a.x) ** 2 + (b.y - a.y) ** 2 + (b.z - a.z) ** 2) / (2 * dt) / 100.0   # m/s
            planted.append(ps[i].z < zmin + 4.0 and v < 0.35)
        ranges, start = [], None
        for i, p in enumerate(planted + [False]):
            if p and start is None:
                start = i
            elif not p and start is not None:
                if i - start >= 2:
                    ranges.append([start, i])
                start = None
        plants[s] = ranges
        if s == "l" and ranges:
            # touchdown = first planted frame whose previous frame is not planted (wraps for loops)
            downs = [r[0] for r in ranges if r[0] > 0] or [ranges[0][0]]
            touch = downs[0] / max(n - 1, 1)
            if len(downs) >= 2:
                gaps = sorted(b - a for a, b in zip(downs, downs[1:]))
                cycles = max(1, int(round((n - 1) / gaps[len(gaps) // 2])))
    return speed, touch, plants, cycles


def _getup_trim(seq, length):
    """The active part of a get-up: from the first lift of the head / pelvis off the floor to standing (seconds)."""
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("optional_skeletal_mesh", _load(TGT_MESH))
    n = int(length * 30) + 1
    ph, hh = [], []
    for i in range(n):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, min(length, i / 30.0), opts)
        ph.append(unreal.AnimPoseExtensions.get_bone_pose(pose, "pelvis", unreal.AnimPoseSpaces.WORLD).translation.z)
        hh.append(unreal.AnimPoseExtensions.get_bone_pose(pose, "head", unreal.AnimPoseSpaces.WORLD).translation.z)
    p0, h0, p1, h1 = min(ph[:15]), min(hh[:15]), ph[-1], hh[-1]
    frac = [max((ph[i] - p0) / max(p1 - p0, 1.0), (hh[i] - h0) / max(h1 - h0, 1.0)) for i in range(n)]
    i0 = next((i for i in range(n) if frac[i] > 0.06), 0)
    i1 = next((i for i in range(i0, n) if ph[i] >= p0 + 0.97 * (p1 - p0) and hh[i] >= h0 + 0.97 * (h1 - h0)), n - 1)
    t0, t1 = max(0.0, (i0 - 2) / 30.0), min(length, (i1 + 3) / 30.0)
    # lying pose at the start of the active part (component space, cm): pelvis position and pelvis -> head direction,
    # used at runtime to line the clip up with where the ragdoll actually lies
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t0, opts)
    pv = unreal.AnimPoseExtensions.get_bone_pose(pose, "pelvis", unreal.AnimPoseSpaces.WORLD).translation
    hv = unreal.AnimPoseExtensions.get_bone_pose(pose, "head", unreal.AnimPoseSpaces.WORLD).translation
    d = (hv.x - pv.x, hv.y - pv.y)
    n = max((d[0] ** 2 + d[1] ** 2) ** 0.5, 1e-3)
    lie = {"pelvis": [round(pv.x, 2), round(pv.y, 2)], "dir": [round(d[0] / n, 4), round(d[1] / n, 4)]}
    return t0, t1, lie


def build(force=False):
    rep = {"created": [], "skipped": [], "failed": [], "notes": []}
    try:
        src_rig, src_mesh, tgt_mesh = _load(SRC_RIG), _load(SRC_MESH), _load(TGT_MESH)
        if not (src_rig and src_mesh and tgt_mesh):
            rep["failed"].append({"item": "inputs", "error": "GASP content or SK_Fighter missing (run Tools/gasp/migrate_from_gasp.py)"})
            return rep
        tgt_rig = _ik_rig_for_fighter(force, rep)
        rtg = _retargeter(src_rig, tgt_rig, force, rep)
        EAL.make_directory(OUT_DIR)
        todo = {k: v for k, v in CLIPS.items() if force or not EAL.does_asset_exist(f"{OUT_DIR}/A_{k}")}
        for clip, (src, _loop, _kind) in todo.items():
            ad = EAL.find_asset_data(src)
            if not ad.is_valid():
                rep["failed"].append({"item": clip, "error": f"source {src} not found"})
                continue
            out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
                [ad], src_mesh, tgt_mesh, rtg, "", "", "", "", OUT_DIR, False, False, True)
            made = [o for o in out if isinstance(o.get_asset(), unreal.AnimSequence)]
            if not made:
                rep["failed"].append({"item": clip, "error": "retarget produced nothing"})
                continue
            src_obj = str(made[0].package_name)
            dst = f"{OUT_DIR}/A_{clip}"
            if src_obj != dst:
                if EAL.does_asset_exist(dst):
                    EAL.delete_asset(dst)
                EAL.rename_asset(src_obj, dst)
            rep["created"].append(dst)
        rows, have = {}, set()
        for clip, (src, loop, kind) in CLIPS.items():
            seq = _load(f"{OUT_DIR}/A_{clip}")
            if seq is None:
                continue
            have.add(clip)
            length = float(seq.get_play_length())
            frames = int(round(length * 60.0))
            if kind == "gait":
                seq.set_editor_property("force_root_lock", False)       # measure with the root travelling
                speed, touch, plants, cycles = _analyse(seq, frames, length, loop)
            else:
                speed, touch, plants, cycles = 0.0, 0.0, {}, 1
            # in place: the native runtime moves the fighter from the sim; the clip must not drag the root
            for prop, val in (("enable_root_motion", False), ("force_root_lock", True)):
                try:
                    seq.set_editor_property(prop, val)
                except Exception as e:  # noqa: BLE001
                    rep["notes"].append(f"A_{clip}: {prop} not set ({e})")
            EAL.save_loaded_asset(seq)
            row = {"asset": f"Mocap/A_{clip}", "frames": frames, "duration": round(length, 5), "loop": loop,
                   "contact": None, "contacts": [], "speed": round(speed, 3), "hands": {"l": "", "r": ""},
                   "priority": "P0", "technique": f"mocap (Game Animation Sample): {src.rsplit('/', 1)[1]}"}
            if kind == "gait":
                row["phase0"] = round(touch, 4)
                row["cycles"] = cycles
                row["foot_plants"] = plants
                rep["notes"].append(f"A_{clip}: {length:.2f}s {cycles} cycles speed {speed:.2f} m/s, L touchdown at {touch:.2f}, "
                                    f"plants {plants}")
            if kind == "getup":
                t0, t1, lie = _getup_trim(seq, length)
                row["trim"] = [round(t0, 3), round(t1, 3)]
                row["lie"] = lie
                rep["notes"].append(f"A_{clip}: get-up active part {row['trim']}")
            rows[clip] = row
        data = _data_dir()
        with open(os.path.join(data, "clips_mocap.json"), "w") as f:
            json.dump({"schema": "fourfold.clips/1", "fps": 60, "asset_root": "/Game/Fourfold/Characters/Fighter/Anims",
                       "clips": rows}, f, indent=1)
        amap = {"schema": "fourfold.anim_map/1"}
        for section, slots in MAP.items():
            amap[section] = {k: v for k, v in slots.items() if v in have}
        # a checkout without the (git-ignored) GASP content keeps the hand-keyed clips
        amap["fallbacks"] = {"clips": {c: FALLBACK[c] for c in have if c in FALLBACK}}
        with open(os.path.join(data, "anim_map_mocap.json"), "w") as f:
            json.dump(amap, f, indent=1)
        rep["notes"].append(f"{len(rows)} mocap clips written to clips_mocap.json")
    except Exception as e:  # noqa: BLE001
        rep["failed"].append({"item": "mocap", "error": f"{e}\n{traceback.format_exc()}"})
    _log(json.dumps({k: (v if k != "skipped" else len(v)) for k, v in rep.items()})[:4000])
    return rep
