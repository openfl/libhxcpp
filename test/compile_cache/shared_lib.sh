#!/usr/bin/env bash
# Builds that share one HXCPP_COMPILE_CACHE with other defines, as a host
# with HXCPP_DEBUGGER and one without do, compile other objects for a cached
# group linked as a library, as the runtime is. The library was named by the
# toolchain and the project alone, so they linked their objects into the same
# file: one after the other, each linked it again, and at the same time one
# could link the other's library, as unresolved debugger symbols showed, or
# one half written, or fail to open it (LNK1104).
# This builds shared_lib/Build.xml, whose library returns VALUE, in folders
# of its own with one cache:
# - One after the other, a build with another VALUE must leave the first
#   one's library as it was, and the first build again must link nothing.
#   With its object trimmed from the cache, and so compiled again under the
#   same name, it must link its library again in its place.
# - With the libraries gone from the cache but their objects kept, four builds
#   at once, two with VALUEs of their own and two with the same one, must each
#   link and print their own VALUE, and leave no half made library behind.
# Usage: test/compile_cache/shared_lib.sh [extra build tool args, eg -DHXCPP_M64]
cd "$(dirname "$0")"
extra=("$@")
rm -rf bin/shared_lib
mkdir -p bin/shared_lib
cd bin/shared_lib
export HXCPP_COMPILE_CACHE="$PWD/cache"
libs=cache/shared_lib_test/lib
failed=0

# build <folder> <value>: builds in a folder of its own and checks that the
# program prints the value. The sources keep their dates, as a newer source
# compiles its cached object again
build() {
  if [ ! -d "$1" ]; then
    mkdir "$1"
    cp -p ../../shared_lib/* "$1/"
  fi
  if ! (cd "$1" && haxelib run hxcpp Build.xml -DVALUE="$2" "${extra[@]}" > build.log 2>&1); then
    echo "::error::the build in $1 with VALUE=$2 failed"
    tail -20 "$1/build.log"
    return 1
  fi
  local printed
  printed=$("$1/prog" | tr -d '\r')
  if [ "$printed" != "$2" ]; then
    echo "::error::the program built in $1 with VALUE=$2 printed $printed"
    return 1
  fi
  echo "the build in $1 printed $2"
}

# The libraries in the cache, each with a checksum
list_libs() {
  find "$libs" -type f ! -name '*.hash' -exec cksum {} + | sort -k3
}

build one 1 || exit 1
first=$(list_libs)
first_objects=$(find cache/shared_lib_test -path "$libs" -prune -o -type f -print)
build two 2 || exit 1
after=$(list_libs)
if [ -n "$first" ] && grep -qxF "$first" <<< "$after"; then
  echo "the build with another VALUE left the first one's library as it was"
else
  echo "::error::the build with another VALUE changed the library the first one linked"
  echo "before: $first"
  echo "after: $after"
  failed=1
fi
build one 1 || failed=1
if grep -q "Link:.*shared_lib_test" one/build.log; then
  echo "::error::the first build linked its library again:"
  grep "Link:" one/build.log
  failed=1
else
  echo "the first build again linked no library"
fi
# An object trimmed from the cache is compiled again under the same name
rm -f $first_objects
build one 1 || failed=1
if grep -q "Link:.*shared_lib_test" one/build.log && [ "$(list_libs | wc -l)" -eq 2 ]; then
  echo "an object compiled again linked its library again in its place"
else
  echo "::error::an object compiled again did not link its library again in its place:"
  grep "Link:" one/build.log
  list_libs
  failed=1
fi

for round in 1 2 3; do
  same=$((round * 10 + 3))
  for value in $((round * 10 + 1)) $((round * 10 + 2)) $same; do
    build "objects$value" $value > /dev/null || exit 1
  done
  rm -f "$libs"/*
  build "a$round" $((round * 10 + 1)) > "a$round.out" 2>&1 & a=$!
  build "b$round" $((round * 10 + 2)) > "b$round.out" 2>&1 & b=$!
  build "c$round" $same > "c$round.out" 2>&1 & c=$!
  build "d$round" $same > "d$round.out" 2>&1 & d=$!
  for pid in $a $b $c $d; do
    wait $pid || failed=1
  done
  cat "a$round.out" "b$round.out" "c$round.out" "d$round.out"
done
left=$(find "$libs" -name '*.tmp-*')
if [ -n "$left" ]; then
  echo "::error::the builds left libraries half made: $left"
  failed=1
fi
exit $failed
