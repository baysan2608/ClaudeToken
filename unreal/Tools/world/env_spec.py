"""Shared constants of the courtyard art pipeline: material slots (+ UV tile sizes), the sim arena loader, scenery layout.

Used by build_arena.py / build_scenery.py (Blender FBX export + level_layout.json) and by the Blender mock-up renderer.  The Unreal
level builder (Content/Python/fourfold/world) reads the generated SourceArt/Environment/level_layout.json and, for the exact arena
solids, Source/FourfoldCore/Data/sim.json itself.
"""
from __future__ import annotations

import json
import math
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
UE_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ENV_ART = os.path.join(UE_ROOT, "SourceArt", "Environment")
TEX_DIR = os.path.join(ENV_ART, "Textures")
MESH_DIR = os.path.join(ENV_ART, "Meshes")
SIM_JSON = os.path.join(UE_ROOT, "Source", "FourfoldCore", "Data", "sim.json")

# material slot name -> UV repeat (metres).  The slot name is the FBX material name; Unreal maps it to MI_Env_<slot>.
TILE = dict(Floor=4.0, StoneWall=3.0, StoneCap=2.4, Plaster=3.0, Timber=2.0, RoofTile=1.2, PoolTile=1.6, Metal=2.0, Iron=2.0,
            Bronze=2.0, Ground=6.0, Rock=3.0, Glow=1.0, Banner=1.0, Shrub=1.0, Foliage=1.0, Bark=2.0, Ridge=1.0, Water=1.0, Sky=1.0)

# which texture set a slot samples (T_Env_<Set>_{BC,N,ORM}); consumed by the Unreal material builder and the Blender mock-up
SLOT_TEXTURES = dict(Floor="Flagstone", StoneWall="Wall", StoneCap="LedgeCap", Plaster="Plaster", Timber="Timber", RoofTile="RoofTile",
                     PoolTile="PoolTile", Metal="MetalPlate", Ground="Ground", Rock="Rock", Bark="Timber")


def _v3(d):
    return d["$v3"] if isinstance(d, dict) else d


def _v2(d):
    return d["$v2"] if isinstance(d, dict) else d


def load_arena():
    with open(SIM_JSON, encoding="utf-8") as f:
        a = json.load(f)["arena_lab"]
    solids = {s["name"]: dict(name=s["name"], kind=s["kind"], surface=s.get("surface", "stone"), min=_v3(s["min"]), max=_v3(s["max"]))
              for s in a["solids"]}
    return dict(half=float(a["half_size"]), solids=solids, pool_min=_v2(a["pool_min"]), pool_max=_v2(a["pool_max"]),
                pool_floor=float(a["pool_floor"]), pool_level=float(a["pool_level"]), metal_min=_v2(a["metal_min"]),
                metal_max=_v2(a["metal_max"]), metal_top=float(a["metal_top"]), player=_v3(a["player_spawn"]),
                opponent=_v3(a["opponent_spawn"]))


def sim_to_ue(p):
    """sim metres (x, y-up, z) -> Unreal centimetres (X, Y, Z)."""
    return [100.0 * p[0], 100.0 * p[2], 100.0 * p[1]]


# ---------------------------------------------------------------------------------------------------- lighting constants
# The fx stream's FFKeyDir (Shaders/Common/FFLighting.ush) = direction TOWARD the light in Unreal axes (-0.45, 0.35, 0.82): the sun
# comes from the south-west, 55 degrees up, behind the default camera (fighters are front-lit, readable); the light travels the opposite way:
SUN_PITCH = -55.2
SUN_YAW = -37.9
SUN_COLOR = (1.0, 0.84, 0.66)


def sun_dir_ue():
    p, y = math.radians(SUN_PITCH), math.radians(SUN_YAW)
    return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))


def sun_dir_sim():
    x, y, z = sun_dir_ue()       # UE (X, Y, Z) = (x, z, y)
    return (x, z, y)
