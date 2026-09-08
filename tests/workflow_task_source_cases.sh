#!/bin/sh
# Fault injection at the real consumer dispatch boundary.
workflow_source_fault_setup() {
    : "${fixture:?}" "${HYDRA_FLEET_BIN:?}"
    HYDRA_SOURCE_REAL_BIN="$HYDRA_FLEET_BIN"
    export HYDRA_SOURCE_REAL_BIN
    cat > "$fixture/source-fleet" <<'WRAPPER'
#!/bin/sh
set -eu
if [ "${1:-}" = workflow-task ] && [ "${2:-}" = run ] && [ "${4:-}" = consume ]; then
    dispatch="$3/steps/produce/attempt-1/remote/dispatch.json"
    sed 's/"submission_key":"[a-f0-9]*"/"submission_key":"0000000000000000000000000000000000000000000000000000000000000000"/' "$dispatch" > "$dispatch.changed"
    mv "$dispatch.changed" "$dispatch"
    if command -v sha256sum >/dev/null 2>&1; then digest="$(sha256sum "$dispatch")"; else digest="$(shasum -a 256 "$dispatch")"; fi
    printf '%s' "${digest%% *}" > "$3/steps/produce/attempt-1/remote/dispatch-sha256"
fi
exec "$HYDRA_SOURCE_REAL_BIN" "$@"
WRAPPER
    chmod +x "$fixture/source-fleet"
    HYDRA_FLEET_BIN="$fixture/source-fleet"
    export HYDRA_FLEET_BIN
}
workflow_source_fault_assert() {
    : "${run_code:?}" "${run_dir:?}"
    [ "$run_code" != 0 ]
    [ "$(cat "$run_dir/state")" = recovery-required ]
    [ ! -d "$run_dir/steps/consume/attempt-1/remote" ]
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 1 ]
    passed=1
    export passed
    printf 'Task Git handoff: altered producer dispatch key blocks consumer before acceptance\n'
}
