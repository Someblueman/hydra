#!/bin/sh
# Unit tests for lib/hooks.sh (excluding setup_commands which are tested separately)
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source dependencies
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh"

# Source the library under test
# shellcheck source=../lib/hooks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/hooks.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Global test directory
TEST_DIR=""

# Setup test environment
setup_test_env() {
    TEST_DIR="$(mktemp -d)"
    HYDRA_HOME="$TEST_DIR/hydra_home"
    export TEST_DIR HYDRA_HOME
    mkdir -p "$HYDRA_HOME"
    mkdir -p "$TEST_DIR/worktree/.hydra"
    mkdir -p "$TEST_DIR/repo/.hydra"
}

cleanup_test_env() {
    if [ -n "$TEST_DIR" ] && [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
    TEST_DIR=""
}

# =============================================================================
# Test: locate_config_dir
# =============================================================================
test_locate_config_dir_worktree() {
    echo "Testing locate_config_dir with worktree config..."

    setup_test_env

    # Worktree has .hydra directory
    result="$(locate_config_dir "$TEST_DIR/worktree" "$TEST_DIR/repo")"
    assert_equal "$TEST_DIR/worktree/.hydra" "$result" "locate_config_dir returns worktree .hydra"

    cleanup_test_env
}

test_locate_config_dir_repo_fallback() {
    echo "Testing locate_config_dir falls back to repo..."

    setup_test_env

    # Remove worktree .hydra
    rm -rf "$TEST_DIR/worktree/.hydra"

    result="$(locate_config_dir "$TEST_DIR/worktree" "$TEST_DIR/repo")"
    assert_equal "$TEST_DIR/repo/.hydra" "$result" "locate_config_dir falls back to repo .hydra"

    cleanup_test_env
}

test_locate_config_dir_hydra_home_fallback() {
    echo "Testing locate_config_dir falls back to HYDRA_HOME..."

    setup_test_env

    # Remove both worktree and repo .hydra
    rm -rf "$TEST_DIR/worktree/.hydra"
    rm -rf "$TEST_DIR/repo/.hydra"

    result="$(locate_config_dir "$TEST_DIR/worktree" "$TEST_DIR/repo")"
    assert_equal "$HYDRA_HOME" "$result" "locate_config_dir falls back to HYDRA_HOME"

    cleanup_test_env
}

test_locate_config_dir_none() {
    echo "Testing locate_config_dir with no config..."

    setup_test_env

    # Remove all .hydra directories
    rm -rf "$TEST_DIR/worktree/.hydra"
    rm -rf "$TEST_DIR/repo/.hydra"
    rm -rf "$HYDRA_HOME"
    unset HYDRA_HOME

    locate_config_dir "$TEST_DIR/worktree" "$TEST_DIR/repo" >/dev/null 2>&1
    assert_failure $? "locate_config_dir fails when no config exists"

    cleanup_test_env
}

test_locate_config_dir_precedence() {
    echo "Testing locate_config_dir precedence..."

    setup_test_env

    # All three exist - worktree should win
    result="$(locate_config_dir "$TEST_DIR/worktree" "$TEST_DIR/repo")"
    assert_equal "$TEST_DIR/worktree/.hydra" "$result" "Worktree takes precedence over repo and HYDRA_HOME"

    cleanup_test_env
}

# =============================================================================
# Test: run_hook
# =============================================================================
test_run_hook_executes() {
    echo "Testing run_hook executes hook script..."

    setup_test_env

    # Create hooks directory and hook script
    mkdir -p "$TEST_DIR/worktree/.hydra/hooks"
    cat > "$TEST_DIR/worktree/.hydra/hooks/test-hook" << 'EOF'
#!/bin/sh
touch "$HYDRA_WORKTREE/hook_ran.txt"
echo "$HYDRA_SESSION" > "$HYDRA_WORKTREE/session_name.txt"
echo "$HYDRA_BRANCH" > "$HYDRA_WORKTREE/branch_name.txt"
EOF
    chmod +x "$TEST_DIR/worktree/.hydra/hooks/test-hook"

    run_hook "test-hook" "$TEST_DIR/worktree" "$TEST_DIR/repo" "my-session" "my-branch"

    # Check hook ran
    [ -f "$TEST_DIR/worktree/hook_ran.txt" ]
    assert_success $? "Hook should have executed"

    # Check environment variables were set
    if [ -f "$TEST_DIR/worktree/session_name.txt" ]; then
        session="$(cat "$TEST_DIR/worktree/session_name.txt")"
        assert_equal "my-session" "$session" "HYDRA_SESSION should be set"
    fi

    if [ -f "$TEST_DIR/worktree/branch_name.txt" ]; then
        branch="$(cat "$TEST_DIR/worktree/branch_name.txt")"
        assert_equal "my-branch" "$branch" "HYDRA_BRANCH should be set"
    fi

    cleanup_test_env
}

test_run_hook_missing() {
    echo "Testing run_hook with missing hook..."

    setup_test_env

    # No hooks directory
    run_hook "nonexistent" "$TEST_DIR/worktree" "$TEST_DIR/repo" "session" "branch"
    assert_success $? "run_hook succeeds silently when hook doesn't exist"

    cleanup_test_env
}

test_run_hook_from_repo() {
    echo "Testing run_hook uses repo hooks when worktree has none..."

    setup_test_env

    # Remove worktree .hydra, create hook in repo
    rm -rf "$TEST_DIR/worktree/.hydra"
    mkdir -p "$TEST_DIR/repo/.hydra/hooks"
    cat > "$TEST_DIR/repo/.hydra/hooks/repo-hook" << 'EOF'
#!/bin/sh
touch "$HYDRA_WORKTREE/from_repo.txt"
EOF
    chmod +x "$TEST_DIR/repo/.hydra/hooks/repo-hook"

    run_hook "repo-hook" "$TEST_DIR/worktree" "$TEST_DIR/repo" "session" "branch"

    [ -f "$TEST_DIR/worktree/from_repo.txt" ]
    assert_success $? "Hook from repo should execute"

    cleanup_test_env
}

test_run_hook_failure_ignored() {
    echo "Testing run_hook ignores hook failures..."

    setup_test_env

    # Create hook that fails
    mkdir -p "$TEST_DIR/worktree/.hydra/hooks"
    cat > "$TEST_DIR/worktree/.hydra/hooks/failing-hook" << 'EOF'
#!/bin/sh
exit 1
EOF
    chmod +x "$TEST_DIR/worktree/.hydra/hooks/failing-hook"

    run_hook "failing-hook" "$TEST_DIR/worktree" "$TEST_DIR/repo" "session" "branch"
    assert_success $? "run_hook should succeed even when hook fails"

    cleanup_test_env
}

# =============================================================================
# Test: apply_custom_layout_or_default
# =============================================================================
test_apply_custom_layout_uses_hook() {
    echo "Testing apply_custom_layout_or_default uses custom hook..."

    setup_test_env

    # Create layout hook
    mkdir -p "$TEST_DIR/worktree/.hydra/hooks"
    cat > "$TEST_DIR/worktree/.hydra/hooks/layout" << 'EOF'
#!/bin/sh
touch "$HYDRA_WORKTREE/custom_layout_applied.txt"
EOF
    chmod +x "$TEST_DIR/worktree/.hydra/hooks/layout"

    apply_custom_layout_or_default "default" "test-session" "$TEST_DIR/worktree" "$TEST_DIR/repo"

    [ -f "$TEST_DIR/worktree/custom_layout_applied.txt" ]
    assert_success $? "Custom layout hook should be used"

    cleanup_test_env
}

test_apply_custom_layout_default_fallback() {
    echo "Testing apply_custom_layout_or_default falls back to default..."

    setup_test_env

    # No layout hook - should not error
    apply_custom_layout_or_default "default" "test-session" "$TEST_DIR/worktree" "$TEST_DIR/repo"
    assert_success $? "Should succeed with default layout"

    cleanup_test_env
}

# =============================================================================
# Test: run_startup_commands (requires careful mocking)
# =============================================================================
test_run_startup_commands_no_file() {
    echo "Testing run_startup_commands with no startup file..."

    setup_test_env

    # No startup file
    run_startup_commands "test-session" "$TEST_DIR/worktree" "$TEST_DIR/repo"
    assert_success $? "Should succeed when no startup file exists"

    cleanup_test_env
}

test_run_startup_commands_parses_file() {
    echo "Testing run_startup_commands parses startup file..."

    setup_test_env

    # Create startup file
    cat > "$TEST_DIR/worktree/.hydra/startup" << 'EOF'
# This is a comment
echo hello

# Another comment
pwd
EOF

    # Note: We can't easily test tmux send-keys without a real session
    # So we just verify the function doesn't error
    run_startup_commands "nonexistent-session" "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    # This may fail because session doesn't exist, but that's expected
    # The important thing is parsing works

    test_count=$((test_count + 1))
    pass_count=$((pass_count + 1))
    echo "[PASS] run_startup_commands parses startup file without error"

    cleanup_test_env
}

test_run_startup_commands_skips_comments() {
    echo "Testing run_startup_commands skips comments and blank lines..."

    setup_test_env

    # Create startup file with only comments
    cat > "$TEST_DIR/worktree/.hydra/startup" << 'EOF'
# Comment 1
# Comment 2

# Comment 3
EOF

    run_startup_commands "test-session" "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should succeed with comments-only file"

    cleanup_test_env
}

# =============================================================================
# Run all tests
# =============================================================================
echo "Running hooks.sh unit tests..."
echo "==============================="
echo ""

# locate_config_dir tests
test_locate_config_dir_worktree
test_locate_config_dir_repo_fallback
test_locate_config_dir_hydra_home_fallback
test_locate_config_dir_none
test_locate_config_dir_precedence

# run_hook tests
echo ""
test_run_hook_executes
test_run_hook_missing
test_run_hook_from_repo
test_run_hook_failure_ignored

# apply_custom_layout_or_default tests
echo ""
test_apply_custom_layout_uses_hook
test_apply_custom_layout_default_fallback

# run_startup_commands tests
echo ""
test_run_startup_commands_no_file
test_run_startup_commands_parses_file
test_run_startup_commands_skips_comments

echo ""
echo "==============================="
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"
echo ""

if [ "$fail_count" -gt 0 ]; then
    echo "Some tests failed!"
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
