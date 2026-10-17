#!/bin/sh
# Task pools: sealed bounds, frozen expansions, unique claims, retry and
# repair budgets, stale-owner rejection and JSON envelopes. No task is
# executed here; execution is covered by test_pool_recovery.sh.

set -u
test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
repo="$base/repo"
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
trap 'rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

json_field() {
    printf '%s\n' "$1" | sed -n "s/.*\"$2\":\"\{0,1\}\([^\",}]*\)\"\{0,1\}[,}].*/\1/p" | sed -n '1p'
}

assert_envelope() {
    case "$1" in
        *'"schema_version":1'*'"ok":'"$3"*'"command":"'"$2"'"'*)
            assert_success 0 "$4" ;;
        *) assert_success 1 "$4"; echo "  Output: $1" ;;
    esac
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
git add README
git commit -qm base
"$HYDRA_BIN" pool list >/dev/null 2>&1
assert_failure $? "pool commands require an initialized project"
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "init prepares the project"
"$HYDRA_BIN" spawn --headless --no-agent pool-head >/dev/null 2>&1
assert_success $? "a headless head is available for exec tasks"

echo "Testing help and validation..."
"$HYDRA_BIN" pool --help | grep -q 'hydra pool claim <pool> --worker'
assert_success $? "pool --help documents claim"
"$HYDRA_BIN" help | grep -q '^  pool '
assert_success $? "hydra help lists pool"
"$HYDRA_BIN" pool create 'Bad Name' >/dev/null 2>&1
assert_failure $? "invalid pool names are refused"
out="$("$HYDRA_BIN" pool create bounded --max-tasks 2 --max-outstanding 1 --max-expansions 2 --max-artifact-bytes 60 --max-repair-rounds 1 --lease 2 --json)"
assert_envelope "$out" "pool create" true "create emits the JSON envelope"
assert_equal '"max_tasks":2,"max_outstanding":1,"max_expansions":2,"max_artifact_bytes":60,"max_repair_rounds":1,"lease_seconds":2,"retry":0' \
    "$(printf '%s' "$out" | sed -n 's/.*"bounds":{\([^}]*\)}.*/\1/p')" "bounds are recorded as created"
"$HYDRA_BIN" pool create bounded >/dev/null 2>&1
assert_failure $? "a duplicate pool is refused"
pool_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path '*/pools/bounded' | sed -n '1p')"
assert_equal 1 "$(sed -n '1p' "$pool_dir/schema-version")" "pool records schema version 1"
assert_equal "$(git hash-object "$pool_dir/manifest.json")" "$(sed -n '1p' "$pool_dir/manifest-digest")" "bounds manifest digest is sealed"
assert_equal "" "$(find "$pool_dir" ! -type l \( -perm -0020 -o -perm -0002 \) -print)" "pool state is private"

echo "Testing bounded growth and sealing..."
"$HYDRA_BIN" pool add bounded --workflow missing >/dev/null 2>&1
assert_failure $? "a missing workflow definition is refused"
"$HYDRA_BIN" pool add bounded --exec no-such-head -- true >/dev/null 2>&1
assert_failure $? "an unknown head is refused"
"$HYDRA_BIN" pool add bounded --exec pool-head --retry 1 -- true >/dev/null 2>&1
assert_failure $? "retry requires idempotent"
out="$("$HYDRA_BIN" pool add bounded --exec pool-head --id first --idempotent --retry 1 --json -- true)"
assert_envelope "$out" "pool add" true "add emits the JSON envelope"
assert_equal pending "$(json_field "$out" state)" "an added task is pending until its expansion is sealed"
out="$("$HYDRA_BIN" pool claim bounded --worker w1 --json)"
assert_equal unsealed "$(json_field "$out" reason)" "claim reports an unsealed expansion"
out="$("$HYDRA_BIN" pool add bounded --exec pool-head --json -- false)"
assert_equal task-0002 "$(json_field "$out" task_id)" "generated task IDs follow the sequence"
"$HYDRA_BIN" pool add bounded --exec pool-head -- true >/dev/null 2>&1
assert_failure $? "max-tasks bounds growth"
"$HYDRA_BIN" pool add bounded --exec pool-head -- true --json 2>&1 | grep -q growth_exhausted
out="$("$HYDRA_BIN" pool seal bounded --json)"
assert_envelope "$out" "pool seal" true "seal emits the JSON envelope"
assert_equal 1 "$(json_field "$out" expansion)" "the first expansion is sealed"
assert_equal '"first","task-0002"' "$(sed -n 's/.*"tasks":\[\(.*\)\].*/\1/p' "$pool_dir/expansions/1/manifest.json")" "the sealed manifest freezes membership"
assert_equal "$(git hash-object "$pool_dir/expansions/1/manifest.json")" "$(sed -n '1p' "$pool_dir/expansions/1/manifest-digest")" "the expansion manifest digest is recorded"
"$HYDRA_BIN" pool seal bounded >/dev/null 2>&1
assert_failure $? "sealing without an open expansion is refused"
"$HYDRA_BIN" pool create roomy --max-tasks 8 --max-expansions 1 --max-artifact-bytes 40 >/dev/null
"$HYDRA_BIN" pool add roomy --exec pool-head --seal -- true >/dev/null
"$HYDRA_BIN" pool add roomy --exec pool-head -- true >/dev/null 2>&1
assert_failure $? "max-expansions bounds growth"
"$HYDRA_BIN" pool create tiny --max-artifact-bytes 12 >/dev/null
"$HYDRA_BIN" pool add tiny --exec pool-head -- a-very-long-argument-that-exceeds-the-bound >/dev/null 2>&1
assert_failure $? "max-artifact-bytes bounds task definitions"

echo "Testing unique claims and stale-owner rejection..."
out="$("$HYDRA_BIN" pool claim bounded --worker w1 --json)"
assert_envelope "$out" "pool claim" true "claim emits the JSON envelope"
assert_equal first "$(json_field "$out" task_id)" "the first ready task is claimed in order"
token="$(json_field "$out" token)"
out="$("$HYDRA_BIN" pool claim bounded --worker w2 --json)"
assert_equal outstanding_limit "$(json_field "$out" reason)" "max-outstanding bounds claimed work"
"$HYDRA_BIN" pool complete bounded first --token wrong --outcome succeeded >/dev/null 2>&1
assert_failure $? "a wrong token cannot complete a claim"
"$HYDRA_BIN" pool complete bounded first --token wrong --outcome succeeded --json 2>/dev/null | grep -q stale_owner
assert_success $? "the wrong token is reported as stale_owner"
out="$("$HYDRA_BIN" pool complete bounded first --token "$token" --outcome failed --json)"
assert_envelope "$out" "pool complete" true "complete emits the JSON envelope"
assert_equal ready "$(json_field "$out" state)" "an idempotent failure with budget returns to ready"
"$HYDRA_BIN" pool complete bounded first --token "$token" --outcome succeeded >/dev/null 2>&1
assert_failure $? "a released token is rejected afterwards"
out="$("$HYDRA_BIN" pool claim bounded --worker w2 --lease 1 --json)"
assert_equal first "$(json_field "$out" task_id)" "the retried task is claimed again"
token2="$(json_field "$out" token)"
sleep 2
out="$("$HYDRA_BIN" pool status bounded --json)"
assert_equal '"stale":true' "$(printf '%s' "$out" | sed -n 's/.*\("stale":[a-z]*\).*/\1/p')" "status shows an expired lease as stale without mutating"
out="$("$HYDRA_BIN" pool claim bounded --worker w3 --json)"
assert_equal 1 "$(json_field "$out" reconciled)" "the next claim reconciles the expired claim"
assert_equal task-0002 "$(json_field "$out" task_id)" "the exhausted task is not re-queued; the next task is claimed"
assert_equal recovery-required "$(sed -n '1p' "$pool_dir/tasks/first/state")" "an idempotent task without budget waits for a decision on an unknown outcome"
"$HYDRA_BIN" pool complete bounded first --token "$token2" --outcome succeeded >/dev/null 2>&1
assert_failure $? "the stale owner cannot complete after reconciliation"
grep -q '"type":"task.stale_owner"' "$pool_dir/events.jsonl"
assert_success $? "stale ownership is recorded as an event"

echo "Testing unknown outcomes, operator decisions and repair rounds..."
token3="$(json_field "$out" token)"
sleep 1 &
dead_pid=$!
wait "$dead_pid"
"$HYDRA_BIN" pool complete bounded task-0002 --token "$token3" --outcome failed >/dev/null
assert_equal failed "$(sed -n '1p' "$pool_dir/tasks/task-0002/state")" "a non-idempotent failure is terminal"
"$HYDRA_BIN" pool create unknown --max-outstanding 2 --max-repair-rounds 1 >/dev/null
"$HYDRA_BIN" pool add unknown --exec pool-head --id risky -- true >/dev/null
"$HYDRA_BIN" pool add unknown --exec pool-head --id safe --idempotent --retry 1 --seal -- true >/dev/null
out="$("$HYDRA_BIN" pool claim unknown --worker dead --owner-pid "$dead_pid" --json)"
assert_equal risky "$(json_field "$out" task_id)" "a claim records its owner pid"
unknown_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path '*/pools/unknown' | sed -n '1p')"
assert_equal "$dead_pid" "$(sed -n '1p' "$unknown_dir/tasks/risky/claim/pid")" "the claim carries the lock-protocol owner record"
assert_equal "$(hostname)" "$(sed -n '1p' "$unknown_dir/tasks/risky/claim/host")" "the claim records the owner host"
out="$("$HYDRA_BIN" pool claim unknown --worker dead --owner-pid "$dead_pid" --json)"
assert_equal 1 "$(json_field "$out" reconciled)" "a dead owner is reconciled on the next claim"
assert_equal recovery-required "$(sed -n '1p' "$unknown_dir/tasks/risky/state")" "a non-idempotent unknown outcome is never invented: recovery-required"
assert_equal safe "$(json_field "$out" task_id)" "the next ready task is claimed by the dead owner"
out="$("$HYDRA_BIN" pool claim unknown --worker next --json)"
assert_equal 1 "$(json_field "$out" reconciled)" "the second dead owner is reconciled"
assert_equal safe "$(json_field "$out" task_id)" "an idempotent unknown outcome with budget is re-queued and claimed"
assert_equal 2 "$(sed -n '1p' "$unknown_dir/tasks/safe/attempts")" "the re-queued attempt consumes retry budget"
"$HYDRA_BIN" pool complete unknown risky --token x --outcome succeeded >/dev/null 2>&1
assert_failure $? "a recovery-required task has no live claim to complete"
"$HYDRA_BIN" pool complete unknown risky --by operator --outcome retry --json | grep -q '"state":"ready"'
assert_success $? "an operator may re-queue a recovery-required task"
assert_equal 1 "$(sed -n '1p' "$unknown_dir/repair-rounds")" "an operator retry consumes a repair round"
"$HYDRA_BIN" pool claim unknown --worker dead --owner-pid "$dead_pid" --json >/dev/null
"$HYDRA_BIN" pool claim unknown --worker again --json >/dev/null
assert_equal recovery-required "$(sed -n '1p' "$unknown_dir/tasks/risky/state")" "the repaired task returns to recovery-required on another unknown outcome"
"$HYDRA_BIN" pool complete unknown risky --by operator --outcome retry >/dev/null 2>&1
assert_failure $? "repair rounds are bounded"
out="$("$HYDRA_BIN" pool complete unknown risky --by operator --outcome failed --json)"
assert_equal failed "$(json_field "$out" state)" "an operator may record a terminal decision"
assert_equal "failed by operator" "$(sed -n '1p' "$unknown_dir/tasks/risky/attempt-2/decision")" "the decision is recorded with its author"

echo "Testing cancellation and listing..."
"$HYDRA_BIN" pool create cancel-me --max-outstanding 2 >/dev/null
"$HYDRA_BIN" pool add cancel-me --exec pool-head --id a -- true >/dev/null
"$HYDRA_BIN" pool add cancel-me --exec pool-head --id b --seal -- true >/dev/null
"$HYDRA_BIN" pool add cancel-me --exec pool-head --id c -- true >/dev/null
out="$("$HYDRA_BIN" pool claim cancel-me --worker w --json)"
token_a="$(json_field "$out" token)"
out="$("$HYDRA_BIN" pool cancel cancel-me b --json)"
assert_envelope "$out" "pool cancel" true "cancel emits the JSON envelope"
assert_equal cancelled "$(json_field "$out" state)" "a ready task is cancelled immediately"
out="$("$HYDRA_BIN" pool cancel cancel-me --json)"
assert_equal 1 "$(json_field "$out" cancelled)" "pool cancellation cancels the pending task"
assert_equal 1 "$(json_field "$out" signalled)" "pool cancellation asks the claimed task to stop"
cancel_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path '*/pools/cancel-me' | sed -n '1p')"
assert_equal cancelled "$(sed -n '1p' "$cancel_dir/tasks/c/state")" "a pending task in an open expansion is cancelled"
assert_equal claimed "$(sed -n '1p' "$cancel_dir/tasks/a/state")" "a claimed task stays claimed until its owner reports"
out="$("$HYDRA_BIN" pool complete cancel-me a --token "$token_a" --outcome failed --json)"
assert_equal cancelled "$(json_field "$out" state)" "a claimed task reporting after cancellation ends cancelled"
"$HYDRA_BIN" pool add cancel-me --exec pool-head -- true >/dev/null 2>&1
assert_failure $? "a cancelled pool accepts no tasks"
out="$("$HYDRA_BIN" pool claim cancel-me --worker w --json)"
assert_equal pool_cancelled "$(json_field "$out" reason)" "a cancelled pool issues no claims"
out="$("$HYDRA_BIN" pool list --json)"
assert_envelope "$out" "pool list" true "list emits the JSON envelope"
printf '%s' "$out" | grep -q '"pool":"bounded"' && printf '%s' "$out" | grep -q '"pool":"cancel-me","state":"cancelled"'
assert_success $? "list names every pool with its state"
out="$("$HYDRA_BIN" pool status bounded --json)"
assert_envelope "$out" "pool status" true "status emits the JSON envelope"
printf '%s' "$out" | grep -q '"drained":true'
assert_success $? "a pool with no open work reports drained"
"$HYDRA_BIN" pool status bounded | grep -q '^  first \[exec\]: recovery-required (attempts=2, expansion=1)$'
assert_success $? "text status lists tasks with attempts and expansion"
"$HYDRA_BIN" state verify >/dev/null 2>&1
assert_success $? "state verifies with pool records present"
"$HYDRA_BIN" kill pool-head --force >/dev/null 2>&1 || true

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
