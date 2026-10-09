"""Poly Haven scanned trees -> game meshes for Unreal: one FBX per tree variant and LOD + a manifest (Blender 5.0, background).

  /Applications/Blender.app/Contents/MacOS/Blender -b --disable-autoexec --factory-startup \
      --python unreal/Tools/blender/world/prep_trees.py [-- --only Fir,Broadleaf]

Input: SourceArt/Environment/PolyHaven/Models/<asset>/<asset>_2k.blend (Tools/world/fetch_polyhaven_models.py; CC0).
Output: SourceArt/Environment/PolyHaven/Trees/SM_Tree_<Species>_<v>_LOD<n>.fbx and trees.json (both git-ignored, rebuildable),
read by the Unreal foliage builder (Content/Python/fourfold/world/foliage.py).

Per game LOD the script takes one of the asset's own LOD meshes and optionally thins it:
  * keep < 1: removes small foliage islands (leaf / twig cards incl. the twigs attached to them) at random (stable hash: a
    coarser LOD keeps a subset of the finer one) and scales the kept ones up by keep^-0.5 (max 1.8) around their own centre,
    so the crown keeps its silhouette and coverage with far fewer cards;
  * bark < 1: collapse-decimates the bark / trunk faces only.
UVs: the scans store their UVs as a geometry-nodes corner attribute (FLOAT_VECTOR "UVMap"), which the FBX exporter skips;
it is converted into a real UV layer. Each mesh gets its origin at the trunk base (bottom centre) and keeps real size (m).
Material slots are renamed to stable slot names (Bark, Trunk, Branch, DeadBranch, Leaf); the manifest maps every slot to its
Poly Haven textures (BC = diff, N = nor_dx, ORM = arm, OP = alpha).
"""
import json
import math
import os
import re
import sys

import bmesh
import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PH = os.path.normpath(os.path.join(HERE, "..", "..", "..", "SourceArt", "Environment", "PolyHaven"))
MODELS = os.path.join(PH, "Models")
OUT = os.path.join(PH, "Trees")

# game LOD -> (source LOD object suffix, thinning options or None)
SPECIES = {
    "Fir": [dict(asset="fir_tree_01", variants=["a", "b", "c"],
                 lods=[("LOD1", None), ("LOD2", None), ("LOD2", dict(keep=0.35, bark=0.45))])],
    "FirSlim": [dict(asset="fir_sapling_medium", variants=["a", "b", "c"],
                     lods=[("LOD1", None), ("LOD2", None), ("LOD2", dict(keep=0.40, bark=0.55))])],
    "Broadleaf": [dict(asset="tree_small_02", variants=[""],
                       lods=[("LOD1", dict(keep=0.40)), ("LOD1", dict(keep=0.15, bark=0.45)), ("LOD1", dict(keep=0.05, bark=0.15))]),
                  dict(asset="island_tree_02", variants=[""],
                       lods=[("LOD1", dict(keep=0.45)), ("LOD1", dict(keep=0.17, bark=0.45)), ("LOD1", dict(keep=0.06, bark=0.15))])],
}
LEAF_WORDS = ("twig", "leaves", "leaf", "needle")
ISLAND_MAX_FACES = 4000          # foliage islands larger than this (trunk + main branches) are never removed or scaled
FBX_KW = dict(use_selection=True, apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", global_scale=1.0,
              axis_forward="-Z", axis_up="Y", bake_space_transform=False, object_types={"MESH"}, use_mesh_modifiers=True,
              mesh_smooth_type="OFF", use_custom_props=False, add_leaf_bones=False, path_mode="STRIP", embed_textures=False,
              bake_anim=False, colors_type="LINEAR", use_triangles=False)


def slot_of(mat_name):
    n = mat_name.lower()
    if any(w in n for w in LEAF_WORDS):
        return "Leaf"
    if "dead" in n:
        return "DeadBranch"
    if "trunk" in n:
        return "Trunk"
    if "branch" in n:
        return "Branch"
    return "Bark"


def textures_of(mat, tex_dir):
    """Slot textures from the material's image nodes: the diff image names the map set (<set>_diff_2k.*)."""
    sets = []
    if mat is not None and mat.use_nodes:
        for nd in mat.node_tree.nodes:
            if nd.type == "TEX_IMAGE" and nd.image is not None:
                f = os.path.basename(bpy.path.abspath(nd.image.filepath) or nd.image.name)
                m = re.match(r"(.+?)_diff(_\dk)?\.\w+$", f)
                if m:
                    sets.append(m.group(1))
    if not sets:
        return {}
    # a material can sample several sets (the trunk blends bark into its own scan): prefer the set named like the material
    s = next((x for x in sets if x == mat.name), next((x for x in sets if mat.name.endswith(x) or x.endswith(mat.name)), sets[0]))
    out = {}
    for key, names in (("BC", [f"{s}_diff_2k.png", f"{s}_diff_2k.jpg"]), ("N", [f"{s}_nor_dx_2k.png"]),
                       ("ORM", [f"{s}_arm_2k.png"]), ("OP", [f"{s}_alpha_2k.png"])):
        for nm in names:
            p = os.path.join(tex_dir, nm)
            if os.path.exists(p):
                out[key] = os.path.relpath(p, PH)
                break
    return out


def uv_from_attribute(me):
    """Turn the geometry-nodes FLOAT_VECTOR corner attribute 'UVMap' into a real UV layer; keep only the first UV layer."""
    a = me.attributes.get("UVMap")
    if a is not None and a.domain == "CORNER" and a.data_type == "FLOAT_VECTOR":
        v = np.empty(len(me.loops) * 3, np.float32)
        a.data.foreach_get("vector", v)
        me.attributes.remove(a)
        uv = me.uv_layers.new(name="UVMap")
        uv.data.foreach_set("uv", v.reshape(-1, 3)[:, :2].ravel())
    while len(me.uv_layers) > 1:
        me.uv_layers.remove(me.uv_layers[-1])
    if len(me.uv_layers) == 0:
        raise RuntimeError(f"{me.name}: no UVs")


def islands(me):
    """Connected components over all faces: face -> island root (vertex index), via vectorised label propagation."""
    nv = len(me.vertices)
    ls = np.empty(len(me.polygons), np.int64)
    lt = np.empty(len(me.polygons), np.int64)
    me.polygons.foreach_get("loop_start", ls)
    me.polygons.foreach_get("loop_total", lt)
    lv = np.empty(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", lv)
    first = lv[ls]
    a = np.repeat(first, lt)
    b = lv[_loop_ranges(ls, lt)]
    lab = np.arange(nv, dtype=np.int64)
    for _ in range(10000):
        m = np.minimum(lab[a], lab[b])
        old = lab.copy()
        np.minimum.at(lab, a, m)
        np.minimum.at(lab, b, m)
        lab = lab[lab]
        if np.array_equal(lab, old):
            break
    return lab[first], ls, lt, lv


def _loop_ranges(ls, lt):
    # concatenated loop indices of every face, without a Python loop
    idx = np.arange(lt.sum(), dtype=np.int64)
    starts = np.repeat(ls - np.concatenate([[0], np.cumsum(lt)[:-1]]), lt)
    return idx + starts


def thin(ob, keep, leaf_idx):
    """Drop foliage islands (stable hash < keep survives) and scale the survivors up around their centre."""
    me = ob.data
    face_isl, ls, lt, lv = islands(me)
    mat = np.empty(len(me.polygons), np.int64)
    me.polygons.foreach_get("material_index", mat)
    is_leaf = np.isin(mat, list(leaf_idx))
    roots, inv, counts = np.unique(face_isl, return_inverse=True, return_counts=True)
    leafy = np.bincount(inv, weights=is_leaf.astype(float), minlength=len(roots)) > 0
    small = counts <= ISLAND_MAX_FACES
    cand = leafy & small
    h = ((roots.astype(np.uint64) * np.uint64(2654435761)) & np.uint64(0xFFFFFFFF)).astype(np.float64) / 4294967296.0
    drop_isl = cand & (h >= keep)
    scale_isl = cand & ~drop_isl
    # scale survivors (vertex positions about their island centroid)
    co = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    vert_isl = np.full(len(me.vertices), -1, np.int64)
    vert_isl[lv] = np.repeat(inv, lt)
    s = min(1.8, 1.0 / math.sqrt(max(keep, 1e-3)))
    sel = scale_isl[vert_isl] & (vert_isl >= 0)
    if sel.any():
        cen = np.zeros((len(roots), 3))
        cnt = np.zeros(len(roots))
        np.add.at(cen, vert_isl[sel], co[sel])
        np.add.at(cnt, vert_isl[sel], 1)
        cen /= np.maximum(cnt, 1)[:, None]
        co[sel] = cen[vert_isl[sel]] + (co[sel] - cen[vert_isl[sel]]) * s
        me.vertices.foreach_set("co", co.ravel())
    # delete dropped faces + loose vertices
    drop_face = drop_isl[inv]
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    dead = [bm.faces[i] for i in np.nonzero(drop_face)[0]]
    bmesh.ops.delete(bm, geom=dead, context="FACES_ONLY")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(me)
    bm.free()
    return int(drop_isl.sum()), int(cand.sum())


def decimate_bark(ob, ratio, leaf_idx):
    me = ob.data
    vg = ob.vertex_groups.new(name="bark")
    bark_verts = set()
    for p in me.polygons:
        if p.material_index not in leaf_idx:
            bark_verts.update(p.vertices)
    vg.add(list(bark_verts), 1.0, "REPLACE")
    md = ob.modifiers.new("dec", "DECIMATE")
    md.decimate_type = "COLLAPSE"
    md.ratio = ratio
    md.vertex_group = "bark"
    md.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=md.name)
    g = ob.vertex_groups.get("bark")          # the Python handle goes stale when the modifier rebuilds the mesh
    if g is not None:
        ob.vertex_groups.remove(g)


def tris(me):
    lt = np.empty(len(me.polygons), np.int64)
    me.polygons.foreach_get("loop_total", lt)
    return int((lt - 2).sum())


def make_game_mesh(src, name, opts, tex_dir):
    me = src.data.copy()
    me.name = name
    me.transform(src.matrix_world)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    uv_from_attribute(me)
    # stable slot names; drop empty slots; textures per slot
    slots, leaf_idx = {}, set()
    for i, m in enumerate(me.materials):
        if m is None:
            continue
        sl = slot_of(m.name)
        slots.setdefault(sl, textures_of(m, tex_dir))
        if sl == "Leaf":
            leaf_idx.add(i)
    info = {}
    if opts and opts.get("keep", 1.0) < 1.0:
        d, c = thin(ob, opts["keep"], leaf_idx)
        info["islands_dropped"] = [d, c]
    if opts and opts.get("bark", 1.0) < 1.0:
        decimate_bark(ob, opts["bark"], leaf_idx)
    # rename materials to the slot names (one material per slot name per species, so LODs share sections)
    for i, m in enumerate(me.materials):
        if m is None:
            continue
        sl = slot_of(m.name)
        mat = bpy.data.materials.get(sl) or bpy.data.materials.new(sl)
        me.materials[i] = mat
    # origin at the trunk base: centre of the lowest 0.4 m of vertices, bottom at z = 0
    co = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    zmin = float(co[:, 2].min())
    low = co[co[:, 2] < zmin + 0.4]
    base = np.array([low[:, 0].mean(), low[:, 1].mean(), zmin])
    co -= base
    me.vertices.foreach_set("co", co.ravel())
    me.update()
    info.update(tris=tris(me), height_m=round(float(co[:, 2].max()), 3),
                radius_m=round(float(np.sqrt((co[:, :2] ** 2).sum(1)).max()), 3))
    return ob, slots, info


def export(ob, path):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=path, **FBX_KW)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else set()
    os.makedirs(OUT, exist_ok=True)
    man_path = os.path.join(OUT, "trees.json")
    manifest = json.load(open(man_path)) if os.path.exists(man_path) else {"license": "CC0 (polyhaven.com)", "meshes": {}}
    for species, sources in SPECIES.items():
        if only and species not in only:
            continue
        n = 0
        for src_def in sources:
            asset = src_def["asset"]
            blend = os.path.join(MODELS, asset, f"{asset}_2k.blend")
            bpy.ops.wm.open_mainfile(filepath=blend, load_ui=False)
            tex_dir = os.path.join(MODELS, asset, "textures")
            for v in src_def["variants"]:
                mesh_name = f"SM_Tree_{species}_{chr(ord('a') + n)}"
                n += 1
                entry = dict(species=species, source=asset, variant=v, lods=[])
                for li, (src_lod, opts) in enumerate(src_def["lods"]):
                    obj_name = f"{asset}_{v}_{src_lod}" if v else f"{asset}_{src_lod}"
                    src = bpy.data.objects.get(obj_name)
                    if src is None:
                        raise RuntimeError(f"{blend}: object {obj_name} not found")
                    ob, slots, info = make_game_mesh(src, f"{mesh_name}_LOD{li}", opts, tex_dir)
                    fbx = os.path.join(OUT, f"{mesh_name}_LOD{li}.fbx")
                    export(ob, fbx)
                    entry["lods"].append(dict(fbx=os.path.relpath(fbx, PH), source=obj_name, **info))
                    entry.setdefault("slots", {}).update({k: t for k, t in slots.items() if k not in entry.get("slots", {})})
                    bpy.data.objects.remove(ob)
                    print(f"[prep_trees] {mesh_name} LOD{li} <- {obj_name}: {info}")
                entry["height_m"] = entry["lods"][0]["height_m"]
                manifest["meshes"][mesh_name] = entry
    with open(man_path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("[prep_trees] wrote", man_path)


main()
