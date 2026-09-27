#!/bin/sh
# Durable head teardown tests.

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo="$test_root/repo"
HYDRA_HOME="$test_root/home"
TMUX_TMPDIR="$test_root/tmux"
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
mkdir -p "$TMUX_TMPDIR"
unset TMUX HYDRA_TMUX_SOCKET HYDRA_TMUX_SOCKET_NAME HYDRA_HEAD_ID HYDRA_INSTANCE_ID
export HYDRA_HOME TMUX_TMPDIR HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 HYDRA_LOCK_RETRIES=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    tmux kill-server 2>/dev/null || true
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
git -C "$repo" init -q
git -C "$repo" config user.name Test
git -C "$repo" config user.email test@example.com
printf 'clean\n' > "$repo/tracked"
printf 'ignored\n' > "$repo/.gitignore"
git -C "$repo" add tracked .gitignore
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

# Preview fixtures cover the same recorded heads used by destructive kill.
for branch in preview-clean preview-tracked preview-untracked; do
    "$HYDRA_BIN" spawn "$branch" --no-agent >/dev/null
done
"$HYDRA_BIN" spawn preview-headless --headless --no-agent >/dev/null
"$HYDRA_BIN" group create preview-group preview-clean preview-tracked >/dev/null
clean_path="$("$HYDRA_BIN" path preview-clean)"
tracked_path="$("$HYDRA_BIN" path preview-tracked)"
untracked_path="$("$HYDRA_BIN" path preview-untracked)"
headless_path="$("$HYDRA_BIN" path preview-headless)"
printf 'staged\n' > "$tracked_path/tracked"
git -C "$tracked_path" add tracked
printf 'ordinary\n' > "$untracked_path/untracked"
printf 'ignored\n' > "$clean_path/ignored"

preview_snapshot() {
    git worktree list --porcelain
    tmux list-sessions -F '#{session_name} #{session_id} #{session_windows}' 2>/dev/null || true
    for _ps_path in "$HYDRA_HOME" "$repo" "$clean_path" "$tracked_path" "$untracked_path" "$headless_path"; do
        if [ -d "$_ps_path" ]; then
            find "$_ps_path" -type f -exec cksum {} \; | LC_ALL=C sort
        else
            printf 'unavailable %s\n' "$_ps_path"
        fi
    done
}

preview_check() {
    preview_snapshot > "$test_root/before"
    "$HYDRA_BIN" kill "$@" < /dev/null > "$test_root/preview" 2>&1
    _pc_status=$?
    preview_snapshot > "$test_root/after"
    assert_success "$_pc_status" "preview succeeds: $*"
    cmp -s "$test_root/before" "$test_root/after"
    assert_success $? "preview preserves sessions, worktrees, state and Git index: $*"
    grep -Fq 'Dry run: no changes will be made.' "$test_root/preview"
    assert_success $? "preview declares no changes: $*"
    if grep -Eiq 'y/n|yes/no|confirm.*[?:]' "$test_root/preview"; then
        assert_success 1 "preview does not prompt: $*"
    else
        assert_success 0 "preview does not prompt: $*"
    fi
}

preview_head() {
    _ph_branch="$1" _ph_session="$2" _ph_path="$3" _ph_tracked="$4" _ph_untracked="$5"
    awk -v branch="$_ph_branch" '/^Branch: / { selected = ($0 == "Branch: " branch) } selected { print }' \
        "$test_root/preview" > "$test_root/head"
    for _ph_line in "Branch: $_ph_branch" "Session: $_ph_session" "Worktree: $_ph_path" \
        "Uncommitted changes: $_ph_tracked" "Untracked changes: $_ph_untracked" 'Branch kept.'; do
        grep -Fxq "$_ph_line" "$test_root/head"
        assert_success $? "$_ph_branch reports $_ph_line"
    done
}

preview_check --dry-run preview-clean
assert_equal 1 "$(grep -c '^Branch: ' "$test_root/preview")" "branch preview selects one head"
preview_head preview-clean preview-clean "$clean_path" no no
preview_check preview-tracked --force --dry-run
preview_head preview-tracked preview-tracked "$tracked_path" yes no
printf 'unstaged\n' >> "$tracked_path/tracked"
preview_check --dry-run preview-tracked
preview_head preview-tracked preview-tracked "$tracked_path" yes no
git -C "$tracked_path" restore --staged tracked
preview_check preview-tracked --dry-run
preview_head preview-tracked preview-tracked "$tracked_path" yes no
preview_check --dry-run --all
assert_equal 4 "$(grep -c '^Branch: ' "$test_root/preview")" "all preview selects every active head"
preview_head preview-untracked preview-untracked "$untracked_path" no yes
preview_head preview-headless - "$headless_path" no no
preview_check --group preview-group --dry-run
assert_equal 2 "$(grep -c '^Branch: ' "$test_root/preview")" "group preview selects group members"
preview_check --dry-run -g preview-group --force
assert_equal 2 "$(grep -c '^Branch: ' "$test_root/preview")" "short group flag selects group members"

mv "$headless_path" "$test_root/moved-headless"
preview_check --dry-run preview-headless
preview_head preview-headless - "$headless_path" unknown unknown
grep -Fqi unavailable "$test_root/preview"
assert_success $? "missing worktree is explained as unavailable"
mv "$test_root/moved-headless" "$headless_path"

for args in '--all preview-clean' '--all --group preview-group' '--group preview-group preview-clean' \
    '--group preview-group -g another-group' '--group' '--unknown'; do
    preview_snapshot > "$test_root/before"
    # These fixed argument cases intentionally use shell splitting.
    # shellcheck disable=SC2086
    "$HYDRA_BIN" kill --dry-run $args < /dev/null > "$test_root/preview" 2>&1
    assert_failure $? "invalid kill arguments fail: $args"
    grep -Fq 'Error:' "$test_root/preview"
    assert_success $? "invalid kill arguments explain error: $args"
    preview_snapshot > "$test_root/after"
    cmp -s "$test_root/before" "$test_root/after"
    assert_success $? "invalid kill arguments preserve all state: $args"
done
preview_check --dry-run --group missing-group
grep -Fq 'No active Hydra heads selected' "$test_root/preview"
assert_success $? "empty group preview reports no heads"
preview_check --dry-run --all --force

for tty_case in branch all group; do
    case "$tty_case" in
        branch) set -- preview-clean ;;
        all) set -- --all ;;
        group) set -- -g preview-group ;;
    esac
    preview_snapshot > "$test_root/before"
    case "$(uname -s)" in
        Darwin) printf 'n\n' | (unset HYDRA_NONINTERACTIVE CI; script -q "$test_root/tty" dash "$HYDRA_BIN" kill --dry-run "$@") > "$test_root/preview" 2>&1 ;;
        *) printf 'n\n' | (unset HYDRA_NONINTERACTIVE CI; script -q -e -c "dash '$HYDRA_BIN' kill --dry-run $*" "$test_root/tty") > "$test_root/preview" 2>&1 ;;
    esac
    assert_success $? "TTY preview exits without confirmation: $tty_case"
    preview_snapshot > "$test_root/after"
    cmp -s "$test_root/before" "$test_root/after"
    assert_success $? "TTY preview preserves all state: $tty_case"
    grep -Fq 'Branch kept.' "$test_root/preview"
    assert_success $? "TTY preview ran rather than aborting: $tty_case"
done

echo "================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
