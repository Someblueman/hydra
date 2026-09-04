#!/bin/sh
# Unit tests for lib/limits.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0
HYDRA_HOME="${TMPDIR:-/tmp}/hydra-limits-unused"

# Source the library under test and its dependencies
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"
# shellcheck source=../lib/output.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/output.sh"
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/identity.sh"
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state_v2.sh"
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"
# shellcheck source=../lib/limits.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/limits.sh"

# Stub tmux for session counting tests (no tmux server required)
# shellcheck disable=SC2317
# shellcheck disable=SC2329
tmux_session_exists() {
    return 0
}

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
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    TEST_PROJECT_ID=project_0123456789abcdefabcd
    export TEST_DIR HYDRA_HOME HYDRA_STATE_V2_ROOT TEST_PROJECT_ID
    mkdir -p "$TEST_DIR/locks"
    state_v2_init_project "$TEST_PROJECT_ID" "$TEST_DIR/repo"
}

# Called indirectly by the queue and state libraries.
# shellcheck disable=SC2317,SC2329
hydra_get_project_id() { printf '%s\n' "$TEST_PROJECT_ID"; }

add_test_head() {
    state_v2_create_head "$TEST_PROJECT_ID" "$1" "$2" "${3:--}" "${4:--}" 100 - - "$TEST_DIR/repo" >/dev/null
}

cleanup_test_env() {
    if [ -n "$TEST_DIR" ] && [ -d "$TEST_DIR" ]; then
        rm -rf "$TEST_DIR"
    fi
    TEST_DIR=""
    HYDRA_HOME=""
    HYDRA_STATE_V2_ROOT=""
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
    echo "Testing get_active_session_count with no active heads..."

    setup_test_env

    result="$(get_active_session_count)"
    assert_equal "0" "$result" "No active heads should return 0"

    cleanup_test_env
}

test_get_active_session_count_with_sessions() {
    echo "Testing get_active_session_count with sessions..."

    setup_test_env

    add_test_head branch1 session1 claude
    add_test_head branch2 session2 aider grp1
    add_test_head branch3 session3 gemini

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
    add_test_head branch1 session1
    add_test_head branch2 session2

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
    add_test_head branch1 session1
    add_test_head branch2 session2

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
    assert_equal "5" "$result" "Should return 5 with no active heads and limit of 5"

    # Add 2 sessions
    add_test_head branch1 session1
    add_test_head branch2 session2

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
    qfile="$(find "$(_get_queue_dir)" -name "*.queue" -type f | head -1)"
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

test_queue_atomic_and_serial_processing() {
    echo "Testing atomic queue publication and serialized processing..."
    setup_test_env

    # shellcheck disable=SC2317,SC2329
    ( atomic_replace() { return 1; }; queue_spawn "not-published" none "" default 50 >/dev/null 2>&1 )
    assert_failure $? "failed atomic publication rejects the queue entry"
    assert_equal "0" "$(get_queue_count)" "failed publication exposes no partial queue file"

    queue_spawn "only-once" none "" default 50 >/dev/null
    spawn_log="$TEST_DIR/spawn.log"
    release_file="$TEST_DIR/release-process"
    second_code="$TEST_DIR/second.code"
    (
        # shellcheck disable=SC2317,SC2329
        spawn_single() {
            printf 'attempt\n' >> "$spawn_log"
            while [ ! -f "$release_file" ]; do sleep 0.05; done
            return 1
        }
        HYDRA_LOCK_RETRIES=20 process_spawn_queue >/dev/null 2>&1 &
        first_pid=$!
        while [ ! -s "$spawn_log" ]; do sleep 0.05; done
        ( HYDRA_LOCK_RETRIES=1 process_spawn_queue >/dev/null 2>&1; printf '%s\n' "$?" > "$second_code" ) &
        second_pid=$!
        sleep 0.2
        : > "$release_file"
        wait "$first_pid"
        wait "$second_pid"
    )
    assert_equal "1" "$(wc -l < "$spawn_log" | tr -d ' ')" "concurrent processors cannot attempt the same entry"
    assert_failure "$(sed -n '1p' "$second_code")" "a concurrent queue processor fails closed"
    assert_equal "1" "$(get_queue_count)" "failed processing retains the queued request"

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

test_queue_priority_order() {
    echo "Testing queue priority-before-time and FIFO..."

    setup_test_env

    queue_spawn "old-low" "claude" "" "default" "10" >/dev/null
    queue_spawn "high" "claude" "" "default" "90" >/dev/null
    queue_spawn "new-low" "claude" "" "default" "10" >/dev/null

    queue_dir="$(_get_queue_dir)"
    order="$(find "$queue_dir" -maxdepth 1 -name "*.queue" -type f | sort | while IFS= read -r f; do
        grep '^branch=' "$f" | cut -d= -f2
    done | tr '\n' ' ')"
    assert_equal "high old-low new-low " "$order" "high priority before older low; FIFO within priority"

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
test_queue_atomic_and_serial_processing
test_dequeue_spawn
test_clear_queue
test_list_queue_empty
test_list_queue_json
test_queue_priority_order

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
