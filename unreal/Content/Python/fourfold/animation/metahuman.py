"""Every fighter clip on the MetaHuman skeleton (setup part `metahuman`).

    import fourfold.animation.metahuman as mh; mh.build(force=False)   -> report dict

Input: Content/Fourfold/Data/metahuman.json + the MetaHuman parts copied by Tools/gasp/migrate_metahuman.py, the
fighter clips (/Game/Fourfold/Characters/Fighter/Anims, incl. Mocap/) and the GASP mannequin (mocap source).
Output (/Game/Fourfold/Characters/MetaHuman):
  * Rigs/IK_MH (auto humanoid retarget chains of the MetaHuman body), Rigs/RTG_Fighter_to_MH, Rigs/RTG_UEFN_to_MH
  * Anims/A_<clip>          every hand-keyed clip, same names (retargeted from SKEL_Fighter)
  * Anims/Mocap/A_mm_<clip> the mocap clips retargeted straight from the GASP mannequin (better than a second hop)
  * metahuman.json gets "anim_root"; the runtime then uses the MetaHuman body + parts and this clip set.
"""
import json
import os
import traceback

import unreal

from . import mocap as MC

EAL = unreal.EditorAssetLibrary
FIGHTER_ANIMS = "/Game/Fourfold/Characters/Fighter/Anims"
MH_ROOT = "/Game/Fourfold/Characters/MetaHuman"
RIGS = MH_ROOT + "/Rigs"
ANIMS = MH_ROOT + "/Anims"


def _log(msg):
    unreal.log(f"[Fourfold][metahuman] {msg}")


def _data_path():
    return os.path.join(unreal.Paths.project_content_dir(), "Fourfold", "Data", "metahuman.json")


def _load(path):
    return unreal.load_asset(path) if path and EAL.does_asset_exist(path) else None


def _ik_rig(path, mesh, force, rep):
    rig = _load(path)
    if rig is not None and not force:
        rep["skipped"].append(path)
        return rig
    if rig is None:
        EAL.make_directory(path.rsplit("/", 1)[0])
        rig = unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit("/", 1)[1], path.rsplit("/", 1)[0],
                                                                       unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    if not c.set_skeletal_mesh(mesh):
        raise RuntimeError(f"{path}: set_skeletal_mesh failed")
    if not c.apply_auto_generated_retarget_definition():
        rep["notes"].append(f"{path}: auto retarget definition reported failure")
    EAL.save_loaded_asset(rig)
    rep["created"].append(path)
    return rig


def _rtg(path, src_rig, src_mesh, tgt_rig, tgt_mesh, force, rep):
    rtg = _load(path)
    if rtg is not None and not force:
        rep["skipped"].append(path)
        if MC.fix_root_motion_op(rtg, rep):
            rep.setdefault("_root_fixed", []).append(path)
        return rtg
    if rtg is None:
        rtg = unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit("/", 1)[1], path.rsplit("/", 1)[0],
                                                                       unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(S, src_rig)
    c.set_ik_rig(T, tgt_rig)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    c.assign_ik_rig_to_all_ops(S, src_rig)
    c.assign_ik_rig_to_all_ops(T, tgt_rig)
    c.set_preview_mesh(S, src_mesh)
    c.set_preview_mesh(T, tgt_mesh)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    c.auto_align_all_bones(T, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    EAL.save_loaded_asset(rtg)
    MC.fix_root_motion_op(rtg, rep)
    rep["created"].append(path)
    return rtg


def _retarget(assets, src_mesh, tgt_mesh, rtg, out_dir, rep):
    """duplicate_and_retarget in batches; outputs keep the source asset names."""
    EAL.make_directory(out_dir)
    made = 0
    for i in range(0, len(assets), 24):
        out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            assets[i:i + 24], src_mesh, tgt_mesh, rtg, "", "", "", "", out_dir, False, False, True)
        made += sum(1 for o in out if isinstance(o.get_asset(), unreal.AnimSequence))
    return made


MATS = MH_ROOT + "/Materials"
TINTED = ("Torso", "Legs", "Feet")
# outfit (owner's choice 2026-10-07): bare-chested, barefoot martial-arts training look. The pants stay (tinted per role).
OUTFIT_OFF = ("Torso", "Feet")
# Kellan's body mesh only keeps what his clothes leave visible (neck, hands), so the fighters render the complete
# body of the same type (m_med_nrw, same skeleton and body UVs) with Kellan's skin material and physics asset.
FULL_BODY_SRC = "/Game/MetaHumans/Common/Common/Mocap/m_med_nrw_body_mocap"
FULL_BODY = MH_ROOT + "/SKM_FF_Body"


def _full_body(body, materials, force, rep):
    if not EAL.does_asset_exist(FULL_BODY_SRC):
        rep["notes"].append(f"{FULL_BODY_SRC} missing: the fighters keep Kellan's partial body (no bare chest)")
        return None
    if EAL.does_asset_exist(FULL_BODY) and not force:
        rep["skipped"].append(FULL_BODY)
        return FULL_BODY
    if EAL.does_asset_exist(FULL_BODY):
        EAL.delete_asset(FULL_BODY)
    m = EAL.duplicate_asset(FULL_BODY_SRC, FULL_BODY)
    m.set_editor_property("physics_asset", body.get_editor_property("physics_asset"))
    mi = _load(materials[0]) if materials else None
    if mi is not None:
        slots = m.get_editor_property("materials")
        for sm in slots:
            sm.set_editor_property("material_interface", mi)
        m.set_editor_property("materials", slots)
    EAL.save_loaded_asset(m)
    rep["created"].append(FULL_BODY)
    return FULL_BODY
# clothing tint per role (multiplies the baked fabric colour); the sim roles must read apart at a glance
TINTS = {"player": [0.45, 0.7, 1.6], "rival": [1.6, 0.5, 0.42], "dummy": [1.0, 0.95, 0.85]}


def _no_neck_hide(face_path, force, rep):
    """Per-slot materials for the face: copies of the head materials with bUseNeckHide off. Kellan's head fades its
    neck / shoulder skirt for the hoodie; bare-chested that shows as a pale patch over the shoulders. Slots that need no
    change stay empty (the runtime keeps the mesh's own material there)."""
    mel = unreal.MaterialEditingLibrary
    face = _load(face_path)
    if face is None:
        return []
    out = []
    for sm in face.get_editor_property("materials"):
        mi = sm.material_interface
        if not isinstance(mi, unreal.MaterialInstanceConstant) or not mel.get_material_instance_static_switch_parameter_value(mi, "bUseNeckHide"):
            out.append("")
            continue
        dst = f"{MATS}/{mi.get_name()}_FF"
        if not EAL.does_asset_exist(dst) or force:
            if EAL.does_asset_exist(dst):
                EAL.delete_asset(dst)
            EAL.make_directory(MATS)
            cp = EAL.duplicate_asset(mi.get_path_name().split(".")[0], dst)
            mel.set_material_instance_static_switch_parameter_value(cp, "bUseNeckHide", False)
            mel.update_material_instance(cp)
            EAL.save_loaded_asset(cp)
            rep["created"].append(dst)
        out.append(dst)
    return out


def _tintable(src_path, force, rep):
    """Copy of a clothing material instance whose base material multiplies base colour by vector param FF_Tint."""
    mel = unreal.MaterialEditingLibrary
    src = _load(src_path)
    if src is None:
        return None
    base = src.get_base_material()
    base_dst = f"{MATS}/M_FF_{base.get_name()}"
    mi_dst = f"{MATS}/{src.get_name()}_FF"
    if EAL.does_asset_exist(mi_dst) and not force:
        rep["skipped"].append(mi_dst)
        return mi_dst
    EAL.make_directory(MATS)
    if not EAL.does_asset_exist(base_dst) or force:
        if EAL.does_asset_exist(base_dst):
            EAL.delete_asset(base_dst)
        mat = EAL.duplicate_asset(base.get_path_name().split(".")[0], base_dst)
        prop = unreal.MaterialProperty.MP_BASE_COLOR
        node = mel.get_material_property_input_node(mat, prop)
        out = mel.get_material_property_input_node_output_name(mat, prop)
        if node is None:
            raise RuntimeError(f"{base_dst}: no base colour input")
        tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -300)
        tint.set_editor_property("parameter_name", "FF_Tint")
        tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        # desaturate the baked fabric colour first (Kellan's hoodie is mustard), then tint: roles read blue / red
        des = mel.create_material_expression(mat, unreal.MaterialExpressionDesaturation, -450, -200)
        mel.connect_material_expressions(node, out, des, "")
        mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -200)
        mel.connect_material_expressions(des, "", mul, "A")
        mel.connect_material_expressions(tint, "", mul, "B")
        mel.connect_material_property(mul, "", prop)
        mel.recompile_material(mat)
        EAL.save_loaded_asset(mat)
        rep["created"].append(base_dst)
    if EAL.does_asset_exist(mi_dst):
        EAL.delete_asset(mi_dst)
    mi = EAL.duplicate_asset(src_path, mi_dst)
    mel.set_material_instance_parent(mi, _load(base_dst))
    mel.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    rep["created"].append(mi_dst)
    return mi_dst


def build(force=False):
    rep = {"created": [], "skipped": [], "failed": [], "notes": []}
    try:
        with open(_data_path()) as f:
            data = json.load(f)
    except (OSError, ValueError):
        rep["notes"].append("metahuman.json not present: run Tools/gasp/migrate_metahuman.py (MetaHuman skipped)")
        return rep
    try:
        body = _load(data.get("body"))
        fighter = _load(MC.TGT_MESH)
        uefn = _load(MC.SRC_MESH)
        if body is None or fighter is None:
            rep["failed"].append({"item": "inputs", "error": f"body {data.get('body')} or SK_Fighter missing"})
            return rep
        ik_mh = _ik_rig(RIGS + "/IK_MH", body, force, rep)
        ik_fighter = _load(MC.IK_FIGHTER) or _ik_rig(MC.IK_FIGHTER, fighter, False, rep)
        rtg_f = _rtg(RIGS + "/RTG_Fighter_to_MH", ik_fighter, fighter, ik_mh, body, force, rep)
        # hand-keyed clips (and hand poses): same names under ANIMS
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        src = [a for a in reg.get_assets_by_path(FIGHTER_ANIMS, recursive=False)
               if str(a.asset_class_path.asset_name) == "AnimSequence"]
        fixed = rep.get("_root_fixed", [])
        todo = [a for a in src if force or rtg_f.get_path_name().split(".")[0] in fixed
                or not EAL.does_asset_exist(f"{ANIMS}/{a.asset_name}")]
        n = _retarget(todo, fighter, body, rtg_f, ANIMS, rep) if todo else 0
        rep["notes"].append(f"{n} fighter clips retargeted ({len(src) - len(todo)} already there)")
        # mocap straight from the GASP mannequin
        if uefn is not None:
            ik_uefn = _load(MC.SRC_RIG)
            rtg_u = _rtg(RIGS + "/RTG_UEFN_to_MH", ik_uefn, uefn, ik_mh, body, force, rep)
            m = 0
            for clip, (srcp, _loop, kind) in MC.CLIPS.items():
                dst = f"{ANIMS}/Mocap/A_{clip}"
                if EAL.does_asset_exist(dst) and not force and rtg_u.get_path_name().split(".")[0] not in rep.get("_root_fixed", []):
                    continue
                ad = EAL.find_asset_data(srcp)
                if not ad.is_valid():
                    continue
                out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
                    [ad], uefn, body, rtg_u, "", "", "", "", ANIMS + "/Mocap", False, False, True)
                made = [o for o in out if isinstance(o.get_asset(), unreal.AnimSequence)]
                if not made:
                    continue
                cur = str(made[0].package_name)
                if cur != dst:
                    if EAL.does_asset_exist(dst):
                        EAL.delete_asset(dst)
                    EAL.rename_asset(cur, dst)
                seq = _load(dst)
                for prop, val in (("enable_root_motion", False), ("force_root_lock", True)):
                    try:
                        seq.set_editor_property(prop, val)
                    except Exception:  # noqa: BLE001
                        pass
                EAL.save_loaded_asset(seq)
                m += 1
            rep["notes"].append(f"{m} mocap clips retargeted from the GASP mannequin")
        # tintable clothing (per-role colour at runtime)
        for part in data.get("parts", []):
            if part.get("kind") == "mesh" and part.get("name") in TINTED and (part.get("source_materials") or part.get("materials")):
                # keep the MetaHuman's own materials as the source (re-runs must not tint a tinted copy)
                part.setdefault("source_materials", part["materials"])
                part["materials"] = [(_tintable(m, force, rep) if m else None) for m in part["source_materials"]]
                part["tint_param"] = "FF_Tint"
            part["enabled"] = part.get("name") not in OUTFIT_OFF
            if part.get("name") == "Face" and part.get("kind") == "mesh":
                part["materials"] = _no_neck_hide(part["asset"], force, rep)
        body_mats = next((p.get("materials") for p in data.get("parts", []) if p.get("name") == "Body"), None)
        full = _full_body(body, body_mats, force, rep)
        if full:
            data["body_mesh"] = full
        else:
            data.pop("body_mesh", None)
        data["tints"] = TINTS
        data["anim_root"] = ANIMS
        data["skeleton"] = body.get_editor_property("skeleton").get_path_name().split(".")[0]
        with open(_data_path(), "w") as f:
            json.dump(data, f, indent=1)
    except Exception as e:  # noqa: BLE001
        rep["failed"].append({"item": "metahuman", "error": f"{e}\n{traceback.format_exc()}"})
    rep.pop("_root_fixed", None)
    _log(json.dumps({k: (v if k != "skipped" else len(v)) for k, v in rep.items()})[:3000])
    return rep
