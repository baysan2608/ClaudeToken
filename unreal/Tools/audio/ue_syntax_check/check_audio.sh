#!/bin/bash
# FourfoldAudio - syntax-check the module's Unreal source against the UE public headers (clang -fsyntax-only, mock UHT).
# Reuses the header mirror prepared by the game stream's tool (Source/Fourfold/Private/Logic/tools/ue_syntax_check/setup.sh):
#   FF_UE_CHECK_DIR=<dir with ue/ prelude.h incdirs.txt>   (default: the game stream's scratch copy)
#   FF_GEN=<dir for the generated mock .generated.h files>  (default: $FF_UE_CHECK_DIR/gen_audio)
# Usage: check_audio.sh            (prints only diagnostics located in Source/FourfoldAudio; exit 1 when there are any)
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../../Source" && pwd)"
DIR="${FF_UE_CHECK_DIR:-/tmp/claude-0/-home-user-ClaudeToken/37fdfe41-9b78-53ea-8620-b33d5491ff57/scratchpad/ue/game/uecheck58}"
GEN="${FF_GEN:-$DIR/gen_audio}"
MOCK="$SRC/Fourfold/Private/Logic/tools/ue_syntax_check"
[ -d "$DIR/ue" ] || { echo "no UE header mirror at $DIR (run $MOCK/setup.sh first)"; exit 2; }
R="$DIR/ue/Engine/Source/Runtime"
if [ ! -d "$GEN" ] || [ -n "$FF_REGEN" ]; then
  python3 "$MOCK/mockuht.py" "$GEN" "$R" "$SRC/Fourfold" "$SRC/FourfoldCore" "$SRC/FourfoldAudio" > /dev/null
else
  python3 "$MOCK/mockuht.py" "$GEN" "$SRC/FourfoldAudio" > /dev/null    # refresh only our own generated headers
fi
INC=""
while read d; do INC="$INC -I$d"; done < "$DIR/incdirs.txt"
cc() {
  timeout 300 clang++ -std=c++20 -fsyntax-only -ferror-limit=0 -Wno-everything ${FF_WARN} \
    -DPLATFORM_LINUX=1 -DPLATFORM_UNIX=1 -DUBT_COMPILED_PLATFORM=Linux -DUBT_COMPILED_TARGET=Game \
    -DUE_BUILD_DEVELOPMENT=1 -DUE_GAME=1 -DWITH_EDITOR=0 -DWITH_EDITORONLY_DATA=0 -DWITH_ENGINE=1 \
    -DWITH_UNREAL_DEVELOPER_TOOLS=0 -DWITH_UNREAL_TARGET_DEVELOPER_TOOLS=0 -DWITH_APPLICATION_CORE=1 -DWITH_COREUOBJECT=1 \
    -DWITH_VERSE_VM=0 -DUSE_STATS_WITHOUT_ENGINE=0 -DWITH_PLUGIN_SUPPORT=0 -DWITH_ACCESSIBILITY=1 -DWITH_PERFCOUNTERS=0 \
    -DWITH_FIXED_TIME_STEP_SUPPORT=1 -DUSE_LOGGING_IN_SHIPPING=0 -DWITH_LOGGING_TO_MEMORY=0 -DUSE_CACHE_FREED_OS_ALLOCS=1 \
    -DUSE_CHECKS_IN_SHIPPING=0 -DUSE_ESTIMATED_UTCNOW=0 -DUE_ALLOW_EXEC_COMMANDS_IN_SHIPPING=1 -DWITH_SERVER_CODE=1 \
    -DWITH_PUSH_MODEL=0 -DWITH_CEF3=0 -DWITH_LIVE_CODING=0 -DWITH_CPP_COROUTINES=0 -DWITH_CPP_MODULES=0 -DIS_MONOLITHIC=1 -DIS_PROGRAM=0 \
    -DUE_IS_ENGINE_MODULE=0 -DENABLE_PGO_PROFILE=0 -DWITH_DEV_AUTOMATION_TESTS=0 -DWITH_PERF_AUTOMATION_TESTS=0 \
    -DWITH_LOW_LEVEL_TESTS=0 -DEXPLICIT_TESTS_TARGET=0 -DWITH_TESTS=0 -DFORCE_ANSI_ALLOCATOR=0 -DWITH_STATE_STREAM=0 \
    -DUE_PROJECT_NAME=Fourfold -DUE_TARGET_NAME=Fourfold -DUE_MODULE_NAME=\"FourfoldAudio\" -DUE_PLUGIN_NAME=\"\" \
    -DUBT_MODULE_MANIFEST=\"x\" -DUBT_MODULE_MANIFEST_DEBUGGAME=\"x\" -DPLATFORM_LINUXARM64=0 -DUE_ENABLE_ICU=1 -DWITH_ICU_V64=0 \
    -DDLLEXPORT= -DDLLIMPORT= -DIMPLEMENT_ENCRYPTION_KEY_REGISTRATION\(\)= -DIMPLEMENT_SIGNING_KEY_REGISTRATION\(\)= -DFF_WITH_UE=1 \
    -DFOURFOLDAUDIO_API= -DUE_VALIDATE_INTERNAL_API=0 -DUE_VALIDATE_EXPERIMENTAL_API=0 -DUE_DISABLE_INLINE_GEN_CPP=1 \
    -I"$MOCK/stubs" -I"$DIR/ue/Engine/Source" -I"$DIR/ue/Engine/Shaders/Shared" \
    -include "$DIR/prelude.h" -include "$R/Engine/Public/EngineSharedPCH.h" \
    -I"$GEN" $INC -I"$SRC/Fourfold/Public" -I"$SRC/Fourfold/Private" -I"$SRC/FourfoldCore/Public" \
    -I"$SRC/FourfoldAudio/Public" -I"$SRC/FourfoldAudio/Private" -I"$SRC/FourfoldAudio/Private/Logic" "$@" 2>&1
}
fail=0
ONLY="${FF_ONLY:-}"     # e.g. FF_ONLY=FourfoldAudioSubsystem.cpp to check one file
for f in "$SRC/FourfoldAudio/Private/FourfoldAudioModule.cpp" "$SRC/FourfoldAudio/Private/FourfoldAudioSubsystem.cpp" \
         "$SRC/FourfoldAudio/Private/Logic/FFAManifest.cpp" "$SRC/FourfoldAudio/Private/Logic/FFARules.cpp" \
         "$SRC/FourfoldAudio/Private/Logic/FFAVoices.cpp" "$SRC/FourfoldAudio/Private/Logic/FFALoops.cpp"; do
  if [ -n "$ONLY" ] && [ "$(basename "$f")" != "$ONLY" ]; then continue; fi
  out=$(cc "$f" | grep -E "^$SRC/FourfoldAudio.*(error|warning)")
  [ -n "$out" ] && { echo "$out"; fail=1; }
  echo "checked $(basename "$f")"
done
exit $fail
