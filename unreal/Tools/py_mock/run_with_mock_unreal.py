"""Dry-run an Unreal editor-Python script HERE (no Unreal) against a permissive mock of the `unreal` module.

    python3 unreal/Tools/py_mock/run_with_mock_unreal.py unreal/Content/Python/fourfold_setup.py [--call pkg.module:func]

What it catches: syntax errors, imports of our own modules, NameErrors / TypeErrors in OUR code, wrong control flow,
crashes in data handling (JSON, paths). What it cannot catch: wrong Unreal API names / signatures (every attribute of
the mock exists). For those, lint against the real stub when available: Tools/py_stub/unreal.py (see
Tools/py_stub/README.md) with `--stub` (attributes not present in the stub are reported).
Architect-owned, read-only for the streams (copy it if you need changes)."""
import argparse
import ast
import os
import runpy
import sys
import types
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL_DIR = os.path.abspath(os.path.join(HERE, "..", ".."))


class _Vec:
    def __init__(self, *a, **k):
        self.args = a
        self.x, self.y, self.z = (list(a) + [0.0, 0.0, 0.0])[:3]

    def __repr__(self):
        return f"Vec{self.args}"


def make_unreal():
    m = mock.MagicMock(name="unreal")
    m.log = lambda *a: print("[unreal.log]", *a)
    m.log_warning = lambda *a: print("[unreal.log_warning]", *a)
    m.log_error = lambda *a: print("[unreal.log_error]", *a)
    for n in ("Vector", "Vector2D", "Rotator", "LinearColor", "Color", "Transform", "Quat", "IntPoint"):
        setattr(m, n, _Vec)
    paths = mock.MagicMock(name="unreal.Paths")
    paths.project_dir = lambda: UNREAL_DIR + "/"
    paths.project_content_dir = lambda: os.path.join(UNREAL_DIR, "Content") + "/"
    paths.project_saved_dir = lambda: os.path.join(UNREAL_DIR, "Saved") + "/"
    paths.convert_relative_path_to_full = lambda p: os.path.abspath(p)
    m.Paths = paths
    m.SystemLibrary.get_engine_version = lambda: "5.8.3-mock"
    m.EditorAssetLibrary.does_asset_exist = lambda *a, **k: False
    m.EditorAssetLibrary.does_directory_exist = lambda *a, **k: False
    return m


def lint_against_stub(script_paths, stub_path):
    """Reports unreal.<Name> attributes used by the scripts that the real stub does not define."""
    with open(stub_path, encoding="utf-8") as f:
        tree = ast.parse(f.read())
    known = {n.name for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))}
    known |= {t.id for n in tree.body if isinstance(n, ast.Assign) for t in n.targets if isinstance(t, ast.Name)}
    problems = []
    for p in script_paths:
        with open(p, encoding="utf-8") as f:
            src = ast.parse(f.read())
        for node in ast.walk(src):
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == "unreal":
                if node.attr not in known:
                    problems.append(f"{p}:{node.lineno}: unreal.{node.attr} not in the stub")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("--call", help="pkg.module:function to call after importing (instead of running as __main__)")
    ap.add_argument("--stub", action="store_true", help="also lint unreal.X names against Tools/py_stub/unreal.py")
    a = ap.parse_args()
    sys.modules["unreal"] = make_unreal()
    content_python = os.path.join(UNREAL_DIR, "Content", "Python")
    sys.path.insert(0, content_python)
    sys.path.insert(0, os.path.dirname(os.path.abspath(a.script)))
    if a.stub:
        stub = os.path.join(UNREAL_DIR, "Tools", "py_stub", "unreal.py")
        if os.path.exists(stub):
            files = [a.script] + [os.path.join(dp, f) for dp, _, fs in os.walk(content_python) for f in fs if f.endswith(".py")]
            for line in lint_against_stub(files, stub):
                print("[stub-lint]", line)
        else:
            print("[stub-lint] no Tools/py_stub/unreal.py yet (see Tools/py_stub/README.md)")
    if a.call:
        mod, fn = a.call.split(":")
        getattr(__import__(mod, fromlist=["_"]), fn)()
    else:
        runpy.run_path(a.script, run_name="__main__")
    print("[mock] finished without Python errors")


if __name__ == "__main__":
    main()
