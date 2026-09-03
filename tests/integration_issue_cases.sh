#!/bin/sh
# Issue-branch integration case sourced by test_integration.sh

test_issue_branch_cleanup() {
    echo "Testing issue branch spawn and kill cycle..."
    
    # Only run if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        echo "[WARN] Skipping issue branch test - not in a git repository"
        return
    fi
    
    # Create isolated test environment
    setup_test_env
    # TEST_DIR is assigned by the parent test runner.
    # shellcheck disable=SC2153
    test_dir="$TEST_DIR"

    repo_parent="$test_dir/project space"
    repo_root="$repo_parent/repo"
    git init -q "$repo_root"
    git -C "$repo_root" config user.email "hydra-tests@example.invalid"
    git -C "$repo_root" config user.name "Hydra Tests"
    git -C "$repo_root" commit --allow-empty -q -m "initial"
    
    # Create a test branch name that looks like an issue branch
    test_branch="issue-999-test-cleanup-$(date +%s)"
    
    # Skip if tmux is not available
    if ! command -v tmux >/dev/null 2>&1; then
        echo "[WARN] Skipping issue branch test - tmux not available"
        cleanup_test_env "$test_dir"
        return
    fi
    
    # Create the branch and worktree
    echo "  Creating test branch '$test_branch'..."
    output="$(cd "$repo_root" && HYDRA_SKIP_AI=1 HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN" spawn "$test_branch" 2>&1)"
    exit_code=$?
    
    if [ "$exit_code" -ne 0 ]; then
        echo "[WARN] Skipping - spawn failed (might be in non-terminal environment)"
        cleanup_test_env "$test_dir"
        return
    fi

    expected_worktree="$(cd "$repo_root" && "$HYDRA_BIN" path "$test_branch")"
    
    # Check that worktree was created
    if [ -d "$expected_worktree" ]; then
        assert_success 0 "Worktree directory was created at expected location"
    else
        assert_failure 1 "Worktree directory was not created at expected location"
    fi
    
    # Check that branch exists in worktree list
    worktree_exists="$(git -C "$repo_root" worktree list | grep -c "$test_branch" || true)"
    if [ "$worktree_exists" -gt 0 ]; then
        assert_success 0 "Branch appears in git worktree list"
    else
        assert_failure 1 "Branch does not appear in git worktree list"
    fi
    
    # Now kill the branch
    echo "  Killing test branch '$test_branch'..."
    output="$(cd "$repo_root" && HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN" kill "$test_branch" 2>&1)"
    exit_code=$?
    echo "  Kill output: $output"
    echo "  Kill exit code: $exit_code"
    assert_success "$exit_code" "hydra kill should succeed"
    
    # Verify worktree was removed
    if [ ! -d "$expected_worktree" ]; then
        assert_success 0 "Worktree directory was successfully removed"
    else
        assert_failure 1 "Worktree directory still exists after kill"
    fi
    
    # Verify branch is no longer in worktree list
    worktree_exists="$(git -C "$repo_root" worktree list | grep -c "$test_branch" || true)"
    if [ "$worktree_exists" -eq 0 ]; then
        assert_success 0 "Branch no longer appears in git worktree list"
    else
        assert_failure 1 "Branch still appears in git worktree list after kill"
    fi
    
    # Verify the durable head is no longer active
    if ! (cd "$repo_root" && "$HYDRA_BIN" list --json | grep -Fq "\"branch\": \"$test_branch\""); then
        assert_success 0 "Branch was removed from active durable state"
    else
        assert_failure 1 "Branch remains in active durable state"
    fi
    
    # Clean up test environment
    cleanup_test_env "$test_dir"
}
