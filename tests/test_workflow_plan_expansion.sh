#!/bin/sh
# Bounded expansion of a finished plan run through the public CLI: admission
# under the base policy, exact-digest execution of each round, retry versus
# new-claim classification, failed branches, exhausted budgets, unknown
# outcomes, changed rounds and replayable chain views.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
export HYDRA_TEST_ROOT="$REPO"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$ROOT/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$REPO/tests/helpers.sh"
# shellcheck source=/dev/null
. "$REPO/tests/tmux_fixture_cleanup.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    for head in plan-smoke plan-expansion-1 plan-expansion-2 plan-expansion-3 plan-expansion-4; do
        (cd "$ROOT/repo" && "$HYDRA_BIN" kill "$head" --force >/dev/null 2>&1) || true
    done
    test_tmux_fixture_cleanup "$ROOT" || return 1
    if [ "$fail_count" -ne 0 ]; then printf 'Failure evidence: %s\n' "$ROOT"; return; fi
    rm -rf "$ROOT"
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
HYDRA_PLAN_TEST_TMUX="$(command -v tmux)" || exit 1
HYDRA_PLAN_TEST_TMUX_SOCKET="$ROOT/tmux.sock"
export HYDRA_PLAN_TEST_TMUX HYDRA_PLAN_TEST_TMUX_SOCKET
mkdir "$ROOT/bin" || exit 1
cat > "$ROOT/bin/tmux" <<'EOF'
#!/bin/sh
exec "$HYDRA_PLAN_TEST_TMUX" -S "$HYDRA_PLAN_TEST_TMUX_SOCKET" -f /dev/null "$@"
EOF
chmod +x "$ROOT/bin/tmux" || exit 1
PATH="$ROOT/bin:$PATH"
export PATH
mkdir "$ROOT/repo"
cp "$REPO/tests/fixtures/plan/repo/"* "$ROOT/repo/"
# Everything an expansion runs is in the source at the base commit: an
# expansion extends the same bound source and never changes it.
cat > "$ROOT/repo/expansion-compose.sh" <<'EOF'
#!/bin/sh
set -eu
cp "$HYDRA_WORKFLOW_INPUTS_DIR/base" "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt"
EOF
cat > "$ROOT/repo/expansion-check.sh" <<'EOF'
#!/bin/sh
set -eu
cmp "$HYDRA_WORKFLOW_INPUTS_DIR/subject" "$HYDRA_WORKFLOW_INPUTS_DIR/expected"
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
printf '{"schema_version":1,"verdict":"pass","subject_sha256":"%s","requirements":["%s"],"evidence":"Compared with the sealed deliverable of an earlier version"}\n' "$hash" "$1" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
EOF
printf '#!/bin/sh\nexit 1\n' > "$ROOT/repo/fail.sh"
cp "$REPO/tests/fixtures/plan/plan.json" "$ROOT/plan.json"
cat > "$ROOT/policy.json" <<'EOF'
{"schema_version": 1,
 "expansion": {"max_nodes": 16, "max_work_seconds": 600, "max_artifact_bytes": 65536, "max_rounds": 4, "max_repairs_per_claim": 1},
 "envelope": {"hosts": ["local"], "tools": ["sh"], "effects": ["worktree", "execute"], "writes": [], "parallelism": 1,
  "timeout_seconds": 180, "artifact_bytes": 4096, "max_heads": 1, "disk_mb": 1, "retry_budget": 0, "repair_budget": 0}}
EOF
cd "$ROOT/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.com
git add . && git commit -qm fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
# First occurrence on the line: the envelope's own digest or error code.
sha_of() { grep -o '"sha256":"[a-f0-9]*"' "$1" | sed -n '1p' | sed 's/.*://; s/"//g'; }
code_of() { grep -o '"code":"[a-z_]*"' "$1" | sed -n '1p' | sed 's/.*://; s/"//g'; }
run_dir_of() { find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$1" -print; }

"$HYDRA_BIN" workflow plan validate "$ROOT/plan.json" "$ROOT/policy.json" > "$ROOT/validate.json"
assert_success $? 'a policy with expansion bounds validates the base plan'
sed 's/"max_rounds": 4/"max_rounds": 0/' "$ROOT/policy.json" > "$ROOT/bad-policy.json"
"$HYDRA_BIN" workflow plan validate "$ROOT/plan.json" "$ROOT/bad-policy.json" > "$ROOT/bad-validate.json" 2>&1
assert_failure $? 'malformed expansion bounds are refused at validation'
grep -q '"code":"invalid_policy"' "$ROOT/bad-validate.json"
assert_success $? 'the bound refusal is a typed policy diagnostic'
"$HYDRA_BIN" workflow plan compile "$ROOT/plan.json" "$ROOT/policy.json" "$ROOT/compiled.json" > "$ROOT/compile.json"
assert_success $? 'base plan compiles with its expansion bounds in the policy'
base_sha="$(sha_of "$ROOT/compile.json")"
"$HYDRA_BIN" workflow plan run "$ROOT/compiled.json" --accept "$base_sha" > "$ROOT/run.out" 2> "$ROOT/run.err"
assert_success $? 'base run completes through the existing workflow runtime'
run="$(sed -n '1p' "$ROOT/run.out")"
run_dir="$(run_dir_of "$run")"
grep -q '"verdict":"pass"' "$run_dir/delivery.json"
assert_success $? 'base delivery passes its artifact-bound check'

"$HYDRA_BIN" workflow plan expand plan-smoke --run "$run" > "$ROOT/empty.txt"
assert_success $? 'an unexpanded run has a readable empty chain'
grep -q "^Expansions of run $run: 0 admitted, integrity intact" "$ROOT/empty.txt"
assert_success $? 'the empty chain names the base run and its integrity'
"$HYDRA_BIN" workflow plan expand plan-smoke --run "$run" --json > "$ROOT/empty.json"
assert_success $? 'the empty chain is available as JSON'
grep -q "\"base_sha256\":\"$base_sha\",\"latest_sha256\":\"$base_sha\",\"rounds\":0,\"integrity\":\"intact\"" "$ROOT/empty.json"
assert_success $? 'the JSON chain binds the base digest as the latest version'
"$HYDRA_BIN" workflow plan expand plan-smoke --run run_00000000000000000000 > "$ROOT/absent.out" 2>&1
assert_failure $? 'expanding an unknown run is refused'
"$HYDRA_BIN" workflow plan expand plan-smoke --run "$run" --proposal "$ROOT/missing.json" > "$ROOT/missing.out" 2>&1
assert_failure $? 'a missing proposal file is refused'

# shellcheck source=/dev/null
. "$REPO/tests/workflow_plan_expansion_cases.sh"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
