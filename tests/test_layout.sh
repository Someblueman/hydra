#!/bin/sh
# Unit tests for lib/layout.sh
# POSIX-compliant test framework

# These checks describe behaviour outside tmux. Detach from any caller's tmux
# server so running the file inside tmux neither skips them nor touches it.
unset TMUX TMUX_PANE

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/layout.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/layout.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Test apply_layout parameter validation
test_apply_layout_validation() {
    echo "Testing apply_layout parameter validation..."
    
    # Test empty layout name
    apply_layout "" 2>/dev/null
    assert_failure $? "apply_layout should fail with empty layout name"
    
    # Test unknown layout
    apply_layout "unknown-layout" 2>/dev/null
    assert_failure $? "apply_layout should fail with unknown layout"
    
    # Test without tmux session (outside tmux)
    apply_layout "default" 2>/dev/null
    assert_failure $? "apply_layout should fail when not in tmux session"
}

# Test get_current_layout function
test_get_current_layout() {
    echo "Testing get_current_layout..."
    
    # Test outside tmux
    get_current_layout >/dev/null 2>&1
    assert_failure $? "get_current_layout should fail when not in tmux session"
}

# Test cycle_layout function
test_cycle_layout() {
    echo "Testing cycle_layout..."
    
    # Test outside tmux
    cycle_layout 2>/dev/null
    assert_failure $? "cycle_layout should fail when not in tmux session"
}

# Run all tests
echo "Running layout.sh unit tests..."
echo "=================================="

test_apply_layout_validation
test_get_current_layout
test_cycle_layout

echo "=================================="
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
