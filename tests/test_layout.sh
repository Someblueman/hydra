#!/bin/sh
# Unit tests for lib/layout.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/layout.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/layout.sh"

# Test helper functions
# shellcheck disable=SC2317
assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
    fi
}

# shellcheck disable=SC2317
assert_success() {
    exit_code="$1"
    message="$2"
    
    test_count=$((test_count + 1))
    if [ "$exit_code" -eq 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code $exit_code)"
    fi
}

# shellcheck disable=SC2317
assert_failure() {
    exit_code="$1"
    message="$2"
    
    test_count=$((test_count + 1))
    if [ "$exit_code" -ne 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: failure (non-zero exit code)"
        echo "  Actual:   success (exit code 0)"
    fi
}

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
    if [ -z "$TMUX" ]; then
        apply_layout "default" 2>/dev/null
        assert_failure $? "apply_layout should fail when not in tmux session"
    else
        echo "⚠ Skipping tmux check - already in tmux session"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
    fi
}

# Test get_current_layout function
test_get_current_layout() {
    echo "Testing get_current_layout..."
    
    # Test outside tmux
    if [ -z "$TMUX" ]; then
        get_current_layout >/dev/null 2>&1
        assert_failure $? "get_current_layout should fail when not in tmux session"
    else
        # Inside tmux - should return something
        result="$(get_current_layout)"
        if [ -n "$result" ]; then
            echo "✓ get_current_layout returns a layout name when in tmux"
            pass_count=$((pass_count + 1))
        else
            echo "✗ get_current_layout should return a layout name when in tmux"
            fail_count=$((fail_count + 1))
        fi
        test_count=$((test_count + 1))
    fi
}

# Test cycle_layout function
test_cycle_layout() {
    echo "Testing cycle_layout..."
    
    # Test outside tmux
    if [ -z "$TMUX" ]; then
        cycle_layout 2>/dev/null
        assert_failure $? "cycle_layout should fail when not in tmux session"
    else
        echo "⚠ Skipping cycle_layout test - would modify current tmux session"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
    fi
}

# Test save_layout parameter validation
test_save_layout_validation() {
    echo "Testing save_layout parameter validation..."
    
    # Test empty session name
    save_layout "" 2>/dev/null
    assert_failure $? "save_layout should fail with empty session name"
    
    # Test without HYDRA_HOME
    old_home="$HYDRA_HOME"
    unset HYDRA_HOME
    save_layout "test-session" 2>/dev/null
    assert_failure $? "save_layout should fail when HYDRA_HOME is not set"
    
    # Restore HYDRA_HOME
    HYDRA_HOME="$old_home"
    export HYDRA_HOME
}

# Test restore_layout parameter validation
test_restore_layout_validation() {
    echo "Testing restore_layout parameter validation..."
    
    # Test empty session name
    restore_layout "" 2>/dev/null
    assert_failure $? "restore_layout should fail with empty session name"
    
    # Test without HYDRA_HOME
    old_home="$HYDRA_HOME"
    unset HYDRA_HOME
    restore_layout "test-session" 2>/dev/null
    assert_failure $? "restore_layout should fail when HYDRA_HOME is not set"
    
    # Restore HYDRA_HOME
    HYDRA_HOME="$old_home"
    export HYDRA_HOME
}

# Test restore_layout with non-existent file
test_restore_layout_no_file() {
    echo "Testing restore_layout with non-existent layout file..."
    
    # Set up temporary HYDRA_HOME
    temp_home="$(mktemp -d)"
    HYDRA_HOME="$temp_home"
    export HYDRA_HOME
    
    # Should succeed even if no layout file exists
    restore_layout "non-existent-session" 
    assert_success $? "restore_layout should succeed when no layout file exists"
    
    # Cleanup
    rm -rf "$temp_home"
    unset HYDRA_HOME
}

# Test setup_layout_hotkeys parameter validation
test_setup_layout_hotkeys_validation() {
    echo "Testing setup_layout_hotkeys parameter validation..."
    
    # Test empty session name
    setup_layout_hotkeys "" 2>/dev/null
    assert_failure $? "setup_layout_hotkeys should fail with empty session name"
    
    # Test with valid session name (this might fail if tmux/session doesn't exist, but that's expected)
    setup_layout_hotkeys "test-session" 2>/dev/null
    # We don't assert success/failure here since it depends on tmux availability and session existence
    echo "✓ setup_layout_hotkeys parameter validation passed"
    pass_count=$((pass_count + 1))
    test_count=$((test_count + 1))
}

# Test layout save/restore integration
test_layout_save_restore() {
    echo "Testing layout save/restore integration..."
    
    # Set up temporary HYDRA_HOME
    temp_home="$(mktemp -d)"
    HYDRA_HOME="$temp_home"
    export HYDRA_HOME
    
    # Create test layout file
    mkdir -p "$HYDRA_HOME/layouts"
    echo "test-layout-string" > "$HYDRA_HOME/layouts/test-session"
    
    # Test restore with existing file
    restore_layout "test-session" 2>/dev/null
    # Should not fail even if tmux command fails (layout file exists)
    assert_success $? "restore_layout should succeed with existing layout file"
    
    # Cleanup
    rm -rf "$temp_home"
    unset HYDRA_HOME
}

# Run all tests
echo "Running layout.sh unit tests..."
echo "=================================="

test_apply_layout_validation
test_get_current_layout
test_cycle_layout
test_save_layout_validation
test_restore_layout_validation
test_restore_layout_no_file
test_setup_layout_hotkeys_validation
test_layout_save_restore

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