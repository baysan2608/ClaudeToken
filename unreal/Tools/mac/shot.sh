#!/usr/bin/env bash
# Runs the Mac game on L_Lab in a window and captures in-engine screenshots (works with a locked screen), then quits.
#   bash unreal/Tools/mac/shot.sh [out_dir] [times, e.g. 40,55,70] [extra game args...]
# Output: <out_dir>/shot_0.png ... (default logs/shots). Game log: ~/Library/Logs/Fourfold/FourfoldShot_<pid>.log (printed last)
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
OUT="${1:-$REPO/logs/shots}"; TIMES="${2:-45}"; shift $(( $# > 2 ? 2 : $# ))
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"; rm -f "$OUT"/shot_*.png   # absolute: the game runs from Engine/Binaries/Mac
LOGNAME_="FourfoldShot_$$.log"                       # one log per run: several sessions may capture at once
LOG="$HOME/Library/Logs/Fourfold/$LOGNAME_"
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$REPO/unreal/Fourfold.uproject" /Game/Fourfold/Maps/L_Lab \
  -game -windowed -ResX=1600 -ResY=900 -log="$LOGNAME_" -FFShot="$TIMES" -FFShotDir="$OUT" -FFShotQuit "$@" > /dev/null 2>&1
grep -E "Failed to compile|Critical error|Ensure condition failed" "$LOG" | head -5
ls "$OUT"/shot_*.png 2> /dev/null
echo "LOG $LOG"
