#!/bin/sh
# The admission lock names the process that holds it. A lock whose holder
# died on this host is taken over, by one waiter at a time; a lock held by a
# live process, by another host, or without an owner record is never broken.
set -eu
ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$ROOT/tests/helpers.sh"
TEST_DIR="$(test_mktemp_dir)"
trap 'test_stop_processes_naming "$TEST_DIR"; rm -rf "$TEST_DIR"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HYDRA_HOME="$TEST_DIR/home"
CLI="$ROOT/bin/hydra"
admit() { "$CLI" admission "$@"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
this_host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"

admit configure 1 0 0 30 - >/dev/null
lock="$HYDRA_HOME/admission/lock"
[ ! -e "$lock" ] || fail 'a finished admission command releases the lock'

# Usage: plant_lock <pid|''> <host|''>: a lock as its holder leaves it.
plant_lock() {
    mkdir "$lock"
    : > "$lock/.claim"
    [ -z "$1" ] || printf '%s\n' "$1" > "$lock/pid"
    [ -z "$2" ] || printf '%s\n' "$2" > "$lock/host"
}
beside_lock() {
    for _bl_entry in "$lock".*; do
        [ ! -e "$_bl_entry" ] || printf '%s ' "${_bl_entry##*/}"
    done
}

# --- a holder that died on this host -------------------------------------
# No process can have PID 99999999: it is above every system's limit, so this
# holder is dead whatever else runs on the machine.
# A caller that could only wait for this lock would give up after about 2 s.
plant_lock 99999999 "$this_host"
admit status > "$TEST_DIR/after-dead" 2>&1 || { cat "$TEST_DIR/after-dead" >&2; fail 'an admission command takes over the lock of a holder that died on this host'; }
grep -q '"ok":true' "$TEST_DIR/after-dead" || fail 'the command that took the lock over answers'
[ ! -e "$lock" ] || fail 'the lock taken over is released'
[ -z "$(beside_lock)" ] || fail "taking the lock over leaves nothing beside it: $(beside_lock)"

# --- locks that are never broken -----------------------------------------
# This test's own shell is a live process on this host.
plant_lock "$$" "$this_host"
if admit status > "$TEST_DIR/live" 2>&1; then fail 'the lock of a live process is not broken'; fi
grep -q '"code":"state_unavailable"' "$TEST_DIR/live" || fail 'a held lock is reported as state_unavailable'
[ "$(sed -n '1p' "$lock/pid")" = "$$" ] || fail "the live holder's record is untouched"
rm -rf "$lock"

plant_lock 99999999 another-host.invalid
if admit status > "$TEST_DIR/foreign" 2>&1; then fail 'a lock held on another host is not broken'; fi
[ "$(sed -n '1p' "$lock/host")" = another-host.invalid ] || fail "the other host's record is untouched"
rm -rf "$lock"

plant_lock 99999999 ''
if admit status > "$TEST_DIR/no-host" 2>&1; then fail 'a lock without a host record is not broken'; fi
[ -d "$lock" ] || fail 'the lock without a host record is untouched'
rm -rf "$lock"

plant_lock '' ''
if admit status > "$TEST_DIR/no-owner" 2>&1; then fail 'a lock without an owner record is not broken'; fi
[ -d "$lock" ] || fail 'the lock without an owner record is untouched'
rm -rf "$lock"

# --- the record a real holder writes -------------------------------------
# df runs inside admission_open once the lock is held; this one waits while
# hold exists, which keeps a real admission command inside the locked section.
mkdir "$TEST_DIR/shim"
real_df="$(command -v df)"
cat > "$TEST_DIR/shim/df" <<EOF
#!/bin/sh
if [ -f "$TEST_DIR/hold" ]; then
    : > "$TEST_DIR/holding"
    while [ -f "$TEST_DIR/hold" ]; do sleep 0.05; done
fi
exec "$real_df" "\$@"
EOF
chmod +x "$TEST_DIR/shim/df"
: > "$TEST_DIR/hold"
PATH="$TEST_DIR/shim:$PATH" "$CLI" admission status > "$TEST_DIR/held" 2>&1 &
command_pid=$!
wait_for 'the admission command to hold the lock' test -f "$TEST_DIR/holding"
holder_pid="$(sed -n '1p' "$lock/pid" 2>/dev/null || true)"
case "$holder_pid" in ''|*[!0-9]*) fail "the held lock records its holder's PID (got '$holder_pid')" ;; esac
[ "$(sed -n '1p' "$lock/host" 2>/dev/null || true)" = "$this_host" ] || fail 'the held lock records this host'
kill -0 "$holder_pid" 2>/dev/null || fail 'the recorded holder is a live process'
# The holder is the subshell that runs the admission command, a child of the
# hydra process: $$ would name the hydra process itself.
[ "$holder_pid" != "$command_pid" ] || fail 'the lock names the subshell that holds it, not the command that started it'
[ "$(ps -o ppid= -p "$holder_pid" | tr -d ' ')" = "$command_pid" ] || fail 'the recorded holder is the subshell of the admission command'

# Killing the command leaves its subshell holding the lock, and alive: the
# lock is still held and must not be broken.
kill -9 "$command_pid"
wait "$command_pid" 2>/dev/null || true
kill -0 "$holder_pid" 2>/dev/null || fail 'the subshell outlives the command that started it'
if admit status > "$TEST_DIR/orphan" 2>&1; then fail "the lock of a live subshell is not broken when its command dies"; fi
[ "$(sed -n '1p' "$lock/pid" 2>/dev/null || true)" = "$holder_pid" ] || fail "the live subshell's record is untouched"
# Once it may continue, the subshell finishes and releases the lock itself.
rm -f "$TEST_DIR/hold"
lock_released() { [ ! -e "$lock" ]; }
wait_for 'the holder to release the lock' lock_released
admit status > "$TEST_DIR/after-release" 2>&1 || fail 'admission works after the holder released the lock'

# --- several waiters, one dead holder --------------------------------------
# Each caller must be serialized behind the one that broke the lock: every
# accepted request gets its own sequence number and one slot is granted. A
# caller that exhausts the short lock wait under load resubmits.
race_request() {
    _race_try=0
    while :; do
        admit request "race_$1" "project_$1" 60 - > "$TEST_DIR/race_$1" 2>&1 || :
        grep -q '"code":"state_unavailable"' "$TEST_DIR/race_$1" || return 0
        _race_try=$((_race_try + 1))
        [ "$_race_try" -lt 20 ] || return 0
        sleep 0.1
    done
}
plant_lock 99999999 "$this_host"
i=0
while [ "$i" -lt 8 ]; do
    race_request "$i" &
    i=$((i + 1))
done
wait
reserved=0 queued=0
i=0
while [ "$i" -lt 8 ]; do
    if grep -q '"state":"reserved"' "$TEST_DIR/race_$i"; then reserved=$((reserved + 1));
    elif grep -q '"state":"queued"' "$TEST_DIR/race_$i"; then queued=$((queued + 1));
    else cat "$TEST_DIR/race_$i" >&2; fail "caller $i was neither granted nor queued"; fi
    i=$((i + 1))
done
if [ "$reserved" -ne 1 ] || [ "$queued" -ne 7 ]; then
    fail "one slot is granted behind a dead holder (reserved=$reserved queued=$queued)"
fi
[ "$(sed -n 's/.*"sequence":\([0-9]*\).*/\1/p' "$TEST_DIR"/race_* | sort -u | wc -l | tr -d ' ')" -eq 8 ] ||
    fail 'every request behind a dead holder has its own sequence number'
[ ! -e "$lock" ] || fail 'the contended lock is released'
[ -z "$(beside_lock)" ] || fail "contended recovery leaves nothing beside the lock: $(beside_lock)"
printf 'Passed admission lock owner record, dead-holder takeover, live, foreign and unrecorded locks, and contended recovery\n'
