#!/usr/bin/env bash
# Exports the Godot project as an Xcode project for iOS (export_project_only).
#   tools/scripts/export_ios.sh <APPLE_TEAM_ID> [debug|release]
# Output: build/ios/Fourfold.xcodeproj (+ Fourfold/ folder with the .pck).
# Then open it in Xcode, pick your signing team/device and Run.
# The Team ID is the 10-character ID from developer.apple.com > Membership.
# It is written only into a temporary copy of export_presets.cfg, never committed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEAM="${1:?usage: export_ios.sh <APPLE_TEAM_ID> [debug|release]}"
MODE="${2:-debug}"
PRESETS="$ROOT/game/export_presets.cfg"
cp "$PRESETS" "$PRESETS.bak"
trap 'mv "$PRESETS.bak" "$PRESETS"' EXIT
sed -i.tmp "s/^application\/app_store_team_id=.*/application\/app_store_team_id=\"$TEAM\"/" "$PRESETS" && rm -f "$PRESETS.tmp"
mkdir -p "$ROOT/build/ios"
"$ROOT/tools/scripts/godot.sh" --headless --export-$MODE "iOS" ../build/ios/Fourfold.ipa
echo "Xcode project: $ROOT/build/ios/Fourfold.xcodeproj"
