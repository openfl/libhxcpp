#!/usr/bin/env bash
# HaxeFoundation/hxcpp#1469: trimming the compile cache, to HXCPP_CACHE_MB or
# by age, took an entry's use from its access and change times. Reusing an
# entry changes neither: an object up to date in the cache is linked from
# there, and Windows does not update access times. So entries every build
# reused looked as old as when they were compiled, and went first.
# This builds trim/Build.xml with a cache of its own, edits main.cpp and builds
# again, which reuses the rest in place, then dates every entry back to 2000,
# as if compiled long ago and never read since. Trimming to one day must keep
# all the last build used, so that build does nothing again, and remove the
# first main.cpp's object, which nothing has used since.
# Usage: test/compile_cache/trim.sh [extra build tool args, eg -DHXCPP_M64]
cd "$(dirname "$0")"
extra=("$@")
rm -rf bin/trim
mkdir -p bin/trim
cp trim/* bin/trim/
cd bin/trim
export HXCPP_COMPILE_CACHE="$PWD/cache"

# build <name> <value>: builds with a main.cpp of its own
build() {
  printf '#include <pre.h>\n\nint main() { return other() + libValue() - %s; }\n' "$2" > main.cpp
  if ! haxelib run hxcpp Build.xml "${extra[@]}" > "$1.log" 2>&1; then
    echo "::error::the build $1 failed"
    tail -20 "$1.log"
    exit 1
  fi
}

build first 1
build second 2
# The records of what each build used keep their dates
find cache -path cache/.used -prune -o -type f -exec touch -t 200001010000 {} +
haxelib run hxcpp cache days 1 "${extra[@]}" > trim.log 2>&1 || { echo "::error::trimming failed"; cat trim.log; exit 1; }
grep "Cache:" trim.log

failed=0
build again 2
if grep -q "Compiling group\|Creating\|Link:" again.log; then
  echo "::error::trimming removed what the last build used, so building it again did this:"
  grep "Compiling group\|Creating\|Link:" again.log
  failed=1
else
  echo "trimming kept what the last build used"
fi
build old 1
if grep -q "Compiling group" old.log; then
  echo "trimming removed the first main.cpp's object"
else
  echo "::error::trimming kept the first main.cpp's object, which nothing has used since"
  failed=1
fi
exit $failed
