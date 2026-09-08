#!/bin/sh
# Sourced after a lost submit acknowledgement; exactly one task was accepted.
: "${root:?}" "${fixture:?}" "${run:?}" "${run_dir:?}"
case "$HYDRA_TEST_DAG_FAULT" in
    dispatch)
        printf ' ' >> "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
        expected=recovery-required
        ;;
    key)
        dispatch="$run_dir/steps/produce/attempt-1/remote/dispatch.json"
        sed 's/"submission_key":"[a-f0-9]*"/"submission_key":"0000000000000000000000000000000000000000000000000000000000000000"/' "$dispatch" > "$fixture/wrong-key"
        cp "$fixture/wrong-key" "$dispatch"
        if command -v sha256sum >/dev/null 2>&1; then digest="$(sha256sum "$dispatch")"; else digest="$(shasum -a 256 "$dispatch")"; fi
        printf '%s' "${digest%% *}" > "$run_dir/steps/produce/attempt-1/remote/dispatch-sha256"
        expected=recovery-required
        ;;
    placement|cancel-offline)
        "$root/bin/hydra" remote remove build >/dev/null
        "$root/bin/hydra" remote add build different-host --hydra "$root/bin/hydra" --home "$HYDRA_HOME" >/dev/null
        expected=waiting-remote
        ;;
    cancel) expected=cancelled ;;
    *) exit 1 ;;
esac
case "$HYDRA_TEST_DAG_FAULT" in
    cancel*) "$root/bin/hydra" workflow cancel "$run" > "$fixture/fault.out" 2> "$fixture/fault.err" || : ;;
    *) "$root/bin/hydra" workflow resume "$run" > "$fixture/fault.out" 2> "$fixture/fault.err" || : ;;
esac
if [ "$HYDRA_TEST_DAG_FAULT" = cancel ]; then
    cancellation_poll=0
    while [ "$(cat "$run_dir/state")" = waiting-remote ] && [ "$cancellation_poll" -lt 10 ]; do
        sleep 1
        cancellation_poll=$((cancellation_poll + 1))
        "$root/bin/hydra" workflow resume "$run" > "$fixture/cancel-resume.out" 2> "$fixture/cancel-resume.err" || :
    done
fi
[ "$(cat "$run_dir/state")" = "$expected" ]
[ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 1 ]
[ ! -d "$run_dir/steps/consume/attempt-1" ]
export passed=1
printf 'Task DAG: %s blocks downstream work without another acceptance (%s)\n' "$HYDRA_TEST_DAG_FAULT" "$expected"
exit 0
