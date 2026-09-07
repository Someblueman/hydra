#!/bin/sh
# Real CLI compilation, admission, execution and final artifact acceptance.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
ROOT="$(mktemp -d)"
export HYDRA_HOME="$ROOT/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    for head in plan-smoke plan-negative plan-guard-graph plan-guard-limits; do
        (cd "$ROOT/repo" && "$HYDRA_BIN" kill "$head" --force >/dev/null 2>&1) || true
    done
    if [ "$fail_count" -ne 0 ]; then printf 'Failure evidence: %s\n' "$ROOT"; return; fi
    rm -rf "$ROOT"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$ROOT/repo"
cp "$REPO/tests/fixtures/plan/repo/"* "$ROOT/repo/"
cp "$REPO/tests/fixtures/plan/plan.json" "$ROOT/plan.json"
cp "$REPO/tests/fixtures/plan/policy.json" "$ROOT/policy.json"
cd "$ROOT/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.com
git add . && git commit -qm fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" workflow plan schema > "$ROOT/schema.json"
assert_success $? 'planning schema is discoverable through the public CLI'
"$HYDRA_BIN" workflow plan validate "$ROOT/plan.json" "$ROOT/policy.json" > "$ROOT/validate.json"
assert_success $? 'complete plan validates with structured coverage'
grep -q '"coverage":' "$ROOT/validate.json"
assert_success $? 'validation includes requirement coverage'
"$HYDRA_BIN" workflow plan compile "$ROOT/plan.json" "$ROOT/policy.json" "$ROOT/compiled.json" > "$ROOT/compile.json"
assert_success $? 'compile resolves a source and input bound artifact'
"$HYDRA_BIN" workflow plan compile "$ROOT/plan.json" "$ROOT/policy.json" "$ROOT/repeated.json" >/dev/null
cmp -s "$ROOT/compiled.json" "$ROOT/repeated.json"
assert_success $? 'identical inputs compile to identical bytes'
"$HYDRA_BIN" workflow plan show "$ROOT/compiled.json" > "$ROOT/preview.txt"
assert_success $? 'compiled plan has a readable scope preview'
sed 's/"inputs":{/"inputs":[],"invalid_inputs":{/' "$ROOT/compiled.json" > "$ROOT/bad-preview.json"
"$HYDRA_BIN" workflow plan show "$ROOT/bad-preview.json" > "$ROOT/bad-preview.out" 2>&1
assert_failure $? 'malformed compiled input declarations fail preview safely'
reject() {
    sed "$1" "$ROOT/plan.json" > "$ROOT/bad.json"
    "$HYDRA_BIN" workflow plan validate "$ROOT/bad.json" "$ROOT/policy.json" > "$ROOT/rejection.json" 2>&1
    assert_failure $? "$2"
    grep -q "\"code\":\"$3\"" "$ROOT/rejection.json"
    assert_success $? "$2 gives a structured diagnostic"
}
reject 's/"objective":/"unknown":/' 'unknown fields rejected' invalid_plan
reject 's/"criterion":/"unknown":/' 'missing acceptance criteria rejected' invalid_record
reject 's/"id": "verify"/"id": "compose"/' 'duplicate IDs rejected' duplicate_id
reject 's/"criterion": "[^"]*"/"criterion": ""/' 'empty criteria rejected' uncovered_requirement
reject 's/"check": "check"/"check": "absent"/' 'uncovered requirements rejected' uncovered_requirement
reject 's/"role": "compose"/"role": "work"/' 'final composition required' missing_composition
reject 's/"kind": "exec"/"kind": "remote"/' 'unsupported operations rejected' unsupported_operation
reject 's/"retry_budget": 0/"retry_budget": 1/' 'untrusted retry assertions rejected' invalid_policy
reject 's/"timeout_seconds": 180/"timeout_seconds": 1/' 'execution budgets enforced' over_budget
reject 's/"local"/"remote"/' 'remote placement rejected' unsupported_host
reject 's/"sh"/"unauthorized-tool"/' 'tool policy enforced' unauthorized
reject 's/^        "compose"$/        "absent-producer"/' 'missing dependencies rejected' missing_dependency
reject 's/"report.txt"/"..\/escape"/' 'unsafe artifact paths rejected' invalid_handoff
reject 's/"max_bytes": 1024/"max_bytes": 524288/' 'artifact budgets enforced' invalid_inputs_or_budget
printf '{"schema_version":1,"schema_version":1}\n' > "$ROOT/bad.json"
"$HYDRA_BIN" workflow plan validate "$ROOT/bad.json" "$ROOT/policy.json" > "$ROOT/rejection.json"
assert_failure $? 'duplicate JSON members rejected'
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$ROOT/compile.json")"
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept 0000000000000000000000000000000000000000000000000000000000000000 >/dev/null 2>&1
assert_failure $? 'execution rejects a different accepted digest'
printf 'changed\n' >> expected.txt
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept "$digest" >/dev/null 2>&1
assert_failure $? 'stale source and inputs rejected before execution'
git checkout -- expected.txt
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept "$digest" > "$ROOT/run.out" 2> "$ROOT/run.err"
assert_success $? 'accepted plan completes through the existing workflow runtime'
run="$(sed -n '1p' "$ROOT/run.out")"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
"$HYDRA_BIN" workflow status "$run" --json > "$ROOT/status.json"
grep -q '"state":"succeeded"' "$ROOT/status.json"
assert_success $? 'existing status command reports completed plan'
grep -q '"verdict":"pass"' "$run_dir/delivery.json"
assert_success $? 'delivery has passing artifact-bound assessment'
cmp -s "$run_dir/steps/compose/attempt-1/artifacts/report" expected.txt
assert_success $? 'delivered bytes match independently checked expected contents'
"$HYDRA_BIN" workflow plan result "$run" > "$ROOT/result.json"
assert_success $? 'public result command verifies and returns the final deliverable'
printf 'tampered\n' > "$run_dir/steps/compose/attempt-1/artifacts/report"
"$HYDRA_BIN" workflow plan result "$run" >/dev/null 2>&1
assert_failure $? 'result retrieval refuses corrupted sealed bytes despite recorded success'
cp expected.txt "$run_dir/steps/compose/attempt-1/artifacts/report"
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept "$digest" >/dev/null 2>&1
assert_failure $? 'new execution cannot reuse existing heads'
"$HYDRA_BIN" kill plan-smoke --force >/dev/null 2>&1
git branch -D plan-smoke >/dev/null 2>&1 || true
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept "$digest" > "$ROOT/durable.out" 2>/dev/null
assert_failure $? 'admission rejects retained durable head state after branch deletion'
if [ ! -s "$ROOT/durable.out" ]; then empty_status=0; else empty_status=1; fi
assert_success "$empty_status" 'durable head rejection creates no workflow run'
sed 's/"verdict":"pass"/"verdict":"fail"/' check.sh > "$ROOT/check.sh"
cp "$ROOT/check.sh" check.sh
git add check.sh && git commit -qm 'negative assessment fixture'
sed 's/plan-smoke/plan-negative/g' "$ROOT/plan.json" > "$ROOT/negative-plan.json"
"$HYDRA_BIN" workflow plan compile "$ROOT/negative-plan.json" "$ROOT/policy.json" "$ROOT/negative.json" > "$ROOT/compile.json"
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$ROOT/compile.json")"
"$HYDRA_BIN" workflow plan run "$ROOT/negative.json" --accept "$digest" > "$ROOT/negative.out" 2> "$ROOT/negative.err"
assert_failure $? 'zero-exit steps with a negative verdict do not count as delivery'
run="$(sed -n '1p' "$ROOT/negative.out")"
"$HYDRA_BIN" workflow status "$run" --json > "$ROOT/negative-status.json"
grep -q '"state":"failed"' "$ROOT/negative-status.json"
assert_success $? 'negative assessment makes the overall run fail'
grep -q '"step_id":"verify","state":"succeeded"' "$ROOT/negative-status.json"
assert_success $? 'negative verdict is evaluated after all child steps succeeded'
{
    printf '#!/bin/sh\nset -eu\n'
    printf 'touch "%s/compose-ready"\n' "$ROOT"
    printf 'while [ ! -f "%s/release" ]; do sleep 1; done\n' "$ROOT"
    # The generated script expands its own workflow environment at execution.
    # shellcheck disable=SC2016
    printf 'printf "Delivered report\\n" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"\n'
} > compose.sh
git add compose.sh && git commit -qm 'hold composition for projection integrity test'
for guard in graph limits; do
    rm -f "$ROOT/guard.json" "$ROOT/compose-ready" "$ROOT/release"
    sed "s/plan-smoke/plan-guard-$guard/g" "$ROOT/plan.json" > "$ROOT/guard-plan.json"
    "$HYDRA_BIN" workflow plan compile "$ROOT/guard-plan.json" "$ROOT/policy.json" "$ROOT/guard.json" > "$ROOT/compile.json"
    digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$ROOT/compile.json")"
    "$HYDRA_BIN" workflow plan run "$ROOT/guard.json" --accept "$digest" > "$ROOT/guard.out" 2> "$ROOT/guard.err" &
    guard_pid=$!
    tries=0
    while [ ! -f "$ROOT/compose-ready" ] && [ "$tries" -lt 30 ]; do sleep 1; tries=$((tries + 1)); done
    if [ -f "$ROOT/compose-ready" ]; then barrier_status=0; else barrier_status=1; fi
    assert_success "$barrier_status" "composition reached the live $guard test barrier"
    run="$(sed -n '1p' "$ROOT/guard.out")"
    run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
    if [ "$guard" = graph ]; then
        sed 's/check.sh/unauthorized.sh/' "$run_dir/graph.tsv" > "$ROOT/graph.tsv"
        cp "$ROOT/graph.tsv" "$run_dir/graph.tsv"
    else
        printf '0\n' > "$run_dir/disk-mb"
    fi
    touch "$ROOT/release"
    wait "$guard_pid"
    assert_failure $? "runtime refuses $guard projections changed after admission"
    assert_equal recovery-required "$(cat "$run_dir/steps/verify/state")" "changed $guard cannot dispatch verification"
done
printf '\nTests: %s; Passed: %s; Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
