#!/usr/bin/env bash
# compiler-rt packages can use the older lib/linux directory even when the
# compiler reports a target-triple runtime directory (Ubuntu 26.04, Clang 21).
set -eu
cxx=${CXX:-clang++}
runtime=$("$cxx" --print-runtime-dir)
resource=$("$cxx" -print-resource-dir)
arch=$(uname -m)
for dir in "$runtime" "$resource/lib/linux"; do
   for name in libclang_rt.fuzzer_no_main.a "libclang_rt.fuzzer_no_main-$arch.a"; do
      if [ -f "$dir/$name" ]; then
         printf '%s\n' "$dir/$name"
         exit 0
      fi
   done
done
echo "No native libclang_rt.fuzzer_no_main in $runtime or $resource/lib/linux; install compiler-rt for $cxx" >&2
exit 1
