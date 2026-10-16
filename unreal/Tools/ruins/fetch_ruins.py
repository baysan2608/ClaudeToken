"""Downloads Intel's "Jungle Ruins" sample scene and extracts only what the L_Ruins map uses (plain python3, no login).

  python3 -I unreal/Tools/ruins/fetch_ruins.py [--keep-zip] [--no-download]

Source: https://cdrdv2.intel.com/v1/dl/getContent/844047 (302 to a signed CDN link): JungleRuins_1_0_1b.zip, 4,254,165,506
bytes. Licence CC BY 4.0; credit "Jungle Ruins" by Cristiano Siqueira, with Charlene Teets / Intel Sample Library
(unreal/docs/CREDITS.md). Into unreal/SourceArt/OpenWorld/JungleRuins/ (git-ignored):
  download/<zip>            resumable (HTTP Range); deleted once every selected member was extracted with a matching size
                            and CRC, unless --keep-zip (re-running then downloads it again)
  extract/JungleRuins/...   108 files, 2.05 GB: the USD layers and textures of the pyramid, river, creek, cinematic terrain
                            and the jungle plants the Blender prep re-scatters (select_members)
  credits_license.txt       the licence file shipped in the zip
  ruins.json                source URL, size, sha256, licence, credit, changes, extracted files
Member names come from the archive (untrusted): only the KEEP_* prefixes are extracted and no path may leave extract/.
"""
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ruins_spec as RS  # noqa: E402

UNREAL = os.path.normpath(os.path.join(HERE, "..", ".."))
ROOT = os.path.join(UNREAL, "SourceArt", "OpenWorld", "JungleRuins")
ZIP = os.path.join(ROOT, "download", RS.ZIP_NAME)
EXTRACT = os.path.join(ROOT, "extract")
MANIFEST = os.path.join(ROOT, "ruins.json")
KEEP_USD = ("USD/JungleRuins_Karma.usda", "USD/cameras/defaultCam.usda", "USD/elements/Pyramid/", "USD/elements/River/",
            "USD/elements/Creek/", "USD/elements/Terrain/Terrain_Cinematic.usd", "USD/elements/RiverForest/",
            "USD/elements/RiverSapling/", "USD/elements/RiverSeedling/", "USD/elements/Shrub/", "USD/elements/Grass_A/",
            "USD/elements/Grass_B/", "USD/elements/Pyramid_Grass_B/")
KEEP_TEX = ("textures/Pyramid/", "textures/Terrain/Terrain_Cinematic_", "textures/Trees/PX_Chamaecyparis_Pisifera_Bark_01_",
            "textures/Trees/PX_Pinus_Rigida_Bark_02_", "textures/Trees/PX_Prunus_x_yedoensis_Leaf_03_",
            "textures/Plants/Grass_Medium_01_", "textures/Plants/Grass_Medium_02_", "textures/Plants/Shrub_04_")
KEEP_DOC = ("credits_license.txt", "Jungle Ruins_1.0.1b_HelpDoc.pdf", "Render_Cover.png")


def select_members(names):
    """Archive member names the pipeline needs, in archive order (directories and everything else skipped)."""
    out = []
    for n in names:
        if n.endswith("/") or not n.startswith("JungleRuins/"):
            continue
        rel = n[len("JungleRuins/"):]
        if ".." in rel.split("/"):
            continue
        if rel in KEEP_DOC or any(rel.startswith(p) for p in KEEP_USD + KEEP_TEX):
            out.append(n)
    return out


def safe_dest(out_dir, name):
    root = os.path.abspath(out_dir)
    dst = os.path.abspath(os.path.join(root, *name.split("/")))
    if os.path.commonpath([root, dst]) != root:
        raise ValueError(f"archive member escapes the extract folder: {name}")
    return dst


def download(url, dst, expected):
    """Resumable download into dst; returns once dst holds exactly `expected` bytes."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst) and os.path.getsize(dst) == expected:
        return dst
    part = dst + ".part"
    have = os.path.getsize(part) if os.path.exists(part) else 0
    headers = {"User-Agent": "fourfold-fetch/1"}
    if have:
        headers["Range"] = f"bytes={have}-"
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as r:
        resume = have and r.status == 206
        done = have if resume else 0
        t0 = time.time()
        with open(part, "ab" if resume else "wb") as f:
            while True:
                chunk = r.read(1 << 22)
                if not chunk:
                    break
                f.write(chunk)
                done += len(chunk)
                if time.time() - t0 > 10.0:
                    print(f"[fetch_ruins] {done / 1e9:.2f} / {expected / 1e9:.2f} GB", flush=True)
                    t0 = time.time()
    if os.path.getsize(part) != expected:
        raise RuntimeError(f"download incomplete: {os.path.getsize(part)} of {expected} bytes (run again to resume)")
    os.replace(part, dst)
    return dst


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def extract(zpath, members, out_dir):
    """Extracts members (CRC-checked by zipfile while reading); returns [(name, bytes)]. Raises on any mismatch."""
    done = []
    with zipfile.ZipFile(zpath) as z:
        for name in members:
            info = z.getinfo(name)
            dst = safe_dest(out_dir, name)
            if os.path.exists(dst) and os.path.getsize(dst) == info.file_size:
                done.append((name, info.file_size))
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            tmp = dst + ".tmp"
            with z.open(info) as src, open(tmp, "wb") as f:
                shutil.copyfileobj(src, f, 1 << 22)            # zipfile raises BadZipFile on a CRC mismatch
            if os.path.getsize(tmp) != info.file_size:
                raise RuntimeError(f"{name}: extracted {os.path.getsize(tmp)} of {info.file_size} bytes")
            os.replace(tmp, dst)
            done.append((name, info.file_size))
    return done


def extracted_ok(manifest):
    return all(os.path.exists(safe_dest(EXTRACT, f["name"])) and os.path.getsize(safe_dest(EXTRACT, f["name"])) == f["bytes"]
               for f in manifest.get("files", []))


def main(argv):
    keep_zip = "--keep-zip" in argv
    if not os.path.exists(ZIP) and os.path.exists(MANIFEST) and extracted_ok(json.load(open(MANIFEST))):
        print(f"[fetch_ruins] up to date: {MANIFEST} lists every extracted file")
        return 0
    if "--no-download" not in argv:
        download(RS.SOURCE_URL, ZIP, RS.ZIP_BYTES)
    if os.path.getsize(ZIP) != RS.ZIP_BYTES:
        raise RuntimeError(f"{ZIP}: {os.path.getsize(ZIP)} bytes, want {RS.ZIP_BYTES}")
    digest = sha256(ZIP)
    with zipfile.ZipFile(ZIP) as z:
        members = select_members(z.namelist())
    files = extract(ZIP, members, EXTRACT)
    shutil.copyfile(safe_dest(EXTRACT, "JungleRuins/credits_license.txt"), os.path.join(ROOT, "credits_license.txt"))
    manifest = {"source_url": RS.SOURCE_URL, "zip": RS.ZIP_NAME, "bytes": RS.ZIP_BYTES, "sha256": digest,
                "license": RS.LICENSE, "credit": RS.CREDIT, "changes": RS.CHANGES,
                "fetched": time.strftime("%Y-%m-%d %H:%M:%S"), "generator": "unreal/Tools/ruins/fetch_ruins.py",
                "files": [{"name": n, "bytes": b} for n, b in files]}
    with open(MANIFEST, "w") as f:
        json.dump(manifest, f, indent=1)
    total = sum(b for _n, b in files)
    print(f"[fetch_ruins] extracted {len(files)} files, {total:,} bytes into {EXTRACT}")
    if not keep_zip:
        os.remove(ZIP)
        print(f"[fetch_ruins] deleted {ZIP} after the verified extraction")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
