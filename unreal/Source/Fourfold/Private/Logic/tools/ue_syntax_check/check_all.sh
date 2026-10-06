#!/bin/bash
# Fourfold - syntax-check every UE source of the module plus a whole-module unity TU; prints only diagnostics located
# in Source/Fourfold (the mock UHT leaves ~130 harmless errors inside unrelated engine headers).
# Usage: FF_UE_CHECK_DIR=... [FF_JOBS=3] [FF_WARN="-Wshadow-all ..."] ./check_all.sh
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../../../.." && pwd)"
MOD="$SRC/Fourfold"
JOBS="${FF_JOBS:-1}"
FILES=$(find "$MOD/Private" -name "*.cpp" -not -path "*/Logic/*" | sort)
check_one() {   # prints the module's own diagnostics for one file, then "checked <name>"
  "$HERE/cc_ue.sh" "$1" | grep -E "^$MOD.*(error|warning)"
  echo "checked $(basename "$1")"
}
export -f check_one; export HERE MOD
OUT=$(printf '%s\n' $FILES | xargs -P "$JOBS" -I{} bash -c 'check_one "$1"' _ {})
echo "$OUT"
fail=0
echo "$OUT" | grep -q -E "(error|warning)" && fail=1
U="${FF_UE_CHECK_DIR:?set FF_UE_CHECK_DIR}/ZZUnityCheck.gen.cpp"   # outside the source tree
ALL=$(find "$MOD/Private" -name "*.cpp" -not -path "*/tests/*" | sort)
for f in $ALL; do echo "#include \"$f\""; done > "$U"
out=$("$HERE/cc_ue.sh" "$U" | grep -E "^$MOD.*(error|warning)"); rm -f "$U"
[ -n "$out" ] && { echo "$out"; fail=1; }
echo "unity TU checked"
exit $fail
