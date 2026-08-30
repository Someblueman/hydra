#!/bin/sh
# Hydra 1.7 disk usage, GC policy, and worktree doctor tests.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(mktemp -d)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    tmux kill-session -t storage-head 2>/dev/null || true
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _two_worktree; do
            [ "$_two_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_two_worktree" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked.txt
git add tracked.txt
git commit -qm init

echo "Running Hydra 1.7 worktree operations tests..."
echo "============================================"

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes"
git add .hydra/config.yml
git commit -qm hydra-config
"$HYDRA_BIN" spawn storage-head --no-agent >/dev/null
assert_success $? "storage head spawns"
worktree="$("$HYDRA_BIN" path storage-head)"

du_json="$("$HYDRA_BIN" du --json)"
case "$du_json" in *'"command":"du"'*'"branch":"storage-head"'*'"worktree_kib":'*) assert_success 0 "du reports worktree and state usage" ;; *) assert_success 1 "du reports worktree and state usage" ;; esac

"$HYDRA_BIN" worktree doctor lock storage-head --reason maintenance >/dev/null
assert_success $? "doctor locks a linked worktree"
doctor_status="$("$HYDRA_BIN" worktree doctor status)"
case "$doctor_status" in *'locked maintenance'*) assert_success 0 "doctor status exposes the lock reason" ;; *) assert_success 1 "doctor status exposes the lock reason" ;; esac
"$HYDRA_BIN" worktree doctor unlock storage-head >/dev/null
assert_success $? "doctor unlocks a linked worktree"

pack_output="$("$HYDRA_BIN" context create storage-head --note 'temporary archive')"
pack_path="$(printf '%s\n' "$pack_output" | sed -n 's/^  path: //p')"
printf '1\n' > "$pack_path/created-at"
archive_dry="$("$HYDRA_BIN" gc --policy archives --older-than 1 --dry-run)"
case "$archive_dry" in *'would-remove-archive'*) assert_success 0 "archive GC is dry-run by default" ;; *) assert_success 1 "archive GC is dry-run by default" ;; esac
if [ -d "$pack_path" ]; then assert_success 0 "archive dry-run preserves data"; else assert_success 1 "archive dry-run preserves data"; fi
"$HYDRA_BIN" gc --policy archives --older-than 1 --apply >/dev/null
assert_success $? "archive GC applies only with explicit policy"
if [ ! -d "$pack_path" ]; then assert_success 0 "applied archive GC removes only the selected archive"; else assert_success 1 "applied archive GC removes only the selected archive"; fi

worktree_root="$(sed -n '1p' .git/hydra/worktree-root)"
orphan_path="$worktree_root/orphan-worktree"
mkdir -p "$worktree_root"
git worktree add -q -b orphan-worktree "$orphan_path"
printf 'untracked\n' > "$orphan_path/untracked.txt"
orphan_preserved="$("$HYDRA_BIN" gc --policy orphaned --apply)"
case "$orphan_preserved" in *'preserved-dirty'*"$orphan_path"*) assert_success 0 "GC preserves dirty orphaned worktrees by default" ;; *) assert_success 1 "GC preserves dirty orphaned worktrees by default" ;; esac
if [ -d "$orphan_path" ]; then assert_success 0 "dirty orphan remains attached"; else assert_success 1 "dirty orphan remains attached"; fi
rm -f "$orphan_path/untracked.txt"
orphan_removed="$("$HYDRA_BIN" gc --policy orphaned --apply)"
case "$orphan_removed" in *'removed-orphan'*"$orphan_path"*) assert_success 0 "GC removes a clean orphan under explicit policy" ;; *) assert_success 1 "GC removes a clean orphan under explicit policy" ;; esac
if [ ! -d "$orphan_path" ]; then assert_success 0 "clean orphan worktree is removed"; else assert_success 1 "clean orphan worktree is removed"; fi

tmux kill-session -t storage-head
move_target="$worktree_root/moved-storage-head"
move_dry="$("$HYDRA_BIN" worktree doctor move storage-head "$move_target" --dry-run)"
case "$move_dry" in *'would-move'*"$worktree"*"$move_target"*) assert_success 0 "worktree move has a non-mutating dry-run" ;; *) assert_success 1 "worktree move has a non-mutating dry-run" ;; esac
if [ -d "$worktree" ] && [ ! -e "$move_target" ]; then assert_success 0 "move dry-run leaves paths unchanged"; else assert_success 1 "move dry-run leaves paths unchanged"; fi
"$HYDRA_BIN" worktree doctor move storage-head "$move_target" >/dev/null
assert_success $? "doctor moves a stopped clean worktree"
assert_equal "$move_target" "$("$HYDRA_BIN" path storage-head)" "move updates authoritative stored path"

repair_dry="$("$HYDRA_BIN" worktree doctor repair --dry-run)"
case "$repair_dry" in *'would-repair'*"$move_target"*) assert_success 0 "repair defaults to a bounded dry-run" ;; *) assert_success 1 "repair defaults to a bounded dry-run" ;; esac
"$HYDRA_BIN" worktree doctor repair --apply >/dev/null
assert_success $? "explicit repair applies to recorded head paths"
"$HYDRA_BIN" worktree doctor prune >/dev/null
assert_success $? "prune is dry-run unless --apply is passed"

"$HYDRA_BIN" kill storage-head >/dev/null
assert_success $? "moved stopped head tears down through its stored path"
[ ! -d "$move_target" ]
assert_success $? "teardown removes the moved worktree"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
