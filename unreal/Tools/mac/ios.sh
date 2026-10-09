#!/usr/bin/env bash
# Build, cook and install Fourfold (Development) on a cabled iPhone / iPad, then start it.
# Signing: team + bundle id from Config/DefaultEngine.ini (automatic signing; Xcode registers the device).
#   bash unreal/Tools/mac/ios.sh           build + cook + install + launch
#   bash unreal/Tools/mac/ios.sh --fast    reinstall the last build and cook (no compile, no cook) + launch
#   bash unreal/Tools/mac/ios.sh --cook    cook + install the last compiled binary (no compile; UAT's -build step can fail in
#                                          Xcode's 'Touch UBT generated tiles' pre-action while a direct xcodebuild works)
#   bash unreal/Tools/mac/ios.sh --check   only check the device and exit
#   add --no-launch to install without starting the app
# Device: the first paired physical iOS device, or IOS_DEVICE=<name or UDID>. Log: logs/ios_deploy.log
# Needs: iPad Developer Mode on, unlocked, USB cable.
# On iOS 27 use this, not the editor's Launch: it also patches the iOS 27 launch crash (see the note before the install).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
UPROJ="$REPO/unreal/Fourfold.uproject"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
LOGDIR="$REPO/logs"; mkdir -p "$LOGDIR"; LOG="$LOGDIR/ios_deploy.log"
MODE=full; LAUNCH=1
for a in "$@"; do
  case "$a" in
    --fast) MODE=fast ;; --cook) MODE=cook ;; --check) MODE=check ;; --no-launch) LAUNCH=0 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done

[[ -x "$UE/Engine/Build/BatchFiles/RunUAT.sh" ]] || { echo "Unreal not found at $UE (set UE_ROOT)"; exit 1; }
[[ -d "$UE/Engine/Binaries/IOS" ]] || { echo "Engine iOS target platform missing: Launcher > Library > 5.8 > Options > iOS"; exit 1; }
BUNDLE="$(grep -m1 '^BundleIdentifier=' "$REPO/unreal/Config/DefaultEngine.ini" | cut -d= -f2)"

echo "== Device"
JSON="$(mktemp -t fourfold_dev)"; trap 'rm -f "$JSON"' EXIT
xcrun devicectl list devices --json-output "$JSON" > /dev/null 2>&1 || { echo "devicectl failed (Xcode command line tools?)"; exit 1; }
DEV="$(/usr/bin/python3 -I - "$JSON" "${IOS_DEVICE:-}" << 'PY'
import json, sys
want = sys.argv[2]
for d in json.load(open(sys.argv[1]))["result"]["devices"]:
    hw, dp, cp = d["hardwareProperties"], d["deviceProperties"], d["connectionProperties"]
    if hw.get("reality") != "physical" or hw.get("platform") != "iOS":
        continue
    if want and want not in (dp.get("name"), hw.get("udid"), d.get("identifier")):
        continue
    print("|".join([hw["udid"], dp.get("name", "?"), dp.get("osVersionNumber", "?"),
                    cp.get("tunnelState", "?"), cp.get("pairingState", "?"), dp.get("developerModeStatus", "?")]))
    break
PY
)"
[[ -n "$DEV" ]] || { echo "No paired iPhone / iPad found. Cable it, unlock it, tap Trust."; exit 1; }
IFS='|' read -r UDID NAME OSV TUNNEL PAIRING DEVMODE <<< "$DEV"
echo "   $NAME  iOS $OSV  udid $UDID  connection=$TUNNEL  pairing=$PAIRING  developer-mode=$DEVMODE"
OK=1
[[ "$PAIRING" == paired ]] || { echo "   Not paired: unlock the device and tap Trust on it."; OK=0; }
[[ "$DEVMODE" == enabled ]] || { echo "   Developer Mode is off. Cabled + unlocked, open Xcode > Window > Devices and Simulators and select the device
   (that reveals the switch), then on it: Settings > Privacy & Security > Developer Mode (bottom) > On, restart, confirm."; OK=0; }
[[ "$TUNNEL" != unavailable ]] || { echo "   Not reachable: plug in the USB cable and unlock the device."; OK=0; }
[[ $OK == 1 ]] || exit 1
[[ "$MODE" == check ]] && { echo "   Ready."; exit 0; }

if pgrep -f "UnrealEditor|UnrealBuildTool|AutomationTool" > /dev/null && [[ -z "${FORCE:-}" ]]; then
  echo "Another Unreal process is running (another session may be measuring). Wait, or run again with FORCE=1."
  pgrep -fl "UnrealEditor|UnrealBuildTool|AutomationTool" | cut -c1-160; exit 1
fi

if [[ "$MODE" == fast ]]; then
  [[ -d "$REPO/unreal/Saved/StagedBuilds/IOS/cookeddata" ]] || { echo "No previous iOS cook; run without --fast."; exit 1; }
  STEPS=(-skipcook -stage -pak -package -nocompileeditor)
elif [[ "$MODE" == cook ]]; then
  STEPS=(-skipbuild -cook -IgnoreCookErrors -stage -pak -package)
else
  STEPS=(-build -cook -IgnoreCookErrors -stage -pak -package)
fi
echo "== BuildCookRun ($MODE) -> $LOG"
"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$UPROJ" -platform=IOS -clientconfig=Development \
  "${STEPS[@]}" -device="IOS@$UDID" -utf8output -nop4 -unattended > "$LOG" 2>&1
RC=$?
if [[ $RC -ne 0 ]]; then
  echo "== FAILED (exit $RC). Last errors:"
  grep -E "error:|Error:|AutomationException|Failed to deploy" "$LOG" | tail -8 | cut -c1-300
  exit $RC
fi
# iOS 27 traps at launch (_UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption) when an app linked against
# SDK 27+ has no UIScene lifecycle. UE 5.8 can adopt it (IOSRuntimeSettings bUseSceneBasedLifecycle), but only in an
# engine built from source: the Launcher engine's precompiled ApplicationCore has no IOSSceneDelegate. Until then, mark
# the executable as linked against SDK 26.0 (warning only) and re-sign with the same certificate and entitlements.
APP="$REPO/unreal/Saved/StagedBuilds/IOS/Fourfold.app"
SDKV="$(xcrun vtool -show-build "$APP/Fourfold" 2>/dev/null | awk '$1 == "sdk" {print $2; exit}')"
if [[ "${SDKV%%.*}" -ge 27 ]]; then
  echo "== Executable linked against SDK $SDKV: marking it 26.0 and re-signing (no UIScene lifecycle yet)"
  SIGN="$(mktemp -d -t fourfold_sign)"
  codesign -d --entitlements - --xml "$APP" > "$SIGN/ents.plist" 2> /dev/null \
    && codesign -d --extract-certificates="$SIGN/cert" "$APP" 2> /dev/null \
    && codesign --remove-signature "$APP/Fourfold" \
    && xcrun vtool -set-build-version ios 17.0 26.0 -replace -output "$SIGN/Fourfold" "$APP/Fourfold" \
    && mv "$SIGN/Fourfold" "$APP/Fourfold" && chmod +x "$APP/Fourfold" \
    && codesign -f -s "$(shasum "$SIGN/cert0" | cut -c1-40)" --entitlements "$SIGN/ents.plist" --generate-entitlement-der "$APP" 2> /dev/null \
    && codesign -v "$APP" || { echo "== Re-signing failed"; exit 1; }
fi
echo "== Installing on $NAME"
xcrun devicectl device install app --device "$UDID" "$APP" >> "$LOG" 2>&1 || { echo "== Install failed (see $LOG)"; exit 1; }
echo "== Installed $BUNDLE on $NAME"
if [[ $LAUNCH == 1 ]]; then
  xcrun devicectl device process launch --device "$UDID" --terminate-existing "$BUNDLE" > /dev/null 2>&1 \
    && echo "== Started. Four-finger tap opens the console: stat unit / stat fps" \
    || echo "== Installed, but could not start it remotely: tap the Fourfold icon (unlock the device first)."
fi
