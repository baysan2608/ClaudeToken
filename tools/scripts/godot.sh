#!/usr/bin/env bash
# Run the Godot editor binary against the game project.
#   tools/scripts/godot.sh --headless ...            (logic only, no rendering)
#   tools/scripts/godot.sh --render ...              (Vulkan lavapipe under Xvfb, Mobile renderer)
# GODOT_BIN overrides the binary (default /home/user/tools/godot; on macOS e.g.
# /Applications/Godot.app/Contents/MacOS/Godot).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GODOT_BIN="${GODOT_BIN:-/home/user/tools/godot}"
if [[ "${1:-}" == "--render" ]]; then
  shift
  export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
  exec xvfb-run -a -s "-screen 0 1600x900x24" "$GODOT_BIN" --path "$ROOT/game" --audio-driver Dummy "$@"
fi
exec "$GODOT_BIN" --path "$ROOT/game" "$@"
