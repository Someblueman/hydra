#!/bin/sh
# Durable approval waits through the public CLI, with no live AI or remote host.
set -u
HYDRA_BIN="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)/bin/hydra"
ROOT="$(mktemp -d)"
export HYDRA_HOME="$ROOT/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    canonical_root="$(cd "$ROOT" && pwd -P)"
    tmux list-sessions -F '#{session_name}|#{session_path}' 2>/dev/null | while IFS='|' read -r test_session test_path; do
        case "$test_path" in "$canonical_root"/*|"$ROOT"/*) tmux kill-session -t "$test_session" 2>/dev/null || true ;; esac
    done
    (cd "$ROOT/repo" && "$HYDRA_BIN" kill approval-worker --force >/dev/null 2>&1) || true
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then printf 'Preserved approval fixture: %s\n' "$ROOT" >&2; else rm -rf "$ROOT"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$ROOT/repo"
cd "$ROOT/repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked
git add tracked && git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" spawn approval-worker --no-agent >/dev/null
worker="$("$HYDRA_BIN" path approval-worker)"
cat > "$ROOT/effect.sh" <<'SCRIPT'
#!/bin/sh
printf '%s\n' "$2" >> "$1"
SCRIPT
cat > "$ROOT/flow.yml" <<EOF2
version: 1
id: approval-wait
resources:
  disk_mb: 1
steps:
  - id: before
    kind: exec
    idempotent: false
    args:
      head: approval-worker
      argv: [sh, $ROOT/effect.sh, $ROOT/effects, before]
  - id: approval
    kind: approval-wait
    needs: [before]
    idempotent: false
    args:
      head: approval-worker
      name: review
      message: Review the evidence before continuing
  - id: after
    kind: exec
    needs: [approval]
    idempotent: false
    args:
      head: approval-worker
      argv: [sh, $ROOT/effect.sh, $ROOT/effects, after]
EOF2
start_wait() {
    "$HYDRA_BIN" workflow run "$ROOT/flow.yml" > "$ROOT/run.out" 2> "$ROOT/run.err"
    run_code=$?
    run="$(sed -n '1p' "$ROOT/run.out")"
    run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
    request="$(cat "$run_dir/steps/approval/request-id" 2>/dev/null)"
    if [ -z "$request" ]; then cat "$ROOT/run.err" >&2; exit 1; fi
}
start_wait
assert_equal 3 "$run_code" "workflow exits with explicit suspended status"
assert_equal waiting-approval "$(cat "$run_dir/state")" "approval wait is durable after coordinator exit"
assert_equal before "$(cat "$ROOT/effects")" "dependent side effect waits for a decision"
"$HYDRA_BIN" workflow requests "$run" --json > "$ROOT/requests.json"
grep -q "$request" "$ROOT/requests.json"
assert_success $? "requests exposes the exact request identity"
"$HYDRA_BIN" workflow decide "$run" "$request" approve --by test-reviewer >/dev/null
assert_success $? "local operator records a bound decision"
assert_equal before "$(cat "$ROOT/effects")" "recording a decision never resumes automatically"
assert_equal local-cli "$(cat "$run_dir/approvals/$request/decision/source")" "decision source is separate from its supplied actor label"
assert_equal "$(id -u)" "$(cat "$run_dir/approvals/$request/decision/principal-uid")" "decision records the local OS principal"
"$HYDRA_BIN" workflow resume "$run" >/dev/null
assert_success $? "explicit resume applies the surviving decision"
assert_equal succeeded "$(cat "$run_dir/state")" "resumed workflow completes"
# shellcheck source=/dev/null
. "$(dirname "$HYDRA_BIN")/../tests/fixture-tools.sh"
statistics_evidence "$HYDRA_BIN" "$run_dir" 0 unverified
assert_success $? "approval continuation is excluded from owner recovery and queue samples"
assert_equal 1 "$(grep -c '^before$' "$ROOT/effects")" "resume preserves the completed non-idempotent attempt"
assert_equal 1 "$(grep -c '^after$' "$ROOT/effects")" "approved action executes once"

start_wait
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null
printf 'changed content\n' >> "$worker/tracked"
"$HYDRA_BIN" workflow resume "$run" >/dev/null
assert_equal 3 "$?" "changed code evidence requires a fresh approval"
assert_equal stale "$(cat "$run_dir/approvals/$request/state")" "old decision remains inspectably stale"
new_request="$(cat "$run_dir/steps/approval/request-id")"
if [ "$new_request" != "$request" ]; then changed=0; else changed=1; fi
assert_success "$changed" "refresh creates a distinct request identity"
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null 2>&1
assert_failure $? "stale request ID cannot authorize changed work"
printf 'second content change\n' >> "$worker/tracked"
"$HYDRA_BIN" workflow decide "$run" "$new_request" approve >/dev/null 2>&1
assert_failure $? "content changes with identical git status still invalidate a request"
"$HYDRA_BIN" workflow resume "$run" >/dev/null
request="$(cat "$run_dir/steps/approval/request-id")"
"$HYDRA_BIN" workflow decide "$run" "$request" reject >/dev/null
"$HYDRA_BIN" workflow resume "$run" >/dev/null
assert_failure $? "rejection fails the run and blocks dependents"
assert_equal 1 "$(grep -c '^after$' "$ROOT/effects")" "rejected work never runs"

start_wait
"$HYDRA_BIN" workflow cancel "$run" >/dev/null
assert_success $? "suspended workflow can be cancelled without an active coordinator"
assert_equal cancelled "$(cat "$run_dir/state")" "cancellation terminates a pending approval wait"
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null 2>&1
assert_failure $? "cancelled approval cannot release work"

# Expiry survives process exit and an old decision cannot outlive it.
cp "$ROOT/flow.yml" "$ROOT/no-expiry.yml"
sed '/name: review/a\
      timeout: 2
' "$ROOT/no-expiry.yml" > "$ROOT/flow.yml"
start_wait
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null
sleep 2
"$HYDRA_BIN" workflow resume "$run" >/dev/null
assert_equal 3 "$?" "expired decision cannot resume its protected action"
assert_equal expired "$(cat "$run_dir/approvals/$request/state")" "expired request remains recorded"
"$HYDRA_BIN" workflow cancel "$run" >/dev/null
cp "$ROOT/no-expiry.yml" "$ROOT/flow.yml"

start_wait
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null 2>&1 &
first_decider=$!
"$HYDRA_BIN" workflow decide "$run" "$request" reject >/dev/null 2>&1 &
second_decider=$!
wait "$first_decider"; first_code=$?
wait "$second_decider"; second_code=$?
if [ "$first_code" -eq 0 ] && [ "$second_code" -ne 0 ]; then exclusive=0
elif [ "$first_code" -ne 0 ] && [ "$second_code" -eq 0 ]; then exclusive=0
else exclusive=1; fi
assert_success "$exclusive" "concurrent conflicting decisions cannot both succeed"
"$HYDRA_BIN" workflow cancel "$run" >/dev/null

start_wait
cp "$run_dir/graph.tsv" "$ROOT/original-graph.tsv"
printf 'retry_policy\tbefore\tfailure\t1\n' >> "$run_dir/graph.tsv"
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null 2>&1
assert_failure $? "modified execution graph invalidates approval"
"$HYDRA_BIN" workflow resume "$run" >/dev/null 2>&1
assert_failure $? "resume rejects an execution graph that differs from the definition"
cp "$ROOT/original-graph.tsv" "$run_dir/graph.tsv"

"$HYDRA_BIN" workflow cancel "$run" >/dev/null
start_wait
"$HYDRA_BIN" workflow decide "$run" "$request" approve >/dev/null
"$HYDRA_BIN" send --delivery safe-point approval-worker 'changed protected prompt' >/dev/null
"$HYDRA_BIN" workflow resume "$run" >/dev/null
assert_equal 3 "$?" "queued prompt steering invalidates an existing approval"
assert_equal stale "$(cat "$run_dir/approvals/$request/state")" "steering changes preserve the stale decision evidence"
"$HYDRA_BIN" workflow cancel "$run" >/dev/null

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
