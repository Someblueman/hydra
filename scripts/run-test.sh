#!/bin/sh
# Keep concurrent case output together; retain every case log for diagnosis.
set -eu
directory=$1
name=$2
shift 2
# Make executes recursive recipes under -n; forward that dry run without a PASS or log.
test_make_flags=${MAKEFLAGS:-}
case ${test_make_flags%% *} in
    -*|*=*|*[!a-zA-Z]*) ;;
    *n*) exec "$@" ;;
esac
# Every case sees git and tmux as CI does, whatever the developer has
# configured (see scripts/test-env.sh).
scripts=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=test-env.sh
# shellcheck disable=SC1091
. "$scripts/test-env.sh"
test_env_directory=$(mktemp -d "${TMPDIR:-/tmp}/hydra-test-env.XXXXXX")
trap 'rm -rf "$test_env_directory"' EXIT
test_env_apply "$scripts" "$test_env_directory"
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
    # CI keeps only the job output, so the reason must appear here.
    lines=$(wc -l < "$log" | tr -d ' ')
    if [ ! -s "$log" ]; then
        printf '%s: the case log is empty (it exited without printing anything)\n' "$name" >&2
    elif [ "$lines" -gt 80 ]; then
        printf '%s: last 80 of %s log lines:\n' "$name" "$lines" >&2
        tail -n 80 "$log" >&2
    else
        cat "$log" >&2
    fi
fi
exit "$code"
