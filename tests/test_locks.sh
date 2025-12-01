#!/bin/sh
# Unit tests for lib/locks.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Set up test environment
test_home="$(mktemp -d)"
HYDRA_HOME="$test_home"
export HYDRA_HOME

# Source the library under test
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Cleanup function
cleanup_test_locks() {
    rm -rf "$test_home"
}

# Test try_lock function
test_try_lock() {
    echo "Testing try_lock..."

    # Test acquiring a lock
    try_lock "test_lock_1"
    assert_success $? "try_lock should succeed acquiring new lock"

    # Verify lock directory exists
    if [ -d "$HYDRA_HOME/locks/test_lock_1.lock" ]; then
        echo "[PASS] Lock directory should exist after try_lock"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Lock directory should exist after try_lock"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    # Test acquiring same lock fails (already held)
    try_lock "test_lock_1"
    assert_failure $? "try_lock should fail when lock already held"

    # Clean up
    release_lock "test_lock_1"
}

# Test release_lock function
test_release_lock() {
    echo "Testing release_lock..."

    # Acquire a lock first
    try_lock "test_lock_2"
    assert_success $? "Setup: acquire lock"

    # Release the lock
    release_lock "test_lock_2"
    assert_success $? "release_lock should succeed"

    # Verify lock directory is removed
    if [ ! -d "$HYDRA_HOME/locks/test_lock_2.lock" ]; then
        echo "[PASS] Lock directory should be removed after release"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Lock directory should be removed after release"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    # Releasing non-existent lock should be safe
    release_lock "non_existent_lock"
    assert_success $? "release_lock should succeed for non-existent lock"
}

# Test cleanup_stale_locks function
test_cleanup_stale_locks() {
    echo "Testing cleanup_stale_locks..."

    # Create locks directory
    mkdir -p "$HYDRA_HOME/locks"

    # Create a "stale" lock (we'll test the function runs without error)
    mkdir -p "$HYDRA_HOME/locks/stale_test.lock"

    # Run cleanup (won't actually clean new lock, just tests the function)
    cleanup_stale_locks
    assert_success $? "cleanup_stale_locks should succeed"

    # Clean up
    rmdir "$HYDRA_HOME/locks/stale_test.lock" 2>/dev/null || true
}

# Test release_session_lock function
test_release_session_lock() {
    echo "Testing release_session_lock..."

    # Acquire a session lock
    try_lock "test_session"
    assert_success $? "Setup: acquire session lock"

    # Release using release_session_lock
    release_session_lock "test_session"
    assert_success $? "release_session_lock should succeed"

    # Verify lock is released
    if [ ! -d "$HYDRA_HOME/locks/test_session.lock" ]; then
        echo "[PASS] Session lock should be released"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Session lock should be released"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
}

# Test lock behavior without HYDRA_HOME
test_lock_without_home() {
    echo "Testing lock behavior without HYDRA_HOME..."

    saved_home="$HYDRA_HOME"
    unset HYDRA_HOME

    # try_lock should succeed (no-op) when HYDRA_HOME not set
    try_lock "no_home_lock"
    assert_success $? "try_lock should succeed when HYDRA_HOME not set"

    # cleanup_stale_locks should succeed when HYDRA_HOME not set
    cleanup_stale_locks
    assert_success $? "cleanup_stale_locks should succeed when HYDRA_HOME not set"

    HYDRA_HOME="$saved_home"
    export HYDRA_HOME
}

# Run all tests
echo "Running locks.sh unit tests..."
echo "================================"

test_try_lock
test_release_lock
test_cleanup_stale_locks
test_release_session_lock
test_lock_without_home

# Clean up
cleanup_test_locks

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
