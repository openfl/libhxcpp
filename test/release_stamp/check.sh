#!/bin/bash
# Checks what `Release stamp` writes, in a copy of the files it reads and writes.
# Usage, from anywhere: bash test/release_stamp/check.sh
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

check() {
   if [ "$2" = "$3" ]; then
      echo "ok   $1"
   else
      echo "FAIL $1"
      echo "       got:      $2"
      echo "       expected: $3"
      failures=$((failures + 1))
   fi
}

# A release commit of 6.0.1: the version in haxelib.json and the top heading of Changes.md
sed 's/"version": "[^"]*"/"version": "6.0.1"/' "$ROOT/haxelib.json" > "$WORK/haxelib.json"
printf 'libhxcpp 6.0.1\n------------------------------------------------------------\n\n* A change\n' > "$WORK/Changes.md"
cp "$ROOT/extraParams.hxml" "$WORK/"
mkdir -p "$WORK/include"

out=$(cd "$WORK" && haxe -p "$ROOT/tools/release" --run Release stamp 2>&1)
check "stamp succeeds" "$?" "0"
check "stamp reports the version" "$(printf '%s\n' "$out" | tr -d '\r' | head -1)" "version=6.0.1"

stamped=$(tr -d '\r' < "$WORK/extraParams.hxml")
for name in libhxcpp hxcpp; do
   check "extraParams defines $name as the version" \
      "$(printf '%s\n' "$stamped" | grep -c "^--macro haxe.macro.Compiler.define(\"$name\",\"6.0.1\")\$")" "1"
done
check "extraParams keeps no other version" "$(printf '%s\n' "$stamped" | grep -c 'Compiler.define("\(lib\)\?hxcpp","6.0.0")')" "0"

# Every other line of the checkout's extraParams.hxml, such as the incremental
# collector's configuration macro, reaches the release as it is
while IFS= read -r line; do
   case "$line" in
      ""|*'Compiler.define("libhxcpp",'*|*'Compiler.define("hxcpp",'*) continue ;;
   esac
   check "extraParams keeps: $line" "$(printf '%s\n' "$stamped" | grep -cxF -- "$line")" "1"
done < <(tr -d '\r' < "$ROOT/extraParams.hxml")

check "HxcppVersion.h names the version" "$(grep -c '"6.0.1"' "$WORK/include/HxcppVersion.h")" "2"

# Stamping twice changes nothing more
cp "$WORK/extraParams.hxml" "$WORK/first.hxml"
(cd "$WORK" && haxe -p "$ROOT/tools/release" --run Release stamp > /dev/null 2>&1)
check "a second stamp leaves extraParams as it was" "$(cmp -s "$WORK/first.hxml" "$WORK/extraParams.hxml" && echo same)" "same"

exit $failures
