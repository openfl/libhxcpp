#!/bin/bash
# run-suite.sh LOG COMMAND...
#
# Runs a CrossByte utest suite, its output kept in LOG as well, and passes
# it with a warning when every test that failed is named in KNOWN_FAILURES, a
# list of method names separated by spaces. Those are failures of CrossByte's
# own tests, each with its reason where the workflow lists it. A crash, a
# timeout, or a run that printed no report fails as before, and so does any
# failure not on the list.
log=$1
shift
"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
if [ "$status" -eq 0 ]; then
  exit 0
fi
if [ "$status" -ne 1 ] || ! grep -q '^results: SOME TESTS FAILURES' "$log"; then
  exit "$status"
fi

# utest's report: a line for each case, then "  method: FAILURE ..." for each
# of its methods that did not pass
failed=$(sed -nE 's/^  ([A-Za-z0-9_]+): (FAILURE|ERROR|WARNING).*/\1/p' "$log" | sort -u)
if [ -z "$failed" ]; then
  exit "$status"
fi
unknown=""
for name in $failed; do
  case " $KNOWN_FAILURES " in
    *" $name "*) echo "::warning::$name failed, as it is known to: see KNOWN_FAILURES" ;;
    *) unknown="$unknown $name" ;;
  esac
done
if [ -n "$unknown" ]; then
  echo "::error::Failed:$unknown"
  exit 1
fi
exit 0
