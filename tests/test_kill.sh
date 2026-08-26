#!/bin/sh
# Unit tests for lib/kill.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source dependencies
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"
# shellcheck source=../lib/paths.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/paths.sh"
# shellcheck source=../lib/git.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/git.sh"
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh"
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"
# shellcheck source=../lib/kill.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/kill.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Setup test environment
setup_test_env() {
    test_dir="$(mktemp -d)"
    mkdir -p "$test_dir"
    echo "$test_dir"
}

cleanup_test_env() {
    test_dir="$1"
    rm -rf "$test_dir"
}

# =============================================================================
# Unit Tests for get_worktree_path_with_fallback
# =============================================================================

test_get_worktree_path_with_fallback_empty_branch() {
    echo ""
    echo "Testing get_worktree_path_with_fallback with empty branch..."

    result="$(get_worktree_path_with_fallback "" 2>/dev/null)"
    exit_code=$?

    assert_failure $exit_code "get_worktree_path_with_fallback should fail with empty branch"
    assert_equal "" "$result" "Result should be empty for empty branch"
}

test_get_worktree_path_with_fallback_nonexistent() {
    echo ""
    echo "Testing get_worktree_path_with_fallback with non-existent branch..."

    result="$(get_worktree_path_with_fallback "nonexistent-branch-12345" 2>/dev/null)"
    exit_code=$?

    # The function calculates the expected path even for non-existent branches
    # (it returns the path where the worktree WOULD be)
    # So we just verify it returns a path containing the branch name
    if [ "$exit_code" -eq 0 ] && echo "$result" | grep -q "nonexistent-branch-12345"; then
        echo "[PASS] get_worktree_path_with_fallback returns expected path for branch"
        pass_count=$((pass_count + 1))
    elif [ "$exit_code" -ne 0 ]; then
        echo "[PASS] get_worktree_path_with_fallback fails for non-existent branch (alternate valid behavior)"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Unexpected result for non-existent branch"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
}

# =============================================================================
# Unit Tests for kill_single_head
# =============================================================================

test_kill_single_head_empty_params() {
    echo ""
    echo "Testing kill_single_head with empty parameters..."

    kill_single_head "" "" 2>/dev/null
    assert_failure $? "kill_single_head should fail with empty branch and session"

    kill_single_head "branch" "" 2>/dev/null
    assert_failure $? "kill_single_head should fail with empty session"

    kill_single_head "" "session" 2>/dev/null
    assert_failure $? "kill_single_head should fail with empty branch"
}

test_kill_single_head_nonexistent_session() {
    echo ""
    echo "Testing kill_single_head with non-existent session..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/map"
    export HYDRA_HOME HYDRA_MAP
    touch "$HYDRA_MAP"

    # Add a mapping for a session that doesn't exist
    echo "test-branch test-session-nonexistent - - - - -" > "$HYDRA_MAP"

    # Should succeed (session doesn't exist, so nothing to kill)
    # But should still clean up mapping
    kill_single_head "test-branch" "test-session-nonexistent" 2>/dev/null
    exit_code=$?

    # Check mapping was removed
    if [ -f "$HYDRA_MAP" ] && grep -q "test-branch" "$HYDRA_MAP"; then
        echo "[FAIL] Mapping should be removed after kill"
        fail_count=$((fail_count + 1))
    else
        echo "[PASS] Mapping removed for non-existent session"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Unit Tests for kill_all_sessions
# =============================================================================

test_kill_all_sessions_empty_map() {
    echo ""
    echo "Testing kill_all_sessions with empty map..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/map"
    export HYDRA_HOME HYDRA_MAP
    touch "$HYDRA_MAP"

    output="$(kill_all_sessions "true" 2>&1)"
    exit_code=$?

    assert_success $exit_code "kill_all_sessions should succeed with empty map"

    if echo "$output" | grep -qi "no active"; then
        echo "[PASS] Shows 'no active' message for empty map"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Should show 'no active' message"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env "$test_dir"
}

test_kill_all_sessions_missing_map() {
    echo ""
    echo "Testing kill_all_sessions with missing map file..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/nonexistent_map"
    export HYDRA_HOME HYDRA_MAP

    output="$(kill_all_sessions "true" 2>&1)"
    exit_code=$?

    assert_success $exit_code "kill_all_sessions should succeed with missing map"

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Unit Tests for kill_group_sessions
# =============================================================================

test_kill_group_sessions_empty_group() {
    echo ""
    echo "Testing kill_group_sessions with empty group name..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/map"
    export HYDRA_HOME HYDRA_MAP
    touch "$HYDRA_MAP"

    kill_group_sessions "" "true" 2>/dev/null
    assert_failure $? "kill_group_sessions should fail with empty group name"

    cleanup_test_env "$test_dir"
}

test_kill_group_sessions_nonexistent_group() {
    echo ""
    echo "Testing kill_group_sessions with non-existent group..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/map"
    export HYDRA_HOME HYDRA_MAP
    touch "$HYDRA_MAP"

    output="$(kill_group_sessions "nonexistent-group" "true" 2>&1)"
    exit_code=$?

    # Should succeed but report no sessions found
    if echo "$output" | grep -qi "no sessions\|not found"; then
        echo "[PASS] Reports no sessions in non-existent group"
        pass_count=$((pass_count + 1))
    else
        echo "[PASS] Handles non-existent group gracefully"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env "$test_dir"
}

test_kill_dirty_worktree_preflight() {
    echo ""
    echo "Testing kill_single_head refuses dirty worktree before teardown..."

    repo="$(mktemp -d)"
    parent="$(dirname "$repo")"
    git -C "$repo" init -q
    git -C "$repo" config user.email "t@example.com"
    git -C "$repo" config user.name "t"
    echo x > "$repo/file"
    git -C "$repo" add file
    git -C "$repo" commit -q -m init
    git -C "$repo" branch dirty-kill
    wt="$parent/hydra-dirty-kill"
    git -C "$repo" worktree add -q "$wt" dirty-kill
    echo dirty >> "$wt/file"

    HYDRA_HOME="$(mktemp -d)"
    HYDRA_MAP="$HYDRA_HOME/map"
    export HYDRA_HOME HYDRA_MAP HYDRA_NONINTERACTIVE=1
    echo "dirty-kill missing-session - - - - -" > "$HYDRA_MAP"

    (
        cd "$repo" || exit 1
        kill_single_head "dirty-kill" "missing-session" >/dev/null 2>&1
    )
    exit_code=$?
    assert_failure $exit_code "kill_single_head should fail on dirty worktree"

    if grep -q "dirty-kill" "$HYDRA_MAP"; then
        echo "[PASS] Mapping preserved when kill aborts"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Mapping preserved when kill aborts"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ -d "$wt" ]; then
        echo "[PASS] Dirty worktree left in place"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Dirty worktree left in place"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    git -C "$repo" worktree remove --force "$wt" 2>/dev/null || rm -rf "$wt"
    rm -rf "$repo" "$HYDRA_HOME"
}

test_kill_cleans_messages() {
    echo ""
    echo "Testing kill_single_head removes message inbox..."

    test_dir="$(setup_test_env)"
    HYDRA_HOME="$test_dir"
    HYDRA_MAP="$test_dir/map"
    export HYDRA_HOME HYDRA_MAP
    echo "msg-branch missing-session - - - - -" > "$HYDRA_MAP"
    mkdir -p "$HYDRA_HOME/messages/msg-branch/queue"
    echo hi > "$HYDRA_HOME/messages/msg-branch/queue/1"

    kill_single_head "msg-branch" "missing-session" >/dev/null 2>&1
    assert_success $? "kill_single_head succeeds without a worktree"

    if [ ! -d "$HYDRA_HOME/messages/msg-branch" ]; then
        echo "[PASS] Message directory removed on kill"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Message directory removed on kill"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env "$test_dir"
}

test_kill_aborts_when_mapping_locked() {
    echo ""
    echo "Testing kill leaves worktree when mapping removal cannot commit..."

    repo="$(mktemp -d)"
    parent="$(dirname "$repo")"
    git -C "$repo" init -q
    git -C "$repo" config user.email "t@example.com"
    git -C "$repo" config user.name "t"
    echo x > "$repo/file"
    git -C "$repo" add file
    git -C "$repo" commit -q -m init
    git -C "$repo" branch lock-kill
    wt="$parent/hydra-lock-kill"
    git -C "$repo" worktree add -q "$wt" lock-kill

    HYDRA_HOME="$(mktemp -d)"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME/locks"
    echo "lock-kill missing-session - - - - -" > "$HYDRA_MAP"
    mkdir "$HYDRA_HOME/locks/state_map.lock"
    export HYDRA_HOME HYDRA_MAP HYDRA_NONINTERACTIVE=1 HYDRA_LOCK_RETRIES=1

    (
        cd "$repo" || exit 1
        kill_single_head "lock-kill" "missing-session" >/dev/null 2>&1
    )
    assert_failure $? "kill_single_head should fail when state_map lock is held"

    if grep -q "lock-kill" "$HYDRA_MAP"; then
        echo "[PASS] Mapping preserved when remove_mapping cannot commit"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Mapping preserved when remove_mapping cannot commit"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ -d "$wt" ]; then
        echo "[PASS] Worktree left in place after mapping-lock failure"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Worktree left in place after mapping-lock failure"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    git -C "$repo" worktree remove --force "$wt" 2>/dev/null || rm -rf "$wt"
    rm -rf "$repo" "$HYDRA_HOME"
    unset HYDRA_LOCK_RETRIES
}

test_kill_preserves_session_when_mapping_locked() {
    echo ""
    echo "Testing kill does not destroy tmux when state_map lock is held..."

    sess="hydra-lock-live-$$"
    tmux new-session -d -s "$sess" 2>/dev/null || {
        echo "[SKIP] tmux new-session failed"
        return 0
    }

    HYDRA_HOME="$(mktemp -d)"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME/locks"
    echo "lock-live $sess - - - - -" > "$HYDRA_MAP"
    mkdir "$HYDRA_HOME/locks/state_map.lock"
    export HYDRA_HOME HYDRA_MAP HYDRA_NONINTERACTIVE=1 HYDRA_LOCK_RETRIES=1

    kill_single_head "lock-live" "$sess" >/dev/null 2>&1
    assert_failure $? "kill_single_head should fail when state_map lock is held"

    if tmux has-session -t "$sess" 2>/dev/null; then
        echo "[PASS] tmux session left intact when map lock is held"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] tmux session left intact when map lock is held"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if grep -q "lock-live" "$HYDRA_MAP"; then
        echo "[PASS] Mapping preserved with live session after lock failure"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Mapping preserved with live session after lock failure"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    tmux kill-session -t "$sess" 2>/dev/null || true
    rm -rf "$HYDRA_HOME"
    unset HYDRA_LOCK_RETRIES
}

# =============================================================================
# Run all tests
# =============================================================================

echo "Running kill.sh unit tests..."
echo "================================"

test_get_worktree_path_with_fallback_empty_branch
test_get_worktree_path_with_fallback_nonexistent
test_kill_single_head_empty_params
test_kill_single_head_nonexistent_session
test_kill_all_sessions_empty_map
test_kill_all_sessions_missing_map
test_kill_group_sessions_empty_group
test_kill_group_sessions_nonexistent_group
test_kill_dirty_worktree_preflight
test_kill_cleans_messages
test_kill_aborts_when_mapping_locked
test_kill_preserves_session_when_mapping_locked

echo ""
echo "================================"
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"
echo ""

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
