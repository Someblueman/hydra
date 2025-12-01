#!/bin/sh
# Unit tests for lib/paths.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/paths.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/paths.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Test get_repo_root function
test_get_repo_root() {
    echo "Testing get_repo_root..."

    # Test in valid git repo
    result="$(get_repo_root)"
    assert_success $? "get_repo_root should succeed in git repo"

    if [ -d "$result/.git" ] || [ -f "$result/.git" ]; then
        echo "[PASS] get_repo_root should return valid git root"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] get_repo_root should return valid git root"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
}

# Test get_worktree_path_for_branch function
test_get_worktree_path_for_branch() {
    echo "Testing get_worktree_path_for_branch..."

    # Test parameter validation
    get_worktree_path_for_branch "" 2>/dev/null
    assert_failure $? "get_worktree_path_for_branch should fail with empty branch"

    # Test valid branch name
    result="$(get_worktree_path_for_branch "test-branch")"
    assert_success $? "get_worktree_path_for_branch should succeed with valid branch"

    # Check that result contains hydra-test-branch
    case "$result" in
        *hydra-test-branch)
            echo "[PASS] get_worktree_path_for_branch should include branch name"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] get_worktree_path_for_branch should include branch name"
            echo "  Got: $result"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test normalize_path function
test_normalize_path() {
    echo "Testing normalize_path..."

    # Test parameter validation
    normalize_path "" 2>/dev/null
    assert_failure $? "normalize_path should fail with empty path"

    # Test with existing directory
    result="$(normalize_path "/tmp")"
    assert_success $? "normalize_path should succeed with existing path"

    if [ "$result" = "/tmp" ] || [ "$result" = "/private/tmp" ]; then
        echo "[PASS] normalize_path should normalize existing path"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] normalize_path should normalize existing path"
        echo "  Got: $result"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    # Test with non-existent path
    result="$(normalize_path "/non/existent/path")"
    assert_success $? "normalize_path should succeed with non-existent path"
    assert_equal "/non/existent/path" "$result" "normalize_path should return unchanged non-existent path"
}

# Run all tests
echo "Running paths.sh unit tests..."
echo "================================"

test_get_repo_root
test_get_worktree_path_for_branch
test_normalize_path

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
