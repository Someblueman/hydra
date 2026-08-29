#!/bin/sh
# Durable lifecycle, adapter correlation, wait, and resume integration.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"

cleanup() {
    tmux kill-session -t lifecycle-test 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
git commit --allow-empty -qm init
mkdir -p .hydra/hooks
export HOOK_LOG="$test_root/teardown-hooks"
cat > .hydra/hooks/pre-teardown <<'EOF'
#!/bin/sh
printf 'pre %s %s\n' "$HYDRA_BRANCH" "$HYDRA_INSTANCE_ID" >> "$HOOK_LOG"
EOF
cat > .hydra/hooks/post-teardown <<'EOF'
#!/bin/sh
printf 'post %s %s\n' "$HYDRA_BRANCH" "$HYDRA_INSTANCE_ID" >> "$HOOK_LOG"
EOF

echo "Running lifecycle integration tests..."
echo "======================================"

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes"
"$HYDRA_BIN" spawn lifecycle-test --no-agent --prompt "Finish safely" >/dev/null
assert_success $? "task-aware head spawns"

project_id="$(sed -n '1p' .git/hydra/project-id)"
head_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch -exec sh -c '[ "$(sed -n "1p" "$1")" = lifecycle-test ] && dirname "$1"' sh {} \;)"
old_instance="$(sed -n '1p' "$head_dir/current-instance")"
observed_before="$(sed -n '1p' "$head_dir/instances/$old_instance/observed-status")"
printf '%s\n' 'not-json' | "$HYDRA_BIN" adapter ingest lifecycle-test >/dev/null 2>&1
assert_failure $? "malformed adapter input is rejected"
printf '{"schema_version":2,"instance_id":"%s","kind":"observed","status":"idle"}\n' "$old_instance" | \
    "$HYDRA_BIN" adapter ingest lifecycle-test >/dev/null 2>&1
assert_failure $? "adapter schema version skew is rejected"
assert_equal "$observed_before" "$(sed -n '1p' "$head_dir/instances/$old_instance/observed-status")" "missed or rejected adapter events leave deterministic fallback state"
lifecycle_json="$("$HYDRA_BIN" lifecycle lifecycle-test --json)"
printf '%s' "$lifecycle_json" | grep -q '^{' && printf '%s' "$lifecycle_json" | grep -q '"schema_version":1'
assert_success $? "lifecycle emits the versioned JSON envelope"
assert_equal "$old_instance" "$(printf '%s' "$lifecycle_json" | sed -n 's/.*"instance_id":"\([^"]*\)".*/\1/p')" "lifecycle reports current instance"

printf '{"schema_version":1,"instance_id":"%s","kind":"observed","status":"idle"}\n' "$old_instance" | \
    "$HYDRA_BIN" adapter ingest lifecycle-test >/dev/null
assert_success $? "canonical adapter observation is accepted"
assert_equal idle "$(sed -n '1p' "$head_dir/instances/$old_instance/observed-status")" "adapter updates observed status"

"$HYDRA_BIN" send --type handoff --delivery safe-point lifecycle-test "Implementation ready for declared outcome" >/dev/null
assert_success $? "typed handoff is queued without pane injection"
receipts_json="$("$HYDRA_BIN" recv --receipts lifecycle-test --json)"
case "$receipts_json" in *'"command":"receipts"'*'"status":"queued"'*) assert_success 0 "handoff receipt is correlated to the active instance" ;; *) assert_success 1 "handoff receipt is correlated to the active instance" ;; esac

"$HYDRA_BIN" notify enable lifecycle.declared --sink terminal --interval 60 >/dev/null
assert_success $? "local lifecycle notification is configured"
notify_output="$("$HYDRA_BIN" outcome lifecycle-test "done" --actor agent --summary "tests passed" 2>&1)"
assert_success $? "current instance declares an outcome"
case "$notify_output" in *'[notify]'*) assert_success 0 "named lifecycle event reaches the local sink" ;; *) assert_success 1 "named lifecycle event reaches the local sink" ;; esac
notify_repeat="$("$HYDRA_BIN" outcome lifecycle-test "done" --actor agent 2>&1)"
case "$notify_repeat" in *'[notify]'*) assert_success 1 "notification sink is rate limited" ;; *) assert_success 0 "notification sink is rate limited" ;; esac
"$HYDRA_BIN" wait lifecycle-test --for outcome=done --timeout 1 >/dev/null
assert_success $? "durable wait observes declared outcome"

tmux send-keys -t lifecycle-test:0.0 'echo API_TOKEN=supersecret' Enter
sleep 1
"$HYDRA_BIN" kill lifecycle-test --transcript redacted >/dev/null
assert_success $? "head tears down"
transcript="$head_dir/transcripts/$old_instance.txt"
test -f "$transcript" && grep -q 'API_TOKEN=\[REDACTED\]' "$transcript" && ! grep -q supersecret "$transcript"
assert_success $? "opt-in transcript is bounded and redacted"
assert_equal 600 "$(stat -f '%Lp' "$transcript" 2>/dev/null || stat -c '%a' "$transcript")" "transcript permissions are private"
grep -q "pre lifecycle-test $old_instance" "$HOOK_LOG" && grep -q "post lifecycle-test $old_instance" "$HOOK_LOG"
assert_success $? "trusted pre/post teardown hooks receive instance identity"
wait_json_file="$test_root/wait-instance-change.json"
wait_code_file="$test_root/wait-instance-change.code"
(
    "$HYDRA_BIN" wait lifecycle-test --for observed=idle --timeout 10 --json > "$wait_json_file" 2>/dev/null
    printf '%s\n' "$?" > "$wait_code_file"
) &
wait_pid=$!
sleep 1
"$HYDRA_BIN" resume lifecycle-test >/dev/null
assert_success $? "head resumes from durable metadata"
wait "$wait_pid"
assert_equal 3 "$(sed -n '1p' "$wait_code_file")" "JSON wait detects current-instance replacement"
case "$(sed -n '1p' "$wait_json_file")" in
    *'"schema_version":1'*'"ok":false'*'"code":"instance_changed"'*) assert_success 0 "instance-change wait failure uses JSON v1" ;;
    *) assert_success 1 "instance-change wait failure uses JSON v1" ;;
esac
new_instance="$(sed -n '1p' "$head_dir/current-instance")"
if [ "$new_instance" != "$old_instance" ]; then
    assert_success 0 "resume creates a new instance"
else
    assert_success 1 "resume creates a new instance"
fi
assert_equal "$new_instance" "$(sed -n '1p' "$head_dir/instances/$old_instance/superseded-by")" "old instance retains successor history"

printf '{"schema_version":1,"instance_id":"%s","kind":"outcome","status":"done"}\n' "$old_instance" | \
    "$HYDRA_BIN" adapter ingest lifecycle-test >/dev/null 2>&1
assert_failure $? "stale prior-instance adapter event is rejected"
"$HYDRA_BIN" wait lifecycle-test --for outcome=done --timeout 0 >/dev/null 2>&1
assert_equal 2 "$?" "prior outcome cannot satisfy the resumed instance"

event_file="$head_dir/events/events.jsonl"
grep -q '"type":"lifecycle.resumed"' "$event_file"
assert_success $? "resume is present in event history"
grep -q "\"instance_id\":\"$old_instance\"" "$event_file" && grep -q "\"instance_id\":\"$new_instance\"" "$event_file"
assert_success $? "event history correlates both instances"

hook_lines_before="$(wc -l < "$HOOK_LOG" | tr -d ' ')"
printf '%s\n' '# changed after trust' >> .hydra/hooks/pre-teardown
teardown_output="$("$HYDRA_BIN" kill lifecycle-test 2>&1)"
assert_success $? "resumed head tears down"
case "$teardown_output" in *"skipped untrusted repository hook"*) assert_success 0 "changed hook content invalidates trust" ;; *) assert_success 1 "changed hook content invalidates trust" ;; esac
assert_equal "$hook_lines_before" "$(wc -l < "$HOOK_LOG" | tr -d ' ')" "untrusted teardown hooks do not execute"
if [ ! -f "$head_dir/transcripts/$new_instance.txt" ]; then
    assert_success 0 "default teardown persists no transcript"
else
    assert_success 1 "default teardown persists no transcript"
fi

stable_instance="$(sed -n '1p' "$head_dir/current-instance")"
stable_ended="$(sed -n '1p' "$head_dir/instances/$stable_instance/ended-at" 2>/dev/null || true)"
try_lock state_map "test resume mapping failure"
HYDRA_LOCK_RETRIES=1 "$HYDRA_BIN" resume lifecycle-test >/dev/null 2>&1
assert_failure $? "resume fails when compatibility mapping cannot commit"
release_lock state_map
assert_equal "$stable_instance" "$(sed -n '1p' "$head_dir/current-instance")" "failed resume restores the prior current instance"
assert_equal "$stable_ended" "$(sed -n '1p' "$head_dir/instances/$stable_instance/ended-at" 2>/dev/null || true)" "failed resume restores prior lifecycle metadata"
if [ ! -f "$head_dir/instances/$stable_instance/superseded-by" ] && ! tmux has-session -t lifecycle-test 2>/dev/null; then
    assert_success 0 "failed resume leaves no ghost successor or session"
else
    assert_success 1 "failed resume leaves no ghost successor or session"
fi

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
