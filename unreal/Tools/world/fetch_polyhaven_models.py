#!/usr/bin/env python3
"""Downloads photo-scanned CC0 models from Poly Haven (polyhaven.com, CC0, no attribution required) for the arena scenery:
the .blend + its textures into SourceArt/Environment/PolyHaven/Models/<asset id>/ (git-ignored, large), plus the DirectX normal
maps the Unreal importer expects. Blender then turns them into game meshes (Tools/blender/world/prep_trees.py).
The .blend, not the FBX: Poly Haven's FBX exports of these trees carry no UV layers (no LayerElementUV), so the twig cards
could not be textured. Open the .blend files only with --disable-autoexec (prep_trees.py's command line does).

  python3 unreal/Tools/world/fetch_polyhaven_models.py [--res 2k] [--only fir_tree_01,tree_small_02]

Writes Models/models.json: asset id -> {res, dimensions_mm, polycount, files}.
"""
import argparse
import json
import os
import shutil
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "Environment", "PolyHaven", "Models"))
API = "https://api.polyhaven.com"
UA = {"User-Agent": "Fourfold-asset-fetch/1.0"}

# scenery role -> Poly Haven asset ids (chosen from thumbnails for a mountain-temple courtyard; each file holds 1-5 variants)
MODELS = {
    "conifer": ["fir_tree_01", "fir_sapling_medium"],
    "broadleaf": ["tree_small_02", "island_tree_02"],
    "shrub": ["shrub_01", "shrub_03", "shrub_04"],
    # scanned rock faces scaled x20-90 as the distant mountain ring (Tools/blender/world/prep_rocks.py)
    "mountain": ["mountainside", "namaqualand_cliff_02", "coastal_cliff_02"],
}


def _json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.load(r)


def _download(url, dst):
    if os.path.exists(dst):
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=600) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(tmp, dst)
    return True


def fetch(aid, res):
    files = _json(f"{API}/files/{aid}")
    info = _json(f"{API}/info/{aid}")
    d = os.path.join(OUT, aid)
    src = files["blend"][res]["blend"]
    got = []
    if _download(src["url"], os.path.join(d, os.path.basename(src["url"]))):
        got.append(os.path.basename(src["url"]))
    for rel, f in src.get("include", {}).items():
        if _download(f["url"], os.path.join(d, rel)):
            got.append(rel)
    # DirectX normals + packed AO/rough/metal per map set (the FBX ships GL normals and separate maps)
    for key, by_res in files.items():
        if key.endswith("_nor_dx") or key.endswith("_arm") or key == "nor_dx" or key == "arm":
            png = by_res.get(res, {}).get("png")
            if png and _download(png["url"], os.path.join(d, "textures", os.path.basename(png["url"]))):
                got.append(os.path.basename(png["url"]))
    print(f"{aid}: {len(got)} new files")
    return {"res": res, "dimensions_mm": info.get("dimensions"), "polycount": info.get("polycount"),
            "blend": os.path.basename(src["url"]), "authors": list((info.get("authors") or {}).keys())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="2k", choices=["1k", "2k", "4k"])
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {s for s in a.only.split(",") if s}
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "models.json")
    manifest = json.load(open(path)) if os.path.exists(path) else {"license": "CC0 (polyhaven.com)", "models": {}}
    for role, ids in MODELS.items():
        for aid in ids:
            if only and aid not in only:
                continue
            manifest["models"][aid] = dict(fetch(aid, a.res), role=role)
    with open(path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("wrote", path)


if __name__ == "__main__":
    sys.exit(main())
