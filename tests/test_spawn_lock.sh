#!/bin/sh
# Spawn rolls back when the state map lock cannot be acquired.

test_count=0
pass_count=0
fail_count=0

HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
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
    export HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME/locks"
    : > "$HYDRA_MAP"

    git init >/dev/null 2>&1
    git config user.name "Test User"
    git config user.email "test@example.com"
    echo "# Test" > README.md
    git add README.md
    git commit -m "init" >/dev/null 2>&1
}

cleanup() {
    if [ -n "$branch" ]; then
        tmux kill-session -t "$branch" 2>/dev/null || true
    fi
    if [ -n "$test_base_dir" ] && [ -d "$test_base_dir" ]; then
        rm -rf "$test_base_dir"
    fi
}

test_spawn_rolls_back_when_map_lock_held() {
    echo "Testing spawn rolls back when state_map lock is held..."
    setup
    branch="lockfail-spawn"
    mkdir "$HYDRA_HOME/locks/state_map.lock"

    output="$("$HYDRA_BIN" spawn "$branch" 2>&1)"
    exit_code=$?
    assert_failure "$exit_code" "spawn should fail when the state map lock is held"

    case "$output" in
        *"Failed to acquire state lock"*|*"Failed to save branch-session mapping"*)
            echo "[PASS] spawn reports mapping/lock failure"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] spawn reports mapping/lock failure"
            echo "  Output: $output"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    if [ ! -s "$HYDRA_MAP" ] || ! grep -q "$branch" "$HYDRA_MAP" 2>/dev/null; then
        echo "[PASS] map has no entry for the aborted spawn"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] map has no entry for the aborted spawn"
        echo "  Map: $(cat "$HYDRA_MAP")"
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

echo "Running spawn lock rollback tests..."
echo "================================"
test_spawn_rolls_back_when_map_lock_held
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
