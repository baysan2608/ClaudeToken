"""Writes Content/Fourfold/Data/anim_map.json ("fourfold.anim_map/1", ARCHITECTURE.md §8.3) from anim_table.py,
resolving every clip against the clips that are actually exported (clips.json) through CLIP_FALLBACK, so the map only
ever names clips that exist.  Also writes docs/animation/COVERAGE.md (which moves still use a stand-in clip).

    python3 unreal/Tools/blender/animation/anim_map_gen.py          (plain Python 3, no Blender needed)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim_table as T  # noqa: E402

UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DATA = os.path.join(UNREAL, "Content", "Fourfold", "Data")
MOVE_INDEX = os.path.join(UNREAL, "Source", "FourfoldCore", "Data", "move_index.json")
MOVE_DEFS = os.path.join(UNREAL, "Source", "FourfoldCore", "Data", "moves.json")
COVERAGE = os.path.join(UNREAL, "docs", "animation", "COVERAGE.md")
SHARED_IDS = ("guard", "evade")
EVADE_DIRS = ("evade_fwd", "evade_back", "evade_l", "evade_r")


def map_key(m):
    """anim_map key of a move_index entry (the shared legacy ids get a per-element key)."""
    return f"{m['id']}@{m['element']}" if m["id"] in SHARED_IDS else m["id"]


def load_moves():
    with open(MOVE_INDEX) as f:
        return json.load(f)["moves"]


def load_def_ids():
    """Every action id the sim can run (moves.json base_ids + registered): the slot moves of move_index.json plus the
    chained actions the sim switches to (lightning, pour, vent, gust_grip, ...), which also need a map entry."""
    with open(MOVE_DEFS) as f:
        d = json.load(f)
    return set(d["base_ids"]) | set(d["registered"])


def load_clips(path=None):
    with open(path or os.path.join(DATA, "clips.json")) as f:
        return json.load(f)


def resolve(name, have):
    """Ideal clip -> the clip that exists (follows CLIP_FALLBACK; None when nothing in the chain exists)."""
    if name is None:
        return None
    if name == "evade_*":
        return name if any(d in have for d in EVADE_DIRS) else resolve("evade_fwd", have)
    seen = set()
    n = name
    while n is not None and n not in seen:
        if n in have:
            return n
        seen.add(n)
        n = T.CLIP_FALLBACK.get(n)
    return None


def build(clips_doc):
    have = set(clips_doc["clips"].keys())
    subs = []          # (key, field, ideal, used)

    def r(key, field, name):
        if name is None:
            return None
        got = resolve(name, have)
        if got != name:
            subs.append((key, field, name, got))
        return got

    moves = {}
    for key, e in T.MOVES.items():
        out = {}
        for fld in ("startup", "hold", "release", "perfect"):
            v = r(key, fld, e[fld])
            if v:
                out[fld] = v
        out["hands"] = {"l": e["hands"][0], "r": e["hands"][1]}
        if e["tiers"]:
            tiers = {}
            for t, d in sorted(e["tiers"].items()):
                td = {}
                for fld in ("startup", "hold", "release"):
                    if fld in d:
                        v = r(f"{key} T{t}", fld, d[fld])
                        if v:
                            td[fld] = v
                if "hands" in d:
                    td["hands"] = {"l": d["hands"][0], "r": d["hands"][1]}
                tiers[str(t)] = td
            out["tiers"] = tiers
        if e["modes"]:
            modes = {}
            for m, d in e["modes"].items():
                md = {}
                for fld in ("startup", "hold", "release"):
                    if fld in d:
                        v = r(f"{key} [{m}]", fld, d[fld])
                        if v:
                            md[fld] = v
                if "hands" in d:
                    md["hands"] = {"l": d["hands"][0], "r": d["hands"][1]}
                modes[m] = md
            out["modes"] = modes
        moves[key] = out

    def rr(group, d):
        out = {}
        for k, v in d.items():
            got = r(group + "." + k, "", v)
            if got:                      # nothing in the chain exists yet: leave the key to the runtime default
                out[k] = got
        return out

    loco = dict(T.LOCOMOTION)
    loco = {k: (v if isinstance(v, list) else v) for k, v in loco.items()}
    loco_out = {}
    for k, v in loco.items():
        loco_out[k] = [r("locomotion.stance", str(i), s) for i, s in enumerate(v)] if isinstance(v, list) else r("locomotion." + k, "", v)
    doc = {
        "schema": "fourfold.anim_map/1",
        "locomotion": loco_out,
        "reactions": rr("reactions", T.REACTIONS),
        "air": rr("air", T.AIR),
        "guard": {"default": r("guard.default", "", T.GUARD["default"]),
                  "by_element": [r("guard.by_element", str(i), g) for i, g in enumerate(T.GUARD["by_element"])]},
        "modes": rr("modes", T.MODES),
        "moves": moves,
        "fallbacks": {"slot": rr("fallbacks.slot", T.SLOT_FALLBACKS),
                      "clips": {k: resolve(v, have) for k, v in sorted(T.CLIP_FALLBACK.items())
                                if k not in have and resolve(v, have)}},
    }
    return doc, subs


def write_coverage(doc, subs, clips_doc, moves_idx):
    have = clips_doc["clips"]
    by_p = {}
    for n, c in have.items():
        by_p.setdefault(c.get("priority", "?"), []).append(n)
    lines = ["# Animation coverage (generated by Tools/blender/animation/anim_map_gen.py - do not edit)", "",
             f"Exported clips: **{len(have)}** (" + ", ".join(f"{p}: {len(v)}" for p, v in sorted(by_p.items())) + ").",
             f"Map entries: **{len(doc['moves'])}**: all {len(moves_idx)} sim slot moves (the shared `guard` / `evade` "
             "ids as per-element keys) + the chained actions the sim switches to ("
             + ", ".join(f"`{k}`" for k in sorted(set(doc['moves']) - {map_key(m) for m in moves_idx})) + ").", ""]
    if subs:
        lines += ["## Stand-ins in use", "", "Moves (or map groups) whose ideal clip (MARTIAL_ARTS.md §4) is not built yet "
                  "and play a fallback clip:", "", "| where | field | ideal clip | plays |", "|---|---|---|---|"]
        for k, f, ideal, got in subs:
            lines.append(f"| `{k}` | {f} | `{ideal}` | `{got}` |")
    else:
        lines += ["Every move plays its ideal clip (no stand-ins)."]
    lines.append("")
    with open(COVERAGE, "w") as f:
        f.write("\n".join(lines))


CLIPS_MD = os.path.join(UNREAL, "docs", "animation", "CLIPS.md")
GROUPS = [("Shared (locomotion, reactions, air, modes)", lambda n: not n[:2] in ("e_", "w_", "f_", "l_", "c_", "a_")
           and not n.startswith("hand_")), ("Earth - Hung Gar", lambda n: n.startswith("e_")),
          ("Water - Tai Chi", lambda n: n.startswith("w_")), ("Fire - Northern Shaolin", lambda n: n.startswith("f_")),
          ("Lightning / Combustion", lambda n: n[:2] in ("l_", "c_")), ("Air - Baguazhang", lambda n: n.startswith("a_")),
          ("Hand shapes", lambda n: n.startswith("hand_"))]


def write_clip_table(clips_doc, doc):
    """docs/animation/CLIPS.md: every exported clip with its timing, technique and the moves that use it."""
    users = {}
    for key, e in doc["moves"].items():
        names = [e.get(f) for f in ("startup", "hold", "release", "perfect")]
        for t in e.get("tiers", {}).values():
            names += [t.get(f) for f in ("startup", "hold", "release")]
        for m in e.get("modes", {}).values():
            names += [m.get(f) for f in ("startup", "hold", "release")]
        for n in names:
            for nn in (EVADE_DIRS if n == "evade_*" else [n] if n else []):
                users.setdefault(nn, set()).add(key)
    for grp in ("locomotion", "reactions", "air", "modes"):
        for k, v in doc[grp].items():
            for n in (v if isinstance(v, list) else [v]):
                users.setdefault(n, set()).add(f"{grp}.{k}")
    lines = ["# Clip catalogue (generated by Tools/blender/animation/anim_map_gen.py from clips.json - do not edit)", "",
             "Frames at 60 fps; `c` = contact frame(s); base = the stance the clip starts / ends on. Previews: "
             "`SourceArt/Animation/previews/<clip>.jpg` (start, anticipation, contact, follow-through, end; gameplay "
             "camera row + side row) and `<clip>.mp4` for P0.", ""]
    clips = clips_doc["clips"]
    for title, pred in GROUPS:
        names = [n for n in sorted(clips) if pred(n)]
        if not names:
            continue
        lines += [f"## {title}", "", "| clip | P | frames | c | loop | base | hands L/R | technique | used by |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for n in names:
            c = clips[n]
            ct = ", ".join(str(x) for x in c["contacts"]) or "-"
            u = sorted(users.get(n, []))
            us = ", ".join(f"`{x}`" for x in u[:8]) + (f" +{len(u) - 8}" if len(u) > 8 else "")
            lines.append(f"| `{n}` | {c['priority']} | {c['frames']} | {ct} | {'yes' if c['loop'] else ''} | "
                         f"{c['base'] or '-'} | {c['hands']['l']}/{c['hands']['r']} | {c['technique']} | {us or '-'} |")
        lines.append("")
    with open(CLIPS_MD, "w") as f:
        f.write("\n".join(lines))


def main():
    clips_doc = load_clips()
    moves_idx = load_moves()
    doc, subs = build(clips_doc)
    missing = [map_key(m) for m in moves_idx if map_key(m) not in doc["moves"]]
    if missing:
        print("ERROR: moves without a map entry:", missing)
        sys.exit(1)
    defs = load_def_ids()
    unknown = [k for k in doc["moves"] if k.split("@")[0] not in defs]
    if unknown:
        print("ERROR: map entries that are not sim action ids:", unknown)
        sys.exit(1)
    with open(os.path.join(DATA, "anim_map.json"), "w") as f:
        json.dump(doc, f, indent=1)
    os.makedirs(os.path.dirname(COVERAGE), exist_ok=True)
    write_coverage(doc, subs, clips_doc, moves_idx)
    write_clip_table(clips_doc, doc)
    print(f"anim_map.json: {len(doc['moves'])} move entries, {len(subs)} stand-in substitutions")


if __name__ == "__main__":
    main()
