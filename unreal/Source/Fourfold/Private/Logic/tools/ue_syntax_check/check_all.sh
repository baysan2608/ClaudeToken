#!/bin/bash
# Fourfold - syntax-check every UE source of the module plus a whole-module unity TU; prints only diagnostics located
# in Source/Fourfold (the mock UHT leaves ~130 harmless errors inside unrelated engine headers).
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../../../.." && pwd)"
MOD="$SRC/Fourfold"
FILES=$(find "$MOD/Private" -name "*.cpp" -not -path "*/Logic/*" | sort)
fail=0
for f in $FILES; do
  out=$("$HERE/cc_ue.sh" "$f" | grep -E "^$MOD.*(error|warning)")
  [ -n "$out" ] && { echo "$out"; fail=1; }
  echo "checked $(basename "$f")"
done
U="$MOD/Private/ZZUnityCheck.gen.cpp"
ALL=$(find "$MOD/Private" -name "*.cpp" -not -path "*/tests/*" -not -name "ZZUnityCheck*" | sort)   # listed before $U exists
for f in $ALL; do echo "#include \"$f\""; done > "$U"
out=$("$HERE/cc_ue.sh" "$U" | grep -E "^$MOD.*(error|warning)"); rm -f "$U"
[ -n "$out" ] && { echo "$out"; fail=1; }
echo "unity TU checked"
exit $fail
