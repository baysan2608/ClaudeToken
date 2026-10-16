"""Full-editor entry of the setup part "ruins" (LOD import needs a rendering editor, not the -run=pythonscript commandlet):

  "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
      "$PWD/unreal/Fourfold.uproject" -ExecutePythonScript="$PWD/unreal/Tools/ruins/editor_setup_ruins.py" -unattended -nosplash

-ExecutePythonScript keeps the editor open, so the script quits it when the setup returns (also after a failure).
Result: unreal/Saved/Fourfold/setup_report.json (parts.ruins)."""
import unreal

import fourfold_setup

try:
    fourfold_setup.main(force=True, only="ruins")
finally:
    unreal.SystemLibrary.quit_editor()
