#!/usr/bin/env python3
"""Downloads the open-world ground-detail scans (Poly Haven, CC0, no attribution required): grass, small plants and boulders,
the .blend + its textures into unreal/SourceArt/OpenWorld/Models/<asset id>/ (git-ignored, ~400 MB at 2k), plus the DirectX
normal and packed AO/rough/metal PNGs. Tools/openworld/prep_vegetation.py (Blender) turns them into game meshes.

The .blend, not the FBX: Poly Haven's FBX exports of the foliage carry no UV layers on the cards (same reason as
Tools/world/fetch_polyhaven_models.py). Open the .blend files only with --disable-autoexec (prep_vegetation.py's command
line does). The shrubs (shrub_01/03/04) are not fetched here: they already live in SourceArt/Environment/PolyHaven/Models
(Tools/world/fetch_polyhaven_models.py) and prep_vegetation.py reads them from there.

  python3 unreal/Tools/openworld/fetch_models.py [--res 2k] [--only fern_02,boulder_01]

Writes Models/models.json: asset id -> {kind, res, dimensions_mm, polycount, blend, authors, license, url}.
"""
import argparse
import json
import os
import shutil
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "SourceArt", "OpenWorld", "Models"))
API = "https://api.polyhaven.com"
UA = {"User-Agent": "Fourfold-asset-fetch/1.0"}

# kind -> Poly Haven asset ids
MODELS = {
    "grass": ["grass_bermuda_01", "grass_medium_02"],
    "plant": ["fern_02", "flower_empodium", "dandelion_01"],
    "boulder": ["namaqualand_boulder_02", "namaqualand_boulder_03", "namaqualand_boulder_04", "namaqualand_boulder_05",
                "namaqualand_boulder_06", "boulder_01"],
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
    # DirectX normals + packed AO/rough/metal (+ alpha / dry variants as PNG) per map set
    for key, by_res in files.items():
        k = key.lower()
        if k.endswith("nor_dx") or k.endswith("arm") or k == "alpha":
            png = by_res.get(res, {}).get("png")
            if png and _download(png["url"], os.path.join(d, "textures", os.path.basename(png["url"]))):
                got.append(os.path.basename(png["url"]))
    print(f"{aid}: {len(got)} new files")
    return {"res": res, "dimensions_mm": info.get("dimensions"), "polycount": info.get("polycount"),
            "blend": os.path.basename(src["url"]), "authors": list((info.get("authors") or {}).keys()),
            "license": "CC0", "url": f"https://polyhaven.com/a/{aid}"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default="2k", choices=["1k", "2k", "4k"])
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {s for s in a.only.split(",") if s}
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "models.json")
    manifest = json.load(open(path)) if os.path.exists(path) else {"license": "CC0 (polyhaven.com)", "models": {}}
    for kind, ids in MODELS.items():
        for aid in ids:
            if only and aid not in only:
                continue
            manifest["models"][aid] = dict(fetch(aid, a.res), kind=kind)
    with open(path, "w") as f:
        json.dump(manifest, f, indent=1)
    print("wrote", path)


if __name__ == "__main__":
    sys.exit(main())
