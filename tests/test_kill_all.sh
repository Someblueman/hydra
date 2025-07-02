#!/bin/sh
# Tests for hydra kill --all functionality
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Get the absolute path to hydra binary
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"

# CI environment detection
if [ -n "${GITHUB_ACTIONS:-}" ] || [ -n "${CI:-}" ]; then
    echo "Running in CI environment"
    # Set non-interactive mode for hydra commands in CI
    export HYDRA_NONINTERACTIVE=1
fi

# Store original directory
original_dir="$(pwd)"

# Test helper functions
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
        echo "  Expected: $expected"
        echo "  Actual: $actual"
    fi
}

assert_contains() {
    haystack="$1"
    needle="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if echo "$haystack" | grep -q "$needle"; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Output: $haystack"
        echo "  Expected to contain: $needle"
    fi
}

# Setup test environment
setup_test_env() {
    # Create a temporary test directory
    test_dir="$(mktemp -d)"
    cd "$test_dir" || exit 1
    
    # Initialize a git repository
    git init >/dev/null 2>&1
    git config user.name "Test User"
    git config user.email "test@example.com"
    
    # Create initial commit
    echo "# Test Project" > README.md
    git add README.md
    git commit -m "Initial commit" >/dev/null 2>&1
    
    # Clean up any existing test sessions
    cleanup_test_sessions
}

# Cleanup helper
cleanup_test_sessions() {
    # Kill any test sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            test-kill-*|killtest-*)
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
        esac
    done
    
    # Clear hydra map
    if [ -f "$HOME/.hydra/map" ]; then
        : > "$HOME/.hydra/map"
    fi
}

# Test: kill --all with no sessions
test_kill_all_no_sessions() {
    echo ""
    echo "Test: kill --all with no sessions"
    
    # Ensure no sessions exist
    cleanup_test_sessions
    
    output="$("$HYDRA_BIN" kill --all 2>&1)"
    assert_contains "$output" "No active Hydra heads to kill" "Should report no sessions"
}

# Test: kill --all with multiple sessions (force mode)
test_kill_all_force() {
    echo ""
    echo "Test: kill --all with multiple sessions (force mode)"
    
    # Create test sessions
    "$HYDRA_BIN" spawn test-kill-1 >/dev/null 2>&1
    "$HYDRA_BIN" spawn test-kill-2 >/dev/null 2>&1
    "$HYDRA_BIN" spawn test-kill-3 >/dev/null 2>&1
    
    # Verify sessions were created
    sessions_before="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^test-kill-')"
    assert_equal "3" "$sessions_before" "Should have 3 test sessions before kill"
    
    # Kill all with force
    output="$("$HYDRA_BIN" kill --all --force 2>&1)"
    assert_contains "$output" "test-kill-1" "Should list test-kill-1"
    assert_contains "$output" "test-kill-2" "Should list test-kill-2"
    assert_contains "$output" "test-kill-3" "Should list test-kill-3"
    assert_contains "$output" "Killing all 3 hydra heads" "Should report killing 3 heads"
    assert_contains "$output" "Succeeded: 3" "Should report 3 successful kills"
    
    # Verify sessions were killed
    sessions_after="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^test-kill-')"
    assert_equal "0" "$sessions_after" "Should have 0 test sessions after kill"
    
    # Verify map is empty
    if [ -f "$HOME/.hydra/map" ]; then
        map_lines="$(grep -c 'test-kill-' "$HOME/.hydra/map" 2>/dev/null)"
        assert_equal "0" "$map_lines" "Map should have no test-kill entries"
    fi
}

# Test: kill --all in non-interactive mode without force
test_kill_all_non_interactive_no_force() {
    echo ""
    echo "Test: kill --all in non-interactive mode without force"
    
    # Create a test session
    "$HYDRA_BIN" spawn test-kill-noforce >/dev/null 2>&1
    
    # Try to kill all without force in non-interactive mode
    output="$(HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN" kill --all 2>&1)"
    exit_code=$?
    
    assert_equal "1" "$exit_code" "Should exit with error code 1"
    assert_contains "$output" "Cannot kill all sessions in non-interactive mode without --force" "Should show error message"
    
    # Verify session still exists
    if tmux has-session -t test-kill-noforce 2>/dev/null; then
        echo "✓ Session was not killed (as expected)"
    else
        echo "✗ Session was killed (unexpected)"
        fail_count=$((fail_count + 1))
    fi
    
    # Cleanup
    "$HYDRA_BIN" kill test-kill-noforce --force >/dev/null 2>&1
}

# Test: kill --all with partial failures
test_kill_all_partial_failure() {
    echo ""
    echo "Test: kill --all with partial failures"
    
    # Create test sessions
    "$HYDRA_BIN" spawn killtest-success1 >/dev/null 2>&1
    "$HYDRA_BIN" spawn killtest-success2 >/dev/null 2>&1
    
    # Create a mapping for a non-existent session
    echo "killtest-phantom killtest-phantom-session" >> "$HOME/.hydra/map"
    
    # Kill all with force
    output="$("$HYDRA_BIN" kill --all --force 2>&1)"
    
    assert_contains "$output" "killtest-success1" "Should list killtest-success1"
    assert_contains "$output" "killtest-success2" "Should list killtest-success2"
    assert_contains "$output" "killtest-phantom" "Should list killtest-phantom"
    assert_contains "$output" "Session 'killtest-phantom-session' not found" "Should report phantom session not found"
    assert_contains "$output" "Succeeded: 3" "Should count phantom cleanup as success"
    
    # Verify real sessions were killed
    sessions_after="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^killtest-')"
    assert_equal "0" "$sessions_after" "Should have 0 killtest sessions after kill"
}

# Test: kill with both --all and branch name should fail
test_kill_all_with_branch_fails() {
    echo ""
    echo "Test: kill with both --all and branch name should fail"
    
    output="$("$HYDRA_BIN" kill --all test-branch 2>&1)"
    exit_code=$?
    
    assert_equal "1" "$exit_code" "Should exit with error code 1"
    assert_contains "$output" "Cannot specify both branch name and --all" "Should show mutual exclusivity error"
}

# Main test runner
main() {
    echo "Running hydra kill --all tests..."
    
    # Setup test environment
    setup_test_env
    
    # Run tests
    test_kill_all_no_sessions
    test_kill_all_force
    test_kill_all_non_interactive_no_force
    test_kill_all_partial_failure
    test_kill_all_with_branch_fails
    
    # Cleanup
    cleanup_test_sessions
    cd "$original_dir" || exit 1
    if [ -n "${test_dir:-}" ] && [ -d "${test_dir:-}" ]; then
        rm -rf "$test_dir"
    fi
    
    # Summary
    echo ""
    echo "Test Summary:"
    echo "  Total: $test_count"
    echo "  Passed: $pass_count"
    echo "  Failed: $fail_count"
    
    if [ "$fail_count" -eq 0 ]; then
        echo ""
        echo "All tests passed!"
        exit 0
    else
        echo ""
        echo "Some tests failed!"
        exit 1
    fi
}

# Run main
main