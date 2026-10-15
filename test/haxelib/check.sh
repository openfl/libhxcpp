#!/usr/bin/env bash
# Check how libhxcpp installs alongside hxcpp, in a haxelib repository of its own:
# setup points the hxcpp name at libhxcpp, an update leaves that redirect on the
# old version until the next run moves it, and off hands hxcpp back.
#
# Needs haxe, haxelib, neko and python3. Usage: test/haxelib/check.sh

set -u
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
PYTHON=$(command -v python3 || command -v python)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/repo" "$WORK/caller"
export HAXELIB_PATH="$WORK/repo"
# Under Git Bash on Windows, haxelib needs the repository as a Windows path
if command -v cygpath > /dev/null; then
   export HAXELIB_PATH=$(cygpath -m "$WORK/repo")
fi
export HXCPP_NONINTERACTIVE=1
cd "$WORK/caller"

failures=0
check() {
   if [ "$2" = "$3" ]; then
      echo "ok   $1"
   else
      echo "FAIL $1"
      echo "       expected: $3"
      echo "       got:      $2"
      failures=$((failures + 1))
   fi
}
contains() {
   if printf '%s' "$2" | grep -qF -- "$3"; then
      echo "ok   $1"
   else
      echo "FAIL $1: no \"$3\" in"
      printf '%s\n' "$2" | sed 's/^/       /'
      failures=$((failures + 1))
   fi
}
normpath() {
   local out
   out=$(printf '%s\n' "$1" | tr -d '\r' | head -1 | tr '\\' '/' | sed 's#/*$##')
   # Windows paths compare without regard to case
   if command -v cygpath > /dev/null; then
      out=$(printf '%s' "$out" | tr '[:upper:]' '[:lower:]')
   fi
   printf '%s\n' "$out"
}
libpath() {
   # Empty when haxelib cannot resolve the library
   local out
   out=$(haxelib libpath "$1" 2>/dev/null) || return 0
   normpath "$out"
}

# A package with this checkout's haxelib.json and run.n at the given version
package() {
   local name="$1" version="$2" dir="$WORK/$1-$2"
   mkdir -p "$dir"
   if [ "$name" = libhxcpp ]; then
      sed "s/\"version\": \"[^\"]*\"/\"version\": \"$version\"/" "$ROOT/haxelib.json" > "$dir/haxelib.json"
      cp "$ROOT/run.n" "$dir/"
      sed "s/,\"[^\"]*\")/,\"$version\")/" "$ROOT/extraParams.hxml" > "$dir/extraParams.hxml"
   else
      printf '{ "name": "%s", "version": "%s", "license": "BSD", "url": "https://example.org", "description": "stand-in", "releasenote": "", "contributors": ["nobody"], "dependencies": {} }\n' "$name" "$version" > "$dir/haxelib.json"
   fi
   "$PYTHON" - "$dir" "$dir.zip" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w") as z:
    for f in os.listdir(src):
        z.write(os.path.join(src, f), f)
PY
   echo "$dir.zip"
}

# Upstream hxcpp, which libhxcpp must never touch
haxelib install "$(package hxcpp 4.3.2)" > /dev/null
upstream=$(libpath hxcpp)

haxelib install "$(package libhxcpp 6.0.0)" > /dev/null
first=$(libpath libhxcpp)

out=$(haxelib run libhxcpp setup 2>&1)
check "setup points hxcpp at libhxcpp" "$(libpath hxcpp)" "$first"
contains "setup says how to go back" "$out" "haxelib run libhxcpp off"
printf 'class Probe {\n\tstatic function run() {\n\t\tvar d = haxe.macro.Context.getDefines();\n\t\tSys.println("hxcpp=" + d.get("hxcpp") + " libhxcpp=" + d.get("libhxcpp"));\n\t}\n}\n' > "$WORK/Probe.hx"
for lib in hxcpp libhxcpp; do
   out=$(haxe -cp "$WORK" -lib $lib --macro "Probe.run()" --no-output 2>&1)
   contains "-lib $lib gets both defines" "$out" "hxcpp=6.0.0 libhxcpp=6.0.0"
   # hxp, which Lime's tools use, takes the line before "-D <name>" as the path
   before=$(haxelib path $lib | tr -d '\r' | awk -v d="-D $lib" 'index($0, d) == 1 { print prev } { prev = $0 }' | sort -u)
   check "-lib $lib: the line before -D $lib is its path" "$(normpath "$before")" "$first"
   check "-lib $lib: only one line comes before -D $lib" "$(printf '%s\n' "$before" | wc -l | tr -d ' ')" "1"
done

haxelib install "$(package libhxcpp 6.0.1)" > /dev/null
second=$(libpath libhxcpp)
check "an update leaves the redirect on the old version" "$(libpath hxcpp)" "$first"

# What the compiler runs to build: the old version must notice and hand over
out=$(haxelib run hxcpp 2>&1)
check "the next run moves the redirect to the update" "$(libpath hxcpp)" "$second"
contains "the next run says so" "$out" "now points at libhxcpp 6.0.1"

out=$(haxelib run hxcpp 2>&1)
if printf '%s' "$out" | grep -qF "now points at"; then
   echo "FAIL an up-to-date redirect is left alone"
   failures=$((failures + 1))
else
   echo "ok   an up-to-date redirect is left alone"
fi

out=$(haxelib run libhxcpp off 2>&1)
check "off hands hxcpp back" "$(libpath hxcpp)" "$upstream"
contains "off says which hxcpp is used" "$out" "uses hxcpp 4.3.2 again"

# Without hxcpp at all
haxelib remove hxcpp > /dev/null 2>&1
check "hxcpp is gone" "$(libpath hxcpp)" ""
haxelib run libhxcpp setup > /dev/null 2>&1
out=$(haxelib run libhxcpp off 2>&1)
contains "off without hxcpp says how to install it" "$out" "haxelib install hxcpp"

echo
if [ "$failures" -eq 0 ]; then
   echo "all checks passed"
else
   echo "$failures checks failed"
fi
[ "$failures" -eq 0 ]
