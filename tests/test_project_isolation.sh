#!/bin/sh
# Two repositories with the same branch label must remain isolated.

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
hydra_home="$test_root/home"
repo_a="$test_root/repo-a"
repo_b="$test_root/repo-b"
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    [ -z "${session_a:-}" ] || tmux kill-session -t "$session_a" 2>/dev/null || true
    [ -z "${session_b:-}" ] || tmux kill-session -t "$session_b" 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

for repo in "$repo_a" "$repo_b"; do
    mkdir -p "$repo"
    git -C "$repo" init -q
    git -C "$repo" config user.name Isolation
    git -C "$repo" config user.email isolation@example.com
    printf 'fixture\n' > "$repo/file"
    git -C "$repo" add file
    git -C "$repo" commit -qm init
done

run_hydra() {
    _rh_repo="$1"
    shift
    (cd "$_rh_repo" && HYDRA_HOME="$hydra_home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 "$HYDRA_BIN" "$@")
}

echo "Running cross-project isolation tests..."
echo "========================================"

run_hydra "$repo_a" init --no-agent --trust >/dev/null
assert_success $? "repository A initializes"
run_hydra "$repo_b" init --no-agent --trust >/dev/null
assert_success $? "repository B initializes"

project_a="$(sed -n '1p' "$repo_a/.git/hydra/project-id")"
project_b="$(sed -n '1p' "$repo_b/.git/hydra/project-id")"
if [ "$project_a" != "$project_b" ]; then
    assert_success 0 "repositories receive distinct project identities"
else
    assert_success 1 "repositories receive distinct project identities"
fi

run_hydra "$repo_a" spawn shared-branch --no-agent --prompt 'task A' >/dev/null 2>&1
assert_success $? "repository A spawns shared branch"
run_hydra "$repo_b" spawn shared-branch --no-agent --prompt 'task B' >/dev/null 2>&1
assert_success $? "repository B spawns shared branch"

head_dir_a="$(find "$hydra_home/state/v2/projects/$project_a/heads" -maxdepth 1 -type d -name 'head_*' | sed -n '1p')"
head_dir_b="$(find "$hydra_home/state/v2/projects/$project_b/heads" -maxdepth 1 -type d -name 'head_*' | sed -n '1p')"
session_a="$(sed -n '1p' "$head_dir_a/session")"
session_b="$(sed -n '1p' "$head_dir_b/session")"
if [ -n "$session_a" ] && [ -n "$session_b" ] && [ "$session_a" != "$session_b" ]; then
    assert_success 0 "same branch label has distinct live session identity"
else
    assert_success 1 "same branch label has distinct live session identity"
fi

path_a="$(run_hydra "$repo_a" path shared-branch)"
path_b="$(run_hydra "$repo_b" path shared-branch)"
if [ "$path_a" != "$path_b" ] && [ -d "$path_a" ] && [ -d "$path_b" ]; then
    assert_success 0 "same branch label has distinct stored worktree paths"
else
    assert_success 1 "same branch label has distinct stored worktree paths"
fi

head_a="$(basename "$path_a")"
head_b="$(basename "$path_b")"
assert_equal "task A" "$(sed -n '1p' "$hydra_home/state/v2/projects/$project_a/heads/$head_a/task")" "repository A task is isolated"
assert_equal "task B" "$(sed -n '1p' "$hydra_home/state/v2/projects/$project_b/heads/$head_b/task")" "repository B task is isolated"

run_hydra "$repo_a" send shared-branch 'A-only message' >/dev/null
assert_success $? "repository A sends a scoped message"
messages_a="$hydra_home/state/v2/projects/$project_a/heads/$head_a/messages/queue"
messages_b="$hydra_home/state/v2/projects/$project_b/heads/$head_b/messages/queue"
if find "$messages_a" -maxdepth 1 -type f 2>/dev/null | grep -q . && \
   { [ ! -d "$messages_b" ] || ! find "$messages_b" -maxdepth 1 -type f 2>/dev/null | grep -q .; }; then
    assert_success 0 "message queue is scoped to project and head"
else
    assert_success 1 "message queue is scoped to project and head"
fi

run_hydra "$repo_a" kill shared-branch >/dev/null
assert_success $? "repository A teardown succeeds"
if tmux has-session -t "$session_b" 2>/dev/null; then
    assert_success 0 "repository A teardown cannot kill repository B session"
else
    assert_success 1 "repository A teardown cannot kill repository B session"
fi
run_hydra "$repo_b" kill shared-branch >/dev/null
assert_success $? "repository B teardown succeeds"

git -C "$repo_a" branch -D shared-branch >/dev/null 2>&1
git -C "$repo_b" branch -D shared-branch >/dev/null 2>&1

echo "========================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
