"""Validator for the shipped fighter (reads the FBX files exactly as Unreal will get them).

    /home/user/tools/bpyenv/bin/python unreal/Tools/blender/character/validate_character.py [art_dir] [--json out.json]

Checks: armature = frozen rig spec (names / parents / object 'root'), every LOD: material slots, triangle budget,
<= 4 influences, normalised weights, no unweighted vertices, no NaNs, only rig bones as groups, ff_* chains skinned,
left / right weight symmetry on mirrored vertices, UVs inside 0-1 and not overlapping per material, textures present
with the right sizes. Exit code 1 on any failure."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "common"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import ff_rig_spec as spec  # noqa: E402

SLOTS = ["skin", "hair", "eyes", "cloth_main", "cloth_accent", "wraps", "sash", "shoes"]
BUDGET = {0: 30000, 1: 15000, 2: 6000}
TEX = {"skin": 2048, "cloth_main": 2048, "cloth_accent": 1024, "hair": 1024, "eyes": 1024, "wraps": 1024,
       "sash": 1024, "shoes": 1024}
# chains that must move something (runtime springs)
CHAINS = ["ff_hair_01", "ff_hair_02", "ff_hair_03", "ff_sash_l_01", "ff_sash_l_02", "ff_sash_r_01", "ff_sash_r_02",
          "ff_hem_fl_01", "ff_hem_fl_02", "ff_hem_fr_01", "ff_hem_fr_02", "ff_hem_bl_01", "ff_hem_bl_02",
          "ff_hem_br_01", "ff_hem_br_02"]


def import_fbx(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False)
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    return arms, meshes


def uv_overlap_fraction(me, slot_index, res=512):
    """Fraction of covered texels hit by more than one triangle of the given material (rasterised at res)."""
    uvl = me.uv_layers[0].data
    cover = np.zeros((res, res), dtype=np.int32)
    me.calc_loop_triangles()
    for lt in me.loop_triangles:
        if lt.material_index != slot_index:
            continue
        uv = np.array([uvl[li].uv for li in lt.loops]) * res
        area = 0.5 * ((uv[1, 0] - uv[0, 0]) * (uv[2, 1] - uv[0, 1]) - (uv[2, 0] - uv[0, 0]) * (uv[1, 1] - uv[0, 1]))
        if abs(area) < 0.25:          # sub-texel (degenerate lips, tiny slivers)
            continue
        lo = np.floor(uv.min(axis=0)).astype(int)
        hi = np.ceil(uv.max(axis=0)).astype(int)
        lo = np.clip(lo, 0, res - 1)
        hi = np.clip(hi, 0, res)
        xs, ys = np.meshgrid(np.arange(lo[0], hi[0]) + 0.5, np.arange(lo[1], hi[1]) + 0.5)
        if xs.size == 0:
            continue
        p = np.stack([xs.ravel(), ys.ravel()], axis=1)
        a, b, c = uv
        def edge(p0, p1, q):
            return (p1[0] - p0[0]) * (q[:, 1] - p0[1]) - (p1[1] - p0[1]) * (q[:, 0] - p0[0])
        w0, w1, w2 = edge(b, c, p), edge(c, a, p), edge(a, b, p)
        inside = ((w0 >= 0) & (w1 >= 0) & (w2 >= 0)) | ((w0 <= 0) & (w1 <= 0) & (w2 <= 0))
        q = p[inside].astype(int)
        np.add.at(cover, (q[:, 1], q[:, 0]), 1)
    covered = (cover > 0).sum()
    return float((cover > 1).sum()) / max(covered, 1), int(covered)


def check_mesh(obj, lod, rep, fails):
    me = obj.data
    names = [s.name for s in obj.material_slots]
    base_names = [n.split(".")[0] for n in names]
    r = {"object": obj.name, "verts": len(me.vertices)}
    tri = sum(len(p.vertices) - 2 for p in me.polygons)
    r["triangles"] = tri
    if tri > BUDGET[lod]:
        fails.append(f"LOD{lod}: {tri} triangles > budget {BUDGET[lod]}")
    if base_names != SLOTS:
        fails.append(f"LOD{lod}: material slots {base_names} != {SLOTS}")
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    if not np.isfinite(co).all():
        fails.append(f"LOD{lod}: NaN / inf vertex positions")
    allowed = {b["name"] for b in spec.BONES}
    gi = {g.index: g.name for g in obj.vertex_groups}
    bad_groups = [g for g in gi.values() if g not in allowed]
    if bad_groups:
        fails.append(f"LOD{lod}: vertex groups not in the rig: {bad_groups[:5]}")
    non_deform = {b["name"] for b in spec.BONES if not b["deform"]}
    infl = np.zeros(len(me.vertices), dtype=int)
    sums = np.zeros(len(me.vertices))
    W = {}
    for v in me.vertices:
        for g in v.groups:
            if g.weight > 1e-4:
                infl[v.index] += 1
                sums[v.index] += g.weight
                nm = gi[g.group]
                W.setdefault(nm, np.zeros(len(me.vertices)))[v.index] = g.weight
                if nm in non_deform:
                    fails.append(f"LOD{lod}: vertex {v.index} weighted to non-deforming bone {nm}")
    r["max_influences"] = int(infl.max())
    r["unweighted"] = int((infl == 0).sum())
    r["weight_sum_range"] = [round(float(sums.min()), 4), round(float(sums.max()), 4)]
    if infl.max() > 4:
        fails.append(f"LOD{lod}: {int((infl > 4).sum())} vertices with > 4 influences")
    if (infl == 0).any():
        fails.append(f"LOD{lod}: {int((infl == 0).sum())} unweighted vertices")
    if np.abs(sums[infl > 0] - 1).max() > 2e-3:
        fails.append(f"LOD{lod}: weights not normalised (sum range {r['weight_sum_range']})")
    # chains
    missing = [c for c in CHAINS if c not in W or W[c].max() < 0.2]
    if missing:
        fails.append(f"LOD{lod}: spring chains without skin: {missing}")
    r["chains_max_weight"] = {c: round(float(W[c].max()), 2) for c in CHAINS if c in W}
    # symmetry: mirrored vertex pairs must have mirrored weights
    from scipy.spatial import cKDTree
    P = co.reshape(-1, 3)
    tree = cKDTree(P)
    d, ix = tree.query(P * np.array([-1, 1, 1]))
    pair = (d < 5e-4) & (np.abs(P[:, 0]) > 1e-3)

    def mirror(n):
        for a, b in (("_l_", "_r_"), ("_r_", "_l_"), ("_fl_", "_fr_"), ("_fr_", "_fl_"), ("_bl_", "_br_"),
                     ("_br_", "_bl_")):
            if a in n:
                return n.replace(a, b)
        if n.endswith("_l"):
            return n[:-2] + "_r"
        if n.endswith("_r"):
            return n[:-2] + "_l"
        return n
    worst = 0.0
    for nm, col in W.items():
        m = W.get(mirror(nm), np.zeros(len(P)))
        diff = np.abs(col[pair] - m[ix[pair]])
        if diff.size:
            worst = max(worst, float(np.percentile(diff, 99.5)))
    r["mirror_pairs"] = int(pair.sum())
    r["symmetry_p995_diff"] = round(worst, 3)
    if worst > 0.08:
        fails.append(f"LOD{lod}: asymmetric weights on mirrored vertices (p99.5 diff {worst:.3f})")
    # UVs
    if not me.uv_layers:
        fails.append(f"LOD{lod}: no UVs")
    else:
        uv = np.empty(len(me.loops) * 2)
        me.uv_layers[0].data.foreach_get("uv", uv)
        if not np.isfinite(uv).all():
            fails.append(f"LOD{lod}: NaN UVs")
        if uv.min() < -1e-4 or uv.max() > 1 + 1e-4:
            fails.append(f"LOD{lod}: UVs outside 0-1 ({uv.min():.4f} .. {uv.max():.4f})")
        if lod == 0:
            ov = {}
            for si, nm in enumerate(base_names):
                frac, cov = uv_overlap_fraction(me, si)
                ov[nm] = {"overlap": round(frac, 5), "coverage": round(cov / 512 / 512, 3)}
                if frac > 0.002:
                    fails.append(f"LOD{lod}: UV overlap in '{nm}' ({frac * 100:.2f} % of covered texels)")
            r["uv"] = ov
    rep[f"LOD{lod}"] = r


def main(argv):
    art = argv[0] if argv and not argv[0].startswith("--") else os.path.join(HERE, "..", "..", "..", "SourceArt",
                                                                             "Character")
    art = os.path.abspath(art)
    out_json = argv[argv.index("--json") + 1] if "--json" in argv else None
    rep, fails = {}, []
    for lod in (0, 1, 2):
        path = os.path.join(art, "SK_Fighter.fbx" if lod == 0 else f"SK_Fighter_LOD{lod}.fbx")
        if not os.path.exists(path):
            fails.append(f"missing {path}")
            continue
        arms, meshes = import_fbx(path)
        if len(arms) != 1:
            fails.append(f"LOD{lod}: expected one armature, got {len(arms)}")
            continue
        a = arms[0]
        if a.name != "root":
            fails.append(f"LOD{lod}: armature object is '{a.name}', expected 'root'")
        names = {b.name for b in a.data.bones}
        want = {b["name"] for b in spec.BONES}
        if names != want:
            fails.append(f"LOD{lod}: bones differ from the rig (missing {sorted(want - names)[:5]}, extra "
                         f"{sorted(names - want)[:5]})")
        par_bad = [b["name"] for b in spec.BONES if b["name"] in names and
                   ((a.data.bones[b["name"]].parent.name if a.data.bones[b["name"]].parent else None) != b["parent"])]
        if par_bad:
            fails.append(f"LOD{lod}: parent mismatches {par_bad[:5]}")
        if len(meshes) != 1:
            fails.append(f"LOD{lod}: expected one mesh object, got {[m.name for m in meshes]}")
        for m in meshes[:1]:
            check_mesh(m, lod, rep, fails)
    # textures
    from PIL import Image
    tex = {}
    for slot, size in TEX.items():
        for kind in ("BC", "N", "ORM"):
            p = os.path.join(art, f"T_Fighter_{slot}_{kind}.png")
            if not os.path.exists(p):
                fails.append(f"missing texture {os.path.basename(p)}")
                continue
            im = Image.open(p)
            tex[os.path.basename(p)] = list(im.size)
            if im.size != (size, size):
                fails.append(f"{os.path.basename(p)} is {im.size}, expected {size}x{size}")
    rep["textures"] = tex
    rep["failures"] = fails
    print(json.dumps({k: v for k, v in rep.items() if k != "textures"}, indent=1))
    print("VALIDATION", "PASSED" if not fails else "FAILED")
    for f in fails:
        print("  FAIL:", f)
    if out_json:
        with open(out_json, "w") as f:
            json.dump(rep, f, indent=1)
    return 0 if not fails else 1


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    sys.exit(main(argv))
