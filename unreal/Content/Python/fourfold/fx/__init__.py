"""Fourfold FX assets (stream `fx`): textures, flipbooks, hero meshes and the 19 master materials of the FX module.

    import fourfold.fx as ffx; ffx.build_all(force=False)   -> {"created", "skipped", "failed", "notes"}

Everything is described by fourfold/fx/spec.py (pure data, also read by the container-side shader checker and preview
renderer). Sources (generated, committed): unreal/SourceArt/VFX/{Textures,Flipbooks,Meshes}. Produces:
    /Game/Fourfold/FX/Textures/T_FX_*            noise (Masks), flipbooks (linear RGBA), rock normal maps
    /Game/Fourfold/FX/Meshes/SM_FX_*             rocks / crystals / metal pieces (no collision, no lightmap UVs)
    /Game/Fourfold/FX/Materials/M_FX_*           masters: Custom nodes #include "/Fourfold/FX/*.ush"
    /Game/Fourfold/FX/Materials/Instances/MI_FX_Rock_<k>   rock instances carrying the baked normal map
The runtime (FourfoldFX C++) loads the masters named in Content/Fourfold/Data/fx_config.json "materials" and creates
dynamic instances; for a static mesh whose slot-0 material is an instance of the slot master it uses that instance
as the parent (keeps the per-rock normal map).
Idempotent: existing assets are skipped unless force=True. Never raises: failures are collected in the report.
APIs used (with sources): unreal/docs/fx/API_NOTES.md.
"""
import importlib
import os
import traceback

import unreal

from . import spec

importlib.reload(spec)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty


def _log(msg):
    unreal.log(f"[Fourfold][fx] {msg}")


def _err(msg):
    unreal.log_error(f"[Fourfold][fx] {msg}")


def _art_dir():
    return os.path.abspath(os.path.join(unreal.Paths.project_dir(), "SourceArt", "VFX"))


def _set(obj, prop, value, report, quiet=False):
    """set_editor_property that never raises (property names drift between engine versions)."""
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:  # noqa: BLE001
        if not quiet:
            report["notes"].append(f"could not set {prop} on {obj}: {e}")
        return False


def _enum(enum_name, *members):
    """First existing member of unreal.<enum_name> among `members` (None when the enum / members do not exist)."""
    e = getattr(unreal, enum_name, None)
    if e is None:
        return None
    for m in members:
        v = getattr(e, m, None)
        if v is not None:
            return v
    return None


def _cls(*names):
    for n in names:
        c = getattr(unreal, n, None)
        if c is not None:
            return c
    return None


def _save(asset):
    try:
        EAL.save_loaded_asset(asset, False)
    except Exception:  # noqa: BLE001
        EAL.save_loaded_asset(asset)


def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


# ------------------------------------------------------------------------------------------------ textures
def import_textures(force, report):
    art = _art_dir()
    TC = unreal.TextureCompressionSettings
    tasks = []
    for name, (rel, kind) in spec.TEXTURES.items():
        src = os.path.join(art, rel)
        dst = f"{spec.TEXTURE_DIR}/{name}"
        if not os.path.exists(src):
            report["failed"].append({"item": dst, "error": f"missing source {src}"})
            continue
        if EAL.does_asset_exist(dst) and not force:
            report["skipped"].append(dst)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", src)
        t.set_editor_property("destination_path", spec.TEXTURE_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        tasks.append((t, name, dst, kind))
    if tasks:
        _tools().import_asset_tasks([t for t, _, _, _ in tasks])
    group_fx = _enum("TextureGroup", "TEXTUREGROUP_EFFECTS")
    group_n = _enum("TextureGroup", "TEXTUREGROUP_WORLD_NORMAL_MAP")
    for _t, name, dst, kind in tasks:
        tex = unreal.load_asset(dst) if EAL.does_asset_exist(dst) else None
        if tex is None:
            report["failed"].append({"item": dst, "error": "texture import produced nothing"})
            continue
        if kind == "normal":
            _set(tex, "srgb", False, report)
            _set(tex, "compression_settings", TC.TC_NORMALMAP, report)
            _set(tex, "flip_green_channel", False, report)       # written in the DirectX convention already
            if group_n is not None:
                _set(tex, "lod_group", group_n, report)
        elif kind == "noise":
            _set(tex, "srgb", False, report)
            _set(tex, "compression_settings", TC.TC_MASKS, report)
            if group_fx is not None:
                _set(tex, "lod_group", group_fx, report)
        else:   # flipbook: packed linear RGBA (light, thickness, temperature / foam, coverage)
            _set(tex, "srgb", False, report)
            _set(tex, "compression_settings", TC.TC_DEFAULT, report)
            if group_fx is not None:
                _set(tex, "lod_group", group_fx, report)
        _save(tex)
        report["created"].append(dst)


def _texture(name):
    if not name:
        return None
    path = name if name.startswith("/") else f"{spec.TEXTURE_DIR}/{name}"
    if path.startswith("/Engine/"):
        # EditorAssetLibrary.does_asset_exist reports False for engine content: load it directly.
        return unreal.load_asset(path)
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else None


# ------------------------------------------------------------------------------------------------ material graph
class Graph:
    """Builds one master from its spec entry with MaterialEditingLibrary."""

    def __init__(self, mat, m, report):
        self.mat = mat
        self.m = m
        self.report = report
        self.src = {}        # source string -> (expression, output name)
        self.nodes = {}      # node id -> custom expression
        self.y = 0

    def note(self, s):
        self.report["notes"].append(f"{self.m['asset']}: {s}")

    def expr(self, cls, x, y, **props):
        e = MEL.create_material_expression(self.mat, cls, int(x), int(y))
        for k, v in props.items():
            _set(e, k, v, self.report)
        return e

    def link(self, a, a_out, b, b_in):
        ok = MEL.connect_material_expressions(a, a_out, b, b_in)
        if ok is False:
            self.note(f"link failed {a_out!r} -> {b_in!r}")
        return ok

    def out(self, a, a_out, prop):
        ok = MEL.connect_material_property(a, a_out, prop)
        if ok is False:
            self.note(f"output link failed {a_out!r} -> {prop}")
        return ok

    def _next_y(self):
        self.y += 140
        return self.y

    # -------------------------------------------------------------- sources
    def resolve(self, src):
        if src in self.src:
            return self.src[src]
        x = -2400
        y = self._next_y()
        kind, _, name = src.partition(":")
        r = None
        if src in ("uv0", "uv1", "uv2"):
            r = (self.expr(unreal.MaterialExpressionTextureCoordinate, x, y, coordinate_index=int(src[2])), "")
        elif src == "vc":
            r = (self.expr(unreal.MaterialExpressionVertexColor, x, y), "")
        elif src == "vca":
            uv3 = self.expr(unreal.MaterialExpressionTextureCoordinate, x - 300, y, coordinate_index=3)
            mask = self.expr(unreal.MaterialExpressionComponentMask, x, y, r=True, g=False, b=False, a=False)
            self.link(uv3, "", mask, "")
            r = (mask, "")
        elif src == "time":
            r = (self.expr(unreal.MaterialExpressionTime, x, y), "")
        elif src == "lpos":
            cls = _cls("MaterialExpressionPreSkinnedLocalPosition", "MaterialExpressionPreSkinnedPosition",
                       "MaterialExpressionLocalPosition")
            r = (self.expr(cls, x, y), "")
        elif src == "lpos_ps":
            pos, out = self.resolve("lpos")
            vi = self.expr(unreal.MaterialExpressionVertexInterpolator, x + 300, y)
            self.link(pos, out, vi, "")
            r = (vi, "")
        elif src == "nrm_local":
            cls = _cls("MaterialExpressionPreSkinnedNormal")
            if cls is not None:
                r = (self.expr(cls, x, y), "")
            else:   # world -> local of the vertex normal
                n, o = self.resolve("nrm_ws")
                t = self.expr(unreal.MaterialExpressionTransform, x + 300, y)
                _set(t, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD, self.report)
                _set(t, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL, self.report)
                self.link(n, o, t, "")
                r = (t, "")
        elif src == "nrm_ws":
            r = (self.expr(unreal.MaterialExpressionVertexNormalWS, x, y), "")
        elif src == "cam":
            r = (self.expr(unreal.MaterialExpressionCameraVectorWS, x, y), "")
        elif src == "wpos_cr":
            e = self.expr(unreal.MaterialExpressionWorldPosition, x, y)
            v = _enum("WorldPositionIncludedOffsets", "WPT_CAMERA_RELATIVE")
            if v is not None:
                _set(e, "world_position_shader_offset", v, self.report)
            r = (e, "")
        elif src in ("ax", "ay", "az"):
            vec = {"ax": (1.0, 0.0, 0.0), "ay": (0.0, 1.0, 0.0), "az": (0.0, 0.0, 1.0)}[src]
            c = self.expr(unreal.MaterialExpressionConstant3Vector, x - 300, y,
                          constant=unreal.LinearColor(vec[0], vec[1], vec[2], 1.0))
            t = self.expr(unreal.MaterialExpressionTransform, x, y)
            _set(t, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL, self.report)
            _set(t, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, self.report)
            self.link(c, "", t, "")
            r = (t, "")
        elif kind == "p":
            r = (self.expr(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name,
                           default_value=float(self.m["scalars"][name]), group="Fourfold"), "")
        elif kind == "v":
            c = self.m["vectors"][name]
            r = (self.expr(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                           default_value=unreal.LinearColor(float(c[0]), float(c[1]), float(c[2]), float(c[3])),
                           group="Fourfold"), "")
        elif kind == "tex":
            tex = _texture(self.m["textures"][name])
            e = self.expr(unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, group="Textures")
            if tex is not None:
                _set(e, "texture", tex, self.report)
            else:
                self.note(f"default texture {self.m['textures'][name]} missing (import textures first)")
            st = unreal.MaterialSamplerType
            _set(e, "sampler_type", st.SAMPLERTYPE_MASKS if name == "Noise" else st.SAMPLERTYPE_LINEAR_COLOR,
                 self.report)
            r = (e, "")
        elif kind == "nmap":
            uv, uo = self.resolve("uv0")
            e = self.expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, group="Textures")
            tex = _texture(self.m["normal_maps"][name])
            if tex is not None:
                _set(e, "texture", tex, self.report)
            _set(e, "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, self.report)
            self.link(uv, uo, e, "UVs")
            t = self.expr(unreal.MaterialExpressionTransform, x + 300, y)
            _set(t, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT, self.report)
            _set(t, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, self.report)
            self.link(e, "RGB", t, "")
            r = (t, "")
        elif kind == "node":
            r = (self.nodes[name], "")
        else:
            raise ValueError(f"unknown graph source {src}")
        self.src[src] = r
        return r

    # -------------------------------------------------------------- custom nodes
    def custom(self, node_id, node, x, y):
        CMOT = unreal.CustomMaterialOutputType
        types = {"float": CMOT.CMOT_FLOAT1, "float2": CMOT.CMOT_FLOAT2, "float3": CMOT.CMOT_FLOAT3,
                 "float4": CMOT.CMOT_FLOAT4}
        e = self.expr(unreal.MaterialExpressionCustom, x, y)
        _set(e, "code", node["code"], self.report)
        _set(e, "output_type", types[node["type"]], self.report)
        _set(e, "description", f"{self.m['asset']} {node_id}", self.report)
        ins = []
        for n in node["inputs"]:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        outs = []
        for n, t in node["outputs"].items():
            co = unreal.CustomOutput()
            co.set_editor_property("output_name", n)
            co.set_editor_property("output_type", types[t])
            outs.append(co)
        _set(e, "inputs", ins, self.report)
        _set(e, "additional_outputs", outs, self.report)
        _set(e, "include_file_paths", list(self.m["includes"]), self.report)
        self.nodes[node_id] = e
        return e

    def build(self):
        m = self.m
        # declare every parameter (also the ones only some styles use) so instances can set them
        for n in m.get("scalars", {}):
            self.resolve("p:" + n)
        for n in m.get("vectors", {}):
            self.resolve("v:" + n)
        x = -900
        for i, (node_id, node) in enumerate(m["nodes"].items()):
            self.custom(node_id, node, x, i * 600)
        for node_id, node in m["nodes"].items():
            e = self.nodes[node_id]
            for name, src in node["inputs"].items():
                a, ao = self.resolve(src)
                self.link(a, ao, e, name)
        props = {"BaseColor": MP.MP_BASE_COLOR, "EmissiveColor": MP.MP_EMISSIVE_COLOR, "Opacity": MP.MP_OPACITY,
                 "Normal": MP.MP_NORMAL, "Roughness": MP.MP_ROUGHNESS, "Specular": MP.MP_SPECULAR,
                 "Metallic": MP.MP_METALLIC, "WorldPositionOffset": MP.MP_WORLD_POSITION_OFFSET}
        for prop, (node_id, out_name) in m["outputs"].items():
            e = self.nodes[node_id]
            if prop == "WorldPositionOffset":
                # node returns a LOCAL offset in cm -> world
                t = self.expr(unreal.MaterialExpressionTransform, -300, -400)
                _set(t, "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL, self.report)
                _set(t, "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD, self.report)
                self.link(e, out_name, t, "")
                self.out(t, "", props[prop])
            else:
                self.out(e, out_name, props[prop])
        for prop, val in m.get("constants", {}).items():
            c = self.expr(unreal.MaterialExpressionConstant, -300, 900, r=float(val))
            self.out(c, "", props[prop])


def _setup_material(mat, m, report):
    BM = unreal.BlendMode
    blend = {"opaque": BM.BLEND_OPAQUE, "alpha_composite": BM.BLEND_ALPHA_COMPOSITE, "additive": BM.BLEND_ADDITIVE,
             "translucent": BM.BLEND_TRANSLUCENT}[m["blend"]]
    _set(mat, "blend_mode", blend, report)
    sm = unreal.MaterialShadingModel
    _set(mat, "shading_model", sm.MSM_DEFAULT_LIT if m["lit"] else sm.MSM_UNLIT, report)
    _set(mat, "two_sided", bool(m.get("two_sided", False)), report)
    if m["lit"]:
        _set(mat, "tangent_space_normal", bool(m.get("tangent_normal", True)), report)
    if m["blend"] == "translucent" and m["lit"]:
        v = _enum("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE")
        if v is not None:
            _set(mat, "translucency_lighting_mode", v, report)
    # usage: procedural meshes use the local vertex factory (no flag); hero meshes may be instanced
    _set(mat, "used_with_instanced_static_meshes", True, report)
    if m.get("full_precision"):
        v = _enum("MaterialFloatPrecisionMode", "MFPM_FULL")
        if v is None or not _set(mat, "float_precision_mode", v, report, quiet=True):
            _set(mat, "use_full_precision", True, report, quiet=True)


def _clear(mat):
    for _ in range(16):
        exprs = MEL.get_material_expressions(mat) or []
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression(mat, e)


def build_material(key, m, force, report):
    path = f"{spec.MATERIAL_DIR}/{m['asset']}"
    if EAL.does_asset_exist(path):
        mat = unreal.load_asset(path)
        if not force:
            report["skipped"].append(path)
            return mat
        _clear(mat)
    else:
        mat = _tools().create_asset(m["asset"], spec.MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        report["failed"].append({"item": path, "error": "create_asset returned None"})
        return None
    _setup_material(mat, m, report)
    Graph(mat, m, report).build()
    try:
        res = MEL.recompile_material(mat)
        if res:
            for line in res:
                report["notes"].append(f"{m['asset']} compile: {line}")
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"{m['asset']}: recompile failed: {e}")
    _save(mat)
    report["created"].append(path)
    return mat


# ------------------------------------------------------------------------------------------------ meshes
def _mesh_pipeline(report):
    pipe = unreal.InterchangeGenericAssetsPipeline()
    _set(pipe, "use_source_name_for_asset", True, report)
    mp = pipe.get_editor_property("mesh_pipeline")
    _set(mp, "import_static_meshes", True, report)
    _set(mp, "import_skeletal_meshes", False, report)
    _set(mp, "build_nanite", False, report)            # 5.8 default True; no Nanite on mobile
    _set(mp, "collision", False, report)               # effects never collide
    _set(mp, "generate_lightmap_u_vs", False, report, quiet=True)   # not a mesh-pipeline property in 5.8 (see below)
    cm = pipe.get_editor_property("common_meshes_properties")
    v = _enum("InterchangeForceMeshType", "IFMT_STATIC_MESH")
    if v is not None:
        _set(cm, "force_all_mesh_as_type", v, report)
    _set(cm, "recompute_normals", False, report)
    _set(cm, "recompute_tangents", False, report)
    matp = pipe.get_editor_property("material_pipeline")
    _set(matp, "import_materials", False, report)
    try:
        _set(matp.get_editor_property("texture_pipeline"), "import_textures", False, report)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"texture_pipeline: {e}")
    return pipe


def _import_fbx(fbx, name, report):
    """Interchange first (5.5+ default), the legacy FBX importer as the fallback."""
    try:
        pipe = _mesh_pipeline(report)
        stack = unreal.InterchangePipelineStackOverride()
        if hasattr(stack, "add_pipeline"):
            stack.add_pipeline(pipe)
        else:
            stack.get_editor_property("override_pipelines").append(pipe)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", spec.MESH_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("options", stack)
        _tools().import_asset_tasks([t])
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"{name}: Interchange import raised ({e})")
    path = f"{spec.MESH_DIR}/{name}"
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        ui = unreal.FbxImportUI()
        _set(ui, "import_mesh", True, report)
        _set(ui, "import_as_skeletal", False, report)
        _set(ui, "import_materials", False, report)
        _set(ui, "import_textures", False, report)
        _set(ui, "mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH, report)
        smd = ui.get_editor_property("static_mesh_import_data")
        _set(smd, "combine_meshes", True, report)
        _set(smd, "generate_lightmap_u_vs", False, report)
        _set(smd, "auto_generate_collision", False, report, quiet=True)
        _set(smd, "normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS, report)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", spec.MESH_DIR)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("options", ui)
        _tools().import_asset_tasks([t])
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"{name}: legacy FBX import raised ({e})")
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")
    return unreal.load_asset(path) if EAL.does_asset_exist(path) else None


def _rock_instance(k, normal_tex, force, report):
    path = f"{spec.INSTANCE_DIR}/MI_FX_Rock_{k}"
    parent = unreal.load_asset(f"{spec.MATERIAL_DIR}/M_FX_Rock")
    if parent is None:
        report["failed"].append({"item": path, "error": "M_FX_Rock missing"})
        return None
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
        if not force:
            report["skipped"].append(path)
            return mi
    else:
        mi = _tools().create_asset(f"MI_FX_Rock_{k}", spec.INSTANCE_DIR, unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, parent)
    tex = _texture(normal_tex)
    if tex is not None:
        MEL.set_material_instance_texture_parameter_value(mi, "RockNormal", tex)
    else:
        report["notes"].append(f"MI_FX_Rock_{k}: normal map {normal_tex} missing")
    try:
        MEL.update_material_instance(mi)
    except Exception:  # noqa: BLE001
        pass
    _save(mi)
    report["created"].append(path)
    return mi


def import_meshes(force, report):
    art = _art_dir()
    sme = None
    try:
        sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"StaticMeshEditorSubsystem unavailable: {e}")
    for name, info in spec.MESHES.items():
        path = f"{spec.MESH_DIR}/{name}"
        try:
            src = os.path.join(art, info["fbx"])
            if EAL.does_asset_exist(path) and not force:
                mesh = unreal.load_asset(path)
                report["skipped"].append(path)
            else:
                if not os.path.exists(src):
                    report["failed"].append({"item": path, "error": f"missing source {src}"})
                    continue
                mesh = _import_fbx(src, name, report)
                if mesh is None:
                    report["failed"].append({"item": path, "error": "no static mesh after import"})
                    continue
                report["created"].append(path)
            if sme is not None:
                try:
                    sme.set_generate_lightmap_uv(mesh, False)
                except Exception:  # noqa: BLE001
                    pass
                try:
                    n_uv = sme.get_num_uv_channels(mesh, 0)
                    if name.startswith("SM_FX_rock") and n_uv < 3:
                        report["notes"].append(f"{name}: only {n_uv} UV channels (the melt needs UV1 / UV2)")
                except Exception:  # noqa: BLE001
                    pass
            # slot 0: the per-rock instance (baked normal map) or the master
            mat = None
            if info.get("normal"):
                k = int(name.rsplit("_", 1)[1])
                mat = _rock_instance(k, info["normal"][1], force, report)
            if mat is None:
                mat = unreal.load_asset(f"{spec.MATERIAL_DIR}/{info['mat']}")
            if mat is not None:
                try:
                    mesh.set_material(0, mat)
                except Exception as e:  # noqa: BLE001
                    report["notes"].append(f"{name}: set_material failed ({e})")
            _save(mesh)
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": path, "error": f"{e}\n{traceback.format_exc()}"})


# ------------------------------------------------------------------------------------------------ entry point
def build_all(force=False):
    report = {"created": [], "skipped": [], "failed": [], "notes": []}
    _log(f"build_all(force={force}) from {_art_dir()}")
    try:
        import_textures(force, report)
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": "textures", "error": f"{e}\n{traceback.format_exc()}"})
    for key, m in spec.MATERIALS.items():
        try:
            build_material(key, m, force, report)
        except Exception as e:  # noqa: BLE001
            report["failed"].append({"item": m["asset"], "error": f"{e}\n{traceback.format_exc()}"})
            _err(f"{m['asset']}: {e}")
    try:
        import_meshes(force, report)
    except Exception as e:  # noqa: BLE001
        report["failed"].append({"item": "meshes", "error": f"{e}\n{traceback.format_exc()}"})
    _log(f"done: {len(report['created'])} created, {len(report['skipped'])} skipped, {len(report['failed'])} failed")
    for f in report["failed"]:
        _err(f"FAILED {f['item']}: {f['error'].splitlines()[0] if f['error'] else ''}")
    return report


def material_paths():
    """Master material asset paths by slot key (what fx_config.json "materials" lists)."""
    return {k: f"{spec.MATERIAL_DIR}/{m['asset']}" for k, m in spec.MATERIALS.items()}
