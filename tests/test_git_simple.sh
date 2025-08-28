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
# shellcheck source=../lib/git.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/git.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

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

# Test find_worktree_path parameter validation
test_find_worktree_path_validation() {
    echo "Testing find_worktree_path parameter validation..."
    
    # Test empty branch name
    result="$(find_worktree_path "" 2>/dev/null)"
    exit_code=$?
    assert_failure $exit_code "find_worktree_path should fail with empty branch name"
    assert_equal "" "$result" "find_worktree_path should return empty for empty branch"
}

# Test find_worktree_path with actual worktrees
test_find_worktree_path_integration() {
    echo "Testing find_worktree_path integration..."
    
    # Only run if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "⚠ Skipping integration tests - not in a git repository"
        return
    fi
    
    # Get current branch
    current_branch="$(git branch --show-current 2>/dev/null || echo "main")"
    if [ -n "$current_branch" ]; then
        # Test finding current worktree
        worktree_path="$(find_worktree_path "$current_branch" 2>/dev/null)"
        exit_code=$?
        assert_success $exit_code "find_worktree_path should succeed for current branch"
        
        # Verify we got a non-empty path
        if [ -n "$worktree_path" ]; then
            assert_success 0 "find_worktree_path returned a path for current branch"
        else
            assert_failure 1 "find_worktree_path returned empty path for current branch"
        fi
    fi
    
    # Test non-existent branch
    fake_branch="non-existent-branch-$(date +%s)"
    result="$(find_worktree_path "$fake_branch" 2>/dev/null)"
    exit_code=$?
    assert_failure $exit_code "find_worktree_path should fail for non-existent branch"
    assert_equal "" "$result" "find_worktree_path should return empty for non-existent branch"
}

# Advanced refs support (safe charset relaxed, core safety retained)
test_advanced_refs_mode() {
    echo "Testing advanced refs mode..."
    
    # Allow '+' in branch names when advanced refs enabled
    export HYDRA_ALLOW_ADVANCED_REFS=1
    validate_branch_name "feature+plus" 2>/dev/null
    assert_success $? "validate_branch_name should allow '+' when HYDRA_ALLOW_ADVANCED_REFS=1"
    
    # Still reject whitespace even in advanced mode
    validate_branch_name "feature bad" 2>/dev/null
    assert_failure $? "validate_branch_name should still reject whitespace in advanced mode"
    
    # Allow '+' in worktree path check when advanced refs enabled
    validate_worktree_path "/tmp/hydra-feature+plus" 2>/dev/null
    assert_success $? "validate_worktree_path should allow '+' when HYDRA_ALLOW_ADVANCED_REFS=1"
    
    unset HYDRA_ALLOW_ADVANCED_REFS
}

# Run all tests
echo "Running git.sh unit tests (edge cases and validation)..."
echo "================================================"

test_git_branch_exists_edge_cases
test_create_worktree_validation
test_delete_worktree_validation
test_get_worktree_branch_validation
test_find_worktree_path_validation
test_find_worktree_path_integration
test_git_functions_integration
test_advanced_refs_mode

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
