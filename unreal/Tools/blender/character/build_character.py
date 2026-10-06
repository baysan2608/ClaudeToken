"""Fourfold fighter - ONE entry point that rebuilds everything the `character` stream ships.

    /home/user/tools/bpyenv/bin/python unreal/Tools/blender/character/build_character.py [--no-textures]
        [--no-previews] [--quick] [--out <dir>]

Steps (all deterministic):
  1. body from the CC0 base mesh -> our proportions -> fitted onto the frozen rig (ch_body / ch_fit)
  2. garments, hair, eyes, sash (ch_garments / ch_hair), skin weights (ch_weights / ch_assemble)
  3. UVs (analytic + unwrap) and packing per material (ch_uv)
  4. Blender objects, budget decimation, LOD1 / LOD2 (ch_scene)
  5. textures per material slot: BaseColor / Normal (DirectX green) / ORM (ch_textures, Cycles AO bake)
  6. FBX export with the frozen settings (ff_fbx_export) -> SourceArt/Character/SK_Fighter*.fbx
  7. validation (validate_character.py) and review renders (ch_previews) -> SourceArt/Character/previews/
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "common"))

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import ch_assemble as A  # noqa: E402
import ch_bl  # noqa: E402
import ch_regions as R  # noqa: E402
import ch_scene as S  # noqa: E402
import ch_weights as cw  # noqa: E402
import ff_fbx_export as fx  # noqa: E402

UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ART = os.path.join(UNREAL, "SourceArt", "Character")
LOD_TARGETS = {0: 30000, 1: 15000, 2: 6000}
LOD_GOALS = {0: 27500, 1: 14000, 2: 5600}        # leave headroom under the budgets


def body_decimate_weights(obj, lm):
    """0 = keep (face, lips, eyelids, fingertips), 1 = free to collapse (wraps, back of the head)."""
    me = obj.data
    v = ch_bl.get_verts(obj)
    w = np.full(len(v), 0.6)
    W = cw.from_vertex_groups(obj)
    dom = np.array(cw.DEFORM_BONES)[np.argmax(W, axis=1)]
    az, z = R.head_polar(v, lm)
    head = dom == "head"
    face = head & (np.abs(az) < 75) & (z < lm["brow_z"] + 0.05) & (z > lm["jaw"][2] - 0.03)
    w[head] = 0.85
    w[face] = 0.15
    for s in ("l", "r"):
        e = lm[f"eye_c_{s}"]
        w[np.linalg.norm(v - e, axis=1) < 0.03] = 0.0
    w[np.linalg.norm(v - lm["mouth"], axis=1) < 0.035] = 0.0
    fingers = np.array([d.startswith(("index", "middle", "ring", "pinky", "thumb")) for d in dom])
    w[fingers] = 0.5
    mats = np.zeros(len(v), dtype=bool)
    for poly in me.polygons:
        if obj.material_slots[poly.material_index].name == "wraps":
            mats[list(poly.vertices)] = True
    w[mats] = 1.0
    return w


def tris(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def build_lods(objs, by_name, lm, log):
    """objs: per-part objects (LOD0 source). Returns {lod: joined object}."""
    out = {}
    # LOD0: decimate the body (dense base-mesh face / fingers) until the whole fits the goal
    total = sum(tris(o) for o in objs)
    body = by_name["body"]
    over = total - LOD_GOALS[0]
    if over > 0:
        bt = tris(body)
        ratio = max(0.3, (bt - over) / bt)
        S.decimate(body, ratio, body_decimate_weights(body, lm), symmetric=True)
        S.clean_weights(body)
    lod0_parts = list(objs)
    log["lod0_parts"] = {o.name: tris(o) for o in lod0_parts}
    for lod in (1, 2):
        dup = [S.duplicate(o, f"{o.name}_LOD{lod}") for o in objs]
        tot = sum(tris(o) for o in dup)
        ratio = LOD_GOALS[lod] / tot
        for o in dup:
            r = ratio
            nm = o.name
            if nm.startswith(("eye_", "lash_")):
                r = max(ratio, 0.6) if lod == 1 else 0.35
            if nm.startswith("lash_") and lod == 2:
                bpy.data.objects.remove(o)
                continue
            S.decimate(o, r, None, symmetric=not nm.startswith(("sash", "collar_diag", "hair_tail", "hair_bun",
                                                                    "hair_tie", "eye_")))
        dup = [o for o in dup if o.name in bpy.data.objects]
        # second pass if still over
        tot = sum(tris(o) for o in dup)
        if tot > LOD_GOALS[lod]:
            r2 = LOD_GOALS[lod] / tot
            for o in dup:
                S.decimate(o, r2, None, symmetric=False)
        for o in dup:
            S.clean_weights(o)
        out[lod] = S.join(dup, f"SK_Fighter_LOD{lod}")
    out[0] = S.join(lod0_parts, "SK_Fighter")
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-textures", action="store_true")
    ap.add_argument("--no-previews", action="store_true")
    ap.add_argument("--quick", action="store_true", help="small textures / few samples (iteration)")
    ap.add_argument("--out", default=ART)
    ap.add_argument("--blend", default=None, help="also save the built scene as .blend here")
    a = ap.parse_args(argv)
    t0 = time.time()
    log = {}
    ch_bl.reset_scene()
    parts, st, lm = A.build_parts(verbose=True)
    A.assign_uvs(parts, lm)
    log["uv"] = A.pack_uvs(parts)
    arm = S.build_rig()
    objs, by_name = [], {}
    for i, p in enumerate(parts):
        o = S.part_object(p, arm)
        att = o.data.attributes.new("part_id", "INT", "FACE")
        att.data.foreach_set("value", np.full(len(o.data.polygons), i, dtype=np.int32))
        objs.append(o)
        by_name[p.name] = o
    lods = build_lods(objs, by_name, lm, log)
    log["tris"] = {f"LOD{k}": tris(o) for k, o in lods.items()}
    print("[character] triangles", log["tris"], "uv", log["uv"])
    os.makedirs(a.out, exist_ok=True)
    if not a.no_textures:
        import ch_textures
        for k, o in lods.items():          # only LOD0 may occlude itself in the AO bake
            o.hide_render = k != 0
        arm.hide_render = True
        log["textures"] = ch_textures.build_all(lods[0], parts, lm, a.out, quick=a.quick)
        for o in lods.values():
            o.hide_render = False
    # FBX: LOD0 = SK_Fighter.fbx, LOD1 / LOD2 separate files (imported into the same asset by the editor script)
    for lod, o in lods.items():
        others = [x for k, x in lods.items() if k != lod]
        for x in others:
            x.hide_set(True)
        path = os.path.join(a.out, "SK_Fighter.fbx" if lod == 0 else f"SK_Fighter_LOD{lod}.fbx")
        fx.export_skeletal_mesh_fbx(path, arm, [o])
        for x in others:
            x.hide_set(False)
    if a.blend:
        bpy.ops.wm.save_as_mainfile(filepath=a.blend)
    if not a.no_previews:
        import ch_previews
        ch_previews.render_all(arm, lods, a.out, quick=a.quick)
    log["seconds"] = round(time.time() - t0, 1)
    with open(os.path.join(a.out, "build_report.json"), "w") as f:
        json.dump(log, f, indent=1, sort_keys=True)
    print("[character] done in", log["seconds"], "s")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    main(argv)
