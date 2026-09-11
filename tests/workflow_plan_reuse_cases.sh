#!/bin/sh
: "${root:?}"
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
# Sourced by the real headless public plan-task acceptance fixture.
workflow_plan_reuse_setup() {
    : "${root:?}" "${fixture:?}"
    cp "$root/tests/fixtures/plan-task/repo/produce.sh" produce.sh
    fixture_json reuse-setup "$fixture/plan.json"
}
workflow_plan_reuse_assert() {
    : "${root:?}" "${fixture:?}" "${code:?}" "${run_dir:?}" "${run:?}"
    [ "$code" = 0 ]
    [ "$(cat "$run_dir/state")" = succeeded ]
    [ "$(cat "$run_dir/repair-round")" = 2 ]
    [ ! -e "$run_dir/repair-pending.json" ]
    for node in produce inspect; do
        [ "$(cat "$run_dir/steps/$node/attempts")" = 1 ]
        [ "$(cat "$run_dir/steps/$node/authoritative-attempt")" = 1 ]
        [ ! -e "$run_dir/steps/$node/attempt-2" ]
    done
    for node in compose verify; do
        [ "$(cat "$run_dir/steps/$node/authoritative-attempt")" = 2 ]
    done
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 6 ]
    "$root/bin/hydra" workflow plan result "$run" > "$fixture/result.json"
    fixture_json reuse-check "$run_dir/repair-2.json" "$run_dir"
    printf 'candidate\ncomposed\n' > "$fixture/expected"
    cmp "$fixture/expected" "$run_dir/steps/compose/attempt-2/artifacts/result"
    _reuse_build=${BUILD_DIR:-$root/build}
    case $_reuse_build in /*) ;; *) _reuse_build=$root/$_reuse_build ;; esac
    make -s -C "$root" BUILD_DIR="$_reuse_build" "$_reuse_build/native-tests/test-plan-reuse-invalidation"
    (cd "$root" && "$_reuse_build/native-tests/test-plan-reuse-invalidation" "$fixture" --fleet-bin "$HYDRA_FLEET_BIN")
    # shellcheck source=/dev/null
    . "$root/tests/fixture-tools.sh"
    statistics_evidence "$root/bin/hydra" "$run_dir" "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" verified
    printf 'Selective repair retained two original receipts and accepted two fresh affected tasks\n'
}
