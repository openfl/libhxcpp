#!/bin/sh
# Builds Main with exceptions, then again into the same folder with
# HXCPP_LINK_NO_EXCEPTIONS, which compiles without -fwasm-exceptions as well as
# linking without it, and runs that build under node. Without the compile
# cache, so the second build has only the stored options to tell it that
# every object must be compiled again. Needs what run.sh does.
# Usage: test/emscripten/link_no_exceptions.sh [extra haxe args]

cd "$(dirname "$0")"
unset HXCPP_COMPILE_CACHE
dir=bin/toggle-exceptions
haxe -m Main -D emscripten --cpp $dir "$@" || { echo "Failed to build with exceptions"; exit 1; }
haxe -m Main -D emscripten --cpp $dir -D HXCPP_LINK_NO_EXCEPTIONS "$@" || { echo "Failed to build again with HXCPP_LINK_NO_EXCEPTIONS"; exit 1; }
if ! actual=$(node $dir/Main.js); then
   echo "Failed: return code was $?"
   exit 1
fi
expected=$(tr -d '\r' < stdout.txt)
actual=$(printf '%s' "$actual" | tr -d '\r')
if [ "$actual" != "$expected" ]; then
   echo "Failed: output did not match"
   echo "Expected stdout:"
   echo "$expected"
   echo "Actual stdout:"
   echo "$actual"
   exit 1
fi
echo "Emscripten HXCPP_LINK_NO_EXCEPTIONS rebuild test passed"
