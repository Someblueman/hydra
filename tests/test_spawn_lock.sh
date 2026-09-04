#!/bin/sh
# Spawn rolls back when the authoritative project-state lock cannot be acquired.

test_count=0
pass_count=0
fail_count=0

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO_ROOT/bin/hydra"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_LOCK_RETRIES=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

setup() {
    test_base_dir="$(mktemp -d)"
    mkdir -p "$test_base_dir/repo"
    cd "$test_base_dir/repo" || exit 1

    export HYDRA_HOME="$test_base_dir/.hydra"
    mkdir -p "$HYDRA_HOME/locks"

    git init >/dev/null 2>&1
    git config user.name "Test User"
    git config user.email "test@example.com"
    echo "# Test" > README.md
    git add README.md
    git commit -m "init" >/dev/null 2>&1
    "$HYDRA_BIN" init --no-agent --trust >/dev/null
    project_id="$(sed -n '1p' .git/hydra/project-id)"
}

cleanup() {
    if [ -n "$branch" ]; then
        tmux kill-session -t "$branch" 2>/dev/null || true
    fi
    if [ -n "$test_base_dir" ] && [ -d "$test_base_dir" ]; then
        rm -rf "$test_base_dir"
    fi
}

test_spawn_rolls_back_when_state_lock_held() {
    echo "Testing spawn rolls back when the project-state lock is held..."
    setup
    branch="lockfail-spawn"
    mkdir "$HYDRA_HOME/locks/state_${project_id}.lock"

    output="$("$HYDRA_BIN" spawn "$branch" 2>&1)"
    exit_code=$?
    assert_failure "$exit_code" "spawn should fail when the project-state lock is held"

    case "$output" in
        *"Failed to acquire"*|*"Failed to persist state"*|*"Failed to commit durable head state"*)
            echo "[PASS] spawn reports state-lock failure"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] spawn reports state-lock failure"
            echo "  Output: $output"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    if ! "$HYDRA_BIN" list --json | grep -q "\"branch\": \"$branch\""; then
        echo "[PASS] durable state has no active head for the aborted spawn"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] durable state has no active head for the aborted spawn"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if tmux has-session -t "$branch" 2>/dev/null; then
        echo "[FAIL] tmux session rolled back"
        fail_count=$((fail_count + 1))
        tmux kill-session -t "$branch" 2>/dev/null || true
    else
        echo "[PASS] tmux session rolled back"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ -d "$test_base_dir/hydra-$branch" ]; then
        echo "[FAIL] worktree rolled back"
        fail_count=$((fail_count + 1))
    else
        echo "[PASS] worktree rolled back"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup
}

test_post_commit_rollback_retires_head() {
    echo "Testing rollback after durable head commit..."
    setup
    branch="postcommit-rollback"
    "$HYDRA_BIN" spawn "$branch" --no-agent >/dev/null
    worktree_path="$("$HYDRA_BIN" path "$branch")"

    HYDRA_LIB_DIR="$REPO_ROOT/lib"
    export HYDRA_LIB_DIR
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/locks.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/identity.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state_v2.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/tmux.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/git.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/lifecycle.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/events.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/spawn.sh"

    session_name="$(get_session_for_branch "$branch")"
    spawn_rollback_session "$session_name" "$branch" "$worktree_path"
    assert_success $? "post-commit rollback succeeds"
    if ! "$HYDRA_BIN" list --json | grep -Fq "\"branch\": \"$branch\""; then
        assert_success 0 "post-commit rollback removes the head from active state"
    else
        assert_success 1 "post-commit rollback removes the head from active state"
    fi
    if ! tmux has-session -t "$session_name" 2>/dev/null && [ ! -d "$worktree_path" ]; then
        assert_success 0 "post-commit rollback removes runtime resources"
    else
        assert_success 1 "post-commit rollback removes runtime resources"
    fi

    cleanup
}

echo "Running spawn lock rollback tests..."
echo "================================"
test_spawn_rolls_back_when_state_lock_held
test_post_commit_rollback_retires_head
echo "================================"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
fi
echo "Some tests failed!"
exit 1
