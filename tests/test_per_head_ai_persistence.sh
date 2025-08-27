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
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected to contain: '$needle'"
        echo "  Actual output: '$haystack'"
    fi
}

setup_env() {
    test_dir="$(mktemp -d)" || exit 1
    export HYDRA_HOME="$test_dir/.hydra"
    export HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"
    : > "$HYDRA_MAP"
}

teardown_env() {
    [ -n "$test_dir" ] && rm -rf "$test_dir"
}

echo "Testing per-head AI persistence and display..."

setup_env

# Create mappings: one with AI set, one without
echo "feature-x sess-x aider" >> "$HYDRA_MAP"
echo "feature-y sess-y" >> "$HYDRA_MAP"

# hydra list should display AI for feature-x
list_output="$("$HYDRA_BIN" list 2>&1)"
assert_contains "$list_output" "feature-x -> sess-x [ai: aider]" "list shows [ai: aider] for feature-x"

# hydra status should also display AI for feature-x (sessions likely dead in CI)
status_output="$("$HYDRA_BIN" status 2>&1)"
assert_contains "$status_output" "feature-x -> sess-x" "status shows feature-x"
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
