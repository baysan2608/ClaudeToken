"""Fourfold FX - material / asset specification (pure data, no `unreal` import).

Read by the editor builder (fourfold.fx.build_all) AND by the container-side checks:
  * unreal/Tools/vfx/shader_check/check_shaders.py wraps every Custom-node `code` below exactly like Unreal's material
    compiler does (one function per node, texture objects as Texture2D + SamplerState, additional outputs as inout)
    and compiles it with DXC (DXIL ps/vs_6_0 and SPIR-V, HLSL 2018 and 2021);
  * the parameter names are checked against the C++ logic (Private/Logic/FxTypes.h kParamNames).

Graph vocabulary (`inputs` values of a custom node):
  uv0 uv1 uv2        TextureCoordinate 0 / 1 / 2                                     float2
  vc                 VertexColor RGB                                                 float3
  vca                vertex alpha = TextureCoordinate 3 .x (FxUeConvert copies the vertex alpha there)  float
  time               Time                                                            float
  lpos               PreSkinnedLocalPosition (vertex stage only; cm)                 float3
  lpos_ps            PreSkinnedLocalPosition through a VertexInterpolator (pixel)   float3
  nrm_local          PreSkinnedNormal (vertex stage only)                            float3
  nrm_ws             VertexNormalWS                                                  float3
  cam                CameraVectorWS (unit, toward the camera)                        float3
  wpos_cr            WorldPosition, camera relative (derivatives)                    float3
  ax ay az           TransformVector local -> world of (1,0,0) / (0,1,0) / (0,0,1)   float3
  p:<Name>           ScalarParameter <Name>                                          float
  v:<Name>           VectorParameter <Name> (RGB)                                    float3
  tex:<Name>         TextureObjectParameter <Name> (code uses <Name> + <Name>Sampler) Texture2D
  node:<id>          the main output of another node of this material                (its type)
  nmap:<Name>        TextureSampleParameter2D <Name> (normal) on uv0 -> TransformVector tangent -> world   float3
`outputs` maps a material property to (node id, output name; "" = main output). WorldPositionOffset values are LOCAL
offsets in cm: the builder inserts a TransformVector local -> world.
"""

FX_ROOT = "/Game/Fourfold/FX"
MATERIAL_DIR = FX_ROOT + "/Materials"
TEXTURE_DIR = FX_ROOT + "/Textures"
MESH_DIR = FX_ROOT + "/Meshes"
INSTANCE_DIR = FX_ROOT + "/Materials/Instances"

ENGINE_DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal"

# ------------------------------------------------------------------------------------------------ textures
# name -> (source file under SourceArt/VFX, kind). kind: "noise" (linear masks), "flipbook" (linear RGBA, packed),
# "normal" (tangent-space normal map; Tools/vfx/meshes.py writes it in the DirectX convention (green flipped from
# Blender's OpenGL bake), so it imports with flip_green_channel off).
TEXTURES = {
    "T_FX_Noise": ("Textures/T_FX_Noise.png", "noise"),
    "T_FX_FB_smoke_puff": ("Flipbooks/T_FX_FB_smoke_puff.png", "flipbook"),
    "T_FX_FB_steam_puff": ("Flipbooks/T_FX_FB_steam_puff.png", "flipbook"),
    "T_FX_FB_dust_puff": ("Flipbooks/T_FX_FB_dust_puff.png", "flipbook"),
    "T_FX_FB_sand_burst": ("Flipbooks/T_FX_FB_sand_burst.png", "flipbook"),
    "T_FX_FB_fire_loop": ("Flipbooks/T_FX_FB_fire_loop.png", "flipbook"),
    "T_FX_FB_fire_burst": ("Flipbooks/T_FX_FB_fire_burst.png", "flipbook"),
    "T_FX_FB_explosion": ("Flipbooks/T_FX_FB_explosion.png", "flipbook"),
    "T_FX_FB_water_splash": ("Flipbooks/T_FX_FB_water_splash.png", "flipbook"),
    "T_FX_FB_puff_atlas": ("Flipbooks/T_FX_FB_puff_atlas.png", "flipbook"),
}
for _i in range(8):
    TEXTURES[f"T_FX_rock_{_i}_N"] = (f"Meshes/T_FX_rock_{_i}_N.png", "normal")

# ------------------------------------------------------------------------------------------------ meshes
# Static meshes (FBX in SourceArt/VFX/Meshes). `mat` = the master their slot-0 instance derives from; `normal` = baked
# normal map set on that instance (parameter RockNormal / CrystalNormal). The FX actor creates its dynamic instance from
# the mesh's own slot material when that instance derives from the slot master (keeps the baked maps).
MESHES = {}
for _i in range(8):
    MESHES[f"SM_FX_rock_{_i}"] = {"fbx": f"Meshes/SM_FX_rock_{_i}.fbx", "mat": "M_FX_Rock",
                                  "normal": ("RockNormal", f"T_FX_rock_{_i}_N")}
MESHES["SM_FX_spike"] = {"fbx": "Meshes/SM_FX_spike.fbx", "mat": "M_FX_Rock", "normal": None}
for _n in ("crystal_0", "crystal_1", "ice_shard"):
    MESHES[f"SM_FX_{_n}"] = {"fbx": f"Meshes/SM_FX_{_n}.fbx", "mat": "M_FX_Crystal", "normal": None}
for _n in ("disc", "lance", "plate", "caltrop"):
    MESHES[f"SM_FX_{_n}"] = {"fbx": f"Meshes/SM_FX_{_n}.fbx", "mat": "M_FX_Metal", "normal": None}
MESHES["SM_FX_chip"] = {"fbx": "Meshes/SM_FX_chip.fbx", "mat": "M_FX_Rock", "normal": None}


# ------------------------------------------------------------------------------------------------ materials
def _p(*names):
    return {n: "p:" + n for n in names}


def _v(*names):
    return {n: "v:" + n for n in names}


def _merge(*ds):
    out = {}
    for d in ds:
        out.update(d)
    return out


ROCK_WPO = """return FFRockOffset(Pos, float3(UV1.x, UV1.y, UV2.x) * 100.0, UV2.y, VC.x, Time, Melt, Crust, Seed, Rise,
	RiseHeight * 100.0, Fade);"""
ROCK_SURF = """FFSurf s = FFRockSurface(Noise, NoiseSampler, Pos, float3(UV1.x, UV1.y, UV2.x) * 100.0, N, Baked, AX, AY, AZ, Time,
	Heat, Melt, Crust, Damage, Frost, Seed, Detail, Glass, Tint);
OutEmissive = s.emissive * GlowScale;
OutNormal = s.normal;
OutRough = s.rough;
OutSpec = s.spec;
return s.albedo;"""

LAVA_WPO = "return FFLavaStripOffset(UV0, UV2, NL, Melt, Crust, Boil);"
LAVA_SURF = """FFSurf s = FFLavaStripSurface(Noise, NoiseSampler, UV0, UV2, N.z, Melt, Crust, Flow, Boil, Seed, GlowScale);
OutEmissive = s.emissive;
OutNormal = s.normal;
OutRough = s.rough;
OutSpec = s.spec;
return s.albedo;"""

METAL_WPO = "return FFShrinkOffset(Pos, Fade);"
METAL_SURF = """float smear = saturate(abs(Spin) / 40.0);
float2 uv = UV0 + float2(Seed * 0.37, Seed * 0.11);
float4 n1 = Noise.Sample(NoiseSampler, float2(uv.x * lerp(0.15, 0.01, smear), uv.y * 28.0));
float4 n2 = Noise.Sample(NoiseSampler, uv * 1.7 + 0.4);
FFSurf s = FFMetalSurface(UV0, n1, n2, saturate(dot(N, Cam)), Heat, Spin);
OutEmissive = s.emissive;
OutNormal = s.normal;
OutRough = s.rough;
OutMetal = s.metal;
return s.albedo;"""

CRYSTAL_WPO = "return FFCrystalOffset(UV1, Rise, RiseHeight);"
CRYSTAL_SHADE = """float4 nz = Noise.Sample(NoiseSampler, float2(UV0.x * 0.6 + UV1.x * 7.0, UV0.y * 1.4 + UV1.x * 3.0));
float4 r = FFCrystalShade(UV0, UV1, normalize(N), Cam, nz, Tint, Frost, Heat, Opacity, Glow, Shatter, Fade);
OutOpacity = r.w;
return FFRGB(r);"""

WATER_WPO = "return FFWaterOffset(Pos, NL, UV0, UV2, Shape, Time, Flow, Frozen, Detail);"
WATER_SHADE = """float3 facet = cross(ddx(WP), ddy(WP));
float4 r = FFWaterShade(Noise, NoiseSampler, UV0, UV2, Pos, N, facet, Cam, Shape, Time, Flow, Frozen, Seed, Fade);
OutOpacity = r.w;
return FFRGB(r);"""

GSTRIP_SURF = """float2 uv = float2(UV0.x - Flow + Seed * 3.1, UV0.y + Seed * 1.7);
float4 n1 = Noise.Sample(NoiseSampler, uv * 0.6);
float4 n2 = Noise.Sample(NoiseSampler, uv * 2.9 + 0.37);
FFSurf s = FFGroundStripSurface(UV0, UV2, n1, n2, Style, Flow, Crust);
OutEmissive = s.emissive;
OutNormal = s.normal;
OutRough = s.rough;
OutSpec = s.spec;
return s.albedo;"""

VINE_WPO = "return FFVineOffset(UV1, Phase, Frozen);"
VINE_SURF = """float2 uv = float2(UV0.x + UV0.y * 0.6, UV0.y * 1.5) + UV1.y * 5.0;
float4 n1 = Noise.Sample(NoiseSampler, uv * float2(1.0, 0.7));
float4 n2 = Noise.Sample(NoiseSampler, uv * 3.1 + 0.4);
FFSurf s = FFVineSurface(UV0, UV1, n1, n2, saturate(dot(N, Cam)), Burn, Frozen);
OutEmissive = s.emissive;
OutNormal = s.normal;
OutRough = s.rough;
OutSpec = s.spec;
return s.albedo;"""

FLAME_WPO = "return FFFlameOffset(Pos, UV0, UV2, float4(VC, VCA), Style, Shape, Age, Scroll, Core, Seed);"
FLAME_SHADE = """float4 r = FFFlameShade(Noise, NoiseSampler, UV0, float4(VC, VCA), abs(dot(N, Cam)), Style, Age, Scroll, Intensity,
	Core, Cover, Seed, Color, Color2, Color3, Color4, EmissiveScale);
OutOpacity = r.w;
return FFRGB(r);"""

FLIPBOOK_PAIR = """float2 ua;
float2 ub;
float bl;
FFFlipbookUV2(UV0, min(UV1.x, 62.999), 8.0, ua, ub, bl);
float4 a = Flipbook.Sample(FlipbookSampler, ua);
float4 b = Flipbook.Sample(FlipbookSampler, ub);
"""
FIRE_SPRITE_SHADE = FLIPBOOK_PAIR + """float4 r = FFFireSpriteShade(a, b, bl, float4(VC, VCA), UV1.y, Intensity, Tint, Color, Color2, Color3, Color4,
	EmissiveScale);
OutOpacity = r.w;
return FFRGB(r);"""

SMOKE_SHADE = """float2 ua;
float2 ub;
float bl;
if (Style > 0.5)
{
	ua = FFFlipbookFrameUV(UV0, UV1.x, 2.0);
	ub = ua;
	bl = 0.0;
}
else
{
	FFFlipbookUV2(UV0, min(UV1.x, 62.999), 8.0, ua, ub, bl);
}
float4 a = Flipbook.Sample(FlipbookSampler, ua);
float4 b = Flipbook.Sample(FlipbookSampler, ub);
float4 nz = Noise.Sample(NoiseSampler, UV0 * 0.75 + float2(UV2.y * 3.7 + Phase * 0.03, UV2.y * 5.1 - Phase * 0.02));
float4 r = FFSmokeShade(a, b, bl, float4(VC, VCA), nz, Style, UV2.x, Opacity, Erosion, Color, dot(-Cam, FFKeyDir()));
OutOpacity = r.w;
return FFRGB(r);"""

SPLASH_SHADE = FLIPBOOK_PAIR + """float4 r = FFSplashShade(a, b, bl, float4(VC, VCA), Color, dot(-Cam, FFKeyDir()));
OutOpacity = r.w;
return FFRGB(r);"""

SPARK_SHADE = "return FFSparkShade(UV0, float4(VC, VCA), EmissiveScale);"
BOLT_SHADE = "return FFBoltShade(UV0, float4(VC, VCA), Age, Intensity, Color, Duration);"

BEAM_WPO = "return FFBeamOffset(Pos, UV0, Taper, Scroll);"
BEAM_SHADE = """float len = max(Height, 0.1);
float4 n1 = Noise.Sample(NoiseSampler, float2(UV0.y * len * 0.8 - Scroll * 2.0, UV0.x * 0.5 + 0.5));
float4 n2 = Noise.Sample(NoiseSampler, float2(UV0.y * len * 4.0 - Scroll * 6.0, UV0.x * 2.0));
float4 r = FFBeamShade(UV0, n1, n2, Age, Intensity, Cover, Grain, Color, Color2);
OutOpacity = r.w;
return FFRGB(r);"""

RING_SHADE = """float2 p = UV0 * 2.0 - 1.0;
float4 nA = Noise.Sample(NoiseSampler, p * 0.9 + float2(Seed, Phase * 0.1));
float4 nB = Noise.Sample(NoiseSampler, p * 0.6 + float2(Seed * 3.0 + 0.3, Phase * 0.5));
float4 r = FFRingShade(UV0, nA, nB, Style, Radius, Width, Phase, Opacity, Cover, Glow, Color);
OutOpacity = r.w;
return FFRGB(r);"""

SHELL_SHADE = """float2 nuv = float2(UV0.x * 3.0 + Seed, UV0.y * 2.0 + Phase * 0.13);
float4 n1 = Noise.Sample(NoiseSampler, nuv);
float4 n2 = Noise.Sample(NoiseSampler, nuv * 2.0 + float2(Phase * 0.21, 0.0));
float4 nc = Noise.Sample(NoiseSampler, float2(UV0.x * 5.0 + floor(Phase * 12.0) * 0.37, UV0.y * 4.0));
float4 r = FFShellShade(saturate(dot(N, Cam)), n1, n2, nc, Color, Color2, Opacity, Rim, Streak, Crackle, Core, Pulse,
	Phase, Glow, Absorb, Cover);
OutOpacity = r.w;
return FFRGB(r);"""

VORTEX_WPO = "return FFVortexOffset(Pos, UV0, Phase, Scroll, Height, Seed);"
VORTEX_SHADE = """float h = UV0.y;
float4 n1 = Noise.Sample(NoiseSampler, float2(UV0.x * 3.0 - Phase * (1.0 + 0.6 * (1.0 - h)), h * 1.3 - Scroll * 0.5));
float4 n2 = Noise.Sample(NoiseSampler, float2(UV0.x * 6.0 - Phase * 1.7 + 0.31, h * 2.4 - Scroll * 0.9));
float4 r = FFVortexShade(UV0, n1, n2, abs(dot(N, Cam)), Scroll, Opacity, Cover, Glow, Color, Color2);
OutOpacity = r.w;
return FFRGB(r);"""

WIND_WPO = "return FFWindOffset(Pos, Style, Age);"
WIND_SHADE = """float2 c1;
float2 c2;
if (Style < 0.5)
{
	c1 = float2(UV0.x * 1.5 - Scroll, UV0.y * 0.6);
	c2 = float2(UV0.x * 0.6 - Scroll * 1.6, UV0.y * 9.0);
}
else if (Style < 1.5)
{
	c1 = float2(UV0.x * 2.0, UV0.y - Age * 2.5);
	c2 = c1 + float2(0.31, 0.77);
}
else
{
	c1 = float2(UV0.y * 0.8 - Flow, UV0.x * 0.5);
	c2 = c1 * 2.0;
}
float4 n1 = Noise.Sample(NoiseSampler, c1);
float4 n2 = Noise.Sample(NoiseSampler, c2);
float4 r = FFWindShade(UV0, UV1, float4(VC, VCA), n1, n2, abs(dot(N, Cam)), Style, Age, Opacity, Cover, Dusty, Flow,
	Color);
OutOpacity = r.w;
return FFRGB(r);"""

GROUND_SURF = """float2 nuv = UV0 * 1.4 + float2(Seed * 3.7, Seed * 5.9);
float4 n = Noise.Sample(NoiseSampler, nuv);
float4 n2 = Noise.Sample(NoiseSampler, nuv * 2.6 + 0.31);
float4 nc = Noise.Sample(NoiseSampler, UV0 * 2.0 + float2(floor(Phase * 10.0) * 0.23, 0.0));
FFSurf s = FFGroundDecalSurface(UV0, n, n2, nc, Style, Phase, Heat, Seed, Fade);
OutEmissive = s.emissive;
OutNormal = s.normal;
OutRough = s.rough;
OutSpec = s.spec;
OutOpacity = s.opacity;
return s.albedo;"""

_UNLIT_OUT = {"EmissiveColor": ("shade", ""), "Opacity": ("shade", "OutOpacity")}
_LIT_OUT = {"BaseColor": ("surf", ""), "EmissiveColor": ("surf", "OutEmissive"), "Normal": ("surf", "OutNormal"),
            "Roughness": ("surf", "OutRough"), "Specular": ("surf", "OutSpec")}
_F1, _F3 = "float", "float3"

MATERIALS = {
    "rock": {
        "asset": "M_FX_Rock", "blend": "opaque", "lit": True, "two_sided": False, "tangent_normal": False,
        "full_precision": True,
        "includes": ["/Fourfold/FX/FFRock.ush"],
        "scalars": {"Heat": 0.0, "Melt": 0.0, "Crust": 0.0, "Damage": 0.0, "Frost": 0.0, "Seed": 0.0, "Detail": 1.0,
                    "Glass": 0.0, "Rise": 1.0, "RiseHeight": 1.0, "Fade": 1.0, "GlowScale": 1.0},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0)},
        "textures": {"Noise": "T_FX_Noise"},
        "normal_maps": {"RockNormal": ENGINE_DEFAULT_NORMAL},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": ROCK_WPO,
                    "inputs": _merge({"Pos": "lpos", "UV1": "uv1", "UV2": "uv2", "VC": "vc", "Time": "time"},
                                     _p("Melt", "Crust", "Seed", "Rise", "RiseHeight", "Fade")), "outputs": {}},
            "surf": {"stage": "ps", "type": _F3, "code": ROCK_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "Pos": "lpos_ps", "UV1": "uv1", "UV2": "uv2",
                                       "N": "nrm_ws", "Baked": "nmap:RockNormal", "AX": "ax", "AY": "ay", "AZ": "az",
                                       "Time": "time"},
                                      _p("Heat", "Melt", "Crust", "Damage", "Frost", "Seed", "Detail", "Glass",
                                         "GlowScale"), _v("Tint")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutSpec": _F1}},
        },
        "outputs": _merge(_LIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "lava_strip": {
        "asset": "M_FX_LavaStrip", "blend": "opaque", "lit": True, "two_sided": False, "tangent_normal": True,
        "full_precision": True,
        "includes": ["/Fourfold/FX/FFRock.ush"],
        "scalars": {"Melt": 1.0, "Crust": 0.0, "Flow": 0.0, "Boil": 0.0, "Seed": 0.0, "Fade": 1.0, "GlowScale": 1.0},
        "vectors": {}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": LAVA_WPO,
                    "inputs": _merge({"UV0": "uv0", "UV2": "uv2", "NL": "nrm_local"}, _p("Melt", "Crust", "Boil")),
                    "outputs": {}},
            "surf": {"stage": "ps", "type": _F3, "code": LAVA_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV2": "uv2", "N": "nrm_ws"},
                                      _p("Melt", "Crust", "Flow", "Boil", "Seed", "GlowScale")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutSpec": _F1}},
        },
        "outputs": _merge(_LIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "metal": {
        "asset": "M_FX_Metal", "blend": "opaque", "lit": True, "two_sided": False, "tangent_normal": True,
        "includes": ["/Fourfold/FX/FFMetal.ush"],
        "scalars": {"Heat": 0.0, "Seed": 0.0, "Spin": 0.0, "Fade": 1.0}, "vectors": {},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": METAL_WPO, "inputs": {"Pos": "lpos", "Fade": "p:Fade"},
                    "outputs": {}},
            "surf": {"stage": "ps", "type": _F3, "code": METAL_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "N": "nrm_ws", "Cam": "cam"},
                                      _p("Heat", "Spin", "Seed")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutMetal": _F1}},
        },
        "outputs": {"BaseColor": ("surf", ""), "EmissiveColor": ("surf", "OutEmissive"), "Normal": ("surf", "OutNormal"),
                    "Roughness": ("surf", "OutRough"), "Metallic": ("surf", "OutMetal"),
                    "WorldPositionOffset": ("wpo", "")},
        "constants": {"Specular": 0.6},
    },
    "crystal": {
        "asset": "M_FX_Crystal", "blend": "alpha_composite", "lit": False, "two_sided": False, "full_precision": True,
        "includes": ["/Fourfold/FX/FFCrystal.ush"],
        "scalars": {"Heat": 0.0, "Frost": 0.4, "Opacity": 0.55, "Glow": 0.6, "Rise": 1.0, "RiseHeight": 1.0,
                    "Shatter": 0.0, "Fade": 1.0, "Seed": 0.0},
        "vectors": {"Tint": (0.70, 0.90, 0.98, 1.0)}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": CRYSTAL_WPO,
                    "inputs": _merge({"UV1": "uv1"}, _p("Rise", "RiseHeight")), "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": CRYSTAL_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV1": "uv1", "N": "nrm_ws", "Cam": "cam"},
                                       _p("Frost", "Heat", "Opacity", "Glow", "Shatter", "Fade"), _v("Tint")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "water": {
        "asset": "M_FX_Water", "blend": "alpha_composite", "lit": False, "two_sided": False, "full_precision": True,
        "includes": ["/Fourfold/FX/FFWater.ush"],
        "scalars": {"Shape": 0.0, "Flow": 0.0, "Frozen": 0.0, "Detail": 1.0, "Seed": 0.0, "Fade": 1.0, "Melt": 1.0,
                    "Crust": 0.0, "Boil": 0.0},
        "vectors": {}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": WATER_WPO,
                    "inputs": _merge({"Pos": "lpos", "NL": "nrm_local", "UV0": "uv0", "UV2": "uv2", "Time": "time"},
                                     _p("Shape", "Flow", "Frozen", "Detail")), "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": WATER_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV2": "uv2", "Pos": "lpos_ps",
                                        "N": "nrm_ws", "WP": "wpos_cr", "Cam": "cam", "Time": "time"},
                                       _p("Shape", "Flow", "Frozen", "Seed", "Fade")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "ground_strip": {
        "asset": "M_FX_GroundStrip", "blend": "opaque", "lit": True, "two_sided": False, "tangent_normal": True,
        "full_precision": True,
        "includes": ["/Fourfold/FX/FFGround.ush"],
        "scalars": {"Style": 0.0, "Flow": 0.0, "Crust": 0.0, "Seed": 0.0, "Melt": 1.0, "Boil": 0.0, "Fade": 1.0},
        "vectors": {}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "surf": {"stage": "ps", "type": _F3, "code": GSTRIP_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV2": "uv2"},
                                      _p("Style", "Flow", "Crust", "Seed")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutSpec": _F1}},
        },
        "outputs": dict(_LIT_OUT),
    },
    "vine": {
        "asset": "M_FX_Vine", "blend": "opaque", "lit": True, "two_sided": False, "tangent_normal": True,
        "includes": ["/Fourfold/FX/FFVine.ush"],
        "scalars": {"Burn": 0.0, "Frozen": 0.0, "Phase": 0.0, "Fade": 1.0, "Seed": 0.0}, "vectors": {},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": VINE_WPO,
                    "inputs": _merge({"UV1": "uv1"}, _p("Phase", "Frozen")), "outputs": {}},
            "surf": {"stage": "ps", "type": _F3, "code": VINE_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV1": "uv1", "N": "nrm_ws", "Cam": "cam"},
                                      _p("Burn", "Frozen")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutSpec": _F1}},
        },
        "outputs": _merge(_LIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "flame": {
        "asset": "M_FX_Flame", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFFlame.ush"],
        "scalars": {"Style": 0.0, "Shape": 0.0, "Age": 0.0, "Scroll": 0.0, "Core": 0.0, "Seed": 0.0, "Intensity": 1.0,
                    "Cover": 0.8, "Height": 1.0, "Width": 0.2, "EmissiveScale": 1.2},
        "vectors": {"Color": (0.62, 0.07, 0.01, 1.0), "Color2": (1.0, 0.36, 0.05, 1.0), "Color3": (1.0, 0.78, 0.30, 1.0),
                    "Color4": (1.0, 0.95, 0.75, 1.0)},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": FLAME_WPO,
                    "inputs": _merge({"Pos": "lpos", "UV0": "uv0", "UV2": "uv2", "VC": "vc", "VCA": "vca"},
                                     _p("Style", "Shape", "Age", "Scroll", "Core", "Seed")), "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": FLAME_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "VC": "vc", "VCA": "vca", "N": "nrm_ws",
                                        "Cam": "cam"},
                                       _p("Style", "Age", "Scroll", "Intensity", "Core", "Cover", "Seed",
                                          "EmissiveScale"), _v("Color", "Color2", "Color3", "Color4")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "fire_sprite": {
        "asset": "M_FX_FireSprite", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFFlame.ush"],
        "scalars": {"Intensity": 1.0, "Age": 0.0, "EmissiveScale": 1.0},
        "vectors": {"Tint": (1.0, 1.0, 1.0, 1.0), "Color": (0.62, 0.07, 0.01, 1.0), "Color2": (1.0, 0.36, 0.05, 1.0),
                    "Color3": (1.0, 0.78, 0.30, 1.0), "Color4": (1.0, 0.95, 0.75, 1.0)},
        "textures": {"Flipbook": "T_FX_FB_explosion"},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": FIRE_SPRITE_SHADE,
                      "inputs": _merge({"Flipbook": "tex:Flipbook", "UV0": "uv0", "UV1": "uv1", "VC": "vc", "VCA": "vca"},
                                       _p("Intensity", "EmissiveScale"),
                                       _v("Tint", "Color", "Color2", "Color3", "Color4")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": dict(_UNLIT_OUT),
    },
    "smoke": {
        "asset": "M_FX_Smoke", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFSmoke.ush", "/Fourfold/Common/FFFlipbook.ush"],
        "scalars": {"Style": 0.0, "Opacity": 1.0, "Erosion": 0.5, "Phase": 0.0},
        "vectors": {"Color": (1.0, 1.0, 1.0, 1.0)},
        "textures": {"Flipbook": "T_FX_FB_smoke_puff", "Noise": "T_FX_Noise"},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": SMOKE_SHADE,
                      "inputs": _merge({"Flipbook": "tex:Flipbook", "Noise": "tex:Noise", "UV0": "uv0", "UV1": "uv1",
                                        "UV2": "uv2", "VC": "vc", "VCA": "vca", "Cam": "cam"},
                                       _p("Style", "Opacity", "Erosion", "Phase"), _v("Color")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": dict(_UNLIT_OUT),
    },
    "splash": {
        "asset": "M_FX_Splash", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFSmoke.ush", "/Fourfold/Common/FFFlipbook.ush"],
        "scalars": {}, "vectors": {"Color": (0.78, 0.9, 1.0, 1.0)},
        "textures": {"Flipbook": "T_FX_FB_water_splash"},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": SPLASH_SHADE,
                      "inputs": _merge({"Flipbook": "tex:Flipbook", "UV0": "uv0", "UV1": "uv1", "VC": "vc",
                                        "VCA": "vca", "Cam": "cam"}, _v("Color")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": dict(_UNLIT_OUT),
    },
    "spark": {
        "asset": "M_FX_Spark", "blend": "additive", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFLightning.ush"],
        "scalars": {"EmissiveScale": 2.5}, "vectors": {}, "textures": {},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": SPARK_SHADE,
                      "inputs": _merge({"UV0": "uv0", "VC": "vc", "VCA": "vca"}, _p("EmissiveScale")), "outputs": {}},
        },
        "outputs": {"EmissiveColor": ("shade", "")},
    },
    "lightning": {
        "asset": "M_FX_Lightning", "blend": "additive", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFLightning.ush"],
        "scalars": {"Age": 0.0, "Intensity": 1.0, "Duration": 0.2}, "vectors": {"Color": (0.42, 0.52, 1.0, 1.0)},
        "textures": {},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": BOLT_SHADE,
                      "inputs": _merge({"UV0": "uv0", "VC": "vc", "VCA": "vca"}, _p("Age", "Intensity", "Duration"),
                                       _v("Color")), "outputs": {}},
        },
        "outputs": {"EmissiveColor": ("shade", "")},
    },
    "beam": {
        "asset": "M_FX_Beam", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFLightning.ush"],
        "scalars": {"Width": 0.12, "Cover": 0.15, "Taper": 0.6, "Grain": 0.0, "Age": 0.0, "Scroll": 0.0, "Height": 1.0,
                    "Intensity": 1.0},
        "vectors": {"Color": (0.9, 0.97, 1.0, 1.0), "Color2": (0.35, 0.55, 1.0, 1.0)},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": BEAM_WPO,
                    "inputs": _merge({"Pos": "lpos", "UV0": "uv0"}, _p("Taper", "Scroll")), "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": BEAM_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0"},
                                       _p("Height", "Scroll", "Age", "Intensity", "Cover", "Grain"),
                                       _v("Color", "Color2")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "ring": {
        "asset": "M_FX_Ring", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFShell.ush"],
        "scalars": {"Style": 0.0, "Radius": 0.8, "Width": 0.06, "Phase": 0.0, "Opacity": 1.0, "Cover": 0.3,
                    "Glow": 1.0, "Seed": 0.0},
        "vectors": {"Color": (1.0, 1.0, 1.0, 1.0)}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": RING_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0"},
                                       _p("Style", "Radius", "Width", "Phase", "Opacity", "Cover", "Glow", "Seed"),
                                       _v("Color")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": dict(_UNLIT_OUT),
    },
    "shell": {
        "asset": "M_FX_Shell", "blend": "alpha_composite", "lit": False, "two_sided": False,
        "includes": ["/Fourfold/FX/FFShell.ush"],
        "scalars": {"Opacity": 0.6, "Rim": 2.5, "Streak": 0.0, "Crackle": 0.0, "Core": 0.0, "Pulse": 0.0, "Phase": 0.0,
                    "Glow": 1.0, "Absorb": 0.0, "Cover": 0.5, "Seed": 0.0, "Height": 1.0},
        "vectors": {"Color": (0.6, 0.5, 0.9, 1.0), "Color2": (0.1, 0.05, 0.2, 1.0)},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "shade": {"stage": "ps", "type": _F3, "code": SHELL_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "N": "nrm_ws", "Cam": "cam"},
                                       _p("Opacity", "Rim", "Streak", "Crackle", "Core", "Pulse", "Phase", "Glow",
                                          "Absorb", "Cover", "Seed"), _v("Color", "Color2")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": dict(_UNLIT_OUT),
    },
    "vortex": {
        "asset": "M_FX_Vortex", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFWind.ush"],
        "scalars": {"Opacity": 0.5, "Cover": 0.55, "Glow": 1.0, "Seed": 0.0, "Phase": 0.0, "Scroll": 0.0, "Height": 3.0,
                    "Radius": 1.6},
        "vectors": {"Color": (0.78, 0.74, 0.66, 1.0), "Color2": (0.95, 0.96, 1.0, 1.0)},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": VORTEX_WPO,
                    "inputs": _merge({"Pos": "lpos", "UV0": "uv0"}, _p("Phase", "Scroll", "Height", "Seed")),
                    "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": VORTEX_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "N": "nrm_ws", "Cam": "cam"},
                                       _p("Phase", "Scroll", "Opacity", "Cover", "Glow"), _v("Color", "Color2")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "wind": {
        "asset": "M_FX_Wind", "blend": "alpha_composite", "lit": False, "two_sided": True,
        "includes": ["/Fourfold/FX/FFWind.ush"],
        "scalars": {"Style": 0.0, "Age": 0.5, "Opacity": 0.6, "Cover": 0.35, "Dusty": 0.3, "Scroll": 0.0, "Flow": 0.0,
                    "Seed": 0.0},
        "vectors": {"Color": (0.70, 0.64, 0.55, 1.0)}, "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "wpo": {"stage": "vs", "type": _F3, "code": WIND_WPO,
                    "inputs": _merge({"Pos": "lpos"}, _p("Style", "Age")), "outputs": {}},
            "shade": {"stage": "ps", "type": _F3, "code": WIND_SHADE,
                      "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0", "UV1": "uv1", "VC": "vc", "VCA": "vca",
                                        "N": "nrm_ws", "Cam": "cam"},
                                       _p("Style", "Age", "Opacity", "Cover", "Dusty", "Scroll", "Flow"), _v("Color")),
                      "outputs": {"OutOpacity": _F1}},
        },
        "outputs": _merge(_UNLIT_OUT, {"WorldPositionOffset": ("wpo", "")}),
    },
    "ground": {
        "asset": "M_FX_Ground", "blend": "translucent", "lit": True, "two_sided": False, "tangent_normal": True,
        "full_precision": True,
        "includes": ["/Fourfold/FX/FFGround.ush"],
        "scalars": {"Style": 0.0, "Fade": 1.0, "Phase": 0.0, "Heat": 0.0, "Seed": 0.0}, "vectors": {},
        "textures": {"Noise": "T_FX_Noise"},
        "nodes": {
            "surf": {"stage": "ps", "type": _F3, "code": GROUND_SURF,
                     "inputs": _merge({"Noise": "tex:Noise", "UV0": "uv0"}, _p("Style", "Phase", "Heat", "Seed", "Fade")),
                     "outputs": {"OutEmissive": _F3, "OutNormal": _F3, "OutRough": _F1, "OutSpec": _F1,
                                 "OutOpacity": _F1}},
        },
        "outputs": _merge(_LIT_OUT, {"Opacity": ("surf", "OutOpacity")}),
    },
}

# Engine-side type of every graph source (used by the shader checker to declare the wrapper parameters).
SOURCE_TYPES = {
    "uv0": "float2", "uv1": "float2", "uv2": "float2", "vc": "float3", "vca": "float", "time": "float",
    "lpos": "float3", "lpos_ps": "float3", "nrm_local": "float3", "nrm_ws": "float3", "cam": "float3",
    "wpos_cr": "float3", "ax": "float3", "ay": "float3", "az": "float3",
}


def source_type(src):
    if src in SOURCE_TYPES:
        return SOURCE_TYPES[src]
    kind = src.split(":", 1)[0]
    return {"p": "float", "v": "float3", "tex": "Texture2D", "nmap": "float3"}.get(kind, "float3")


def driven_params():
    """Every scalar / vector parameter name declared by the masters (for the cross-check with FxTypes.h)."""
    names = set()
    for m in MATERIALS.values():
        names.update(m.get("scalars", {}).keys())
        names.update(m.get("vectors", {}).keys())
    return names


# Parameters that only tune a master's look in the editor (the C++ logic never sets them).
TUNING_ONLY = {"GlowScale", "Duration"}
