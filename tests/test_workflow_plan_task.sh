#!/bin/sh
# Real task executions with required intermediate and combined validation.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    workflow_task_fixture_quiesce || return 1
    for workspace in "$HYDRA_HOME"/fleet/tasks/task_*/workspace; do
        [ -f "$workspace/.git/hydra/project-id" ] || continue
        (cd "$workspace" && "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
    done
    if [ "${passed:-0}" = 1 ]; then rm -rf "$fixture"; else printf 'Plan task evidence: %s\n' "$fixture" >&2; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$fixture/source"
cp "$root/tests/fixtures/plan-task/repo/"* "$fixture/source/"
cp "$root/tests/fixtures/plan-task/plan.json" "$fixture/plan.json"
cp "$root/tests/fixtures/plan-task/policy.json" "$fixture/policy.json"
cd "$fixture/source"
git init -q
git config user.name Test
git config user.email test@example.invalid
if [ "${HYDRA_TEST_PLAN_SOURCE:-0}" != 0 ]; then
    cat >> produce.sh <<'SOURCE'
printf 'derived-source\n' > marker
 git add marker result.txt
 git -c user.name=Producer -c user.email=producer@example.invalid commit -qm candidate
SOURCE
    cat >> compose.sh <<'SOURCE'
[ "$(git show HEAD:marker)" = derived-source ]
SOURCE
    sed '/"id": "compose"/,/"writes"/s/"task_input": "recipe"/"task_input": "recipe", "source_step": "produce"/' "$fixture/plan.json" > "$fixture/derived.json"
    mv "$fixture/derived.json" "$fixture/plan.json"
    if [ "${HYDRA_TEST_PLAN_SOURCE:-0}" = dirty ]; then
        printf 'printf uncommitted > dirty-file\n' >> produce.sh
    fi
fi
if [ -n "${HYDRA_TEST_PLAN_REPAIR:-}" ]; then
    # shellcheck source=/dev/null
    . "$root/tests/workflow_plan_repair_cases.sh"
    workflow_plan_repair_setup
    if [ "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" = 1 ]; then workflow_plan_repair_fault_setup; fi
fi
git add .
git commit -qm recipes
commit="$(git rev-parse HEAD)"
"$root/bin/hydra" init --no-agent --trust >/dev/null
mode="${HYDRA_TEST_PLAN_REPAIR:-${HYDRA_TEST_PLAN_TASK_VERDICT:-pass}}"
for node in produce inspect compose verify; do
    inputs='[]'; outputs='["result.txt"]'; argv="[\"sh\",\"$node.sh\"]"
    case "$node" in
        produce) argv="[\"sh\",\"produce.sh\",\"$mode\"]" ;;
        inspect) inputs='["subject","validation"]'; outputs='["report.json"]'; argv="[\"sh\",\"check.sh\",\"part-check\",\"part-content\",\"$mode\"]" ;;
        compose) inputs='["subject"]' ;;
        verify) inputs='["subject","validation"]'; outputs='["report.json"]'; argv='["sh","check.sh","final-check","final-content","pass"]' ;;
    esac
    if [ -n "${HYDRA_TEST_PLAN_REPAIR:-}" ]; then inputs="$(printf '%s' "$inputs" | sed 's/^\[/["repair",/;s/,]/]/')"; fi
    cat > "$node.json" <<JSON
{"schema_version":1,"host":"local","project":"$fixture/source","source":{"commit":"$commit"},"work":{"kind":"exec","argv":$argv},"inputs":$inputs,"outputs":$outputs,"capabilities":["exec"],"completion":"command-exit","limits":{"transport_seconds":10,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}
JSON
done
if [ "${HYDRA_TEST_PLAN_REPAIR_BUDGET:-0}" = 1 ]; then
    for definition in "$fixture/plan.json" "$fixture/policy.json"; do
        sed 's/"artifact_bytes": 65536/"artifact_bytes": 40000/' "$definition" > "$fixture/updated"
        mv "$fixture/updated" "$definition"
    done
    if "$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json" > "$fixture/compile.json"; then exit 1; fi
    grep -q invalid_inputs_or_budget "$fixture/compile.json"
    passed=1
    printf 'Plan task repair: all output rounds must fit the accepted artifact budget\n'
    exit 0
fi
"$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json" > "$fixture/compile.json"
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile.json")"
"$root/bin/hydra" workflow plan show "$fixture/compiled.json" > "$fixture/preview"
code=0
"$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/run.out" 2> "$fixture/run.err" || code=$?
run="$(sed -n '1p' "$fixture/run.out")"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
if [ -n "${HYDRA_TEST_PLAN_REPAIR:-}" ]; then
    if [ "${HYDRA_TEST_PLAN_REPAIR_FAULT:-0}" = 1 ]; then workflow_plan_repair_fault_resume; fi
    workflow_plan_repair_assert
elif [ "${HYDRA_TEST_PLAN_SOURCE:-0}" = dirty ]; then
    [ "$code" != 0 ]
    [ "$(cat "$run_dir/steps/compose/state")" = recovery-required ]
    [ ! -d "$run_dir/steps/compose/attempt-1/remote" ]
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 2 ]
elif [ "$mode" = pass ]; then
    [ "$code" = 0 ]
    [ "$(cat "$run_dir/state")" = succeeded ]
    "$root/bin/hydra" workflow plan result "$run" > "$fixture/result.json"
    printf 'candidate\ncomposed\n' > "$fixture/expected"
    cmp "$fixture/expected" "$run_dir/steps/compose/attempt-1/artifacts/result"
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 4 ]
else
    [ "$code" != 0 ]
    [ "$(cat "$run_dir/state")" = failed ]
    [ ! -d "$run_dir/steps/compose/attempt-1" ]
    [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 2 ]
fi
passed=1
printf 'Plan task validation: %s, public CLI admission and evidence join passed\n' "$mode"
