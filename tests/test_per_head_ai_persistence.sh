#!/bin/sh
# Tests for per-head AI persistence and display

test_count=0
pass_count=0
fail_count=0

original_dir="$(pwd)"
HYDRA_BIN="${HYDRA_BIN:-$original_dir/bin/hydra}"

assert_contains() {
    haystack="$1"
    needle="$2"
    message="$3"
    test_count=$((test_count + 1))
    if echo "$haystack" | grep -F -q "$needle"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Expected to contain: '$needle'"
        echo "  Actual output: '$haystack'"
    fi
}

setup_env() {
    test_dir="$(mktemp -d)" || exit 1
    export HYDRA_HOME="$test_dir/.hydra"
    export HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
    mkdir -p "$HYDRA_HOME" "$test_dir/repo"
    git -C "$test_dir/repo" init -q
    git -C "$test_dir/repo" config user.name Test
    git -C "$test_dir/repo" config user.email test@example.com
    git -C "$test_dir/repo" commit --allow-empty -qm init
    cd "$test_dir/repo" || exit 1
    "$HYDRA_BIN" init --no-agent --trust >/dev/null
}

teardown_env() {
    tmux kill-session -t feature-x 2>/dev/null || true
    tmux kill-session -t feature-y 2>/dev/null || true
    cd "$original_dir" || exit 1
    [ -n "$test_dir" ] && rm -rf "$test_dir"
}

echo "Testing per-head AI persistence and display..."

setup_env

"$HYDRA_BIN" spawn feature-x --no-agent >/dev/null
"$HYDRA_BIN" spawn feature-y --no-agent >/dev/null
project_id="$(sed -n '1p' .git/hydra/project-id)"
feature_x_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch -exec sh -c '[ "$(sed -n "1p" "$1")" = feature-x ] && dirname "$1"' sh {} \;)"
printf 'aider\n' > "$feature_x_dir/profile"

# hydra list should display AI for feature-x
list_output="$("$HYDRA_BIN" list 2>&1)"
assert_contains "$list_output" "feature-x -> feature-x" "list shows feature-x"
assert_contains "$list_output" "ai: aider" "list shows persisted aider profile"

# hydra status should also display AI for feature-x (sessions likely dead in CI)
status_output="$("$HYDRA_BIN" status 2>&1)"
assert_contains "$status_output" "feature-x -> feature-x" "status shows feature-x"
assert_contains "$status_output" "ai: aider" "status annotates AI tool"

teardown_env

echo ""
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"

if [ "$fail_count" -gt 0 ]; then
    exit 1
else
    exit 0
fi
