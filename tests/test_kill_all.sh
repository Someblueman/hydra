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
test_base_dir=""
suffix=""

# Test helper functions
assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
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
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Output: $haystack"
        echo "  Expected to contain: $needle"
    fi
}

# Setup test environment
setup_test_env() {
    # Create a unique parent directory to avoid worktree collisions
    test_base_dir="$(mktemp -d)"
    mkdir -p "$test_base_dir/repo"
    cd "$test_base_dir/repo" || exit 1
    
    # Isolate hydra state in this base dir
    export HYDRA_HOME="$test_base_dir/.hydra"
    export HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"
    : > "$HYDRA_MAP"
    
    # Initialize a git repository
    git init >/dev/null 2>&1
    git config user.name "Test User"
    git config user.email "test@example.com"
    
    # Create initial commit
    echo "# Test Project" > README.md
    git add README.md
    git commit -m "Initial commit" >/dev/null 2>&1
    
    # Unique suffix for branch names
    suffix="$(date +%s%N 2>/dev/null || date +%s)"
    
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
    if [ -n "${HYDRA_HOME:-}" ] && [ -f "$HYDRA_HOME/map" ]; then
        : > "$HYDRA_HOME/map"
    fi
    
    # Remove any hydra-* worktrees under our unique base dir
    if [ -n "$test_base_dir" ] && [ -d "$test_base_dir" ]; then
        rm -rf "$test_base_dir"/hydra-*
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
    
    # Create test sessions with unique names
    BR1="test-kill-1-$suffix"
    BR2="test-kill-2-$suffix"
    BR3="test-kill-3-$suffix"
    "$HYDRA_BIN" spawn "$BR1" >/dev/null 2>&1
    "$HYDRA_BIN" spawn "$BR2" >/dev/null 2>&1
    "$HYDRA_BIN" spawn "$BR3" >/dev/null 2>&1
    
    # Verify sessions were created
    sessions_before="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -E -c "^(${BR1}|${BR2}|${BR3})$")"
    assert_equal "3" "$sessions_before" "Should have 3 test sessions before kill"
    
    # Kill all with force
    output="$("$HYDRA_BIN" kill --all --force 2>&1)"
    assert_contains "$output" "$BR1" "Should list $BR1"
    assert_contains "$output" "$BR2" "Should list $BR2"
    assert_contains "$output" "$BR3" "Should list $BR3"
    assert_contains "$output" "Killing all 3 hydra heads" "Should report killing 3 heads"
    assert_contains "$output" "Succeeded: 3" "Should report 3 successful kills"
    
    # Verify sessions were killed
    sessions_after="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -E -c "^(${BR1}|${BR2}|${BR3})$")"
    assert_equal "0" "$sessions_after" "Should have 0 test sessions after kill"
    
    # Verify map is empty
    if [ -n "${HYDRA_HOME:-}" ] && [ -f "$HYDRA_HOME/map" ]; then
        map_lines="$(grep -E -c "(${BR1}|${BR2}|${BR3})" "$HYDRA_HOME/map" 2>/dev/null)"
        assert_equal "0" "$map_lines" "Map should have no test-kill entries"
    fi
}

# Test: kill --all in non-interactive mode without force
test_kill_all_non_interactive_no_force() {
    echo ""
    echo "Test: kill --all in non-interactive mode without force"
    
    # Create a test session
    NOFORCE="test-kill-noforce-$suffix"
    "$HYDRA_BIN" spawn "$NOFORCE" >/dev/null 2>&1
    
    # Try to kill all without force in non-interactive mode
    output="$(HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN" kill --all 2>&1)"
    exit_code=$?
    
    assert_equal "1" "$exit_code" "Should exit with error code 1"
    assert_contains "$output" "Cannot kill all sessions in non-interactive mode without --force" "Should show error message"
    
    # Verify session still exists
    if tmux has-session -t "$NOFORCE" 2>/dev/null; then
        echo "[PASS] Session was not killed (as expected)"
    else
        echo "[FAIL] Session was killed (unexpected)"
        fail_count=$((fail_count + 1))
    fi
    
    # Cleanup
    "$HYDRA_BIN" kill "$NOFORCE" --force >/dev/null 2>&1
}

# Test: kill --all with partial failures
test_kill_all_partial_failure() {
    echo ""
    echo "Test: kill --all with partial failures"
    
    # Create test sessions with unique names
    K1="killtest-success1-$suffix"
    K2="killtest-success2-$suffix"
    "$HYDRA_BIN" spawn "$K1" >/dev/null 2>&1
    "$HYDRA_BIN" spawn "$K2" >/dev/null 2>&1
    
    # Create a mapping for a non-existent session
    echo "killtest-phantom-$suffix killtest-phantom-session" >> "$HYDRA_HOME/map"
    
    # Kill all with force
    output="$("$HYDRA_BIN" kill --all --force 2>&1)"
    
    assert_contains "$output" "$K1" "Should list $K1"
    assert_contains "$output" "$K2" "Should list $K2"
    assert_contains "$output" "killtest-phantom-$suffix" "Should list phantom mapping"
    assert_contains "$output" "Session 'killtest-phantom-session' not found" "Should report phantom session not found"
    assert_contains "$output" "Succeeded: 3" "Should count phantom cleanup as success"
    
    # Verify real sessions were killed
    sessions_after="$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -E -c "^(${K1}|${K2})$")"
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
    
    # Skip if tmux cannot create sessions in this environment
    if ! command -v tmux >/dev/null 2>&1 || ! tmux new-session -d -s killall-sanity 2>/dev/null; then
        echo "tmux unavailable or cannot create sessions; skipping kill --all tests"
        exit 0
    fi
    tmux kill-session -t killall-sanity 2>/dev/null || true
    
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
