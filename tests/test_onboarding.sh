#!/bin/sh
# Throwaway-repository, no-agent first-head onboarding smoke test.
# Must not create branches or worktrees in the Hydra source repository.

test_count=0
pass_count=0
fail_count=0

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO_ROOT/bin/hydra"

assert_contains() {
    text="$1"
    pattern="$2"
    message="$3"

    test_count=$((test_count + 1))
    if echo "$text" | grep -q "$pattern"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Text does not contain: '$pattern'"
        echo "  Actual text: '$text'"
    fi
}

snapshot_source_repo() {
    (cd "$REPO_ROOT" && git worktree list && git branch --list)
}

echo "Running onboarding smoke tests..."
echo "================================="

SRC_BEFORE="$(snapshot_source_repo)"

base_dir="$(mktemp -d)" || exit 1
repo_dir="$base_dir/repo"
hydra_home="$base_dir/.hydra"
mkdir -p "$repo_dir" "$hydra_home"

trap 'tmux kill-session -t "${branch:-}" 2>/dev/null || true; rm -rf "$base_dir"' EXIT INT TERM

export HOME="$base_dir"
export HYDRA_HOME="$hydra_home"
export HYDRA_SKIP_AI=1
export HYDRA_NONINTERACTIVE=1
unset HYDRA_ROOT

# --- run-from-source verify ---
echo "Testing run-from-source verify..."
ver_out="$("$HYDRA_BIN" version 2>&1)"
assert_success $? "hydra version from source should succeed"
assert_contains "$ver_out" "Hydra version" "version prints the contract line"

doc_out="$("$HYDRA_BIN" doctor 2>&1)"
assert_success $? "hydra doctor from source should succeed"
assert_contains "$doc_out" "Installation:" "doctor reports installation"
assert_contains "$doc_out" "source checkout" "doctor detects source layout"
assert_contains "$doc_out" "HYDRA_HOME writable" "doctor reports writable home"
assert_contains "$doc_out" "Agents:" "doctor reports agents"

# --- throwaway repo first head ---
echo "Testing throwaway-repository first head..."
cd "$repo_dir" || exit 1
git init >/dev/null 2>&1
git config user.name "Onboarding Test"
git config user.email "onboarding@example.com"
echo "# throwaway" > README.md
git add README.md
git commit -m "init" >/dev/null 2>&1

branch="first-head"
spawn_out="$("$HYDRA_BIN" spawn "$branch" 2>&1)"
assert_success $? "HYDRA_SKIP_AI=1 spawn should succeed without an agent"
assert_contains "$spawn_out" "Creating worktree" "spawn creates a worktree"
assert_contains "$spawn_out" "Creating tmux session" "spawn creates a tmux session"

list_out="$("$HYDRA_BIN" list 2>&1)"
assert_success $? "hydra list should succeed after spawn"
assert_contains "$list_out" "$branch" "list shows the first head"

if tmux has-session -t "$branch" 2>/dev/null; then
    assert_success 0 "tmux session exists for the first head"
else
    assert_failure 0 "tmux session exists for the first head"
fi

wt_path="$base_dir/hydra-$branch"
if [ -d "$wt_path" ]; then
    assert_success 0 "worktree lives under the throwaway parent"
else
    assert_failure 0 "worktree lives under the throwaway parent"
fi

"$HYDRA_BIN" kill "$branch" >/dev/null 2>&1
assert_success $? "hydra kill should clean up the first head"

if tmux has-session -t "$branch" 2>/dev/null; then
    assert_failure 0 "tmux session is gone after kill"
else
    assert_success 0 "tmux session is gone after kill"
fi

if [ -d "$wt_path" ]; then
    assert_failure 0 "worktree is gone after kill"
else
    assert_success 0 "worktree is gone after kill"
fi

if [ -f "$HYDRA_HOME/map" ] && grep -q "$branch" "$HYDRA_HOME/map" 2>/dev/null; then
    assert_failure 0 "state map no longer lists the first head"
else
    assert_success 0 "state map no longer lists the first head"
fi

# --- missing agent recovery (only when claude is absent) ---
echo "Testing missing-agent first-run error..."
if command -v claude >/dev/null 2>&1; then
    echo "[SKIP] claude is on PATH; missing-agent error not asserted"
else
    missing_out="$(
        unset HYDRA_SKIP_AI
        "$HYDRA_BIN" spawn missing-agent-head 2>&1
    )"
    missing_code=$?
    assert_failure "$missing_code" "spawn without HYDRA_SKIP_AI fails when claude is missing"
    assert_contains "$missing_out" "not installed or not on PATH" "missing agent names the precondition"
    assert_contains "$missing_out" "HYDRA_SKIP_AI=1" "missing agent names HYDRA_SKIP_AI recovery"
    if [ -d "$base_dir/hydra-missing-agent-head" ]; then
        assert_failure 0 "missing-agent spawn does not leave a worktree"
        rm -rf "$base_dir/hydra-missing-agent-head"
    else
        assert_success 0 "missing-agent spawn does not leave a worktree"
    fi
    git -C "$repo_dir" branch -D missing-agent-head >/dev/null 2>&1 || true
fi

# --- not in a git repository ---
echo "Testing not-in-git-repo first-run error..."
nogit="$(mktemp -d)"
nogit_out="$(
    cd "$nogit" || exit 1
    HYDRA_HOME="$hydra_home" HYDRA_SKIP_AI=1 "$HYDRA_BIN" spawn stray 2>&1
)"
nogit_code=$?
assert_failure "$nogit_code" "spawn outside a git repo should fail"
assert_contains "$nogit_out" "Not in a git repository" "not-in-repo names the precondition"
assert_contains "$nogit_out" "Next:" "not-in-repo names a recovery action"
assert_contains "$nogit_out" "README Quick Start" "not-in-repo points at the Quick Start"
rm -rf "$nogit"

# --- source tree unchanged ---
SRC_AFTER="$(snapshot_source_repo)"
test_count=$((test_count + 1))
if [ "$SRC_BEFORE" = "$SRC_AFTER" ]; then
    pass_count=$((pass_count + 1))
    echo "[PASS] Hydra source repository has no new worktrees or branches"
else
    fail_count=$((fail_count + 1))
    echo "[FAIL] Hydra source repository worktrees or branches changed"
    echo "  Before: $SRC_BEFORE"
    echo "  After: $SRC_AFTER"
fi

# Extra safety: no hydra-* worktrees under the source parent that we created
if git -C "$REPO_ROOT" worktree list | grep -q "hydra-first-head\|hydra-missing-agent"; then
    assert_failure 0 "no onboarding worktrees attached to the source repo"
else
    assert_success 0 "no onboarding worktrees attached to the source repo"
fi

echo ""
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
