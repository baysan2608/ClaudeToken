"""Fourfold world build (stream `world_audio`): environment textures, materials, static meshes, the level L_Lab with its lighting.

    import fourfold.world as fw; fw.build_all(force=False, lighting="auto")      -> report dict

Sources (generated here, committed): unreal/SourceArt/Environment/{Textures,Meshes}, meshes.json, level_layout.json (Tools/world/*),
HLSL unreal/Shaders/Env/FFEnv.ush (included as /Fourfold/Env/FFEnv.ush), arena geometry Source/FourfoldCore/Data/sim.json.  Produces:
    /Game/Fourfold/Env/Textures/T_Env_*          /Game/Fourfold/Env/Materials/{M_Env_*, MI_Env_<slot>, MPC_Arena}
    /Game/Fourfold/Env/Meshes/SM_Env_*           /Game/Fourfold/Maps/L_Lab   (root actor tag FourfoldArena, solids tagged FFSolid_<name>)
Idempotent: existing assets are kept unless force=True; an existing level that already has the FourfoldArena actor is kept as is.
`lighting`: "auto" (bake with Lightmass, fall back to dynamic lighting when the build fails), "baked", "dynamic" (skip the bake).
Never raises: failures are collected in the report.  APIs used (with sources): unreal/docs/world/API_NOTES.md."""
import traceback

import unreal

from . import common as C

__all__ = ["build_all"]


def build_all(force=False, lighting="auto"):
    report = C.new_report()
    info = {}
    try:
        from . import textures, materials, meshes, level
        # 1. textures
        t_rep = C.new_report()
        try:
            texs = textures.import_all(force, t_rep)
        except Exception as e:  # noqa: BLE001
            texs = {}
            t_rep["failed"].append({"item": "textures", "error": f"{e}\n{traceback.format_exc()}"})
        C.merge(report, t_rep)
        # 2. materials
        m_rep = C.new_report()
        try:
            instances = materials.build_all(force, texs, m_rep)
        except Exception as e:  # noqa: BLE001
            instances = {}
            m_rep["failed"].append({"item": "materials", "error": f"{e}\n{traceback.format_exc()}"})
        C.merge(report, m_rep)
        # 3. meshes
        s_rep = C.new_report()
        try:
            imported = meshes.import_all(force, instances, s_rep)
        except Exception as e:  # noqa: BLE001
            imported = {}
            s_rep["failed"].append({"item": "meshes", "error": f"{e}\n{traceback.format_exc()}"})
        C.merge(report, s_rep)
        # 4. level (+ lighting build)
        l_rep = C.new_report()
        try:
            mode = level.build_level(force, imported, lighting, l_rep)
            if mode:
                info["lighting"] = mode
        except Exception as e:  # noqa: BLE001
            l_rep["failed"].append({"item": "level", "error": f"{e}\n{traceback.format_exc()}"})
        C.merge(report, l_rep)
        try:
            unreal.EditorAssetLibrary.save_directory(C.ENV_ROOT, only_if_is_dirty=True, recursive=True)
        except Exception:  # noqa: BLE001
            pass
    except Exception as e:  # noqa: BLE001
        C.err(f"world build failed: {e}\n{traceback.format_exc()}")
        report["failed"].append({"item": "world", "error": str(e)})
    report["info"] = info
    C.log(f"done: created {len(report['created'])}, skipped {len(report['skipped'])}, failed {len(report['failed'])} {info}")
    return report
