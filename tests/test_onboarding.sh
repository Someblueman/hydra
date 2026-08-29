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

# Replay the documented run-from-source PATH setup before leaving the checkout.
export HYDRA_ROOT="$REPO_ROOT"
export PATH="$HYDRA_ROOT/bin:$PATH"
path_ver_out="$(hydra version 2>&1)"
assert_success $? "documented run-from-source PATH setup should find hydra"
assert_contains "$path_ver_out" "Hydra version" "PATH-resolved hydra runs from source"

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
init_out="$(hydra init --no-agent --trust 2>&1)"
assert_success $? "hydra init should configure a trusted no-agent project"
assert_contains "$init_out" "Initialized Hydra project" "init reports project identity"

dry_out="$(hydra spawn dry-head --no-agent --prompt 'dry task' --dry-run 2>&1)"
assert_success $? "spawn --dry-run should succeed after init"
assert_contains "$dry_out" "no changes will be made" "dry-run states its non-mutating contract"
assert_contains "$dry_out" "task_bytes: 8 (content redacted)" "dry-run reports but redacts task content"
if git show-ref --verify --quiet refs/heads/dry-head; then
    assert_failure 0 "dry-run does not create a branch"
else
    assert_success 0 "dry-run does not create a branch"
fi

spawn_out="$(hydra spawn "$branch" --no-agent --prompt 'Inspect this task without pane typing' 2>&1)"
assert_success $? "first-class --no-agent spawn should succeed"
assert_contains "$spawn_out" "Creating worktree" "spawn creates a worktree"
assert_contains "$spawn_out" "Creating tmux session" "spawn creates a tmux session"

list_out="$(hydra list 2>&1)"
assert_success $? "hydra list should succeed after spawn"
assert_contains "$list_out" "$branch" "list shows the first head"

if tmux has-session -t "$branch" 2>/dev/null; then
    assert_success 0 "tmux session exists for the first head"
else
    assert_failure 0 "tmux session exists for the first head"
fi

wt_path="$(hydra path "$branch")"
if [ -d "$wt_path" ]; then
    assert_success 0 "stored identity-scoped worktree path exists"
else
    assert_failure 0 "stored identity-scoped worktree path exists"
fi
project_id="$(sed -n '1p' "$(git rev-parse --git-common-dir)/hydra/project-id")"
head_id="$(basename "$wt_path")"
head_dir="$HYDRA_HOME/state/v2/projects/$project_id/heads/$head_id"
assert_equal "Inspect this task without pane typing" "$(sed -n '1p' "$head_dir/task")" "spawn records resolved task before launch"
hydra events verify --project "$project_id" --head "$head_id" >/dev/null 2>&1
assert_success $? "spawn creates a verifiable lifecycle event"

hydra kill "$branch" >/dev/null 2>&1
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

if grep -q "$branch" "$HYDRA_HOME/state/v2/projects/$project_id/compat-map" 2>/dev/null; then
    assert_failure 0 "state map no longer lists the first head"
else
    assert_success 0 "state map no longer lists the first head"
fi

git branch -D "$branch" >/dev/null 2>&1
assert_success $? "Quick Start should remove the disposable branch"
if git show-ref --verify --quiet "refs/heads/$branch"; then
    assert_failure 0 "disposable branch is gone after cleanup"
else
    assert_success 0 "disposable branch is gone after cleanup"
fi

# HYDRA_SKIP_SETUP suppresses setup commands but must not bypass trust for the
# rest of repository-controlled YAML.
untrusted_marker="$base_dir/untrusted-yaml-ran"
mkdir -p .hydra
printf 'startup:\n  - touch %s\n' "$untrusted_marker" > .hydra/config.yml
untrusted_out="$(HYDRA_SKIP_SETUP=1 hydra spawn untrusted-config --no-agent 2>&1)"
untrusted_code=$?
assert_failure "$untrusted_code" "skip-setup cannot bypass repository config trust"
assert_contains "$untrusted_out" "not trusted or changed" "untrusted YAML names the trust boundary"
if [ ! -e "$untrusted_marker" ] && ! tmux has-session -t untrusted-config 2>/dev/null; then
    assert_success 0 "untrusted YAML executes no startup command"
else
    assert_failure 0 "untrusted YAML executes no startup command"
fi
git branch -D untrusted-config >/dev/null 2>&1 || true
rm -f .hydra/config.yml

# --- not in a git repository ---
echo "Testing not-in-git-repo first-run error..."
nogit="$(mktemp -d)"
nogit_out="$(
    cd "$nogit" || exit 1
    HYDRA_HOME="$hydra_home" "$HYDRA_BIN" spawn stray --no-agent 2>&1
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
