"""Fourfold open world - editor setup (setup part "openworld"; owner: open-world stream).

Builds what the runtime AFourfoldOpenWorld actor needs (the terrain itself is generated at runtime from
Content/Fourfold/Data/openworld, written by unreal/Tools/openworld/gen_world.py):
- /Game/Fourfold/OpenWorld/Textures/T_OW_<Layer>_{BC,N,ORM}  (Poly Haven CC0, unreal/Tools/openworld/fetch_textures.py)
- /Game/Fourfold/OpenWorld/Materials/M_OW_Terrain + MI_OW_Terrain: 4 splat layers from the vertex colour
  (R grass, G rock, B sand, A ash) + snow by height and slope, world-aligned, macro variation
- /Game/Fourfold/Maps/L_World: sun / sky / clouds / fog / post (the Lab's lighting rig, re-tuned for 2 km) and one
  FourfoldOpenWorld actor. The player controller starts roaming on that map.

  import fourfold_setup; fourfold_setup.main(only="openworld")      # or force=True to rebuild
"""
import json
import os

import unreal

from fourfold.world import common as C
from fourfold.world import textures as WT
from fourfold.world.materials import Graph, RGB, get_or_create_material

ROOT = "/Game/Fourfold/OpenWorld"
TEX_DIR = f"{ROOT}/Textures"
MAT_DIR = f"{ROOT}/Materials"
MAP_PATH = "/Game/Fourfold/Maps/L_World"
LAYERS = ["Grass", "Rock", "Sand", "Ash", "Snow"]
ST = unreal.MaterialSamplerType
MP = unreal.MaterialProperty


def _report():
    return {"created": [], "skipped": [], "failed": [], "notes": []}


def _src_dir():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.join(proj, "SourceArt", "OpenWorld", "PolyHaven")


# ------------------------------------------------------------------------------------------------ textures
def import_textures(force, rep):
    src = _src_dir()
    out = {}
    if not os.path.isdir(src):
        rep["failed"].append({"item": "openworld textures", "error": f"missing {src} (run unreal/Tools/openworld/fetch_textures.py)"})
        return out, {}
    manifest = {}
    try:
        manifest = json.load(open(os.path.join(src, "polyhaven.json"))).get("layers", {})
    except Exception as e:  # noqa: BLE001
        rep["notes"].append(f"polyhaven.json unreadable: {e}")
    C.make_dirs(ROOT, TEX_DIR)
    tasks = []
    for fn in sorted(os.listdir(src)):
        if not (fn.startswith("T_OW_") and fn.endswith(".png")):
            continue
        name = fn[:-4]
        dst = f"{TEX_DIR}/{name}"
        out[name] = dst
        if C.asset_exists(dst) and not force:
            rep["skipped"].append(dst)
            continue
        t = unreal.AssetImportTask()
        for k, v in (("filename", os.path.join(src, fn)), ("destination_path", TEX_DIR), ("destination_name", name),
                     ("replace_existing", True), ("automated", True), ("save", False)):
            t.set_editor_property(k, v)
        tasks.append((t, name, dst))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for t, _, _ in tasks])
    for _t, name, dst in tasks:
        tex = unreal.load_asset(dst)
        if tex is None:
            rep["failed"].append({"item": dst, "error": "import produced nothing"})
            continue
        WT.apply_settings(tex, name, rep)
        C.save_asset(tex)
        rep["created"].append(dst)
    return out, manifest


# ------------------------------------------------------------------------------------------------ material
def _node(graph, cls, x, y, **props):
    return graph.expr(getattr(unreal, cls), x, y, **props)


def _add(g, a, b, x, y):
    n = _node(g, "MaterialExpressionAdd", x, y)
    g.link(a[0], a[1], n, "A")
    g.link(b[0], b[1], n, "B")
    return (n, "")


def _mul(g, a, b, x, y):
    n = _node(g, "MaterialExpressionMultiply", x, y)
    g.link(a[0], a[1], n, "A")
    g.link(b[0], b[1], n, "B")
    return (n, "")


def _lerp(g, a, b, alpha, x, y):
    n = _node(g, "MaterialExpressionLinearInterpolate", x, y)
    g.link(a[0], a[1], n, "A")
    g.link(b[0], b[1], n, "B")
    g.link(alpha[0], alpha[1], n, "Alpha")
    return (n, "")


def build_terrain_material(force, texs, manifest, rep):
    C.make_dirs(ROOT, MAT_DIR)
    mat, fresh = get_or_create_material(MAT_DIR, "M_OW_Terrain", force)
    if not fresh:
        rep["skipped"].append(f"{MAT_DIR}/M_OW_Terrain")
        return mat
    notes = rep["notes"]
    g = Graph(mat, notes)
    X = -3200
    wp = _node(g, "MaterialExpressionWorldPosition", X, 0)
    xy = _node(g, "MaterialExpressionComponentMask", X + 200, 0, r=True, g=True, b=False, a=False)
    g.link(wp, "", xy, "")
    wz = _node(g, "MaterialExpressionComponentMask", X + 200, 200, r=False, g=False, b=True, a=False)
    g.link(wp, "", wz, "")
    samples = {}
    y = -1600
    for layer in LAYERS:
        size_m = float(manifest.get(layer, {}).get("size_m", 3.0))
        if layer == "Grass":
            size_m = min(size_m, 9.0)        # the aerial scan reads too coarse at its true 15 m under a fighter
        tile = g.scalar(f"Tile_{layer}", size_m * 100.0, X + 200, y, "Tiling")
        uv = _node(g, "MaterialExpressionDivide", X + 450, y)
        g.link(xy, "", uv, "A")
        g.link(tile, "", uv, "B")
        bc = g.texture(f"{layer}_BC", unreal.load_asset(texs.get(f"T_OW_{layer}_BC", "")) if texs.get(f"T_OW_{layer}_BC") else None,
                       ST.SAMPLERTYPE_COLOR, X + 750, y, uv)
        nm = g.texture(f"{layer}_N", unreal.load_asset(texs.get(f"T_OW_{layer}_N", "")) if texs.get(f"T_OW_{layer}_N") else None,
                       ST.SAMPLERTYPE_NORMAL, X + 750, y + 220, uv)
        orm = g.texture(f"{layer}_ORM", unreal.load_asset(texs.get(f"T_OW_{layer}_ORM", "")) if texs.get(f"T_OW_{layer}_ORM") else None,
                        ST.SAMPLERTYPE_MASKS, X + 750, y + 440, uv)
        if layer == "Grass":
            gt = g.vector("GrassTint", (0.56, 0.74, 0.45), X + 1000, y - 120, "Grade")
            bc = _mul(g, (bc, "RGB"), (gt, ""), X + 1100, y)[0]
        if layer == "Rock":
            # far rock: the same scan at 14x the tile, faded in with distance so cliffs keep strata at 1 km
            big = g.scalar("Tile_RockFar", size_m * 100.0 * 14.0, X + 200, y + 600, "Tiling")
            uvb = _node(g, "MaterialExpressionDivide", X + 450, y + 600)
            g.link(xy, "", uvb, "A")
            g.link(big, "", uvb, "B")
            far = g.texture("Rock_BC_Far", unreal.load_asset(texs["T_OW_Rock_BC"]) if texs.get("T_OW_Rock_BC") else None,
                            ST.SAMPLERTYPE_COLOR, X + 750, y + 600, uvb)
            depth = _node(g, "MaterialExpressionPixelDepth", X + 750, y + 820)
            fdist = g.scalar("RockFarStartCm", 6000.0, X + 750, y + 900, "Tiling")
            fd = _node(g, "MaterialExpressionDivide", X + 950, y + 820)
            g.link(depth, "", fd, "A")
            g.link(fdist, "", fd, "B")
            fs = _node(g, "MaterialExpressionSaturate", X + 1100, y + 820)
            g.link(fd, "", fs, "")
            bc = _lerp(g, (bc, "RGB"), (far, "RGB"), (fs, ""), X + 1250, y + 600)[0]
        samples[layer] = (bc, nm, orm)
        y += 700
    vc = _node(g, "MaterialExpressionVertexColor", X + 1100, 1800)
    weights = {"Grass": (vc, "R"), "Rock": (vc, "G"), "Sand": (vc, "B"), "Ash": (vc, "A")}
    # snow: above SnowStart (cm), faded over SnowFade, only where the slope is gentle
    s_start = g.scalar("SnowStartCm", 17000.0, X + 1100, 2100, "Snow")
    s_fade = g.scalar("SnowFadeCm", 5000.0, X + 1100, 2220, "Snow")
    sub = _node(g, "MaterialExpressionSubtract", X + 1350, 2100)
    g.link(wz, "", sub, "A")
    g.link(s_start, "", sub, "B")
    div = _node(g, "MaterialExpressionDivide", X + 1550, 2100)
    g.link(sub, "", div, "A")
    g.link(s_fade, "", div, "B")
    sat = _node(g, "MaterialExpressionSaturate", X + 1750, 2100)
    g.link(div, "", sat, "")
    nws = _node(g, "MaterialExpressionVertexNormalWS", X + 1350, 2350)
    nz = _node(g, "MaterialExpressionComponentMask", X + 1550, 2350, r=False, g=False, b=True, a=False)
    g.link(nws, "", nz, "")
    flat = _node(g, "MaterialExpressionSubtract", X + 1750, 2350)
    g.link(nz, "", flat, "A")
    g.link(g.const(0.42, X + 1550, 2500), "", flat, "B")
    flat4 = _mul(g, (flat, ""), (g.const(4.0, X + 1750, 2500), ""), X + 1950, 2350)
    flat_s = _node(g, "MaterialExpressionSaturate", X + 2150, 2350)
    g.link(flat4[0], "", flat_s, "")
    snow_w = _mul(g, (sat, ""), (flat_s, ""), X + 2350, 2200)

    graded = {"Grass": "", "Rock": ""}       # base colours replaced by math nodes (single unnamed output)

    def src(layer, idx, out_name):
        if idx == 0 and layer in graded:
            return (samples[layer][0], "")
        return (samples[layer][idx], out_name)

    def blend(idx, out_name, x):
        acc = None
        yy = -1200
        for layer in ("Grass", "Rock", "Sand", "Ash"):
            term = _mul(g, src(layer, idx, out_name), weights[layer], x, yy)
            acc = term if acc is None else _add(g, acc, term, x + 220, yy)
            yy += 160
        return _lerp(g, acc, src("Snow", idx, out_name), snow_w, x + 450, yy)

    base = blend(0, "RGB", X + 2600)
    nrm = blend(1, "RGB", X + 3300)
    orm_r = blend(2, "R", X + 4000)
    orm_g = blend(2, "G", X + 4700)
    # macro variation (large scale brightness / hue drift) from the shared noise texture
    noise_tex = unreal.load_asset("/Game/Fourfold/Env/Textures/T_Env_Noise")
    mtile = g.scalar("MacroTileCm", 9000.0, X + 2600, 1500, "Macro")
    muv = _node(g, "MaterialExpressionDivide", X + 2850, 1500)
    g.link(xy, "", muv, "A")
    g.link(mtile, "", muv, "B")
    noise = g.texture("MacroNoise", noise_tex, ST.SAMPLERTYPE_MASKS, X + 3100, 1500, muv)
    mlo = g.scalar("MacroDark", 0.78, X + 3100, 1750, "Macro")
    mhi = g.scalar("MacroBright", 1.12, X + 3100, 1870, "Macro")
    macro = _lerp(g, (mlo, ""), (mhi, ""), (noise, "R"), X + 3400, 1600)
    tint = g.vector("Tint", (1.0, 1.0, 1.0), X + 3400, 1800, "Grade")
    bright = g.scalar("Brightness", 1.0, X + 3400, 1950, "Grade")
    col = _mul(g, base, macro, X + 5200, -600)
    col = _mul(g, col, (tint, ""), X + 5400, -600)
    col = _mul(g, col, (bright, ""), X + 5600, -600)
    rough_s = g.scalar("RoughnessScale", 1.0, X + 5200, 300, "Grade")
    rough = _mul(g, orm_g, (rough_s, ""), X + 5400, 300)
    g.out(col[0], "", MP.MP_BASE_COLOR)
    g.out(nrm[0], "", MP.MP_NORMAL)
    g.out(rough[0], "", MP.MP_ROUGHNESS)
    g.out(orm_r[0], "", MP.MP_AMBIENT_OCCLUSION)
    try:
        unreal.MaterialEditingLibrary.recompile_material(mat)
    except Exception as e:  # noqa: BLE001
        notes.append(f"M_OW_Terrain recompile: {e}")
    C.save_asset(mat)
    rep["created"].append(f"{MAT_DIR}/M_OW_Terrain")
    return mat


def build_instance(parent, force, rep):
    path = f"{MAT_DIR}/MI_OW_Terrain"
    if C.asset_exists(path) and not force:
        rep["skipped"].append(path)
        return unreal.load_asset(path)
    if C.asset_exists(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_OW_Terrain", MAT_DIR, unreal.MaterialInstanceConstant,
                                                                     unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    C.save_asset(mi)
    rep["created"].append(path)
    return mi


# ------------------------------------------------------------------------------------------------ level
def build_level(force, terrain_mi, rep):
    from fourfold.world import level as WL
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
    # re-tune the Lab rig for a 2 km valley: longer shadows, thinner fog that starts further out
    sun = b.lights.get("sun")
    if sun is not None:
        for k, v in (("dynamic_shadow_distance_movable_light", 20000.0), ("dynamic_shadow_cascades", 4),
                     ("far_shadow_cascade_count", 2), ("far_shadow_distance", 120000.0), ("intensity", 12.0)):
            C.set_prop(sun, k, v, rep, quiet=True)
    fog = b.actors.get("FF_Fog")
    if fog is not None:
        fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        for k, v in (("fog_density", 0.0035), ("fog_height_falloff", 0.06), ("start_distance", 6000.0),
                     ("fog_max_opacity", 0.9), ("volumetric_fog_distance", 8000.0)):
            C.set_prop(fc, k, v, rep, quiet=True)
        fog.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
    ow = b.spawn(unreal.FourfoldOpenWorld, (0, 0, 0), label="FF_OpenWorld", tags=("FourfoldArena",), folder="Fourfold/OpenWorld")
    if terrain_mi is not None:
        C.set_prop(ow, "terrain_material", terrain_mi, rep)
    try:
        b.les.save_current_level()
        rep["created"].append(MAP_PATH)
    except Exception as e:  # noqa: BLE001
        rep["failed"].append({"item": MAP_PATH, "error": f"save: {e}"})


def build_all(force=False):
    rep = _report()
    texs, manifest = import_textures(force, rep)
    mat = build_terrain_material(force, texs, manifest, rep)
    mi = build_instance(mat, force, rep)
    build_level(force, mi, rep)
    return rep
