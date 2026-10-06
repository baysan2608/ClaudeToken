"""Shared helpers of the world package: logging, paths, defensive property setters, asset plumbing.

Everything here is written so that a renamed property / missing class in a newer engine shows up as a line in the report
(`notes`) instead of aborting the build."""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary

ENV_ROOT = "/Game/Fourfold/Env"
TEX_DIR = ENV_ROOT + "/Textures"
MAT_DIR = ENV_ROOT + "/Materials"
MESH_DIR = ENV_ROOT + "/Meshes"
MAP_DIR = "/Game/Fourfold/Maps"
MAP_PATH = MAP_DIR + "/L_Lab"


def log(msg):
    unreal.log(f"[Fourfold][world] {msg}")


def warn(msg):
    unreal.log_warning(f"[Fourfold][world] {msg}")


def err(msg):
    unreal.log_error(f"[Fourfold][world] {msg}")


def new_report():
    return {"created": [], "skipped": [], "failed": [], "notes": []}


def merge(into, other):
    for k in ("created", "skipped", "failed", "notes"):
        into[k].extend(other.get(k, []))


def project_paths():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    env_art = os.path.join(proj, "SourceArt", "Environment")
    return dict(
        project=proj,
        art=env_art,
        textures=os.path.join(env_art, "Textures"),
        meshes=os.path.join(env_art, "Meshes"),
        meshes_json=os.path.join(env_art, "meshes.json"),
        layout_json=os.path.join(env_art, "level_layout.json"),
        sim_json=os.path.join(proj, "Source", "FourfoldCore", "Data", "sim.json"),
    )


def load_json(path, report, what):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:  # noqa: BLE001
        report["notes"].append(f"{what} not readable ({path}: {e})")
        return None


def set_prop(obj, prop, value, report=None, quiet=False):
    """obj.set_editor_property that never raises.  Returns True on success."""
    if value is None:
        return False
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:  # noqa: BLE001
        if report is not None and not quiet:
            report["notes"].append(f"could not set {prop} on {_name(obj)}: {e}")
        return False


def get_prop(obj, prop, default=None):
    try:
        return obj.get_editor_property(prop)
    except Exception:  # noqa: BLE001
        return default


def _name(obj):
    try:
        return obj.get_name()
    except Exception:  # noqa: BLE001
        return str(obj)


def enum(enum_name, *members):
    """First existing member of unreal.<enum_name> among `members`, else None."""
    cls = getattr(unreal, enum_name, None)
    if cls is None:
        return None
    for m in members:
        v = getattr(cls, m, None)
        if v is not None:
            return v
    return None


def save_asset(asset):
    try:
        EAL.save_loaded_asset(asset, False)
    except Exception:  # noqa: BLE001
        try:
            EAL.save_loaded_asset(asset)
        except Exception:  # noqa: BLE001
            pass


def asset_exists(path):
    try:
        return bool(EAL.does_asset_exist(path))
    except Exception:  # noqa: BLE001
        return False


def make_dirs(*paths):
    for p in paths:
        try:
            if not EAL.does_directory_exist(p):
                EAL.make_directory(p)
        except Exception:  # noqa: BLE001
            pass


def linear(c):
    """sRGB-ish 0..1 triple -> linear (approx. gamma 2.2), for colours quoted from the mock-up / design values."""
    return tuple(float(v) ** 2.2 for v in c)
