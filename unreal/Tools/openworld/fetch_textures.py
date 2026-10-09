#!/usr/bin/env python3
"""Downloads the open-world terrain texture sets (Poly Haven, CC0, no attribution required) into
unreal/SourceArt/OpenWorld/PolyHaven/T_OW_<Layer>_{BC,N,ORM}.png (git-ignored) + polyhaven.json (ids, physical sizes),
read by Content/Python/fourfold/openworld/materials.py.

  python3 unreal/Tools/openworld/fetch_textures.py [--res 2k] [--only Grass,Rock]

Layers follow the splat channels of gen_world.py: Grass (R), Rock (G), Sand (B), Ash (A), plus Snow (by height).
"""
import argparse
import json
import os
import shutil
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "OpenWorld", "PolyHaven"))
LAYERS = {
    "Grass": "aerial_grass_rock",
    "Rock": "rock_face",
    "Sand": "gravelly_sand",
    "Ash": "burned_ground_01",
    "Snow": "snow_02",
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
    os.makedirs(OUT, exist_ok=True)
    manifest_path = os.path.join(OUT, "polyhaven.json")
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {"license": "CC0 (polyhaven.com)", "layers": {}}
    for layer, aid in LAYERS.items():
        if only and layer not in only:
            continue
        files = _json(f"{API}/files/{aid}")
        info = _json(f"{API}/info/{aid}")
        for kind, key in MAPS.items():
            url = files[key][a.res]["png"]["url"]
            dst = os.path.join(OUT, f"T_OW_{layer}_{kind}.png")
            print(f"{layer:6s} {kind:3s} <- {url}")
            _download(url, dst)
        size_m = max(info.get("dimensions") or [2000.0]) / 1000.0
        manifest["layers"][layer] = {"id": aid, "size_m": round(size_m, 3), "res": a.res,
                                     "authors": list((info.get("authors") or {}).keys())}
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("wrote", manifest_path)


if __name__ == "__main__":
    main()
