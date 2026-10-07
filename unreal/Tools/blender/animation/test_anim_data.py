"""Self-test of the animation data (plain Python 3):  python3 unreal/Tools/blender/animation/test_anim_data.py

Asserts (ARCHITECTURE.md §8.3):
  * clips.json schema keys; every clip entry has asset / frames / duration / loop / contact / contacts / base / speed /
    hands / foot_plants / priority / technique with sane values (contacts inside the clip, loops without contact,
    plant ranges ordered and inside the clip, hand shapes known, gait clips with a design speed);
  * every clip has its FBX in SourceArt/Animation (A_<clip>.fbx, non-empty, FBX binary header);
  * the ten hand-pose clips exist;
  * anim_map.json: schema, every sim move of move_index.json present (shared ids as guard@<el> / evade@<el>), every
    referenced clip exists in clips.json, tiers 0..3, hands known;
  * timing: a startup clip's contact fits the move's startup within the runtime rate clamp (0.6-1.6) - warnings only.
Exit code 1 on any failure.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim_map_gen as G  # noqa: E402

UNREAL = G.UNREAL
ART = os.path.join(UNREAL, "SourceArt", "Animation")
SHAPES = ["fist", "palm", "willow", "tiger", "crane", "sword", "oxtongue", "relaxed", "cup", "spread"]
CLIP_KEYS = ["asset", "frames", "duration", "loop", "contact", "contacts", "base", "speed", "hands", "foot_plants",
             "priority", "technique"]
GAITS = {"walk": 1.4, "run": 5.5}
# Actions the sim switches to with start_action / morph_action (not slot moves of move_index.json) -> element.
# Keep in sync with Source/FourfoldCore (grep start_action / morph_action); logic_tests.cpp scans the sources for them.
CHAINED = {"lightning": 2, "pour": 2, "vent": 2, "gust_grip": 3, "flare_dash": 2}
ELEMENT_PREFIX = {"e_": 0, "w_": 1, "f_": 2, "l_": 2, "c_": 2, "a_": 3}

fails, warns = [], []


def check(cond, msg):
    if not cond:
        fails.append(msg)


def main():
    clips_doc = G.load_clips()
    amap = json.load(open(os.path.join(G.DATA, "anim_map.json")))
    moves = G.load_moves()
    # ------------------------------------------------------------------ clips.json
    check(clips_doc.get("schema") == "fourfold.clips/1", "clips.json schema")
    check(clips_doc.get("fps") == 60, "clips.json fps must be 60")
    check(clips_doc.get("rig") == "ff-manny-1.0", "clips.json rig")
    check(clips_doc.get("asset_root") == "/Game/Fourfold/Characters/Fighter/Anims", "clips.json asset_root")
    clips = clips_doc["clips"]
    for n, c in clips.items():
        for k in CLIP_KEYS:
            check(k in c, f"clip {n}: key {k} missing")
        if any(k not in c for k in CLIP_KEYS):
            continue
        check(c["asset"] == "A_" + n, f"clip {n}: asset name {c['asset']}")
        fr = c["frames"]
        check(isinstance(fr, int) and fr >= 1, f"clip {n}: frames {fr}")
        check(abs(c["duration"] - fr / 60.0) < 1e-4, f"clip {n}: duration {c['duration']} != frames / 60")
        if c["loop"]:
            check(c["contact"] is None and c["contacts"] == [], f"clip {n}: a loop has no contact")
        if c["contacts"]:
            check(c["contact"] == c["contacts"][0], f"clip {n}: contact != contacts[0]")
            check(all(0 <= x <= fr for x in c["contacts"]), f"clip {n}: contact outside the clip")
            check(c["contacts"] == sorted(c["contacts"]), f"clip {n}: contacts not ordered")
        check(c["hands"].get("l") in SHAPES and c["hands"].get("r") in SHAPES, f"clip {n}: hands {c['hands']}")
        check(c["base"] is None or c["base"] in clips or c["base"] in ("guard", "idle", "e_seize"),
              f"clip {n}: base {c['base']} is not a clip")
        for s in ("l", "r"):
            rngs = c["foot_plants"].get(s)
            check(isinstance(rngs, list), f"clip {n}: foot_plants.{s}")
            last = -1
            for a, b in rngs or []:
                check(0 <= a < b <= fr and a >= last, f"clip {n}: plant range {a, b} of {s}")
                last = b
        check(c["priority"] in ("P0", "P1", "P2"), f"clip {n}: priority")
        if n in GAITS:
            check(abs(c["speed"] - GAITS[n]) < 1e-6, f"clip {n}: design speed {c['speed']}")
        fbx = os.path.join(ART, c["asset"] + ".fbx")
        ok = os.path.exists(fbx) and os.path.getsize(fbx) > 1000
        if ok:
            with open(fbx, "rb") as f:
                ok = f.read(20).startswith(b"Kaydara FBX Binary")
        check(ok, f"clip {n}: FBX {fbx} missing or not a binary FBX")
    for s in SHAPES:
        check("hand_" + s in clips, f"hand pose clip hand_{s} missing")
        check(clips_doc["hand_poses"].get(s) == "hand_" + s, f"hand_poses.{s}")
    # ------------------------------------------------------------------ anim_map.json
    check(amap.get("schema") == "fourfold.anim_map/1", "anim_map schema")
    for k in ("locomotion", "reactions", "air", "guard", "modes", "moves", "fallbacks"):
        check(k in amap, f"anim_map: {k} missing")

    def ref(where, name):
        if name == "evade_*":
            check(any(d in clips for d in G.EVADE_DIRS), f"{where}: evade_* without directional evade clips")
            return
        check(isinstance(name, str) and name in clips, f"{where}: clip {name!r} not in clips.json")

    lo = amap["locomotion"]
    for k in ("idle", "walk", "run", "strafe_l", "strafe_r", "back"):
        ref("locomotion." + k, lo.get(k))
    check(len(lo.get("stance", [])) == 4, "locomotion.stance needs 4 entries")
    for i, s in enumerate(lo.get("stance", [])):
        ref(f"locomotion.stance[{i}]", s)
    for grp in ("reactions", "air", "modes"):
        for k, v in amap[grp].items():
            ref(f"{grp}.{k}", v)
    ref("guard.default", amap["guard"]["default"])
    check(len(amap["guard"]["by_element"]) == 4, "guard.by_element needs 4 entries")
    for i, g in enumerate(amap["guard"]["by_element"]):
        ref(f"guard.by_element[{i}]", g)
    for k, v in amap["fallbacks"]["slot"].items():
        ref("fallbacks.slot." + k, v)
    for k, v in amap["fallbacks"].get("clips", {}).items():
        ref("fallbacks.clips." + k, v)
    mv = amap["moves"]
    timing = {}
    for m in moves:
        key = G.map_key(m)
        check(key in mv, f"move {key} ({m['name']}) has no anim_map entry")
        timing[key] = m
    def_ids = G.load_def_ids()
    for cid in CHAINED:
        check(cid in mv, f"chained action {cid} has no anim_map entry (the fighter would drop to its stance)")
    for key, e in mv.items():
        check(key in timing or key.split("@")[0] in def_ids, f"anim_map move {key} is not a sim action id")
        if key in CHAINED:
            el = CHAINED[key]
            for fld in ("startup", "hold", "release"):
                n = e.get(fld)
                if n and n != "evade_*" and n[:2] in ELEMENT_PREFIX:
                    check(ELEMENT_PREFIX[n[:2]] == el, f"chained action {key}.{fld} = {n} is a clip of another element")
        for fld in ("startup", "hold", "release", "perfect"):
            if fld in e:
                ref(f"moves.{key}.{fld}", e[fld])
        check(e["hands"]["l"] in SHAPES and e["hands"]["r"] in SHAPES, f"moves.{key}.hands")
        for t, d in e.get("tiers", {}).items():
            check(t in ("0", "1", "2", "3"), f"moves.{key}.tiers key {t}")
            for fld in ("startup", "hold", "release"):
                if fld in d:
                    ref(f"moves.{key}.tiers.{t}.{fld}", d[fld])
        for md, d in e.get("modes", {}).items():
            check(md == md.lower(), f"moves.{key}.modes.{md} must be lower-case")
            for fld in ("startup", "hold", "release"):
                if fld in d:
                    ref(f"moves.{key}.modes.{md}.{fld}", d[fld])
        # timing: rate = contact / startup must stay inside the runtime clamp
        m = timing.get(key)
        su = e.get("startup")
        if m and su and su != "evade_*" and su in clips and m["startup"] > 0.03:
            c = clips[su]
            if c["contact"] is not None:
                rate = (c["contact"] / 60.0) / m["startup"]
                if not 0.6 <= rate <= 1.6:
                    warns.append(f"{key}: {su} contact {c['contact']}f vs startup {m['startup'] * 60:.0f}f "
                                 f"(rate {rate:.2f}, clamped)")
    # ------------------------------------------------------------------ report
    for w in warns:
        print("warn:", w)
    if fails:
        for f in fails[:80]:
            print("FAIL:", f)
        print(f"{len(fails)} failures")
        sys.exit(1)
    print(f"OK: {len(clips)} clips with FBX, {len(mv)} move entries covering all {len(moves)} sim moves, "
          f"{len(warns)} timing warnings")


if __name__ == "__main__":
    main()
