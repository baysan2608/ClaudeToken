"""Setup part `mocap`: retargets Epic's Game Animation Sample mocap onto the fighter (fourfold.animation.mocap).

Skipped (status ok, with a note) when the GASP content has not been copied in yet (unreal/Tools/gasp/migrate_from_gasp.py).
"""
import unreal

from .animation import mocap as _mocap


def build_all(force=False):
    if not unreal.EditorAssetLibrary.does_asset_exist(_mocap.SRC_MESH):
        return {"created": [], "skipped": [], "failed": [],
                "notes": ["GASP content not present: run Tools/gasp/migrate_from_gasp.py first (mocap skipped)"]}
    return _mocap.build(force=force)
