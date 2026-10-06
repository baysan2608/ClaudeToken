"""Stateful dry run of fourfold.fx.build_all against a mock `unreal` (stream `fx`; extends the architect's
Tools/py_mock/run_with_mock_unreal.py, which stays as is).

    python3 unreal/Tools/vfx/py_mock_fx.py [--force]

Differences from the shared mock: assets "exist" once an import task or create_asset produced them, so the
post-import code (texture settings, mesh materials, rock instances) runs too; every MaterialEditingLibrary call is
recorded and checked (expressions connected only to inputs their node has, custom-node input names match the spec,
every material property of the spec connected). Prints a summary; exit code 1 on a Python error or a failed check.
"""
import argparse
import os
import sys
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL_DIR = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(UNREAL_DIR, "Tools", "py_mock"))

import run_with_mock_unreal as base  # noqa: E402


class Expr:
    def __init__(self, cls, mat):
        self.cls = cls
        self.mat = mat
        self.props = {}

    def set_editor_property(self, k, v):
        self.props[k] = v

    def get_editor_property(self, k):
        return self.props.get(k)


def make():
    m = base.make_unreal()
    existing = set()
    calls = {"links": [], "outputs": [], "exprs": []}

    def exists(p, *a, **k):
        return p in existing

    m.EditorAssetLibrary.does_asset_exist = exists
    m.EditorAssetLibrary.does_directory_exist = lambda *a, **k: True

    class Task:
        def __init__(self):
            self.p = {}

        def set_editor_property(self, k, v):
            self.p[k] = v

        def get_editor_property(self, k):
            return self.p.get(k, [])

    m.AssetImportTask = Task

    tools = mock.MagicMock()

    def import_tasks(tasks):
        for t in tasks:
            existing.add(f"{t.p['destination_path']}/{t.p['destination_name']}")

    def create_asset(name, path, cls, factory):
        existing.add(f"{path}/{name}")
        a = mock.MagicMock(name=f"{path}/{name}")
        a.get_name.return_value = name
        return a

    tools.import_asset_tasks = import_tasks
    tools.create_asset = create_asset
    m.AssetToolsHelpers.get_asset_tools = lambda: tools

    mel = mock.MagicMock()

    def create_expr(mat, cls, x, y):
        e = Expr(cls, mat)
        calls["exprs"].append(e)
        return e

    def connect(a, a_out, b, b_in):
        calls["links"].append((a, a_out, b, b_in))
        return True

    def connect_prop(a, a_out, prop):
        calls["outputs"].append((a, a_out, prop))
        return True

    mel.create_material_expression = create_expr
    mel.connect_material_expressions = connect
    mel.connect_material_property = connect_prop
    mel.get_material_expressions = lambda mat: []
    mel.recompile_material = lambda mat: []
    m.MaterialEditingLibrary = mel

    class CustomIO:
        def __init__(self):
            self.p = {}

        def set_editor_property(self, k, v):
            self.p[k] = v

    m.CustomInput = CustomIO
    m.CustomOutput = CustomIO
    return m, existing, calls


def check(calls, spec):
    problems = []
    customs = [e for e in calls["exprs"] if "Custom" in str(e.cls)]
    for e in customs:
        names = [ci.p["input_name"] for ci in e.props.get("inputs", [])]
        outs = [co.p["output_name"] for co in e.props.get("additional_outputs", [])]
        linked_in = [b_in for (_a, _ao, b, b_in) in calls["links"] if b is e]
        missing = set(names) - set(linked_in)
        if missing:
            problems.append(f"{e.props.get('description')}: inputs not connected: {sorted(missing)}")
        extra = set(linked_in) - set(names)
        if extra:
            problems.append(f"{e.props.get('description')}: links to undeclared inputs: {sorted(extra)}")
        for (a, ao, _b, _bi) in calls["links"] + [(a, ao, None, None) for (a, ao, _p) in calls["outputs"]]:
            if a is e and ao and ao not in outs:
                problems.append(f"{e.props.get('description')}: uses undeclared output {ao}")
        if not e.props.get("include_file_paths"):
            problems.append(f"{e.props.get('description')}: no include_file_paths")
    n_mat = len(spec.MATERIALS)
    n_custom = sum(len(mm["nodes"]) for mm in spec.MATERIALS.values())
    n_out = sum(len(mm["outputs"]) + len(mm.get("constants", {})) for mm in spec.MATERIALS.values())
    if len(customs) != n_custom:
        problems.append(f"{len(customs)} custom nodes created, spec has {n_custom}")
    if len(calls["outputs"]) != n_out:
        problems.append(f"{len(calls['outputs'])} material outputs connected, spec has {n_out} (for {n_mat} masters)")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    m, existing, calls = make()
    sys.modules["unreal"] = m
    sys.path.insert(0, os.path.join(UNREAL_DIR, "Content", "Python"))
    import fourfold.fx as ffx  # noqa: E402
    rep = ffx.build_all(force=a.force)
    print(f"created {len(rep['created'])}, skipped {len(rep['skipped'])}, failed {len(rep['failed'])}, "
          f"notes {len(rep['notes'])}")
    for f in rep["failed"]:
        print("FAILED", f["item"], f["error"].splitlines()[0])
    problems = check(calls, ffx.spec)
    for p in problems:
        print("CHECK", p)
    # second run must skip everything
    calls["exprs"].clear()
    rep2 = ffx.build_all(force=False)
    print(f"second run: created {len(rep2['created'])}, skipped {len(rep2['skipped'])}, failed {len(rep2['failed'])}")
    ok = not rep["failed"] and not problems and not rep2["created"]
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
