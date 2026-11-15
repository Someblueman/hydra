#!/bin/sh
# Admission keeps finished records like event archives: none unchanged for 30
# days and at most the 256 most recently changed, and never a queued, reserved
# or unknown one, the record a command names, or the highest sequence number.
set -eu
ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
trap 'rm -rf "$TEST_DIR"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HYDRA_HOME="$TEST_DIR/home with spaces"
CLI="$ROOT/bin/hydra"
admit() { "$CLI" admission "$@"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
records="$HYDRA_HOME/admission"
now="$(date +%s)"
today="$(date +%Y%m%d)"

admit configure 0 0 0 128 - >/dev/null
# record <id> <state> <sequence> <touch-time> [deadline]
record() {
    printf 'project_a %s %s %s %s - %s\n' "$2" "$((now - 60))" "${5:-$((now - 30))}" "$3" fixture > "$records/$1.request"
    touch -t "$4" "$records/$1.request"
}
minute() { printf '%s%02d%02d' "$today" "$(($1 / 60))" "$(($1 % 60))"; }
sequence=1
i=1
while [ "$i" -le 300 ]; do
    record "released_$i" released "$sequence" "$(minute "$((i - 1))")"
    i=$((i + 1)) sequence=$((sequence + 1))
done
i=1
while [ "$i" -le 50 ]; do
    record "refused_$i" refused "$sequence" "$(minute "$((299 + i))")"
    i=$((i + 1)) sequence=$((sequence + 1))
done
i=1
while [ "$i" -le 100 ]; do
    record "old_cancelled_$i" cancelled "$sequence" 202001010000
    i=$((i + 1)) sequence=$((sequence + 1))
done
# In use, however old: reservations, unknown outcomes and a live queue entry.
i=1
while [ "$i" -le 20 ]; do
    record "reserved_$i" reserved "$sequence" 202001010000
    i=$((i + 1)) sequence=$((sequence + 1))
done
i=1
while [ "$i" -le 10 ]; do
    record "unknown_$i" unknown "$sequence" 202001010000
    i=$((i + 1)) sequence=$((sequence + 1))
done
record queued_1 queued "$sequence" 202001010000 "$((now + 3600))"
# The highest sequence number is an old finished record.
record old_top cancelled 9999 202001010000
count() { find "$records" ! -name admission -prune -type f -name "$1" | wc -l | tr -d ' '; }
[ "$(count '*.request')" -eq 482 ] || fail 'the fixture holds 482 records'

started="$(date +%s)"
admit status --summary >/dev/null || fail 'status reads the records'
printf 'Scanned and pruned 482 records in %ss\n' "$(($(date +%s) - started))"

[ "$(count 'reserved_*')" -eq 20 ] || fail 'reserved records are never removed'
[ "$(count 'unknown_*')" -eq 10 ] || fail 'unknown outcomes are never removed'
[ "$(count 'queued_*')" -eq 1 ] || fail 'a queued record is never removed'
[ -f "$records/old_top.request" ] || fail 'the record with the highest sequence number is kept'
[ "$(count 'old_cancelled_*')" -eq 0 ] || fail 'finished records unchanged for 30 days are removed'
[ "$(count 'refused_*')" -eq 50 ] || fail 'the most recently changed finished records are kept'
[ "$(count 'released_*')" -eq 206 ] || fail 'at most 256 finished records are kept'
[ ! -e "$records/released_94.request" ] && [ -f "$records/released_95.request" ] ||
    fail 'the least recently changed finished records go first'
[ ! -e "$records/lock" ] || fail 'the admission lock is released'

# Sequence numbers continue past the kept highest one; the new request queues
# behind queued_1.
admit request next project_a 60 - | grep -q '"sequence":10000' || fail 'a new request takes the next sequence number'
admit cancel next >/dev/null
# A command that names a finished record past retention still finds it.
touch -t 202001010000 "$records/refused_1.request"
admit inspect refused_1 | grep -q '"state":"refused"' || fail 'the named record survives its own scan'
[ ! -e "$records/refused_1.request" ] || admit status >/dev/null
[ ! -e "$records/refused_1.request" ] || fail 'the next scan removes it'
admit inspect reserved_1 | grep -q '"state":"reserved"' || fail 'reservations stay inspectable'
printf 'Passed admission retention: finished records pruned by age and count, records in use kept\n'
