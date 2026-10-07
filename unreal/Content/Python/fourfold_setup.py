"""Fourfold one-click editor setup (stream `world_audio`).

    Tools > Execute Python Script... > unreal/Content/Python/fourfold_setup.py          (runs main())
    or in the Output Log's Python prompt:     import fourfold_setup; fourfold_setup.main(force=True, only="fx")

main(force=False, only=None, lighting="auto") runs the build packages in a fixed order - fx, character, animation, world, audio -
each one optional (a missing package or a failure is recorded and the rest continues), saves all dirty packages after each part, writes
Saved/Fourfold/setup_report.json and prints a readable summary.  Parts: fx character animation world audio (`only` = one name, a list
or a comma separated string).  The world part creates the level /Game/Fourfold/Maps/L_Lab and builds its lighting (Lightmass; falls back to
dynamic lighting when the build fails); `lighting` = "auto" | "baked" | "dynamic" (skip the bake).  Idempotent: existing assets are
kept unless force=True.  The shaders compile in the background afterwards (bottom-right corner): wait until the counter reaches zero."""
import importlib
import json
import os
import sys
import time
import traceback

import unreal

PARTS = ["fx", "character", "animation", "mocap", "metahuman", "world", "audio"]
TITLES = {"fx": "VFX textures / meshes / materials", "character": "fighter mesh + materials", "animation": "animation sequences", "mocap": "retargeted motion capture (GASP)", "metahuman": "clips on the MetaHuman skeleton",
          "world": "arena level, environment, lighting", "audio": "sounds"}


def _log(msg):
    unreal.log(f"[Fourfold] {msg}")


def _project_dir():
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def report_path():
    return os.path.join(_project_dir(), "Saved", "Fourfold", "setup_report.json")


def _parts(only):
    if only is None or only == "" or only == "all":
        return list(PARTS)
    if isinstance(only, str):
        only = [p.strip() for p in only.split(",") if p.strip()]
    out = [p for p in PARTS if p in only]
    unknown = [p for p in only if p not in PARTS]
    if unknown:
        unreal.log_warning(f"[Fourfold] unknown part(s) {unknown}: valid parts are {PARTS}")
    return out


def _purge(pkg):
    """Forget cached modules of a package so that edits are picked up without restarting the editor."""
    for name in list(sys.modules):
        if name == pkg or name.startswith(pkg + "."):
            del sys.modules[name]


def _save_all():
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        return True
    except Exception:  # noqa: BLE001
        try:
            unreal.EditorAssetLibrary.save_directory("/Game/Fourfold", only_if_is_dirty=True, recursive=True)
            return True
        except Exception as e:  # noqa: BLE001
            unreal.log_warning(f"[Fourfold] save all failed: {e}")
            return False


def _run_part(part, force, lighting):
    """-> (status, report dict).  status: 'ok' | 'failed' | 'missing'."""
    name = f"fourfold.{part}"
    try:
        _purge(name)
        mod = importlib.import_module(name)
    except ImportError as e:
        return "missing", {"created": [], "skipped": [], "failed": [{"item": name, "error": f"not available: {e}"}], "notes": []}
    except Exception as e:  # noqa: BLE001
        return "failed", {"created": [], "skipped": [], "failed": [{"item": name, "error": f"import raised: {e}\n{traceback.format_exc()}"}],
                          "notes": []}
    fn = getattr(mod, "build_all", None)
    if fn is None:
        return "missing", {"created": [], "skipped": [], "failed": [{"item": name, "error": "package has no build_all()"}], "notes": []}
    try:
        try:
            import inspect
            kwargs = {"force": force}
            if "lighting" in inspect.signature(fn).parameters:
                kwargs["lighting"] = lighting
            rep = fn(**kwargs)
        except TypeError:
            rep = fn(force)
        if not isinstance(rep, dict):
            rep = {"created": [], "skipped": [], "failed": [], "notes": [f"build_all returned {type(rep).__name__}"]}
        for k in ("created", "skipped", "failed", "notes"):
            rep.setdefault(k, [])
        return ("failed" if rep["failed"] else "ok"), rep
    except Exception as e:  # noqa: BLE001
        return "failed", {"created": [], "skipped": [], "failed": [{"item": name, "error": f"build_all raised: {e}\n{traceback.format_exc()}"}],
                          "notes": []}


def _summary(results, t0):
    lines = ["", "=" * 78, "FOURFOLD SETUP SUMMARY", "=" * 78]
    tot_f = 0
    for part, (status, rep, dt) in results.items():
        f = len(rep["failed"])
        tot_f += f
        lines.append(f"{part:10s} {status.upper():8s} created {len(rep['created']):4d}  skipped {len(rep['skipped']):4d}  failed {f:3d}   {dt:6.1f}s   {TITLES.get(part, '')}")
        for item in rep["failed"][:6]:
            first = str(item.get("error", "")).strip().splitlines()
            lines.append(f"    FAILED {item.get('item')}: {first[0][:150] if first else ''}")
        if f > 6:
            lines.append(f"    ... and {f - 6} more (see the report)")
    lines.append("-" * 78)
    lines.append(f"total {time.time() - t0:.1f}s, {tot_f} failed item(s).  Report: {report_path()}")
    lines.append("Shaders compile in the background: wait until the counter in the bottom-right corner reaches zero before playing.")
    lines.append("=" * 78)
    return "\n".join(lines)


def main(force=False, only=None, lighting="auto"):
    t0 = time.time()
    parts = _parts(only)
    _log(f"setup starts: parts={parts} force={force} lighting={lighting}")
    results = {}
    task = None
    try:
        task = unreal.ScopedSlowTask(len(parts), "Fourfold setup")
        task.__enter__()
        task.make_dialog(True)
    except Exception:  # noqa: BLE001
        task = None
    try:
        for part in parts:
            if task is not None:
                try:
                    if task.should_cancel() is True:
                        _log("cancelled by the user")
                        break
                    task.enter_progress_frame(1, f"{part}: {TITLES.get(part, '')}")
                except Exception:  # noqa: BLE001
                    pass
            _log(f"--- {part} ---")
            t1 = time.time()
            status, rep = _run_part(part, force, lighting)
            _save_all()
            results[part] = (status, rep, time.time() - t1)
            _log(f"{part}: {status} (created {len(rep['created'])}, skipped {len(rep['skipped'])}, failed {len(rep['failed'])})")
    finally:
        if task is not None:
            try:
                task.__exit__(None, None, None)
            except Exception:  # noqa: BLE001
                pass
    _save_all()
    report = {
        "schema": "fourfold.setup_report/1",
        "started": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t0)),
        "duration_s": round(time.time() - t0, 1),
        "engine": _engine_version(),
        "force": bool(force), "only": parts, "lighting": lighting,
        "parts": {p: {"status": s, "duration_s": round(dt, 1), **rep} for p, (s, rep, dt) in results.items()},
    }
    try:
        os.makedirs(os.path.dirname(report_path()), exist_ok=True)
        with open(report_path(), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1, default=str)
    except Exception as e:  # noqa: BLE001
        unreal.log_warning(f"[Fourfold] could not write the report: {e}")
    text = _summary(results, t0)
    for line in text.splitlines():
        unreal.log(line)
    print(text)
    return report


def _engine_version():
    try:
        return str(unreal.SystemLibrary.get_engine_version())
    except Exception:  # noqa: BLE001
        return "unknown"


if __name__ == "__main__":
    main()
