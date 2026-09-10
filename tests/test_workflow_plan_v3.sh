#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
make -s -C "$root" BUILD_DIR="${BUILD_DIR:-build}" build-test-fixture
fixture_native="${BUILD_DIR:-$root/build}/native-tests/fixture-json"
case "$fixture_native" in /*) ;; *) fixture_native="$root/$fixture_native" ;; esac
fixture=$(mktemp -d)
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
active_branch=
cleanup() {
    [ -z "$active_branch" ] || "$root/bin/hydra" kill "$active_branch" --force >/dev/null 2>&1 || true
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
mkdir "$fixture/repo"
cp "$fixture_native" "$fixture/repo/fixture-native"
cp "$root/tests/fixtures/plan/repo/compose.sh" "$fixture/repo/"
cp "$root/tests/fixtures/plan/repo/expected.txt" "$fixture/repo/"
cat > "$fixture/repo/check.sh" <<'CHECK'
#!/bin/sh
set -eu
exec "$(dirname "$0")/fixture-native" v3-report "$HYDRA_WORKFLOW_INPUTS_DIR/subject" "$HYDRA_WORKFLOW_VALIDATION_FILE" "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
CHECK
chmod +x "$fixture/repo/check.sh"
cat > "$fixture/repo/compose.sh" <<'COMPOSE'
#!/bin/sh
set -eu
if [ "${HYDRA_V3_FAULT:-}" = wrong-artifact ]; then
    printf 'Wrong candidate artifact\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
else
    printf 'Delivered report\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
fi
COMPOSE
chmod +x "$fixture/repo/compose.sh"
cp "$root/tests/fixtures/plan/plan.json" "$fixture/plan.json"
cp "$root/tests/fixtures/plan/policy.json" "$fixture/policy.json"
fixture_json v3-plan "$fixture/plan.json"
cp "$fixture/plan.json" "$fixture/plan.base.json"
cd "$fixture/repo"
git init -q; git config user.name Test; git config user.email test@example.invalid; git add .; git commit -qm fixture
"$root/bin/hydra" init --no-agent --trust >/dev/null
run_case() {
    mode=$1; expected=$2; report_verdict=$3
    branch="plan-smoke-${mode:-ok}"
    active_branch=$branch
    cp "$fixture/plan.base.json" "$fixture/plan.json"
    fixture_json v3-mode "$fixture/plan.json" "$mode" "$branch"
    rm -f "$fixture/compiled.json"
    "$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json" > "$fixture/compile-$mode.json" 2> "$fixture/compile-$mode.err" || { cat "$fixture/compile-$mode.err"; cat "$fixture/compile-$mode.json"; cat "$fixture/plan.json"; exit 1; }
    "$root/bin/hydra" workflow plan check-definition "$fixture/compiled.json" check >/dev/null
    "$root/bin/hydra" workflow plan check-recipe "$fixture/compiled.json" check >/dev/null
    digest=$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile-$mode.json")
    HYDRA_V3_FAULT="$mode" "$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/run-$mode.out" 2>/dev/null || true
    [ -n "$(sed -n '1p' "$fixture/run-$mode.out")" ]
    run=$(sed -n '1p' "$fixture/run-$mode.out")
    run_dir=$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print -quit)
    state=$(cat "$run_dir/state")
    [ -s "$run_dir/steps/verify/attempt-1/outputs/check.json" ]
    grep -q '"type":"run\.' "$run_dir/events.jsonl"
    actual_verdict=$(fixture_json verdict "$run_dir/steps/verify/attempt-1/outputs/check.json")
    [ "$actual_verdict" = "$report_verdict" ]
    "$root/bin/hydra" kill "$branch" --force >/dev/null 2>&1 || true
    active_branch=
    [ "$state" = "$expected" ] || exit 1
}
run_case '' succeeded pass
run_case wrong-artifact failed fail
run_case drop failed pass
run_case raw-tampered failed pass
run_case stale-subject failed pass
run_case missing-measurement failed pass
run_case changed-predicate failed pass
run_case malformed-recipe failed pass
run_case nonzero failed fail
run_case assessment-pass succeeded pass
run_case assessment-fail failed fail
run_case assessment-inconclusive failed inconclusive
run_case assessment-missing-authority failed pass
run_case assessment-stale-rubric failed pass
echo 'Public workflow v3 evidence controls passed'
