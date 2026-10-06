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
DATA_JSON = os.path.join(UNREAL, "Content", "Fourfold", "Data", "character.json")
LOD_TARGETS = {0: 30000, 1: 15000, 2: 6000}
LOD_SCREEN_SIZES = [1.0, 0.30, 0.12]
LOD_GOALS = {0: 29300, 1: 14600, 2: 5800}        # a little headroom under the budgets (30k / 15k / 6k)


def body_decimate_weights(obj, lm):
    """0 = keep (face, lips, eyelids), 1 = free to collapse (back of the head under the hair)."""
    me = obj.data
    v = ch_bl.get_verts(obj)
    w = np.full(len(v), 0.6)
    W = cw.from_vertex_groups(obj)
    dom = np.array(cw.DEFORM_BONES)[np.argmax(W, axis=1)]
    az, z = R.head_polar(v, lm)
    head = dom == "head"
    face = head & (np.abs(az) < 75) & (z < lm["brow_z"] + 0.05) & (z > lm["jaw"][2] - 0.03)
    w[head] = 0.7
    w[head & (R.hair_field(v, lm) > 0.004)] = 1.0          # scalp under the hair cap: never seen
    w[face] = 0.15
    w[R.ear_weight(v, lm) > 0.5] = 0.25                     # ears keep their rim
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
    w[mats] = 0.55            # the hand wraps are in every close-up: keep their silhouette round
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
        dup = []
        for o in objs:
            if lod == 2 and o.name.startswith("lash_"):        # lashes are not worth their triangles at LOD2
                continue
            dup.append(S.duplicate(o, f"{o.name}_LOD{lod}"))
        tot = sum(tris(o) for o in dup)
        ratio = LOD_GOALS[lod] / tot
        for o in dup:
            r = ratio
            nm = o.name
            if nm.startswith(("eye_", "lash_")):
                r = max(ratio, 0.6) if lod == 1 else 0.35
            S.decimate(o, r, None, symmetric=not nm.startswith(("sash", "collar_diag", "hair_tail", "hair_bun",
                                                                    "hair_tie", "eye_", "lash_")))
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


def write_character_json(path, log):
    """Runtime data for the game (palettes, yaw offset, slots) + import settings for the editor script."""
    import ch_design
    import ch_textures
    import ff_rig_spec as spec
    tint_vec = {"FF_Main": [1.0, 0.0, 0.0], "FF_Accent": [0.0, 1.0, 0.0], "FF_Trim": [0.0, 0.0, 1.0]}
    data = {
        "schema": "fourfold.character/1",
        "rig": spec.SPEC_VERSION,
        "mesh": "/Game/Fourfold/Characters/Fighter/SK_Fighter",
        "skeleton": "/Game/Fourfold/Characters/Fighter/SKEL_Fighter",
        "physics_asset": "/Game/Fourfold/Characters/Fighter/PA_Fighter",
        "mesh_yaw_offset_deg": -90.0,
        "height_m": 1.79,
        "slots": list(A.SLOTS),
        "palettes": ch_design.PALETTES,
        "materials": {s: f"/Game/Fourfold/Characters/Fighter/Materials/MI_Fighter_{s}" for s in A.SLOTS},
        "slot_tint": dict(ch_design.SLOT_TINT),
        "slot_tint_select": {s: tint_vec[t] for s, t in ch_design.SLOT_TINT.items()},
        "slot_params": {"skin": {"ScatterStrength": 0.22}, "cloth_main": {"SheenStrength": 0.25},
                        "cloth_accent": {"SheenStrength": 0.20}, "sash": {"SheenStrength": 0.35},
                        "wraps": {"SheenStrength": 0.15, "Porosity": 1.0}, "shoes": {"SheenStrength": 0.10}},
        "status_params": {"vectors": ["FF_Main", "FF_Accent", "FF_Trim", "FF_ElementColor"],
                          "scalars": ["FF_Wet", "FF_Frost", "FF_Burn", "FF_ElementGlow"]},
        "detail_tiling": ch_textures.detail_tiling(log.get("uv", {})),
        "textures": {s: {"size": A.TEX_SIZE[s], "maps": [f"T_Fighter_{s}_{k}" for k in ("BC", "N", "ORM")]}
                     for s in A.SLOTS},
        "lods": {"count": 3, "screen_sizes": LOD_SCREEN_SIZES, "triangles": [log["tris"].get(f"LOD{i}") for i in range(3)],
                 "sources": ["SK_Fighter.fbx", "SK_Fighter_LOD1.fbx", "SK_Fighter_LOD2.fbx"]},
        "bones": {"count": len(spec.BONES) + 1, "manny": spec.UE5_MANNY_BONES,
                  "secondary": spec.FF_SECONDARY_BONES},
        "sockets_hint": {"hand_l": "hand_l", "hand_r": "hand_r", "foot_l": "foot_l", "foot_r": "foot_r",
                         "head": "head", "chest": "spine_05", "pelvis": "pelvis"},
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
        f.write("\n")
    return path


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-textures", action="store_true")
    ap.add_argument("--no-previews", action="store_true")
    ap.add_argument("--quick", action="store_true", help="small textures / few samples (iteration)")
    ap.add_argument("--out", default=ART)
    ap.add_argument("--blend", default=None, help="also save the built scene as .blend here")
    ap.add_argument("--scratch", default=None, help="cache dir for the AO bake (reused while the mesh is unchanged)")
    ap.add_argument("--json", default=None, help="write character.json here (default: Content/Fourfold/Data when "
                                                   "--out is SourceArt/Character)")
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
    log["ngons_split"] = {f"LOD{k}": S.triangulate_ngons(o) for k, o in sorted(lods.items())}
    log["tris"] = {f"LOD{k}": tris(o) for k, o in sorted(lods.items())}
    print("[character] triangles", log["tris"], "uv", log["uv"])
    os.makedirs(a.out, exist_ok=True)
    if not a.no_textures:
        import ch_textures
        for k, o in lods.items():          # only LOD0 may occlude itself in the AO bake
            o.hide_render = k != 0
        arm.hide_render = True
        lods[0]["ff_part_names"] = [p.name for p in parts]
        log["textures"] = ch_textures.build_all(lods[0], parts, lm, a.out, quick=a.quick,
                                                ao_cache=os.path.join(a.scratch, "ao_cache.npz") if a.scratch else None)
        for o in lods.values():
            o.hide_render = False
        log["detail"] = ch_textures.build_detail_textures(a.out)
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
    json_path = a.json or (DATA_JSON if os.path.abspath(a.out) == os.path.abspath(ART) else None)
    if json_path:
        write_character_json(json_path, log)
    with open(os.path.join(a.out, "build_report.json"), "w") as f:
        json.dump(log, f, indent=1, sort_keys=True)
    print("[character] done in", log["seconds"], "s")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    main(argv)
