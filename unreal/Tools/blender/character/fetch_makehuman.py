"""Fetch more CC0 MakeHuman data files (pinned commit) into SourceArt/Character/third_party/makehuman/.

    python3 unreal/Tools/blender/character/fetch_makehuman.py targets/nose/nose-trans-up.target [...]
"""
import os
import sys
import urllib.request

COMMIT = "a8bc2d54ff0ac92e78ff71431b1023eda42bf482"
BASE = f"https://raw.githubusercontent.com/makehumancommunity/makehuman/{COMMIT}/makehuman/data/"
HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.abspath(os.path.join(HERE, "..", "..", "..", "SourceArt", "Character", "third_party", "makehuman"))


def fetch(rel):
    out = os.path.join(DEST, rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with urllib.request.urlopen(BASE + rel, timeout=60) as r, open(out, "wb") as f:
        f.write(r.read())
    return out


if __name__ == "__main__":
    for rel in sys.argv[1:]:
        print(fetch(rel))
