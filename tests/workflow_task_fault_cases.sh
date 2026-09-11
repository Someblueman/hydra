#!/bin/sh
# Sourced after a lost submit acknowledgement; exactly one task was accepted.
: "${root:?}" "${fixture:?}" "${run:?}" "${run_dir:?}"
case "$HYDRA_TEST_DAG_FAULT" in
    dispatch)
        printf ' ' >> "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
        expected=recovery-required
        ;;
    attempt)
        printf '2\n' > "$run_dir/steps/produce/attempts"
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
    cancel)
        : "${original_id:?}"
        # The confirmed-stop case starts after the receiver has published its
        # plain head. Cancelling during incomplete startup may correctly remain
        # outcome_unknown; the offline case below covers that safety boundary.
        cancellation_poll=0
        while [ "$cancellation_poll" -lt 150 ]; do
            "$root/bin/hydra" fleet task status build --id "$original_id" > "$fixture/receiver-status"
            if grep -q '"execution_head_id"' "$fixture/receiver-status"; then break; fi
            sleep 0.2
            cancellation_poll=$((cancellation_poll + 1))
        done
        grep -q '"execution_head_id"' "$fixture/receiver-status"
        expected=cancelled
        ;;
    *) exit 1 ;;
esac
if [ "$HYDRA_TEST_DAG_FAULT" = cancel-offline ]; then
    HYDRA_TEST_DAG_RUN=$run_dir
    HYDRA_TEST_DAG_REAL_FIND=$(command -v find)
    HYDRA_TEST_DAG_REAL_MV=$(command -v mv)
    export HYDRA_TEST_DAG_RUN HYDRA_TEST_DAG_REAL_FIND HYDRA_TEST_DAG_REAL_MV
    mkdir "$fixture/snapshot-tools"
    for tool in find mv; do
        ln -s "$root/tests/fixtures/workflow/cancel-snapshot.sh" "$fixture/snapshot-tools/$tool"
    done
    printf '0\n' > "$fixture/cancel-snapshot-count"
    PATH="$fixture/snapshot-tools:$PATH"
    export PATH
fi
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
actual=$(cat "$run_dir/state")
if [ "$actual" != "$expected" ]; then
    printf 'Task DAG %s: expected %s, got %s\n' "$HYDRA_TEST_DAG_FAULT" "$expected" "$actual" >&2
    cat "$fixture/fault.out" "$fixture/fault.err" >&2
    exit 1
fi
if [ "$HYDRA_TEST_DAG_FAULT" = cancel-offline ]; then [ -f "$fixture/cancel-snapshot-raced" ]; fi
[ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 1 ]
[ ! -d "$run_dir/steps/consume/attempt-1" ]
export passed=1
printf 'Task DAG: %s blocks downstream work without another acceptance (%s)\n' "$HYDRA_TEST_DAG_FAULT" "$expected"
exit 0
