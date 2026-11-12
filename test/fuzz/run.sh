#!/usr/bin/env bash
# Fuzzes the runtime's parsers and decoders (libhxcpp #257).
#
#   test/fuzz/run.sh [seconds per target] [target...]
#   test/fuzz/run.sh --replay [target...]
#
# The first form builds Fuzz.hx with libFuzzer, ASan and UBSan and runs each
# target, all of them by default, for the given time (60 seconds by default),
# starting from corpus/<target> and regressions/<target>. Inputs that reach
# new code go to out/corpus/<target>. A crash, a sanitizer report or a broken
# invariant stops that target and fails the run, and libFuzzer keeps the
# input as out/crash-<target>-<hash>; once fixed, it belongs in
# regressions/<target>. On Linux and macOS this needs clang with
# compiler-rt's libFuzzer; on Windows, Git Bash and Visual Studio 2022 or
# later, whose cl.exe has libFuzzer and ASan but no UBSan.
#
# --replay builds Fuzz.hx without libFuzzer, with the default compiler, and
# runs every input in corpus/ and regressions/ once. HXCPP_SANITIZE in the
# environment adds sanitizers to that build.

set -u
cd "$(dirname "$0")"

replay=0
if [ "${1:-}" = --replay ]; then
   replay=1
   shift
fi
seconds=60
if [ $replay = 0 ] && [[ "${1:-}" =~ ^[0-9]+$ ]]; then
   seconds=$1
   shift
fi

windows=0
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) windows=1 ;;
esac
exe() {
   if [ $windows = 1 ]; then echo "$1/Fuzz.exe"; else echo "$1/Fuzz"; fi
}

export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
# Sanitizer options are split at colons, so a Windows path cannot go in them;
# cl.exe has no UBSan to suppress anyway
if [ $windows = 0 ]; then
   export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1:suppressions=$PWD/../sanitizers/ubsan.supp}
fi

if [ $replay = 1 ]; then
   defines=()
   if [ -n "${HXCPP_SANITIZE:-}" ]; then
      defines=(-D "HXCPP_SANITIZE=$HXCPP_SANITIZE")
   fi
   haxe build.hxml "${defines[@]}" || exit 1
   fuzz=$(exe bin)
else
   if [ $windows = 1 ]; then
      haxe libfuzzer.hxml -D HXCPP_SANITIZE=address,fuzzer-no-link || exit 1
      # The ASan runtime DLL sits beside cl.exe, which Git Bash does not have on its path
      vs=$("/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath | tr -d '\r')
      dll=$(ls "$(cygpath "$vs")"/VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll | tail -1)
      export PATH="$(dirname "$dll"):$PATH"
   else
      export CXX=${CXX:-clang++}
      # libFuzzer without its main, which Fuzz.main replaces. The runtime
      # directory can hold one archive per architecture, named for it, and
      # the i386 one sorts first, so take the one for this machine
      lib=$(bash ./find-libfuzzer.sh) || exit 1
      haxe libfuzzer.hxml -D HXCPP_SANITIZE=fuzzer-no-link,address,undefined -D "FUZZER_NO_MAIN=$lib" || exit 1
   fi
   fuzz=$(exe out)
fi

if [ $# -gt 0 ]; then
   targets=("$@")
else
   targets=($("$fuzz" list | tr -d '\r'))
   if [ ${#targets[@]} = 0 ]; then
      echo "$fuzz list gave no targets" >&2
      exit 1
   fi
fi

failed=()
for target in "${targets[@]}"; do
   dirs=()
   for dir in corpus/$target regressions/$target; do
      [ -d "$dir" ] && dirs+=("$dir")
   done
   echo "=== $target"
   if [ $replay = 1 ]; then
      "$fuzz" "$target" "${dirs[@]}" || failed+=("$target")
   else
      mkdir -p "out/corpus/$target"
      "$fuzz" "$target" "out/corpus/$target" "${dirs[@]}" -max_total_time="$seconds" -max_len=4096 \
         -timeout=20 -rss_limit_mb=4096 -detect_leaks=0 -print_final_stats=1 \
         -artifact_prefix="out/crash-$target-" || failed+=("$target")
   fi
done

if [ ${#failed[@]} -gt 0 ]; then
   echo "Failed: ${failed[*]}"
   exit 1
fi
echo "All ${#targets[@]} targets passed"
