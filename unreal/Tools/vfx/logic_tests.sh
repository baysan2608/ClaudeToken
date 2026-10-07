#!/usr/bin/env bash
# Builds and runs the FourfoldFX engine-free logic tests with clang++ alone (no cmake / ninja needed; mirrors
# Source/FourfoldFX/Private/Logic/tests/CMakeLists.txt minus the core soak and the CPU preview).
#   bash unreal/Tools/vfx/logic_tests.sh [build_dir] [test filter]
#   bash unreal/Tools/vfx/logic_tests.sh <build_dir> --write-config unreal/Content/Fourfold/Data/fx_config.json
set -euo pipefail
U="$(cd "$(dirname "$0")/../.." && pwd)"
L="$U/Source/FourfoldFX/Private/Logic"
OUT="${1:-${TMPDIR:-/tmp}/ffx_logic_tests}"
shift $(( $# > 0 ? 1 : 0 ))
mkdir -p "$OUT/obj"
FLAGS=(-std=c++20 -O1 -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Wconversion -Wundef -Werror
       -Wshorten-64-to-32 -Wshadow-all -include "$U/CoreTests/ue_macro_poison.h" -I"$L" -I"$U/Source/FourfoldCore/Public")
newest_header=$(ls -t "$L"/*.h | head -1)
pids=()
build() {   # source object [extra flags...]
	local src="$1" obj="$2"
	shift 2
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$newest_header" -nt "$obj" ]; then
		rm -f "$obj"
		clang++ "${FLAGS[@]}" "$@" -c "$src" -o "$obj" &
		pids+=($!)
	fi
}
for f in "$L"/*.cpp "$U/Source/FourfoldCore/Private/Util/Json.cpp"; do build "$f" "$OUT/obj/lib_$(basename "$f" .cpp).o"; done
for f in "$L"/tests/*.cpp; do
	case "$(basename "$f")" in fx_preview.cpp|core_soak.cpp) continue ;; esac
	build "$f" "$OUT/obj/test_$(basename "$f" .cpp).o" -DFF_LOGIC_TESTS=1 "-DFFX_UNREAL_DIR=\"$U\""
done
fail=0
for p in ${pids[@]+"${pids[@]}"}; do wait "$p" || fail=1; done
[ "$fail" = 0 ] || { echo "compile failed" >&2; exit 1; }
clang++ "$OUT"/obj/*.o -o "$OUT/ffx_logic_tests"
# Unreal compiles the module in unity batches: every Logic source must also compile in ONE translation unit.
for f in "$L"/*.cpp; do echo "#include \"$f\""; done > "$OUT/unity.cpp"
clang++ "${FLAGS[@]}" -fsyntax-only "$OUT/unity.cpp"
"$OUT/ffx_logic_tests" "$@"
