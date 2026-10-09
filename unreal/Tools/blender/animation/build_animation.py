"""Fourfold animation build - every clip of the catalogue: solve -> validate -> previews -> FBX -> clips.json.

    B=/home/user/tools/bpyenv/bin/python
    $B unreal/Tools/blender/animation/build_animation.py                      # all clips: validate, export, json, sheets
    $B .../build_animation.py --only "e_*,idle" --no-export --review /tmp/rv  # iterate: big review sheets only
    $B .../build_animation.py --videos P0                                     # + MP4 for every P0 clip
    $B .../build_animation.py --check-fbx                                     # re-import each FBX and compare

Outputs (owned by the animation stream):
    unreal/SourceArt/Animation/A_<clip>.fbx, previews/<clip>.jpg (+ .mp4), validation_report.json
    unreal/Content/Fourfold/Data/clips.json
Validation errors block the export of that clip (the build exits non-zero).
"""
import argparse
import fnmatch
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "common")))

import ffa_validate as validate  # noqa: E402
import clips as catalog  # noqa: E402
import ffa_rig as rig  # noqa: E402

UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ART = os.path.join(UNREAL, "SourceArt", "Animation")
PREV = os.path.join(ART, "previews")
DATA = os.path.join(UNREAL, "Content", "Fourfold", "Data")
ASSET_ROOT = "/Game/Fourfold/Characters/Fighter/Anims"
BASE_CLIP = {"e_seize": "e_seize_loop", "air": "fall", "lying": "knockdown", "f_grip": "f_thermal_hold", "f_draw": "f_heat_draw"}       # base poses that are not clips -> the loop clip that holds them
HAND_POSES = ["fist", "palm", "willow", "tiger", "crane", "sword", "oxtongue", "relaxed", "cup", "spread"]


def select(names, only, prio):
    out = []
    pats = [p.strip() for p in only.split(",")] if only else None
    for n in names:
        if pats and not any(fnmatch.fnmatch(n, p) for p in pats):
            continue
        out.append(n)
    return out


def sheet_frames(res):
    c = res.clip
    n = res.frames
    if c.loop or not c.contacts:
        return [(int(round(n * k / 4)), lab) for k, lab in enumerate(("start", "q1", "mid", "q3", "end"))]
    ct = c.contacts[0]
    keys = sorted({k[0] for k in c.keys})
    antic = c.antic if c.antic is not None else max([k for k in keys if k < ct - 1] or [max(0, ct // 2)])
    follow = c.follow if c.follow is not None else min(n, ct + max(3, (n - ct) // 3))
    return [(0, "start"), (antic, "antic"), (ct, "contact"), (follow, "follow"), (n, "end")]


def _base_clip(c, name):
    """clips.json 'base' must name a clip: pose-only bases map to the clip that holds that pose (a loop built on its
    own pose names itself)."""
    import clips as catalog
    b = BASE_CLIP.get(c.base, c.base)
    if b is not None and b not in catalog.CATALOG and c.loop:
        return name
    return b


def clip_entry(res):
    c = res.clip
    contacts = list(c.contacts)
    return {
        "asset": "A_" + res.name,
        "frames": res.frames,
        "duration": round(res.frames / 60.0, 6),
        "loop": bool(c.loop),
        "contact": contacts[0] if contacts else None,
        "contacts": contacts,
        "base": _base_clip(c, res.name),
        "speed": float(c.speed),
        "hands": {"l": c.hands[0], "r": c.hands[1]},
        "foot_plants": {s: [list(r) for r in res.plants.get(s, [])] for s in rig.SIDES},
        "priority": c.priority,
        "technique": c.technique,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--no-export", action="store_true")
    ap.add_argument("--no-sheets", action="store_true")
    ap.add_argument("--videos", default="", help="priority list (e.g. P0 or P0,P1) or 'all' for MP4 previews")
    ap.add_argument("--review", default="", help="directory for large review sheets (not committed)")
    ap.add_argument("--check-fbx", action="store_true")
    ap.add_argument("--no-json", action="store_true")
    # under Blender (`Blender -b --factory-startup --python build_animation.py -- <args>`) our args follow "--"
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    a = ap.parse_args(argv)
    cat = catalog.load()
    names = select(list(cat.keys()), a.only, None)
    os.makedirs(ART, exist_ok=True)
    os.makedirs(PREV, exist_ok=True)
    report_path = os.path.join(ART, "validation_report.json")
    report = {}
    if os.path.exists(report_path):
        try:
            report = json.load(open(report_path))
        except Exception:  # noqa: BLE001
            report = {}
    results = {}
    failed = []
    t0 = time.time()
    for n in names:
        res = cat[n]().build()
        results[n] = res
        v = validate.validate(res)
        v["stats"]["polish"] = getattr(res, "polish", 0)
        report[n] = v
        status = "OK" if not v["n_errors"] else f"FAIL ({v['n_errors']})"
        print(f"{n:22s} {res.frames:4d}f {status:10s} {v['stats']}", flush=True)
        for e in v["errors"][:6]:
            print("     !", e)
        for w in v["warnings"][:3]:
            print("     ~", w)
        if v["n_errors"]:
            failed.append(n)
    print(f"solved + validated {len(names)} clips in {time.time() - t0:.1f}s; failed: {failed}")
    # ---------------------------------------------------------------- previews
    vids = set(x.strip() for x in a.videos.split(",") if x.strip())
    if not a.no_sheets or a.review or vids:
        import ffa_render as render     # PIL: system python only (Blender's python has no PIL -> pass --no-sheets)
    for n in names:
        res = results[n]
        if n.startswith("hand_"):
            continue
        fr = sheet_frames(res)
        if not a.no_sheets:
            render.contact_sheet(res, fr, os.path.join(PREV, n + ".jpg"),
                                 title=f"{n}  ({res.clip.priority}, {res.frames}f{', loop' if res.clip.loop else ''})  "
                                       f"{res.clip.technique}")
        if a.review:
            render.contact_sheet(res, fr, os.path.join(a.review, n + ".png"), views=("game", "side", "front"),
                                 panel=(300, 340), title=f"{n} {res.clip.technique}")
        if vids and ("all" in vids or res.clip.priority in vids):
            render.video(res, os.path.join(PREV, n + ".mp4"))
    # ---------------------------------------------------------------- export
    if not a.no_export:
        import ffa_export as ex
        ex.reset_scene()
        for n in names:
            if n in failed:
                print("skip export (validation failed):", n)
                continue
            p = ex.export_clip(results[n], ART)
            report[n]["fbx"] = os.path.relpath(p, UNREAL)
            report[n]["fbx_bytes"] = os.path.getsize(p)
        if a.check_fbx:
            for n in names:
                if n in failed:
                    continue
                chk = ex.check_fbx(os.path.join(ART, f"A_{n}.fbx"), results[n])
                report[n]["fbx_check"] = chk
                print(f"{n:22s} fbx re-import {chk}")
            ex.reset_scene()
    with open(report_path, "w") as f:
        json.dump(report, f, indent=1, sort_keys=True)
    # ---------------------------------------------------------------- clips.json (merged) + anim_map.json
    if not a.no_json and not a.no_export:
        path = os.path.join(DATA, "clips.json")
        clips = {}
        if os.path.exists(path):
            try:
                clips = json.load(open(path)).get("clips", {})
            except Exception:  # noqa: BLE001
                clips = {}
        for n in names:
            if n in failed:
                continue
            clips[n] = clip_entry(results[n])
        # drop entries whose clip left the catalogue or whose FBX is gone
        clips = {n: c for n, c in clips.items() if n in cat and os.path.exists(os.path.join(ART, f"A_{n}.fbx"))}
        clips = dict(sorted(clips.items()))
        doc = {"schema": "fourfold.clips/1", "fps": 60, "rig": "ff-manny-1.0", "asset_root": ASSET_ROOT,
               "clips": clips, "hand_poses": {h: "hand_" + h for h in HAND_POSES}}
        os.makedirs(DATA, exist_ok=True)
        with open(path, "w") as f:
            json.dump(doc, f, indent=1)
        print("wrote clips.json with", len(clips), "clips")
        import anim_map_gen
        anim_map_gen.main()
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    import numpy as np
    with np.errstate(all="ignore"):   # numpy 2.0 + macOS Accelerate: spurious matmul FP warnings
        main()
