#!/bin/sh
# Unit tests for lib/maintenance.sh
# POSIX-compliant test framework

test_count=0
pass_count=0
fail_count=0

HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/tmux.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/messages.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/maintenance.sh"
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Stub tmux for maintenance tests
# shellcheck disable=SC2317
# shellcheck disable=SC2329
tmux_session_exists() {
    case "$1" in
        alive-session) return 0 ;;
        *) return 1 ;;
    esac
}

setup_env() {
    TEST_DIR="$(mktemp -d)"
    HYDRA_HOME="$TEST_DIR"
    HYDRA_MAP="$TEST_DIR/map"
    export HYDRA_HOME HYDRA_MAP
    mkdir -p "$HYDRA_HOME/locks"
    touch "$HYDRA_MAP"
    _STATE_CACHE_LOADED=""
}

cleanup_env() {
    rm -rf "$TEST_DIR"
}

test_count_stale_locks_mkdir_format() {
    echo "Testing count_stale_locks with mkdir lock dirs..."
    setup_env

    mkdir -p "$HYDRA_HOME/locks/fresh.lock"
    mkdir -p "$HYDRA_HOME/locks/old.lock"
    touch -t 202001010000 "$HYDRA_HOME/locks/old.lock"

    result="$(count_stale_locks)"
    assert_equal "1" "$result" "count_stale_locks should see the aged mkdir lock"

    cleaned="$(clean_stale_locks)"
    assert_equal "1" "$cleaned" "clean_stale_locks should remove the aged mkdir lock"

    if [ -d "$HYDRA_HOME/locks/fresh.lock" ]; then
        echo "[PASS] Fresh lock directory preserved"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Fresh lock directory preserved"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ ! -d "$HYDRA_HOME/locks/old.lock" ]; then
        echo "[PASS] Stale mkdir lock removed"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Stale mkdir lock removed"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_env
}

test_clean_dead_mappings_cleans_messages() {
    echo "Testing clean_dead_mappings removes message dirs..."
    setup_env

    echo "dead-branch dead-session - - - - -" > "$HYDRA_MAP"
    echo "live-branch alive-session - - - - -" >> "$HYDRA_MAP"
    mkdir -p "$HYDRA_HOME/messages/dead-branch/queue"
    echo "msg" > "$HYDRA_HOME/messages/dead-branch/queue/1"
    mkdir -p "$HYDRA_HOME/messages/live-branch/queue"

    result="$(clean_dead_mappings)"
    assert_equal "1" "$result" "One dead mapping removed"

    if [ ! -d "$HYDRA_HOME/messages/dead-branch" ]; then
        echo "[PASS] Message dir removed for dead mapping"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Message dir removed for dead mapping"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if grep -q "live-branch" "$HYDRA_MAP"; then
        echo "[PASS] Live mapping preserved"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Live mapping preserved"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_env
}

test_branch_has_mapping_matches_literal_branch() {
    echo "Testing branch_has_mapping uses literal branch identity..."
    setup_env

    echo "feature/x alive-session - - - - -" > "$HYDRA_MAP"

    branch_has_mapping "feature/x"
    assert_success $? "Exact branch name is found"
    branch_has_mapping "feature.x"
    assert_failure $? "Regex-like branch name does not match a different branch"

    cleanup_env
}

echo "Running maintenance.sh unit tests..."
echo "================================"
test_count_stale_locks_mkdir_format
test_clean_dead_mappings_cleans_messages
test_branch_has_mapping_matches_literal_branch
echo "================================"
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
