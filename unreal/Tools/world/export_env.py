"""Builds every environment mesh, exports the FBX files (SourceArt/Environment/Meshes/*.fbx) and writes
SourceArt/Environment/meshes.json (stats, pivots, expected sizes, slots) + level_layout.json (placements, Unreal centimetres).

    /home/user/tools/bpyenv/bin/python Tools/world/export_env.py [--only SM_Env_WallN,...] [--no-fbx] [--stats]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import env_spec as ES            # noqa: E402
import build_arena as BA         # noqa: E402

try:
    import build_scenery as BS   # noqa: E402
except ImportError:              # scenery not written / not requested
    BS = None

SOLID_MESH = {"north_wall": "SM_Env_WallN", "south_wall": "SM_Env_WallS", "west_wall": "SM_Env_WallW", "east_wall": "SM_Env_WallE",
              "cover_wall": "SM_Env_CoverWall", "terrace": "SM_Env_Terrace", "step_block": "SM_Env_StepBlock",
              "high_ledge": "SM_Env_HighLedge", "pillar_ne": "SM_Env_Pillar", "pillar_sw": "SM_Env_Pillar"}


def all_builders(A, scenery=True):
    b = dict(BA.ARENA_BUILDERS)
    if scenery and BS is not None:
        b.update(BS.SCENERY_BUILDERS)
    return b


def build_all_objects(lookup, scenery=True):
    """Mock-up scene: every mesh placed at its level position (Blender)."""
    import bpy_io as IO
    A = ES.load_arena()
    objs = []
    for name, fn in all_builders(A, scenery).items():
        if name == "SM_Env_Sky":
            continue                      # the mock-up world shader stands in for the sky dome
        mesh, pivot = fn(A)
        ob = IO.to_object(mesh, pivot, lookup, name=name)
        objs.append(ob)
        if name == "SM_Env_Pillar":
            s2 = A["solids"]["pillar_sw"]
            c2 = ((np.array(s2["min"]) + np.array(s2["max"])) / 2)
            ob2 = IO.to_object(mesh, pivot, lookup, name="SM_Env_Pillar_2")
            from mathutils import Vector
            b = IO.sim_to_blender(c2)
            ob2.location = Vector((float(b[0]), float(b[1]), float(b[2])))
            objs.append(ob2)
    if scenery and BS is not None and hasattr(BS, "place_instances"):
        objs += BS.place_instances(lookup, A)
    return objs


def layout_entries(A, info):
    """Placements in Unreal centimetres.  info: name -> meshes.json entry."""
    ents = []
    def ent(actor, mesh, pivot_sim, tags=(), group="arena", mobility="static", shadow=False, extra=None):
        e = dict(actor=actor, mesh=mesh, location=[round(v, 3) for v in ES.sim_to_ue(pivot_sim)], rotation=[0.0, 0.0, 0.0], scale=[1.0, 1.0, 1.0],
                 tags=list(tags), group=group, mobility=mobility, cast_shadow=shadow)
        if mesh in info:
            e["expected_size_cm"] = info[mesh]["expected_size_cm"]
            e["lightmap_res"] = info[mesh].get("lightmap_res")
        if extra:
            e.update(extra)
        return e
    for sname, mesh in SOLID_MESH.items():
        s = A["solids"][sname]
        c = (np.array(s["min"]) + np.array(s["max"])) / 2
        ents.append(ent(f"FFArena_{sname}", mesh, c, tags=[f"FFSolid_{sname}"], group="solid", shadow=True,
                        extra=dict(solid=sname, size_cm=[round(100 * (s["max"][i] - s["min"][i]), 3) for i in (0, 2, 1)])))
    for name in ("SM_Env_Floor", "SM_Env_PoolBasin", "SM_Env_PoolWater", "SM_Env_MetalPlate", "SM_Env_Banners", "SM_Env_Lanterns", "SM_Env_Rings"):
        if name in info:
            e = ent(f"FFArena_{name[7:]}", name, info[name]["pivot_sim"], group="arena",
                    mobility="static" if info[name].get("lightmap_res") else "movable")        # no lightmap UVs -> Movable
            ents.append(e)
    if BS is not None and hasattr(BS, "layout_entries"):
        ents += BS.layout_entries(A, info, ent)
    return ents


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--no-fbx", action="store_true")
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args()
    A = ES.load_arena()
    builders = all_builders(A)
    only = [n for n in a.only.split(",") if n]
    if not a.no_fbx:
        import bpy_io as IO
        IO.reset_scene()
    info = {}
    total_tris = 0
    for name, fn in builders.items():
        mesh, pivot = fn(A)
        lm = BA.LIGHTMAP.get(name) or (getattr(BS, "LIGHTMAP", {}).get(name) if BS else None)
        texel = None
        if lm:
            texel = mesh.lightmap_uvs(lm[0], lm[1])
        mesh_stats = mesh.stats()
        mn, mx = np.array(mesh_stats["min"]), np.array(mesh_stats["max"])
        size = mx - mn
        e = dict(file=f"Meshes/{name}.fbx", tris=mesh_stats["tris"], slots=[s for s, _ in mesh.slots], slot_tris=mesh_stats["slots"],
                 pivot_sim=[round(float(v), 4) for v in pivot],
                 bounds_sim=[[round(float(v), 4) for v in (mn - np.array(pivot))], [round(float(v), 4) for v in (mx - np.array(pivot))]],
                 expected_size_cm=[round(float(size[0] * 100), 2), round(float(size[2] * 100), 2), round(float(size[1] * 100), 2)],
                 lightmap_res=(lm[1] if lm else None), lightmap_texel_m=(round(texel, 3) if texel else None))
        info[name] = e
        total_tris += e["tris"]
        if a.stats:
            print(f"{name:22s} tris={e['tris']:6d} size_cm={e['expected_size_cm']} lm={e['lightmap_res']}")
        if not a.no_fbx and (not only or name in only):
            import bpy_io as IO
            mesh.recenter(pivot)
            ob = IO.to_object(mesh, (0.0, 0.0, 0.0), None, name=name, place=False)
            IO.export_fbx(ob, os.path.join(ES.MESH_DIR, name + ".fbx"))
            print("exported", name, e["tris"], "tris")
    with open(os.path.join(ES.ENV_ART, "meshes.json"), "w", encoding="utf-8") as f:
        json.dump(dict(schema="fourfold.env.meshes/1", units="cm (expected_size_cm in Unreal axes X Y Z); pivot_sim in sim metres",
                       meshes=info), f, indent=1)
    layout = layout_entries(A, info)
    with open(os.path.join(ES.ENV_ART, "level_layout.json"), "w", encoding="utf-8") as f:
        lights = [dict(l, location=[round(v, 3) for v in ES.sim_to_ue(l["pos"])]) for l in BA.light_points(A)]
        json.dump(dict(schema="fourfold.env.layout/1", sun=dict(pitch=ES.SUN_PITCH, yaw=ES.SUN_YAW, color=list(ES.SUN_COLOR)),
                       mask=dict(rect=[-18.0, -18.0, 36.0]), actors=layout, lights=lights), f, indent=1)
    print("total tris", total_tris, "meshes", len(info), "layout actors", len(layout))


if __name__ == "__main__":
    main()
