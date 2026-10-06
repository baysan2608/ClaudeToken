# Real `unreal` Python stub (owner-provided)

The Linux build container has no Unreal Editor, so editor-Python scripts are written blind. After the first editor
launch with `bDeveloperMode=True` (Config/DefaultEditor.ini), Unreal writes the exact Python API of the installed
engine to `unreal/Intermediate/PythonStub/unreal.py`. Copy that file here (`unreal/Tools/py_stub/unreal.py`) and commit
it: every later session lints its scripts against it
(`python3 unreal/Tools/py_mock/run_with_mock_unreal.py <script> --stub`).
