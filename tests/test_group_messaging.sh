#!/bin/sh
# Tests for enhanced group workflows and cross-session messaging
# POSIX-compliant test framework
# shellcheck disable=SC1091

set -eu

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# Setup test environment
setup_test_env() {
    TEST_HOME="$(mktemp -d)"
    export TEST_HOME
    export HYDRA_HOME="$TEST_HOME/.hydra"
    export HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"
    : > "$HYDRA_MAP"

    # Source required libraries
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    . "$SCRIPT_DIR/../lib/locks.sh"
    . "$SCRIPT_DIR/../lib/state.sh"
    . "$SCRIPT_DIR/../lib/output.sh"
    . "$SCRIPT_DIR/../lib/messages.sh"
}

cleanup_test_env() {
    rm -rf "$TEST_HOME" 2>/dev/null || true
}

assert_equal() {
    expected="$1"
    actual="$2"
    msg="$3"
    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        printf "${GREEN}[PASS]${NC} %s\n" "$msg"
        pass_count=$((pass_count + 1))
    else
        printf "${RED}[FAIL]${NC} %s (expected '%s', got '%s')\n" "$msg" "$expected" "$actual"
        fail_count=$((fail_count + 1))
    fi
}

assert_success() {
    result="$1"
    msg="$2"
    test_count=$((test_count + 1))
    if [ "$result" -eq 0 ]; then
        printf "${GREEN}[PASS]${NC} %s\n" "$msg"
        pass_count=$((pass_count + 1))
    else
        printf "${RED}[FAIL]${NC} %s (expected success, got %s)\n" "$msg" "$result"
        fail_count=$((fail_count + 1))
    fi
}

assert_failure() {
    result="$1"
    msg="$2"
    test_count=$((test_count + 1))
    if [ "$result" -ne 0 ]; then
        printf "${GREEN}[PASS]${NC} %s\n" "$msg"
        pass_count=$((pass_count + 1))
    else
        printf "${RED}[FAIL]${NC} %s (expected failure, got success)\n" "$msg"
        fail_count=$((fail_count + 1))
    fi
}

assert_contains() {
    haystack="$1"
    needle="$2"
    msg="$3"
    test_count=$((test_count + 1))
    case "$haystack" in
        *"$needle"*)
            printf "${GREEN}[PASS]${NC} %s\n" "$msg"
            pass_count=$((pass_count + 1))
            ;;
        *)
            printf "${RED}[FAIL]${NC} %s (output does not contain '%s')\n" "$msg" "$needle"
            fail_count=$((fail_count + 1))
            ;;
    esac
}

# =============================================================================
# Message Queue Tests
# =============================================================================

test_get_message_dir() {
    echo "Testing get_message_dir..."
    setup_test_env

    result="$(get_message_dir "feature-a")"
    expected="$HYDRA_HOME/messages/feature-a"
    assert_equal "$expected" "$result" "get_message_dir returns correct path"

    # Test branch name sanitization
    result="$(get_message_dir "feature/test-branch")"
    expected="$HYDRA_HOME/messages/feature_test-branch"
    assert_equal "$expected" "$result" "get_message_dir sanitizes branch names"

    # Test empty branch
    get_message_dir "" 2>/dev/null && result=0 || result=1
    assert_failure "$result" "get_message_dir fails with empty branch"

    cleanup_test_env
}

test_ensure_message_dir() {
    echo "Testing ensure_message_dir..."
    setup_test_env

    ensure_message_dir "test-branch"
    result=$?
    assert_success "$result" "ensure_message_dir succeeds"

    msg_dir="$(get_message_dir "test-branch")"
    if [ -d "$msg_dir/queue" ] && [ -d "$msg_dir/archive" ]; then
        result=0
    else
        result=1
    fi
    assert_success "$result" "Message directories created"

    cleanup_test_env
}

test_send_message() {
    echo "Testing send_message..."
    setup_test_env

    # Send a message
    send_message "target-branch" "Hello world" "sender-branch"
    result=$?
    assert_success "$result" "send_message succeeds"

    # Verify message file exists
    msg_dir="$(get_message_dir "target-branch")"
    msg_count=0
    for f in "$msg_dir/queue"/*; do
        [ -f "$f" ] && msg_count=$((msg_count + 1))
    done
    assert_equal "1" "$msg_count" "One message in queue"

    # Verify message content
    msg_file=""
    for f in "$msg_dir/queue"/*; do
        [ -f "$f" ] && msg_file="$f" && break
    done
    content=$(cat "$msg_file")
    assert_equal "Hello world" "$content" "Message content matches"

    # Test empty parameters
    send_message "" "message" 2>/dev/null && result=0 || result=1
    assert_failure "$result" "send_message fails with empty target"

    send_message "branch" "" 2>/dev/null && result=0 || result=1
    assert_failure "$result" "send_message fails with empty message"

    cleanup_test_env
}

test_recv_messages() {
    echo "Testing recv_messages..."
    setup_test_env

    # Setup: create test messages
    ensure_message_dir "recv-test"
    msg_dir="$(get_message_dir "recv-test")"
    echo "Message 1" > "$msg_dir/queue/1735344000_sender1_123"
    echo "Message 2" > "$msg_dir/queue/1735344001_sender2_456"

    # Test receiving messages
    output=$(recv_messages "recv-test")
    result=$?
    assert_success "$result" "recv_messages succeeds"
    assert_contains "$output" "FROM sender1: Message 1" "First message received"
    assert_contains "$output" "FROM sender2: Message 2" "Second message received"

    # Messages should be removed (not peek mode)
    msg_count=0
    for f in "$msg_dir/queue"/*; do
        [ -f "$f" ] && msg_count=$((msg_count + 1))
    done
    assert_equal "0" "$msg_count" "Messages removed after recv"

    # Test peek mode
    ensure_message_dir "peek-test"
    msg_dir="$(get_message_dir "peek-test")"
    echo "Peek message" > "$msg_dir/queue/1735344000_sender_789"

    recv_messages "peek-test" --peek >/dev/null
    msg_count=0
    for f in "$msg_dir/queue"/*; do
        [ -f "$f" ] && msg_count=$((msg_count + 1))
    done
    assert_equal "1" "$msg_count" "Messages remain in peek mode"

    cleanup_test_env
}

test_count_messages() {
    echo "Testing count_messages..."
    setup_test_env

    # Setup
    ensure_message_dir "count-test"
    msg_dir="$(get_message_dir "count-test")"
    echo "msg1" > "$msg_dir/queue/1_a_1"
    echo "msg2" > "$msg_dir/queue/2_b_2"
    echo "msg3" > "$msg_dir/queue/3_c_3"

    result=$(count_messages "count-test")
    assert_equal "3" "$result" "count_messages returns correct count"

    # Test empty queue
    ensure_message_dir "empty-test"
    result=$(count_messages "empty-test")
    assert_equal "0" "$result" "count_messages returns 0 for empty queue"

    cleanup_test_env
}

# =============================================================================
# Group Workflow Tests (CLI validation only - no tmux required)
# =============================================================================

test_group_create_validation() {
    echo "Testing group create validation..."
    HYDRA_BIN="$(cd "$(dirname "$0")" && pwd)/../bin/hydra"

    # Test missing arguments
    output="$("$HYDRA_BIN" group create 2>&1)" || true
    assert_contains "$output" "Error" "group create fails without arguments"

    # Test invalid group name
    output="$("$HYDRA_BIN" group create 'bad@name!' branch1 2>&1)" || true
    assert_contains "$output" "Invalid group name" "group create rejects invalid name"
}

test_group_wait_validation() {
    echo "Testing group wait validation..."
    HYDRA_BIN="$(cd "$(dirname "$0")" && pwd)/../bin/hydra"

    # Test missing arguments
    output="$("$HYDRA_BIN" group wait 2>&1)" || true
    assert_contains "$output" "Error" "group wait fails without arguments"

    # Test empty group (should succeed immediately)
    setup_test_env
    output="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" group wait nonexistent 2>&1)"
    assert_contains "$output" "No sessions" "group wait handles empty group"
    cleanup_test_env
}

test_group_status_validation() {
    echo "Testing group status validation..."
    HYDRA_BIN="$(cd "$(dirname "$0")" && pwd)/../bin/hydra"

    # Test missing arguments
    output="$("$HYDRA_BIN" group status 2>&1)" || true
    assert_contains "$output" "Error" "group status fails without arguments"

    # Test empty group
    setup_test_env
    output="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" group status nonexistent 2>&1)"
    assert_contains "$output" "No sessions" "group status handles empty group"

    # Test empty group JSON output
    output="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" group status nonexistent --json 2>&1)"
    assert_contains "$output" '"sessions": []' "group status --json handles empty group"
    cleanup_test_env
}

test_send_recv_validation() {
    echo "Testing send/recv command validation..."
    HYDRA_BIN="$(cd "$(dirname "$0")" && pwd)/../bin/hydra"

    # Test send missing arguments
    output="$("$HYDRA_BIN" send 2>&1)" || true
    assert_contains "$output" "Error" "send fails without arguments"

    output="$("$HYDRA_BIN" send branch-only 2>&1)" || true
    assert_contains "$output" "Error" "send fails without message"

    # Test recv outside tmux
    output="$("$HYDRA_BIN" recv 2>&1)" || true
    assert_contains "$output" "Error" "recv fails outside tmux session"
}

# =============================================================================
# Run Tests
# =============================================================================

echo "Running enhanced group workflows and messaging tests..."
echo "========================================================"
echo ""

echo "--- Message Queue Unit Tests ---"
test_get_message_dir
test_ensure_message_dir
test_send_message
test_recv_messages
test_count_messages

echo ""
echo "--- Group Workflow Validation Tests ---"
test_group_create_validation
test_group_wait_validation
test_group_status_validation

echo ""
echo "--- Send/Recv Validation Tests ---"
test_send_recv_validation

echo ""
echo "========================================================"
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
