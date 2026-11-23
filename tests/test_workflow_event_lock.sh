#!/bin/sh
# The workflow event lock names the process that writes the event, so
# stale-lock recovery neither breaks a live writer's lock because the
# writer's parent exited, nor keeps a dead writer's lock because its parent
# lives. Writers here are subshells of a parent shell, as the run driver's
# step workers are. A stale lock is broken by one writer at a time, and a lock
# whose writer died before it recorded itself is recovered too.

test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
trap 'test_stop_processes_naming "$base"; rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

echo "Testing the workflow event lock owner..."

# writer.sh MODE RUN BASE: a parent shell whose subshell publishes an event
# whose detail "slow" holds the lock while BASE/hold exists.
cat > "$base/writer.sh" <<'EOF'
#!/bin/sh
mode="$1" run="$2" base="$3"
. "$HYDRA_TEST_ROOT/lib/output.sh"
. "$HYDRA_TEST_ROOT/lib/locks.sh"
. "$HYDRA_TEST_ROOT/lib/workflow_runtime.sh"
# The detail is escaped while the lock is held.
json_escape() {
    if [ "$1" = slow ]; then
        : > "$base/holding"
        while [ -f "$base/hold" ]; do sleep 0.05; done
    fi
    printf '%s' "$1"
}
holding() {
    tries=0
    while [ ! -f "$base/holding" ] && [ "$tries" -lt 200 ]; do sleep 0.05; tries=$((tries + 1)); done
    [ -f "$base/holding" ]
}
case "$mode" in
    write) workflow_event "$run" "" "$4" "${5:-}" ;;
    parent-exits)
        # The subshell keeps the lock after this parent has exited.
        ( workflow_event "$run" "" child.event slow ) &
        printf '%s\n' "$!" > "$base/child"
        holding
        ;;
    child-dies)
        ( workflow_event "$run" "" child.event slow ) &
        child=$!
        printf '%s\n' "$child" > "$base/child"
        holding || exit 1
        sed -n '1p' "$run/.events.lock/owner-pid" > "$base/held-by"
        kill -9 "$child"
        wait "$child" 2>/dev/null
        rm -f "$base/hold"
        # This parent is alive, and publishes after its writer died.
        started="$(date +%s)"
        workflow_event "$run" "" parent.event
        status=$?
        printf '%s %s\n' "$status" "$(($(date +%s) - started))" > "$base/parent-result"
        ;;
    late-breaker)
        # This writer finds the owner $4 dead. Before it acts on that, another
        # writer breaks the stale lock and takes it.
        stale_owner="$4"
        workflow_pid_alive() {
            case "$1" in ''|*[!0-9]*) return 1 ;; esac
            if [ "$1" = "$stale_owner" ] && [ ! -f "$base/judged" ]; then
                : > "$base/judged"
                holding
                return 1
            fi
            kill -0 "$1" 2>/dev/null
        }
        workflow_event "$run" "" late.event
        ;;
esac
EOF
HYDRA_TEST_ROOT="$root"
export HYDRA_TEST_ROOT

new_run() {
    rm -rf "$base/run" "$base/holding" "$base/child" "$base/held-by" "$base/parent-result"
    mkdir "$base/run" && printf 'run_lock_test\n' > "$base/run/run-id"
    : > "$base/hold"
}
types() { sed -n 's/.*"sequence":\([0-9]*\),.*"type":"\([^"]*\)".*/\1 \2/p' "${1:-$base/run}/events.jsonl" | tr '\n' ' '; }

# The parent exits while its subshell holds the lock.
new_run
sh "$base/writer.sh" parent-exits "$base/run" "$base" &
parent=$!
wait "$parent"
child="$(sed -n '1p' "$base/child")"
assert_equal "$child" "$(sed -n '1p' "$base/run/.events.lock/owner-pid")" 'the lock names the subshell that writes, not its parent'
kill -0 "$parent" 2>/dev/null
assert_failure $? 'the parent has exited'
sh "$base/writer.sh" write "$base/run" "$base" third.event &
third=$!
sleep 2
grep -q third.event "$base/run/events.jsonl"
assert_failure $? 'a second writer waits for the live writer instead of breaking its lock'
rm -f "$base/hold"
wait "$third"
assert_success $? 'the second writer publishes once the first released the lock'
assert_equal '1 child.event 2 third.event ' "$(types)" 'both events keep their own sequence numbers'
# shellcheck disable=SC2016 # The inner shell expands its own argument.
wait_for 'the orphaned writer to exit' sh -c '! kill -0 "$1" 2>/dev/null' sh "$child"
assert_success $? 'the orphaned writer exited'

# The subshell dies holding the lock while its parent lives on.
new_run
sh "$base/writer.sh" child-dies "$base/run" "$base"
assert_equal "$(sed -n '1p' "$base/child")" "$(sed -n '1p' "$base/held-by")" 'the dead writer had recorded its own PID'
read -r status seconds < "$base/parent-result"
assert_equal 0 "$status" 'the live parent publishes after its writer died holding the lock'
if [ "$seconds" -lt 10 ]; then check=0; else check=1; fi
assert_success "$check" "the dead writer's lock is recovered at once (${seconds}s), not waited out"
assert_equal '1 parent.event ' "$(types)" 'only the published event is recorded'
if [ ! -e "$base/run/.events.lock" ]; then check=0; else check=1; fi
assert_success "$check" 'the lock is released'

echo "Testing stale and abandoned workflow event locks..."

# Each case below has its own directory, with the run in it, so the cases
# that wait on a lock wait side by side.
new_case() {
    case_dir="$base/$1"
    mkdir -p "$case_dir/run" && printf 'run_lock_test\n' > "$case_dir/run/run-id"
    : > "$case_dir/hold"
}
# The PID of a process that cannot be running (see test_dead_pid).
dead_pid() { test_dead_pid; }
# A lock as a writer leaves it when it dies: the directory, its claim and
# the owner record $2 (none without it).
stale_lock() {
    mkdir "$1/run/.events.lock" && : > "$1/run/.events.lock/.claim"
    [ "$#" -lt 2 ] || printf '%s\n' "$2" > "$1/run/.events.lock/owner-pid"
}
# Publishes one event of type $2 in the background and records the writer's
# exit status and the seconds it took in <case>/result.
timed_write() {
    (
        started="$(date +%s)"
        sh "$base/writer.sh" write "$1/run" "$1" "$2"
        printf '%s %s\n' "$?" "$(($(date +%s) - started))" > "$1/result"
    ) &
}
# What the lock protocol left in a run directory.
lock_entries() {
    for lock_entry in "$1"/run/.events.lock*; do
        [ ! -e "$lock_entry" ] || printf '%s ' "${lock_entry##*/}"
    done
}
within() { [ "$1" -ge "$2" ] && [ "$1" -lt "$3" ]; }

# A writer died after creating the lock and before recording itself as its
# owner: the lock has no owner, and no process will ever release it.
new_case ownerless; ownerless="$case_dir"
stale_lock "$ownerless"
timed_write "$ownerless" after.event; ownerless_writer=$!
# The same with an empty owner record, and with nothing in the directory.
new_case empty-record; empty_record="$case_dir"
stale_lock "$empty_record" && : > "$empty_record/run/.events.lock/owner-pid"
timed_write "$empty_record" after.event; empty_record_writer=$!
new_case bare; bare="$case_dir"
mkdir "$bare/run/.events.lock"
timed_write "$bare" after.event; bare_writer=$!

# A live writer that records itself late keeps its lock.
new_case late-owner; late_owner="$case_dir"
stale_lock "$late_owner"
(
    sleep 2
    printf '%s\n' "$$" > "$late_owner/run/.events.lock/owner-pid"
    sleep 6
    printf '{"schema_version":1,"sequence":1,"type":"holder.event"}\n' >> "$late_owner/run/events.jsonl"
    rm -rf "$late_owner/run/.events.lock"
) &
late_holder=$!
timed_write "$late_owner" after.event; late_owner_writer=$!

# Another live process has announced that it is breaking the stale lock: it
# may be about to rename the lock away and take it, so a second writer leaves
# the lock alone. The announcement of a dead process does not count.
new_case announced; announced="$case_dir"
stale_lock "$announced" "$(dead_pid)"
sleep 60 &
breaker=$!
: > "$announced/run/.events.lock.breaking.$breaker"
timed_write "$announced" after.event; announced_writer=$!
(sleep 3; kill "$breaker") &
breaker_killer=$!

# Of two writers that both found the owner dead, the slower one acts after the
# faster one broke the lock and took it. It must not delete the faster one's
# lock: both would then append with the same sequence number.
new_case late-breaker; late_breaker="$case_dir"
late_dead="$(dead_pid)"
stale_lock "$late_breaker" "$late_dead"
sh "$base/writer.sh" late-breaker "$late_breaker/run" "$late_breaker" "$late_dead" &
late_writer=$!
wait_for 'the slower writer to find the owner dead' test -f "$late_breaker/judged"
sh "$base/writer.sh" write "$late_breaker/run" "$late_breaker" first.event slow &
first_writer=$!
wait_for 'the faster writer to take the lock' test -f "$late_breaker/holding"
first_owner="$(sed -n '1p' "$late_breaker/run/.events.lock/owner-pid")"
sleep 3
grep -q late.event "$late_breaker/run/events.jsonl"
assert_failure $? 'a writer does not break the lock another writer took after breaking the stale one'
assert_equal "$first_owner" "$(sed -n '1p' "$late_breaker/run/.events.lock/owner-pid" 2>/dev/null)" 'the faster writer still holds its lock'
rm -f "$late_breaker/hold"
wait "$first_writer"
assert_success $? 'the faster writer publishes'
wait "$late_writer"
assert_success $? 'the slower writer publishes after it'
assert_equal '1 first.event 2 late.event ' "$(types "$late_breaker/run")" 'each of the two events has its own sequence number'
assert_equal '' "$(lock_entries "$late_breaker")" 'two breakers leave nothing of the lock behind'

# Many writers find one stale lock at the same moment.
new_case contention; contention="$case_dir"
stale_lock "$contention" "$(dead_pid)"
contention_writers=''
for contention_writer in 1 2 3 4 5 6; do
    sh "$base/writer.sh" write "$contention/run" "$contention" "writer$contention_writer.event" &
    contention_writers="$contention_writers $!"
done
contention_status=0
for contention_writer in $contention_writers; do wait "$contention_writer" || contention_status=1; done
assert_success "$contention_status" 'six writers that found one stale lock all publish'
assert_equal '1 2 3 4 5 6 ' "$(sed -n 's/.*"sequence":\([0-9]*\),.*/\1/p' "$contention/run/events.jsonl" | tr '\n' ' ')" \
    'their events are numbered 1 to 6, each number once'
assert_equal '' "$(lock_entries "$contention")" 'six breakers leave nothing of the lock behind'

for abandoned_case in ownerless empty-record bare; do
    case "$abandoned_case" in
        ownerless) abandoned_dir="$ownerless" abandoned_writer="$ownerless_writer" abandoned_what='a lock without an owner record' ;;
        empty-record) abandoned_dir="$empty_record" abandoned_writer="$empty_record_writer" abandoned_what='a lock with an empty owner record' ;;
        *) abandoned_dir="$bare" abandoned_writer="$bare_writer" abandoned_what='a lock directory without a claim' ;;
    esac
    wait "$abandoned_writer"
    read -r status seconds < "$abandoned_dir/result"
    assert_equal 0 "$status" "$abandoned_what: the event is published, not dropped"
    within "$seconds" 4 25
    assert_success $? "$abandoned_what: its writer is given time to record itself, then the lock is broken (${seconds}s)"
    assert_equal '1 after.event ' "$(types "$abandoned_dir/run")" "$abandoned_what: the event is recorded once"
    assert_equal '' "$(lock_entries "$abandoned_dir")" "$abandoned_what: nothing of the lock is left behind"
done

wait "$late_owner_writer"
wait "$late_holder"
read -r status seconds < "$late_owner/result"
assert_equal 0 "$status" 'a writer waits for a lock whose live owner recorded itself late'
within "$seconds" 7 30
assert_success $? "that lock is released by its owner, not broken (${seconds}s)"
assert_equal '1 holder.event 2 after.event ' "$(types "$late_owner/run")" 'the late owner and the waiting writer keep their own sequence numbers'

wait "$announced_writer"
wait "$breaker_killer"
wait "$breaker" 2>/dev/null
read -r status seconds < "$announced/result"
assert_equal 0 "$status" 'a writer publishes once the announced breaker is gone'
within "$seconds" 2 25
assert_success $? "a stale lock that another process is breaking is left to it (${seconds}s)"
assert_equal '1 after.event ' "$(types "$announced/run")" 'the event is recorded once'
assert_equal ".events.lock.breaking.$breaker " "$(lock_entries "$announced")" "a dead breaker's announcement is left alone"

printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
