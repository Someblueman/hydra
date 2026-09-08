#!/bin/sh
# Exercise the public shell authority with competing processes and private state.
set -eu
ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HYDRA_HOME="$TEST_DIR/home with spaces"
CLI="$ROOT/bin/hydra"
admit() { "$CLI" admission "$@"; }
has() { printf '%s\n' "$1" | grep -q "$2" || { printf 'Missing %s in %s\n' "$2" "$1" >&2; exit 1; }; }

admit configure 2 1 0 8 linux,gpu >/dev/null
has "$(admit request one project_a 60 linux)" '"state":"reserved"'
has "$(admit request two project_a 60 linux)" '"reason":"project_limit"'
has "$(admit request three project_b 60 gpu)" '"reason":"fifo"'
if admit request one project_b 60 linux > "$TEST_DIR/error"; then exit 1; fi
has "$(cat "$TEST_DIR/error")" submission_conflict
admit release one --confirmed >/dev/null
has "$(admit claim two)" '"state":"reserved"'
has "$(admit claim three)" '"state":"reserved"'
has "$(admit request incompatible project_c 60 missing)" capability_unavailable
admit unknown two >/dev/null
has "$(admit request four project_c 60 -)" host_limit
if admit cancel two >/dev/null; then exit 1; fi
if admit release two >/dev/null; then exit 1; fi
has "$(admit status --json)" '"reserved":2'
admit cancel four >/dev/null
has "$(admit claim four)" '"state":"cancelled"'
admit release two --confirmed >/dev/null
admit release three --confirmed >/dev/null

# The receiver observes disk itself, and expires waiting requests on inspection.
admit configure 1 0 999999999999 1 - >/dev/null
has "$(admit request disk project_a 60 -)" disk_floor
if admit request overflow project_b 60 - > "$TEST_DIR/error"; then exit 1; fi
has "$(cat "$TEST_DIR/error")" queue_full
admit cancel disk >/dev/null
has "$(admit inspect disk)" '"state":"cancelled"'
has "$(admit status --summary)" '"requests":\[\],"requests_omitted":true'
has "$(admit request expiry project_a 1 -)" disk_floor
sleep 1
has "$(admit status)" '"state":"expired"'
admit configure 1 0 0 30 - >/dev/null

# A stale client observation is never used to grant capacity. Race 12 callers
# from distinct projects; every accepted request has a unique FIFO sequence.
i=0
while [ "$i" -lt 12 ]; do
    admit request "race_$i" "project_$i" 60 - > "$TEST_DIR/race_$i" &
    i=$((i + 1))
done
wait
reserved=0 queued=0
i=0
while [ "$i" -lt 12 ]; do
    if grep -q '"state":"reserved"' "$TEST_DIR/race_$i"; then reserved=$((reserved + 1));
    elif grep -q '"state":"queued"' "$TEST_DIR/race_$i"; then queued=$((queued + 1));
    else cat "$TEST_DIR/race_$i"; exit 1; fi
    i=$((i + 1))
done
[ "$reserved" -eq 1 ] && [ "$queued" -eq 11 ]
[ "$(sed -n 's/.*"sequence":\([0-9]*\).*/\1/p' "$TEST_DIR"/race_* | sort -u | wc -l | tr -d ' ')" -eq 12 ]
has "$(admit status)" '"reserved":1,"queued":11'

# Broken state must fail closed, including links and partial records.
printf 'partial\n' > "$HYDRA_HOME/admission/broken.request"
if admit request blocked project_x 60 - > "$TEST_DIR/error"; then exit 1; fi
has "$(cat "$TEST_DIR/error")" recovery_required
rm "$HYDRA_HOME/admission/broken.request"
ln -s "$TEST_DIR/absent" "$HYDRA_HOME/admission/broken.request"
if admit request blocked project_x 60 - >/dev/null; then exit 1; fi
rm "$HYDRA_HOME/admission/broken.request"
mkdir "$HYDRA_HOME/admission/lock"
if admit claim race_1 >/dev/null; then exit 1; fi
rmdir "$HYDRA_HOME/admission/lock"
has "$(admit status)" '"reserved":1'
chmod go+w "$HYDRA_HOME"
if admit status >/dev/null; then exit 1; fi
chmod 700 "$HYDRA_HOME"
ln -s "$HYDRA_HOME" "$TEST_DIR/home-alias"
has "$(HYDRA_HOME="$TEST_DIR/home-alias" "$CLI" admission status)" '"reserved":1'
printf 'Passed admission CLI, FIFO, deadlines, backpressure, ownership, and concurrent-submitter checks\n'
