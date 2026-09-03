#!/bin/sh
# Durable static-DAG execution, retry, cancellation, and recovery acceptance.

set -u

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    for _twr_session in workflow-a workflow-b; do
        tmux kill-session -t "$_twr_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _twr_worktree; do
            [ "$_twr_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_twr_worktree" 2>/dev/null || true
        done
    fi
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then
        echo "Preserved workflow runtime fixture: $test_root" >&2
    else
        rm -rf "$test_root"
    fi
}
trap cleanup EXIT HUP INT TERM

wait_for_file() {
    _twf_file="$1"
    _twf_count=0
    while [ ! -f "$_twf_file" ] && [ "$_twf_count" -lt 100 ]; do
        sleep 0.1
        _twf_count=$((_twf_count + 1))
    done
    [ -f "$_twf_file" ]
}

run_dir_for() {
    _trf_run="$1"
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$_trf_run" -print | sed -n '1p'
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
"$HYDRA_BIN" init --no-agent --trust >/dev/null
git add .hydra/config.yml
git commit -qm config

"$HYDRA_BIN" workflow status ../config.yml >/dev/null 2>&1
assert_failure $? "workflow commands reject traversing run IDs"

cat > "$test_root/barrier.sh" <<'EOF'
#!/bin/sh
set -eu
dir="$1"
mine="$2"
other="$3"
: > "$dir/$mine"
count=0
while [ ! -f "$dir/$other" ] && [ "$count" -lt 50 ]; do
    sleep 0.1
    count=$((count + 1))
done
[ -f "$dir/$other" ]
EOF
chmod +x "$test_root/barrier.sh"

cat > "$test_root/parallel.yml" <<EOF
version: 1
id: parallel-runtime
parallelism: 2
resources:
  disk_mb: 1
  max_heads: 2
steps:
  - id: spawn-a
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: workflow-a
      group: workflow-runtime
  - id: spawn-b
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: workflow-b
      group: workflow-runtime
  - id: barrier-a
    kind: exec
    needs: [spawn-a, spawn-b]
    retry: 0
    idempotent: true
    args:
      head: workflow-a
      argv: [sh, $test_root/barrier.sh, $test_root, barrier-a, barrier-b]
  - id: barrier-b
    kind: exec
    needs: [spawn-a, spawn-b]
    retry: 0
    idempotent: true
    args:
      head: workflow-b
      argv: [sh, $test_root/barrier.sh, $test_root, barrier-b, barrier-a]
  - id: join
    kind: exec
    needs: [barrier-a, barrier-b]
    retry: 0
    idempotent: true
    args:
      head: workflow-a
      argv: [true]
EOF

"$HYDRA_BIN" workflow run "$test_root/parallel.yml" > "$test_root/parallel.out"
assert_success $? "static DAG executes successfully"
parallel_run="$(sed -n '1p' "$test_root/parallel.out")"
parallel_dir="$(run_dir_for "$parallel_run")"
assert_equal succeeded "$(sed -n '1p' "$parallel_dir/state")" "run records a terminal success"
parallel_json="$("$HYDRA_BIN" workflow status "$parallel_run" --json)"
case "$parallel_json" in
    *'"schema_version":1'*'"ok":true'*'"command":"workflow status"'*'"data":{'*)
        assert_success 0 "workflow status uses the public JSON envelope" ;;
    *) assert_success 1 "workflow status uses the public JSON envelope" ;;
esac
assert_equal 1 "$(sed -n '1p' "$parallel_dir/steps/barrier-a/attempts")" "first fan-out step runs once"
assert_equal 1 "$(sed -n '1p' "$parallel_dir/steps/barrier-b/attempts")" "second fan-out step runs once"
if [ -f "$test_root/barrier-a" ] && [ -f "$test_root/barrier-b" ]; then parallel_status=0; else parallel_status=1; fi
assert_success "$parallel_status" "parallelism permits mutually synchronized fan-out steps"
awk -F '"sequence":|,' 'NF > 1 { print $2 }' "$parallel_dir/events.jsonl" | LC_ALL=C sort -n | uniq -d | grep . >/dev/null 2>&1
assert_failure $? "concurrent events retain unique sequence numbers"
grep -q '"run_id":"' "$parallel_dir/events.jsonl" && grep -q '"step_id":"barrier-a"' "$parallel_dir/events.jsonl"
assert_success $? "events correlate run and step identities"
if [ -s "$parallel_dir/manifest.tsv" ] && [ -s "$parallel_dir/resolved.yml" ] && [ -s "$parallel_dir/graph.tsv" ]; then manifest_status=0; else manifest_status=1; fi
assert_success "$manifest_status" "resolved workflow and manifest exist before execution evidence"

sleep 30 &
terminal_owner=$!
printf '%s\n' "$terminal_owner" > "$parallel_dir/owner-pid"
"$HYDRA_BIN" workflow cancel "$parallel_run" >/dev/null 2>&1
assert_failure $? "terminal workflow refuses cancellation"
kill -0 "$terminal_owner" 2>/dev/null
assert_success $? "terminal workflow cancellation does not signal a reused owner PID"
kill "$terminal_owner" 2>/dev/null || true
wait "$terminal_owner" 2>/dev/null || true

cat > "$test_root/glob-message.yml" <<'EOF'
version: 1
id: literal-glob-message
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: send-literal
    kind: message
    needs: []
    retry: 0
    idempotent: false
    args:
      head: workflow-a
      message: '*'
EOF
"$HYDRA_BIN" workflow run "$test_root/glob-message.yml" >/dev/null
assert_success $? "runtime decoding preserves a glob scalar"
glob_message="$(find "$HYDRA_HOME/state/v2/projects" -path '*/messages/queue/*' -type f | sed -n '1p')"
assert_equal "*" "$(sed -n '1p' "$glob_message")" "runtime dispatches the literal glob without pathname expansion"

cat > "$test_root/retry.sh" <<'EOF'
#!/bin/sh
set -eu
count_file="$1"
count="$(sed -n '1p' "$count_file" 2>/dev/null || printf 0)"
count=$((count + 1))
printf '%s\n' "$count" > "$count_file"
[ "$count" -ge 2 ]
EOF
chmod +x "$test_root/retry.sh"
cat > "$test_root/retry.yml" <<EOF
version: 1
id: bounded-retry
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: retry
    kind: exec
    needs: []
    retry: 1
    idempotent: true
    args:
      head: workflow-a
      argv: [sh, $test_root/retry.sh, $test_root/retry-count]
EOF
"$HYDRA_BIN" workflow run "$test_root/retry.yml" > "$test_root/retry.out"
assert_success $? "idempotent failure retries within its declared bound"
retry_run="$(sed -n '1p' "$test_root/retry.out")"
retry_dir="$(run_dir_for "$retry_run")"
assert_equal 2 "$(sed -n '1p' "$retry_dir/steps/retry/attempts")" "retry count is authoritative"
assert_equal 2 "$(sed -n '1p' "$test_root/retry-count")" "retry side effect ran exactly twice"

cat > "$test_root/long.sh" <<'EOF'
#!/bin/sh
set -eu
started="$1"
completed="$2"
: > "$started"
trap 'exit 143' HUP INT TERM
sleep 30
: > "$completed"
EOF
chmod +x "$test_root/long.sh"
cat > "$test_root/cancel.yml" <<EOF
version: 1
id: cancellation
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: long
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: workflow-a
      argv: [sh, $test_root/long.sh, $test_root/cancel-started, $test_root/cancel-completed]
EOF
"$HYDRA_BIN" workflow run "$test_root/cancel.yml" > "$test_root/cancel.out" 2>&1 &
cancel_runner=$!
wait_for_file "$test_root/cancel-started" || exit 1
cancel_run="$(sed -n '1p' "$test_root/cancel.out")"
cancel_dir="$(run_dir_for "$cancel_run")"
"$HYDRA_BIN" workflow cancel "$cancel_run" > "$test_root/cancel-command.out"
assert_success $? "cancellation reaches an active workflow owner"
wait "$cancel_runner" 2>/dev/null || true
assert_equal cancelled "$(sed -n '1p' "$cancel_dir/state")" "cancelled run records a terminal state"
if [ ! -s "$cancel_dir/residual-children.tsv" ] && [ ! -f "$test_root/cancel-completed" ]; then cancel_status=0; else cancel_status=1; fi
assert_success "$cancel_status" "cancellation leaves no reported or completed child command"

cat > "$test_root/gate-tree.sh" <<'EOF'
#!/bin/sh
started="$1"
child_file="$2"
sleep 30 &
child=$!
printf '%s\n' "$child" > "$child_file"
: > "$started"
wait "$child"
EOF
chmod +x "$test_root/gate-tree.sh"
cat > "$test_root/gate-cancel.yml" <<EOF
version: 1
id: gate-tree-cancellation
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: gate-tree
    kind: gate
    needs: []
    retry: 0
    idempotent: true
    args:
      head: workflow-a
      name: cancellation-tree
      argv: [sh, $test_root/gate-tree.sh, $test_root/gate-started, $test_root/gate-child-pid]
EOF
"$HYDRA_BIN" workflow run "$test_root/gate-cancel.yml" > "$test_root/gate-cancel.out" 2>&1 &
gate_runner=$!
wait_for_file "$test_root/gate-started" || exit 1
gate_run="$(sed -n '1p' "$test_root/gate-cancel.out")"
"$HYDRA_BIN" workflow cancel "$gate_run" >/dev/null
assert_success $? "gate workflow cancellation reaches the command tree"
wait "$gate_runner" 2>/dev/null || true
gate_child="$(sed -n '1p' "$test_root/gate-child-pid")"
gate_wait=0
while kill -0 "$gate_child" 2>/dev/null && [ "$gate_wait" -lt 50 ]; do
    sleep 0.1
    gate_wait=$((gate_wait + 1))
done
kill -0 "$gate_child" 2>/dev/null
assert_failure $? "gate workflow cancellation leaves no descendant process"

cat > "$test_root/effect.sh" <<'EOF'
#!/bin/sh
set -eu
count_file="$1"
count="$(sed -n '1p' "$count_file" 2>/dev/null || printf 0)"
printf '%s\n' "$((count + 1))" > "$count_file"
EOF
chmod +x "$test_root/effect.sh"
cat > "$test_root/resumable.sh" <<'EOF'
#!/bin/sh
set -eu
count_file="$1"
started="$2"
count="$(sed -n '1p' "$count_file" 2>/dev/null || printf 0)"
count=$((count + 1))
printf '%s\n' "$count" > "$count_file"
if [ "$count" -eq 1 ]; then
    : > "$started"
    trap 'exit 143' HUP INT TERM
    sleep 30
fi
EOF
chmod +x "$test_root/resumable.sh"
cat > "$test_root/resume.yml" <<EOF
version: 1
id: interrupted-resume
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: effect
    kind: exec
    needs: []
    retry: 0
    idempotent: false
    args:
      head: workflow-a
      argv: [sh, $test_root/effect.sh, $test_root/effect-count]
  - id: resumable
    kind: exec
    needs: [effect]
    retry: 1
    idempotent: true
    args:
      head: workflow-a
      argv: [sh, $test_root/resumable.sh, $test_root/resume-count, $test_root/resume-started]
EOF
"$HYDRA_BIN" workflow run "$test_root/resume.yml" > "$test_root/resume.out" 2>&1 &
resume_runner=$!
wait_for_file "$test_root/resume-started" || exit 1
resume_run="$(sed -n '1p' "$test_root/resume.out")"
resume_dir="$(run_dir_for "$resume_run")"
kill -KILL "$resume_runner" 2>/dev/null || true
resume_worker="$(sed -n '1p' "$resume_dir/steps/resumable/worker-pid")"
resume_command="$(sed -n '1p' "$resume_dir/steps/resumable/command-pid")"
kill -TERM "$resume_command" "$resume_worker" 2>/dev/null || true
resume_wait=0
while { kill -0 "$resume_command" 2>/dev/null || kill -0 "$resume_worker" 2>/dev/null; } && [ "$resume_wait" -lt 50 ]; do
    sleep 0.1
    resume_wait=$((resume_wait + 1))
done
resume_base="$(git rev-parse HEAD)"
git commit --allow-empty -qm advanced-after-interruption
"$HYDRA_BIN" workflow resume "$resume_run" >/dev/null 2>&1
assert_failure $? "resume rejects a checkout advanced beyond the recorded base"
git switch --detach -q "$resume_base"
"$HYDRA_BIN" workflow resume "$resume_run" >/dev/null
assert_success $? "stale run resumes its retryable interrupted step"
assert_equal succeeded "$(sed -n '1p' "$resume_dir/state")" "resumed run reaches success"
assert_equal 1 "$(sed -n '1p' "$test_root/effect-count")" "resume never repeats a completed non-idempotent effect"
assert_equal 2 "$(sed -n '1p' "$test_root/resume-count")" "interrupted idempotent attempt retries once"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
