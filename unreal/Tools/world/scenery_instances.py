#!/usr/bin/env python3
"""Instance layout of the scanned scenery meshes (Poly Haven trees, Tools/blender/world/prep_trees.py) that replace the card
trees of build_scenery.build_trees: the same 90 positions and kinds (same seed, same placement loop), each mapped to a scanned
species, with its own height, yaw and variant.  Writes SourceArt/Environment/scenery_instances.json, read by the Unreal
foliage builder (Content/Python/fourfold/world/foliage.py).

  python3 unreal/Tools/world/scenery_instances.py

Coordinates: sim (x, y-up, z) metres -> Unreal cm = 100 * (x, z, y), as everywhere in the world pipeline.
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build_scenery as BS  # noqa: E402

OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "Environment", "scenery_instances.json"))

# card-tree kind -> (species, height range m).  Species names = mesh families made by prep_trees.py.
SPECIES = {
    "pine": ("Fir", (13.0, 19.0)),            # fir_tree_01: tall conifers, the bulk of the forest
    "cypress": ("FirSlim", (9.0, 13.0)),      # fir_sapling_medium: narrow young firs between them
    "broad": ("Broadleaf", (6.0, 9.0)),       # tree_small_02 / island_tree_02: wide crowns near the halls
}


def tree_positions():
    """build_scenery.build_trees' placement loop, verbatim (rng 777): [(x, z, kind)]."""
    rng = np.random.default_rng(777)
    buildings = [(-26.0, -44.0), (31.0, 38.0), (34.0, -66.0)]
    placed, trees, tries = [], [], 0
    while len(trees) < 90 and tries < 2000:
        tries += 1
        ang = rng.uniform(0, BS.TAU)
        dist = rng.uniform(21.0, 82.0)
        x, z = dist * math.cos(ang), dist * math.sin(ang)
        if abs(x) < 20.5 and abs(z) < 20.5:
            continue
        if any((x - bx) ** 2 + (z - bz) ** 2 < 10.0 ** 2 for bx, bz in buildings):
            continue
        if any((x - px) ** 2 + (z - pz) ** 2 < 4.2 ** 2 for px, pz in placed):
            continue
        placed.append((x, z))
        kind = rng.choice(["pine", "broad", "cypress"], p=[0.45, 0.35, 0.20])
        trees.append((x, z, str(kind)))
    return trees


# distant mountain ring: scanned rock faces (Tools/blender/world/prep_rocks.py) scaled up, faces turned to the arena.
# Most of them stand where the duel camera looks (Unreal -Y = sim -z), a few behind and to the sides.
MOUNTAIN_MESHES = ["SM_Rock_Cliff", "SM_Rock_Strata", "SM_Rock_Granite"]


def mountains():
    rng = np.random.default_rng(31)
    out = []
    arcs = [(-90.0, 75.0, 11, (1300.0, 3400.0)), (90.0, 70.0, 5, (1500.0, 3600.0))]     # (centre deg, half width, count, dist m)
    for centre, half, count, (d0, d1) in arcs:
        for k in range(count):
            a = math.radians(centre - half + 2.0 * half * (k + rng.uniform(0.2, 0.8)) / count)     # Unreal yaw of the spot
            d = float(rng.uniform(d0, d1))
            ux, uy = d * math.cos(a), d * math.sin(a)                                             # Unreal metres
            x, z = ux, uy                                                                         # sim x = UE X, sim z = UE Y
            h = float(rng.uniform(230.0, 520.0)) * (0.7 + 0.3 * (d - d0) / (d1 - d0))
            y = float(BS.ground_h(x, z))
            out.append(dict(mesh=MOUNTAIN_MESHES[int(rng.integers(0, len(MOUNTAIN_MESHES)))], height_m=round(h, 1),
                            face_to_deg=round(math.degrees(math.atan2(-uy, -ux)), 1), yaw_jitter_deg=round(float(rng.uniform(-18, 18)), 1),
                            width_scale=round(float(rng.uniform(1.0, 1.7)), 2),
                            location_cm=[round(100.0 * ux, 1), round(100.0 * uy, 1), round(100.0 * y - 0.12 * 100.0 * h, 1)]))
    return out


def main():
    rng = np.random.default_rng(778)
    out = []
    for x, z, kind in tree_positions():
        species, (h0, h1) = SPECIES[kind]
        dist = math.hypot(x, z)
        # nearer trees a little smaller so the crowns frame the wall line instead of walling it in
        h = float(rng.uniform(h0, h1)) * (0.85 + 0.15 * min(1.0, (dist - 21.0) / 40.0))
        y = float(BS.ground_h(x, z))
        out.append(dict(species=species, variant=int(rng.integers(0, 1 << 16)), height_m=round(h, 2),
                        yaw_deg=round(float(rng.uniform(0.0, 360.0)), 1),
                        lean_deg=round(float(rng.normal(0.0, 1.5)), 2),
                        location_cm=[round(100.0 * x, 1), round(100.0 * z, 1), round(100.0 * y - 15.0, 1)]))
    data = dict(schema="fourfold.env.instances/1", units="cm, Unreal axes; height_m = target height",
                source="Tools/world/scenery_instances.py (tree positions = build_scenery.build_trees, rng 777)", trees=out,
                mountains=mountains())
    with open(OUT, "w") as f:
        json.dump(data, f, indent=1)
    by = {}
    for t in out:
        by[t["species"]] = by.get(t["species"], 0) + 1
    print("wrote", OUT, by)


if __name__ == "__main__":
    main()
