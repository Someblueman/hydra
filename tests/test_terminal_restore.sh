#!/bin/sh
# Restarting a stopped interactive terminal keeps the worktree exactly as it is
# and refuses, without recreating anything, when the worktree itself is gone.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
if ! command -v tmux >/dev/null 2>&1; then
    printf 'SKIP terminal restore: tmux unavailable\n'
    exit 0
fi
# A short private socket path: macOS rejects long socket paths.
test_root="$(mktemp -d /tmp/hydra-terminal-restore.XXXXXX)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
TMUX_TMPDIR="$test_root/tmux"
export TMUX_TMPDIR
unset TMUX
mkdir "$TMUX_TMPDIR"

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Records whether a condition holds as one assertion.
holds() {
    _holds_message="$1"
    shift
    if "$@"; then assert_success 0 "$_holds_message"; else assert_success 1 "$_holds_message"; fi
}
live_sessions() { tmux list-sessions -F '#{session_name}' 2>/dev/null || :; }

cleanup() {
    tmux kill-server 2>/dev/null || : # This test's private socket only.
    rm -rf "$test_root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
git commit --allow-empty -qm init

echo "Running terminal restore tests..."
echo "================================="

"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" spawn restore-me --no-agent >/dev/null
assert_success $? "interactive head spawns"
worktree="$("$HYDRA_BIN" path restore-me)"
printf 'committed\n' > "$worktree/committed.txt"
git -C "$worktree" add committed.txt
git -C "$worktree" -c commit.gpgSign=false commit -qm work
printf 'draft\n' > "$worktree/draft.txt"
head_before="$(git -C "$worktree" rev-parse HEAD)"
status_before="$(git -C "$worktree" status --porcelain)"
session="$(tmux list-sessions -F '#{session_name}')"
tmux kill-session -t "$session"

"$HYDRA_BIN" resume --terminal restore-me >/dev/null
assert_success $? "resume --terminal restarts a stopped interactive terminal"
holds "the head has a live terminal again" [ -n "$(live_sessions)" ]
assert_equal "$head_before" "$(git -C "$worktree" rev-parse HEAD)" "committed work is unchanged"
assert_equal "$status_before" "$(git -C "$worktree" status --porcelain)" "uncommitted work is unchanged"
assert_equal "draft" "$(cat "$worktree/draft.txt")" "the dirty file keeps its contents"
live="$(tmux list-panes -a -F '#{pane_current_path}' | sed -n '1p')"
case "$live" in *"$(basename "$worktree")") assert_success 0 "the restarted terminal opens in the head's worktree" ;;
    *) assert_success 1 "the restarted terminal opens in the head's worktree" ;; esac

"$HYDRA_BIN" resume --terminal restore-me >/dev/null 2>&1
assert_failure $? "a live terminal is never given a second one"

tmux kill-server 2>/dev/null
mv "$worktree" "$test_root/moved-away"
output="$("$HYDRA_BIN" resume --terminal restore-me 2>&1)"
assert_failure $? "a missing worktree is refused"
case "$output" in *"worktree of 'restore-me' is missing"*"will not recreate it"*) assert_success 0 "the refusal names the missing worktree" ;;
    *) assert_success 1 "the refusal names the missing worktree" ;; esac
holds "the refused restore created no worktree" [ ! -e "$worktree" ]
holds "the refused restore started no terminal" [ -z "$(live_sessions)" ]

"$HYDRA_BIN" resume --terminal 2>/dev/null
assert_failure $? "resume --terminal requires a branch"

echo ""
echo "Tests run: $test_count  Passed: $pass_count  Failed: $fail_count"
[ "$fail_count" -eq 0 ]
