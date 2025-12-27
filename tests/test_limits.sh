#!/bin/sh
# Unit tests for lib/limits.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test and its dependencies
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"
# shellcheck source=../lib/limits.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/limits.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Global test directory for isolation
TEST_DIR=""

# Setup test environment - creates isolated HYDRA_HOME
# NOTE: Do not use $(setup_test_env) - call directly to preserve exports
setup_test_env() {
    # Create unique test directory
    TEST_DIR="$(mktemp -d)"
    HYDRA_HOME="$TEST_DIR"
    HYDRA_MAP="$TEST_DIR/map"
    export TEST_DIR HYDRA_HOME HYDRA_MAP
    mkdir -p "$TEST_DIR/queue"
    mkdir -p "$TEST_DIR/locks"
    touch "$HYDRA_MAP"
    # Reset state cache
    _STATE_CACHE_LOADED=""
}

cleanup_test_env() {
    if [ -n "$TEST_DIR" ] && [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
    TEST_DIR=""
    HYDRA_HOME=""
    HYDRA_MAP=""
    # Reset state cache after cleanup
    _STATE_CACHE_LOADED=""
}

# =============================================================================
# Configuration Tests
# =============================================================================

test_get_max_sessions() {
    echo "Testing get_max_sessions..."

    # Default (unlimited)
    unset HYDRA_MAX_SESSIONS
    result="$(get_max_sessions)"
    assert_equal "0" "$result" "Default should be 0 (unlimited)"

    # With limit set
    HYDRA_MAX_SESSIONS=5
    export HYDRA_MAX_SESSIONS
    result="$(get_max_sessions)"
    assert_equal "5" "$result" "Should return configured limit"

    unset HYDRA_MAX_SESSIONS
}

test_is_limit_enabled() {
    echo "Testing is_limit_enabled..."

    unset HYDRA_MAX_SESSIONS
    is_limit_enabled
    assert_failure $? "Should return false when no limit"

    HYDRA_MAX_SESSIONS=10
    export HYDRA_MAX_SESSIONS
    is_limit_enabled
    assert_success $? "Should return true when limit > 0"

    HYDRA_MAX_SESSIONS=0
    export HYDRA_MAX_SESSIONS
    is_limit_enabled
    assert_failure $? "Should return false when limit = 0"

    unset HYDRA_MAX_SESSIONS
}

# =============================================================================
# Session Counting Tests
# =============================================================================

test_get_active_session_count_empty() {
    echo "Testing get_active_session_count with empty map..."

    setup_test_env

    # Empty map
    result="$(get_active_session_count)"
    assert_equal "0" "$result" "Empty map should return 0"

    cleanup_test_env
}

test_get_active_session_count_with_sessions() {
    echo "Testing get_active_session_count with sessions..."

    setup_test_env

    # Add some sessions to map
    echo "branch1 session1 claude - - - -" > "$HYDRA_MAP"
    echo "branch2 session2 aider grp1 - - -" >> "$HYDRA_MAP"
    echo "branch3 session3 gemini - - - -" >> "$HYDRA_MAP"

    result="$(get_active_session_count)"
    assert_equal "3" "$result" "Should count all sessions"

    cleanup_test_env
}

# =============================================================================
# Limit Check Tests
# =============================================================================

test_would_exceed_limit_no_limit() {
    echo "Testing would_exceed_limit with no limit set..."

    setup_test_env
    unset HYDRA_MAX_SESSIONS

    would_exceed_limit 100
    assert_failure $? "Should not exceed when no limit set"

    cleanup_test_env
}

test_would_exceed_limit_under() {
    echo "Testing would_exceed_limit under limit..."

    setup_test_env
    HYDRA_MAX_SESSIONS=5
    export HYDRA_MAX_SESSIONS

    # Empty - should not exceed
    would_exceed_limit 3
    assert_failure $? "3 sessions should not exceed limit of 5 with 0 active"

    # Add 2 sessions
    echo "branch1 session1 - - - - -" > "$HYDRA_MAP"
    echo "branch2 session2 - - - - -" >> "$HYDRA_MAP"

    # 2 active + 2 requested = 4, should not exceed 5
    would_exceed_limit 2
    assert_failure $? "2 sessions should not exceed limit with 2 active"

    unset HYDRA_MAX_SESSIONS
    cleanup_test_env
}

test_would_exceed_limit_over() {
    echo "Testing would_exceed_limit over limit..."

    setup_test_env
    HYDRA_MAX_SESSIONS=5
    export HYDRA_MAX_SESSIONS

    # Add 2 sessions
    echo "branch1 session1 - - - - -" > "$HYDRA_MAP"
    echo "branch2 session2 - - - - -" >> "$HYDRA_MAP"

    # 2 active + 4 requested = 6, should exceed 5
    would_exceed_limit 4
    assert_success $? "4 sessions should exceed limit with 2 active"

    unset HYDRA_MAX_SESSIONS
    cleanup_test_env
}

test_get_available_capacity() {
    echo "Testing get_available_capacity..."

    setup_test_env

    # No limit
    unset HYDRA_MAX_SESSIONS
    result="$(get_available_capacity)"
    assert_equal "unlimited" "$result" "Should return unlimited when no limit"

    # With limit
    HYDRA_MAX_SESSIONS=5
    export HYDRA_MAX_SESSIONS

    result="$(get_available_capacity)"
    assert_equal "5" "$result" "Should return 5 with empty map and limit of 5"

    # Add 2 sessions
    echo "branch1 session1 - - - - -" > "$HYDRA_MAP"
    echo "branch2 session2 - - - - -" >> "$HYDRA_MAP"

    result="$(get_available_capacity)"
    assert_equal "3" "$result" "Should return 3 with 2 active and limit of 5"

    unset HYDRA_MAX_SESSIONS
    cleanup_test_env
}

# =============================================================================
# Queue Tests
# =============================================================================

test_queue_spawn() {
    echo "Testing queue_spawn..."

    setup_test_env

    result="$(queue_spawn "feature-test" "claude" "mygroup" "default" "50")"
    assert_success $? "queue_spawn should succeed"

    # Check file was created
    count="$(get_queue_count)"
    assert_equal "1" "$count" "Should have 1 queued entry"

    # Check file contents
    qfile="$(find "$HYDRA_HOME/queue" -name "*.queue" -type f | head -1)"
    if [ -f "$qfile" ]; then
        grep -q "branch=feature-test" "$qfile"
        assert_success $? "Queue file should contain branch"
        grep -q "ai_tool=claude" "$qfile"
        assert_success $? "Queue file should contain ai_tool"
        grep -q "group=mygroup" "$qfile"
        assert_success $? "Queue file should contain group"
    fi

    cleanup_test_env
}

test_queue_spawn_multiple() {
    echo "Testing queue_spawn with multiple entries..."

    setup_test_env

    queue_spawn "feature-1" "claude" "" "default" "50" >/dev/null
    queue_spawn "feature-2" "aider" "" "default" "30" >/dev/null
    queue_spawn "feature-3" "gemini" "" "default" "70" >/dev/null

    count="$(get_queue_count)"
    assert_equal "3" "$count" "Should have 3 queued entries"

    cleanup_test_env
}

test_dequeue_spawn() {
    echo "Testing dequeue_spawn..."

    setup_test_env

    queue_spawn "feature-1" "claude" "" "default" "50" >/dev/null
    queue_spawn "feature-2" "aider" "" "default" "50" >/dev/null

    count="$(get_queue_count)"
    assert_equal "2" "$count" "Should have 2 entries"

    dequeue_spawn "feature-1"
    assert_success $? "dequeue_spawn should succeed for existing entry"

    count="$(get_queue_count)"
    assert_equal "1" "$count" "Should have 1 entry after removal"

    dequeue_spawn "nonexistent"
    assert_failure $? "dequeue_spawn should fail for nonexistent entry"

    cleanup_test_env
}

test_clear_queue() {
    echo "Testing clear_queue..."

    setup_test_env

    queue_spawn "feature-1" "claude" "" "default" "50" >/dev/null
    queue_spawn "feature-2" "aider" "" "default" "50" >/dev/null
    queue_spawn "feature-3" "gemini" "" "default" "50" >/dev/null

    result="$(clear_queue)"
    assert_equal "3" "$result" "clear_queue should return count of cleared entries"

    count="$(get_queue_count)"
    assert_equal "0" "$count" "Queue should be empty after clear"

    cleanup_test_env
}

test_list_queue_empty() {
    echo "Testing list_queue with empty queue..."

    setup_test_env

    result="$(list_queue)"
    echo "$result" | grep -q "No pending spawns"
    assert_success $? "Empty queue should show no pending message"

    cleanup_test_env
}

test_list_queue_json() {
    echo "Testing list_queue --json..."

    setup_test_env

    queue_spawn "feature-1" "claude" "" "default" "50" >/dev/null

    result="$(list_queue --json)"
    echo "$result" | grep -q '"queue"'
    assert_success $? "JSON output should have queue key"
    echo "$result" | grep -q '"branch":"feature-1"'
    assert_success $? "JSON output should contain branch"

    cleanup_test_env
}

# =============================================================================
# Run Tests
# =============================================================================

echo "Running limits.sh unit tests..."
echo "================================"

test_get_max_sessions
test_is_limit_enabled
test_get_active_session_count_empty
test_get_active_session_count_with_sessions
test_would_exceed_limit_no_limit
test_would_exceed_limit_under
test_would_exceed_limit_over
test_get_available_capacity
test_queue_spawn
test_queue_spawn_multiple
test_dequeue_spawn
test_clear_queue
test_list_queue_empty
test_list_queue_json

echo ""
echo "================================"
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
