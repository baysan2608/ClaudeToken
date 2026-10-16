"""Fourfold Jungle Ruins map - editor setup (setup part "ruins"; owner: open-world stream).

Builds /Game/Fourfold/Maps/L_Ruins from the Blender prep (unreal/Tools/blender/ruins/prep_ruins.py ->
unreal/SourceArt/OpenWorld/JungleRuins/Game/layout.json + FBX + PNG):
- /Game/Fourfold/Ruins/Textures/T_Ruins_*      BC sRGB, N normal maps (DirectX), ORM masks, OP opacity
- /Game/Fourfold/Ruins/Materials/MI_Ruins_*    instances of M_Env_Surface / M_Env_Water / M_FF_TreeLeaf / M_FF_TreeBark
- /Game/Fourfold/Ruins/Meshes|Vegetation/SM_Ruins_*   LOD0 + LOD1 / LOD2; ground, backdrop, stone and water keep per-poly
                                               collision data (the bake traces it; FX debris lands on the ground), plants none
- the level: 3 x 3 terrain chunks, the backdrop, the pyramid, river / creek surfaces (all at the origin: the prep wrote them
  in map space), the Lab lighting rig re-tuned for a humid jungle (jungle()), and FF_RuinsWorld, an AFourfoldOpenWorld that
  reads Content/Fourfold/Data/ruins (unreal/Tools/ruins/bake_ruins.py) and instances the jungle from its world.json props.
Credit (CC BY 4.0): "Jungle Ruins" by Cristiano Siqueira, with Charlene Teets / Intel Sample Library (unreal/docs/CREDITS.md).
LODs 1-2 need the full editor: run unreal/Tools/ruins/editor_setup_ruins.py with -ExecutePythonScript.

  import fourfold_setup; fourfold_setup.main(only="ruins")      # force=True rebuilds
"""
import json
import os
import sys

import unreal

from fourfold.world import common as C
from fourfold.world import foliage as FO
from fourfold.world import meshes as WM

ROOT = "/Game/Fourfold/Ruins"
TEX_DIR = ROOT + "/Textures"
MAT_DIR = ROOT + "/Materials"
MESH_DIR = ROOT + "/Meshes"
VEG_DIR = ROOT + "/Vegetation"
MAP_PATH = "/Game/Fourfold/Maps/L_Ruins"
MASTERS = {"surface": C.MAT_DIR + "/M_Env_Surface", "water": C.MAT_DIR + "/M_Env_Water",
           "leaf": FO.TREE_DIR + "/M_FF_TreeLeaf", "bark": FO.TREE_DIR + "/M_FF_TreeBark"}
TEX_PARAMS = {"BC": "BaseColor", "N": "Normal", "ORM": "ORM", "OP": "Opacity"}
LOD_SCREEN = {"ground": [1.0, 0.35, 0.12], "stone": [1.0, 0.4, 0.15], "veg": [0.5, 0.18, 0.06]}
ROLE_TAGS = {"ground": ("FFRuinsGround",), "backdrop": ("FFRuinsGround", "FFRuinsBackdrop"),
             "stone": ("FFRuinsGround", "FFRuinsStone"), "water": ("FFRuinsWater",)}
MEL = unreal.MaterialEditingLibrary
# humid jungle light: the sun stands high behind the opening view (the spawn looks north-west at the pyramid), warm haze
SUN_PITCH, SUN_YAW = -48.0, -94.0
SUN_COLOR = (255, 236, 205)


def _report():
    return {"created": [], "skipped": [], "failed": [], "notes": []}


def _proj():
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def _spec():
    tools = os.path.join(_proj(), "Tools", "ruins")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import ruins_spec
    return ruins_spec


def _src():
    return os.path.join(_proj(), "SourceArt", "OpenWorld", "JungleRuins", "Game")


def _import_textures(src, force, rep):
    tasks = []
    for f in sorted(f for f in os.listdir(src) if f.startswith("T_Ruins_") and f.endswith(".png")):
        name = f[:-4]
        dst = f"{TEX_DIR}/{name}"
        if C.asset_exists(dst) and not force:
            rep["skipped"].append(dst)
            continue
        t = unreal.AssetImportTask()
        for k, v in (("filename", os.path.join(src, f)), ("destination_path", TEX_DIR), ("destination_name", name),
                     ("replace_existing", True), ("automated", True), ("save", False)):
            t.set_editor_property(k, v)
        tasks.append((t, dst, name))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _d, _n in tasks])
    for _t, dst, name in tasks:
        tex = unreal.load_asset(dst) if C.asset_exists(dst) else None
        if tex is None:
            rep["failed"].append({"item": dst, "error": "import produced nothing"})
            continue
        kind = ("normal" if name.endswith("_N") else "masks" if name.endswith("_ORM") else
                "opacity" if name.endswith("_OP") else "color")
        FO._tex_settings(tex, kind, rep)
        C.save_asset(tex)
        rep["created"].append(dst)


def _materials(man, force, rep):
    masters = {k: unreal.load_asset(p) if C.asset_exists(p) else None for k, p in MASTERS.items()}
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mis = {}
    for mesh in man["meshes"].values():
        for slot, d in mesh["slots"].items():
            if slot in mis:
                continue
            name = f"MI_Ruins_{slot}"
            path = f"{MAT_DIR}/{name}"
            parent = masters.get(d["master"])
            if parent is None:
                rep["failed"].append({"item": path, "error": f"master {MASTERS.get(d['master'])} missing (run the world part)"})
                continue
            if C.asset_exists(path) and not force:
                mis[slot] = unreal.load_asset(path)
                rep["skipped"].append(path)
                continue
            mi = unreal.load_asset(path) if C.asset_exists(path) else tools.create_asset(
                name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mi, parent)
            for key, param in TEX_PARAMS.items():
                if key not in d:
                    continue
                tpath = f"{TEX_DIR}/{d[key]}"
                tex = unreal.load_asset(tpath) if C.asset_exists(tpath) else None
                if tex is None:
                    rep["notes"].append(f"{name}: texture {d[key]} missing")
                    continue
                MEL.set_material_instance_texture_parameter_value(mi, param, tex)
            for k, v in d.get("scalars", {}).items():
                MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
            for k, v in d.get("vectors", {}).items():
                MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(float(v[0]), float(v[1]), float(v[2]), 1.0))
            MEL.update_material_instance(mi)
            C.save_asset(mi)
            mis[slot] = mi
            rep["created"].append(path)
    return mis


def _complex_collision(sm, rep):
    """Per-poly collision data (the bake's traces, FX debris); components decide whether it is used."""
    bs = C.get_prop(sm, "body_setup")
    if bs is None:
        sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        sms.add_simple_collisions(sm, unreal.ScriptCollisionShapeType.BOX)
        bs = C.get_prop(sm, "body_setup")
    if bs is None or not C.set_prop(bs, "collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE, rep):
        rep["failed"].append({"item": sm.get_name(), "error": "could not set complex-as-simple collision"})


def _import_mesh(name, entry, dest, mis, force, rep):
    dst = f"{dest}/{name}"
    if C.asset_exists(dst) and not force:
        rep["skipped"].append(dst)
        return
    pipe = WM._pipeline(name, rep)
    stack = unreal.InterchangePipelineStackOverride()
    if hasattr(stack, "add_pipeline"):
        stack.add_pipeline(pipe)
    else:
        stack.get_editor_property("override_pipelines").append(pipe)
    t = unreal.AssetImportTask()
    for k, v in (("filename", os.path.join(_src(), entry["lods"][0]["fbx"])), ("destination_path", dest),
                 ("destination_name", name), ("replace_existing", True), ("automated", True), ("save", False), ("options", stack)):
        t.set_editor_property(k, v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    sm = unreal.load_asset(dst) if C.asset_exists(dst) else None
    if sm is None:
        rep["failed"].append({"item": dst, "error": "no static mesh after import"})
        return
    for i, lod in enumerate(entry["lods"][1:], start=1):
        FO._import_lod(sm, i, os.path.join(_src(), lod["fbx"]), rep)
    if len(entry["lods"]) > 1:
        try:
            C.set_prop(sm, "auto_compute_lod_screen_size", False, rep, quiet=True)
            unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_lod_screen_sizes(
                sm, LOD_SCREEN[entry["role"]][:len(entry["lods"])])
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"{name}: LOD screen sizes not set ({e})")
    if entry["role"] == "veg":
        WM._remove_collision(sm, rep)
    else:
        _complex_collision(sm, rep)
    slots = list(sm.get_editor_property("static_materials") or [])
    for s in slots:
        nm = str(s.get_editor_property("material_slot_name")).split(".")[0]
        if nm in mis:
            s.set_editor_property("material_interface", mis[nm])
        else:
            rep["notes"].append(f"{name}: slot {nm} without a material")
    sm.set_editor_property("static_materials", slots)
    C.save_asset(sm)
    rep["created"].append(dst)


def _block_all(comp, rep):
    try:
        comp.set_collision_profile_name("BlockAll")
        comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    except Exception as e:  # noqa: BLE001
        rep["failed"].append({"item": comp.get_name(), "error": f"collision: {e}"})


def jungle(b, rep):
    """The Lab rig (Builder.build_lighting) re-tuned: high warm sun over the canopy, humid green-grey haze, a sky light
    that fills the shade, first-person-friendly post (little motion blur). Exposure stays manual (bias only)."""
    P = C.set_prop
    sun_a = b.actors.get("FF_Sun")
    if sun_a is not None:
        sun_a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=SUN_PITCH, yaw=SUN_YAW), False)
        sc = sun_a.get_component_by_class(unreal.DirectionalLightComponent)
        for k, v in (("intensity", 9.0), ("light_color", unreal.Color(*SUN_COLOR, 255)),
                     ("dynamic_shadow_distance_movable_light", 6000.0), ("dynamic_shadow_cascades", 3),
                     ("enable_light_shaft_bloom", True), ("bloom_scale", 0.2), ("bloom_threshold", 0.7),
                     ("volumetric_scattering_intensity", 1.6), ("light_source_angle", 1.0)):
            P(sc, k, v, rep, quiet=True)
    sky = b.lights.get("sky")
    if sky is not None:
        for k, v in (("intensity", 1.25), ("lower_hemisphere_color", unreal.LinearColor(0.07, 0.09, 0.05, 1.0))):
            P(sky, k, v, rep, quiet=True)
    atm = b.actors.get("FF_SkyAtmosphere")
    if atm is not None:
        ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
        for k, v in (("mie_scattering_scale", 0.02), ("mie_anisotropy", 0.8), ("aerial_pespective_view_distance_scale", 2.0)):
            P(ac, k, v, rep, quiet=True)
    fog = b.actors.get("FF_Fog")
    if fog is not None:
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        for k, v in (("fog_density", 0.02), ("fog_height_falloff", 0.12), ("start_distance", 400.0), ("fog_max_opacity", 0.9),
                     ("fog_inscattering_luminance", unreal.LinearColor(0.10, 0.13, 0.10, 1.0)),
                     ("directional_inscattering_luminance", unreal.LinearColor(0.75, 0.62, 0.38, 1.0)),
                     ("directional_inscattering_exponent", 8.0), ("enable_volumetric_fog", True),
                     ("volumetric_fog_scattering_distribution", 0.6), ("volumetric_fog_albedo", unreal.Color(232, 245, 226, 255)),
                     ("volumetric_fog_extinction_scale", 0.7), ("volumetric_fog_distance", 6000.0)):
            P(fc, k, v, rep, quiet=True)
        fog.set_actor_location(unreal.Vector(0.0, 0.0, -1000.0), False, False)   # haze base at the river (world y -10)
    ppv = b.actors.get("FF_Post")
    if ppv is not None:
        try:
            st = ppv.get_editor_property("settings")
            for k, v in (("auto_exposure_bias", 0.4), ("motion_blur_amount", 0.15), ("vignette_intensity", 0.3),
                         ("film_grain_intensity", 0.06), ("bloom_intensity", 0.55), ("white_temp", 6000.0),
                         ("color_saturation", unreal.Vector4(1.0, 1.03, 0.97, 1.0)),
                         ("ambient_occlusion_intensity", 0.85), ("ambient_occlusion_radius", 160.0)):
                P(st, "override_" + k, True, rep, quiet=True)
                P(st, k, v, rep, quiet=True)
            ppv.set_editor_property("settings", st)
        except Exception as e:  # noqa: BLE001
            rep["notes"].append(f"jungle post: {e}")


def build_level(man, force, rep):
    from fourfold.world import level as WL
    RS = _spec()
    b = WL.Builder(rep, mirror_y=False)
    if C.asset_exists(MAP_PATH):
        if not force:
            rep["skipped"].append(MAP_PATH)
            return
        b.les.load_level(MAP_PATH)
        for a in list(b.eas.get_all_level_actors() or []):
            try:
                if a is not None and not isinstance(a, unreal.WorldSettings):
                    b.eas.destroy_actor(a)
            except Exception:  # noqa: BLE001
                continue
    else:
        C.make_dirs("/Game/Fourfold/Maps")
        if not b.les.new_level(MAP_PATH):
            rep["failed"].append({"item": MAP_PATH, "error": "new_level returned False"})
            return
    b.build_lighting({})
    jungle(b, rep)
    for act in man["actors"]:
        role = man["meshes"][act["mesh"]]["role"]
        a = b.mesh_actor(act["label"], f"{MESH_DIR}/{act['mesh']}", (0, 0, 0), tags=ROLE_TAGS[role], mobility="static",
                         cast_shadow=role in ("ground", "stone"), folder="Fourfold/Ruins")
        if a is None:
            continue
        comp = a.static_mesh_component
        if role in ("ground", "stone"):
            C.set_prop(comp, "cast_dynamic_shadow", True, rep, quiet=True)   # dynamic sun: static meshes must cast
        if role in ("ground", "backdrop", "stone"):
            _block_all(comp, rep)
    ow = b.spawn(unreal.FourfoldOpenWorld, (0, 0, 0), label="FF_RuinsWorld", tags=("FourfoldArena",), folder="Fourfold/Ruins")
    for k, v in (("data_dir_name", RS.DATA_DIR), ("build_terrain", False), ("build_water", False), ("build_solids", False),
                 ("build_props", True), ("build_cover", False), ("normalize_prop_scale", False), ("tree_wind_distance", 4000.0)):
        C.set_prop(ow, k, v, rep)
    try:
        b.les.save_current_level()
        rep["created"].append(MAP_PATH)
    except Exception as e:  # noqa: BLE001
        rep["failed"].append({"item": MAP_PATH, "error": f"save: {e}"})


def build_all(force=False):
    rep = _report()
    src = _src()
    man_path = os.path.join(src, "layout.json")
    if not os.path.exists(man_path):
        rep["failed"].append({"item": "ruins", "error": f"missing {man_path} (run unreal/Tools/blender/ruins/prep_ruins.py)"})
        return rep
    man = json.load(open(man_path))
    C.make_dirs(ROOT, TEX_DIR, MAT_DIR, MESH_DIR, VEG_DIR)
    _import_textures(src, force, rep)
    mis = _materials(man, force, rep)
    for name, m in sorted(man["meshes"].items()):
        _import_mesh(name, m, VEG_DIR if m["role"] == "veg" else MESH_DIR, mis, force, rep)
    build_level(man, force, rep)
    rep["notes"].append(f"{len(man['meshes'])} meshes; {len(man.get('props', []))} props are instanced at runtime from world.json")
    return rep
