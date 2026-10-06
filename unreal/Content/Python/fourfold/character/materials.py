"""Fighter master materials + per-slot instances (editor Python, MaterialEditingLibrary).

Masters (all Default Lit, mobile-friendly, <= 5 texture samplers, used_with_skeletal_mesh):
  M_Fighter_Skin   BaseColor / Normal / ORM + tiling pore detail normal, fake subsurface (warm back-scatter rim
                   from FFSkinScatter, scaled by BaseColor.a = thin-part mask), status layers
  M_Fighter_Cloth  neutral BaseColor x palette tint (BaseColor.a = tint mask; TintSelect picks FF_Main / FF_Accent /
                   FF_Trim), tiling weave detail normal, sheen (fuzz toward grazing angles), status layers, two-sided
  M_Fighter_Hair   BaseColor / Normal / ORM, status layers, two-sided (lash strips)
  M_Fighter_Eyes   BaseColor / Normal / ORM, glossy, the iris (BaseColor.a) glows with FF_ElementGlow
Every master exposes FF_Main FF_Accent FF_Trim FF_ElementColor (vectors) and FF_Wet FF_Frost FF_Burn FF_ElementGlow
(scalars) so the game can set the same parameters on every slot's dynamic instance.
HLSL lives in unreal/Shaders/Character/FFFighter.ush, included as /Fourfold/Character/FFFighter.ush.
"""
import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
INCLUDE = "/Fourfold/Character/FFFighter.ush"

STATUS_SCALARS = (("FF_Wet", 0.0), ("FF_Frost", 0.0), ("FF_Burn", 0.0), ("FF_ElementGlow", 0.0))
PALETTE_VECTORS = (("FF_Main", (0.09, 0.12, 0.23)), ("FF_Accent", (0.56, 0.35, 0.11)), ("FF_Trim", (0.80, 0.74, 0.62)),
                   ("FF_ElementColor", (1.0, 0.45, 0.12)))


def _tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _set(obj, prop, value, notes=None):
    """set_editor_property that never raises (property names drift between engine versions)."""
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:  # noqa: BLE001
        if notes is not None:
            notes.append(f"could not set {prop} on {obj}: {e}")
        return False


def _clear(mat):
    for _ in range(16):
        exprs = MEL.get_material_expressions(mat) or []
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression(mat, e)


def get_or_create_material(folder, name, force):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        mat = unreal.load_asset(path)
        if not force:
            return mat, False
        _clear(mat)
    else:
        mat = _tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return mat, True


class Graph:
    """Small helper around MaterialEditingLibrary for one material."""

    def __init__(self, mat, notes):
        self.mat = mat
        self.notes = notes

    def expr(self, cls, x, y, **props):
        e = MEL.create_material_expression(self.mat, cls, int(x), int(y))
        for k, v in props.items():
            _set(e, k, v, self.notes)
        return e

    def link(self, a, a_out, b, b_in):
        ok = MEL.connect_material_expressions(a, a_out, b, b_in)
        if ok is False:
            self.notes.append(f"{self.mat.get_name()}: link failed {a_out} -> {b_in}")
        return ok

    def out(self, a, a_out, prop):
        ok = MEL.connect_material_property(a, a_out, prop)
        if ok is False:
            self.notes.append(f"{self.mat.get_name()}: output link failed {a_out} -> {prop}")
        return ok

    def scalar(self, name, value, x, y, group="Fighter"):
        return self.expr(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name,
                         default_value=float(value), group=group)

    def vector(self, name, rgb, x, y, group="Fighter"):
        return self.expr(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                         default_value=unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), 1.0),
                         group=group)

    def texture(self, name, tex, sampler, x, y, uv=None, uv_out=""):
        e = self.expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, group="Textures")
        if tex is not None:
            _set(e, "texture", tex, self.notes)
        _set(e, "sampler_type", sampler, self.notes)
        _set(e, "sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS, self.notes)
        if uv is not None:
            self.link(uv, uv_out, e, "UVs")
        return e

    def custom(self, code, output_type, inputs, outputs, x, y, desc):
        e = self.expr(unreal.MaterialExpressionCustom, x, y)
        _set(e, "code", code, self.notes)
        _set(e, "output_type", output_type, self.notes)
        _set(e, "description", desc, self.notes)
        ins = []
        for n in inputs:
            s = unreal.CustomInput()
            s.set_editor_property("input_name", n)
            ins.append(s)
        outs = []
        for n, t in outputs:
            s = unreal.CustomOutput()
            s.set_editor_property("output_name", n)
            s.set_editor_property("output_type", t)
            outs.append(s)
        _set(e, "inputs", ins, self.notes)
        _set(e, "additional_outputs", outs, self.notes)
        _set(e, "include_file_paths", [INCLUDE], self.notes)
        return e


CMOT = unreal.CustomMaterialOutputType
ST = unreal.MaterialSamplerType

STATUS_CODE = """float r; float3 e;
float3 b = FFFighterStatus(Base, Rough, Rim, Wet, Frost, Burn, Glow, ElementColor, Noise, T, Porosity, r, e);
OutRough = r;
OutEmissive = e;
return b;"""


def _common_inputs(g, tex, x0=-2600):
    """UV, the three slot textures, rim fresnel, time, status parameters. Returns a dict of nodes."""
    n = {}
    n["uv"] = g.expr(unreal.MaterialExpressionTextureCoordinate, x0, 0, coordinate_index=0)
    n["bc"] = g.texture("BaseColor", tex.get("BC"), ST.SAMPLERTYPE_COLOR, x0 + 400, -400, n["uv"])
    n["n"] = g.texture("Normal", tex.get("N"), ST.SAMPLERTYPE_NORMAL, x0 + 400, 0, n["uv"])
    n["orm"] = g.texture("ORM", tex.get("ORM"), ST.SAMPLERTYPE_MASKS, x0 + 400, 400, n["uv"])
    n["rim"] = g.expr(unreal.MaterialExpressionFresnel, x0 + 1200, 700, exponent=3.0, base_reflect_fraction=0.0)
    n["time"] = g.expr(unreal.MaterialExpressionTime, x0 + 1200, 900)
    for i, (name, val) in enumerate(STATUS_SCALARS):
        n[name] = g.scalar(name, val, x0 + 1200, 1100 + 120 * i, group="Status")
    for i, (name, rgb) in enumerate(PALETTE_VECTORS):
        n[name] = g.vector(name, rgb, x0 + 800, -1400 + 160 * i, group="Palette")
    nt = g.scalar("StatusNoiseTiling", 6.0, x0, 1500, group="Status")
    nuv = g.expr(unreal.MaterialExpressionMultiply, x0 + 200, 1500)
    g.link(n["uv"], "", nuv, "A")
    g.link(nt, "", nuv, "B")
    n["noise"] = g.texture("StatusNoise", tex.get("NOISE"), ST.SAMPLERTYPE_MASKS, x0 + 400, 1500, nuv)
    return n


def _detail_normal(g, n, tex_detail, tiling, strength, x0=-1800):
    dt = g.scalar("DetailTiling", tiling, x0 - 400, 200, group="Detail")
    ds = g.scalar("DetailStrength", strength, x0 - 400, 320, group="Detail")
    duv = g.expr(unreal.MaterialExpressionMultiply, x0 - 200, 200)
    g.link(n["uv"], "", duv, "A")
    g.link(dt, "", duv, "B")
    det = g.texture("DetailNormal", tex_detail, ST.SAMPLERTYPE_NORMAL, x0, 200, duv)
    blend = g.custom("return FFBlendDetailNormal(BaseN, DetailN, Strength);", CMOT.CMOT_FLOAT3,
                     ["BaseN", "DetailN", "Strength"], [], x0 + 400, 100, "detail normal")
    g.link(n["n"], "RGB", blend, "BaseN")
    g.link(det, "RGB", blend, "DetailN")
    g.link(ds, "", blend, "Strength")
    return blend


def _status(g, n, base, base_out, porosity, x0=-600):
    st = g.custom(STATUS_CODE, CMOT.CMOT_FLOAT3,
                  ["Base", "Rough", "Rim", "Wet", "Frost", "Burn", "Glow", "ElementColor", "Noise", "T", "Porosity"],
                  [("OutRough", CMOT.CMOT_FLOAT1), ("OutEmissive", CMOT.CMOT_FLOAT3)], x0, 0, "status layers")
    por = g.scalar("Porosity", porosity, x0 - 400, 1300, group="Status")
    g.link(base, base_out, st, "Base")
    g.link(n["orm"], "G", st, "Rough")
    g.link(n["rim"], "", st, "Rim")
    g.link(n["FF_Wet"], "", st, "Wet")
    g.link(n["FF_Frost"], "", st, "Frost")
    g.link(n["FF_Burn"], "", st, "Burn")
    g.link(n["FF_ElementGlow"], "", st, "Glow")
    g.link(n["FF_ElementColor"], "", st, "ElementColor")
    g.link(n["noise"], "RGB", st, "Noise")
    g.link(n["time"], "", st, "T")
    g.link(por, "", st, "Porosity")
    return st


def _finish(g, n, st, normal_node, normal_out, specular, emissive_extra=None):
    g.out(st, "", MP.MP_BASE_COLOR)
    g.out(st, "OutRough", MP.MP_ROUGHNESS)
    if emissive_extra is not None:
        add = g.expr(unreal.MaterialExpressionAdd, -200, 600)
        g.link(st, "OutEmissive", add, "A")
        g.link(emissive_extra[0], emissive_extra[1], add, "B")
        g.out(add, "", MP.MP_EMISSIVE_COLOR)
    else:
        g.out(st, "OutEmissive", MP.MP_EMISSIVE_COLOR)
    g.out(normal_node, normal_out, MP.MP_NORMAL)
    g.out(n["orm"], "R", MP.MP_AMBIENT_OCCLUSION)
    spec = g.scalar("Specular", specular, -400, 900)
    g.out(spec, "", MP.MP_SPECULAR)
    metal = g.expr(unreal.MaterialExpressionConstant, -400, 1000, r=0.0)
    g.out(metal, "", MP.MP_METALLIC)


def _setup(mat, two_sided, notes):
    _set(mat, "blend_mode", unreal.BlendMode.BLEND_OPAQUE, notes)
    _set(mat, "shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT, notes)
    _set(mat, "two_sided", bool(two_sided), notes)
    _set(mat, "used_with_skeletal_mesh", True, notes)


def build_skin(mat, tex, notes):
    _setup(mat, False, notes)
    g = Graph(mat, notes)
    n = _common_inputs(g, tex)
    nrm = _detail_normal(g, n, tex.get("DETAIL"), tex.get("detail_tiling", 60.0), 0.35)
    st = _status(g, n, n["bc"], "RGB", 0.3)
    sc = g.vector("ScatterColor", (0.95, 0.30, 0.18), -1400, 900, group="Skin")
    ss = g.scalar("ScatterStrength", 0.22, -1400, 1060, group="Skin")
    scatter = g.custom("return FFSkinScatter(Base, Mask, Rim, ScatterColor, Strength);", CMOT.CMOT_FLOAT3,
                       ["Base", "Mask", "Rim", "ScatterColor", "Strength"], [], -900, 900, "fake subsurface")
    g.link(n["bc"], "RGB", scatter, "Base")
    g.link(n["bc"], "A", scatter, "Mask")
    g.link(n["rim"], "", scatter, "Rim")
    g.link(sc, "", scatter, "ScatterColor")
    g.link(ss, "", scatter, "Strength")
    _finish(g, n, st, nrm, "", 0.45, (scatter, ""))


def build_cloth(mat, tex, notes):
    _setup(mat, True, notes)
    g = Graph(mat, notes)
    n = _common_inputs(g, tex)
    nrm = _detail_normal(g, n, tex.get("DETAIL"), tex.get("detail_tiling", 60.0), 0.6)
    sel = g.vector("TintSelect", (1.0, 0.0, 0.0), -1800, -1400, group="Palette")
    gain = g.scalar("TintGain", 2.0, -1800, -1240, group="Palette")
    tint = g.custom("return FFTint(Neutral, Mask, Main, Accent, Trim, Select, Gain);", CMOT.CMOT_FLOAT3,
                    ["Neutral", "Mask", "Main", "Accent", "Trim", "Select", "Gain"], [], -1300, -900, "palette tint")
    g.link(n["bc"], "RGB", tint, "Neutral")
    g.link(n["bc"], "A", tint, "Mask")
    g.link(n["FF_Main"], "", tint, "Main")
    g.link(n["FF_Accent"], "", tint, "Accent")
    g.link(n["FF_Trim"], "", tint, "Trim")
    g.link(sel, "", tint, "Select")
    g.link(gain, "", tint, "Gain")
    sh_s = g.scalar("SheenStrength", 0.25, -1300, -600, group="Cloth")
    sheen = g.custom("return FFClothSheen(Base, Rim, Strength);", CMOT.CMOT_FLOAT3, ["Base", "Rim", "Strength"], [],
                     -1000, -700, "sheen")
    g.link(tint, "", sheen, "Base")
    g.link(n["rim"], "", sheen, "Rim")
    g.link(sh_s, "", sheen, "Strength")
    st = _status(g, n, sheen, "", 1.0)
    _finish(g, n, st, nrm, "", 0.35)


def build_hair(mat, tex, notes):
    _setup(mat, True, notes)
    g = Graph(mat, notes)
    n = _common_inputs(g, tex)
    st = _status(g, n, n["bc"], "RGB", 0.6)
    _finish(g, n, st, n["n"], "RGB", 0.40)


def build_eyes(mat, tex, notes):
    _setup(mat, False, notes)
    g = Graph(mat, notes)
    n = _common_inputs(g, tex)
    st = _status(g, n, n["bc"], "RGB", 0.0)
    # charged iris glow: element colour x glow x iris mask (BaseColor.a)
    m1 = g.expr(unreal.MaterialExpressionMultiply, -900, 1000)
    g.link(n["FF_ElementColor"], "", m1, "A")
    g.link(n["FF_ElementGlow"], "", m1, "B")
    m2 = g.expr(unreal.MaterialExpressionMultiply, -700, 1000)
    g.link(m1, "", m2, "A")
    g.link(n["bc"], "A", m2, "B")
    _finish(g, n, st, n["n"], "RGB", 0.65, (m2, ""))


MASTERS = {"M_Fighter_Skin": build_skin, "M_Fighter_Cloth": build_cloth, "M_Fighter_Hair": build_hair,
           "M_Fighter_Eyes": build_eyes}
SLOT_MASTER = {"skin": "M_Fighter_Skin", "hair": "M_Fighter_Hair", "eyes": "M_Fighter_Eyes",
               "cloth_main": "M_Fighter_Cloth", "cloth_accent": "M_Fighter_Cloth", "wraps": "M_Fighter_Cloth",
               "sash": "M_Fighter_Cloth", "shoes": "M_Fighter_Cloth"}


def recompile(mat, notes):
    try:
        res = MEL.recompile_material(mat)
        if res:
            for e in res:
                notes.append(f"{mat.get_name()} compile message: {e}")
    except Exception as e:  # noqa: BLE001
        notes.append(f"{mat.get_name()}: recompile failed: {e}")


def get_or_create_instance(folder, name, parent, force):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        mi = unreal.load_asset(path)
        created = False
    else:
        mi = _tools().create_asset(name, folder, unreal.MaterialInstanceConstant,
                                   unreal.MaterialInstanceConstantFactoryNew())
        created = True
    MEL.set_material_instance_parent(mi, parent)
    return mi, created or force
