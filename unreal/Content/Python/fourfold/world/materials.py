"""Courtyard materials built with MaterialEditingLibrary (editor Python).

Masters (all mobile-friendly: <= 6 texture samplers, default wrap sampler shared, HLSL in /Fourfold/Env/FFEnv.ush):
  M_Env_Surface  BaseColor / Normal / ORM (+ macro noise) -> grade (saturation, tint, grime from vertex colour G, wetness) -> PBR
                 used by stone, caps, plaster, timber, roof tile, pool tile, metal plate, ground, rock, bark
  M_Env_Floor    M_Env_Surface + the arena mask (contact AO, wall-edge dirt, pool splash wetness, rust halo) sampled by world XY
  M_Env_Plain    constant colour / metallic / roughness (iron, bronze)
  M_Env_Glow     lantern paper, windows, coals: flickering emissive
  M_Env_Water    translucent pool surface: two panned normal maps, fresnel opacity + sky reflection (emissive)
  M_Env_Banner   masked two-sided cloth (atlas), sway by vertex colour (R hang, G flutter)
  M_Env_Foliage  masked two-sided leaf cards (atlas), wind by vertex colour B, tint R, gradient G, fake translucency
  M_Env_Ridge    unlit vertex-colour mountain layers (+ noise detail)
  M_Env_Sky      unlit sky dome: gradient, sun glow, two cloud layers (T_Env_Clouds)
Instances MI_Env_<Slot> exist for every FBX material slot (Tools/world/env_spec.py: TILE).  The optional collection MPC_Arena
(scalar Wetness 0..1) lets the game wet every arena surface at once (CollectionParameter 'Wetness' is max()-ed with the instance value).
"""
import unreal

from . import common as C
from . import textures as T

MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
INCLUDE = "/Fourfold/Env/FFEnv.ush"
ST = unreal.MaterialSamplerType
CMOT = unreal.CustomMaterialOutputType

# ---------------------------------------------------------------------------------------------------------- Custom node bodies
# (kept as module constants: Tools/world/check_hlsl.py compiles every one of them with DXC)
SURFACE_CODE = """float r; float ao;
float3 c = FFEnvSurface(Base, Rough, AO, Grime, GrimeAmount, GrimeColor, Wet, Porosity, Sat, Tint, Bright, Macro, Halo, r, ao);
OutRough = saturate(r * RoughScale);
OutAO = ao;
return c;"""
SURFACE_INPUTS = ["Base", "Rough", "AO", "Grime", "GrimeAmount", "GrimeColor", "Wet", "Porosity", "Sat", "Tint", "Bright", "Macro", "Halo", "RoughScale"]
MACRO_CODE = "return FFEnvMacro(Noise, Strength);"
WORLD_UV_CODE = "return FFEnvWorldUV(WP, TileCm);"
NORMAL_CODE = "return FFEnvNormal(N, Strength);"
MASK_UV_CODE = "return FFArenaMaskUV(WP, Rect);"
WATER_UV_CODE = "return FFWaterUV(WP, T, Dir.xy, Speed, TileCm);"
WATER_NORMAL_CODE = "return FFWaterNormal(N1, N2, Strength);"
WATER_COLOR_CODE = """float o; float f;
float3 c = FFWaterColor(Deep, Shallow, NoV, F0, OMin, OMax, o, f);
OutOpacity = o;
OutFresnel = f;
return c;"""
BANNER_CODE = "return FFBannerSway(NormalWS, Hang, Flutter, T, WP, Amp, AmpFlutter, Speed);"
FOLIAGE_WIND_CODE = "return FFFoliageWind(Sway, WP, T, Amp);"
FOLIAGE_COLOR_CODE = """float3 f;
float3 c = FFFoliageColor(Tex, TreeTint, Grad, Tone, Fill, f);
OutFill = f;
return c;"""
FLICKER_CODE = "return FFFlicker(T, WP, Amount);"
SKY_DIR_CODE = "return FFSkyDir(WP, Center);"
SKY_UVA_CODE = "return FFSkyCloudUV(Dir, Scale, Drift.xy, T);"
SKY_UVB_CODE = "return FFSkyCloudUVSun(Dir, Scale, Drift.xy, T, SunToward);"
SKY_SHADE_CODE = ("return FFSkyShade(Dir, SunToward, SunColor, Zenith, Horizon, GroundHorizon, GroundBottom, CloudLit, CloudShade, "
                  "CloudA, CloudB, Cover, Cirrus);")
RIDGE_CODE = "return VC * (1.0 - Detail + Detail * 2.0 * Noise) * Bright;"

# every body with its inputs / outputs, for check_hlsl.py:  name -> (code, [inputs], return type, [(extra out, type)])
CUSTOM_BODIES = {
    "surface": (SURFACE_CODE, SURFACE_INPUTS, "float3", [("OutRough", "float"), ("OutAO", "float")]),
    "macro": (MACRO_CODE, ["Noise", "Strength"], "float", []),
    "world_uv": (WORLD_UV_CODE, ["WP", "TileCm"], "float2", []),
    "normal": (NORMAL_CODE, ["N", "Strength"], "float3", []),
    "mask_uv": (MASK_UV_CODE, ["WP", "Rect"], "float2", []),
    "water_uv": (WATER_UV_CODE, ["WP", "T", "Dir", "Speed", "TileCm"], "float2", []),
    "water_normal": (WATER_NORMAL_CODE, ["N1", "N2", "Strength"], "float3", []),
    "water_color": (WATER_COLOR_CODE, ["Deep", "Shallow", "NoV", "F0", "OMin", "OMax"], "float3", [("OutOpacity", "float"), ("OutFresnel", "float")]),
    "banner": (BANNER_CODE, ["NormalWS", "Hang", "Flutter", "T", "WP", "Amp", "AmpFlutter", "Speed"], "float3", []),
    "foliage_wind": (FOLIAGE_WIND_CODE, ["Sway", "WP", "T", "Amp"], "float3", []),
    "foliage_color": (FOLIAGE_COLOR_CODE, ["Tex", "TreeTint", "Grad", "Tone", "Fill"], "float3", [("OutFill", "float3")]),
    "flicker": (FLICKER_CODE, ["T", "WP", "Amount"], "float", []),
    "sky_dir": (SKY_DIR_CODE, ["WP", "Center"], "float3", []),
    "sky_uva": (SKY_UVA_CODE, ["Dir", "Scale", "Drift", "T"], "float2", []),
    "sky_uvb": (SKY_UVB_CODE, ["Dir", "Scale", "Drift", "T", "SunToward"], "float2", []),
    "sky_shade": (SKY_SHADE_CODE, ["Dir", "SunToward", "SunColor", "Zenith", "Horizon", "GroundHorizon", "GroundBottom", "CloudLit",
                                   "CloudShade", "CloudA", "CloudB", "Cover", "Cirrus"], "float3", []),
    "ridge": (RIDGE_CODE, ["VC", "Noise", "Detail", "Bright"], "float3", []),
}
# input types for the DXC wrapper (default float3 unless listed)
SCALAR_INPUTS = {"Rough", "AO", "Grime", "GrimeAmount", "Wet", "Porosity", "Sat", "Bright", "Macro", "Halo", "RoughScale", "Noise", "Strength",
                 "TileCm", "T", "Speed", "NoV", "F0", "OMin", "OMax", "Hang", "Flutter", "Amp", "AmpFlutter", "Sway", "TreeTint", "Grad",
                 "Fill", "Amount", "Scale", "Cover", "Cirrus", "Detail"}


def _mat_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def _clear(mat):
    for _ in range(16):
        exprs = MEL.get_material_expressions(mat) or []
        if not exprs:
            return
        for e in exprs:
            MEL.delete_material_expression(mat, e)


def get_or_create_material(folder, name, force):
    path = f"{folder}/{name}"
    if C.asset_exists(path):
        mat = unreal.load_asset(path)
        if not force:
            return mat, False
        _clear(mat)
    else:
        mat = _mat_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return mat, True


class Graph:
    """Small helper around MaterialEditingLibrary for one material."""

    def __init__(self, mat, notes):
        self.mat = mat
        self.notes = notes

    def expr(self, cls, x, y, **props):
        e = MEL.create_material_expression(self.mat, cls, int(x), int(y))
        for k, v in props.items():
            C.set_prop(e, k, v, {"notes": self.notes})
        return e

    def link(self, a, a_out, b, b_in):
        """Connects a[a_out] -> b[b_in]; a_out may be a tuple of candidate output names (first that works wins)."""
        outs = a_out if isinstance(a_out, (tuple, list)) else (a_out,)
        for o in outs:
            try:
                if MEL.connect_material_expressions(a, o, b, b_in) is not False:
                    return True
            except Exception:  # noqa: BLE001
                pass
        self.notes.append(f"{self.mat.get_name()}: link failed {a_out!r} -> {b_in!r}")
        return False

    def out(self, a, a_out, prop):
        outs = a_out if isinstance(a_out, (tuple, list)) else (a_out,)
        for o in outs:
            try:
                if MEL.connect_material_property(a, o, prop) is not False:
                    return True
            except Exception:  # noqa: BLE001
                pass
        self.notes.append(f"{self.mat.get_name()}: output link failed {a_out!r} -> {prop}")
        return False

    # --- node factories
    def scalar(self, name, value, x, y, group="Env"):
        return self.expr(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=float(value), group=group)

    def vector(self, name, rgb, x, y, group="Env", alpha=1.0):
        return self.expr(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
                         default_value=unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), float(alpha)), group=group)

    def const(self, value, x, y):
        return self.expr(unreal.MaterialExpressionConstant, x, y, r=float(value))

    def const3(self, rgb, x, y):
        return self.expr(unreal.MaterialExpressionConstant3Vector, x, y,
                         constant=unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), 1.0))

    def texture(self, name, tex, sampler, x, y, uv=None, uv_out="", clamp=False):
        e = self.expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, group="Textures")
        if tex is not None:
            C.set_prop(e, "texture", tex, {"notes": self.notes})
        C.set_prop(e, "sampler_type", sampler, {"notes": self.notes})
        src = C.enum("SamplerSourceMode", "SSM_FROM_TEXTURE_ASSET") if clamp else C.enum("SamplerSourceMode", "SSM_WRAP_WORLD_GROUP_SETTINGS")
        C.set_prop(e, "sampler_source", src, {"notes": self.notes}, quiet=True)
        if uv is not None:
            self.link(uv, uv_out, e, "UVs")
        return e

    def custom(self, code, output_type, inputs, outputs, x, y, desc):
        e = self.expr(unreal.MaterialExpressionCustom, x, y)
        rep = {"notes": self.notes}
        C.set_prop(e, "code", code, rep)
        C.set_prop(e, "output_type", output_type, rep)
        C.set_prop(e, "description", desc, rep)
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
        C.set_prop(e, "inputs", ins, rep)
        C.set_prop(e, "additional_outputs", outs, rep)
        C.set_prop(e, "include_file_paths", [INCLUDE], rep)
        return e

    def multiply(self, a, a_out, b, b_out, x, y):
        m = self.expr(unreal.MaterialExpressionMultiply, x, y)
        self.link(a, a_out, m, "A")
        self.link(b, b_out, m, "B")
        return m

    def wire(self, node, mapping):
        """mapping: {input_name: (source_node, output_name)}"""
        for k, (src, out) in mapping.items():
            self.link(src, out, node, k)


RGB = ("RGB", "")                  # texture sample colour output: Epic names it "RGB" in Python (empty name = first output as fallback)
VCRGB = ("", "RGB")
F1, F2, F3 = CMOT.CMOT_FLOAT1, CMOT.CMOT_FLOAT2, CMOT.CMOT_FLOAT3


def _collection(g, mpc, x, y):
    """CollectionParameter node for MPC_Arena.Wetness, or None."""
    if mpc is None:
        return None
    try:
        n = g.expr(unreal.MaterialExpressionCollectionParameter, x, y)
        C.set_prop(n, "collection", mpc, {"notes": g.notes})
        C.set_prop(n, "parameter_name", "Wetness", {"notes": g.notes})
        return n
    except Exception as e:  # noqa: BLE001
        g.notes.append(f"CollectionParameter unavailable: {e}")
        return None


# ------------------------------------------------------------------------------------------------------------------ masters
def build_surface(mat, tex, notes, mpc=None, floor=False):
    """Textured opaque surface (see module doc).  floor=True adds the arena mask (T_Env_ArenaMask)."""
    g = Graph(mat, notes)
    X = -2400
    uv0 = g.expr(unreal.MaterialExpressionTextureCoordinate, X, 0, coordinate_index=0)
    uvs = g.scalar("UVScale", 1.0, X, -160, "Surface")
    uv = g.multiply(uv0, "", uvs, "", X + 220, -40)
    bc = g.texture("BaseColor", tex.get("BC"), ST.SAMPLERTYPE_COLOR, X + 500, -420, uv)
    nm = g.texture("Normal", tex.get("N"), ST.SAMPLERTYPE_NORMAL, X + 500, 0, uv)
    orm = g.texture("ORM", tex.get("ORM"), ST.SAMPLERTYPE_MASKS, X + 500, 420, uv)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, X, 700)
    mtile = g.scalar("MacroTileCm", 1100.0, X, 860, "Macro")
    nuv = g.custom(WORLD_UV_CODE, F2, ["WP", "TileCm"], [], X + 300, 760, "macro uv")
    g.wire(nuv, {"WP": (wp, ""), "TileCm": (mtile, "")})
    noise = g.texture("MacroNoise", tex.get("NOISE"), ST.SAMPLERTYPE_MASKS, X + 600, 760, nuv)
    mstr = g.scalar("MacroStrength", 0.12, X + 600, 980, "Macro")
    macro = g.custom(MACRO_CODE, F1, ["Noise", "Strength"], [], X + 900, 800, "macro tint")
    g.wire(macro, {"Noise": (noise, "R"), "Strength": (mstr, "")})
    vc = g.expr(unreal.MaterialExpressionVertexColor, X + 300, 1150)
    # parameters
    grime_amt = g.scalar("GrimeAmount", 1.0, X + 900, 1100, "Surface")
    grime_col = g.vector("GrimeColor", (0.62, 0.58, 0.48), X + 900, 1250, "Surface")
    wet_p = g.scalar("Wetness", 0.0, X + 900, 1400, "Surface")
    porosity = g.scalar("Porosity", 1.0, X + 900, 1520, "Surface")
    sat = g.scalar("Saturation", 0.85, X + 900, 1640, "Grade")
    tint = g.vector("Tint", (1.0, 1.0, 1.0), X + 900, 1760, "Grade")
    bright = g.scalar("Brightness", 1.0, X + 900, 1900, "Grade")
    rscale = g.scalar("RoughnessScale", 1.0, X + 900, 2020, "Surface")
    nstr = g.scalar("NormalStrength", 1.0, X + 500, 2150, "Surface")
    mscale = g.scalar("MetallicScale", 1.0, X + 500, 2270, "Surface")
    wet_src, wet_out = wet_p, ""
    coll = _collection(g, mpc, X + 900, 1400 + 140)
    if coll is not None:
        mx = g.expr(unreal.MaterialExpressionMax, X + 1150, 1450)
        g.link(wet_p, "", mx, "A")
        g.link(coll, "", mx, "B")
        wet_src, wet_out = mx, ""
    grime_in, grime_out = vc, "G"
    ao_src, ao_out = orm, "R"
    halo_node = g.const(0.0, X + 1150, 1850)
    halo = (halo_node, "")
    wet_final = (wet_src, wet_out)
    if floor:
        rect = g.vector("MaskRect", (-18.0, -18.0, 36.0), X, 1300, "Arena")
        muv = g.custom(MASK_UV_CODE, F2, ["WP", "Rect"], [], X + 300, 1350, "arena mask uv")
        g.wire(muv, {"WP": (wp, ""), "Rect": (rect, "")})
        mask = g.texture("ArenaMask", tex.get("MASK"), ST.SAMPLERTYPE_MASKS, X + 600, 1350, muv, clamp=True)
        dirt = g.scalar("DirtAmount", 0.9, X + 900, 2150, "Arena")
        splash = g.scalar("SplashWet", 0.55, X + 900, 2270, "Arena")
        aostr = g.scalar("ContactAO", 0.9, X + 900, 2390, "Arena")
        mx_g = g.expr(unreal.MaterialExpressionMax, X + 1250, 1100)            # grime = max(vertex G, mask G * DirtAmount)
        mul_g = g.multiply(mask, "G", dirt, "", X + 1100, 1000)
        g.link(vc, "G", mx_g, "A")
        g.link(mul_g, "", mx_g, "B")
        grime_in, grime_out = mx_g, ""
        # ao = orm.R * lerp(1, mask.R, ContactAO)
        lrp = g.expr(unreal.MaterialExpressionLinearInterpolate, X + 1100, 400)
        one = g.const(1.0, X + 900, 340)
        g.link(one, "", lrp, "A")
        g.link(mask, "R", lrp, "B")
        g.link(aostr, "", lrp, "Alpha")
        ao_mul = g.multiply(orm, "R", lrp, "", X + 1300, 420)
        ao_src, ao_out = ao_mul, ""
        # wet = max(wet, mask.B * SplashWet)
        sp = g.multiply(mask, "B", splash, "", X + 1100, 1500)
        mx_w = g.expr(unreal.MaterialExpressionMax, X + 1350, 1500)
        g.link(wet_src, wet_out, mx_w, "A")
        g.link(sp, "", mx_w, "B")
        wet_final = (mx_w, "")
        halo = (mask, "A")
    surf = g.custom(SURFACE_CODE, F3, SURFACE_INPUTS, [("OutRough", F1), ("OutAO", F1)], X + 1700, 600, "surface grade")
    g.wire(surf, {
        "Base": (bc, RGB), "Rough": (orm, "G"), "AO": (ao_src, ao_out), "Grime": (grime_in, grime_out),
        "GrimeAmount": (grime_amt, ""), "GrimeColor": (grime_col, ""), "Wet": wet_final, "Porosity": (porosity, ""),
        "Sat": (sat, ""), "Tint": (tint, ""), "Bright": (bright, ""), "Macro": (macro, ""), "Halo": halo, "RoughScale": (rscale, ""),
    })
    nrm = g.custom(NORMAL_CODE, F3, ["N", "Strength"], [], X + 1700, 0, "normal strength")
    g.wire(nrm, {"N": (nm, RGB), "Strength": (nstr, "")})
    metal = g.multiply(orm, "B", mscale, "", X + 1700, 1000)
    g.out(surf, "", MP.MP_BASE_COLOR)
    g.out(surf, "OutRough", MP.MP_ROUGHNESS)
    g.out(surf, "OutAO", MP.MP_AMBIENT_OCCLUSION)
    g.out(metal, "", MP.MP_METALLIC)
    g.out(nrm, "", MP.MP_NORMAL)


def build_plain(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    base = g.vector("BaseColor", (0.1, 0.1, 0.1), -600, -200, "Plain")
    metal = g.scalar("Metallic", 0.0, -600, 0, "Plain")
    rough = g.scalar("Roughness", 0.6, -600, 120, "Plain")
    g.out(base, "", MP.MP_BASE_COLOR)
    g.out(metal, "", MP.MP_METALLIC)
    g.out(rough, "", MP.MP_ROUGHNESS)


def build_glow(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    col = g.vector("GlowColor", (1.0, 0.52, 0.18), -900, -100, "Glow")
    inten = g.scalar("GlowIntensity", 3.0, -900, 60, "Glow")
    amt = g.scalar("Flicker", 0.12, -900, 180, "Glow")
    t = g.expr(unreal.MaterialExpressionTime, -900, 300)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, -900, 420)
    fl = g.custom(FLICKER_CODE, F1, ["T", "WP", "Amount"], [], -600, 300, "flicker")
    g.wire(fl, {"T": (t, ""), "WP": (wp, ""), "Amount": (amt, "")})
    c1 = g.multiply(col, "", inten, "", -400, 0)
    em = g.multiply(c1, "", fl, "", -200, 100)
    base = g.multiply(col, "", g.const(0.25, -400, 200), "", -200, -100)
    g.out(base, "", MP.MP_BASE_COLOR)
    g.out(em, "", MP.MP_EMISSIVE_COLOR)
    g.out(g.const(0.45, -200, 300), "", MP.MP_ROUGHNESS)


def build_water(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    C.set_prop(mat, "blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT, {"notes": notes})
    C.set_prop(mat, "translucency_lighting_mode", C.enum("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"),
               {"notes": notes}, quiet=True)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, -2000, 0)
    t = g.expr(unreal.MaterialExpressionTime, -2000, 160)
    d1 = g.vector("Dir1", (0.9, 0.35, 0.0), -2000, 300, "Water")
    d2 = g.vector("Dir2", (-0.4, 0.8, 0.0), -2000, 420, "Water")
    s1 = g.scalar("Speed1", 0.012, -2000, 540, "Water")
    s2 = g.scalar("Speed2", 0.008, -2000, 600, "Water")
    t1 = g.scalar("Tile1Cm", 450.0, -2000, 660, "Water")
    t2 = g.scalar("Tile2Cm", 230.0, -2000, 720, "Water")
    uv1 = g.custom(WATER_UV_CODE, F2, ["WP", "T", "Dir", "Speed", "TileCm"], [], -1700, 100, "water uv 1")
    g.wire(uv1, {"WP": (wp, ""), "T": (t, ""), "Dir": (d1, ""), "Speed": (s1, ""), "TileCm": (t1, "")})
    uv2 = g.custom(WATER_UV_CODE, F2, ["WP", "T", "Dir", "Speed", "TileCm"], [], -1700, 400, "water uv 2")
    g.wire(uv2, {"WP": (wp, ""), "T": (t, ""), "Dir": (d2, ""), "Speed": (s2, ""), "TileCm": (t2, "")})
    n1 = g.texture("WaterNormal", tex.get("WATERN"), ST.SAMPLERTYPE_NORMAL, -1400, 100, uv1)
    n2 = g.texture("WaterNormal2", tex.get("WATERN"), ST.SAMPLERTYPE_NORMAL, -1400, 400, uv2)
    strength = g.scalar("RippleStrength", 0.7, -1400, 650, "Water")
    nrm = g.custom(WATER_NORMAL_CODE, F3, ["N1", "N2", "Strength"], [], -1100, 200, "water normal")
    g.wire(nrm, {"N1": (n1, RGB), "N2": (n2, RGB), "Strength": (strength, "")})
    vn = g.expr(unreal.MaterialExpressionVertexNormalWS, -1400, 900)
    cv = g.expr(unreal.MaterialExpressionCameraVectorWS, -1400, 1020) if hasattr(unreal, "MaterialExpressionCameraVectorWS") \
        else g.expr(unreal.MaterialExpressionCameraVector, -1400, 1020)
    nov = g.expr(unreal.MaterialExpressionDotProduct, -1100, 950)
    g.link(vn, "", nov, "A")
    g.link(cv, "", nov, "B")
    deep = g.vector("DeepColor", C.linear((0.05, 0.28, 0.32)), -1100, 1150, "Water")
    shallow = g.vector("ShallowColor", C.linear((0.22, 0.62, 0.62)), -1100, 1270, "Water")
    f0 = g.scalar("F0", 0.02, -1100, 1390, "Water")
    omin = g.scalar("OpacityMin", 0.62, -1100, 1450, "Water")
    omax = g.scalar("OpacityMax", 0.92, -1100, 1510, "Water")
    col = g.custom(WATER_COLOR_CODE, F3, ["Deep", "Shallow", "NoV", "F0", "OMin", "OMax"], [("OutOpacity", F1), ("OutFresnel", F1)], -700, 1000, "water colour")
    g.wire(col, {"Deep": (deep, ""), "Shallow": (shallow, ""), "NoV": (nov, ""), "F0": (f0, ""), "OMin": (omin, ""), "OMax": (omax, "")})
    sky = g.vector("SkyReflection", C.linear((0.55, 0.68, 0.85)), -700, 1300, "Water")
    emis = g.multiply(sky, "", col, "OutFresnel", -400, 1250)
    g.out(col, "", MP.MP_BASE_COLOR)
    g.out(col, "OutOpacity", MP.MP_OPACITY)
    g.out(emis, "", MP.MP_EMISSIVE_COLOR)
    g.out(nrm, "", MP.MP_NORMAL)
    g.out(g.scalar("Roughness", 0.06, -400, 1450, "Water"), "", MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular", 0.6, -400, 1550, "Water"), "", MP.MP_SPECULAR)


def build_banner(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    C.set_prop(mat, "blend_mode", unreal.BlendMode.BLEND_MASKED, {"notes": notes})
    C.set_prop(mat, "two_sided", True, {"notes": notes})
    C.set_prop(mat, "opacity_mask_clip_value", 0.5, {"notes": notes}, quiet=True)
    uv = g.expr(unreal.MaterialExpressionTextureCoordinate, -1200, 0, coordinate_index=0)
    atlas = g.texture("Atlas", tex.get("BANNERS"), ST.SAMPLERTYPE_COLOR, -900, 0, uv)
    tint = g.vector("Tint", (1.0, 1.0, 1.0), -900, 250, "Cloth")
    base = g.multiply(atlas, RGB, tint, "", -600, 0)
    fill = g.scalar("BackFill", 0.1, -900, 400, "Cloth")
    em = g.multiply(base, "", fill, "", -400, 200)
    g.out(base, "", MP.MP_BASE_COLOR)
    g.out(em, "", MP.MP_EMISSIVE_COLOR)
    g.out(atlas, "A", MP.MP_OPACITY_MASK)
    g.out(g.scalar("Roughness", 0.88, -400, 400, "Cloth"), "", MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular", 0.2, -400, 500, "Cloth"), "", MP.MP_SPECULAR)
    # sway
    vn = g.expr(unreal.MaterialExpressionVertexNormalWS, -1200, 700)
    vc = g.expr(unreal.MaterialExpressionVertexColor, -1200, 850)
    t = g.expr(unreal.MaterialExpressionTime, -1200, 1000)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, -1200, 1120)
    amp = g.scalar("SwayCm", 4.0, -1200, 1250, "Wind")
    ampf = g.scalar("FlutterCm", 55.0, -1200, 1330, "Wind")
    spd = g.scalar("SwaySpeed", 0.9, -1200, 1410, "Wind")
    sw = g.custom(BANNER_CODE, F3, ["NormalWS", "Hang", "Flutter", "T", "WP", "Amp", "AmpFlutter", "Speed"], [], -800, 900, "cloth sway")
    g.wire(sw, {"NormalWS": (vn, ""), "Hang": (vc, "R"), "Flutter": (vc, "G"), "T": (t, ""), "WP": (wp, ""), "Amp": (amp, ""),
                "AmpFlutter": (ampf, ""), "Speed": (spd, "")})
    g.out(sw, "", MP.MP_WORLD_POSITION_OFFSET)


def build_foliage(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    C.set_prop(mat, "blend_mode", unreal.BlendMode.BLEND_MASKED, {"notes": notes})
    C.set_prop(mat, "two_sided", True, {"notes": notes})
    C.set_prop(mat, "opacity_mask_clip_value", 0.5, {"notes": notes}, quiet=True)
    uv = g.expr(unreal.MaterialExpressionTextureCoordinate, -1500, 0, coordinate_index=0)
    atlas = g.texture("Atlas", tex.get("FOLIAGE"), ST.SAMPLERTYPE_COLOR, -1200, 0, uv)
    vc = g.expr(unreal.MaterialExpressionVertexColor, -1200, 300)
    tone = g.vector("Tone", (1.0, 1.0, 1.0), -1200, 500, "Leaf")
    fill = g.scalar("Fill", 0.35, -1200, 620, "Leaf")
    col = g.custom(FOLIAGE_COLOR_CODE, F3, ["Tex", "TreeTint", "Grad", "Tone", "Fill"], [("OutFill", F3)], -800, 100, "leaf colour")
    g.wire(col, {"Tex": (atlas, RGB), "TreeTint": (vc, "R"), "Grad": (vc, "G"), "Tone": (tone, ""), "Fill": (fill, "")})
    g.out(col, "", MP.MP_BASE_COLOR)
    g.out(col, "OutFill", MP.MP_EMISSIVE_COLOR)
    g.out(atlas, "A", MP.MP_OPACITY_MASK)
    g.out(g.scalar("Roughness", 0.85, -500, 300, "Leaf"), "", MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular", 0.1, -500, 400, "Leaf"), "", MP.MP_SPECULAR)
    t = g.expr(unreal.MaterialExpressionTime, -1200, 800)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, -1200, 920)
    amp = g.scalar("WindCm", 14.0, -1200, 1040, "Wind")
    wind = g.custom(FOLIAGE_WIND_CODE, F3, ["Sway", "WP", "T", "Amp"], [], -800, 800, "wind")
    g.wire(wind, {"Sway": (vc, "B"), "WP": (wp, ""), "T": (t, ""), "Amp": (amp, "")})
    g.out(wind, "", MP.MP_WORLD_POSITION_OFFSET)


def build_ridge(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    C.set_prop(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT, {"notes": notes})
    C.set_prop(mat, "two_sided", True, {"notes": notes})
    uv = g.expr(unreal.MaterialExpressionTextureCoordinate, -1200, 0, coordinate_index=0)
    tile = g.scalar("DetailTiling", 40.0, -1200, 160, "Ridge")
    uvm = g.multiply(uv, "", tile, "", -950, 60)
    noise = g.texture("Noise", tex.get("NOISE"), ST.SAMPLERTYPE_MASKS, -700, 60, uvm)
    vc = g.expr(unreal.MaterialExpressionVertexColor, -700, -200)
    detail = g.scalar("Detail", 0.22, -700, 300, "Ridge")
    bright = g.scalar("Brightness", 1.0, -700, 400, "Ridge")
    col = g.custom(RIDGE_CODE, F3, ["VC", "Noise", "Detail", "Bright"], [], -300, 0, "ridge colour")
    g.wire(col, {"VC": (vc, VCRGB), "Noise": (noise, "R"), "Detail": (detail, ""), "Bright": (bright, "")})
    g.out(col, "", MP.MP_EMISSIVE_COLOR)


# sky colour defaults (design values from the mock-up, converted to linear): late-day warm horizon, cool zenith
SKY_DEFAULTS = {
    "Zenith": C.linear((0.24, 0.38, 0.60)), "Horizon": C.linear((0.92, 0.74, 0.58)), "GroundHorizon": C.linear((0.74, 0.62, 0.50)),
    "GroundBottom": C.linear((0.17, 0.15, 0.13)), "CloudLit": C.linear((1.0, 0.80, 0.58)), "CloudShade": C.linear((0.42, 0.44, 0.58)),
    "SunColor": (1.0, 0.72, 0.42),
}
SUN_TOWARD = (-0.8713, -0.3171, 0.3746)           # unit vector TO the sun (Unreal axes) = fx FFKeyDir; opposite of the sun Rotator in level.py


def build_sky(mat, tex, notes, mpc=None):
    g = Graph(mat, notes)
    C.set_prop(mat, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT, {"notes": notes})
    C.set_prop(mat, "two_sided", True, {"notes": notes})
    C.set_prop(mat, "is_sky", True, {"notes": notes}, quiet=True)
    wp = g.expr(unreal.MaterialExpressionWorldPosition, -2000, 0)
    ctr = g.expr(unreal.MaterialExpressionObjectPositionWS, -2000, 140)
    t = g.expr(unreal.MaterialExpressionTime, -2000, 280)
    sun = g.vector("SunToward", SUN_TOWARD, -2000, 420, "Sky")
    scale = g.scalar("CloudScale", 1.6, -2000, 560, "Sky")
    drift = g.vector("CloudDrift", (0.0016, 0.0007, 0.0), -2000, 640, "Sky")
    cover = g.scalar("CloudCover", 0.5, -2000, 760, "Sky")
    cirrus = g.scalar("CirrusAmount", 0.6, -2000, 840, "Sky")
    d = g.custom(SKY_DIR_CODE, F3, ["WP", "Center"], [], -1700, 60, "sky dir")
    g.wire(d, {"WP": (wp, ""), "Center": (ctr, "")})
    ua = g.custom(SKY_UVA_CODE, F2, ["Dir", "Scale", "Drift", "T"], [], -1400, 100, "cloud uv A")
    g.wire(ua, {"Dir": (d, ""), "Scale": (scale, ""), "Drift": (drift, ""), "T": (t, "")})
    ub = g.custom(SKY_UVB_CODE, F2, ["Dir", "Scale", "Drift", "T", "SunToward"], [], -1400, 400, "cloud uv B")
    g.wire(ub, {"Dir": (d, ""), "Scale": (scale, ""), "Drift": (drift, ""), "T": (t, ""), "SunToward": (sun, "")})
    ca = g.texture("Clouds", tex.get("CLOUDS"), ST.SAMPLERTYPE_MASKS, -1100, 100, ua)
    cb = g.texture("Clouds2", tex.get("CLOUDS"), ST.SAMPLERTYPE_MASKS, -1100, 400, ub)
    cols = {}
    y = 1000
    for name, val in SKY_DEFAULTS.items():
        cols[name] = g.vector(name, val, -2000, y, "Sky")
        y += 110
    shade = g.custom(SKY_SHADE_CODE, F3, ["Dir", "SunToward", "SunColor", "Zenith", "Horizon", "GroundHorizon", "GroundBottom", "CloudLit",
                                         "CloudShade", "CloudA", "CloudB", "Cover", "Cirrus"], [], -600, 400, "sky shade")
    g.wire(shade, {"Dir": (d, ""), "SunToward": (sun, ""), "CloudA": (ca, RGB), "CloudB": (cb, RGB), "Cover": (cover, ""), "Cirrus": (cirrus, ""),
                   **{k: (v, "") for k, v in cols.items()}})
    g.out(shade, "", MP.MP_EMISSIVE_COLOR)


MASTERS = {
    "M_Env_Surface": lambda m, tex, n, mpc: build_surface(m, tex, n, mpc, floor=False),
    "M_Env_Floor": lambda m, tex, n, mpc: build_surface(m, tex, n, mpc, floor=True),
    "M_Env_Plain": build_plain,
    "M_Env_Glow": build_glow,
    "M_Env_Water": build_water,
    "M_Env_Banner": build_banner,
    "M_Env_Foliage": build_foliage,
    "M_Env_Ridge": build_ridge,
    "M_Env_Sky": build_sky,
}

# ----------------------------------------------------------------------------------------------------------------- instances
# slot -> (master, texture set (T_Env_<Set>_*), scalars, vectors)
SLOTS = {
    "Floor": ("M_Env_Floor", "Flagstone", dict(Saturation=0.80, GrimeAmount=0.7, MacroStrength=0.32), dict(Tint=(1.0, 0.96, 0.90))),
    "StoneWall": ("M_Env_Surface", "Wall", dict(Saturation=0.80, GrimeAmount=0.85), dict(Tint=(1.0, 0.97, 0.92))),
    "StoneCap": ("M_Env_Surface", "LedgeCap", dict(Saturation=0.82, GrimeAmount=0.4), dict()),
    "Plaster": ("M_Env_Surface", "Plaster", dict(Saturation=0.95, GrimeAmount=1.0, MacroStrength=0.20), dict(GrimeColor=(0.55, 0.52, 0.42))),
    "Timber": ("M_Env_Surface", "Timber", dict(Saturation=0.9, GrimeAmount=0.5), dict(Tint=(0.92, 0.85, 0.78))),
    "RoofTile": ("M_Env_Surface", "RoofTile", dict(Saturation=0.85, GrimeAmount=0.5), dict()),
    "PoolTile": ("M_Env_Surface", "PoolTile", dict(Saturation=1.0, GrimeAmount=0.7, Porosity=0.3), dict()),
    "Metal": ("M_Env_Surface", "MetalPlate", dict(Saturation=0.9, GrimeAmount=0.4), dict()),
    "Ground": ("M_Env_Surface", "Ground", dict(Saturation=0.85, GrimeAmount=0.0, MacroStrength=0.22, MacroTileCm=2500.0), dict()),
    "Rock": ("M_Env_Surface", "Rock", dict(Saturation=0.85, GrimeAmount=0.3), dict()),
    "Bark": ("M_Env_Surface", "Timber", dict(Saturation=0.7, GrimeAmount=0.0), dict(Tint=(0.55, 0.5, 0.45))),
    "Iron": ("M_Env_Plain", None, dict(Metallic=0.75, Roughness=0.5), dict(BaseColor=(0.05, 0.05, 0.055))),
    "Bronze": ("M_Env_Plain", None, dict(Metallic=0.85, Roughness=0.38), dict(BaseColor=(0.36, 0.22, 0.09))),
    "Glow": ("M_Env_Glow", None, dict(GlowIntensity=3.0, Flicker=0.12), dict(GlowColor=(1.0, 0.52, 0.18))),
    "Banner": ("M_Env_Banner", None, dict(), dict()),
    "Foliage": ("M_Env_Foliage", None, dict(), dict()),
    "Ridge": ("M_Env_Ridge", None, dict(), dict()),
    "Water": ("M_Env_Water", None, dict(), dict()),
    "Sky": ("M_Env_Sky", None, dict(), dict()),
}


def _create_mpc(report, force):
    """MPC_Arena with scalar 'Wetness'.  Returns the asset or None."""
    path = f"{C.MAT_DIR}/MPC_Arena"
    try:
        if C.asset_exists(path) and not force:
            report["skipped"].append(path)
            return unreal.load_asset(path)
        factory = getattr(unreal, "MaterialParameterCollectionFactoryNew", None)
        cls = getattr(unreal, "MaterialParameterCollection", None)
        if factory is None or cls is None:
            report["notes"].append("MaterialParameterCollection classes not available: arena wetness parameter skipped")
            return None
        mpc = unreal.load_asset(path) if C.asset_exists(path) else _mat_tools().create_asset("MPC_Arena", C.MAT_DIR, cls, factory())
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", "Wetness")
        p.set_editor_property("default_value", 0.0)
        mpc.set_editor_property("scalar_parameters", [p])
        C.save_asset(mpc)
        report["created"].append(path)
        return mpc
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"MPC_Arena not created ({e}); materials use the instance Wetness only")
        return None


# the scanned paving / plaster are pale: under auto exposure they read near-white next to the fighters
PHOTO_BRIGHTNESS = {"Floor": 0.58, "Plaster": 0.85, "StoneCap": 0.85}


def _photo_manifest():
    """SourceArt/Environment/PolyHaven/polyhaven.json (Tools/world/fetch_polyhaven.py) or {} when the sets were not fetched."""
    import json
    import os
    p = os.path.join(os.path.dirname(C.project_paths()["textures"].rstrip("/")), "PolyHaven", "polyhaven.json")
    try:
        with open(p) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def build_all(force, texs, report):
    """texs: {texture name: asset path}.  Returns {slot: MaterialInstanceConstant}."""
    C.make_dirs(C.ENV_ROOT, C.MAT_DIR)
    # every texture parameter of a master needs a valid default texture (a missing one is a material compile error that also breaks
    # the instances): the surface masters default to the flagstone set, instances override per slot
    tex = {
        "BC": T.tex("T_Env_Flagstone_BC"), "N": T.tex("T_Env_Flagstone_N"), "ORM": T.tex("T_Env_Flagstone_ORM"),
        "NOISE": T.tex("T_Env_Noise"), "CLOUDS": T.tex("T_Env_Clouds"), "MASK": T.tex("T_Env_ArenaMask"), "WATERN": T.tex("T_Env_WaterN"),
        "BANNERS": T.tex("T_Env_Banners_BC"), "FOLIAGE": T.tex("T_Env_Foliage_BC"),
    }
    mpc = _create_mpc(report, force)
    masters = {}
    for name, builder in MASTERS.items():
        path = f"{C.MAT_DIR}/{name}"
        try:
            mat, rebuild = get_or_create_material(C.MAT_DIR, name, force)
            masters[name] = mat
            if not rebuild:
                report["skipped"].append(path)
                continue
            builder(mat, tex, report["notes"], mpc) if name in ("M_Env_Surface", "M_Env_Floor") else builder(mat, tex, report["notes"])
            MEL.recompile_material(mat)
            try:
                st = MEL.get_statistics(mat)
                report["notes"].append(f"{name}: pixel instr {st.get_editor_property('num_pixel_shader_instructions')}, "
                                       f"samplers {st.get_editor_property('num_samplers')}")
            except Exception:  # noqa: BLE001
                pass
            C.save_asset(mat)
            report["created"].append(path)
        except Exception as e:  # noqa: BLE001
            import traceback
            report["failed"].append({"item": path, "error": f"{e}\n{traceback.format_exc()}"})
    photo = _photo_manifest()
    instances = {}
    for slot, (master, texset, scalars, vectors) in SLOTS.items():
        name = f"MI_Env_{slot}"
        path = f"{C.MAT_DIR}/{name}"
        parent = masters.get(master)
        if parent is None:
            report["failed"].append({"item": path, "error": f"master {master} missing"})
            continue
        try:
            if C.asset_exists(path):
                mi = unreal.load_asset(path)
                instances[slot] = mi
                if not force:
                    report["skipped"].append(path)
                    continue
            else:
                mi = _mat_tools().create_asset(name, C.MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            instances[slot] = mi
            MEL.set_material_instance_parent(mi, parent)
            if texset:
                for suffix, pname in (("BC", "BaseColor"), ("N", "Normal"), ("ORM", "ORM")):
                    t = T.tex(f"T_Env_{texset}_{suffix}")
                    if t is not None:
                        MEL.set_material_instance_texture_parameter_value(mi, pname, t)
                    else:
                        report["notes"].append(f"{name}: texture T_Env_{texset}_{suffix} missing")
            scalars = dict(scalars)
            if slot in photo.get("uv_scale", {}):
                # photo-scanned set: real-world repeat size, and the procedural grading (desaturate + painted grime) toned down
                scalars["UVScale"] = photo["uv_scale"][slot]
                scalars["Saturation"] = 1.0
                scalars["GrimeAmount"] = scalars.get("GrimeAmount", 0.5) * 0.8
                if slot in PHOTO_BRIGHTNESS:
                    scalars["Brightness"] = PHOTO_BRIGHTNESS[slot]
            for k, v in scalars.items():
                MEL.set_material_instance_scalar_parameter_value(mi, k, float(v))
            for k, v in vectors.items():
                MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(float(v[0]), float(v[1]), float(v[2]), 1.0))
            MEL.update_material_instance(mi)
            C.save_asset(mi)
            report["created"].append(path)
        except Exception as e:  # noqa: BLE001
            import traceback
            report["failed"].append({"item": path, "error": f"{e}\n{traceback.format_exc()}"})
    return instances
