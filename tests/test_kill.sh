#!/bin/sh
# Durable head teardown tests.

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo="$test_root/repo"
HYDRA_HOME="$test_root/home"
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
export HYDRA_HOME HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 HYDRA_LOCK_RETRIES=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    for session in kill-dead kill-dirty kill-locked; do
        tmux kill-session -t "$session" 2>/dev/null || true
    done
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
git -C "$repo" init -q
git -C "$repo" config user.name Test
git -C "$repo" config user.email test@example.com
printf 'clean\n' > "$repo/tracked"
git -C "$repo" add tracked
git -C "$repo" commit -qm init
cd "$repo" || exit 1
"$HYDRA_BIN" init --no-agent --trust >/dev/null
project_id="$(sed -n '1p' .git/hydra/project-id)"

head_dir() {
    _hd_branch="$1"
    find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch \
        -exec sh -c '[ "$(sed -n "1p" "$1")" = "$2" ] && dirname "$1"' sh {} "$_hd_branch" \;
}

echo "Running durable teardown tests..."
echo "================================="

"$HYDRA_BIN" spawn kill-dead --no-agent >/dev/null
dead_path="$("$HYDRA_BIN" path kill-dead)"
tmux kill-session -t kill-dead
kill_out="$("$HYDRA_BIN" kill kill-dead 2>&1)"
assert_success $? "dead tmux session can be durably torn down"
case "$kill_out" in
    *"Removed kill-dead. Branch kept; 'hydra lifecycle kill-dead' shows its history."*) assert_success 0 "kill explains that the branch is kept and where history lives" ;;
    *) assert_success 1 "kill explains that the branch is kept and where history lives"; echo "  Actual: $kill_out" ;;
esac
case "$kill_out" in
    *"has been killed"*) assert_success 1 "kill no longer prints the old killed wording" ;;
    *) assert_success 0 "kill no longer prints the old killed wording" ;;
esac
"$HYDRA_BIN" lifecycle kill-dead >/dev/null 2>&1
assert_success $? "hydra lifecycle still shows a removed head's history"
if git show-ref --verify --quiet refs/heads/kill-dead; then assert_success 0 "kill keeps the branch"; else assert_success 1 "kill keeps the branch"; fi
if [ ! -d "$dead_path" ]; then assert_success 0 "dead-session worktree is removed"; else assert_success 1 "dead-session worktree is removed"; fi
assert_equal stopped "$(sed -n '1p' "$(head_dir kill-dead)/desired-state")" "dead head is marked stopped"

"$HYDRA_BIN" spawn kill-dirty --no-agent >/dev/null
dirty_path="$("$HYDRA_BIN" path kill-dirty)"
printf 'dirty\n' > "$dirty_path/tracked"
"$HYDRA_BIN" kill kill-dirty >/dev/null 2>&1
assert_failure $? "dirty worktree teardown is refused"
if tmux has-session -t kill-dirty 2>/dev/null; then assert_success 0 "dirty refusal preserves tmux"; else assert_success 1 "dirty refusal preserves tmux"; fi
assert_equal running "$(sed -n '1p' "$(head_dir kill-dirty)/desired-state")" "dirty refusal preserves durable state"
printf 'clean\n' > "$dirty_path/tracked"
printf 'keep\n' > "$dirty_path/untracked"
"$HYDRA_BIN" kill kill-dirty --protect-untracked >/dev/null 2>&1
assert_failure $? "UI protection refuses untracked files in noninteractive mode"
assert_equal keep "$(cat "$dirty_path/untracked")" "untracked refusal preserves file contents"
assert_equal running "$(sed -n '1p' "$(head_dir kill-dirty)/desired-state")" "untracked refusal preserves running state"
"$HYDRA_BIN" kill kill-dirty >/dev/null

"$HYDRA_BIN" spawn kill-locked --no-agent >/dev/null
locked_path="$("$HYDRA_BIN" path kill-locked)"
mkdir "$HYDRA_HOME/locks/state_${project_id}.lock"
"$HYDRA_BIN" kill kill-locked >/dev/null 2>&1
assert_failure $? "teardown fails closed during state-lock contention"
if tmux has-session -t kill-locked 2>/dev/null; then assert_success 0 "lock failure preserves tmux"; else assert_success 1 "lock failure preserves tmux"; fi
if [ -d "$locked_path" ]; then assert_success 0 "lock failure preserves worktree"; else assert_success 1 "lock failure preserves worktree"; fi
assert_equal running "$(sed -n '1p' "$(head_dir kill-locked)/desired-state")" "failed preflight preserves running state"
rmdir "$HYDRA_HOME/locks/state_${project_id}.lock"
"$HYDRA_BIN" kill kill-locked >/dev/null
assert_success $? "interrupted teardown can be retried"

echo "================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
