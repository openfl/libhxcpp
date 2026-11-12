#!/bin/bash
# haxelib-install.sh LIBRARY [VERSION]
#
# haxelib install, tried three times. lib.haxe.org fails a request now and
# then, and a nightly run should not fail on one bad download, nor fail with
# nothing to show for it: the output is kept out of the log while the install
# works, and shown for every try that does not.
log=$(mktemp)
for attempt in 1 2 3; do
  if haxelib install "$@" --always > "$log" 2>&1; then
    rm -f "$log"
    exit 0
  fi
  echo "::warning::haxelib install $* failed, try $attempt of 3:"
  tail -n 40 "$log"
  if [ $attempt -lt 3 ]; then
    sleep $((attempt * 30))
  fi
done
rm -f "$log"
echo "::error::haxelib install $* failed three times"
exit 1
