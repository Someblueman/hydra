#!/bin/sh
# Task pools under contention and failure: competing workers never
# double-claim, cancellation reaches claimed work, a crashed worker's
# unknown outcome is reconciled from evidence and never replayed, and pool
# tasks execute through the public exec and workflow commands.

set -u
test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
base="$(cd "$base" && pwd -P)"
repo="$base/repo"
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
cleanup() {
    test_stop_processes_naming "$base"
    rm -rf "$base"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

json_field() {
    printf '%s\n' "$1" | sed -n "s/.*\"$2\":\"\{0,1\}\([^\",}]*\)\"\{0,1\}[,}].*/\1/p" | sed -n '1p'
}

state_is() {
    [ "$(sed -n '1p' "$1" 2>/dev/null)" = "$2" ]
}

pool_path() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/pools/$1" | sed -n '1p'
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
git add README
git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
"$HYDRA_BIN" spawn --headless --no-agent rec-head >/dev/null 2>&1
assert_success $? "a headless head is available"

echo "Testing competing claimers..."
"$HYDRA_BIN" pool create race --max-outstanding 4 >/dev/null
for id in a b c; do "$HYDRA_BIN" pool add race --exec rec-head --id "$id" -- true >/dev/null; done
"$HYDRA_BIN" pool seal race >/dev/null
i=1
while [ "$i" -le 6 ]; do
    HYDRA_POOL_LOCK_TRIES=400 "$HYDRA_BIN" pool claim race --worker "w$i" --json > "$base/claim-$i.json" 2>&1 &
    i=$((i + 1))
done
wait
claimed="$(cat "$base"/claim-*.json | grep -c '"claimed":true')"
assert_equal 3 "$claimed" "six competing claimers obtain exactly one claim per task"
assert_equal 3 "$(cat "$base"/claim-*.json | grep -c '"claimed":false,"reason":"none"')" "the other claimers find nothing left"
assert_equal 3 "$(cat "$base"/claim-*.json | sed -n 's/.*"token":"\([0-9a-f]*\)".*/\1/p' | LC_ALL=C sort -u | wc -l | tr -d ' ')" "claim tokens are unique"
assert_equal "a b c" "$(cat "$base"/claim-*.json | sed -n 's/.*"task_id":"\([a-z]*\)".*/\1/p' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')" "each task is claimed once"
race_dir="$(pool_path race)"
assert_equal 3 "$(grep -c '"type":"task.claimed"' "$race_dir/events.jsonl")" "the event log records three claims"

echo "Testing competing workers over the public exec path..."
"$HYDRA_BIN" pool create work --max-outstanding 2 >/dev/null
for id in t1 t2 t3 t4; do
    "$HYDRA_BIN" pool add work --exec rec-head --id "$id" -- sh -c "printf '%s\\n' $id >> '$base/executed'" >/dev/null
done
"$HYDRA_BIN" pool seal work >/dev/null
"$HYDRA_BIN" pool run work --worker alpha --json > "$base/alpha.json" 2>&1 &
alpha=$!
"$HYDRA_BIN" pool run work --worker beta --json > "$base/beta.json" 2>&1 &
beta=$!
wait "$alpha"
assert_success $? "the first worker loop exits cleanly"
wait "$beta"
assert_success $? "the second worker loop exits cleanly"
assert_equal 4 "$(cat "$base/alpha.json" "$base/beta.json" | grep -o '"outcome":"succeeded"' | wc -l | tr -d ' ')" "two workers execute every task exactly once"
assert_equal "t1 t2 t3 t4" "$(LC_ALL=C sort "$base/executed" | tr '\n' ' ' | sed 's/ $//')" "each command ran once"
work_dir="$(pool_path work)"
assert_equal "1 1 1 1" "$(for id in t1 t2 t3 t4; do sed -n '1p' "$work_dir/tasks/$id/attempts"; done | tr '\n' ' ' | sed 's/ $//')" "no task was attempted twice"
grep -q '^Exec run run_' "$work_dir/tasks/t1/attempt-1/stdout"
assert_success $? "the attempt evidence is the public exec command's output"
"$HYDRA_BIN" pool status work --json | grep -q '"drained":true'
assert_success $? "the pool is drained"

echo "Testing cancellation of claimed work..."
"$HYDRA_BIN" pool create long >/dev/null
"$HYDRA_BIN" pool add long --exec rec-head --id wait --seal -- sh -c "sleep 30" >/dev/null
long_dir="$(pool_path long)"
"$HYDRA_BIN" pool run long --worker slow --json > "$base/long.json" 2>&1 &
long_worker=$!
wait_for "the long task to be claimed" state_is "$long_dir/tasks/wait/state" claimed
assert_success $? "the worker claims the long task"
out="$("$HYDRA_BIN" pool cancel long wait --json)"
assert_equal signalled "$(json_field "$out" result)" "cancelling a claimed task asks its owner to stop"
wait "$long_worker"
long_status=$?
assert_equal 0 "$long_status" "the worker loop ends after cancellation"
assert_equal cancelled "$(sed -n '1p' "$long_dir/tasks/wait/state")" "cancellation propagates to the claimed task"
grep -q '"state":"cancelled"' "$base/long.json"
assert_success $? "the worker reports the cancelled task"
grep -q '"stopped":"pool_cancelled"\|"stopped":"none"' "$base/long.json"
assert_success $? "the worker loop stopped for a recorded reason"

echo "Testing worker crash: unknown outcomes are reconciled, never replayed..."
"$HYDRA_BIN" pool create crash >/dev/null
"$HYDRA_BIN" pool add crash --exec rec-head --id risky --seal -- sh -c "sleep 45" >/dev/null
crash_dir="$(pool_path crash)"
"$HYDRA_BIN" pool run crash --worker doomed --json > "$base/crash.json" 2>&1 &
doomed=$!
wait_for "the risky task to be claimed" state_is "$crash_dir/tasks/risky/state" claimed
assert_success $? "the worker claims the risky task"
wait_for "the risky command to be supervised" sh -c "[ -f '$crash_dir/tasks/risky/attempt-1/command-pid' ]"
sleep 2
test_signal_tree "$doomed" KILL
wait "$doomed" 2>/dev/null || true
assert_equal claimed "$(sed -n '1p' "$crash_dir/tasks/risky/state")" "a crashed worker leaves its claim behind"
out="$("$HYDRA_BIN" pool status crash --json)"
printf '%s' "$out" | grep -q '"stale":true'
assert_success $? "status shows the dead owner's claim as stale"
out="$("$HYDRA_BIN" pool claim crash --worker restarted --json)"
assert_equal 1 "$(json_field "$out" reconciled)" "the restarted coordinator reconciles the dead owner"
assert_equal none "$(json_field "$out" reason)" "nothing is handed out in place of the uncertain task"
assert_equal recovery-required "$(sed -n '1p' "$crash_dir/tasks/risky/state")" "a non-idempotent task with an unknown outcome becomes recovery-required"
assert_equal 1 "$(sed -n '1p' "$crash_dir/tasks/risky/attempts")" "the uncertain attempt is not replayed"
assert_equal unknown "$(sed -n '1p' "$crash_dir/tasks/risky/attempt-1/outcome")" "the attempt outcome is recorded as unknown"
[ ! -d "$crash_dir/tasks/risky/attempt-2" ]
assert_success $? "no second attempt is invented for the risky task"
grep -q '"type":"task.stale_owner","detail":"outcome=unknown source=unknown"' "$crash_dir/events.jsonl"
assert_success $? "the stale owner is recorded with an unknown outcome"
"$HYDRA_BIN" pool create crash-safe >/dev/null
"$HYDRA_BIN" pool add crash-safe --exec rec-head --id safe --idempotent --retry 1 --seal -- sh -c "printf x >> '$base/safe-runs'; sleep 45" >/dev/null
safe_dir="$(pool_path crash-safe)"
"$HYDRA_BIN" pool run crash-safe --worker doomed-too --json > "$base/crash-safe.json" 2>&1 &
doomed=$!
wait_for "the idempotent task to start" sh -c "grep -q x '$base/safe-runs' 2>/dev/null"
assert_success $? "the idempotent task started once"
test_signal_tree "$doomed" KILL
wait "$doomed" 2>/dev/null || true
out="$("$HYDRA_BIN" pool claim crash-safe --worker restarted --json)"
assert_equal safe "$(json_field "$out" task_id)" "an idempotent task with budget is re-queued and claimed again"
assert_equal 2 "$(sed -n '1p' "$safe_dir/tasks/safe/attempts")" "the re-queued task consumed retry budget"
assert_equal unknown "$(sed -n '1p' "$safe_dir/tasks/safe/attempt-1/outcome")" "its first attempt stays recorded as unknown"

echo "Testing workflow tasks and evidence-based reconciliation..."
cat > "$base/quick.yml" <<EOF
version: 1
id: quick
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: touch
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: rec-head
      argv: [sh, -c, "printf w >> '$base/workflow-runs'"]
EOF
"$HYDRA_BIN" pool create evidence --max-outstanding 2 >/dev/null
"$HYDRA_BIN" pool add evidence --workflow "$base/quick.yml" --id one >/dev/null
"$HYDRA_BIN" pool add evidence --workflow "$base/quick.yml" --id two --seal >/dev/null
out="$("$HYDRA_BIN" pool run evidence --worker flow --max 1 --json)"
assert_success $? "a workflow task runs through hydra workflow run"
run_id="$(json_field "$out" run_id)"
case "$run_id" in run_*) assert_success 0 "the workflow run ID is recorded" ;; *) assert_success 1 "the workflow run ID is recorded ($out)" ;; esac
evidence_dir="$(pool_path evidence)"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run_id" | sed -n '1p')"
assert_equal succeeded "$(sed -n '1p' "$run_dir/state" 2>/dev/null)" "the workflow run record succeeded"
assert_equal "$run_id" "$(sed -n '1p' "$evidence_dir/tasks/one/attempt-1/run-id")" "the attempt binds the run ID"
sleep 1 &
dead_pid=$!
wait "$dead_pid"
out="$("$HYDRA_BIN" pool claim evidence --worker gone --owner-pid "$dead_pid" --json)"
assert_equal two "$(json_field "$out" task_id)" "a second workflow task is claimed by a dead owner"
printf '%s\n' "$run_id" > "$evidence_dir/tasks/two/attempt-1/run-id"
out="$("$HYDRA_BIN" pool claim evidence --worker checker --json)"
assert_equal 1 "$(json_field "$out" reconciled)" "the dead owner's claim is reconciled"
assert_equal succeeded "$(sed -n '1p' "$evidence_dir/tasks/two/state")" "a recorded terminal workflow run is authoritative evidence"
assert_equal workflow-record "$(sed -n '1p' "$evidence_dir/tasks/two/attempt-1/source")" "the outcome source is the workflow record"
assert_equal w "$(cat "$base/workflow-runs")" "reconciliation from evidence did not replay the workflow"

cat > "$base/slow.yml" <<EOF
version: 1
id: slow
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: linger
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: rec-head
      argv: [sleep, "45"]
EOF
"$HYDRA_BIN" pool create slow >/dev/null
"$HYDRA_BIN" pool add slow --workflow "$base/slow.yml" --id linger --seal >/dev/null
slow_dir="$(pool_path slow)"
"$HYDRA_BIN" pool run slow --worker patient --json > "$base/slow.json" 2>&1 &
patient=$!
wait_for "the workflow run to start" sh -c "grep -q '^run_' '$slow_dir/tasks/linger/attempt-1/stdout' 2>/dev/null"
assert_success $? "the workflow task publishes its run ID early"
slow_run="$(sed -n '1p' "$slow_dir/tasks/linger/attempt-1/stdout")"
slow_run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$slow_run" | sed -n '1p')"
wait_for "the workflow step to run" state_is "$slow_run_dir/steps/linger/state" running
out="$("$HYDRA_BIN" pool cancel slow --json)"
assert_equal 1 "$(json_field "$out" signalled)" "pool cancellation signals the claimed workflow task"
wait "$patient" 2>/dev/null || true
assert_equal cancelled "$(sed -n '1p' "$slow_dir/tasks/linger/state")" "the workflow task ends cancelled"
assert_equal cancelled "$(sed -n '1p' "$slow_run_dir/state")" "cancellation reached the workflow run through hydra workflow cancel"
"$HYDRA_BIN" state verify >/dev/null 2>&1
assert_success $? "state verifies after pool execution"
"$HYDRA_BIN" kill rec-head --force >/dev/null 2>&1 || true

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
