#!/bin/sh
# Unit tests for lib/deps.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source dependencies
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh"
# shellcheck source=../lib/deps.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/deps.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Setup test environment
TEST_DIR=""
setup_test_env() {
    TEST_DIR="$(mktemp -d)"
    mkdir -p "$TEST_DIR"
    HYDRA_HOME="$TEST_DIR"
    HYDRA_MAP="$TEST_DIR/map"
    export HYDRA_HOME HYDRA_MAP
    touch "$HYDRA_MAP"
}

cleanup_test_env() {
    test_dir="$1"
    rm -rf "$test_dir"
    unset HYDRA_HOME HYDRA_MAP
    TEST_DIR=""
}

# =============================================================================
# Unit Tests for validate_deps_spec
# =============================================================================

test_validate_deps_spec_empty() {
    echo ""
    echo "Testing validate_deps_spec with empty spec..."

    setup_test_env
    test_dir="$TEST_DIR"

    validate_deps_spec "" 2>/dev/null
    assert_failure $? "validate_deps_spec should fail with empty spec"

    cleanup_test_env "$test_dir"
}

test_validate_deps_spec_single() {
    echo ""
    echo "Testing validate_deps_spec with single branch..."

    setup_test_env
    test_dir="$TEST_DIR"

    validate_deps_spec "feature-branch" 2>/dev/null
    assert_success $? "validate_deps_spec should succeed with single branch"

    cleanup_test_env "$test_dir"
}

test_validate_deps_spec_multiple() {
    echo ""
    echo "Testing validate_deps_spec with multiple branches..."

    setup_test_env
    test_dir="$TEST_DIR"

    validate_deps_spec "branch1,branch2,branch3" 2>/dev/null
    assert_success $? "validate_deps_spec should succeed with multiple branches"

    cleanup_test_env "$test_dir"
}

test_validate_deps_spec_invalid_chars() {
    echo ""
    echo "Testing validate_deps_spec with invalid characters..."

    setup_test_env
    test_dir="$TEST_DIR"

    validate_deps_spec "branch;rm -rf /" 2>/dev/null
    assert_failure $? "validate_deps_spec should fail with semicolon"

    validate_deps_spec "branch\`echo\`" 2>/dev/null
    assert_failure $? "validate_deps_spec should fail with backticks"

    validate_deps_spec "branch|cat" 2>/dev/null
    assert_failure $? "validate_deps_spec should fail with pipe"

    cleanup_test_env "$test_dir"
}

test_validate_deps_spec_empty_in_list() {
    echo ""
    echo "Testing validate_deps_spec with empty entry in list..."

    setup_test_env
    test_dir="$TEST_DIR"

    validate_deps_spec "branch1,,branch2" 2>/dev/null
    assert_failure $? "validate_deps_spec should fail with empty entry"

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Unit Tests for check_circular_deps
# =============================================================================

test_check_circular_deps_no_deps() {
    echo ""
    echo "Testing check_circular_deps with no dependencies..."

    setup_test_env
    test_dir="$TEST_DIR"

    check_circular_deps "branch" "" 2>/dev/null
    assert_success $? "check_circular_deps should succeed with no deps"

    cleanup_test_env "$test_dir"
}

test_check_circular_deps_self_reference() {
    echo ""
    echo "Testing check_circular_deps with self-reference..."

    setup_test_env
    test_dir="$TEST_DIR"

    check_circular_deps "branch1" "branch1" 2>/dev/null
    assert_failure $? "check_circular_deps should fail when branch depends on itself"

    cleanup_test_env "$test_dir"
}

test_check_circular_deps_no_cycle() {
    echo ""
    echo "Testing check_circular_deps with no cycle..."

    setup_test_env
    test_dir="$TEST_DIR"

    check_circular_deps "branch3" "branch1,branch2" 2>/dev/null
    assert_success $? "check_circular_deps should succeed with no cycle"

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Unit Tests for is_dep_complete
# =============================================================================

test_is_dep_complete_no_session() {
    echo ""
    echo "Testing is_dep_complete with no session..."

    setup_test_env
    test_dir="$TEST_DIR"

    # Missing durable state cannot satisfy a dependency.
    is_dep_complete "nonexistent-branch" 2>/dev/null
    assert_failure $? "is_dep_complete should reject a non-existent branch"

    cleanup_test_env "$test_dir"
}

test_is_dep_complete_with_mapping() {
    echo ""
    echo "Testing is_dep_complete with active mapping..."

    setup_test_env
    test_dir="$TEST_DIR"

    # Add a mapping (session won't actually exist, but mapping does)
    echo "active-branch active-session - - - - -" >> "$HYDRA_MAP"

    # A dead/missing tmux session is not lifecycle evidence.
    is_dep_complete "active-branch" 2>/dev/null
    assert_failure $? "is_dep_complete does not treat session disappearance as success"

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Unit Tests for check_deps_complete
# =============================================================================

test_check_deps_complete_empty() {
    echo ""
    echo "Testing check_deps_complete with empty deps..."

    setup_test_env
    test_dir="$TEST_DIR"

    check_deps_complete "" 2>/dev/null
    assert_success $? "check_deps_complete should succeed with empty deps"

    cleanup_test_env "$test_dir"
}

test_check_deps_complete_dash() {
    echo ""
    echo "Testing check_deps_complete with dash (no deps)..."

    setup_test_env
    test_dir="$TEST_DIR"

    check_deps_complete "-" 2>/dev/null
    assert_success $? "check_deps_complete should succeed with dash"

    cleanup_test_env "$test_dir"
}

test_check_deps_complete_nonexistent() {
    echo ""
    echo "Testing check_deps_complete with non-existent branches..."

    setup_test_env
    test_dir="$TEST_DIR"

    # Non-existent branches have no sessions, so they're complete
    check_deps_complete "nonexistent1,nonexistent2" 2>/dev/null
    assert_failure $? "check_deps_complete should keep non-existent branches pending"

    cleanup_test_env "$test_dir"
}

# =============================================================================
# Run all tests
# =============================================================================

echo "Running deps.sh unit tests..."
echo "================================"

test_validate_deps_spec_empty
test_validate_deps_spec_single
test_validate_deps_spec_multiple
test_validate_deps_spec_invalid_chars
test_validate_deps_spec_empty_in_list
test_check_circular_deps_no_deps
test_check_circular_deps_self_reference
test_check_circular_deps_no_cycle
test_is_dep_complete_no_session
test_is_dep_complete_with_mapping
test_check_deps_complete_empty
test_check_deps_complete_dash
test_check_deps_complete_nonexistent

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
