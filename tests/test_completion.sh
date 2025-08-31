#!/bin/sh
# Unit tests for completion dispatcher in lib/completion.sh via bin/hydra
# POSIX-compliant tests

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Local helper to assert non-empty string
# shellcheck disable=SC2317
assert_nonempty() {
    value="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ -n "$value" ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Got empty output"
    fi
}

HYDRA_NONINTERACTIVE=1
export HYDRA_NONINTERACTIVE

echo "Running completion dispatcher tests..."
echo "====================================="

# Test bash completion generation
out="$(HYDRA_HOME="$(mktemp -d)" bin/hydra completion bash 2>/dev/null)"
rc=$?
assert_success "$rc" "hydra completion bash should succeed"
assert_nonempty "$out" "bash completion output should be non-empty"

# Test zsh completion generation
out="$(HYDRA_HOME="$(mktemp -d)" bin/hydra completion zsh 2>/dev/null)"
rc=$?
assert_success "$rc" "hydra completion zsh should succeed"
assert_nonempty "$out" "zsh completion output should be non-empty"

# Test fish completion generation
out="$(HYDRA_HOME="$(mktemp -d)" bin/hydra completion fish 2>/dev/null)"
rc=$?
assert_success "$rc" "hydra completion fish should succeed"
assert_nonempty "$out" "fish completion output should be non-empty"

# Test unknown shell returns failure
out="$(HYDRA_HOME="$(mktemp -d)" bin/hydra completion unknown 2>/dev/null)"
rc=$?
assert_failure "$rc" "hydra completion unknown should fail"

echo "====================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi

