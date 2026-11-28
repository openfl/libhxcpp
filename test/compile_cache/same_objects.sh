#!/usr/bin/env bash
# Builds that share one HXCPP_COMPILE_CACHE with the same defines, as two
# projects built at once do, compile the same cached objects and make the
# same cached precompiled header, as the runtime's and hxcpp.h's are. Each
# compiler wrote them in place, so from an empty cache one build failed to
# write them while another did (C1083), or compiled or linked with one half
# written, or deleted, after its own write failed (C1083, LNK1104).
# This builds same_objects/Build.xml, whose cached group with a precompiled
# header has its sources in one folder, in four folders of its own at once,
# from an empty cache, three times. Each build must print 36, and leave
# nothing half made in the cache.
# Usage: test/compile_cache/same_objects.sh [extra build tool args, eg -DHXCPP_M64]
cd "$(dirname "$0")"
extra=("$@")
rm -rf bin/same_objects
mkdir -p bin/same_objects/shared
cp same_objects/pre.h same_objects/main.cpp bin/same_objects/shared/
cd bin/same_objects
for part in 0 1 2 3 4 5 6 7; do
  cat > "shared/part$part.cpp" <<EOF
#include <pre.h>

int part$part()
{
   std::map<std::string, int> seen;
   for (int i = 0; i <= $part; i++)
      seen[std::to_string(i)] = i;
   return (int)seen.size();
}
EOF
done
# A path the compiler reads as it is, on Windows too
shared=$(cd shared && { pwd -W 2>/dev/null || pwd; })
failed=0

# build <folder>: builds in a folder of its own and checks what it prints
build() {
  mkdir "$1"
  cp ../../same_objects/Build.xml "$1/"
  if ! (cd "$1" && haxelib run hxcpp Build.xml -DSHARED="$shared" "${extra[@]}" > build.log 2>&1); then
    echo "::error::the build in $1 failed"
    tail -20 "$1/build.log"
    return 1
  fi
  local printed
  printed=$("$1/prog" | tr -d '\r')
  if [ "$printed" != 36 ]; then
    echo "::error::the program built in $1 printed $printed"
    return 1
  fi
  echo "the build in $1 printed 36"
}

for round in 1 2 3; do
  export HXCPP_COMPILE_CACHE="$PWD/cache$round"
  pids=()
  for folder in a b c d; do
    build "$folder$round" > "$folder$round.out" 2>&1 &
    pids+=($!)
  done
  for pid in "${pids[@]}"; do
    wait $pid || failed=1
  done
  cat a$round.out b$round.out c$round.out d$round.out
  left=$(find "$HXCPP_COMPILE_CACHE" -name '*.tmp-*' -o -name '*.old-*')
  if [ -n "$left" ]; then
    echo "::error::the builds left entries half made in the cache: $left"
    failed=1
  fi
done
exit $failed
