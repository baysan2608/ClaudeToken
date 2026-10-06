#!/usr/bin/env python3
"""Build Content/Fourfold/Data/sfx_manifest.json from the generator index (SourceArt/Audio/sound_index.json) and the rule data
(event_rules.py), and validate it.

    /home/user/tools/bpyenv/bin/python unreal/Tools/audio/build_manifest.py            # write + validate
    ... build_manifest.py --check                                                      # validate only (exit 1 on any problem)

Validation: every sound a rule can play exists, one-shot plays never reference a loop and loop rules always do, every table /
limiter referenced exists, template placeholders expand to existing sounds, every Godot-manifest sound is shipped; prints the
coverage of the core's event types and the sounds no rule references.
"""
from __future__ import annotations

import argparse
import itertools
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import event_rules as ER  # noqa: E402

UE_ROOT = HERE.parent.parent
REPO = UE_ROOT.parent
INDEX = UE_ROOT / "SourceArt" / "Audio" / "sound_index.json"
OUT = UE_ROOT / "Content" / "Fourfold" / "Data" / "sfx_manifest.json"
GODOT_MANIFEST = REPO / "game" / "assets" / "audio" / "manifest.json"
CORE_SRC = UE_ROOT / "Source" / "FourfoldCore" / "Private"

ASSET_ROOT = {"SFX": "/Game/Fourfold/Audio/SFX", "Ambience": "/Game/Fourfold/Audio/Ambience"}
PRIORITY = {"impact": 3, "element": 2, "loop": 1, "ui": 4, "ambience": 0}


def pitch_var(name: str, entry: dict) -> float:
    cat = entry["category"]
    if cat in ("ambience",) or entry["loop"]:
        return 0.0
    if cat == "ui" or name.startswith(("charge_", "chime_", "ko_", "round_", "victory", "unlock", "combo_", "challenge_", "element_",
                                       "scenario_")):
        return 0.0
    if name.startswith(("step_", "cloth_", "breath_", "swing_", "kick_", "land")):
        return 0.05
    if cat == "impact":
        return 0.06
    return 0.04


def attenuation(name: str, entry: dict) -> str:
    if entry["bus"] in ("ui", "ambience") or name in ER.TWO_D_NAMES:
        return "2d"
    if name in ER.FEEL_SOUNDS:
        return "feel"
    if name in ER.FAR_SOUNDS:
        return "far"
    if name.startswith(ER.NEAR_PREFIX):
        return "near"
    return "mid"


def build_sounds(index: dict) -> dict:
    sounds = {}
    for name, e in sorted(index.items()):
        sounds[name] = {
            "asset": f"{ASSET_ROOT[e['dir']]}/S_{name}",
            "gain_db": min(float(e["suggested_volume_db"]), 0.0),
            "pitch_var": pitch_var(name, e),
            "max_voices": int(e["max_voices"]),
            "loop": bool(e["loop"]),
            "category": e["category"],
            "attenuation": attenuation(name, e),
            "bus": e["bus"],
            "family": e["family"],
            "duration_s": e["duration_s"],
            "priority": 3 if e["family"] == "system" else PRIORITY[e["category"]],
            "stinger": e["family"] == "system",
        }
    return sounds


def build(index: dict) -> dict:
    return {
        "schema": "fourfold.sfx/1",
        "generator": "unreal/Tools/audio (render_audio.py + build_manifest.py); every sound is synthesised, original",
        "mix": ER.MIX,
        "sounds": build_sounds(index),
        "tables": ER.TABLES,
        "events": ER.EV,
        "loops": ER.LOOPS,
        "steps": ER.STEPS,
        "ambience": ER.AMBIENCE,
        "ui": ER.UI,
    }


# ------------------------------------------------------------------------------------------------ validation


def refs_of(sound, tables, expect):
    """Every concrete sound name a sound reference can produce (for validation)."""
    out = set()
    if isinstance(sound, str):
        if "{" in sound:
            fields = re.findall(r"\{([^}]+)\}", sound)
            if not expect or any(f not in expect for f in fields):
                raise ValueError(f"template {sound!r} needs an 'expect' map")
            for combo in itertools.product(*[expect[f] for f in fields]):
                nm = sound
                for f, v in zip(fields, combo):
                    nm = nm.replace("{" + f + "}", str(v))
                out.add(nm)
        else:
            out.add(sound)
    elif isinstance(sound, list):
        out.update(sound)
    elif isinstance(sound, dict):
        t = tables.get(sound["table"])
        if t is None:
            raise ValueError(f"unknown table {sound['table']!r}")
        for v in t.values():
            out.update(v if isinstance(v, list) else [v])
        if sound.get("default"):
            out.add(sound["default"])
    return out


def validate(m: dict) -> tuple[list, set]:
    errs = []
    sounds = m["sounds"]
    tables = m["tables"]
    limiters = m["mix"]["limiters"]
    used = set()

    def need(name, loop, where):
        if name not in sounds:
            errs.append(f"{where}: unknown sound {name!r}")
            return
        used.add(name)
        if bool(sounds[name]["loop"]) != loop:
            errs.append(f"{where}: {name!r} is {'a loop' if sounds[name]['loop'] else 'a one-shot'} but is used as a "
                        f"{'loop' if loop else 'one-shot'}")

    for et, rules in m["events"].items():
        for r in rules:
            for p in r["play"]:
                try:
                    names = refs_of(p["sound"], tables, p.get("expect"))
                except ValueError as ex:
                    errs.append(f"events.{et}.{r['id']}: {ex}")
                    continue
                # a table used as one-shot: its values are one-shots (the 'element_ui' table maps ints to UI sounds)
                for nm in names:
                    if nm in sounds:
                        need(nm, False, f"events.{et}.{r['id']}")
                    else:
                        errs.append(f"events.{et}.{r['id']}: unknown sound {nm!r}")
                if p.get("limit") and p["limit"] not in limiters:
                    errs.append(f"events.{et}.{r['id']}: unknown limiter {p['limit']!r}")
    for grp in ("bodies", "actors"):
        for r in m["loops"][grp]:
            need(r["sound"], True, f"loops.{grp}.{r['id']}")
    for r in m["loops"]["events"]:
        need(r["sound"], True, f"loops.events.{r['event']}")
    for sname, variants in m["steps"]["surfaces"].items():
        for v in variants:
            need(v, False, f"steps.{sname}")
    for v in m["steps"]["cloth_sounds"]:
        need(v, False, "steps.cloth")
    for b in m["ambience"]["beds"]:
        need(b["sound"], True, "ambience.beds")
    for a in m["ambience"]["accents"]:
        for s in a["sounds"]:
            need(s, False, "ambience.accents")
    for cue, s in m["ui"].items():
        need(s, False, f"ui.{cue}")
    for nm in m["mix"]["duck"]["triggers"]:
        if nm not in sounds:
            errs.append(f"mix.duck: unknown trigger sound {nm!r}")
    for k in ("sting", "swing"):
        if k not in limiters:
            errs.append(f"mix.limiters.{k} missing")
    return errs, used


def core_event_types() -> set:
    types = set()
    for p in CORE_SRC.rglob("*.cpp"):
        for mt in re.finditer(r'(?:emit|Emit)\(\s*"([a-z_]+)"', p.read_text(errors="ignore")):
            types.add(mt.group(1))
    types.add("fx")
    return types


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    index = json.loads(INDEX.read_text())
    m = build(index)
    errs, used = validate(m)
    # every sound of the Godot build ships
    if GODOT_MANIFEST.exists():
        godot = set(json.loads(GODOT_MANIFEST.read_text()))
        miss = sorted(godot - set(m["sounds"]))
        if miss:
            errs.append(f"Godot sounds missing from the set: {miss}")
    core = core_event_types()
    handled = set(m["events"]) | {e["event"] for e in m["loops"]["events"]}
    silent_ok = {"fx"}   # handled via rules above
    uncovered = sorted(core - handled)
    unused = sorted(set(m["sounds"]) - used - {s for r in m["events"].values() for rr in r for p in rr["play"]
                                               for s in (refs_of(p["sound"], m["tables"], p.get("expect")) if True else ())})
    text = json.dumps(m, indent=1, sort_keys=False) + "\n"
    if not args.check:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(text)
    print(f"sounds {len(m['sounds'])} (loops {sum(1 for s in m['sounds'].values() if s['loop'])}), event types with rules {len(m['events'])}, "
          f"rules {sum(len(v) for v in m['events'].values())}, loop rules {len(m['loops']['bodies']) + len(m['loops']['actors']) + len(m['loops']['events'])}")
    print(f"core event types without a rule ({len(uncovered)}): {' '.join(uncovered)}")
    print(f"sounds no rule references ({len(unused)}): {' '.join(unused)}")
    if errs:
        print("\nERRORS:")
        for e in errs:
            print("  ", e)
        return 1
    print(f"manifest OK ({len(text) // 1024} KB){'' if args.check else ' -> ' + str(OUT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
