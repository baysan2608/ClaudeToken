#!/usr/bin/env python3
"""Generates FFGAnimDefaults.gen.cpp: the built-in clip catalogue and move -> clip map of the native anim runtime,
parsed from unreal/docs/MARTIAL_ARTS.md (§3 clip catalogue, §4 move map). The runtime uses these defaults when
Content/Fourfold/Data/clips.json / anim_map.json are missing or lack an entry; the JSON files (stream `animation`)
override them key by key.  Run:  python3 gen_anim_defaults.py  (deterministic output, committed)."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))  # unreal/
DOC = os.path.join(ROOT, "docs", "MARTIAL_ARTS.md")
OUT = os.path.join(HERE, "..", "FFGAnimDefaults.gen.cpp")

TICK = re.compile(r"`([a-z0-9_*]+)`")


def rows(section_lines):
    for ln in section_lines:
        if not ln.startswith("|") or ln.startswith("|---") or ln.startswith("| clip") or ln.startswith("| slot"):
            continue
        cells = [c.strip() for c in ln.strip().strip("|").split("|")]
        yield cells


def parse_doc(text):
    lines = text.splitlines()
    sec3 = []
    sec4 = []
    cur = None
    sub = None
    for ln in lines:
        if ln.startswith("## 3."):
            cur = 3
        elif ln.startswith("## 4."):
            cur = 4
        elif ln.startswith("## ") and not ln.startswith("## 3.") and not ln.startswith("## 4."):
            cur = None
        if ln.startswith("### "):
            sub = ln
        if cur == 3:
            sec3.append((sub, ln))
        elif cur == 4:
            sec4.append((sub, ln))
    return sec3, sec4


def clip_catalogue(sec3):
    clips = {}
    hand_names = ["hand_fist", "hand_palm", "hand_willow", "hand_tiger", "hand_crane", "hand_sword", "hand_oxtongue",
                  "hand_relaxed", "hand_cup", "hand_spread"]
    for sub, ln in sec3:
        if not ln.startswith("| `"):
            continue
        cells = [c.strip() for c in ln.strip().strip("|").split("|")]
        names = TICK.findall(cells[0])
        if "hand_fist" in names:
            names = hand_names
        shared = sub.startswith("### 3.1")
        if shared:
            frames_s, loop_s, c_s = cells[2], cells[3], cells[4]
        else:
            frames_s, c_s = cells[2], cells[3]
            loop_s = "✓" if "loop" in frames_s else ""
        m = re.match(r"(\d+)", frames_s)
        frames = int(m.group(1)) if m else 0
        loop = "✓" in loop_s or "loop" in frames_s
        contacts = [int(x) for x in re.findall(r"\d+", c_s)] if c_s not in ("–", "-", "") else []
        for n in names:
            clips[n] = {"frames": frames, "loop": loop, "contacts": contacts}
    return clips


SPEEDS = {"walk": 1.4, "run": 5.5, "strafe_l": 1.1, "strafe_r": 1.1, "walk_back": 1.0}
CLIP_FALLBACKS = {}


def hands_of(cell):
    t = re.sub(r"\([^)]*\)", "", cell)
    t = t.split("→")[0]
    parts = [p.strip() for p in t.split("/") if p.strip()]
    if not parts:
        return ("", "")
    if len(parts) == 1:
        return (parts[0], parts[0])
    return (parts[0], parts[1])


def parse_clips_cell(slot, cell, loops):
    e = {"startup": "", "hold": "", "release": "", "perfect": "", "tiers": {}, "modes": {}}
    # fallbacks "(fallback `x`)"
    for m in re.finditer(r"`([a-z0-9_]+)`\s*\(fallback `([a-z0-9_]+)`\)", cell):
        CLIP_FALLBACKS[m.group(1)] = m.group(2)
    c = re.sub(r"\(fallback `[a-z0-9_]+`\)", "", cell)
    # Thermal modes: "HEAT `a` → `b`; DRAW / SCORCH `c`; VENT `d`"
    if re.search(r"\b(HEAT|DRAW|VENT|SCORCH)\b", c):
        first = None
        for part in c.split(";"):
            names = TICK.findall(part)
            modes = re.findall(r"\b(HEAT|DRAW|VENT|SCORCH)\b", part)
            sub_e = {}
            if len(names) >= 2:
                sub_e = {"hold": names[0], "release": names[1]}
            elif len(names) == 1:
                sub_e = {"hold": names[0]} if names[0] in loops else {"startup": names[0]}
            for md in modes:
                e["modes"][md.lower()] = sub_e
                if first is None:
                    first = sub_e
        if first:
            e.update(first)
        return e
    # charge / tiers in parentheses or after ';'
    tiers = {}
    charge = None
    # "charge `x`" anywhere
    m = re.search(r"charge `([a-z0-9_]+)`(?:\s*→\s*`([a-z0-9_]+)`)?", c)
    if m:
        charge = m.group(1)
        if m.group(2):
            tiers.setdefault(1, {})["release"] = m.group(2)
    for m in re.finditer(r"T(\d)(?:/T(\d))?(\+)?:?\s*`([a-z0-9_]+)`", c):
        t1 = int(m.group(1))
        t2 = int(m.group(2)) if m.group(2) else None
        if m.group(1) == "0":
            continue
        tiers.setdefault(t1, {})["release"] = m.group(4)
        if t2:
            tiers.setdefault(t2, {})["release"] = m.group(4)
    # T+A shape clip -> ignored (shape is not an action phase)
    c_main = re.sub(r"\(T\+A `[a-z0-9_]+`\)", "", c)
    # main chain: text before the first '(' or ';' that contains charge / tier info
    main = c_main
    if c_main.startswith("T0"):
        main = c_main.split(";")[0]
    else:
        main = re.split(r"\(|;", c_main)[0]
    names = TICK.findall(main)
    if not names:
        names = TICK.findall(c_main)[:1]
    if slot == "guard":
        if len(names) >= 2 and names[1] in ("deflect", "l_redirect"):
            e["hold"], e["perfect"] = names[0], names[1]
        elif len(names) >= 2:
            e["startup"], e["hold"] = names[0], names[1]
        elif names:
            e["hold"] = names[0]
    elif len(names) >= 3:
        e["startup"], e["hold"], e["release"] = names[0], names[1], names[2]
    elif len(names) == 2:
        a, b = names
        if a in loops:
            e["hold"], e["release"] = a, b
        elif b in loops:
            e["startup"], e["hold"] = a, b
        else:
            e["startup"], e["release"] = a, b
    elif len(names) == 1:
        n = names[0]
        if n in loops and slot in ("evade_hold", "tech", "sink", "guard"):
            e["hold"] = n
        elif n in loops:
            e["hold"] = n
        else:
            e["startup"] = n
    if charge:
        e["hold"] = charge
    if c_main.startswith("T0") and "T1+" in c_main and not tiers:
        pass
    e["tiers"] = tiers
    return e


def move_map(sec4, loops):
    moves = {}
    for sub, ln in sec4:
        if not ln.startswith("| ") or ln.startswith("| slot") or ln.startswith("|---"):
            continue
        cells = [c.strip() for c in ln.strip().strip("|").split("|")]
        if len(cells) < 5:
            continue
        slot = cells[0]
        ids = TICK.findall(cells[1])
        if not ids:
            continue
        mid = ids[0]
        e = parse_clips_cell(slot, cells[3], loops)
        hl, hr = hands_of(cells[4])
        e["hands"] = (hl, hr)
        e["slot"] = slot
        key = mid
        if mid == "guard" or mid == "evade":
            # legacy shared ids: keep per element, keyed by the section's element
            el = {"Earth": 0, "Water": 1, "Fire": 2, "Air": 3}[re.search(r"### 4\.\d+ (\w+)", sub).group(1)]
            key = "%s@%d" % (mid, el)
        if key in moves:
            continue
        moves[key] = e
    return moves


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    text = open(DOC, encoding="utf-8").read()
    sec3, sec4 = parse_doc(text)
    clips = clip_catalogue(sec3)
    loops = {n for n, c in clips.items() if c["loop"]}
    moves = move_map(sec4, loops)
    out = []
    out.append("// GENERATED by Private/Logic/tools/gen_anim_defaults.py from unreal/docs/MARTIAL_ARTS.md - do not edit by hand.")
    out.append("// Built-in clip catalogue + move -> clip map of the native anim runtime (overridden key by key by")
    out.append("// Content/Fourfold/Data/clips.json and anim_map.json).")
    out.append('#include "FFGAnimLibrary.h"')
    out.append("")
    out.append("namespace ffg {")
    out.append("")
    out.append("void AddBuiltinAnimDefaults(AnimLibrary& lib) {")
    for n in sorted(clips):
        c = clips[n]
        contacts = "{" + ", ".join(str(x) for x in c["contacts"]) + "}"
        spd = SPEEDS.get(n, 0.0)
        out.append("\tlib.AddDefaultClip(%s, %d, %s, %s, %sf);" % (cstr(n), c["frames"], "true" if c["loop"] else "false",
                                                                   contacts, repr(float(spd))))
    for a in sorted(CLIP_FALLBACKS):
        out.append("\tlib.AddClipFallback(%s, %s);" % (cstr(a), cstr(CLIP_FALLBACKS[a])))
    for k in sorted(moves):
        e = moves[k]
        out.append("\t{")
        out.append("\t\tMoveAnimEntry& m = lib.DefaultMove(%s);" % cstr(k))
        for f in ("startup", "hold", "release", "perfect"):
            if e[f]:
                out.append("\t\tm.base.%s = %s;" % (f, cstr(e[f])))
        if e["hands"][0] or e["hands"][1]:
            out.append("\t\tm.base.hand_l = %s;" % cstr(e["hands"][0]))
            out.append("\t\tm.base.hand_r = %s;" % cstr(e["hands"][1]))
        for t in sorted(e["tiers"]):
            for f, v in sorted(e["tiers"][t].items()):
                out.append("\t\tm.tiers[%d].%s = %s;" % (t, f, cstr(v)))
        for md in sorted(e["modes"]):
            for f, v in sorted(e["modes"][md].items()):
                out.append("\t\tm.modes[%s].%s = %s;" % (cstr(md), f, cstr(v)))
        out.append("\t}")
    out.append("}")
    out.append("")
    out.append("}  // namespace ffg")
    open(OUT, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("clips %d, moves %d, clip fallbacks %d -> %s" % (len(clips), len(moves), len(CLIP_FALLBACKS), OUT))


if __name__ == "__main__":
    sys.exit(main())
