#!/usr/bin/env bash
# libhxcpp #471: errors on the compile threads.
# - A failed compile whose old object could not be deleted, as when it is
#   read-only, linked that object and exited 0.
# - Exiting from a compile thread, as HXCPP_EXIT_ON_ERROR does on a compile
#   error, or an error the build tool reports there, hung the build.
# Each build here must fail, and within the time limit.
cd "$(dirname "$0")"
failed=0

# Runs a build in the background and fails it after $limit seconds
limit=600
build() {
  "$@" > build.log 2>&1 &
  local pid=$! waited=0
  while kill -0 $pid 2>/dev/null; do
    if [ $waited -ge $limit ]; then
      kill $pid 2>/dev/null
      echo "hung"
      return 0
    fi
    sleep 1
    waited=$((waited+1))
  done
  wait $pid
  echo "exit $?"
}

expect_failure() {
  local name=$1; shift
  local result
  result=$(build "$@")
  if [ "$result" = "exit 0" ] || [ "$result" = "hung" ]; then
    echo "::error::$name: $result, where it should fail"
    tail -20 build.log
    failed=1
  else
    echo "$name: failed as it should ($result)"
  fi
}

(cd error && HXCPP_EXIT_ON_ERROR=1 expect_failure "HXCPP_EXIT_ON_ERROR in the environment" haxe build.hxml -D "$HXCPP_ARCH_FLAG") || failed=1
(cd error && expect_failure "-D HXCPP_EXIT_ON_ERROR" haxe build.hxml -D HXCPP_EXIT_ON_ERROR -D "$HXCPP_ARCH_FLAG") || failed=1
(cd tags && expect_failure "two optimisation tags" haxe build.hxml -D "$HXCPP_ARCH_FLAG") || failed=1

# Windows only: elsewhere a read-only file can still be deleted
if [ "$OS" = "Windows_NT" ]; then
  (
    cd error
    rm -rf ro && mkdir ro && cp build.hxml ro/
    cd ro
    printf 'class Main {\n\tstatic function main() {\n\t\tSys.println("old code");\n\t}\n}\n' > Main.hx
    haxe build.hxml -D "$HXCPP_ARCH_FLAG" > first.log 2>&1 || { echo "::error::first build failed"; cat first.log; exit 1; }
    obj=$(ls bin/obj/*/*_Main.obj | head -1)
    attrib +R "$(cygpath -w "$obj")"
    # Newer than the object by more than the build tool's one second
    sleep 2
    cp ../Main.hx Main.hx
    expect_failure "a compile error over a read-only object" haxe build.hxml -D "$HXCPP_ARCH_FLAG"
    # The old object must not be linked: the link itself may then fail or not
    if grep -q 'Link:' build.log; then
      echo "::error::a compile error over a read-only object: linked the old object"
      failed=1
    fi
    status=$failed
    attrib -R "$(cygpath -w "$obj")"
    exit $status
  ) || failed=1
fi

exit $failed
