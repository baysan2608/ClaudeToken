#!/bin/bash
# Fourfold - one-time setup of the UE header syntax check (Linux, clang). Downloads the public headers of a UE source
# mirror (sparse, headers only, ~150 MB) into $FF_UE_CHECK_DIR, then writes the mock UHT headers, include list and
# API-macro prelude there. Usage: FF_UE_CHECK_DIR=/some/scratch [FF_UE_MIRROR=<git url>] ./setup.sh
# Default mirror: a public UE 5.8.2 tree. Also used: github.com/F-Fumino/UE5.7 (5.7), github.com/Pekyyyyyy/Toon-UE (5.5.2).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DIR="${FF_UE_CHECK_DIR:?set FF_UE_CHECK_DIR to a scratch directory}"
REPO="${FF_UE_MIRROR:-https://github.com/AFIshInWater/UE5.8.git}"
SRC="$HERE/../../../../.."          # unreal/Source
mkdir -p "$DIR"
if [ ! -d "$DIR/ue/.git" ]; then
  git clone --depth 1 --filter=blob:none --no-checkout "$REPO" "$DIR/ue"
  git -C "$DIR/ue" sparse-checkout init --no-cone
  printf '%s\n' '/Engine/Source/Runtime/*/Public/' '/Engine/Source/Runtime/*/Classes/' '/Engine/Source/Runtime/*/Internal/' \
    '/Engine/Source/Runtime/*/*/Public/' '/Engine/Source/Runtime/*/*/Classes/' '/Engine/Source/Runtime/*/*/Internal/' \
    '!/Engine/Source/Runtime/*/Public/ThirdParty/' '/Engine/Shaders/Shared/' '/Engine/Source/Runtime/Launch/Resources/' \
    > "$DIR/ue/.git/info/sparse-checkout"
  git -C "$DIR/ue" checkout
fi
R="$DIR/ue/Engine/Source/Runtime"
find "$R" -type d \( -name Public -o -name Classes -o -name Internal \) -not -path "*/Public/*" -not -path "*/Classes/*" \
  -not -path "*/Internal/*" | sort > "$DIR/incdirs.txt"
grep -rhoE "\b[A-Z][A-Z0-9_]*_API\b" "$R" --include=*.h --include=*.inl | sort -u > "$DIR/api_all.txt"
grep -rhoE "#\s*define\s+[A-Z][A-Z0-9_]*_API\b" "$R" --include=*.h --include=*.inl | sed -E 's/#\s*define\s+//' | sort -u > "$DIR/api_defined.txt"
{ echo "#pragma once"; comm -23 "$DIR/api_all.txt" "$DIR/api_defined.txt" | grep -v -E "UE_VALIDATE_[A-Z]+_API" | sed 's/^/#define /'
  echo "#define FOURFOLD_API"; printf '#include <cstddef>\nusing std::nullptr_t;\n'; } > "$DIR/prelude.h"
rm -rf "$DIR/gen"
python3 "$HERE/mockuht.py" "$DIR/gen" "$R" "$SRC/Fourfold" "$SRC/FourfoldCore"
echo "ready: $DIR"
