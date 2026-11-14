#!/bin/sh
# Workflow run driver: scheduling decisions and recorded event order, terminal
# state reporting, and ownership of a live run.

set -u

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(test_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")" && cd "$test_dir" && pwd -P)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    test_stop_processes_naming "$test_root"
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _twd_worktree; do
            [ "$_twd_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_twd_worktree" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

run_dir_for() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$1" -print | sed -n '1p'
}

# The recorded order of transitions without timestamps: "type step detail".
event_trace() {
    sed -n 's/.*"step_id":\([^,]*\),"type":"\([^"]*\)","detail":"\([^"]*\)".*/\2 \1 \3/p' "$1/events.jsonl" |
        sed 's/ "\([^"]*\)" / \1 /; s/ null / - /; s/ $//'
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked.txt
git add tracked.txt
git commit -qm base
git branch -M main
"$HYDRA_BIN" init --no-agent --trust --write-shared-config >/dev/null
git add .hydra/config.yml
git commit -qm config
"$HYDRA_BIN" spawn driver-head --headless --no-agent >/dev/null
assert_success $? "fixture head spawns"

# A failure cascades through dependents in graph order: within one pass a
# cancellation is visible to every later step, so d is cancelled before e.
cat > "$test_root/cascade.yml" <<'EOF'
version: 1
id: driver-cascade
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: a
    kind: exec
    needs: []
    retry: 1
    idempotent: true
    args:
      head: driver-head
      argv: ["false"]
  - id: b
    kind: exec
    needs: [a]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: c
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: d
    kind: exec
    needs: [b]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: e
    kind: exec
    needs: [a]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: f
    kind: exec
    needs: [c]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: g
    kind: exec
    needs: [d, f]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
EOF
"$HYDRA_BIN" workflow run "$test_root/cascade.yml" > "$test_root/cascade.out" 2> "$test_root/cascade.err"
assert_failure $? "a failed step fails the run"
cascade_run="$(sed -n '1p' "$test_root/cascade.out")"
cascade_dir="$(run_dir_for "$cascade_run")"
assert_equal failed "$(sed -n '1p' "$cascade_dir/state")" "the run records a terminal failure"
expected_trace="run.created -
run.running -
step.ready a
step.ready c
step.running a attempt=1
step.retrying a attempt=1 class=failure
step.running a attempt=2
step.failed a attempt=2 class=failure
step.cancelled b dependency_failed
step.cancelled d dependency_failed
step.cancelled e dependency_failed
step.cancelled g dependency_failed
step.running c attempt=1
step.succeeded c attempt=1 class=success
step.ready f
step.running f attempt=1
step.succeeded f attempt=1 class=success
run.failed -"
assert_equal "$expected_trace" "$(event_trace "$cascade_dir")" "transitions are decided and recorded in graph order"
cascade_status="$("$HYDRA_BIN" workflow status "$cascade_run")"
assert_equal "Workflow $cascade_run: failed (source: recorded)
  a [exec]: failed (attempts=2)
  b [exec]: cancelled (attempts=0)
  c [exec]: succeeded (attempts=1)
  d [exec]: cancelled (attempts=0)
  e [exec]: cancelled (attempts=0)
  f [exec]: succeeded (attempts=1)
  g [exec]: cancelled (attempts=0)" "$cascade_status" "status reports every step in graph order"
assert_equal "{\"schema_version\":1,\"ok\":true,\"command\":\"workflow status\",\"data\":{\"run_id\":\"$cascade_run\",\"state\":\"failed\",\"source\":\"recorded\",\"evidence_expired\":false,\"steps\":[{\"step_id\":\"a\",\"state\":\"failed\",\"attempts\":2},{\"step_id\":\"b\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"c\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"d\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"e\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"f\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"g\",\"state\":\"cancelled\",\"attempts\":0}]}}" \
    "$("$HYDRA_BIN" workflow status "$cascade_run" --json)" "status JSON reports every step in graph order"

# Deciding the terminal state of a failed run reads every step state without
# leaving a reader to die on a closed pipe and report it on stderr.
{
    printf 'version: 1\nid: driver-fanout-failure\nparallelism: 1\nresources:\n  disk_mb: 1\n  max_heads: 1\nsteps:\n'
    printf '  - id: root\n    kind: exec\n    needs: []\n    retry: 0\n    idempotent: true\n    args:\n      head: driver-head\n      argv: ["false"]\n'
    for fanout_step in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
        printf '  - id: leaf-%s\n    kind: exec\n    needs: [root]\n    retry: 0\n    idempotent: true\n    args:\n      head: driver-head\n      argv: ["true"]\n' "$fanout_step"
    done
} > "$test_root/fanout.yml"
"$HYDRA_BIN" workflow run "$test_root/fanout.yml" > "$test_root/fanout.out" 2> "$test_root/fanout.err"
assert_failure $? "a failed root fails the fan-out run"
assert_equal failed "$(sed -n '1p' "$(run_dir_for "$(sed -n '1p' "$test_root/fanout.out")")/state")" "the fan-out run records a terminal failure"
assert_equal "" "$(cat "$test_root/fanout.err")" "a failed run reports no state-reader errors"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
