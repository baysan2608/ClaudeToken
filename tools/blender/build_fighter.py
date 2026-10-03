#!/usr/bin/env python
"""Build the Fourfold training fighter from nothing.

    /home/user/tools/bpyenv/bin/python tools/blender/build_fighter.py            # run from the repo root

Writes
    assets_src/fighter.blend                         (armature + skinned mesh + all actions, fake-user'd)
    game/assets/characters/fighter.glb               (glTF binary, +Y up, skin + one animation per clip)
    game/assets/characters/fighter_clips.json        (duration / loop / contact / notes per clip)

Pipeline: factory-empty scene -> armature (fighter_skeleton) -> procedural low-poly mesh with explicit weights
(fighter_mesh) -> pose engine / IK clip catalogue (fighter_pose, fighter_dsl, fighter_clips) baked to 30 fps
LINEAR-keyed actions -> save .blend -> glTF export -> clip json.
"""
import argparse
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

import bpy  # noqa: E402

import fighter_clips as C  # noqa: E402
import fighter_dsl as dsl  # noqa: E402
import fighter_pose as fp  # noqa: E402
import fighter_scene as fsc  # noqa: E402

# the exact clip names required by the game (also the export order)
SPEC_ORDER = [
    "idle", "stance_earth", "stance_water", "stance_fire", "stance_air", "walk", "run", "strafe_l", "strafe_r", "walk_back",
    "evade_l", "evade_r", "evade_back", "evade_fwd", "jump", "fall", "land", "glide", "air_dash",
    "guard", "deflect", "earth_wall",
    "hit_front", "hit_back", "hit_heavy", "knockdown", "getup", "stagger",
    "earth_lift", "earth_throw", "earth_heavy", "earth_hold",
    "water_draw", "water_hold", "water_whip", "water_freeze", "water_shield",
    "fire_jab", "fire_charge", "fire_release", "heat_draw", "magma_hold", "pour", "lightning_charge", "lightning_release",
    "air_push", "air_gust",
]


def build(args):
    t0 = time.time()
    arm, mesh_ob, mb = fsc.build_scene()
    rig = fp.RigModel(arm)
    tris = sum(len(f) - 2 for f in mb.faces)
    print(f"[fighter] mesh: {len(mb.verts)} verts, {tris} tris, bones: {len(arm.data.bones)}")

    missing = [n for n in SPEC_ORDER if n not in C.CLIPS]
    extra = [n for n in C.CLIPS if n not in SPEC_ORDER]
    if missing or extra:
        raise SystemExit(f"clip catalogue mismatch: missing={missing} extra={extra}")

    clips_json = {}
    bad = []
    for name in SPEC_ORDER:
        cd = C.CLIPS[name]
        res = dsl.build_clip(rig, cd)
        n = res["n"]
        # sanity: IK reach for the planted legs of non-gait clips is reported, not hidden
        worst = max((max(v for k, v in d.items() if k.startswith("reach_") and k.endswith("leg")) for d in res["diags"]), default=0.0)
        if worst > 0.03:
            bad.append((name, worst))
        act = fp.bake_action(arm, name, res["frames"])
        act["fighter_loop"] = bool(cd.loop)
        contact = None
        if cd.contact is not None:
            contact = round(round(cd.contact * fp.FPS) / fp.FPS, 4)
        act["fighter_contact"] = -1.0 if contact is None else contact
        clips_json[name] = {
            "duration": round(n / fp.FPS, 4),
            "loop": bool(cd.loop),
            "contact": contact,
            "notes": cd.notes.strip(),
            "frames": n,
        }
    if bad:
        print("[fighter] WARNING leg reach misses (m):", bad)

    sc = bpy.context.scene
    sc.frame_start = 0
    sc.frame_end = max(c["frames"] for c in clips_json.values())
    sc.render.fps = 30
    fp.reset_pose(arm)
    bpy.context.view_layer.objects.active = arm

    blend_path = os.path.join(REPO, "assets_src", "fighter.blend")
    glb_path = os.path.join(REPO, "game", "assets", "characters", "fighter.glb")
    json_path = os.path.join(REPO, "game", "assets", "characters", "fighter_clips.json")
    if args.out_dir:
        blend_path = os.path.join(args.out_dir, "fighter.blend")
        glb_path = os.path.join(args.out_dir, "fighter.glb")
        json_path = os.path.join(args.out_dir, "fighter_clips.json")
    for p in (blend_path, glb_path, json_path):
        os.makedirs(os.path.dirname(p), exist_ok=True)

    # export wants the animation to be un-assigned (all actions are exported separately)
    arm.animation_data.action = None
    bpy.context.preferences.filepaths.save_version = 0       # no fighter.blend1 backups
    bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=True)
    print("[fighter] wrote", blend_path)

    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    mesh_ob.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.gltf(
        filepath=glb_path,
        export_format="GLB",
        use_selection=True,
        export_yup=True,
        export_apply=False,
        export_materials="EXPORT",
        export_normals=True,
        export_texcoords=False,
        export_tangents=False,
        export_vertex_color="NONE",
        export_cameras=False,
        export_lights=False,
        export_extras=False,
        export_skins=True,
        export_influence_nb=4,
        export_all_influences=False,
        export_def_bones=True,
        export_animations=True,
        export_animation_mode="ACTIONS",
        export_force_sampling=False,
        export_optimize_animation_size=False,
        export_reset_pose_bones=True,
        export_frame_range=False,
        export_nla_strips=False,
        export_morph=False,
        export_leaf_bone=False,
    )
    print("[fighter] wrote", glb_path, f"({os.path.getsize(glb_path) / 1024:.0f} KiB)")

    with open(json_path, "w") as f:
        json.dump(clips_json, f, indent=2)
        f.write("\n")
    print("[fighter] wrote", json_path)

    check_glb(glb_path, SPEC_ORDER)
    patch_godot_import(glb_path + ".import", clips_json)
    print(f"[fighter] done in {time.time() - t0:.1f}s")
    return arm, mesh_ob, clips_json


POST_IMPORT = "res://assets/characters/fighter_post_import.gd"


def patch_godot_import(import_path, clips):
    """Godot's glTF importer cannot know which animations loop.  game/assets/characters/fighter_post_import.gd (an
    EditorScenePostImport) sets loop modes from fighter_clips.json; make sure Godot's fighter.glb.import points at it
    (and carries no expanded per-animation settings).  On a fresh checkout without the .import file, run
    `tools/scripts/godot.sh --headless --import` once and re-run this script."""
    if not os.path.exists(import_path):
        print("[fighter] note: no", os.path.basename(import_path), "yet - run godot --headless --import, then rebuild to hook the loop post-import script")
        return
    lines = open(import_path).read().split("\n")
    out, i = [], 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("_subresources="):
            depth = ln.count("{") - ln.count("}")
            while depth > 0:
                i += 1
                depth += lines[i].count("{") - lines[i].count("}")
            out.append("_subresources={}")
        elif ln.startswith("import_script/path="):
            out.append(f'import_script/path="{POST_IMPORT}"')
        else:
            out.append(ln)
        i += 1
    open(import_path, "w").write("\n".join(out))
    print("[fighter] hooked", os.path.basename(import_path), "->", POST_IMPORT, f"({sum(1 for c in clips.values() if c['loop'])} looping clips)")


def check_glb(path, expect):
    data = open(path, "rb").read()
    n = struct.unpack("<I", data[12:16])[0]
    j = json.loads(data[20:20 + n])
    anims = [a["name"] for a in j.get("animations", [])]
    print(f"[fighter] glb check: {len(j['nodes'])} nodes, {len(j['skins'])} skin(s) with {len(j['skins'][0]['joints'])} joints, "
          f"{len(anims)} animations, {len(j['meshes'])} mesh(es), materials={[m['name'] for m in j['materials']]}")
    miss = [e for e in expect if e not in anims]
    if miss:
        raise SystemExit(f"glb is missing animations: {miss}")
    for key in ("cameras", "extensionsUsed"):
        if key in j:
            print(f"[fighter] note: glb has {key}: {j[key]}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=None, help="write the three outputs here instead of the repo locations")
    a = ap.parse_args(sys.argv[1:])
    build(a)
