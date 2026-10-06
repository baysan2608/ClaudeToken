#!/usr/bin/env bash
# FourfoldCore: builds every CMake target with each available compiler and runs the checks.
#   unreal/CoreTests/run_all.sh                 # g++ and clang++ (whichever are installed)
#   FF_COMPILERS="clang++" FF_JOBS=8 unreal/CoreTests/run_all.sh
#   FF_BUILD_ROOT=/tmp/ffb unreal/CoreTests/run_all.sh
# Steps per compiler: configure (Ninja if present) -> build ff_all (ff_tests, ff_perf, ff_core_shared +
# ff_facade_link_test, ff_poison, ff_unity) -> ff_tests -q -> ff_facade_link_test -> ff_perf all 10.
# Before that, embed_data.py --check verifies Private/Generated/EmbeddedDataGen.cpp matches Data/*.json.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${FF_BUILD_ROOT:-${TMPDIR:-/tmp}/fourfold-core}"
JOBS="${FF_JOBS:-4}"
COMPILERS="${FF_COMPILERS:-g++ clang++}"
GEN=()
if command -v ninja > /dev/null 2>&1; then GEN=(-G Ninja); fi

python3 "$HERE/tools/embed_data.py" --check

ran=0
for cxx in $COMPILERS; do
  if ! command -v "$cxx" > /dev/null 2>&1; then
    echo "== $cxx: not installed, skipped"
    continue
  fi
  dir="$ROOT/build-${cxx//+/x}"
  echo "== $cxx -> $dir"
  cmake -S "$HERE" -B "$dir" "${GEN[@]}" -DCMAKE_CXX_COMPILER="$cxx" > /dev/null
  cmake --build "$dir" --target ff_all -j "$JOBS"
  "$dir/ff_tests" -q
  "$dir/ff_facade_link_test"
  "$dir/ff_perf" all 10
  ran=$((ran + 1))
done
if [ "$ran" -eq 0 ]; then
  echo "no compiler found (FF_COMPILERS=$COMPILERS)" >&2
  exit 1
fi
echo "== all checks passed ($ran compiler(s))"
