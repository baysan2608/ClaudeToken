"""Scanned trees (Poly Haven, CC0) for the scenery: textures, two masters, per-slot instances, LOD'd static meshes, and the forest
actors that replace the card trees of SM_Env_Trees.

Sources (git-ignored, rebuildable): SourceArt/Environment/PolyHaven/Trees/{trees.json, SM_Tree_*_LOD<n>.fbx}
(Tools/world/fetch_polyhaven_models.py -> Tools/blender/world/prep_trees.py); the layout SourceArt/Environment/scenery_instances.json
(Tools/world/scenery_instances.py, committed: same 90 spots as the card trees).  Without the sources the card trees stay.
Produces /Game/Fourfold/Env/Trees/{Textures/T_PH_*, M_FF_TreeLeaf, M_FF_TreeBark, MI_<mesh>_<slot>, SM_Tree_*}.
Masters:
  M_FF_TreeLeaf  masked, two-sided foliage shading (sun shines through needles / leaves), dithered LOD fade, wind = trunk sway
                 growing with the height in the tree + per-card flutter, per-tree colour variation from the tree's position
  M_FF_TreeBark  opaque bark / trunk / branches with the same trunk sway (bark and leaves move together), dithered LOD fade
LODs 1-2 need the full editor (StaticMeshEditorSubsystem is missing in a -run=pythonscript commandlet): run the world part with
-ExecutePythonScript, or LOD0 alone is imported (notes say so).
Every tree is a movable StaticMeshActor (identical meshes are auto-instanced by the renderer), shadow casting, no collision,
kept out of distance-field lighting like the rest of the backdrop (see level.py)."""
import json
import os

import unreal

from . import common as C
from .materials import MEL, MP, ST, F3, RGB, Graph, get_or_create_material

TREE_DIR = C.ENV_ROOT + "/Trees"
TEX_DIR = TREE_DIR + "/Textures"
LOD_SCREEN = [1.0, 0.42, 0.16]           # LOD0 above 42 % screen height, LOD2 below 16 %

WIND_CODE = """float h = saturate((WP.z - OP.z) / max(B.z, 100.0));
float ph = dot(OP.xy, float2(0.0031, 0.0047));
float g = sin(T * 0.85 + ph) * 0.65 + sin(T * 1.9 + ph * 1.7) * 0.25 + sin(T * 0.37 + ph * 0.5) * 0.45;
float2 d = normalize(Dir.xy + float2(1e-4, 0.0));
float3 sway = float3(d * g, 0.0) * (Amp * h * h);
float fl = sin(T * 5.3 + dot(WP, float3(0.031, 0.047, 0.023))) * Flutter * h;
return sway + float3(fl * 0.55, fl * 0.45, fl * 0.35);"""
TINT_CODE = """float r = frac(sin(dot(OP.xy * 0.01, float2(12.9898, 78.233))) * 43758.5453);
float3 hue = lerp(float3(1.03, 0.98, 0.86), float3(0.90, 1.0, 1.05), r);
return Base * hue * lerp(1.0 - Var, 1.0 + Var * 0.6, r);"""
CUSTOM_BODIES = {   # for Tools/world/check_hlsl.py-style checks: name -> (code, inputs, return type)
    "tree_wind": (WIND_CODE, ["WP", "OP", "B", "T", "Amp", "Flutter", "Dir"], "float3"),
    "tree_tint": (TINT_CODE, ["Base", "OP", "Var"], "float3"),
}


def _paths():
    p = C.project_paths()
    ph = os.path.join(p["art"], "PolyHaven")
    return dict(ph=ph, trees_json=os.path.join(ph, "Trees", "trees.json"), rocks_json=os.path.join(ph, "Rocks", "rocks.json"),
                instances_json=os.path.join(p["art"], "scenery_instances.json"))


# ------------------------------------------------------------------------------------------------------------------ textures
def _tex_kind(fn):
    f = fn.lower()
    if "_nor_dx" in f:
        return "normal"
    if "_arm" in f:
        return "masks"
    if "_alpha" in f:
        return "opacity"
    return "color"


def _tex_settings(tex, kind, report):
    P = C.set_prop
    if kind == "color":
        P(tex, "srgb", True, report)
        P(tex, "compression_settings", C.enum("TextureCompressionSettings", "TC_DEFAULT"), report)
        P(tex, "lod_group", C.enum("TextureGroup", "TEXTUREGROUP_WORLD"), report, quiet=True)
    elif kind == "normal":
        P(tex, "srgb", False, report)
        P(tex, "compression_settings", C.enum("TextureCompressionSettings", "TC_NORMALMAP"), report)
        P(tex, "flip_green_channel", False, report, quiet=True)            # Poly Haven nor_dx is DirectX already
        P(tex, "lod_group", C.enum("TextureGroup", "TEXTUREGROUP_WORLD_NORMAL_MAP"), report, quiet=True)
    elif kind == "masks":
        P(tex, "srgb", False, report)
        P(tex, "compression_settings", C.enum("TextureCompressionSettings", "TC_MASKS", "TC_DEFAULT"), report)
        P(tex, "lod_group", C.enum("TextureGroup", "TEXTUREGROUP_WORLD"), report, quiet=True)
    else:
        # cut-out masks: keep the coverage of the cards in the small mips, or distant crowns go thin and see-through
        P(tex, "srgb", False, report)
        P(tex, "compression_settings", C.enum("TextureCompressionSettings", "TC_GRAYSCALE", "TC_MASKS"), report)
        P(tex, "do_scale_mips_for_alpha_coverage", True, report, quiet=True)
        P(tex, "alpha_coverage_thresholds", unreal.Vector4(0.5, 0.0, 0.0, 0.0), report, quiet=True)
        P(tex, "lod_group", C.enum("TextureGroup", "TEXTUREGROUP_WORLD"), report, quiet=True)
    P(tex, "max_texture_size", 2048, report, quiet=True)


def import_textures(manifest, force, report):
    """{relative source path: Texture2D} for every texture any slot references."""
    ph = _paths()["ph"]
    rels = sorted({r for m in manifest["meshes"].values() for t in m["slots"].values() for r in t.values()})
    C.make_dirs(TREE_DIR, TEX_DIR)
    out, tasks = {}, []
    for rel in rels:
        stem = os.path.splitext(os.path.basename(rel))[0].replace("_2k", "")
        name = f"T_PH_{stem}"
        dst = f"{TEX_DIR}/{name}"
        out[rel] = dst
        if C.asset_exists(dst) and not force:
            report["skipped"].append(dst)
            continue
        src = os.path.join(ph, rel)
        if not os.path.exists(src):
            report["failed"].append({"item": dst, "error": f"missing {src}"})
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", TEX_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        tasks.append((t, rel, dst))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _, _ in tasks])
    for _t, rel, dst in tasks:
        tex = unreal.load_asset(dst) if C.asset_exists(dst) else None
        if tex is None:
            report["failed"].append({"item": dst, "error": "texture import produced nothing"})
            continue
        _tex_settings(tex, _tex_kind(rel), report)
        C.save_asset(tex)
        report["created"].append(dst)
    return {rel: unreal.load_asset(p) for rel, p in out.items() if C.asset_exists(p)}


# ------------------------------------------------------------------------------------------------------------------ masters
def _wind(g, x, y, flutter):
    wp = g.expr(unreal.MaterialExpressionWorldPosition, x - 400, y)
    op = g.expr(unreal.MaterialExpressionObjectPositionWS, x - 400, y + 100)
    bounds = g.expr(unreal.MaterialExpressionObjectBounds, x - 400, y + 200)
    t = g.expr(unreal.MaterialExpressionTime, x - 400, y + 300)
    amp = g.scalar("SwayCm", 28.0, x - 400, y + 400, "Wind")
    fl = g.scalar("FlutterCm", flutter, x - 400, y + 500, "Wind")
    d = g.vector("WindDir", (0.8, 0.6, 0.0), x - 400, y + 600, "Wind")
    w = g.custom(WIND_CODE, F3, ["WP", "OP", "B", "T", "Amp", "Flutter", "Dir"], [], x, y, "tree wind")
    g.wire(w, {"WP": (wp, ""), "OP": (op, ""), "B": (bounds, ""), "T": (t, ""), "Amp": (amp, ""), "Flutter": (fl, ""), "Dir": (d, "")})
    return w, op


def _common(mat, notes, masked):
    rep = {"notes": notes}
    C.set_prop(mat, "dithered_lod_transition", True, rep, quiet=True)
    C.set_prop(mat, "used_with_instanced_static_meshes", True, rep, quiet=True)
    if masked:
        C.set_prop(mat, "blend_mode", unreal.BlendMode.BLEND_MASKED, rep)
        C.set_prop(mat, "two_sided", True, rep)
        C.set_prop(mat, "opacity_mask_clip_value", 0.4, rep, quiet=True)
        C.set_prop(mat, "shading_model", C.enum("MaterialShadingModel", "MSM_TWO_SIDED_FOLIAGE"), rep)


def build_leaf(mat, tex, notes):
    g = Graph(mat, notes)
    _common(mat, notes, masked=True)
    uv = g.expr(unreal.MaterialExpressionTextureCoordinate, -1700, 0, coordinate_index=0)
    bc = g.texture("BaseColor", tex.get("BC"), ST.SAMPLERTYPE_COLOR, -1400, -200, uv)
    nm = g.texture("Normal", tex.get("N"), ST.SAMPLERTYPE_NORMAL, -1400, 50, uv)
    orm = g.texture("ORM", tex.get("ORM"), ST.SAMPLERTYPE_MASKS, -1400, 300, uv)
    op = g.texture("Opacity", tex.get("OP"), ST.SAMPLERTYPE_LINEAR_GRAYSCALE, -1400, 550, uv)
    w, opos = _wind(g, -600, 900, 2.5)
    var = g.scalar("ColorVariation", 0.12, -1000, -100, "Leaf")
    tint = g.custom(TINT_CODE, F3, ["Base", "OP", "Var"], [], -900, -250, "per-tree tint")
    g.wire(tint, {"Base": (bc, RGB), "OP": (opos, ""), "Var": (var, "")})
    bright = g.scalar("Brightness", 1.0, -900, -380, "Leaf")
    base = g.multiply(tint, "", bright, "", -650, -250)
    g.out(base, "", MP.MP_BASE_COLOR)
    g.out(nm, RGB, MP.MP_NORMAL)
    g.out(orm, "G", MP.MP_ROUGHNESS)
    g.out(orm, "R", MP.MP_AMBIENT_OCCLUSION)
    g.out(g.scalar("Specular", 0.35, -650, 150, "Leaf"), "", MP.MP_SPECULAR)
    g.out(op, ("R", "RGB", ""), MP.MP_OPACITY_MASK)
    # light through the leaves: the leaf colour pushed toward warm yellow-green
    sss_tint = g.vector("Transmission", (0.55, 0.65, 0.22), -900, 400, "Leaf")
    sss = g.multiply(tint, "", sss_tint, "", -650, 400)
    g.out(sss, "", MP.MP_SUBSURFACE_COLOR)
    g.out(w, "", MP.MP_WORLD_POSITION_OFFSET)


def build_bark(mat, tex, notes):
    g = Graph(mat, notes)
    _common(mat, notes, masked=False)
    uv = g.expr(unreal.MaterialExpressionTextureCoordinate, -1700, 0, coordinate_index=0)
    bc = g.texture("BaseColor", tex.get("BC"), ST.SAMPLERTYPE_COLOR, -1400, -200, uv)
    nm = g.texture("Normal", tex.get("N"), ST.SAMPLERTYPE_NORMAL, -1400, 50, uv)
    orm = g.texture("ORM", tex.get("ORM"), ST.SAMPLERTYPE_MASKS, -1400, 300, uv)
    w, _ = _wind(g, -600, 700, 0.0)
    bright = g.scalar("Brightness", 1.0, -900, -380, "Bark")
    g.out(g.multiply(bc, RGB, bright, "", -650, -250), "", MP.MP_BASE_COLOR)
    g.out(nm, RGB, MP.MP_NORMAL)
    g.out(orm, "G", MP.MP_ROUGHNESS)
    g.out(orm, "R", MP.MP_AMBIENT_OCCLUSION)
    g.out(w, "", MP.MP_WORLD_POSITION_OFFSET)


def build_masters(manifest, texs, force, report):
    first = next(iter(manifest["meshes"].values()))["slots"]
    leaf_t = next((t for s, t in first.items() if s == "Leaf"), {})
    bark_t = next((t for s, t in first.items() if s != "Leaf"), {})
    out = {}
    for name, builder, tdef in (("M_FF_TreeLeaf", build_leaf, leaf_t), ("M_FF_TreeBark", build_bark, bark_t)):
        path = f"{TREE_DIR}/{name}"
        mat, rebuild = get_or_create_material(TREE_DIR, name, force)
        out[name] = mat
        if not rebuild:
            report["skipped"].append(path)
            continue
        builder(mat, {k: texs.get(v) for k, v in tdef.items()}, report["notes"])
        MEL.recompile_material(mat)
        C.save_asset(mat)
        report["created"].append(path)
    return out


def build_instances(manifest, masters, texs, force, report):
    """{mesh name: {slot: MaterialInstanceConstant}}."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    params = {"BC": "BaseColor", "N": "Normal", "ORM": "ORM", "OP": "Opacity"}
    out = {}
    for mesh, m in manifest["meshes"].items():
        out[mesh] = {}
        for slot, tdef in m["slots"].items():
            name = f"MI_{mesh[3:]}_{slot}"
            path = f"{TREE_DIR}/{name}"
            if C.asset_exists(path) and not force:
                out[mesh][slot] = unreal.load_asset(path)
                report["skipped"].append(path)
                continue
            mi = unreal.load_asset(path) if C.asset_exists(path) else tools.create_asset(
                name, TREE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mi, masters["M_FF_TreeLeaf" if slot == "Leaf" else "M_FF_TreeBark"])
            for k, rel in tdef.items():
                if k in params and texs.get(rel) is not None:
                    MEL.set_material_instance_texture_parameter_value(mi, params[k], texs[rel])
            if m.get("kind") == "rock":            # distant mountains: no wind
                MEL.set_material_instance_scalar_parameter_value(mi, "SwayCm", 0.0)
            if m["species"] == "Broadleaf" and slot == "Leaf":
                MEL.set_material_instance_scalar_parameter_value(mi, "FlutterCm", 3.5)
                MEL.set_material_instance_scalar_parameter_value(mi, "SwayCm", 16.0)
            MEL.update_material_instance(mi)
            C.save_asset(mi)
            out[mesh][slot] = mi
            report["created"].append(path)
    return out


# ------------------------------------------------------------------------------------------------------------------ meshes
def _import_fbx(fbx, name, report):
    from . import meshes as M
    pipe = M._pipeline(name, report)
    stack = unreal.InterchangePipelineStackOverride()
    if hasattr(stack, "add_pipeline"):
        stack.add_pipeline(pipe)
    else:
        stack.get_editor_property("override_pipelines").append(pipe)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", TREE_DIR)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", stack)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])


def _import_lod(mesh, index, fbx, report):
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for fn in (getattr(sms, "import_lod", None), getattr(getattr(unreal, "EditorStaticMeshLibrary", None), "import_lod", None)):
        if fn is None:
            continue
        try:
            r = fn(mesh, index, fbx)
            if r is not None and r >= 0:
                return True
        except Exception as e:  # noqa: BLE001
            report["notes"].append(f"{mesh.get_name()}: import_lod {index} raised {e}")
    report["failed"].append({"item": f"{mesh.get_name()} LOD{index}", "error": f"import_lod failed ({fbx})"})
    return False


def _assign(mesh, mis, report):
    slots = list(mesh.get_editor_property("static_materials") or [])
    missing = []
    for sm in slots:
        nm = str(sm.get_editor_property("material_slot_name")).split(".")[0]
        imp = str(sm.get_editor_property("imported_material_slot_name")).split(".")[0]
        mi = mis.get(nm) or mis.get(imp)
        if mi is None:
            missing.append(nm)
            continue
        sm.set_editor_property("material_interface", mi)
    mesh.set_editor_property("static_materials", slots)
    if missing:
        report["notes"].append(f"{mesh.get_name()}: slots without a tree material: {missing}")


def import_meshes(manifest, mis, force, report):
    """{mesh name: dict(path, height_cm, species)}."""
    from . import meshes as M
    ph = _paths()["ph"]
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    out = {}
    for name, m in manifest["meshes"].items():
        dst = f"{TREE_DIR}/{name}"
        if not C.asset_exists(dst) or force:
            lod0 = os.path.join(ph, m["lods"][0]["fbx"])
            try:
                _import_fbx(lod0, name, report)
            except Exception as e:  # noqa: BLE001
                report["failed"].append({"item": dst, "error": f"import raised {e}"})
                continue
            mesh = unreal.load_asset(dst) if C.asset_exists(dst) else None
            if mesh is None:
                report["failed"].append({"item": dst, "error": "no static mesh after import"})
                continue
            for i, lod in enumerate(m["lods"][1:], start=1):
                _import_lod(mesh, i, os.path.join(ph, lod["fbx"]), report)
            try:
                C.set_prop(mesh, "auto_compute_lod_screen_size", False, report, quiet=True)
                sms.set_lod_screen_sizes(mesh, LOD_SCREEN[:len(m["lods"])])
            except Exception as e:  # noqa: BLE001
                report["notes"].append(f"{name}: LOD screen sizes not set ({e})")
            M._remove_collision(mesh, report)
            _assign(mesh, mis.get(name, {}), report)
            C.save_asset(mesh)
            report["created"].append(dst)
        else:
            report["skipped"].append(dst)
        out[name] = dict(path=dst, height_cm=100.0 * float(m["height_m"]), species=m["species"])
    return out


def import_all(force, report):
    """Returns the forest description for place() or {} when the scanned trees are not available."""
    p = _paths()
    if not os.path.exists(p["trees_json"]):
        report["notes"].append("scanned trees not prepared (Tools/world/fetch_polyhaven_models.py + Tools/blender/world/prep_trees.py): "
                               "card trees stay")
        return {}
    manifest = C.load_json(p["trees_json"], report, "trees.json")
    layout = C.load_json(p["instances_json"], report, "scenery_instances.json")
    if not manifest or not layout:
        return {}
    C.make_dirs(C.ENV_ROOT, TREE_DIR, TEX_DIR)
    texs = import_textures(manifest, force, report)
    masters = build_masters(manifest, texs, force, report)
    mis = build_instances(manifest, masters, texs, force, report)
    meshes = import_meshes(manifest, mis, force, report)
    rocks = {}
    if os.path.exists(p["rocks_json"]):
        rman = C.load_json(p["rocks_json"], report, "rocks.json") or {"meshes": {}}
        for m in rman["meshes"].values():
            m["species"] = "Rock"
        rtex = import_textures(rman, force, report)
        rmis = build_instances(rman, masters, rtex, force, report)
        rocks = import_meshes(rman, rmis, force, report)
        for name, r in rocks.items():
            r["face_yaw_deg"] = float(rman["meshes"][name].get("face_yaw_deg", 90.0))
    return dict(meshes=meshes, rocks=rocks, layout=layout) if meshes else {}


# ------------------------------------------------------------------------------------------------------------------ placement
def place(builder, forest, report):
    """Spawns one actor per tree of the layout (replaces the card trees FFScenery_Trees).  builder = level.Builder."""
    meshes = forest.get("meshes") or {}
    by_species = {}
    for name in sorted(meshes):
        by_species.setdefault(meshes[name]["species"], []).append(name)
    n = 0
    for i, t in enumerate(forest["layout"].get("trees", [])):
        names = by_species.get(t["species"])
        if not names:
            continue
        mname = names[t["variant"] % len(names)]
        mi = meshes[mname]
        s = 100.0 * float(t["height_m"]) / max(mi["height_cm"], 1.0)
        lean = float(t.get("lean_deg", 0.0))
        a = builder.mesh_actor(f"FFTree_{i:02d}", mi["path"], tuple(t["location_cm"]), mobility="movable", cast_shadow=True,
                               scale=(s, s, s), rot=(lean, float(t["yaw_deg"]), 0.0), folder="Fourfold/Scenery/Forest")
        if a is None:
            continue
        comp = a.static_mesh_component
        C.set_prop(comp, "affect_distance_field_lighting", False, report, quiet=True)
        C.set_prop(comp, "cast_dynamic_shadow", True, report, quiet=True)
        try:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:  # noqa: BLE001
            pass
        n += 1
    rocks = forest.get("rocks") or {}
    nm = 0
    for i, mt in enumerate(forest["layout"].get("mountains", []) if rocks else []):
        r = rocks.get(mt["mesh"])
        if r is None:
            continue
        s = 100.0 * float(mt["height_m"]) / max(r["height_cm"], 1.0)
        yaw = float(mt["face_to_deg"]) - r["face_yaw_deg"] + float(mt.get("yaw_jitter_deg", 0.0))
        a = builder.mesh_actor(f"FFMountain_{i:02d}", r["path"], tuple(mt["location_cm"]), mobility="movable", cast_shadow=False,
                               scale=(s * float(mt.get("width_scale", 1.0)), s, s), rot=(0.0, yaw, 0.0), folder="Fourfold/Scenery/Mountains")
        if a is None:
            continue
        comp = a.static_mesh_component
        C.set_prop(comp, "affect_distance_field_lighting", False, report, quiet=True)
        C.set_prop(comp, "affect_dynamic_indirect_lighting", False, report, quiet=True)
        try:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:  # noqa: BLE001
            pass
        nm += 1
    if nm:
        report["notes"].append(f"mountains: {nm} scanned rock faces placed")
    if n:
        old = builder.actors.pop("FFScenery_Trees", None)
        if old is not None:
            builder.eas.destroy_actor(old)
        report["notes"].append(f"forest: {n} scanned trees placed (card trees removed)")
        report["created"].append(f"{n} actors FFTree_*")
    return n
