#!/usr/bin/env bash
# One command on the Mac: pull, build, and open Fourfold in Unreal (assets built automatically).
# On a build failure the log is pushed to the branch so Claude can read it.
#   bash ~/Fourfold/unreal/Tools/mac/run.sh
set -uo pipefail
BRANCH=claude/loving-dirac-0gtvnt
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
UPROJ="$REPO/unreal/Fourfold.uproject"
LOGDIR="$REPO/logs"; mkdir -p "$LOGDIR"
LOG="$LOGDIR/build_editor.log"

echo "== Updating code"
cd "$REPO" && git checkout -q "$BRANCH" && git pull -q --rebase --autostash origin "$BRANCH" || echo "(pull failed, using local copy)"

echo "== Finding Unreal 5.8"
UE=""
for d in "${UE_ROOT:-}" "/Users/Shared/Epic Games/UE_5.8" /Users/Shared/Epic\ Games/UE_5.* "/Applications/Epic Games/UE_5.8"; do
  [[ -n "$d" && -x "$d/Engine/Build/BatchFiles/Mac/Build.sh" ]] && { UE="$d"; break; }
done
if [[ -z "$UE" ]]; then
  echo "Unreal not found. Run again with: UE_ROOT=\"/path/to/UE_5.8\" bash $0"; exit 1
fi
echo "   $UE"

echo "== Building (first time takes several minutes)"
"$UE/Engine/Build/BatchFiles/Mac/Build.sh" FourfoldEditor Mac Development -project="$UPROJ" -waitmutex 2>&1 | tee "$LOG"
if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
  echo "== Build FAILED. Sending the log to Claude via GitHub..."
  git add -f "$LOG" && git commit -qm "Mac build log (failed)" && git pull -q --rebase --autostash origin "$BRANCH" && git push -q origin "$BRANCH" \
    && echo "   Sent. Tell Claude: 'build log pushed'." || echo "   Could not push. Paste the output of: grep -m 40 error $LOG"
  exit 1
fi

echo "== Build OK. Opening Unreal; asset setup runs automatically (watch Window > Output Log for [Fourfold])."
open -a "$UE/Engine/Binaries/Mac/UnrealEditor.app" --args "$UPROJ" \
  -ExecutePythonScript="$REPO/unreal/Content/Python/fourfold_setup.py"
echo "   When setup finishes: Content Browser > Fourfold/Maps/L_Lab, then Play."
