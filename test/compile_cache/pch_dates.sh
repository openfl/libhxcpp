#!/usr/bin/env bash
# HaxeFoundation/hxcpp#1466: clang keeps each header's date in a precompiled
# header and refuses it once a date differs, though the content is the same.
# The compile cache finds a precompiled header by content, so a fresh clone, a
# branch switched and back or a restored CI cache stopped the next compile with
# "has been modified since the precompiled header was built".
# This builds a program with the compile cache, then edits it and builds it
# again with hxcpp.h dated later, and then earlier, than the cached precompiled
# header. Each build must succeed and print what it was edited to. Only clang
# and clang-cl check the dates, but every compiler must pass.
# Uses HXCPP_COMPILE_CACHE, or a cache of its own when that is not set.
# Usage: test/compile_cache/pch_dates.sh [extra haxe args, eg -D HXCPP_M64 or -D emscripten]
cd "$(dirname "$0")"
extra=("$@")
hxcpp=$(haxelib libpath hxcpp | tr -d '\r')
header="${hxcpp//\\//}/include/hxcpp.h"
[ -f "$header" ] || { echo "::error::no hxcpp.h at $header"; exit 1; }
rm -rf bin
mkdir -p bin/src
[ -n "$HXCPP_COMPILE_CACHE" ] || export HXCPP_COMPILE_CACHE="$PWD/bin/cache"

# Later builds in the same checkout see the header's date as it was
touch -r "$header" bin/header-date
trap 'touch -r bin/header-date "$header"' EXIT

run() {
  if [ -f bin/cpp/Main.js ]; then node bin/cpp/Main.js
  elif [ -f bin/cpp/Main.exe ]; then bin/cpp/Main.exe
  else bin/cpp/Main
  fi
}

# build <name>: builds and runs a Main that prints its name
build() {
  printf 'class Main {\n\tstatic function main() {\n\t\tSys.println("%s");\n\t}\n}\n' "$1" > bin/src/Main.hx
  if ! haxe -cp bin/src -m Main --cpp bin/cpp "${extra[@]}" > bin/build.log 2>&1; then
    echo "::error::the build $1 failed"
    tail -20 bin/build.log
    return 1
  fi
  local out
  out=$(run | tr -d '\r')
  if [ "$out" != "$1" ]; then
    echo "::error::the build $1 printed \"$out\""
    return 1
  fi
  echo "$1: built and printed its name"
}

failed=0
build first || failed=1
touch "$header"
build later || failed=1
touch -t 200001010000 "$header"
build earlier || failed=1
exit $failed
