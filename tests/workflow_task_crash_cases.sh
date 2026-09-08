#!/bin/sh
# Sourced by the real-task fixture. Crash ownership, preserving accepted work.
: "${root:?}" "${fixture:?}"
"$root/bin/hydra" workflow run "$fixture/workflow.yml" > "$fixture/run.out" 2> "$fixture/run.err" &
launcher=$!
run_dir=''
wait_tick=0
while [ "$wait_tick" -lt 100 ]; do
    wait_tick=$((wait_tick + 1))
    run="$(sed -n '1p' "$fixture/run.out")"
    if [ -n "$run" ]; then
        run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
        if [ -n "$run_dir" ] && [ -s "$run_dir/steps/produce/attempt-1/remote/receipt.json" ]; then break; fi
    fi
    sleep 0.2
done
[ -s "$run_dir/steps/produce/attempt-1/remote/receipt.json" ]
cp "$run_dir/steps/produce/attempt-1/remote/receipt.json" "$fixture/original-receipt"
cp "$run_dir/steps/produce/attempt-1/remote/dispatch.json" "$fixture/original-dispatch"
if "$root/bin/hydra" workflow resume "$run" > "$fixture/concurrent.out" 2> "$fixture/concurrent.err"; then
    printf 'Concurrent coordinator unexpectedly admitted\n' >&2
    exit 1
fi
owner="$(cat "$run_dir/owner-pid")"
kill -KILL "$owner"
if [ "${HYDRA_TEST_DAG_CRASH:-0}" = 2 ]; then
    # Stop only the coordinator's observer; the accepted task remains remote.
    worker="$(cat "$run_dir/steps/produce/worker-pid")"
    observer="$(cat "$run_dir/steps/produce/command-pid")"
    children="$(pgrep -P "$observer" || :)"
    kill -KILL "$worker" "$observer" 2>/dev/null || :
    printf '%s\n' "$children" | while IFS= read -r child; do
        [ -z "$child" ] || kill -KILL "$child" 2>/dev/null || :
    done
fi
wait "$launcher" 2>/dev/null || :
"$root/bin/hydra" workflow resume "$run" > "$fixture/recovery.out" 2> "$fixture/recovery.err" || run_code=$?
[ "$run_code" = 0 ]
cmp "$fixture/original-receipt" "$run_dir/steps/produce/attempt-1/remote/receipt.json"
cmp "$fixture/original-dispatch" "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
printf 'Task DAG: concurrent owner rejected; SIGKILL recovery preserves dispatch and receipt\n'
