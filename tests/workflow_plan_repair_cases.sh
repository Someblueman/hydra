#!/bin/sh
# Sourced by the existing public plan-task acceptance fixture.
# Task variables in generated script text must remain literal.
# shellcheck disable=SC2016
workflow_plan_repair_setup() {
    : "${fixture:?}"
    for definition in "$fixture/plan.json" "$fixture/policy.json"; do
        sed 's/"repair_budget": 0/"repair_budget": 1/;s/"max_heads": 4/"max_heads": 8/;s/"timeout_seconds": 600/"timeout_seconds": 1200/' "$definition" > "$fixture/updated"
        mv "$fixture/updated" "$definition"
    done
    sed '/"recipe": {/i\
          "repair": {"repair":"plan"},
' "$fixture/plan.json" > "$fixture/updated"
    mv "$fixture/updated" "$fixture/plan.json"
    cat > produce.sh <<'SCRIPT'
#!/bin/sh
set -eu
attempt="$(sed -n 's/.*"attempt":\([0-9]*\).*/\1/p' "$HYDRA_TASK_INPUT_DIR/repair")"
case "$1" in
    pass|crash) if [ "$attempt" = 1 ]; then printf 'bad\n'; else printf 'candidate\n'; fi ;;
    exhaust) printf 'bad-%s\n' "$attempt" ;;
    same) printf 'bad\n' ;;
    combine) printf 'candidate\n' ;;
esac > result.txt
SCRIPT
    if [ "${HYDRA_TEST_PLAN_SOURCE:-0}" = 1 ]; then
        cat >> produce.sh <<'SOURCE'
printf 'derived-source\n' > marker
 git add marker result.txt
 git -c user.name=Producer -c user.email=producer@example.invalid commit -qm candidate
SOURCE
    fi
    if [ "$HYDRA_TEST_PLAN_REPAIR" = combine ]; then
        cat >> compose.sh <<'COMPOSE'
if grep -q '"attempt":1' "$HYDRA_TASK_INPUT_DIR/repair"; then printf 'bad composition\n' > result.txt; fi
COMPOSE
    fi
    sed '/^verdict=pass$/d; /set -eu/a\
verdict=pass
s/esac | cmp - "$HYDRA_TASK_INPUT_DIR\/subject"/esac | cmp - "$HYDRA_TASK_INPUT_DIR\/subject" || verdict=fail/' check.sh > "$fixture/check"
    mv "$fixture/check" check.sh
    if [ "$HYDRA_TEST_PLAN_REPAIR" = same ]; then
        sed '/^printf .*schema_version/i\
if grep -q '\''"attempt":2'\'' "$HYDRA_TASK_INPUT_DIR/repair"; then verdict=pass; fi
' check.sh > "$fixture/check"
        mv "$fixture/check" check.sh
    fi
}
workflow_plan_repair_assert() {
    : "${run_dir:?}" "${code:?}" "${root:?}" "${run:?}"
    if [ "$HYDRA_TEST_PLAN_REPAIR" = crash ]; then
        [ "$code" != 0 ]
        [ ! -e "$run_dir/repair-2.json" ]
        [ ! -d "$run_dir/steps/produce/attempt-2" ]
        return
    fi
    [ "$(cat "$run_dir/repair-round")" = 2 ]
    [ -s "$run_dir/repair-2.json" ]
    [ ! -e "$run_dir/repair-pending.json" ]
    [ "$(cat "$run_dir/steps/produce/authoritative-attempt")" = 2 ]
    failed_step=inspect
    [ "$HYDRA_TEST_PLAN_REPAIR" != combine ] || failed_step=verify
    grep -q '"verdict":"fail"' "$run_dir/steps/$failed_step/attempt-1/artifacts/report"
    grep -q '"previous":{' "$run_dir/steps/produce/attempt-2/inputs/repair"
    if cmp -s "$run_dir/steps/produce/attempt-1/remote/receipt.json" "$run_dir/steps/produce/attempt-2/remote/receipt.json"; then exit 1; fi
    if cmp -s "$run_dir/steps/inspect/attempt-1/remote/receipt.json" "$run_dir/steps/inspect/attempt-2/remote/receipt.json"; then exit 1; fi
    if [ "$HYDRA_TEST_PLAN_REPAIR" = pass ] || [ "$HYDRA_TEST_PLAN_REPAIR" = combine ]; then
        [ "$code" = 0 ]
        [ "$(cat "$run_dir/state")" = succeeded ]
        printf 'candidate\ncomposed\n' > "$fixture/expected"
        cmp "$fixture/expected" "$run_dir/steps/compose/attempt-2/artifacts/result"
        "$root/bin/hydra" workflow plan result "$run" > "$fixture/result.json"
        python3 "$root/tests/statistics_evidence.py" "$root/bin/hydra" "$run_dir" "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" verified
        task_count=6
        [ "$HYDRA_TEST_PLAN_REPAIR" != combine ] || task_count=8
        [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = "$task_count" ]
    else
        [ "$code" != 0 ]
        [ "$(cat "$run_dir/state")" = failed ]
        [ ! -d "$run_dir/steps/compose/attempt-2" ]
        [ ! -e "$run_dir/repair-3.json" ]
        [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 4 ]
    fi
}

workflow_plan_repair_fault_setup() {
    HYDRA_REPAIR_REAL_BIN="$HYDRA_FLEET_BIN"
    HYDRA_REPAIR_FAULT_MARKER="$fixture/repair-fault"
    HYDRA_REPAIR_FAULT_STEP=inspect
    [ "${HYDRA_TEST_PLAN_REUSE:-0}" != 1 ] || HYDRA_REPAIR_FAULT_STEP=verify
    export HYDRA_REPAIR_REAL_BIN HYDRA_REPAIR_FAULT_MARKER HYDRA_REPAIR_FAULT_STEP
    cat > "$fixture/fleet-wrapper" <<'WRAPPER'
#!/bin/sh
set -eu
if [ "${1:-}" = workflow-plan ] && [ "${2:-}" = repair ] && [ ! -f "$HYDRA_REPAIR_FAULT_MARKER" ]; then
    # Producer reset is durable before the next step's write is refused.
    chmod 500 "$3/steps/$HYDRA_REPAIR_FAULT_STEP"
    result=0
    "$HYDRA_REPAIR_REAL_BIN" "$@" || result=$?
    chmod 700 "$3/steps/$HYDRA_REPAIR_FAULT_STEP"
    : > "$HYDRA_REPAIR_FAULT_MARKER"
    exit "$result"
fi
exec "$HYDRA_REPAIR_REAL_BIN" "$@"
WRAPPER
    chmod +x "$fixture/fleet-wrapper"
    HYDRA_FLEET_BIN="$fixture/fleet-wrapper"
    export HYDRA_FLEET_BIN
}
workflow_plan_repair_fault_resume() {
    [ "$code" != 0 ]
    [ -s "$run_dir/repair-pending.json" ]
    reset_step=produce pending_step=inspect
    if [ "${HYDRA_TEST_PLAN_REUSE:-0}" = 1 ]; then reset_step=compose pending_step=verify; fi
    [ "$(cat "$run_dir/steps/$reset_step/attempts")" = 2 ]
    [ "$(cat "$run_dir/steps/$pending_step/attempts")" = 1 ]
    cp "$run_dir/repair-2.json" "$fixture/repair-original"
    "$root/bin/hydra" workflow resume "$run" > "$fixture/resume.out" 2> "$fixture/resume.err"
    cmp "$fixture/repair-original" "$run_dir/repair-2.json"
    code=0
}
