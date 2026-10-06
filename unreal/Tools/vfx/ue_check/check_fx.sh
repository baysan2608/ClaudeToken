#!/bin/bash
# FourfoldFX - syntax-check the module's Unreal sources against public UE 5.8 headers (clang -fsyntax-only, mock UHT).
# Needs a prepared UE header mirror (the game stream's setup: Source/Fourfold/Private/Logic/tools/ue_syntax_check/setup.sh
# with FF_UE_CHECK_DIR=<dir>) and the ProceduralMeshComponent plugin sources (sparse clone, see below).
#   FF_UE_CHECK_DIR=<mirror dir> FF_UE_PMC_DIR=<clone with Engine/Plugins/Runtime/ProceduralMeshComponent> ./check_fx.sh
# Plugin clone: git clone --depth 1 --filter=blob:none --no-checkout https://github.com/AFIshInWater/UE5.8.git ue &&
#   git -C ue sparse-checkout init --no-cone && echo '/Engine/Plugins/Runtime/ProceduralMeshComponent/Source/' >
#   ue/.git/info/sparse-checkout && git -C ue checkout
# Prints only diagnostics inside Source/FourfoldFX (the mock UHT leaves unrelated noise in engine headers).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../../Source" && pwd)"
DIR="${FF_UE_CHECK_DIR:?set FF_UE_CHECK_DIR (prepared UE header mirror)}"
PMC="${FF_UE_PMC_DIR:?set FF_UE_PMC_DIR (clone containing the ProceduralMeshComponent plugin)}"
PMC_PUB="$PMC/Engine/Plugins/Runtime/ProceduralMeshComponent/Source/ProceduralMeshComponent/Public"
GEN="${FF_FX_GEN_DIR:-$DIR/genfx}"
R="$DIR/ue/Engine/Source/Runtime"
mkdir -p "$GEN"
python3 "$HERE/mockuht.py" "$GEN" "$R" "$SRC/Fourfold" "$SRC/FourfoldCore" "$SRC/FourfoldFX" "$PMC_PUB" >/dev/null
PRE="$GEN/fx_prelude.h"
{ cat "$DIR/prelude.h"; echo "#define FOURFOLDFX_API"; echo "#define PROCEDURALMESHCOMPONENT_API"; } > "$PRE"
INC=""
while read d; do INC="$INC -I$d"; done < "$DIR/incdirs.txt"
cc_one() {
  timeout 300 clang++ -std=c++20 -fsyntax-only -ferror-limit=0 -Wno-everything ${FF_WARN:-} \
  -DPLATFORM_LINUX=1 -DPLATFORM_UNIX=1 -DUBT_COMPILED_PLATFORM=Linux -DUBT_COMPILED_TARGET=Game \
  -DUE_BUILD_DEVELOPMENT=1 -DUE_GAME=1 -DWITH_EDITOR=0 -DWITH_EDITORONLY_DATA=0 -DWITH_ENGINE=1 \
  -DWITH_UNREAL_DEVELOPER_TOOLS=0 -DWITH_UNREAL_TARGET_DEVELOPER_TOOLS=0 -DWITH_APPLICATION_CORE=1 -DWITH_COREUOBJECT=1 \
  -DWITH_VERSE_VM=0 -DUSE_STATS_WITHOUT_ENGINE=0 -DWITH_PLUGIN_SUPPORT=0 -DWITH_ACCESSIBILITY=1 -DWITH_PERFCOUNTERS=0 \
  -DWITH_FIXED_TIME_STEP_SUPPORT=1 -DUSE_LOGGING_IN_SHIPPING=0 -DWITH_LOGGING_TO_MEMORY=0 -DUSE_CACHE_FREED_OS_ALLOCS=1 \
  -DUSE_CHECKS_IN_SHIPPING=0 -DUSE_ESTIMATED_UTCNOW=0 -DUE_ALLOW_EXEC_COMMANDS_IN_SHIPPING=1 -DWITH_SERVER_CODE=1 \
  -DWITH_PUSH_MODEL=0 -DWITH_CEF3=0 -DWITH_LIVE_CODING=0 -DWITH_CPP_COROUTINES=0 -DWITH_CPP_MODULES=0 -DIS_MONOLITHIC=1 -DIS_PROGRAM=0 \
  -DUE_IS_ENGINE_MODULE=0 -DENABLE_PGO_PROFILE=0 -DWITH_DEV_AUTOMATION_TESTS=0 -DWITH_PERF_AUTOMATION_TESTS=0 \
  -DWITH_LOW_LEVEL_TESTS=0 -DEXPLICIT_TESTS_TARGET=0 -DWITH_TESTS=0 -DFORCE_ANSI_ALLOCATOR=0 -DWITH_STATE_STREAM=0 \
  -DUE_PROJECT_NAME=Fourfold -DUE_TARGET_NAME=Fourfold -DUE_MODULE_NAME=\"FourfoldFX\" -DUE_PLUGIN_NAME=\"\" \
  -DUBT_MODULE_MANIFEST=\"x\" -DUBT_MODULE_MANIFEST_DEBUGGAME=\"x\" -DPLATFORM_LINUXARM64=0 -DUE_ENABLE_ICU=1 -DWITH_ICU_V64=0 \
  -DDLLEXPORT= -DDLLIMPORT= -DIMPLEMENT_ENCRYPTION_KEY_REGISTRATION\(\)= -DIMPLEMENT_SIGNING_KEY_REGISTRATION\(\)= -DFF_WITH_UE=1 \
  -DUE_VALIDATE_INTERNAL_API=0 -DUE_VALIDATE_EXPERIMENTAL_API=0 -DUE_DISABLE_INLINE_GEN_CPP=1 \
  -I"$DIR/ue/Engine/Source" -I"$DIR/ue/Engine/Shaders/Shared" \
  -include "$PRE" -include "$DIR/ue/Engine/Source/Runtime/Engine/Public/EngineSharedPCH.h" \
  -I"$GEN" $INC -I"$PMC_PUB" \
  -I"$SRC/Fourfold/Public" -I"$SRC/FourfoldCore/Public" \
  -I"$SRC/FourfoldFX/Public" -I"$SRC/FourfoldFX/Private" -I"$SRC/FourfoldFX/Private/Logic" "$@" 2>&1
}
fail=0
MOD="$SRC/FourfoldFX"
for f in $(find "$MOD/Private" -maxdepth 1 -name "*.cpp" | sort); do
  out=$(cc_one "$f" | grep -E "^$MOD.*(error|warning)")
  [ -n "$out" ] && { echo "$out"; fail=1; }
  echo "checked $(basename "$f")"
done
# the whole module (UE glue + engine-free logic) as one unity translation unit, like UnrealBuildTool's unity build
U="$GEN/ZZFxUnity.gen.cpp"
for f in $(find "$MOD/Private" -name "*.cpp" -not -path "*/tests/*" | sort); do echo "#include \"$f\""; done > "$U"
out=$(cc_one "$U" | grep -E "^$MOD.*(error|warning)")
[ -n "$out" ] && { echo "$out"; fail=1; }
echo "unity TU checked"
exit $fail
