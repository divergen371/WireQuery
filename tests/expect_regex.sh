#!/bin/sh
# Usage: expect_regex.sh <binary> [args ...] <pattern> <min_count>
# Runs the binary and asserts that exit code == 0 and stdout matches pattern at least min_count times.
set -eu
if [ "$#" -lt 3 ]; then
  echo "usage: $0 <binary> [args ...] <pattern> <min_count>" >&2
  exit 2
fi
# Extract last two args as pattern and min_count
MIN_COUNT=${@: -1}
PATTERN=${@: -2:1}
# Remaining as command
ARGC=$#
CMD_ARGS=$(printf '%s\n' "$@" | sed -n '1,'$((ARGC-2))'p')
# shellcheck disable=SC2086
OUT=$(eval "$CMD_ARGS" 2>&1)
RC=$?
if [ $RC -ne 0 ]; then
  echo "command failed with exit code $RC" >&2
  echo "$OUT"
  exit 1
fi
COUNT=$(printf '%s' "$OUT" | grep -E -c "$PATTERN" || true)
if [ "$COUNT" -lt "$MIN_COUNT" ]; then
  echo "expected at least $MIN_COUNT match(es) of /$PATTERN/, got $COUNT" >&2
  echo "$OUT"
  exit 1
fi
exit 0
