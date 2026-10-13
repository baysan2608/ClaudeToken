"""Poly Haven scans -> open-world ground detail for Unreal: grass clumps, small plants, shrubs and boulders, one FBX per mesh and
LOD (the prep_trees.py convention, Unreal's foliage importer adds LOD1+ to LOD0) + game textures + vegetation.json + a contact
sheet (Blender 5.0, background).

  /Applications/Blender.app/Contents/MacOS/Blender -b --disable-autoexec --factory-startup \
      --python unreal/Tools/openworld/prep_vegetation.py [-- --only SM_OW_Fern_a,SM_OW_Boulder_b] [--no-bake] [--no-sheet]

Input (CC0, git-ignored): SourceArt/OpenWorld/Models/<asset>/<asset>_2k.blend (Tools/openworld/fetch_models.py) and, for the
shrubs, SourceArt/Environment/PolyHaven/Models/shrub_0x (Tools/world/fetch_polyhaven_models.py, role "shrub"; prep_trees.py
does not export them). The .blend files are appended object by object; nothing inside them runs (--disable-autoexec).
Output: SourceArt/OpenWorld/Meshes/SM_OW_<Name>_LOD<n>.fbx, T_OW_Veg_<Set>_{BC,N,ORM}.png, T_OW_Boulder_<v>_{BC,N,ORM}.png,
vegetation.json; docs/openworld/previews/vegetation_sheet.png.

How each kind is built:
  * grass  - "lawn" clumps are scattered here from grass_bermuda_01's single sprigs (5-30 tris each; the scan's 0.2 M tris are
             a geometry-nodes scatter of those sprigs): dart-thrown positions in a disk, random order, LOD0 takes sprigs in that
             order up to its budget, LOD1/2 a cheap-sprig prefix of the same order widened (xy) to keep the coverage.
             "tuft" clumps are grass_medium_02 tufts (real blade strips): LOD0 collapse-decimated, LOD1/2 also drop whole blades
             (stable hash, a coarser LOD keeps a subset) - blade strips break up below ~30 % collapse, dropping blades does not.
  * plant / shrub - the scan's own LODs where it has them, else collapse decimation (alpha cards survive it well).
  * boulder - the scan's coarsest LOD collapse-decimated to the budget; LOD1/2 decimated from LOD0 so UVs stay one layout.
             The tangent-space normal map is re-baked (Cycles, CPU) from the full scan (LOD0 + its own normal map) onto the
             game LOD0, so the scan detail survives the 3k-tri silhouette; BC / ORM are the scan's maps (same UV layout).
Every mesh: real size (m), Blender Z-up exported with prep_trees' FBX settings; pivot = root (foliage) / bbox centre (boulders)
in xy, z = 0 at the scan's authored ground (parts below it are embedment, "below_ground_m" in the manifest). Material slots carry
the texture-set name (Grass, GrassTall, Fern, ...). Foliage vertex colour: R = height fraction (wind bend weight), G = random
phase per sprig / blade / frond, B = 0. Textures: BC sRGB (foliage: alpha = opacity), N DirectX (green down), ORM = AO, rough,
metal (Poly Haven "arm").
"""
import json
import math
import os
import sys

import bmesh  # noqa: F401  (bpy needs it loaded for some mesh ops in background mode)
import bpy
import numpy as np
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.normpath(os.path.join(HERE, "..", ".."))
OW_MODELS = os.path.join(UNREAL, "SourceArt", "OpenWorld", "Models")
ENV_MODELS = os.path.join(UNREAL, "SourceArt", "Environment", "PolyHaven", "Models")
OUT = os.path.join(UNREAL, "SourceArt", "OpenWorld", "Meshes")
SHEET = os.path.join(UNREAL, "docs", "openworld", "previews", "vegetation_sheet.png")
sys.path.insert(0, os.path.join(UNREAL, "Tools", "blender", "world"))
import prep_trees as PT  # noqa: E402  (FBX settings, export, tris, islands)

# texture set (= material slot) -> Poly Haven asset, output size, alpha-tested
SETS = {
    "Grass": dict(asset="grass_bermuda_01", res=2048, alpha=True),
    "GrassTall": dict(asset="grass_medium_02", res=2048, alpha=True, alt={"Dry": "grass_medium_02_dry_diff_2k.png"}),
    "Fern": dict(asset="fern_02", res=1024, alpha=True),
    "Flower": dict(asset="flower_empodium", res=1024, alpha=True),
    "Dandelion": dict(asset="dandelion_01", res=1024, alpha=True),
    "Shrub01": dict(asset="shrub_01", res=1024, alpha=True),
    "Shrub03": dict(asset="shrub_03", res=1024, alpha=True),
    "Shrub04": dict(asset="shrub_04", res=1024, alpha=True),
}
SLOT_OF_ASSET = {v["asset"]: k for k, v in SETS.items()}
BOULDER_RES = 1024        # BC / ORM / baked N per boulder (baked at 2x, downsampled)
BAKE_RES = 2048

BERMUDA = "grass_bermuda_01"
SPRIGS = {  # sprig family -> (weight, object suffixes)
    "medium": (0.44, "abcdef"), "small": (0.32, "abcdef"), "seedling": (0.07, "abcd"),
    "dead": (0.07, "ab"), "flattened": (0.05, "a"), "single": (0.05, "ab"),
}
CHEAP_SPRIG_TRIS = 20

GM = "grass_medium_02"
MESHES = [
    # --- grass: <= 1500 / 400 / 120 tris
    dict(name="SM_OW_GrassLawn_a", kind="grass", build="lawn", radius=0.30, seed=11, lods=[1480, 395, 118]),
    dict(name="SM_OW_GrassLawn_b", kind="grass", build="lawn", radius=0.27, seed=29, lods=[1480, 395, 118],
         flowering=True, parts=[dict(obj=(GM, f"{GM}_c"), at=(0.04, -0.03, 0.4), lods=[(0.65, 420), (0.45, 110), (0.25, 36)])]),
    dict(name="SM_OW_GrassTuft_a", kind="grass", build="parts", lods=[1480, 395, 118],
         parts=[dict(obj=(GM, f"{GM}_e"), at=(0, 0, 0.0), lods=[(1.0, 1040), (0.55, 275), (0.3, 82)]),
                dict(obj=(GM, f"{GM}_a"), at=(0.21, 0.09, 2.1), lods=[(1.0, 430), (0.55, 115), (0.3, 34)])]),
    dict(name="SM_OW_GrassTuft_b", kind="grass", build="parts", lods=[1480, 395, 118],
         parts=[dict(obj=(GM, f"{GM}_d"), at=(0, 0, 0.0), lods=[(0.8, 900), (0.45, 240), (0.25, 72)]),
                dict(obj=(GM, f"{GM}_b"), at=(-0.22, 0.12, 1.3), lods=[(0.8, 570), (0.45, 150), (0.25, 44)])]),
    # --- small plants: LOD0 <= 2000
    dict(name="SM_OW_Fern_a", kind="plant", lods=[(("fern_02", "fern_02_b"), 1950), ("@0", 620), ("@0", 210)]),
    dict(name="SM_OW_Fern_b", kind="plant", lods=[(("fern_02", "fern_02_c"), 1950), ("@0", 620), ("@0", 210)]),
    dict(name="SM_OW_Fern_c", kind="plant", lods=[(("fern_02", "fern_02_d"), None), ("@0", 300), ("@0", 120)]),
    dict(name="SM_OW_Flower_a", kind="plant", lods=[(("flower_empodium", "flower_empodium_a_LOD0"), None),
                                                     (("flower_empodium", "flower_empodium_a_LOD1"), None), ("@1", 150)]),
    dict(name="SM_OW_Flower_b", kind="plant", lods=[(("flower_empodium", "flower_empodium_c_LOD0"), None),
                                                     (("flower_empodium", "flower_empodium_c_LOD1"), None), ("@1", 150)]),
    dict(name="SM_OW_Dandelion_a", kind="plant", lods=[(("dandelion_01", "dandelion_01_a_LOD1"), None),
                                                        (("dandelion_01", "dandelion_01_a_LOD2"), None),
                                                        (("dandelion_01", "dandelion_01_a_LOD3"), None)]),
    dict(name="SM_OW_Dandelion_b", kind="plant", lods=[(("dandelion_01", "dandelion_01_b_LOD1"), None),
                                                        (("dandelion_01", "dandelion_01_b_LOD2"), None),
                                                        (("dandelion_01", "dandelion_01_b_LOD3"), None)]),
    # --- shrubs (Poly Haven's "shrubs" are 0.2-0.4 m ground bushes)
    dict(name="SM_OW_Shrub_a", kind="shrub", lods=[(("shrub_01", "shrub_01_a_LOD1"), None), (("shrub_01", "shrub_01_a_LOD2"), None),
                                                    (("shrub_01", "shrub_01_a_LOD3"), None)]),
    dict(name="SM_OW_Shrub_b", kind="shrub", lods=[(("shrub_01", "shrub_01_c_LOD1"), None), (("shrub_01", "shrub_01_c_LOD2"), None),
                                                    (("shrub_01", "shrub_01_c_LOD3"), None)]),
    dict(name="SM_OW_Shrub_c", kind="shrub", lods=[(("shrub_03", "shrub_03_a_LOD1"), None), ("@0", 700), ("@0", 300)]),
    dict(name="SM_OW_Shrub_d", kind="shrub", lods=[(("shrub_04", "shrub_04_c_LOD1"), 2000), ("@0", 800), ("@0", 320)]),
]
for _v, _a, _low in (("a", "namaqualand_boulder_02", "LOD3"), ("b", "namaqualand_boulder_03", "LOD3"),
                     ("c", "namaqualand_boulder_04", "LOD3"), ("d", "namaqualand_boulder_05", "LOD2"),
                     ("e", "namaqualand_boulder_06", "LOD3"), ("f", "boulder_01", "LOD3")):
    MESHES.append(dict(name=f"SM_OW_Boulder_{_v}", kind="boulder", asset=_a, high=(_a, f"{_a}_LOD0"),
                       lods=[((_a, f"{_a}_{_low}"), 3000), ("@0", 800), ("@0", 200)]))

KIND_ORDER = ["grass", "plant", "shrub", "boulder"]
KEEP_ATTRS = {"position", "material_index", "sharp_face", "custom_normal"}


# ----------------------------------------------------------------------------------------------------------- source meshes
def blend_of(asset):
    for d in (OW_MODELS, ENV_MODELS):
        p = os.path.join(d, asset, f"{asset}_2k.blend")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(f"{asset}_2k.blend (run Tools/openworld/fetch_models.py)")


def tex_dir(asset):
    return os.path.join(os.path.dirname(blend_of(asset)), "textures")


_SRC = {}


def preload(keys):
    """Append the source objects (per asset in one go), evaluate their modifiers, keep world-space mesh copies."""
    by_asset = {}
    for a, n in keys:
        if (a, n) not in _SRC:
            by_asset.setdefault(a, set()).add(n)
    sc = bpy.context.scene
    for asset, names in by_asset.items():
        with bpy.data.libraries.load(blend_of(asset), link=False) as (src, dst):
            missing = names - set(src.objects)
            if missing:
                raise RuntimeError(f"{asset}: objects not found {sorted(missing)}")
            dst.objects = sorted(names)
        obs = [o for o in dst.objects if o is not None]
        for ob in obs:
            sc.collection.objects.link(ob)
        dg = bpy.context.evaluated_depsgraph_get()
        dg.update()
        for name, ob in zip(sorted(names), obs):
            ev = ob.evaluated_get(dg)
            me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
            me.transform(ev.matrix_world)
            me.name = f"src:{name}"
            _SRC[(asset, name)] = me
        for ob in obs:
            bpy.data.objects.remove(ob)


def verts(me):
    co = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    return co.reshape(-1, 3)


def root_xy(me, boulder=False):
    co = verts(me)
    if boulder:
        return (co[:, :2].min(0) + co[:, :2].max(0)) * 0.5
    low = co[co[:, 2] < co[:, 2].min() + 0.02]
    return low[:, :2].mean(0)


def src_mesh(key, recentre=True):
    """Copy of a source mesh, by default moved so its root sits at the origin in xy (z keeps the authored ground at 0)."""
    me = _SRC[key].copy()
    if recentre:
        r = root_xy(me)
        me.transform(Matrix.Translation((-float(r[0]), -float(r[1]), 0.0)))
    return me


def new_ob(me, name, coll=None):
    ob = bpy.data.objects.new(name, me)
    (coll or bpy.context.scene.collection).objects.link(ob)
    return ob


def clean(me, boulder):
    while len(me.uv_layers) > 1:
        me.uv_layers.remove(me.uv_layers[-1])
    if len(me.uv_layers) == 0:
        raise RuntimeError(f"{me.name}: no UVs")
    uv = me.uv_layers[0].name
    for a in [a.name for a in me.attributes]:
        if a.startswith(".") or a in KEEP_ATTRS or a == uv or a in me.color_attributes:
            continue
        me.attributes.remove(me.attributes[a])
    if boulder:   # plain smooth normals: the baked tangent space must match what Unreal computes (MikkTSpace on vertex normals)
        for a in ("custom_normal", "sharp_face", "sharp_edge"):
            if a in me.attributes:
                me.attributes.remove(me.attributes[a])
        me.shade_smooth()


def decimate(me, target):
    """Collapse-decimate a mesh (in place) to <= target tris."""
    if target is None or PT.tris(me) <= target:
        return me
    ob = new_ob(me, "dec_tmp")
    ratio = target / PT.tris(me)
    for attempt in range(6):
        md = ob.modifiers.new("dec", "DECIMATE")
        md.ratio = ratio
        md.use_collapse_triangulate = True
        dg = bpy.context.evaluated_depsgraph_get()
        out = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
        ob.modifiers.remove(md)
        t = PT.tris(out)
        if t <= target or attempt == 5:
            break
        bpy.data.meshes.remove(out)
        ratio *= 0.97 * target / t
    bpy.data.objects.remove(ob)
    out.name = me.name
    return out


def island_labels(me):
    face_isl, ls, lt, lv = PT.islands(me)
    roots, inv = np.unique(face_isl, return_inverse=True)
    vert_isl = np.full(len(me.vertices), -1, np.int64)
    vert_isl[lv] = np.repeat(inv, lt)
    return roots, inv, vert_isl


def _hash01(x, salt=0):
    x = (np.asarray(x, np.uint64) + np.uint64(salt)) * np.uint64(2654435761)
    return ((x ^ (x >> np.uint64(13))) & np.uint64(0xFFFFFFFF)).astype(np.float64) / 4294967296.0


def thin_blades(me, keep, salt=0):
    """Drop whole islands (blades / fronds) at random (stable hash < keep survives), widen survivors about their base."""
    if keep >= 1.0:
        return me
    roots, inv, vert_isl = island_labels(me)
    h = _hash01(roots, salt)
    drop_isl = h >= keep
    co = verts(me)
    s = min(1.45, keep ** -0.3)
    keep_v = (vert_isl >= 0) & ~drop_isl[np.maximum(vert_isl, 0)]
    for i in np.nonzero(~drop_isl)[0]:
        m = vert_isl == i
        c = co[m]
        b = c[c[:, 2] <= c[:, 2].min() + 1e-3].mean(0)
        co[m, :2] = b[:2] + (c[:, :2] - b[:2]) * s
        co[m, 2] = b[2] + (c[:, 2] - b[2]) * (1 + (s - 1) * 0.3)
    me.vertices.foreach_set("co", co.ravel())
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.verts[i] for i in np.nonzero(~keep_v)[0]], context="VERTS")
    bm.to_mesh(me)
    bm.free()
    me.update()
    return me


def join(meshes_with_mats, name):
    """Join (mesh, 4x4) instances into one mesh (materials merged by the join operator)."""
    obs = []
    for me, mat in meshes_with_mats:
        ob = new_ob(me, f"{name}_part")
        ob.matrix_world = mat
        obs.append(ob)
    bpy.ops.object.select_all(action="DESELECT")
    for ob in obs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    me = ob.data
    me.transform(ob.matrix_world)
    ob.data = bpy.data.meshes.new("tmp")
    bpy.data.objects.remove(ob)
    me.name = name
    return me


def slot_materials(me):
    """Rename material slots to texture-set names; merge duplicate slots."""
    names = []
    for m in me.materials:
        src = (m.name if m else "").split(".")[0]
        names.append(SLOT_OF_ASSET.get(src, "Rock"))      # boulder scans -> one "Rock" slot
    uniq = list(dict.fromkeys(names))
    remap = np.array([uniq.index(n) for n in names] or [0], np.int64)
    mi = np.zeros(len(me.polygons), np.int64)
    me.polygons.foreach_get("material_index", mi)
    me.materials.clear()
    for n in uniq:
        me.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))
    me.polygons.foreach_set("material_index", remap[mi] if len(mi) else mi)
    return uniq


def wind_colors(me, height):
    roots, inv, vert_isl = island_labels(me)
    co = verts(me)
    col = np.zeros((len(me.vertices), 4), np.float32)
    col[:, 0] = np.clip(co[:, 2] / max(height, 1e-3), 0.0, 1.0)
    col[:, 1] = _hash01(roots, 7)[np.maximum(vert_isl, 0)]
    col[:, 3] = 1.0
    a = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
    a.data.foreach_set("color", col.ravel())


# ----------------------------------------------------------------------------------------------------------- builders
def sprig_lib():
    lib = {}
    for fam, (_, sfx) in SPRIGS.items():
        for s in sfx:
            key = (BERMUDA, f"{BERMUDA}_{fam}_{s}")
            me = _SRC[key]
            co = verts(me)
            lib.setdefault(fam, []).append(dict(key=key, tris=PT.tris(me),
                                                area=float(np.prod(co[:, :2].max(0) - co[:, :2].min(0)))))
    return lib


def lawn_layout(spec, lib):
    rng = np.random.default_rng(spec["seed"])
    R = spec["radius"]
    pts = np.zeros((0, 2))
    for _ in range(6000):
        r = R * math.sqrt(rng.random())
        a = rng.random() * 2 * math.pi
        p = np.array([r * math.cos(a), r * math.sin(a)])
        if len(pts) == 0 or np.hypot(*(pts - p).T).min() > 0.02:
            pts = np.vstack([pts, p])
        if len(pts) >= 500:
            break
    fams = list(SPRIGS)
    w = np.array([SPRIGS[f][0] for f in fams])
    if spec.get("flowering"):
        w[fams.index("seedling")] *= 2.5
    w /= w.sum()
    items = []
    for p in pts:
        fam = fams[rng.choice(len(fams), p=w)]
        el = lib[fam][rng.integers(len(lib[fam]))]
        edge = 1.0 - 0.4 * (np.hypot(*p) / R) ** 2
        items.append(dict(el=el, xy=p, rot=rng.random() * 2 * math.pi, tilt=(rng.normal(0, 0.08), rng.normal(0, 0.08)),
                          scale=float(rng.uniform(1.5, 2.2) * edge)))
    order = rng.permutation(len(items))
    return [items[i] for i in order]


def build_lawn(spec, lod, budget, layout, part_meshes):
    used = sum(PT.tris(m) for m in part_meshes)
    chosen = []
    for it in layout:
        if lod > 0 and it["el"]["tris"] > CHEAP_SPRIG_TRIS:
            continue
        if used + it["el"]["tris"] > budget:
            continue
        chosen.append(it)
        used += it["el"]["tris"]
    if lod == 0:
        spec["_cov0"] = sum(it["el"]["area"] * it["scale"] ** 2 for it in chosen)
    cov = sum(it["el"]["area"] * it["scale"] ** 2 for it in chosen)
    widen = 1.0 if lod == 0 else min(2.5, math.sqrt(spec["_cov0"] / max(cov, 1e-6)))
    inst = []
    for it in chosen:
        s = it["scale"]
        m = (Matrix.Translation((float(it["xy"][0]), float(it["xy"][1]), 0.0)) @ Matrix.Rotation(it["rot"], 4, "Z")
             @ Matrix.Rotation(it["tilt"][0], 4, "X") @ Matrix.Rotation(it["tilt"][1], 4, "Y")
             @ Matrix.Diagonal((s * widen, s * widen, s * (1 + (widen - 1) * 0.25), 1.0)))
        inst.append((src_mesh(it["el"]["key"]), m))
    inst += [(pm, Matrix.Identity(4)) for pm in part_meshes]
    return join(inst, f"{spec['name']}_LOD{lod}")


def part_meshes(spec, lod):
    out = []
    for pi, p in enumerate(spec.get("parts", [])):
        keep, target = p["lods"][lod]
        me = src_mesh(p["obj"])
        me = thin_blades(me, keep, salt=pi * 101)
        me = decimate(me, target)
        x, y, rz = p["at"]
        me.transform(Matrix.Translation((x, y, 0.0)) @ Matrix.Rotation(rz, 4, "Z"))
        out.append(me)
    return out


def build_lods(spec, lib):
    """-> list of LOD meshes (world space, root near the origin)."""
    lods = []
    if spec["kind"] == "grass":
        layout = lawn_layout(spec, lib) if spec["build"] == "lawn" else None
        for li, budget in enumerate(spec["lods"]):
            parts = part_meshes(spec, li)
            if layout is not None:
                me = build_lawn(spec, li, budget, layout, parts)
            else:
                me = join([(m, Matrix.Identity(4)) for m in parts], f"{spec['name']}_LOD{li}")
            lods.append(me)
        return lods
    boulder = spec["kind"] == "boulder"
    for li, (src, target) in enumerate(spec["lods"]):
        if isinstance(src, str) and src.startswith("@"):
            me = lods[int(src[1:])].copy()
        else:
            me = src_mesh(src, recentre=not boulder)   # boulders: recentred once, from LOD0, in main()
        me = decimate(me, target)
        me.name = f"{spec['name']}_LOD{li}"
        lods.append(me)
    return lods


# ----------------------------------------------------------------------------------------------------------- textures
def _img(path):
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"      # raw values for 8- and 16-bit files alike
    return img


def _pixels(path, res):
    img = _img(path)
    if tuple(img.size) != (res, res):
        img.scale(res, res)
    a = np.empty(res * res * 4, np.float32)
    img.pixels.foreach_get(a)
    bpy.data.images.remove(img)
    return a.reshape(res, res, 4)


def _save(arr, path, alpha):
    h, w, _ = arr.shape
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=alpha)
    img.colorspace_settings.name = "Non-Color"
    if not alpha:
        arr = arr.copy()
        arr[..., 3] = 1.0
    img.pixels.foreach_set(np.ascontiguousarray(arr, np.float32).ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return os.path.basename(path)


_TEX = {}


def texture_set(slot):
    if slot in _TEX:
        return _TEX[slot]
    s = SETS[slot]
    a, res, d = s["asset"], s["res"], tex_dir(s["asset"])
    base = os.path.join(OUT, f"T_OW_Veg_{slot}")
    alpha = _pixels(os.path.join(d, f"{a}_alpha_2k.png"), res)[..., 0] if s["alpha"] else None
    out = {}
    for tag, diff in [("BC", f"{a}_diff_2k.jpg")] + [(f"BC_{k}", v) for k, v in s.get("alt", {}).items()]:
        bc = _pixels(os.path.join(d, diff), res)
        if alpha is not None:
            bc[..., 3] = alpha
        out[tag] = _save(bc, f"{base}_{tag}.png", alpha is not None)
    out["N"] = _save(_pixels(os.path.join(d, f"{a}_nor_dx_2k.png"), res), f"{base}_N.png", False)
    out["ORM"] = _save(_pixels(os.path.join(d, f"{a}_arm_2k.png"), res), f"{base}_ORM.png", False)
    _TEX[slot] = out
    print(f"[prep_vegetation] textures {slot}: {out}")
    return out


def bake_boulder_normal(spec, low_me, path):
    """Tangent-space (DirectX) normal map of the full scan (LOD0 + its normal map) on the game LOD0's UVs."""
    a = spec["asset"]
    d = tex_dir(a)
    sc = bpy.context.scene
    high = new_ob(src_mesh(spec["high"], recentre=False), "bake_high")
    high.data.transform(Matrix.Translation((-spec["_pivot"][0], -spec["_pivot"][1], 0.0)))   # same shift as the game LODs
    clean(high.data, boulder=False)
    hm = bpy.data.materials.new("bake_high")
    hm.use_nodes = True
    nt = hm.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tn = nt.nodes.new("ShaderNodeTexImage")
    tn.image = bpy.data.images.load(os.path.join(d, f"{a}_nor_gl_2k.exr"), check_existing=True)
    tn.image.colorspace_settings.name = "Non-Color"
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    high.data.materials.clear()
    high.data.materials.append(hm)
    low = new_ob(low_me.copy(), "bake_low")
    img = bpy.data.images.new("bake_n", BAKE_RES, BAKE_RES, alpha=False)
    img.colorspace_settings.name = "Non-Color"
    lm = bpy.data.materials.new("bake_low")
    lm.use_nodes = True
    it = lm.node_tree.nodes.new("ShaderNodeTexImage")
    it.image = img
    lm.node_tree.nodes.active = it
    low.data.materials.clear()
    low.data.materials.append(lm)
    dims = np.ptp(verts(low.data), axis=0)
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 4
    bk = sc.render.bake
    bk.use_selected_to_active = True
    bk.cage_extrusion = float(dims.max()) * 0.02
    bk.max_ray_distance = float(dims.max()) * 0.06
    bk.margin = 16
    bk.normal_space = "TANGENT"
    bk.normal_r, bk.normal_g, bk.normal_b = "POS_X", "NEG_Y", "POS_Z"
    bpy.ops.object.select_all(action="DESELECT")
    high.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    bpy.ops.object.bake(type="NORMAL")
    img.scale(BOULDER_RES, BOULDER_RES)
    arr = np.empty(BOULDER_RES * BOULDER_RES * 4, np.float32)
    img.pixels.foreach_get(arr)
    out = _save(arr.reshape(BOULDER_RES, BOULDER_RES, 4), path, False)
    for ob in (high, low):
        bpy.data.objects.remove(ob)
    bpy.data.images.remove(img)
    return out


def boulder_textures(spec, lod0, bake):
    a, d = spec["asset"], tex_dir(spec["asset"])
    v = spec["name"].rsplit("_", 1)[1]
    base = os.path.join(OUT, f"T_OW_Boulder_{v}")
    out = {"BC": _save(_pixels(os.path.join(d, f"{a}_diff_2k.jpg"), BOULDER_RES), f"{base}_BC.png", False)}
    if bake:
        out["N"] = bake_boulder_normal(spec, lod0, f"{base}_N.png")
        out["N_baked"] = True
    else:
        out["N"] = _save(_pixels(os.path.join(d, f"{a}_nor_dx_2k.png"), BOULDER_RES), f"{base}_N.png", False)
    out["ORM"] = _save(_pixels(os.path.join(d, f"{a}_arm_2k.png"), BOULDER_RES), f"{base}_ORM.png", False)
    return out


# ----------------------------------------------------------------------------------------------------------- sheet
def preview_material(name, tex, alpha):
    m = bpy.data.materials.get(f"pv_{name}")
    if m:
        return m
    m = bpy.data.materials.new(f"pv_{name}")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]

    def tnode(file, color):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(os.path.join(OUT, file), check_existing=True)
        n.image.colorspace_settings.name = "sRGB" if color else "Non-Color"
        n.image.alpha_mode = "STRAIGHT"
        return n
    bc = tnode(tex["BC"], True)
    nt.links.new(bc.outputs["Color"], bsdf.inputs["Base Color"])
    if alpha:
        nt.links.new(bc.outputs["Alpha"], bsdf.inputs["Alpha"])
    orm = tnode(tex["ORM"], False)
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(orm.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    n = tnode(tex["N"], False)       # DirectX -> OpenGL: flip green
    sp = nt.nodes.new("ShaderNodeSeparateColor")
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    cb = nt.nodes.new("ShaderNodeCombineColor")
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(n.outputs["Color"], sp.inputs["Color"])
    nt.links.new(sp.outputs["Red"], cb.inputs["Red"])
    nt.links.new(sp.outputs["Green"], inv.inputs[1])
    nt.links.new(inv.outputs[0], cb.inputs["Green"])
    nt.links.new(sp.outputs["Blue"], cb.inputs["Blue"])
    nt.links.new(cb.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def render_sheet(entries, cells):
    sc = bpy.context.scene
    for ob in list(sc.collection.objects):
        bpy.data.objects.remove(ob)
    sc.render.engine = "BLENDER_EEVEE"
    sc.eevee.taa_render_samples = 32
    W, H = 900, 380
    sc.render.resolution_x, sc.render.resolution_y = W, H
    sc.render.film_transparent = False
    sc.view_settings.view_transform = "AgX"
    sc.world = bpy.data.worlds.new("sheet")
    sc.world.use_nodes = True
    bg = sc.world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.55, 0.62, 0.72, 1)
    bg.inputs[1].default_value = 0.8
    sun = new_ob(bpy.data.lights.new("sun", "SUN"), "sun")
    sun.data.energy = 3.5
    sun.data.angle = math.radians(3)
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    cam = new_ob(bpy.data.cameras.new("cam"), "cam")
    cam.data.type = "ORTHO"
    sc.camera = cam
    ground = new_ob(bpy.data.meshes.new("ground"), "ground")
    ground.data.from_pydata([(-50, -50, 0), (50, -50, 0), (50, 50, 0), (-50, 50, 0)], [], [(0, 1, 2, 3)])
    gm = bpy.data.materials.new("ground")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.20, 0.19, 0.15, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
    ground.data.materials.append(gm)
    txt_mat = bpy.data.materials.new("txt")
    txt_mat.use_nodes = True
    em = txt_mat.node_tree.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1, 1, 1, 1)
    txt_mat.node_tree.links.new(em.outputs[0], txt_mat.node_tree.nodes["Material Output"].inputs["Surface"])
    tiles = []
    tmp = os.path.join(bpy.app.tempdir, "vegsheet")
    os.makedirs(tmp, exist_ok=True)
    for name, e in entries:
        lods = cells[name]
        dims = np.ptp(verts(lods[0]), axis=0)
        span = max(float(dims[0]), float(dims[1]))
        gap = span * 1.15
        obs = []
        for li, me in enumerate(lods):
            pm = me.copy()
            for i, m in enumerate(pm.materials):
                slot = m.name.split(".")[0]
                tex = e["slots"][slot]
                pm.materials[i] = preview_material(f"{name}_{slot}" if e["kind"] == "boulder" else slot, tex, e["kind"] != "boulder")
            ob = new_ob(pm, f"pv_{name}_{li}")
            ob.location = ((li - 1) * gap, 0, 0)
            obs.append(ob)
        tilt = math.radians(68)
        total_w = gap * 2 + span
        ortho = max(total_w * 1.08, float(dims[2]) * 1.6 * W / H)
        cam.data.ortho_scale = ortho
        mid_z = float(dims[2]) * 0.5
        dirv = Vector((0, math.sin(tilt), -math.cos(tilt)))
        target = Vector((0, 0, mid_z - ortho * H / W * 0.08))
        cam.location = target - dirv * 20
        cam.rotation_euler = (tilt, 0, 0)
        cam.data.clip_end = 100
        cu = bpy.data.curves.new("lbl", "FONT")
        t = e["lods"]
        sz = e["size_m"]
        cu.body = (f"{name}\n{sz[0]:.2f} x {sz[1]:.2f} x {sz[2]:.2f} m   "
                   f"tris {' / '.join(str(l['tris']) for l in t)}")
        cu.size = ortho * 0.034
        txt = new_ob(cu, "lbl")
        txt.data.materials.append(txt_mat)
        txt.parent = cam
        txt.location = (-ortho * 0.48, -ortho * H / W * 0.5 + ortho * 0.075, -5)
        sc.render.filepath = os.path.join(tmp, f"{name}.png")
        bpy.ops.render.render(write_still=True)
        tiles.append(sc.render.filepath)
        for ob in obs + [txt]:
            bpy.data.objects.remove(ob)
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows * H, cols * W, 4), np.float32)
    sheet[..., 3] = 1
    for i, p in enumerate(tiles):
        img = bpy.data.images.load(p)
        a = np.empty(W * H * 4, np.float32)
        img.pixels.foreach_get(a)
        bpy.data.images.remove(img)
        r, c = divmod(i, cols)
        y0 = (rows - 1 - r) * H
        sheet[y0:y0 + H, c * W:(c + 1) * W] = a.reshape(H, W, 4)
    os.makedirs(os.path.dirname(SHEET), exist_ok=True)
    img = bpy.data.images.new("sheet", cols * W, rows * H, alpha=False)
    img.pixels.foreach_set(sheet.ravel())
    img.filepath_raw = SHEET
    img.file_format = "PNG"
    img.save()
    print("[prep_vegetation] wrote", SHEET)


# ----------------------------------------------------------------------------------------------------------- main
def source_info():
    info = {}
    for d in (OW_MODELS, ENV_MODELS):
        p = os.path.join(d, "models.json")
        if os.path.exists(p):
            for k, v in json.load(open(p))["models"].items():
                info.setdefault(k, v)
    return info


def needed_keys(spec):
    keys = []
    if spec["kind"] == "grass":
        if spec["build"] == "lawn":
            keys += [(BERMUDA, f"{BERMUDA}_{f}_{s}") for f, (_, sfx) in SPRIGS.items() for s in sfx]
        keys += [p["obj"] for p in spec.get("parts", [])]
    else:
        keys += [s for s, _ in spec["lods"] if isinstance(s, tuple)]
    if "high" in spec:
        keys.append(spec["high"])
    return keys


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else set()
    bake = "--no-bake" not in argv
    sheet = "--no-sheet" not in argv
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sinfo = source_info()
    man_path = os.path.join(OUT, "vegetation.json")
    manifest = json.load(open(man_path)) if os.path.exists(man_path) else {}
    manifest.update({
        "license": "CC0 1.0 (Poly Haven, polyhaven.com; no attribution required, authors listed per mesh)",
        "generator": "Tools/openworld/prep_vegetation.py (sources: Tools/openworld/fetch_models.py)",
        "units": "metres; Blender Z-up exported with prep_trees.py's FBX settings; pivot xy = root / bbox centre, z 0 = ground",
        "lod_files": "one FBX per LOD (SM_OW_<Name>_LOD<n>.fbx); import LOD0 and add LOD1+ like foliage.py does for trees",
        "vertex_color": "foliage only: R = height fraction (wind bend weight), G = random phase per sprig / blade / frond",
        "textures": "BC sRGB (foliage: alpha = opacity mask), N DirectX tangent space, ORM = AO / roughness / metallic",
    })
    manifest.setdefault("meshes", {})
    specs = [s for s in MESHES if not only or s["name"] in only]
    keys = list(dict.fromkeys(k for s in specs for k in needed_keys(s)))
    preload(keys)
    lib = sprig_lib() if any(s.get("build") == "lawn" for s in specs) else None
    cells = {}
    for spec in specs:
        name, kind = spec["name"], spec["kind"]
        boulder = kind == "boulder"
        lods = build_lods(spec, lib)
        for me in lods:
            clean(me, boulder)
        # pivot from LOD0, applied to every LOD
        r = root_xy(lods[0], boulder)
        spec["_pivot"] = (float(r[0]), float(r[1]))
        for me in lods:
            me.transform(Matrix.Translation((-float(r[0]), -float(r[1]), 0.0)))
        co0 = verts(lods[0])
        mn, mx = co0.min(0), co0.max(0)
        height = float(mx[2])
        slots = None
        entry_lods = []
        for li, me in enumerate(lods):
            names = slot_materials(me)
            slots = slots or names
            if not boulder:
                wind_colors(me, height)
            ob = new_ob(me, f"{name}_LOD{li}")
            fbx = os.path.join(OUT, f"{name}_LOD{li}.fbx")
            PT.export(ob, fbx)
            entry_lods.append(dict(fbx=os.path.basename(fbx), tris=PT.tris(me),
                                   size_mb=round(os.path.getsize(fbx) / 1e6, 2)))
            bpy.data.objects.remove(ob)
        if boulder:
            tex = {"Rock": boulder_textures(spec, lods[0], bake)}
        else:
            tex = {s: texture_set(s) for s in slots}
        assets = sorted({k[0] for k in needed_keys(spec)})
        entry = dict(kind=kind, lods=entry_lods, size_m=[round(float(v), 3) for v in (mx - mn)],
                     below_ground_m=round(max(0.0, -float(mn[2])), 3), slots=tex,
                     masked=not boulder, two_sided=not boulder,
                     source=[dict(id=a, license="CC0", url=f"https://polyhaven.com/a/{a}",
                                  authors=sinfo.get(a, {}).get("authors", [])) for a in assets])
        manifest["meshes"][name] = entry
        cells[name] = lods
        print(f"[prep_vegetation] {name}: tris {[l['tris'] for l in entry_lods]} size {entry['size_m']} slots {list(tex)}")
    manifest["meshes"] = dict(sorted(manifest["meshes"].items(),
                                     key=lambda kv: (KIND_ORDER.index(kv[1]["kind"]), kv[0])))
    with open(man_path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("[prep_vegetation] wrote", man_path)
    if sheet:
        order = [(n, e) for n, e in manifest["meshes"].items() if n in cells]
        render_sheet(order, cells)


if __name__ == "__main__":
    main()
