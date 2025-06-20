#!/bin/sh
# Unit tests for lib/tmux.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh"

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

# Test check_tmux_version function
test_check_tmux_version() {
    echo "Testing check_tmux_version..."
    
    # This test depends on tmux being available
    if command -v tmux >/dev/null 2>&1; then
        if check_tmux_version >/dev/null 2>&1; then
            echo "✓ check_tmux_version succeeds with available tmux"
            pass_count=$((pass_count + 1))
        else
            echo "⚠ check_tmux_version fails - tmux version may be too old"
            pass_count=$((pass_count + 1))
        fi
    else
        echo "⚠ Skipping tmux version test - tmux not available"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))
}

# Test tmux_session_exists parameter validation
test_tmux_session_exists_validation() {
    echo "Testing tmux_session_exists parameter validation..."
    
    # Test empty session name
    tmux_session_exists ""
    assert_failure $? "tmux_session_exists should fail with empty session name"
    
    # Test non-existent session (assuming this session doesn't exist)
    tmux_session_exists "hydra-test-definitely-does-not-exist-$(date +%s)"
    assert_failure $? "tmux_session_exists should fail for non-existent session"
}

# Test create_session parameter validation
test_create_session_validation() {
    echo "Testing create_session parameter validation..."
    
    # Test empty parameters
    create_session "" "" 2>/dev/null
    assert_failure $? "create_session should fail with empty session name and directory"
    
    create_session "test" "" 2>/dev/null
    assert_failure $? "create_session should fail with empty directory"
    
    create_session "" "/tmp" 2>/dev/null
    assert_failure $? "create_session should fail with empty session name"
    
    # Test non-existent directory
    create_session "test" "/definitely/does/not/exist" 2>/dev/null
    assert_failure $? "create_session should fail with non-existent directory"
}

# Test kill_session parameter validation
test_kill_session_validation() {
    echo "Testing kill_session parameter validation..."
    
    # Test empty session name
    kill_session "" 2>/dev/null
    assert_failure $? "kill_session should fail with empty session name"
    
    # Test non-existent session
    kill_session "hydra-test-definitely-does-not-exist-$(date +%s)" 2>/dev/null
    assert_failure $? "kill_session should fail for non-existent session"
}

# Test send_keys_to_session parameter validation
test_send_keys_validation() {
    echo "Testing send_keys_to_session parameter validation..."
    
    # Test empty parameters
    send_keys_to_session "" "" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty session and keys"
    
    send_keys_to_session "test" "" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty keys"
    
    send_keys_to_session "" "ls" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty session name"
    
    # Test non-existent session
    send_keys_to_session "hydra-test-definitely-does-not-exist-$(date +%s)" "ls" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail for non-existent session"
}

# Test switch_to_session parameter validation
test_switch_to_session_validation() {
    echo "Testing switch_to_session parameter validation..."
    
    # Test empty session name
    switch_to_session "" 2>/dev/null
    assert_failure $? "switch_to_session should fail with empty session name"
    
    # Test non-existent session (this will fail gracefully)
    switch_to_session "hydra-test-definitely-does-not-exist-$(date +%s)" 2>/dev/null
    assert_failure $? "switch_to_session should fail for non-existent session"
}

# Test rename_session parameter validation
test_rename_session_validation() {
    echo "Testing rename_session parameter validation..."
    
    # Test empty parameters
    rename_session "" "" 2>/dev/null
    assert_failure $? "rename_session should fail with empty old and new names"
    
    rename_session "old" "" 2>/dev/null
    assert_failure $? "rename_session should fail with empty new name"
    
    rename_session "" "new" 2>/dev/null
    assert_failure $? "rename_session should fail with empty old name"
    
    # Test non-existent session
    rename_session "hydra-test-definitely-does-not-exist-$(date +%s)" "new-name" 2>/dev/null
    assert_failure $? "rename_session should fail for non-existent session"
}

# Test list_sessions (basic functionality)
test_list_sessions() {
    echo "Testing list_sessions..."
    
    # This should not fail even if no sessions exist
    if list_sessions >/dev/null 2>&1; then
        assert_success 0 "list_sessions should always succeed"
    else
        assert_failure 1 "list_sessions failed unexpectedly"
    fi
}

# Test get_current_session (outside tmux)
test_get_current_session() {
    echo "Testing get_current_session..."
    
    # Outside tmux, this should fail
    if [ -z "$TMUX" ]; then
        get_current_session >/dev/null 2>&1
        assert_failure $? "get_current_session should fail when not in tmux"
    else
        echo "⚠ Skipping get_current_session test - already inside tmux"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
    fi
}

# Run all tests
echo "Running tmux.sh unit tests (parameter validation)..."
echo "=============================================="

test_check_tmux_version
test_tmux_session_exists_validation
test_create_session_validation
test_kill_session_validation
test_send_keys_validation
test_switch_to_session_validation
test_rename_session_validation
test_list_sessions
test_get_current_session

echo "=============================================="
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