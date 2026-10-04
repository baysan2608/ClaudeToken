#!/usr/bin/env bash
# Run the Godot editor binary against the game project.
#   tools/scripts/godot.sh                 open/play the game (desktop window)
#   tools/scripts/godot.sh --headless ...  logic only, no rendering (tests, soak)
#   tools/scripts/godot.sh --render ...    rendered run for captures (on Linux: Vulkan lavapipe under Xvfb;
#                                          on macOS: a normal window using Metal)
# GODOT_BIN overrides the binary. Defaults: macOS /Applications/Godot.app (or ~/Applications),
# Linux cloud container /home/user/tools/godot, else `godot` on PATH.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ -z "${GODOT_BIN:-}" ]]; then
  for c in /Applications/Godot.app/Contents/MacOS/Godot "$HOME/Applications/Godot.app/Contents/MacOS/Godot" \
           /home/user/tools/godot "$(command -v godot 2>/dev/null || true)"; do
    if [[ -n "$c" && -x "$c" ]]; then GODOT_BIN="$c"; break; fi
  done
fi
if [[ -z "${GODOT_BIN:-}" ]]; then
  echo "Godot 4.7.2 not found. Install it (godotengine.org) or set GODOT_BIN=/path/to/Godot" >&2
  exit 1
fi
if [[ "${1:-}" == "--render" ]]; then
  shift
  if [[ "$(uname)" == "Darwin" ]]; then
    exec "$GODOT_BIN" --path "$ROOT/game" "$@"
  fi
  export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
  exec xvfb-run -a -s "-screen 0 1600x900x24" "$GODOT_BIN" --path "$ROOT/game" --audio-driver Dummy "$@"
fi
exec "$GODOT_BIN" --path "$ROOT/game" "$@"
