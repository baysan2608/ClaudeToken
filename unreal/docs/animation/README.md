# Stream `animation` - hand-keyed martial-arts clips on the shared rig

Status and per-clip progress: [PROGRESS.md](PROGRESS.md) · move coverage: [COVERAGE.md](COVERAGE.md) (generated) ·
Unreal APIs: [API_NOTES.md](API_NOTES.md).

## What is here
| Path | What |
|---|---|
| `Tools/blender/animation/ffa_*.py` | the authoring toolkit (numpy; Blender only for the FBX bake): rig rest data + FK (`ffa_rig`), body-space IK solver (`ffa_solver`), key/ease/overlap/layer DSL (`ffa_dsl`), hand shapes (`ffa_hands`), proxy mannequin (`ffa_proxy`), software preview renderer (`ffa_render`), validator (`ffa_validate`), FBX bake/export + re-import check (`ffa_export`) |
| `Tools/blender/animation/clips/*.py` | the clip catalogue (MARTIAL_ARTS.md §3): `bases.py` (stance poses), `shared.py`, `earth.py`, `water.py`, `fire.py`, `air.py`, `hand_shapes.py` |
| `Tools/blender/animation/build_animation.py` | solve -> validate -> previews -> FBX -> `clips.json` -> `anim_map.json` |
| `Tools/blender/animation/anim_table.py`, `anim_map_gen.py` | move -> clip table for all 160 sim moves and the generator that resolves it against the exported clips |
| `Tools/blender/animation/test_anim_data.py`, `test_unreal_import.py` | data self-test; scripted fake-editor run of the Unreal import |
| `SourceArt/Animation/A_<clip>.fbx`, `previews/` | exported clips (60 fps, frame 0 first key, 103 bones) and contact sheets / MP4s |
| `Content/Fourfold/Data/clips.json`, `anim_map.json` | runtime data (ARCHITECTURE §8.3) |
| `Content/Python/fourfold/animation/__init__.py` | Unreal import (`build_all(force=False)`) |

## Regenerate / test here
```
B=/home/user/tools/bpyenv/bin/python; cd unreal
$B Tools/blender/animation/build_animation.py                 # everything (about 1-2 min)
$B Tools/blender/animation/build_animation.py --only "e_*"     # some clips (merges into clips.json)
$B Tools/blender/animation/build_animation.py --videos P0      # + MP4 previews
python3 Tools/blender/animation/test_anim_data.py
python3 Tools/blender/animation/test_unreal_import.py
python3 Tools/py_mock/run_with_mock_unreal.py Content/Python/fourfold/animation/__init__.py --call fourfold.animation:build_all
```

## On the Mac
Run the setup (`fourfold_setup.py`, order fx -> character -> animation ...) or, after the character import,
`import fourfold.animation as fa; fa.build_all()` in the editor's Python console. Result:
`/Game/Fourfold/Characters/Fighter/Anims/A_<clip>`. Re-import after regenerating: `fa.build_all(force=True)`.
