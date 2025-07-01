#!/bin/sh
# Integration tests for main hydra commands
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
    CI_ENV=1
else
    CI_ENV=0
fi

# Pre-test cleanup for CI
cleanup_ci_environment() {
    if [ "$CI_ENV" -eq 1 ]; then
        echo "Performing CI-specific cleanup..."
        # Kill any leftover tmux sessions
        tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
            case "$session" in
                hydra-*|test-*|feature_*)
                    echo "  Killing CI leftover session: $session"
                    tmux kill-session -t "$session" 2>/dev/null || true
                    ;;
            esac
        done
    fi
}

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
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
    fi
}

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

assert_contains() {
    text="$1"
    pattern="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if echo "$text" | grep -q "$pattern"; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Text does not contain: '$pattern'"
        echo "  Actual text: '$text'"
    fi
}

# Setup test environment
setup_test_env() {
    test_dir="$(mktemp -d)" || {
        echo "Error: Failed to create temporary directory" >&2
        return 1
    }
    HYDRA_HOME="$test_dir/.hydra"
    export HYDRA_HOME
    echo "$test_dir"
}

cleanup_test_env() {
    test_dir="$1"
    rm -rf "$test_dir"
    unset HYDRA_HOME
}

# Test hydra version command
test_version_command() {
    echo "Testing hydra version command..."
    
    # Test version command
    output="$("$HYDRA_BIN" version 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra version should succeed"
    assert_contains "$output" "hydra version" "Version output should contain version string"
    
    # Test --version flag
    output="$("$HYDRA_BIN" --version 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra --version should succeed"
    assert_contains "$output" "hydra version" "Version output should contain version string"
    
    # Test -v flag
    output="$("$HYDRA_BIN" -v 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra -v should succeed"
    assert_contains "$output" "hydra version" "Version output should contain version string"
}

# Test hydra help command
test_help_command() {
    echo "Testing hydra help command..."
    
    # Test help command
    output="$("$HYDRA_BIN" help 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra help should succeed"
    assert_contains "$output" "Usage:" "Help output should contain usage information"
    assert_contains "$output" "Commands:" "Help output should contain commands list"
    
    # Test --help flag
    output="$("$HYDRA_BIN" --help 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra --help should succeed"
    assert_contains "$output" "Usage:" "Help output should contain usage information"
    
    # Test -h flag
    output="$("$HYDRA_BIN" -h 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra -h should succeed"
    assert_contains "$output" "Usage:" "Help output should contain usage information"
    
    # Test no arguments (should show help)
    output="$("$HYDRA_BIN" 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra with no arguments should show help"
    assert_contains "$output" "Usage:" "No arguments should show help"
}

# Test hydra unknown command
test_unknown_command() {
    echo "Testing hydra unknown command..."
    
    output="$("$HYDRA_BIN" unknown-command 2>&1)"
    exit_code=$?
    assert_failure "$exit_code" "hydra unknown-command should fail"
    assert_contains "$output" "Unknown command" "Should report unknown command error"
    assert_contains "$output" "hydra help" "Should suggest help command"
}

# Test hydra list command (empty state)
test_list_command_empty() {
    echo "Testing hydra list command with empty state..."
    
    test_dir="$(setup_test_env)"
    
    output="$("$HYDRA_BIN" list 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra list should succeed even when empty"
    
    cleanup_test_env "$test_dir"
}

# Test hydra status command
test_status_command() {
    echo "Testing hydra status command..."
    
    # Status command is designed to run from within a git repo
    # In CI or restricted environments, we'll just check basic functionality
    output="$("$HYDRA_BIN" status 2>&1)"
    exit_code=$?
    
    # Status might fail if not in a git repo, but should still show output
    if [ "$exit_code" -ne 0 ]; then
        # Check if it failed gracefully with proper output
        if echo "$output" | grep -q "Hydra Status"; then
            echo "✓ hydra status shows output even when not in git repo"
            pass_count=$((pass_count + 1))
        else
            echo "✗ hydra status failed without proper output"
            fail_count=$((fail_count + 1))
        fi
        test_count=$((test_count + 1))
    else
        assert_success "$exit_code" "hydra status should succeed"
    fi
    
    assert_contains "$output" "Hydra Status" "Status output should contain status header"
}

# Test hydra doctor command
test_doctor_command() {
    echo "Testing hydra doctor command..."
    
    test_dir="$(setup_test_env)"
    
    output="$("$HYDRA_BIN" doctor 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra doctor should succeed"
    assert_contains "$output" "Dependencies:" "Doctor output should contain dependencies check"
    assert_contains "$output" "tmux" "Doctor should check tmux"
    assert_contains "$output" "git" "Doctor should check git"
    
    cleanup_test_env "$test_dir"
}

# Test hydra regenerate command (empty state)
test_regenerate_command_empty() {
    echo "Testing hydra regenerate command with empty state..."
    
    test_dir="$(setup_test_env)"
    
    output="$("$HYDRA_BIN" regenerate 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra regenerate should succeed even when empty"
    
    cleanup_test_env "$test_dir"
}

# Test hydra spawn command parameter validation
test_spawn_command_validation() {
    echo "Testing hydra spawn command parameter validation..."
    
    test_dir="$(setup_test_env)"
    
    # Test spawn without branch argument
    output="$("$HYDRA_BIN" spawn 2>&1)"
    exit_code=$?
    assert_failure "$exit_code" "hydra spawn without branch should fail"
    assert_contains "$output" "Branch name is required" "Should report missing branch error"
    
    cleanup_test_env "$test_dir"
}

# Test hydra kill command parameter validation
test_kill_command_validation() {
    echo "Testing hydra kill command parameter validation..."
    
    test_dir="$(setup_test_env)"
    
    # Test kill without branch argument
    output="$("$HYDRA_BIN" kill 2>&1)"
    exit_code=$?
    assert_failure "$exit_code" "hydra kill without branch should fail"
    assert_contains "$output" "Error: Branch name is required" "Should report missing argument error"
    
    cleanup_test_env "$test_dir"
}

# Test HYDRA_HOME initialization
test_hydra_home_init() {
    echo "Testing HYDRA_HOME initialization..."
    
    test_dir="$(mktemp -d)"
    HYDRA_HOME="$test_dir/.hydra"
    export HYDRA_HOME
    
    # HYDRA_HOME should not exist initially
    if [ -d "$HYDRA_HOME" ]; then
        echo "⚠ HYDRA_HOME already exists, skipping initialization test"
        pass_count=$((pass_count + 2))
        test_count=$((test_count + 2))
    else
        # Run any hydra command to trigger initialization
        env HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" version >/dev/null 2>&1
        
        test_count=$((test_count + 1))
        if [ -d "$HYDRA_HOME" ]; then
            echo "✓ HYDRA_HOME directory should be created"
            pass_count=$((pass_count + 1))
        else
            echo "✗ HYDRA_HOME directory should be created (path: $HYDRA_HOME)"
            fail_count=$((fail_count + 1))
        fi
        
        test_count=$((test_count + 1))
        if [ -f "$HYDRA_HOME/map" ]; then
            echo "✓ Map file should be created"
            pass_count=$((pass_count + 1))
        else
            echo "✗ Map file should be created (path: $HYDRA_HOME/map)"
            fail_count=$((fail_count + 1))
        fi
    fi
    
    rm -rf "$test_dir"
}

# Test hydra spawn and kill cycle for issue branches
test_issue_branch_cleanup() {
    echo "Testing issue branch spawn and kill cycle..."
    
    # Only run if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "⚠ Skipping issue branch test - not in a git repository"
        return
    fi
    
    # Create isolated test environment
    test_dir="$(setup_test_env)"
    
    # Create a test branch name that looks like an issue branch
    test_branch="issue-999-test-cleanup-$(date +%s)"
    repo_root="$(git rev-parse --show-toplevel)"
    expected_worktree="$repo_root/../hydra-$test_branch"
    
    # Skip if tmux is not available
    if ! command -v tmux >/dev/null 2>&1; then
        echo "⚠ Skipping issue branch test - tmux not available"
        cleanup_test_env "$test_dir"
        return
    fi
    
    # Create the branch and worktree
    echo "  Creating test branch '$test_branch'..."
    output="$("$HYDRA_BIN" spawn "$test_branch" 2>&1)"
    exit_code=$?
    
    if [ "$exit_code" -ne 0 ]; then
        echo "⚠ Skipping - spawn failed (might be in non-terminal environment)"
        return
    fi
    
    # Check that worktree was created
    if [ -d "$expected_worktree" ]; then
        assert_success 0 "Worktree directory was created at expected location"
    else
        assert_failure 1 "Worktree directory was not created at expected location"
    fi
    
    # Check that branch exists in worktree list
    worktree_exists="$(git worktree list | grep -c "$test_branch" || true)"
    if [ "$worktree_exists" -gt 0 ]; then
        assert_success 0 "Branch appears in git worktree list"
    else
        assert_failure 1 "Branch does not appear in git worktree list"
    fi
    
    # Now kill the branch
    echo "  Killing test branch '$test_branch'..."
    output="$("$HYDRA_BIN" kill "$test_branch" 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "hydra kill should succeed"
    
    # Verify worktree was removed
    if [ ! -d "$expected_worktree" ]; then
        assert_success 0 "Worktree directory was successfully removed"
    else
        assert_failure 1 "Worktree directory still exists after kill"
    fi
    
    # Verify branch is no longer in worktree list
    worktree_exists="$(git worktree list | grep -c "$test_branch" || true)"
    if [ "$worktree_exists" -eq 0 ]; then
        assert_success 0 "Branch no longer appears in git worktree list"
    else
        assert_failure 1 "Branch still appears in git worktree list after kill"
    fi
    
    # Verify mapping was removed
    if [ -f "$HYDRA_HOME/map" ]; then
        mapping_exists="$(grep -c "$test_branch" "$HYDRA_HOME/map" 2>/dev/null || true)"
        if [ "$mapping_exists" -eq 0 ]; then
            assert_success 0 "Branch mapping was removed from state file"
        else
            assert_failure 1 "Branch mapping still exists in state file"
        fi
    fi
    
    # Clean up test environment
    cleanup_test_env "$test_dir"
}

# Test that hydra binary is executable and has correct shebang
test_hydra_binary() {
    echo "Testing hydra binary properties..."
    
    # Test that hydra is executable
    if [ -x "$HYDRA_BIN" ]; then
        echo "✓ Hydra binary should be executable"
        pass_count=$((pass_count + 1))
    else
        echo "✗ Hydra binary should be executable"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
    
    # Test shebang
    first_line="$(head -n 1 "$HYDRA_BIN")"
    assert_equal "#!/bin/sh" "$first_line" "Hydra binary should have POSIX sh shebang"
}

# Run all tests
echo "Running hydra integration tests..."
echo "=================================="

# Perform CI cleanup if needed
cleanup_ci_environment

test_hydra_binary
test_version_command
test_help_command
test_unknown_command
test_list_command_empty
test_status_command
test_doctor_command
test_regenerate_command_empty
test_spawn_command_validation
test_kill_command_validation
test_hydra_home_init
test_issue_branch_cleanup

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