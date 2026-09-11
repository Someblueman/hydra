#!/bin/sh
# Keep concurrent case output together; retain every case log for diagnosis.
set -eu
directory=$1
name=$2
shift 2
mkdir -p "$directory"
log="$directory/$name.log"
started=$(date +%s)
code=0
"$@" > "$log" 2>&1 || code=$?
elapsed=$(($(date +%s) - started))
if [ "$code" -eq 0 ]; then
    printf 'PASS %s (%ss)\n' "$name" "$elapsed"
else
    printf 'FAIL %s (%ss, exit %s): %s\n' "$name" "$elapsed" "$code" "$log" >&2
    cat "$log" >&2
fi
exit "$code"
