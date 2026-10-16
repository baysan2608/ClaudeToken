"""Intel "Jungle Ruins" (CC BY 4.0) -> the L_Ruins map sources for Unreal (Blender 5.0, background).

  /Applications/Blender.app/Contents/MacOS/Blender -b --disable-autoexec --factory-startup \
      --python unreal/Tools/blender/ruins/prep_ruins.py [-- --only terrain,stone,water,veg]

Input (git-ignored, unreal/Tools/ruins/fetch_ruins.py): SourceArt/OpenWorld/JungleRuins/extract/JungleRuins/{USD,textures}.
USD layers are read by Blender's importer (meshes, material names) and by pxr (instancer arrays, material texture files);
nothing in them executes. Output (git-ignored): SourceArt/OpenWorld/JungleRuins/Game/ - SM_Ruins_<Name>_LOD<n>.fbx in map
space (Blender metres minus ruins_spec.CENTER_BL, so every map mesh imports at the Unreal origin), T_Ruins_<Set>_{BC,N,ORM,OP}.png,
layout.json (meshes / slots / actors / props for the setup part "ruins" and the bake). Preview (tracked):
unreal/docs/ruins/previews/layout.png.
  terrain  Terrain_Cinematic (1 km, 1.24 m triangles): the 288 m core split into 3 x 3 chunks (faces by centroid, so
           neighbours share their seam vertices), LOD1 / LOD2 collapse-decimated with every open edge locked; the rest of
           the tile becomes one decimated backdrop with the core cut out. Textures: the core crop of the 8k cinematic maps
           (12 cm pixels) at 2048, the whole tile at 2048 for the backdrop.
  stone    Pyramid.usd (159 k tris, 19 materials): LOD0 as authored, LOD1 / LOD2 decimated; its 2048 maps repacked
           (normal green flipped to DirectX, ORM = 1 / roughness / 0). The source stops at 2048: no 4k hero set exists.
  water    River.usd clipped to the tile (flat at -9.99 m in the core) and Creek.usd (sloped, 470 m south: scenery).
  veg      the scene's point instancers: instances inside the core (trees also in a 120 m ring) kept 1 in
           ruins_spec.THIN[kind] by a stable hash; prototypes collapse-decimated to ruins_spec.LOD_TRIS; textures 1024
           (leaves 2048) with opacity. Metres = raw prototype units x instance scale x 0.01 (the layers' centimetres).
"""
import json
import math
import os
import subprocess
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(UNREAL, "Tools", "ruins"))
sys.path.insert(0, os.path.join(UNREAL, "Tools", "openworld"))
import ruins_spec as RS  # noqa: E402
import prep_vegetation as PV  # noqa: E402  (decimate, join; imports prep_trees for the FBX settings)
from pxr import Usd, UsdGeom, UsdShade  # noqa: E402

PT = PV.PT
SRC = os.path.join(UNREAL, "SourceArt", "OpenWorld", "JungleRuins", "extract", "JungleRuins")
USD = os.path.join(SRC, "USD", "elements")
OUT = os.path.join(UNREAL, "SourceArt", "OpenWorld", "JungleRuins", "Game")
TMP = os.path.join(OUT, "tmp")
PREVIEW = os.path.join(UNREAL, "docs", "ruins", "previews", "layout.png")
CX, CY = RS.CENTER_BL
CORE_L = (RS.CORE_BL[0] - CX, RS.CORE_BL[1] - CY, RS.CORE_BL[2] - CX, RS.CORE_BL[3] - CY)
TILE_L = (RS.TILE_BL[0] - CX, RS.TILE_BL[1] - CY, RS.TILE_BL[2] - CX, RS.TILE_BL[3] - CY)
TERRAIN_RES, BACKDROP_RES, BACKDROP_N_RES, STONE_RES = 2048, 2048, 1024, 2048
TERRAIN_LOD_RATIO = (0.3, 0.08)
BACKDROP_RATIO = 0.03
STONE_TRIS = (56000, 16000)
VEG_MASKED = {"RiverFamily_Leaves", "Grass_01", "Grass_02", "Shrub_04"}
VEG_RES = {"RiverFamily_Trunk": 1024, "RiverFamily_Branches": 1024, "RiverFamily_Leaves": 2048, "Grass_01": 1024,
           "Grass_02": 1024, "Shrub_04": 1024}
TERRAIN_SCALARS = {"UVScale": 1.0, "GrimeAmount": 0.0, "Saturation": 1.0, "Brightness": 2.2, "MacroStrength": 0.12,
                   "MacroTileCm": 2400.0}
STONE_SCALARS = {"UVScale": 1.0, "GrimeAmount": 0.0, "Saturation": 1.0, "Brightness": 1.0, "MacroStrength": 0.08}
WATER_SLOT = {"master": "water",
              "scalars": {"Speed1": 0.04, "Speed2": 0.025, "Tile1Cm": 600.0, "Tile2Cm": 260.0, "RippleStrength": 0.6,
                          "OpacityMin": 0.72, "OpacityMax": 0.95},
              "vectors": {"Dir1": [0.95, 0.3, 0.0], "Dir2": [-0.3, 0.95, 0.0], "DeepColor": [0.03, 0.05, 0.025],
                          "ShallowColor": [0.12, 0.16, 0.08], "SkyReflection": [0.30, 0.38, 0.34]}}


# ------------------------------------------------------------------------------------------------------------ helpers
def base(name):
    return name.split(".")[0]


def usd_import(path, mask=""):
    """Imports meshes (+ material names) of a USD layer; returns the new MESH objects."""
    before = set(bpy.data.objects)
    bpy.ops.wm.usd_import(filepath=path, import_cameras=False, import_lights=False, import_materials=True,
                          import_meshes=True, import_volumes=False, import_shapes=False, import_skeletons=False,
                          import_blendshapes=False, import_points=False, import_subdivision=False,
                          support_scene_instancing=False, import_usd_preview=False, read_mesh_uvs=True,
                          read_mesh_colors=False, prim_path_mask=mask, create_collection=False,
                          apply_unit_conversion_scale=True, scale=1.0, import_textures_mode="IMPORT_NONE")
    return [o for o in bpy.data.objects if o not in before and o.type == "MESH"]


def keep_uv(me, name):
    for n in [u.name for u in me.uv_layers if u.name != name]:
        me.uv_layers.remove(me.uv_layers[n])
    if len(me.uv_layers) == 0:
        me.uv_layers.new(name=name)


def weld(me, dist=1e-3):
    """Merges coincident vertices. The scene's USD meshes import split (every terrain quad has its own 4 vertices), which
    makes every vertex an open edge: the seam lock would freeze the whole mesh and collapse decimation could only shrink
    each quad on its own. UVs and normals live on the face corners and survive the merge."""
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bm.to_mesh(me)
    bm.free()
    me.update()
    return me


def map_mesh(ob, name):
    """Copy of an imported object's mesh in map space (Blender metres minus CENTER_BL), welded, 'st' UVs only."""
    me = ob.data.copy()
    me.transform(ob.matrix_world)
    me.transform(Matrix.Translation((-CX, -CY, 0.0)))
    weld(me)
    keep_uv(me, "st")
    me.name = name
    return me


def verts_world(ob):
    co = np.empty(len(ob.data.vertices) * 3, np.float32)
    ob.data.vertices.foreach_get("co", co)
    m = np.array(ob.matrix_world, np.float64)
    return co.reshape(-1, 3).astype(np.float64).dot(m[:3, :3].T) + m[:3, 3]


def keep_faces(me, inside):
    """Deletes every face whose centroid fails inside(x, y) (numpy arrays in, bool array out)."""
    c = np.empty(len(me.polygons) * 3, np.float32)
    me.polygons.foreach_get("center", c)
    c = c.reshape(-1, 3)
    drop = np.nonzero(~inside(c[:, 0], c[:, 1]))[0]
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[int(i)] for i in drop], context="FACES")
    bm.to_mesh(me)
    bm.free()
    me.update()
    return me


def planar_uv(me, x0, y0, size):
    keep_uv(me, "st")
    co = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    vi = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", vi)
    uv = np.stack([(co[vi, 0] - x0) / size, (co[vi, 1] - y0) / size], 1).astype(np.float32)
    me.uv_layers[0].data.foreach_set("uv", uv.ravel())


def set_material(me, name):
    me.materials.clear()
    me.materials.append(bpy.data.materials.get(name) or bpy.data.materials.new(name))
    me.polygons.foreach_set("material_index", np.zeros(len(me.polygons), np.int32))


def boundary_count(me):
    bm = bmesh.new()
    bm.from_mesh(me)
    n = sum(1 for v in bm.verts if v.is_boundary)
    bm.free()
    return n


def decimate_locked(me, ratio):
    """Collapse decimation that keeps every open edge (chunk seams, the backdrop's hole) exactly, so neighbours meet."""
    work = me.copy()
    ob = bpy.data.objects.new("dec_locked", work)
    bpy.context.scene.collection.objects.link(ob)
    bm = bmesh.new()
    bm.from_mesh(work)
    edge = [v.index for v in bm.verts if v.is_boundary]
    bm.free()
    vg = ob.vertex_groups.new(name="lock")
    vg.add(edge, 1.0, "REPLACE")
    md = ob.modifiers.new("dec", "DECIMATE")
    md.decimate_type = "COLLAPSE"
    md.ratio = ratio
    md.use_collapse_triangulate = True
    md.vertex_group = "lock"
    md.invert_vertex_group = True
    md.vertex_group_factor = 1.0
    dg = bpy.context.evaluated_depsgraph_get()
    out = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    bpy.data.objects.remove(ob)
    out.name = me.name
    return out


def thin_cards(me, keep, salt):
    """Drops whole islands (leaf cards, twig clusters, grass blades) by a stable hash (< keep survives) and widens each
    survivor about its own centre by keep^-0.5 (at most 1.8x), so the canopy keeps its coverage. The largest island (the
    trunk with its branches) always stays as it is."""
    face_isl, _ls, lt, lv = PT.islands(me)
    roots, inv = np.unique(face_isl, return_inverse=True)
    isl_tris = np.bincount(inv, weights=(lt - 2).astype(np.float64))
    card = np.ones(len(roots), bool)
    card[int(np.argmax(isl_tris))] = False
    drop = card & (PV._hash01(roots, salt) >= keep)
    vert_isl = np.full(len(me.vertices), -1, np.int64)
    vert_isl[lv] = np.repeat(inv, lt)
    co = PV.verts(me).astype(np.float64)
    sel = vert_isl >= 0
    vi = vert_isl[sel]
    n = len(roots)
    cnt = np.maximum(np.bincount(vi, minlength=n), 1)
    centre = np.stack([np.bincount(vi, weights=co[sel, k], minlength=n) / cnt for k in range(3)], 1)
    grow = sel.copy()
    grow[sel] = card[vi] & ~drop[vi]
    s = min(1.8, max(keep, 1e-3) ** -0.5)
    co[grow] = centre[vert_isl[grow]] + (co[grow] - centre[vert_isl[grow]]) * s
    me.vertices.foreach_set("co", co.astype(np.float32).ravel())
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[int(i)] for i in np.nonzero(drop[inv])[0]], context="FACES")
    bm.to_mesh(me)
    bm.free()
    me.update()
    return me


def lod_mesh(me, target, salt):
    """Collapse decimation to `target` tris. Separate cards (a tree's leaves, a tuft's blades) cannot collapse below two
    triangles each, so when they hold the mesh above the target a share of them is dropped first (thin_cards, the share
    tightened until the target is met or 3 % of the cards remain) and the rest decimates to the target."""
    base_me = PV.decimate(me.copy(), target)
    t = PT.tris(base_me)
    if t <= target * 1.05:
        return base_me
    face_isl, _ls, lt, _lv = PT.islands(base_me)
    _roots, inv = np.unique(face_isl, return_inverse=True)
    isl_tris = np.bincount(inv, weights=(lt - 2).astype(np.float64))
    cards = float(isl_tris.sum() - isl_tris.max())
    if cards <= 0.0:
        return base_me
    keep = min(1.0, max(0.03, (target - (t - cards)) / cards))
    for _ in range(5):
        out = PV.decimate(thin_cards(base_me.copy(), keep, salt), target)
        t = PT.tris(out)
        if t <= target * 1.05 or keep <= 0.03:
            break
        bpy.data.meshes.remove(out)
        keep = max(0.03, keep * 0.9 * target / t)
    return out


def export_lods(name, lods, role, slots, layout):
    entry = {"role": role, "lods": [], "slots": slots}
    for li, me in enumerate(lods):
        ob = bpy.data.objects.new(f"{name}_LOD{li}", me)
        bpy.context.scene.collection.objects.link(ob)
        fbx = os.path.join(OUT, f"{name}_LOD{li}.fbx")
        PT.export(ob, fbx)
        entry["lods"].append({"fbx": os.path.basename(fbx), "tris": PT.tris(me)})
        bpy.data.objects.remove(ob)
    layout["meshes"][name] = entry
    print(f"[prep_ruins] {name}: tris {[lod['tris'] for lod in entry['lods']]}", flush=True)
    return entry


# ------------------------------------------------------------------------------------------------------------ images
def sips(*args):
    subprocess.run(["/usr/bin/sips", *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def image_size(path):
    out = subprocess.run(["/usr/bin/sips", "-g", "pixelWidth", "-g", "pixelHeight", path], check=True, capture_output=True,
                         text=True).stdout.split()
    return int(out[out.index("pixelWidth:") + 1]), int(out[out.index("pixelHeight:") + 1])


def load_rgba(path):
    """Image -> float32 H x W x 4, row 0 = top, raw values (no colour management)."""
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"
    w, h = img.size
    a = np.empty(w * h * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(h, w, 4)[::-1].copy()


def save_rgba(arr, path, alpha):
    """float32 H x W x 4 (row 0 = top) -> 8-bit PNG; alpha forced to 1 unless `alpha`."""
    h, w, _ = arr.shape
    out = np.ascontiguousarray(arr[::-1], np.float32).copy()
    if not alpha:
        out[..., 3] = 1.0
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=alpha)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(out.ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return os.path.splitext(os.path.basename(path))[0]


def resized(path, res):
    os.makedirs(TMP, exist_ok=True)
    tmp = os.path.join(TMP, "resized.png")
    sips("-s", "format", "png", "-z", str(res), str(res), path, "--out", tmp)
    return load_rgba(tmp)


def crop(path, row, col, h, w, out_h, out_w):
    os.makedirs(TMP, exist_ok=True)
    tmp = os.path.join(TMP, "crop.png")
    sips("-s", "format", "png", "-c", str(h), str(w), "--cropOffset", str(row), str(col), path, "--out", tmp)
    sips("-z", str(out_h), str(out_w), tmp, "--out", tmp)
    return load_rgba(tmp)


def save_final(canvas, name, res, normal):
    """Canvas -> T_Ruins_<name>.png at res x res (normal maps: green flipped OpenGL -> DirectX first)."""
    if normal:
        canvas = canvas.copy()
        canvas[..., 1] = 1.0 - canvas[..., 1]
    os.makedirs(TMP, exist_ok=True)
    tmp = os.path.join(TMP, "canvas.png")
    save_rgba(canvas, tmp, False)
    sips("-z", str(res), str(res), tmp, "--out", os.path.join(OUT, f"T_Ruins_{name}.png"))
    return f"T_Ruins_{name}"


def material_files(mat_prim):
    """{'BC' | 'N' | 'R' | 'OP': absolute path} of a UsdPreviewSurface material, by texture file name."""
    out = {}
    for q in Usd.PrimRange(mat_prim):
        if q.GetTypeName() != "Shader":
            continue
        inp = UsdShade.Shader(q).GetInput("file")
        if not inp or inp.Get() is None:
            continue
        path = inp.Get().resolvedPath
        if not path:
            continue
        low = os.path.basename(path).lower()
        role = ("OP" if "opacity" in low else "N" if "normal" in low else
                "R" if ("roughness" in low and "metallic" not in low) else
                "BC" if ("basecolor" in low or "albedo" in low) else None)
        if role and role not in out:
            out[role] = path
    return out


_MATS = {}


def usd_materials(path):
    if path not in _MATS:
        st = Usd.Stage.Open(path)
        _MATS[path] = {p.GetName(): material_files(p) for p in st.Traverse() if p.GetTypeName() == "Material"}
    return _MATS[path]


def texture_set(name, files, res, masked):
    """T_Ruins_<name>_{BC,N,ORM[,OP]}.png from a USD material's maps; returns the slot's texture names."""
    stem = os.path.join(OUT, f"T_Ruins_{name}")
    bc = resized(files["BC"], res)
    out = {}
    if masked:
        op = resized(files["OP"], res)[..., 0] if "OP" in files else bc[..., 3].copy()
        bc[..., 3] = op
        out["OP"] = save_rgba(np.dstack([op, op, op, np.ones_like(op)]), stem + "_OP.png", False)
    out["BC"] = save_rgba(bc, stem + "_BC.png", masked)
    n = resized(files["N"], res)
    n[..., 1] = 1.0 - n[..., 1]          # UsdPreviewSurface normal maps are OpenGL (+Y up); Unreal reads DirectX
    out["N"] = save_rgba(n, stem + "_N.png", False)
    r = resized(files["R"], res)[..., 0] if "R" in files else np.full((res, res), 0.8, np.float32)
    out["ORM"] = save_rgba(np.dstack([np.ones_like(r), r, np.zeros_like(r), np.ones_like(r)]), stem + "_ORM.png", False)
    return out


# ------------------------------------------------------------------------------------------------------------ terrain
def uv_map(ob, files):
    """Planar world -> uv fit of one terrain tile (u, v = A (x, y) + b) with its material's maps."""
    me = ob.data
    co = verts_world(ob)
    vi = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", vi)
    uv = np.empty(len(me.loops) * 2, np.float32)
    me.uv_layers["st"].data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2).astype(np.float64)
    X = np.stack([co[vi, 0], co[vi, 1], np.ones(len(vi))], 1)
    coef = np.linalg.lstsq(X, uv, rcond=None)[0]
    resid = float(np.abs(X.dot(coef) - uv).max())
    A, b = coef[:2].T, coef[2]
    if resid > 2e-3 or A[0, 0] <= 0.0 or A[1, 1] <= 0.0 or abs(A[0, 1]) > 1e-6 or abs(A[1, 0]) > 1e-6:
        raise RuntimeError(f"{ob.name}: terrain UVs are not an axis-aligned planar map (residual {resid:.4f}, A {A.tolist()})")
    f = files[base(me.materials[0].name)]
    w, h = image_size(f["BC"])
    return {"bc": f["BC"], "n": f["N"], "A": A, "b": b, "w": w, "h": h,
            "extent": (co[:, 0].min(), co[:, 1].min(), co[:, 0].max(), co[:, 1].max())}


def assemble(tiles, rect, ppm, key):
    """Stitches the tiles' `key` maps ('bc' | 'n') over rect = (x0, y0, x1, y1) (Blender metres) at ppm px / m; row 0 = north."""
    x0, y0, x1, y1 = rect
    W, H = int(round((x1 - x0) * ppm)), int(round((y1 - y0) * ppm))
    canvas = np.zeros((H, W, 4), np.float32)
    canvas[..., 3] = 1.0
    for t in tiles:
        ex0, ey0, ex1, ey1 = t["extent"]
        ix0, iy0, ix1, iy1 = max(x0, ex0), max(y0, ey0), min(x1, ex1), min(y1, ey1)
        if ix1 - ix0 < 1e-3 or iy1 - iy0 < 1e-3:
            continue
        u0, v0 = t["A"].dot([ix0, iy0]) + t["b"]
        u1, v1 = t["A"].dot([ix1, iy1]) + t["b"]
        c0, c1 = int(round(u0 * t["w"])), int(round(u1 * t["w"]))
        r0, r1 = int(round((1.0 - v1) * t["h"])), int(round((1.0 - v0) * t["h"]))
        cx0, cx1 = int(round((ix0 - x0) * ppm)), int(round((ix1 - x0) * ppm))
        cy0, cy1 = int(round((y1 - iy1) * ppm)), int(round((y1 - iy0) * ppm))
        if c1 <= c0 or r1 <= r0 or cx1 <= cx0 or cy1 <= cy0:
            continue
        canvas[cy0:cy1, cx0:cx1] = crop(t[key], r0, c0, r1 - r0, c1 - c0, cy1 - cy0, cx1 - cx0)[: cy1 - cy0, : cx1 - cx0]
    return canvas


def build_terrain(layout):
    path = os.path.join(USD, "Terrain", "Terrain_Cinematic.usd")
    obs = usd_import(path)
    if len(obs) != 16:
        raise RuntimeError(f"Terrain_Cinematic: expected 16 tiles, imported {len(obs)}")
    tiles = [uv_map(ob, usd_materials(path)) for ob in obs]
    ppm = tiles[0]["A"][0, 0] * tiles[0]["w"]                      # 8.192 px / m: the maps' own resolution
    slot = {"master": "surface", "BC": save_final(assemble(tiles, RS.CORE_BL, ppm, "bc"), "Terrain_BC", TERRAIN_RES, False),
            "N": save_final(assemble(tiles, RS.CORE_BL, ppm, "n"), "Terrain_N", TERRAIN_RES, True),
            "ORM": save_rgba(np.dstack([np.ones((64, 64)), np.full((64, 64), 0.85), np.zeros((64, 64)), np.ones((64, 64))])
                             .astype(np.float32), os.path.join(OUT, "T_Ruins_Terrain_ORM.png"), False),
            "scalars": TERRAIN_SCALARS}
    full = weld(PV.join([(map_mesh(ob, f"tile_{i}"), Matrix.Identity(4)) for i, ob in enumerate(obs)], "terrain_full"))
    size = (CORE_L[2] - CORE_L[0]) / RS.CHUNKS
    for cj in range(RS.CHUNKS):
        for ci in range(RS.CHUNKS):
            x0, y0 = CORE_L[0] + ci * size, CORE_L[1] + cj * size
            me = keep_faces(full.copy(), lambda x, y: (x >= x0) & (x < x0 + size) & (y >= y0) & (y < y0 + size))
            me.name = f"SM_Ruins_Terrain_{ci}_{cj}"
            planar_uv(me, CORE_L[0], CORE_L[1], CORE_L[2] - CORE_L[0])
            set_material(me, "Terrain")
            me.shade_smooth()
            lods = [me] + [decimate_locked(me, r) for r in TERRAIN_LOD_RATIO]
            seam = boundary_count(me)
            for li, lod in enumerate(lods[1:], start=1):
                if boundary_count(lod) != seam:
                    raise RuntimeError(f"{me.name} LOD{li}: seam has {boundary_count(lod)} vertices, LOD0 {seam} (cracks)")
            export_lods(me.name, lods, "ground", {"Terrain": slot}, layout)
    back = keep_faces(full.copy(), lambda x, y: ~((x >= CORE_L[0]) & (x < CORE_L[2]) & (y >= CORE_L[1]) & (y < CORE_L[3])))
    back.name = "SM_Ruins_Backdrop"
    back = decimate_locked(back, BACKDROP_RATIO)
    planar_uv(back, TILE_L[0], TILE_L[1], TILE_L[2] - TILE_L[0])
    set_material(back, "Backdrop")
    back.shade_smooth()
    bslot = {"master": "surface",
             "BC": save_final(assemble(tiles, RS.TILE_BL, BACKDROP_RES / 1000.0, "bc"), "Backdrop_BC", BACKDROP_RES, False),
             "N": save_final(assemble(tiles, RS.TILE_BL, BACKDROP_N_RES / 1000.0, "n"), "Backdrop_N", BACKDROP_N_RES, True),
             "ORM": "T_Ruins_Terrain_ORM", "scalars": TERRAIN_SCALARS}
    export_lods("SM_Ruins_Backdrop", [back], "backdrop", {"Backdrop": bslot}, layout)


# ------------------------------------------------------------------------------------------------------------ stone / water
def build_stone(layout):
    path = os.path.join(USD, "Pyramid", "Pyramid.usd")
    ob = next(o for o in usd_import(path) if base(o.name) == "Pyramid_v1a")
    me = map_mesh(ob, "SM_Ruins_Pyramid")
    files = usd_materials(path)
    slots = {}
    for i, m in enumerate(me.materials):
        src = base(m.name)
        part = "Pyr_" + src.replace("MI_Pyramid_", "")
        me.materials[i] = bpy.data.materials.get(part) or bpy.data.materials.new(part)
        if part not in slots:
            slots[part] = dict(master="surface", scalars=STONE_SCALARS, **texture_set(part, files[src], STONE_RES, False))
    lods = [me] + [PV.decimate(me.copy(), t) for t in STONE_TRIS]
    export_lods("SM_Ruins_Pyramid", lods, "stone", slots, layout)


def face_up(me):
    """Turns every face of a surface mesh to face +Z (water is seen and traced from above; one-sided collision and
    materials ignore back faces)."""
    bm = bmesh.new()
    bm.from_mesh(me)
    down = [f for f in bm.faces if f.normal.z < 0.0]
    if down:
        bmesh.ops.reverse_faces(bm, faces=down)
    bm.to_mesh(me)
    bm.free()
    me.update()
    print(f"[prep_ruins] {me.name}: {len(down)} of {len(me.polygons)} faces turned up", flush=True)
    return me


def build_water(layout):
    for name, rel, prim in (("SM_Ruins_River", ("River", "River.usd"), "River"),
                            ("SM_Ruins_Creek", ("Creek", "Creek.usd"), "Creek")):
        ob = next(o for o in usd_import(os.path.join(USD, *rel)) if base(o.name) == prim)
        me = keep_faces(map_mesh(ob, name), lambda x, y: (x >= TILE_L[0]) & (x < TILE_L[2]) & (y >= TILE_L[1]) & (y < TILE_L[3]))
        face_up(me)                                              # River.usd is wound left-handed: its faces point down
        planar_uv(me, TILE_L[0], TILE_L[1], 10.0)                # the water material pans world-space normals; UVs unused
        set_material(me, "Water")
        export_lods(name, [me], "water", {"Water": WATER_SLOT}, layout)


# ------------------------------------------------------------------------------------------------------------ jungle
def instancers(pi_path, kind):
    """[(instancer name, kept instances)] of one point-instancer layer; instance = dict(proto, pos (Blender m), q, s)."""
    st = Usd.Stage.Open(pi_path, Usd.Stage.LoadNone)
    mpu = UsdGeom.GetStageMetersPerUnit(st)
    c = RS.CORE_BL
    out = []
    for p in st.TraverseAll():
        if p.GetTypeName() != "PointInstancer":
            continue
        pi = UsdGeom.PointInstancer(p)
        targets = pi.GetPrototypesRel().GetTargets()
        ids, ori, sca = pi.GetProtoIndicesAttr().Get(), pi.GetOrientationsAttr().Get(), pi.GetScalesAttr().Get()
        pos = np.array(pi.GetPositionsAttr().Get(), np.float64) * mpu
        x, y = pos[:, 0], pos[:, 1]
        core = (x >= c[0]) & (x < c[2]) & (y >= c[1]) & (y < c[3])
        ring = (~core & (x >= c[0] - RS.RING_M) & (x < c[2] + RS.RING_M) & (y >= c[1] - RS.RING_M) & (y < c[3] + RS.RING_M)
                if kind == "tree" else np.zeros_like(core))
        salt = RS.salt(p.GetName())
        kept = []
        for j in np.nonzero(core | ring)[0]:
            j = int(j)
            if not RS.keep(salt, j, RS.THIN[kind] if core[j] else RS.RING_THIN):
                continue
            q = ori[j] if ori else None
            s = (float(sca[j][0]) + float(sca[j][1]) + float(sca[j][2])) / 3.0 if sca else 1.0
            kept.append({"proto": targets[int(ids[j])].name, "pos": pos[j].copy(),
                         "q": (q.GetReal(), *q.GetImaginary()) if q is not None else (1.0, 0.0, 0.0, 0.0), "s": s * mpu})
        out.append((p.GetName(), kept))
    return out


def prototypes(classes_path, names):
    """Prototype meshes in raw units (the class layer's /root x100 and its centimetres cancel) -> {name: mesh}."""
    if not names:
        return {}
    obs = usd_import(classes_path, ",".join(f"/root/{n}" for n in sorted(names)))
    out = {}
    for n in names:
        ob = next((o for o in obs if base(o.name) == n or (o.parent is not None and base(o.parent.name) == n)), None)
        if ob is None:
            raise RuntimeError(f"{classes_path}: prototype /root/{n} not imported")
        me = ob.data.copy()
        me.transform(ob.matrix_world)
        weld(me)
        keep_uv(me, "st")
        out[n] = me
    return out


def veg_slot_name(mat_name):
    n = base(mat_name)
    n = n[3:] if n.startswith("MI_") else n
    return n[: -len("_TwoSided")] if n.endswith("_TwoSided") else n


def mesh_height(me):
    co = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    z = co.reshape(-1, 3)[:, 2]
    return float(z.max() - z.min())


def trunk_radius(me):
    """Median distance of trunk vertices 0.4..1.6 m above the base from their centre (raw units), 0 without a trunk."""
    ti = {i for i, m in enumerate(me.materials) if m is not None and m.name.startswith("RiverFamily_Trunk")}
    if not ti:
        return 0.0
    vs = set()
    for p in me.polygons:
        if p.material_index in ti:
            vs.update(p.vertices)
    co = np.array([me.vertices[v].co[:] for v in sorted(vs)], np.float64)
    z0 = co[:, 2].min()
    band = co[(co[:, 2] > z0 + 0.4) & (co[:, 2] < z0 + 1.6)]
    if len(band) < 8:
        return 0.0
    d = band[:, :2] - band[:, :2].mean(0)
    return float(np.median(np.hypot(d[:, 0], d[:, 1])))


def build_vegetation(layout):
    props, counts, info, slot_defs = [], {}, {}, {}
    for pi_rel, cls_rel, kind in RS.VEG:
        cls_path = os.path.join(USD, cls_rel)
        groups = instancers(os.path.join(USD, pi_rel), kind)
        names = sorted({i["proto"] for _n, kept in groups for i in kept})
        pending = [n for n in names if f"SM_Ruins_{RS.proto_key(n)}" not in info]
        files = usd_materials(cls_path) if pending else {}
        for n, me in prototypes(cls_path, pending).items():
            mesh_name = f"SM_Ruins_{RS.proto_key(n)}"
            slots = {}
            for i, m in enumerate(me.materials):
                src = base(m.name)
                slot = veg_slot_name(src)
                me.materials[i] = bpy.data.materials.get(slot) or bpy.data.materials.new(slot)
                if slot not in slot_defs:
                    tex = texture_set(slot, files[src], VEG_RES[slot], slot in VEG_MASKED)
                    if slot in VEG_MASKED:
                        grass = slot.startswith("Grass")
                        slot_defs[slot] = dict(master="leaf", scalars={"SwayCm": 3.0 if grass else 5.0, "FlutterCm": 1.5,
                                                                       "ColorVariation": 0.18,
                                                                       "Brightness": 1.3 if grass else 1.0}, **tex)
                    else:
                        slot_defs[slot] = dict(master="bark", scalars={"SwayCm": 1.0}, **tex)
                slots[slot] = slot_defs[slot]
            info[mesh_name] = {"height": mesh_height(me), "trunk": trunk_radius(me)}
            lods = [lod_mesh(me, t, RS.salt(mesh_name) + li) for li, t in enumerate(RS.LOD_TRIS[kind])]
            entry = export_lods(mesh_name, lods, "veg", slots, layout)
            entry.update(kind=kind, height_m=round(info[mesh_name]["height"], 3), trunk_r_m=round(info[mesh_name]["trunk"], 3))
        for _n, kept in groups:
            for inst in kept:
                mesh_name = f"SM_Ruins_{RS.proto_key(inst['proto'])}"
                rot = Quaternion(inst["q"]).to_matrix()
                x, y, z = RS.bl_to_world(*inst["pos"])
                prop = {"mesh": mesh_name, "kind": kind, "pos": [round(x, 3), round(y, 3), round(z, 3)],
                        "yaw": round(RS.bl_yaw_to_ue(math.atan2(rot[1][0], rot[0][0])) % 360.0, 2), "scale": round(inst["s"], 4)}
                if kind == "tree":
                    prop["trunk_r"] = round(info[mesh_name]["trunk"] * inst["s"], 3)
                props.append(prop)
                counts[kind] = counts.get(kind, 0) + 1
    for kind, n in counts.items():
        if n > RS.BUDGET[kind]:
            raise RuntimeError(f"{kind}: {n} instances over the budget {RS.BUDGET[kind]}")
    trees = sorted(info[p["mesh"]]["height"] * p["scale"] for p in props if p["kind"] == "tree")
    if trees and not 6.0 <= trees[len(trees) // 2] <= 30.0:
        raise RuntimeError(f"median tree height {trees[len(trees) // 2]:.1f} m: unit conversion is off")
    layout["props"], layout["counts"] = props, counts
    print(f"[prep_ruins] props {counts}, median tree {trees[len(trees) // 2] if trees else 0:.1f} m", flush=True)


# ------------------------------------------------------------------------------------------------------------ preview
def write_preview(layout):
    """Top view of the core at 2 px / m: terrain height (grey), river level and below (blue), pyramid (sand), props."""
    n = 576
    ppm = n / (CORE_L[2] - CORE_L[0])
    hmap = np.full((n, n), np.nan, np.float32)
    for name, m in layout["meshes"].items():
        if m["role"] != "ground":
            continue
        ob = next((o for o in bpy.data.objects if o.name == "preview_tmp"), None)
        path = os.path.join(OUT, m["lods"][0]["fbx"])
        bpy.ops.import_scene.fbx(filepath=path)
        ob = bpy.context.selected_objects[0]
        co = verts_world(ob)                                      # Unreal import axes do not apply: Blender re-imports
        bpy.data.objects.remove(ob)
        c = np.clip(((co[:, 0] - CORE_L[0]) * ppm).astype(int), 0, n - 1)
        r = np.clip(((CORE_L[3] - co[:, 1]) * ppm).astype(int), 0, n - 1)
        hmap[r, c] = np.fmax(hmap[r, c], co[:, 2])
    for _ in range(3):                                            # close the gaps between 1.24 m vertices
        pad = np.pad(hmap, 1, constant_values=np.nan)
        neigh = np.nanmax(np.stack([pad[1 + dy:n + 1 + dy, 1 + dx:n + 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)]), 0)
        hmap = np.where(np.isnan(hmap), neigh, hmap)
    g = np.clip((np.nan_to_num(hmap, nan=-10.0) + 12.0) / 40.0, 0.0, 1.0)
    img = np.stack([g * 0.55 + 0.2, g * 0.6 + 0.25, g * 0.45 + 0.15, np.ones_like(g)], -1).astype(np.float32)
    img[np.nan_to_num(hmap, nan=0.0) < -9.5] = (0.15, 0.3, 0.55, 1.0)
    px, _, pz = RS.bl_to_world(*RS.PYRAMID_BL, 0.0)
    pc, pr = int((px - CORE_L[0]) * ppm), int((-pz - CORE_L[1] - (CORE_L[3] - CORE_L[1])) * -ppm)
    img[max(0, pr - 42):pr + 42, max(0, pc - 42):pc + 42, :3] = img[max(0, pr - 42):pr + 42, max(0, pc - 42):pc + 42, :3] * 0.4 + np.array([0.85, 0.75, 0.55]) * 0.6
    colours = {"tree": (0.05, 0.3, 0.08), "sapling": (0.15, 0.5, 0.15), "seedling": (0.35, 0.6, 0.2), "shrub": (0.45, 0.65, 0.25)}
    for p in layout.get("props", []):
        col = colours.get(p["kind"])
        if col is None:
            continue
        cc, rr = int((p["pos"][0] - CORE_L[0]) * ppm), int((p["pos"][2] - (-CORE_L[3])) * ppm)
        if 0 <= cc < n and 0 <= rr < n:
            img[max(0, rr - 1):rr + 2, max(0, cc - 1):cc + 2, :3] = col
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    save_rgba(img, PREVIEW, False)
    print(f"[prep_ruins] wrote {PREVIEW}", flush=True)


# ------------------------------------------------------------------------------------------------------------ main
def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else {"terrain", "stone", "water", "veg"}
    if not os.path.isdir(USD):
        raise SystemExit(f"missing {USD}: run python3 -I unreal/Tools/ruins/fetch_ruins.py first")
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    path = os.path.join(OUT, "layout.json")
    layout = json.load(open(path)) if os.path.exists(path) else {}
    layout.update({"version": 1, "generator": "unreal/Tools/blender/ruins/prep_ruins.py",
                   "source": {"url": RS.SOURCE_URL, "license": RS.LICENSE, "credit": RS.CREDIT, "changes": RS.CHANGES},
                   "frame": {"core_bl": list(RS.CORE_BL), "center_bl": list(RS.CENTER_BL),
                             "world": "x = bx - cx, y = bz, z = -(by - cy)", "unreal": "UE = 100 * (x, z, y), meshes at the origin"}})
    layout.setdefault("meshes", {})
    for part, fn in (("terrain", build_terrain), ("stone", build_stone), ("water", build_water), ("veg", build_vegetation)):
        if part in only:
            fn(layout)
            bpy.ops.wm.read_factory_settings(use_empty=True)
    layout["actors"] = [{"label": "FF_" + name[3:], "mesh": name} for name, m in sorted(layout["meshes"].items())
                        if m["role"] != "veg"]
    with open(path, "w") as f:
        json.dump(layout, f, indent=1)
    print(f"[prep_ruins] wrote {path}: {len(layout['meshes'])} meshes, {len(layout.get('props', []))} props", flush=True)
    if "terrain" in only or "veg" in only:
        write_preview(layout)


if __name__ == "__main__":
    main()
