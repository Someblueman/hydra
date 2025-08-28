#!/bin/sh
# Common test helper functions for Hydra tests (POSIX)

# Functions rely on global counters:
# - test_count, pass_count, fail_count

assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"

    test_count=$((test_count + 1))
    if [ "${expected}" = "${actual}" ]; then
        pass_count=$((pass_count + 1))
        echo "✓ ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "✗ ${message}"
        echo "  Expected: '${expected}'"
        echo "  Actual:   '${actual}'"
    fi
}

assert_success() {
    exit_code="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ "${exit_code}" -eq 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "✗ ${message}"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code ${exit_code})"
    fi
}

assert_failure() {
    exit_code="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ "${exit_code}" -ne 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ ${message}"
    else
        fail_count=$((fail_count + 1))
        echo "✗ ${message}"
        echo "  Expected: failure (non-zero exit code)"
        echo "  Actual:   success (exit code 0)"
    fi
}

