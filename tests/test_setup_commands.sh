#!/bin/sh
# Unit tests for setup command functionality in lib/hooks.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test and its dependencies
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
    unset HYDRA_SKIP_SETUP HYDRA_SETUP_CONTINUE
}

# =============================================================================
# parse_setup_commands Tests
# =============================================================================

test_parse_setup_commands_basic() {
    echo "Testing parse_setup_commands with basic config..."

    setup_test_env

    # Create config file with setup section
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - npm install
  - echo hello
  - cp .env.example .env

windows:
  - name: main
EOF

    result="$(parse_setup_commands "$TEST_DIR/worktree/.hydra/config.yml")"

    # Check all commands are parsed
    echo "$result" | grep -q "npm install"
    assert_success $? "Should parse 'npm install' command"

    echo "$result" | grep -q "echo hello"
    assert_success $? "Should parse 'echo hello' command"

    echo "$result" | grep -q "cp .env.example .env"
    assert_success $? "Should parse 'cp' command"

    # Check count
    count="$(echo "$result" | wc -l | tr -d ' ')"
    assert_equal "3" "$count" "Should have 3 commands"

    cleanup_test_env
}

test_parse_setup_commands_no_setup() {
    echo "Testing parse_setup_commands with no setup section..."

    setup_test_env

    # Create config file without setup section
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
windows:
  - name: main
    panes:
      - nvim
EOF

    result="$(parse_setup_commands "$TEST_DIR/worktree/.hydra/config.yml")"

    # Should be empty
    assert_equal "" "$result" "Should return empty for config without setup"

    cleanup_test_env
}

test_parse_setup_commands_empty_setup() {
    echo "Testing parse_setup_commands with empty setup section..."

    setup_test_env

    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:

windows:
  - name: main
EOF

    result="$(parse_setup_commands "$TEST_DIR/worktree/.hydra/config.yml")"

    assert_equal "" "$result" "Should return empty for empty setup section"

    cleanup_test_env
}

test_parse_setup_commands_nonexistent_file() {
    echo "Testing parse_setup_commands with nonexistent file..."

    setup_test_env

    result="$(parse_setup_commands "/nonexistent/config.yml")"
    assert_success $? "Should succeed (silently) for nonexistent file"
    assert_equal "" "$result" "Should return empty for nonexistent file"

    cleanup_test_env
}

# =============================================================================
# run_setup_commands Tests
# =============================================================================

test_run_setup_commands_success() {
    echo "Testing run_setup_commands with successful commands..."

    setup_test_env

    # Create config with commands that should succeed
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - touch marker1.txt
  - touch marker2.txt
EOF

    result="$(run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>&1)"
    assert_success $? "run_setup_commands should succeed"

    # Check marker files were created in worktree
    [ -f "$TEST_DIR/worktree/marker1.txt" ]
    assert_success $? "marker1.txt should be created in worktree"

    [ -f "$TEST_DIR/worktree/marker2.txt" ]
    assert_success $? "marker2.txt should be created in worktree"

    cleanup_test_env
}

test_run_setup_commands_failure_aborts() {
    echo "Testing run_setup_commands failure aborts spawn..."

    setup_test_env

    # Create config with a failing command
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - touch success.txt
  - false
  - touch should_not_exist.txt
EOF

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_failure $? "run_setup_commands should fail on command failure"

    # First command should have run
    [ -f "$TEST_DIR/worktree/success.txt" ]
    assert_success $? "First command should have executed"

    # Third command should NOT have run
    [ ! -f "$TEST_DIR/worktree/should_not_exist.txt" ]
    assert_success $? "Commands after failure should not execute"

    cleanup_test_env
}

test_run_setup_commands_continue_on_failure() {
    echo "Testing run_setup_commands continues with HYDRA_SETUP_CONTINUE..."

    setup_test_env

    # Create config with a failing command
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - touch before_fail.txt
  - false
  - touch after_fail.txt
EOF

    HYDRA_SETUP_CONTINUE=1
    export HYDRA_SETUP_CONTINUE

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    # Note: This may still return success because while loop is in subshell

    # Both marker files should exist
    [ -f "$TEST_DIR/worktree/before_fail.txt" ]
    assert_success $? "Command before failure should have executed"

    [ -f "$TEST_DIR/worktree/after_fail.txt" ]
    assert_success $? "Command after failure should execute with HYDRA_SETUP_CONTINUE"

    cleanup_test_env
}

test_run_setup_commands_skip() {
    echo "Testing run_setup_commands with HYDRA_SKIP_SETUP..."

    setup_test_env

    # Create config
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - touch should_not_exist.txt
EOF

    HYDRA_SKIP_SETUP=1
    export HYDRA_SKIP_SETUP

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should succeed with skip flag"

    # File should NOT be created
    [ ! -f "$TEST_DIR/worktree/should_not_exist.txt" ]
    assert_success $? "Setup should be skipped"

    cleanup_test_env
}

test_run_setup_commands_no_config() {
    echo "Testing run_setup_commands with no config file..."

    setup_test_env

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should succeed when no config exists"

    cleanup_test_env
}

test_run_setup_commands_runs_in_worktree() {
    echo "Testing run_setup_commands executes in worktree directory..."

    setup_test_env

    # Create config that writes pwd to a file
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - pwd > pwd_output.txt
EOF

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should succeed"

    # Check the pwd was the worktree
    if [ -f "$TEST_DIR/worktree/pwd_output.txt" ]; then
        pwd_result="$(cat "$TEST_DIR/worktree/pwd_output.txt")"
        echo "$pwd_result" | grep -q "worktree"
        assert_success $? "Command should run in worktree directory"
    else
        assert_failure 1 "pwd_output.txt should be created"
    fi

    cleanup_test_env
}

test_run_setup_commands_repo_config() {
    echo "Testing run_setup_commands uses repo config when worktree has none..."

    setup_test_env

    # Create config in repo, not worktree
    cat > "$TEST_DIR/repo/.hydra/config.yml" <<'EOF'
setup:
  - touch from_repo.txt
EOF

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should use repo config"

    [ -f "$TEST_DIR/worktree/from_repo.txt" ]
    assert_success $? "Should execute commands from repo config"

    cleanup_test_env
}

test_run_setup_commands_worktree_precedence() {
    echo "Testing run_setup_commands prefers worktree config over repo..."

    setup_test_env

    # Create config in both locations
    cat > "$TEST_DIR/worktree/.hydra/config.yml" <<'EOF'
setup:
  - touch from_worktree.txt
EOF

    cat > "$TEST_DIR/repo/.hydra/config.yml" <<'EOF'
setup:
  - touch from_repo.txt
EOF

    run_setup_commands "$TEST_DIR/worktree" "$TEST_DIR/repo" 2>/dev/null
    assert_success $? "Should succeed"

    [ -f "$TEST_DIR/worktree/from_worktree.txt" ]
    assert_success $? "Should use worktree config (higher precedence)"

    [ ! -f "$TEST_DIR/worktree/from_repo.txt" ]
    assert_success $? "Should NOT use repo config when worktree has config"

    cleanup_test_env
}

# =============================================================================
# Run Tests
# =============================================================================

echo "Running setup_commands unit tests..."
echo "====================================="

test_parse_setup_commands_basic
test_parse_setup_commands_no_setup
test_parse_setup_commands_empty_setup
test_parse_setup_commands_nonexistent_file
test_run_setup_commands_success
test_run_setup_commands_failure_aborts
test_run_setup_commands_continue_on_failure
test_run_setup_commands_skip
test_run_setup_commands_no_config
test_run_setup_commands_runs_in_worktree
test_run_setup_commands_repo_config
test_run_setup_commands_worktree_precedence

echo ""
echo "====================================="
echo "Test Results:"
echo "  Total:  $test_count"
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
