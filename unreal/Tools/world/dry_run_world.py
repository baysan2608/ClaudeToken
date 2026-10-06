"""Dry run of the editor-Python world build against a small fake `unreal` module (no Unreal needed): fake asset registry (imports
"create" assets), fake static meshes with bounds read from meshes.json, recorded actor spawns.  Catches Python errors in our code and
checks the level placement logic (solids at the sim box centres, tags, lighting actors, mirror detection).

    python3 Tools/world/dry_run_world.py [--mirror] [--verbose]
Exit code 1 on a failure of the checks.  (The real engine API names are checked separately: Tools/world/verify_props.py.)"""
import json
import os
import sys
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
UE = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(UE, "Tools", "py_mock"))
sys.path.insert(0, os.path.join(UE, "Content", "Python"))
import run_with_mock_unreal as R   # noqa: E402


class FakeObj:
    def __init__(self, name="obj"):
        self.__dict__["_props"] = {}
        self.__dict__["_name"] = name

    def set_editor_property(self, k, v):
        self._props[k] = v

    def get_editor_property(self, k):
        if k == "tags":
            return self._props.get("tags", [])
        if k not in self._props:
            raise Exception(f"no property {k}")
        return self._props[k]

    def get_name(self):
        return self._name


class FakeStaticMesh(FakeObj):
    def __init__(self, name, size, center):
        super().__init__(name)
        self.size, self.center = size, center
        self._props["static_materials"] = [FakeSlot(n) for n in ("Floor",)]

    def get_bounds(self):
        b = FakeObj("bounds")
        v = lambda x, y, z: mock.Mock(x=x, y=y, z=z)
        b._props["box_extent"] = v(*(s / 2 for s in self.size))
        b._props["origin"] = v(*self.center)
        return b


class FakeSlot(FakeObj):
    def __init__(self, n):
        super().__init__(n)
        self._props.update(material_slot_name=n, imported_material_slot_name=n)


class FakeActor(FakeObj):
    def __init__(self, cls, loc, rot):
        super().__init__("actor")
        self.cls, self.loc, self.rot = cls, loc, rot
        self.scale = None
        self.static_mesh_component = FakeComp(self)
        self.label = None

    def set_actor_label(self, l):
        self.label = l

    def set_actor_scale3d(self, s):
        self.scale = s

    def set_folder_path(self, p):
        pass

    def get_component_by_class(self, c):
        return FakeComp(self)


class FakeComp(FakeObj):
    def __init__(self, owner):
        super().__init__("comp")
        self.owner = owner
        self.mesh = None

    def set_static_mesh(self, m):
        self.mesh = m

    def __getattr__(self, k):
        if k.startswith("set_") or k in ("recapture_sky",):
            return lambda *a, **kw: True
        raise AttributeError(k)


def main():
    mirror = "--mirror" in sys.argv
    verbose = "--verbose" in sys.argv
    unreal = R.make_unreal()

    class Rot:                      # the shared mock drops keyword arguments; keep them to check the sun direction
        def __init__(self, roll=0.0, pitch=0.0, yaw=0.0):
            self.roll, self.pitch, self.yaw = roll, pitch, yaw
            self.args = (roll, pitch, yaw)
    unreal.Rotator = Rot
    sys.modules["unreal"] = unreal
    meshes_json = json.load(open(os.path.join(UE, "SourceArt", "Environment", "meshes.json")))["meshes"]
    assets = {}
    actors = []

    def exists(p, *a, **k):
        return p in assets

    def load_asset(p, *a, **k):
        return assets.get(p)

    class StaticMesh(FakeStaticMesh):
        pass

    unreal.StaticMesh = StaticMesh
    unreal.StaticMeshActor = type("StaticMeshActor", (), {})
    for n in ("TargetPoint", "DirectionalLight", "SkyLight", "ExponentialHeightFog", "SphereReflectionCapture", "PointLight",
              "PostProcessVolume", "LightmassImportanceVolume", "LightmassCharacterIndirectDetailVolume"):
        setattr(unreal, n, type(n, (), {}))
    unreal.EditorAssetLibrary.does_asset_exist = exists
    unreal.load_asset = load_asset
    unreal.Name = lambda s: s

    class Tools:
        def import_asset_tasks(self, tasks):
            for t in tasks:
                dst = t.get_editor_property("destination_path") + "/" + t.get_editor_property("destination_name")
                nm = t.get_editor_property("destination_name")
                if nm.startswith("SM_Env"):
                    info = meshes_json[nm]
                    b = info["bounds_sim"]
                    size = info["expected_size_cm"]
                    cy = 100.0 * 0.5 * (b[0][2] + b[1][2]) * (-1 if mirror else 1)
                    cx = 100.0 * 0.5 * (b[0][0] + b[1][0])
                    cz = 100.0 * 0.5 * (b[0][1] + b[1][1])
                    assets[dst] = StaticMesh(nm, size, (cx, cy, cz))
                else:
                    assets[dst] = FakeObj(nm)

        def create_asset(self, name, folder, cls, factory):
            o = FakeObj(name)
            assets[folder + "/" + name] = o
            return o

    class AssetImportTask(FakeObj):
        pass

    unreal.AssetImportTask = AssetImportTask
    unreal.AssetToolsHelpers.get_asset_tools = lambda: Tools()
    unreal.MaterialEditingLibrary.get_material_expressions = lambda m: []

    class Les:
        def new_level(self, p):
            return True

        def load_level(self, p):
            return True

        def save_current_level(self):
            return True

        def build_light_maps(self, *a):
            return True

    class Eas:
        def spawn_actor_from_class(self, cls, loc, rot):
            a = FakeActor(cls, loc, rot)
            actors.append(a)
            return a

        def get_all_level_actors(self):
            return list(actors)

        def destroy_actor(self, a):
            actors.remove(a)

    les, eas = Les(), Eas()
    unreal.get_editor_subsystem = lambda c: les if c is unreal.LevelEditorSubsystem else eas
    unreal.LevelEditorSubsystem = type("LES", (), {})
    unreal.EditorActorSubsystem = type("EAS", (), {})
    unreal.get_editor_subsystem = lambda c: les if c is unreal.LevelEditorSubsystem else eas
    import fourfold.world as fw
    rep = fw.build_all(force=False)
    fails = []
    # checks
    solids = [a for a in actors if a.label and a.label.startswith("FFSolid_")]
    if len(solids) != 10:
        fails.append(f"expected 10 solid actors, got {len(solids)}")
    sim = json.load(open(os.path.join(UE, "Source", "FourfoldCore", "Data", "sim.json")))["arena_lab"]
    for s in sim["solids"]:
        a = next((x for x in solids if x.label == "FFSolid_" + s["name"]), None)
        if a is None:
            fails.append("missing " + s["name"])
            continue
        mn, mx = s["min"]["$v3"], s["max"]["$v3"]
        want = [(mn[0] + mx[0]) * 50, (mn[2] + mx[2]) * 50, (mn[1] + mx[1]) * 50]
        got = [a.loc.x, a.loc.y, a.loc.z] if hasattr(a.loc, "x") else list(a.loc.args)
        if any(abs(g - w) > 0.01 for g, w in zip(got, want)):
            fails.append(f"{s['name']} at {got}, want {want}")
        if ("FFSolid_" + s["name"]) not in a._props.get("tags", []):
            fails.append(f"{s['name']} lacks its tag")
        if mirror and not (a.scale is not None and a.scale.args[1] < 0 if hasattr(a.scale, "args") else False):
            fails.append(f"{s['name']} not mirrored under --mirror")
    if not any("FourfoldArena" in a._props.get("tags", []) for a in actors):
        fails.append("no actor tagged FourfoldArena")
    kinds = {a.cls.__name__ for a in actors if hasattr(a.cls, "__name__")}
    for k in ("DirectionalLight", "SkyLight", "ExponentialHeightFog", "PostProcessVolume", "LightmassImportanceVolume"):
        if k not in kinds:
            fails.append(f"no {k} spawned")
    sun = next((a for a in actors if getattr(a.cls, "__name__", "") == "DirectionalLight"), None)
    if sun is not None and hasattr(sun.rot, "pitch"):
        import math
        p, y = math.radians(sun.rot.pitch), math.radians(sun.rot.yaw)
        toward = [-math.cos(p) * math.cos(y), -math.cos(p) * math.sin(y), -math.sin(p)]     # opposite of the light's travel direction
        want = (-0.45, 0.35, 0.82)                                                              # fx FFKeyDir (docs/fx/REQUESTS.md)
        n = math.sqrt(sum(c * c for c in want))
        if any(abs(t - w / n) > 0.01 for t, w in zip(toward, want)):
            fails.append(f"sun direction toward the light {toward} differs from fx FFKeyDir {want}")
    if rep["failed"]:
        fails.append(f"{len(rep['failed'])} failed items: " + "; ".join(f"{f['item']}: {f['error'][:80]}" for f in rep["failed"][:6]))
    print(f"actors {len(actors)}  created {len(rep['created'])}  skipped {len(rep['skipped'])}  failed {len(rep['failed'])}  info {rep.get('info')}")
    if verbose:
        for n in rep["notes"]:
            print("NOTE", n[:160])
    for f in fails:
        print("FAIL", f)
    print("dry run", "FAILED" if fails else "OK")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
