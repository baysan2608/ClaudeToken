"""Exercise Content/Python/fourfold/animation (the Unreal import) end to end against a scripted fake of the editor:
SKEL_Fighter exists, Interchange "creates" two AnimSequences per FBX (the take and a morph stub, as UE 5.x does), the
script must keep the right one, rename it to Anims/A_<clip>, set loop / root-motion / compression properties from
clips.json, delete the scratch folder and report.  Also runs the missing-skeleton path.

    python3 unreal/Tools/blender/animation/test_unreal_import.py
"""
import importlib
import json
import os
import sys
import types
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
UNREAL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(UNREAL, "Tools", "py_mock"))
import run_with_mock_unreal as runner  # noqa: E402


class FakeAsset:
    def __init__(self, path):
        self.path = path
        self.props = {}

    def get_name(self):
        return self.path.rsplit("/", 1)[-1]

    def get_path_name(self):
        return f"{self.path}.{self.get_name()}"

    def set_editor_property(self, k, v):
        self.props[k] = v

    def get_editor_property(self, k):
        return self.props.get(k)


class Skeleton(FakeAsset):
    pass


class SkeletalMesh(FakeAsset):
    pass


class AnimSequence(FakeAsset):
    def get_play_length(self):
        return 0.5 if "Morph" not in self.path else 0.0

    def set_preview_skeletal_mesh(self, m):
        self.props["preview"] = m


def make_world(with_skeleton=True):
    u = runner.make_unreal()
    u.Skeleton, u.SkeletalMesh, u.AnimSequence = Skeleton, SkeletalMesh, AnimSequence
    store = {}
    dirs = set()
    skel_path = "/Game/Fourfold/Characters/Fighter/SKEL_Fighter"
    if with_skeleton:
        store[skel_path] = Skeleton(skel_path)
        store["/Game/Fourfold/Characters/Fighter/SK_Fighter"] = SkeletalMesh("/Game/Fourfold/Characters/Fighter/SK_Fighter")
    store["/Engine/Animation/DefaultAnimBoneCompressionSettings"] = FakeAsset("/Engine/Animation/DefaultAnimBoneCompressionSettings")
    eal = types.SimpleNamespace()
    eal.does_asset_exist = lambda p: p.split(".")[0] in store
    eal.does_directory_exist = lambda p: p in dirs
    eal.make_directory = lambda p: dirs.add(p) or True
    eal.delete_directory = lambda p: [store.pop(k) for k in list(store) if k.startswith(p + "/")] and False or dirs.discard(p) or True
    eal.delete_asset = lambda p: store.pop(p.split(".")[0], None) is not None
    eal.list_assets = lambda p, recursive=True, include_folder=False: [store[k].get_path_name() for k in store if k.startswith(p + "/")]

    def rename(src, dst):
        a = store.pop(src.split(".")[0])
        a.path = dst
        store[dst] = a
        return True
    eal.rename_asset = rename
    eal.save_loaded_asset = lambda a, only_dirty=True: True
    u.EditorAssetLibrary = eal
    u.load_asset = lambda p: store.get(p.split(".")[0])

    class Task:
        def __init__(self):
            self.props = {}

        def set_editor_property(self, k, v):
            self.props[k] = v

        def get_editor_property(self, k):
            return self.props.get(k)

    u.AssetImportTask = Task
    imported = []

    def import_asset_tasks(tasks):
        for t in tasks:
            d = t.props["destination_path"]
            for nm in ("Anim_0_Root", "Root_MorphAnim_0"):
                store[f"{d}/{nm}"] = AnimSequence(f"{d}/{nm}")
            store[f"{d}/root_Skeleton"] = Skeleton(f"{d}/root_Skeleton")    # stray skeleton (must be cleaned up)
            t.props["imported_object_paths"] = [f"{d}/Anim_0_Root"]
            imported.append(t.props["filename"])
    u.AssetToolsHelpers.get_asset_tools.return_value.import_asset_tasks = import_asset_tasks
    return u, store, imported


def run(with_skeleton=True):
    u, store, imported = make_world(with_skeleton)
    sys.modules["unreal"] = u
    sys.path.insert(0, os.path.join(UNREAL, "Content", "Python"))
    for m in [m for m in sys.modules if m.startswith("fourfold")]:
        del sys.modules[m]
    fa = importlib.import_module("fourfold.animation")
    return fa.build_all(force=False), store, imported, fa


def main():
    clips = json.load(open(os.path.join(UNREAL, "Content", "Fourfold", "Data", "clips.json")))["clips"]
    rep, store, imported, fa = run(True)
    assert not rep["failed"], rep["failed"][:3]
    assert len(rep["created"]) == len(clips), (len(rep["created"]), len(clips))
    for n, c in clips.items():
        a = store.get(f"{fa.ANIM_DIR}/A_{n}")
        assert a is not None, n
        assert a.props.get("loop") == c["loop"], (n, a.props.get("loop"))
        assert a.props.get("enable_root_motion") is False and a.props.get("force_root_lock") is True, n
        assert a.props.get("allow_frame_stripping") is False, n
        assert a.props.get("bone_compression_settings") is not None, n
    stray = [k for k in store if k.startswith(fa.SCRATCH)]
    assert not stray, stray[:5]
    # second run: everything is skipped
    rep2 = fa.build_all(force=False)
    assert len(rep2["skipped"]) == len(clips) and not rep2["created"], rep2
    rep0, _, imp0, _ = run(False)
    assert rep0["failed"] and not imp0 and "SKEL_Fighter" in rep0["failed"][0]["item"], rep0
    print(f"OK: {len(rep['created'])} clips imported (scripted fake editor), re-run skipped all, "
          f"missing-skeleton path stops cleanly; notes: {len(rep['notes'])}")


if __name__ == "__main__":
    main()
