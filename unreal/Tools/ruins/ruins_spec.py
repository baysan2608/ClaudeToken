"""Jungle Ruins map (L_Ruins): the numbers and frame conversions every stage of the pipeline shares (pure Python 3, no
dependencies; imported by fetch_ruins.py, bake_ruins.py, the tests, the Blender prep and the editor setup part "ruins").

Source: Intel Sample Library "Jungle Ruins" 1.0.1b, CC BY 4.0, by Cristiano Siqueira with Charlene Teets (unreal/docs/CREDITS.md).
Frames:
  USD / Blender  metres, Z up, x / y as authored (the 1 km cinematic terrain tile spans x, y 12..1012).
  world (sim)    metres, +Y up:  x = bx - CENTER_BL[0],  y = bz,  z = -(by - CENTER_BL[1])  (world.json, ff::WorldDef).
  Unreal         UE = 100 * (x, z, y) (FourfoldCoords.h WorldToUE). An FBX written in Blender-local map space
                 (bx - cx, by - cy, bz) imports at exactly that point, so every map mesh sits at the origin.
Measured from the USD on 2026-10-14 (unreal/docs/ruins/README.md): pyramid 42 x 42 m centred (543.7, 809.8), z -7.2..13.3;
the river is flat at z -9.99 inside the core, in a gorge up to 58 m deep; the creek (sloped, z 0.9..5.7) is at x 680..832,
y 196..328, 470 m south of the pyramid - outside any ~250 m core around the pyramid, so it stays scenery.
"""
import math
import zlib

SOURCE_URL = "https://cdrdv2.intel.com/v1/dl/getContent/844047"
ZIP_NAME = "JungleRuins_1_0_1b.zip"
ZIP_BYTES = 4254165506
LICENSE = "CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)"
CREDIT = "\"Jungle Ruins\" by Cristiano Siqueira, with Charlene Teets / Intel Sample Library"
CHANGES = ("288 m core cut from the cinematic terrain and re-meshed with LODs; pyramid LODs and repacked textures; jungle thinned "
           "and re-scattered from the scene's own plants; textures cropped and resized; Fourfold water material and lighting")

CORE_BL = (456.0, 584.0, 744.0, 872.0)        # x0, y0, x1, y1: the playable 288 m square (pyramid + river curve)
CENTER_BL = (600.0, 728.0)
TILE_BL = (12.0, 12.0, 1012.0, 1012.0)         # Terrain_Cinematic: the backdrop beyond the core
RING_M = 120.0                                 # trees are also kept this far outside the core (scenery behind the edge)
CHUNKS = 3                                     # core terrain: CHUNKS x CHUNKS meshes of 96 m
PYRAMID_BL = (543.7, 809.8)
CAMERA_BL = (572.5, 766.5)                     # the scene's own camera: the player spawn, looking at the pyramid

GRID = {"nx": 145, "nz": 145, "x0": -144.0, "z0": -144.0, "cell": 2.0}
EDGE_MARGIN = 8.0
WADE_DEPTH = 0.85
DATA_DIR = "ruins"                             # Content/Fourfold/Data/ruins = AFourfoldOpenWorld.DataDirName
VEG_ASSET_DIR = "/Game/Fourfold/Ruins/Vegetation"

# point-instancer layer, prototype layer (both relative to extract/JungleRuins/USD/elements), kind
VEG = [
    ("RiverForest/PI_S_RiverForest.usd", "RiverForest/riverforest_classes.usda", "tree"),
    ("RiverSapling/PI_RiverSapling.usd", "RiverSapling/riversapling_classes.usda", "sapling"),
    ("RiverSeedling/PI_S_RiverSeedling.usd", "RiverSeedling/riverseedling_classes.usda", "seedling"),
    ("Shrub/PI_Shrub.usd", "Shrub/shrub_classes.usda", "shrub"),
    ("Grass_A/PI_Grass_A.usd", "Grass_A/grass_A_classes.usda", "grass"),
    ("Grass_B/PI_Grass_B.usd", "Grass_B/grass_B_classes.usda", "grass"),
    ("Pyramid_Grass_B/PI_Pyramid_GrassB.usd", "Pyramid_Grass_B/grass_B_classes.usda", "pyramid_grass"),
]
THIN = {"tree": 6, "sapling": 8, "seedling": 60, "shrub": 6, "grass": 40, "pyramid_grass": 10}   # keep 1 in N (core)
RING_THIN = 20                                                                                    # trees in the ring
LOD_TRIS = {"tree": (12000, 3500, 1000), "sapling": (6000, 2000, 600), "seedling": (1500, 500, 150),
            "shrub": (1500, 500, 150), "grass": (800, 250, 80), "pyramid_grass": (800, 250, 80)}
CULL_M = {"tree": 260.0, "sapling": 120.0, "seedling": 60.0, "shrub": 50.0, "grass": 35.0, "pyramid_grass": 35.0}
SHADOW = {"tree": True, "sapling": True, "seedling": False, "shrub": False, "grass": False, "pyramid_grass": False}
BUDGET = {"tree": 1200, "sapling": 600, "seedling": 6500, "shrub": 2300, "grass": 16000, "pyramid_grass": 5000}

# Encounter sites (element: earth 0, water 1, fire 2, air 3). mode: flat = flattest reachable ground within radius of the
# Blender anchor; bank = beside the river; high = the highest flat reachable ground within radius of the anchor.
SITE_PLAN = [
    {"id": "site_ruins_stair", "name": "Stair Warden", "region": "earth", "element": 0, "preset": "novice", "aggro": 9.0,
     "mode": "flat", "anchor": (519.7, 785.8), "radius": 8.0},
    {"id": "site_ruins_river", "name": "River Keeper", "region": "water", "element": 1, "preset": "adept", "aggro": 10.0,
     "mode": "bank", "anchor": None, "radius": 0.0},
    {"id": "site_ruins_summit", "name": "Summit Adept", "region": "air", "element": 3, "preset": "master", "aggro": 9.0,
     "mode": "high", "anchor": PYRAMID_BL, "radius": 10.0},
]
SHRINE = {"id": "shrine_ruins", "name": "Ruins Shrine", "behind_spawn_m": 6.0, "radius": 6.0}
MIN_SITE_GAP_M = 20.0                          # between sites, and from a site to the spawn


def bl_to_world(bx, by, bz):
    return (bx - CENTER_BL[0], bz, -(by - CENTER_BL[1]))


def world_to_bl(x, y, z):
    return (x + CENTER_BL[0], CENTER_BL[1] - z, y)


def world_to_ue(x, y, z):
    return (x * 100.0, z * 100.0, y * 100.0)


def ue_to_world(X, Y, Z):
    return (X / 100.0, Z / 100.0, Y / 100.0)


def bl_yaw_to_ue(yaw_rad):
    """Blender yaw (counter-clockwise about +Z, radians) -> Unreal yaw (degrees): the FBX import mirrors Y."""
    return -math.degrees(yaw_rad)


def facing_toward(x, z, tx, tz):
    """Sim facing (radians, forward = (sin f, cos f)) from world (x, z) toward (tx, tz)."""
    return math.atan2(tx - x, tz - z)


def salt(name):
    """Per-instancer salt of the thinning hash (stable across runs and platforms)."""
    return zlib.crc32(name.encode("utf-8")) & 0xFFFFFFFF


def keep(salt_value, index, every):
    """Stable thinning: True for about 1 in `every` instances of one instancer."""
    if every <= 1:
        return True
    h = (index * 2654435761 + salt_value * 40503) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 13
    return h % every == 0


def proto_key(name):
    """Point-instancer prototype name -> mesh suffix: RiverForest_01_Animated_Translucent -> RiverForest_01."""
    for suffix in ("_Animated_Translucent", "_Translucent"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name
