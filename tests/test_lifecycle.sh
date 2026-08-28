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
lifecycle_json="$("$HYDRA_BIN" lifecycle lifecycle-test --json)"
printf '%s' "$lifecycle_json" | grep -q '^{' && printf '%s' "$lifecycle_json" | grep -q '"schema_version":1'
assert_success $? "lifecycle emits the versioned JSON envelope"
assert_equal "$old_instance" "$(printf '%s' "$lifecycle_json" | sed -n 's/.*"instance_id":"\([^"]*\)".*/\1/p')" "lifecycle reports current instance"

printf '{"schema_version":1,"instance_id":"%s","kind":"observed","status":"idle"}\n' "$old_instance" | \
    "$HYDRA_BIN" adapter ingest lifecycle-test >/dev/null
assert_success $? "canonical adapter observation is accepted"
assert_equal idle "$(sed -n '1p' "$head_dir/instances/$old_instance/observed-status")" "adapter updates observed status"

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
"$HYDRA_BIN" resume lifecycle-test >/dev/null
assert_success $? "head resumes from durable metadata"
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

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
