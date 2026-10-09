#!/usr/bin/env python3
"""Downloads photo-scanned CC0 PBR texture sets from Poly Haven (polyhaven.com, CC0, no attribution required) and writes
them as the arena texture sets SourceArt/Environment/T_Env_<Set>_{BC,N,ORM}.png (replacing the procedural ones from
gen_env_textures*.py).

  python3 unreal/Tools/world/fetch_polyhaven.py [--res 2k] [--only Flagstone,Wall]

Maps: Diffuse -> BC (sRGB), nor_dx -> N (DirectX normal, as the importer expects), arm -> ORM (R AO, G roughness, B metal,
the same packing the materials use). Also writes SourceArt/Environment/polyhaven.json: asset id, physical size (m) and the
UV scale per material slot (slot repeat size from env_spec.TILE / scanned size), read by the Unreal material builder.
"""
import argparse
import json
import os
import shutil
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "Environment", "PolyHaven"))   # git-ignored (large)
sys.path.insert(0, HERE)
import env_spec as ES  # noqa: E402

# texture set -> Poly Haven asset id (chosen from contact sheets for a mountain-temple courtyard)
SETS = {
    "Flagstone": "rock_tile_floor",            # was precast_stone_paving (modern sidewalk slabs)
    "Wall": "japanese_stone_wall",
    "LedgeCap": "castle_wall_slates",
    "Plaster": "clay_plaster",
    "Timber": "brown_planks_07",
    "RoofTile": "grey_roof_tiles",
    "PoolTile": "floor_tiles_02",
    "MetalPlate": "metal_plate",
    "Ground": "dry_ground_rocks",
    "Rock": "dark_rock",
    "Cloth": "rough_linen",          # fighters' training pants (setup part metahuman), not an arena slot
}
MAPS = {"BC": "Diffuse", "N": "nor_dx", "ORM": "arm"}
API = "https://api.polyhaven.com"
UA = {"User-Agent": "Fourfold-asset-fetch/1.0"}


def _json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.load(r)


def _download(url, dst):
    tmp = dst + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(tmp, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="2k", choices=["1k", "2k", "4k"])
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {s for s in a.only.split(",") if s}
    manifest_path = os.path.join(OUT, "polyhaven.json")
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {"license": "CC0 (polyhaven.com)", "sets": {}}
    for tset, aid in SETS.items():
        if only and tset not in only:
            continue
        files = _json(f"{API}/files/{aid}")
        info = _json(f"{API}/info/{aid}")
        for kind, key in MAPS.items():
            url = files[key][a.res]["png"]["url"]
            dst = os.path.join(OUT, f"T_Env_{tset}_{kind}.png")
            print(f"{tset:10s} {kind:3s} <- {url}")
            _download(url, dst)
        size_m = max(info.get("dimensions") or [2000.0]) / 1000.0
        manifest["sets"][tset] = {"id": aid, "size_m": round(size_m, 3), "res": a.res, "authors": list((info.get("authors") or {}).keys())}
    # UV scale per material slot: a UV unit spans ES.TILE[slot] metres on the meshes
    manifest["uv_scale"] = {slot: round(ES.TILE[slot] / manifest["sets"][tset]["size_m"], 4)
                            for slot, tset in ES.SLOT_TEXTURES.items() if tset in manifest["sets"]}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("wrote", manifest_path)


if __name__ == "__main__":
    main()
