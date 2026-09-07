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
assert_equal cwd-last "$(profile_field codex resume_mode)" "Codex interactive resume remains cwd-scoped"
assert_equal "'codex' resume --last" "$(profile_resume_command codex '')" "Codex interactive resume supports heads without recorded IDs"
assert_equal "'codex' resume --last" "$(profile_resume_command codex recorded-session)" "Codex interactive recipe stays separate from headless exact resume"
assert_equal cursor-agent "$(profile_field cursor executable)" "Cursor profile selects the agent CLI"
for builtin in agy cursor opencode; do
    profile_exists "$builtin"
    assert_success $? "$builtin is an interactive profile"
    assert_equal task-file "$(profile_field "$builtin" prompt_mode)" "$builtin declares task delivery"
    assert_equal none "$(profile_field "$builtin" resume_mode)" "$builtin interactive launch does not invent a recorded session"
done

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

mkdir "$test_root/bin"
cat > "$test_root/bin/agent-argv" <<'EOF'
#!/bin/sh
case "$1" in
    --prompt=*|--prompt-interactive=*)
        [ "$#" -eq 1 ] || exit 1
        printf '%s\n' "${1%%=*}" > "$FAKE_OUTPUT.flag"
        printf '%s' "${1#*=}" > "$FAKE_OUTPUT" ;;
    --)
        [ "$#" -eq 2 ] || exit 1
        printf '%s\n' "$1" > "$FAKE_OUTPUT.flag"
        printf '%s' "$2" > "$FAKE_OUTPUT" ;;
    *) exit 1 ;;
esac
EOF
chmod +x "$test_root/bin/agent-argv"
builtin_payload="--option $task_payload"
printf '%s' "$builtin_payload" > "$test_root/builtin-task"
for builtin in agy cursor opencode; do
    executable="$(profile_field "$builtin" executable)"
    ln -s agent-argv "$test_root/bin/$executable"
    launch="$(profile_launch_command "$builtin" "$test_root/builtin-task" "")"
    PATH="$test_root/bin:$PATH" sh -c "$launch"
    assert_success $? "$builtin delivers exactly one prompt argument"
    assert_equal "$builtin_payload" "$(cat "$fake_output")" "$builtin preserves literal task bytes and leading options"
    case "$builtin" in agy) flag=--prompt-interactive ;; cursor) flag=-- ;; opencode) flag=--prompt ;; esac
    assert_equal "$flag" "$(cat "$fake_output.flag")" "$builtin uses its interactive prompt flag"
done
# A formerly custom name must not silently switch executables after an upgrade.
cp -R "$HYDRA_HOME/profiles/fixture" "$HYDRA_HOME/profiles/agy"
assert_equal "$fake_agent" "$(profile_field agy executable)" "registered custom agy executable is preserved"
assert_equal 1 "$(profile_list | grep -c '^agy$')" "a previously custom builtin name is listed once"
launch="$(profile_launch_command agy "$task_file" "")"
FAKE_OUTPUT="$fake_output" sh -c "$launch"
assert_equal "$task_payload" "$(sed '1d' "$fake_output")" "existing custom prompt recipe is preserved"
printf '{}\n' > "$HYDRA_HOME/profiles/agy/adapter.json"
profile_executable_path agy >/dev/null 2>&1
assert_failure $? "a stored headless profile cannot resolve to the builtin launch executable"
assert_equal 0 "$(profile_list | grep -c '^agy$')" "headless-only names stay out of the interactive list"
profile_exists agy
assert_failure $? "a stored headless name is not an interactive profile"

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
