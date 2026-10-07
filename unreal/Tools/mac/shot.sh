#!/usr/bin/env bash
# Starts the Mac game on L_Lab in a window, waits until it is playing, takes a screenshot, quits.
#   bash unreal/Tools/mac/shot.sh [out.png] [extra seconds before the shot] [extra game args...]
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
OUT="${1:-$REPO/logs/shot.png}"; WAIT="${2:-12}"; shift $(( $# > 2 ? 2 : $# ))
LOG="$HOME/Library/Logs/Fourfold/FourfoldShot.log"; rm -f "$LOG"
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$REPO/unreal/Fourfold.uproject" /Game/Fourfold/Maps/L_Lab \
  -game -windowed -ResX=1600 -ResY=900 -log=FourfoldShot.log "$@" > /dev/null 2>&1 &
PID=$!
for _ in $(seq 1 180); do grep -q "Bringing up level for play" "$LOG" 2>/dev/null && break; sleep 1; done
sleep "$WAIT"
osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true" > /dev/null 2>&1
sleep 1
screencapture -x "$OUT"
kill "$PID" 2> /dev/null; wait "$PID" 2> /dev/null
grep -E "Failed to compile|Critical error|Ensure condition failed" "$LOG" | head -5
echo "$OUT"
