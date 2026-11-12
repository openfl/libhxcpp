#!/usr/bin/env bash
# HXCPP_PGO from start to end on this machine's compiler (CXX picks GCC or Clang on
# Linux): a use build without counts refuses, a generate build runs and counts, a use
# build reads the counts, a second use build with nothing changed links nothing, and a
# generate build that links again clears the counts.
#
# Needs haxe with libhxcpp set up. Usage: test/pgo/check.sh [arch define, default HXCPP_M64]

set -u
ARCH=${1:-HXCPP_M64}
cd "$(dirname "$0")"
rm -rf bin

exe=bin/Main
[ -n "${WINDIR:-}" ] && exe=bin/Main.exe

failures=0
fail() { echo "FAIL $1"; failures=$((failures + 1)); }
ok() { echo "ok   $1"; }

build() {
   haxe -main Main -D $ARCH "$@" --cpp bin > build.log 2>&1
}

# Counts the program wrote: beside the program with MSVC, in pgo otherwise
counts() {
   find bin -maxdepth 1 -name '*.pgc' | wc -l
   find bin/pgo -name '*.pgc' -o -name '*.gcda' -o -name '*.profraw' 2> /dev/null | wc -l
}
count_total() { counts | awk '{ s += $1 } END { print s }'; }

if build -D HXCPP_PGO=use; then fail "a use build without counts should refuse"; cat build.log
elif grep -q "found no counts" build.log; then ok "a use build without counts refuses"
else fail "a use build without counts failed for another reason"; cat build.log; fi

if build -D HXCPP_PGO=generate; then ok "generate builds"; else fail "generate build"; cat build.log; exit 1; fi
[ "$($exe 1000)" = "total=1000" ] && [ "$($exe)" = "total=100000" ] || fail "the instrumented program's output"
n=$(count_total)
[ "$n" -gt 0 ] && ok "the instrumented program wrote counts ($n files)" || fail "the instrumented program wrote no counts"

if build -D HXCPP_PGO=use; then ok "use builds"; else fail "use build"; cat build.log; exit 1; fi
[ "$($exe)" = "total=100000" ] && ok "the optimised program runs" || fail "the optimised program's output"

sleep 1; touch bin/marker
build -D HXCPP_PGO=use || fail "second use build"
[ -z "$(find "$exe" -newer bin/marker)" ] && ok "a use build with nothing changed links nothing" || fail "a use build with nothing changed linked again"

build -D HXCPP_PGO=generate || fail "second generate build"
[ "$(count_total)" = 0 ] && ok "a generate build that links clears the counts" || fail "a generate build left counts"

rm -rf bin build.log
[ "$failures" = 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
