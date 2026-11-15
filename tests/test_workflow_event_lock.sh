#!/bin/sh
# The workflow event lock names the process that writes the event, so
# stale-lock recovery neither breaks a live writer's lock because the
# writer's parent exited, nor keeps a dead writer's lock because its parent
# lives. Writers here are subshells of a parent shell, as the run driver's
# step workers are.

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
    write) workflow_event "$run" "" "$4" ;;
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
esac
EOF
HYDRA_TEST_ROOT="$root"
export HYDRA_TEST_ROOT

new_run() {
    rm -rf "$base/run" "$base/holding" "$base/child" "$base/held-by" "$base/parent-result"
    mkdir "$base/run" && printf 'run_lock_test\n' > "$base/run/run-id"
    : > "$base/hold"
}
types() { sed -n 's/.*"sequence":\([0-9]*\),.*"type":"\([^"]*\)".*/\1 \2/p' "$base/run/events.jsonl" | tr '\n' ' '; }

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

printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
