#!/bin/sh
# Simple unit tests for lib/git.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Original working directory
original_dir="$(pwd)"

# Source the library under test
. "$(dirname "$0")/../lib/git.sh"

# Test helper functions
assert_equal() {
    expected="$1"
    actual="$2" 
    message="$3"
    
    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
    fi
}

assert_success() {
    exit_code="$1"
    message="$2"
    
    test_count=$((test_count + 1))
    if [ "$exit_code" -eq 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code $exit_code)"
    fi
}

assert_failure() {
    exit_code="$1"
    message="$2"
    
    test_count=$((test_count + 1))
    if [ "$exit_code" -ne 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: failure (non-zero exit code)"
        echo "  Actual:   success (exit code 0)"
    fi
}

# Test git_branch_exists function with empty/invalid inputs
test_git_branch_exists_edge_cases() {
    echo "Testing git_branch_exists edge cases..."
    
    # Test empty branch name
    git_branch_exists ""
    assert_failure $? "git_branch_exists should return failure for empty branch name"
    
    # Test with whitespace-only branch name
    git_branch_exists "   "
    assert_failure $? "git_branch_exists should return failure for whitespace-only branch name"
}

# Test create_worktree parameter validation
test_create_worktree_validation() {
    echo "Testing create_worktree parameter validation..."
    
    # Test empty parameters
    create_worktree "" "" 2>/dev/null
    assert_failure $? "create_worktree should fail with empty branch and path"
    
    create_worktree "branch" "" 2>/dev/null
    assert_failure $? "create_worktree should fail with empty path"
    
    create_worktree "" "/some/path" 2>/dev/null
    assert_failure $? "create_worktree should fail with empty branch"
}

# Test delete_worktree parameter validation
test_delete_worktree_validation() {
    echo "Testing delete_worktree parameter validation..."
    
    # Test empty path
    delete_worktree "" 2>/dev/null
    assert_failure $? "delete_worktree should fail with empty path"
    
    # Test non-existent path
    delete_worktree "/definitely/does/not/exist" 2>/dev/null
    assert_failure $? "delete_worktree should fail with non-existent path"
}

# Test get_worktree_branch parameter validation
test_get_worktree_branch_validation() {
    echo "Testing get_worktree_branch parameter validation..."
    
    # Test empty path
    get_worktree_branch "" 2>/dev/null
    assert_failure $? "get_worktree_branch should fail with empty path"
    
    # Test non-existent path
    get_worktree_branch "/definitely/does/not/exist" 2>/dev/null
    assert_failure $? "get_worktree_branch should fail with non-existent path"
}

# Integration test that actually creates a git repo
test_git_functions_integration() {
    echo "Testing git functions integration..."
    
    # Only run if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "⚠ Skipping integration tests - not in a git repository"
        return
    fi
    
    # Check if current branch exists
    current_branch="$(git branch --show-current 2>/dev/null || echo "main")"
    if [ -n "$current_branch" ]; then
        git_branch_exists "$current_branch"
        assert_success $? "git_branch_exists should succeed for current branch '$current_branch'"
    fi
    
    # Test non-existent branch
    git_branch_exists "definitely-does-not-exist-$(date +%s)"
    assert_failure $? "git_branch_exists should fail for non-existent branch"
}

# Run all tests
echo "Running git.sh unit tests (edge cases and validation)..."
echo "================================================"

test_git_branch_exists_edge_cases
test_create_worktree_validation
test_delete_worktree_validation
test_get_worktree_branch_validation
test_git_functions_integration

echo "================================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

# Restore original directory
cd "$original_dir" || exit 1

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi