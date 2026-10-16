"""Bakes the L_Ruins level into the sim's world data (run in the FULL editor after the setup part "ruins"):

  "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
      "$PWD/unreal/Fourfold.uproject" -ExecutePythonScript="$PWD/unreal/Tools/ruins/bake_ruins.py" -unattended -nosplash

Writes unreal/Content/Fourfold/Data/ruins/{world.json, heights.f32, water.f32} (ff::WorldDef; read by AFourfoldOpenWorld
with DataDirName "ruins") and unreal/Saved/Fourfold/ruins_bake_report.json. Never hand-edit the outputs: re-run this.
  heights  line traces straight down on the 2 m grid (ruins_spec.GRID) against the actors tagged FFRuinsGround (terrain
           chunks, backdrop, pyramid; per-poly collision); water surfaces ignored
  water    the same traces against the river / creek surfaces only (tag FFRuinsWater, query collision on for the bake);
           a sample is wet where the surface lies above the ground (NaN elsewhere)
  solids   interior boxes of the pyramid (ruins_data.stone_boxes: stop shots and line of sight, never change footing),
           boxes around big trunks (layout.json props with trunk_r) and invisible rim walls where walkable ground drops into
           ground you could not climb out of (ruins_data.rim_walls: the river gorge's cliff banks)
  spawn    the scene's own camera position, facing the pyramid; sites and the shrine from ruins_spec.SITE_PLAN / SHRINE on
           flat ground reachable both ways from the spawn. ruins_data.validate must pass or nothing is written.
"""
import json
import math
import os
import sys
import time

import unreal

UNREAL = os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0, os.path.join(UNREAL, "Tools", "ruins"))
import ruins_data as RD  # noqa: E402
import ruins_spec as RS  # noqa: E402

MAP = "/Game/Fourfold/Maps/L_Ruins"
OUT = os.path.join(UNREAL, "Content", "Fourfold", "Data", RS.DATA_DIR)
LAYOUT = os.path.join(UNREAL, "SourceArt", "OpenWorld", "JungleRuins", "Game", "layout.json")
REPORT = os.path.join(UNREAL, "Saved", "Fourfold", "ruins_bake_report.json")
TOP_CM, BOTTOM_CM = 30000.0, -30000.0


class ValidationFailed(RuntimeError):
    """ruins_data.validate found errors: the report is written, the data is not."""


def log(msg):
    unreal.log(f"[Fourfold][ruins bake] {msg}")


def tagged(tag):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    out = []
    for a in eas.get_all_level_actors() or []:
        try:
            if any(str(t) == tag for t in a.get_editor_property("tags")):
                out.append(a)
        except Exception:  # noqa: BLE001
            continue
    return sorted(out, key=lambda a: a.get_actor_label())


def trace(world, x, z, ignore):
    """World y of the first blocking hit straight down at world (x, z), and the hit actor; (None, None) on a miss."""
    X, Y, _ = RS.world_to_ue(x, 0.0, z)
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(X, Y, TOP_CM), unreal.Vector(X, Y, BOTTOM_CM),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, True, ignore,
                                                 unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return None, None
    t = hit.to_tuple()   # BreakHitResult order: blocking, initial overlap, time, distance, location, impact point, ..., actor
    if len(t) < 10:
        raise RuntimeError(f"unexpected HitResult tuple ({len(t)} fields)")
    return (t[5].z / 100.0, t[9]) if t[0] else (None, None)


def grid_trace(world, grid, ignore, samples=None):
    """{sample index: world y} for every sample (or the given ones); edge samples that slip between triangles are retried
    0.3 m inward."""
    out = {}
    for j in (samples if samples is not None else range(grid.nx * grid.nz)):
        x, z = grid.pos(j % grid.nx, j // grid.nx)
        y, _a = trace(world, x, z, ignore)
        if y is None:
            y, _a = trace(world, x - math.copysign(0.3, x), z - math.copysign(0.3, z), ignore)
        out[j] = y
    return out


def footprint(actors, grid, pad=2.0):
    """Samples inside the XZ bounds of the actors (+ pad metres)."""
    mask = [False] * (grid.nx * grid.nz)
    for a in actors:
        o, e = a.get_actor_bounds(False)
        x0, x1 = (o.x - e.x) / 100.0 - pad, (o.x + e.x) / 100.0 + pad
        z0, z1 = (o.y - e.y) / 100.0 - pad, (o.y + e.y) / 100.0 + pad
        for j in range(len(mask)):
            x, z = grid.pos(j % grid.nx, j // grid.nx)
            if x0 <= x <= x1 and z0 <= z <= z1:
                mask[j] = True
    return mask


def place(plan, fl, water, heights, grid, ok):
    if plan["mode"] == "bank":
        ik = RD.pick_bank(fl, water, heights, grid, ok)
    else:
        ax, _, az = RS.bl_to_world(plan["anchor"][0], plan["anchor"][1], 0.0)
        pick = RD.pick_high if plan["mode"] == "high" else RD.pick_flat
        ik = pick(fl, grid, ok, ax, az, plan["radius"], **({"max_slope": plan["max_slope"]} if "max_slope" in plan else {}))
    if ik is None:
        raise RuntimeError(f"{plan['id']}: no flat ground reachable both ways near its anchor")
    x, z = grid.pos(*ik)
    return x, z


def main():
    t0 = time.time()
    if os.path.exists(REPORT):
        os.remove(REPORT)                     # never leave a previous run's report behind a crash
    # Static meshes compile asynchronously on load and get their collision only once compiled, on a later frame; the bake
    # traces in this frame, so compile them synchronously (otherwise every trace misses the level's own meshes).
    unreal.SystemLibrary.execute_console_command(None, "Editor.AsyncStaticMeshCompilation 0")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ground, stone, water_actors = tagged("FFRuinsGround"), tagged("FFRuinsStone"), tagged("FFRuinsWater")
    if not ground or not stone or not water_actors:
        raise RuntimeError(f"{MAP}: ground {len(ground)}, stone {len(stone)}, water {len(water_actors)} actors (run the ruins setup)")
    layout = json.load(open(LAYOUT))
    grid = RD.Grid.from_spec(RS.GRID)
    n = grid.nx * grid.nz
    # heights: terrain + stone, water surfaces ignored (their components have no collision outside the bake anyway)
    hit = grid_trace(world, grid, water_actors)
    missing = [j for j in range(n) if hit[j] is None]
    if missing:
        raise RuntimeError(f"{len(missing)} samples without ground, first at {grid.pos(missing[0] % grid.nx, missing[0] // grid.nx)}")
    heights = [hit[j] for j in range(n)]
    # bare terrain under the stone footprint (pyramid excluded) -> interior boxes
    mask = footprint(stone, grid)
    bare = grid_trace(world, grid, water_actors + stone, [j for j in range(n) if mask[j]])
    h_ter = [bare[j] if mask[j] and bare[j] is not None else heights[j] for j in range(n)]
    # water: only the river / creek surfaces block, for the duration of the bake
    for a in water_actors:
        a.static_mesh_component.set_collision_profile_name("BlockAll")
        a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    try:
        surf = grid_trace(world, grid, ground + stone)
    finally:
        for a in water_actors:
            a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    water = [surf[j] if surf[j] is not None and surf[j] > heights[j] + 0.02 else RD.NAN for j in range(n)]
    fl = RD.sample_floors(heights, water, RS.WADE_DEPTH)
    # spawn, sites, shrine
    sx, _, sz = RS.bl_to_world(RS.CAMERA_BL[0], RS.CAMERA_BL[1], 0.0)
    px, _, pz = RS.bl_to_world(RS.PYRAMID_BL[0], RS.PYRAMID_BL[1], 0.0)
    start = grid.nearest(sx, sz)
    # the river runs in a gorge with cliff banks: walls on its rim keep fighters out of ground they could not leave
    walls = RD.rim_walls(fl, grid, start, RS.EDGE_MARGIN)
    blocked = RD.blocked_samples(walls, fl, grid)
    fwd = RD.reach(fl, grid, start, RS.EDGE_MARGIN, blocked=blocked)
    back = RD.reach(fl, grid, start, RS.EDGE_MARGIN, reverse=True, blocked=blocked)
    ok = [bool(fwd[j] and back[j]) and not (RD.wet(water[j]) and heights[j] < water[j]) and
          grid.inside(*grid.pos(j % grid.nx, j // grid.nx), RS.EDGE_MARGIN + 2.0) for j in range(n)]
    facing = RS.facing_toward(sx, sz, px, pz)
    spawn = (sx, RD.floor_at(heights, water, grid, RS.WADE_DEPTH, sx, sz), sz)
    sites, placed = [], [(sx, sz)]
    for plan in RS.SITE_PLAN:
        x, z = place(plan, fl, water, heights, grid, ok)
        near = min(math.hypot(x - qx, z - qz) for qx, qz in placed)
        if near < RS.MIN_SITE_GAP_M:
            raise RuntimeError(f"{plan['id']} at ({x:.1f}, {z:.1f}) is {near:.1f} m from another site / the spawn")
        placed.append((x, z))
        y = RD.floor_at(heights, water, grid, RS.WADE_DEPTH, x, z)
        sites.append(RD.make_site(plan["id"], plan["name"], plan["region"], plan["element"], (x, y, z),
                                  RS.facing_toward(x, z, sx, sz), plan["preset"], plan["aggro"]))
    bx, bz = sx - math.sin(facing) * RS.SHRINE["behind_spawn_m"], sz - math.cos(facing) * RS.SHRINE["behind_spawn_m"]
    ik = RD.pick_flat(fl, grid, ok, bx, bz, RS.SHRINE["radius"], max_slope=0.4)
    if ik is None:
        raise RuntimeError("shrine: no flat reachable ground behind the spawn")
    hx, hz = grid.pos(*ik)
    shrines = [RD.make_shrine(RS.SHRINE["id"], RS.SHRINE["name"], (hx, RD.floor_at(heights, water, grid, RS.WADE_DEPTH, hx, hz), hz))]
    # solids: stone interiors + big trunks
    props = layout["props"]
    solids = (RD.stone_boxes(heights, h_ter, grid, mask) + RD.trunk_boxes(props, heights, water, grid, RS.WADE_DEPTH, RS.EDGE_MARGIN)
              + walls)
    # props for AFourfoldOpenWorld (HISMs) + their assets / cull / shadow
    out_props = [{"mesh": p["mesh"], "pos": p["pos"], "yaw": p["yaw"], "scale": p["scale"]} for p in props]
    assets = {}
    for name, m in sorted(layout["meshes"].items()):
        if m["role"] == "veg":
            assets[name] = {"asset": f"{RS.VEG_ASSET_DIR}/{name}.{name}", "cull": RS.CULL_M[m["kind"]], "shadow": RS.SHADOW[m["kind"]]}
    world_json = RD.world_dict(grid, spawn, facing, sites, shrines, solids, out_props, assets, RS.EDGE_MARGIN, RS.WADE_DEPTH,
                               {"url": RS.SOURCE_URL, "license": RS.LICENSE, "credit": RS.CREDIT, "changes": RS.CHANGES})
    errs = RD.validate(world_json, heights, water, grid)
    trapped = RD.traps(fl, grid, start, RS.EDGE_MARGIN, blocked=blocked)
    report = {"map": MAP, "seconds": round(time.time() - t0, 1), "samples": n,
              "wet": sum(1 for j in range(n) if RD.wet(water[j]) and heights[j] < water[j]),
              "height_range": [round(min(heights), 2), round(max(heights), 2)], "stone_samples": sum(mask),
              "solids": len(solids), "rim_walls": len(walls), "traps": len(trapped), "sites": [s["id"] for s in sites], "props": len(out_props),
              "errors": errs}
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        json.dump(report, f, indent=1)
    if errs:
        raise ValidationFailed("bake validation failed (nothing written): " + "; ".join(errs))
    paths = RD.write_world(OUT, world_json, heights, water)
    log(f"wrote {sorted(paths)} in {report['seconds']} s: {report['wet']} wet samples, {len(solids)} solids, "
        f"{len(trapped)} trap samples, sites {report['sites']}")


# -ExecutePythonScript keeps the editor open: report any failure, then quit.
try:
    main()
except ValidationFailed as e:
    unreal.log_error(f"[Fourfold][ruins bake] {e}")
except Exception as e:  # noqa: BLE001
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as f:
        json.dump({"map": MAP, "errors": [f"{type(e).__name__}: {e}"]}, f, indent=1)
    unreal.log_error(f"[Fourfold][ruins bake] {type(e).__name__}: {e}")
finally:
    unreal.SystemLibrary.quit_editor()
