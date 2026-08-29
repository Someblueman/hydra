#!/bin/sh
# Agent profile and safe task-injection tests.

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
HYDRA_HOME="$test_root/home"
HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
export HYDRA_HOME HYDRA_LIB_DIR
mkdir -p "$HYDRA_HOME"

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/identity.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state_v2.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/profiles.sh"

cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

echo "Running agent profile tests..."
echo "=============================="

assert_equal none "$(profile_resolve none)" "none is a first-class profile"
assert_equal task-file "$(profile_field claude prompt_mode)" "Claude task transport is declared"
assert_equal session-id "$(profile_field claude resume_mode)" "Claude resume recipe is declared"
assert_equal cwd-last "$(profile_field codex resume_mode)" "Codex fallback resume confidence is explicit"

fake_agent="$test_root/fake agent"
fake_output="$test_root/args"
task_file="$test_root/task with quote'"
cat > "$fake_agent" <<'EOF'
#!/bin/sh
printf '%s\n' "$#" > "$FAKE_OUTPUT"
printf '%s' "$1" >> "$FAKE_OUTPUT"
EOF
chmod +x "$fake_agent"
# The literal command substitution is the injection payload under test.
# shellcheck disable=SC2016
task_payload='Review $(touch should-not-exist); "quotes" and '"'"'single quotes'"'"''
printf '%s' "$task_payload" > "$task_file"

profile_create_custom fixture -- "$fake_agent" >/dev/null 2>&1
assert_failure $? "custom profile rejects malformed executable arguments"
profile_create_custom fixture "$fake_agent" task-file
assert_success $? "custom profile accepts an explicit executable path"
launch="$(profile_launch_command fixture "$task_file" "")"
FAKE_OUTPUT="$fake_output" export FAKE_OUTPUT
(
    cd "$test_root" || exit 1
    sh -c "$launch"
)
assert_success $? "generated task launch command executes"
assert_equal 1 "$(sed -n '1p' "$fake_output")" "task is delivered as one argument"
actual_payload="$(sed '1d' "$fake_output")"
assert_equal "$task_payload" "$actual_payload" "task bytes survive shell-safe injection"
if [ ! -e "$test_root/should-not-exist" ]; then
    assert_success 0 "task content cannot inject a shell command"
else
    assert_success 1 "task content cannot inject a shell command"
fi

planned_task_file="$test_root/planned task"
planned_launch="$(profile_launch_command fixture "$planned_task_file" "")"
printf '%s' "$task_payload" > "$planned_task_file"
FAKE_OUTPUT="$fake_output" sh -c "$planned_launch"
assert_success $? "launch recipe may be resolved before durable task creation"
assert_equal "$task_payload" "$(sed '1d' "$fake_output")" "planned launch reads the committed task at execution time"

provider_id="$(profile_new_provider_id claude instance_fixture)"
case "$provider_id" in
    ????????-????-4???-8???-????????????) assert_success 0 "provider session ID is UUID-shaped" ;;
    *) assert_success 1 "provider session ID is UUID-shaped" ;;
esac

echo "=============================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
