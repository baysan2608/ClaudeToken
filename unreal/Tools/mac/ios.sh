#!/usr/bin/env bash
# Build, cook and install Fourfold (Development) on a cabled iPhone / iPad, then start it.
# Signing: team + bundle id from Config/DefaultEngine.ini (automatic signing; Xcode registers the device).
#   bash unreal/Tools/mac/ios.sh           build + cook + install + launch
#   bash unreal/Tools/mac/ios.sh --fast    reinstall the last build and cook (no compile, no cook) + launch
#   bash unreal/Tools/mac/ios.sh --check   only check the device and exit
#   add --no-launch to install without starting the app
# Device: the first paired physical iOS device, or IOS_DEVICE=<name or UDID>. Log: logs/ios_deploy.log
# Needs: iPad Developer Mode on, unlocked, USB cable (the install step uses usbmux; Wi-Fi only works for Xcode itself).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
UPROJ="$REPO/unreal/Fourfold.uproject"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
LOGDIR="$REPO/logs"; mkdir -p "$LOGDIR"; LOG="$LOGDIR/ios_deploy.log"
MODE=full; LAUNCH=1
for a in "$@"; do
  case "$a" in
    --fast) MODE=fast ;; --check) MODE=check ;; --no-launch) LAUNCH=0 ;;
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
  STEPS=(-skipcook -stage -pak -package -deploy -nocompileeditor)
else
  STEPS=(-build -cook -stage -pak -package -deploy)
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
echo "== Installed $BUNDLE on $NAME"
if [[ $LAUNCH == 1 ]]; then
  xcrun devicectl device process launch --device "$UDID" "$BUNDLE" > /dev/null 2>&1 \
    && echo "== Started. Four-finger tap opens the console: stat unit / stat fps" \
    || echo "== Installed, but could not start it remotely: tap the Fourfold icon (unlock the device first)."
fi
