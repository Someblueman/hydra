#!/bin/sh
# Sourced by test_t3_distributed.sh: the two-host plan run with a coordinator
# restart (both transports), plus lost acknowledgment, stale and malformed
# results over the controlled transport. Task identity is preserved; stale
# results never advance dependents.
: "${fixture:?}" "${root:?}" "${transport:?}" "${plan_digest:?}"
t3_acceptances() { find "$fixture/host-$1/home/fleet/tasks" -name acceptance.json 2>/dev/null | wc -l | tr -d ' '; }
t3_run_dir() { find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$1" -print; }
t3_task_id() { sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$1" | sed -n '1p'; }
t3_plan_run() { (cd "$fixture/source" && "$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$plan_digest"); }
t3_resume() { (cd "$fixture/source" && "$root/bin/hydra" workflow resume "$1"); }
# Wait for a file to exist while a background coordinator runs.
t3_wait_file() {
    _twf_polls=0
    while [ ! -s "$1" ] && [ "$_twf_polls" -lt 600 ]; do sleep 0.2; _twf_polls=$((_twf_polls + 1)); done
    [ -s "$1" ]
}

# Run 1: the complete scenario. Over the controlled transport the first
# submission's acknowledgment is lost after host-a accepted it.
if [ "$transport" = controlled ]; then
    : > "$fixture/control/lose-ack"
    : > "$fixture/control/record-result"
    run_code=0
    t3_plan_run > "$fixture/run.out" 2> "$fixture/run.err" || run_code=$?
    [ "$run_code" = 3 ] || { cat "$fixture/run.out" "$fixture/run.err" >&2; exit 1; }
    run="$(sed -n '1p' "$fixture/run.out")"
    run_dir="$(t3_run_dir "$run")"
    [ "$(cat "$run_dir/state")" = waiting-remote ]
    [ "$(t3_acceptances a)" = 1 ] && [ "$(t3_acceptances b)" = 0 ]
    lost_id="$(t3_task_id "$fixture/control/lost-response")"
    [ -n "$lost_id" ]
    cp "$run_dir/steps/produce/attempt-1/remote/dispatch.json" "$fixture/produce-dispatch"
    [ ! -e "$run_dir/steps/produce/attempt-1/remote/receipt.json" ]
    t3_resume "$run" > "$fixture/resume.out" 2> "$fixture/resume.err" &
    launcher=$!
else
    t3_plan_run > "$fixture/run.out" 2> "$fixture/run.err" &
    launcher=$!
    run_polls=0
    while [ -z "$(sed -n '1p' "$fixture/run.out" 2>/dev/null)" ] && [ "$run_polls" -lt 300 ]; do sleep 0.2; run_polls=$((run_polls + 1)); done
    run="$(sed -n '1p' "$fixture/run.out")"
    [ -n "$run" ]
    run_dir="$(t3_run_dir "$run")"
    t3_wait_file "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
    cp "$run_dir/steps/produce/attempt-1/remote/dispatch.json" "$fixture/produce-dispatch"
fi
# Coordinator restart: once the second host holds a receipt, a concurrent
# coordinator is refused, the owner is killed, and resume continues the same
# attempts with the same dispatches and receipts.
t3_wait_file "$run_dir/steps/produce/attempt-1/remote/receipt.json"
t3_wait_file "$run_dir/steps/inspect/attempt-1/remote/receipt.json"
cmp "$fixture/produce-dispatch" "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
cp "$run_dir/steps/produce/attempt-1/remote/receipt.json" "$fixture/produce-receipt"
cp "$run_dir/steps/inspect/attempt-1/remote/dispatch.json" "$fixture/inspect-dispatch"
cp "$run_dir/steps/inspect/attempt-1/remote/receipt.json" "$fixture/inspect-receipt"
if [ "$transport" = controlled ]; then [ "$(t3_task_id "$fixture/produce-receipt")" = "$lost_id" ]; fi
if t3_resume "$run" > "$fixture/concurrent.out" 2> "$fixture/concurrent.err"; then
    printf 'Concurrent coordinator unexpectedly admitted\n' >&2
    exit 1
fi
kill -KILL "$(cat "$run_dir/owner-pid")"
wait "$launcher" 2>/dev/null || :
launcher=""
resume_code=0
t3_resume "$run" > "$fixture/recovery.out" 2> "$fixture/recovery.err" || resume_code=$?
[ "$resume_code" = 0 ] || { cat "$fixture/recovery.out" "$fixture/recovery.err" >&2; exit 1; }
cmp "$fixture/produce-dispatch" "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
cmp "$fixture/produce-receipt" "$run_dir/steps/produce/attempt-1/remote/receipt.json"
cmp "$fixture/inspect-dispatch" "$run_dir/steps/inspect/attempt-1/remote/dispatch.json"
cmp "$fixture/inspect-receipt" "$run_dir/steps/inspect/attempt-1/remote/receipt.json"
[ "$(cat "$run_dir/state")" = succeeded ]
# Run, step and attempt visibility through the CLI, without attaching.
(cd "$fixture/source" && "$root/bin/hydra" workflow status "$run" --json) > "$fixture/status.json"
grep -q '"state":"succeeded"' "$fixture/status.json"
for step in produce inspect compose verify; do
    grep -q "{\"step_id\":\"$step\",\"state\":\"succeeded\",\"attempts\":1}" "$fixture/status.json"
done
printf 'candidate\ncomposed\n' > "$fixture/expected"
cmp "$fixture/expected" "$run_dir/steps/compose/attempt-1/artifacts/result"
(cd "$fixture/source" && "$root/bin/hydra" workflow plan result "$run") > "$fixture/plan-result.json"
grep -q '"ok":true' "$fixture/plan-result.json"
# Every execution has a distinct identity accepted on its placed host.
[ "$(t3_acceptances a)" = 2 ] && [ "$(t3_acceptances b)" = 2 ]
: > "$fixture/task-ids"
for placement in produce:a inspect:b compose:a verify:b; do
    step="${placement%%:*}" host="${placement#*:}"
    task_id="$(t3_task_id "$run_dir/steps/$step/attempt-1/remote/receipt.json")"
    [ -f "$fixture/host-$host/home/fleet/tasks/$task_id/acceptance.json" ]
    printf '%s\n' "$task_id" >> "$fixture/task-ids"
    "$root/bin/hydra" fleet task status "host-$host" --id "$task_id" > "$fixture/$step-status"
    grep -q '"state":"succeeded"' "$fixture/$step-status"
    grep -q '"result_state":"ready"' "$fixture/$step-status"
    "$root/bin/hydra" fleet task logs "host-$host" --id "$task_id" --source work > "$fixture/$step-logs"
    grep -q '"available":true' "$fixture/$step-logs"
    "$root/bin/hydra" fleet task observe "host-$host" --id "$task_id" > "$fixture/$step-observation"
    grep -q '"execution_state":"succeeded"' "$fixture/$step-observation"
done
[ "$(sort -u "$fixture/task-ids" | wc -l | tr -d ' ')" = 4 ]
# Recorded observations replay to the same scheduling decisions.
(cd "$fixture/source" && "$root/bin/hydra" workflow replay "$run") > "$fixture/replay-1.json"
(cd "$fixture/source" && "$root/bin/hydra" workflow replay "$run") > "$fixture/replay-2.json"
cmp "$fixture/replay-1.json" "$fixture/replay-2.json"
grep -q '"decisions":\["produce"' "$fixture/replay-1.json"
printf 'T3 (%s): two-host plan succeeded; coordinator restart preserved dispatch, receipt and task identity\n' "$transport"
if [ "$transport" = controlled ]; then
# Run 2: a stale result (another task's sealed result replayed for this
# producer) cannot advance the validator; the run needs recovery.
: > "$fixture/control/stale-result"
stale_code=0
t3_plan_run > "$fixture/stale.out" 2> "$fixture/stale.err" || stale_code=$?
[ "$stale_code" != 0 ]
stale_run="$(sed -n '1p' "$fixture/stale.out")"
stale_dir="$(t3_run_dir "$stale_run")"
[ "$(cat "$stale_dir/state")" = recovery-required ] || { cat "$stale_dir/state" "$fixture/stale.err" >&2; exit 1; }
[ ! -d "$stale_dir/steps/inspect/attempt-1" ]
[ "$(t3_acceptances a)" = 3 ] && [ "$(t3_acceptances b)" = 2 ]
[ ! -e "$fixture/control/stale-result" ]
# Run 3: a result whose digest does not match its bytes is malformed.
: > "$fixture/control/bad-result"
bad_code=0
t3_plan_run > "$fixture/bad.out" 2> "$fixture/bad.err" || bad_code=$?
[ "$bad_code" != 0 ]
bad_run="$(sed -n '1p' "$fixture/bad.out")"
bad_dir="$(t3_run_dir "$bad_run")"
[ "$(cat "$bad_dir/state")" = recovery-required ]
[ "$(cat "$bad_dir/steps/produce/attempt-1/exit-code")" = 76 ]
[ ! -d "$bad_dir/steps/inspect/attempt-1" ]
[ "$(t3_acceptances a)" = 4 ] && [ "$(t3_acceptances b)" = 2 ]
printf 'T3 (controlled): lost acknowledgment reconciled to one acceptance; stale and malformed results blocked dependents\n'
fi
