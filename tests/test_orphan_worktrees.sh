#!/bin/sh
# Leftover (orphaned) worktree detection: doctor, cleanup, the native TUI rows
# and gc share one authority, and never touch worktrees Hydra did not create.

test_count=0
pass_count=0
fail_count=0
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
test_root="$(cd "$(mktemp -d)" && pwd -P)"
parent="$test_root/work"
repo="$parent/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    for _session in orphan-live gone-clean gone-dirty; do
        tmux kill-session -t "$_session" 2>/dev/null || true
    done
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _worktree; do
            [ "$_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_worktree" 2>/dev/null || true
        done
    fi
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

contains() {
    case "$2" in *"$1"*) assert_success 0 "$3" ;; *) assert_success 1 "$3" ;; esac
}

check() {
    _check_message="$1"
    shift
    if "$@"; then assert_success 0 "$_check_message"; else assert_success 1 "$_check_message"; fi
}

lacks() {
    case "$2" in *"$1"*) assert_success 1 "$3" ;; *) assert_success 0 "$3" ;; esac
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked.txt
git add tracked.txt
git commit -qm init

echo "Running leftover worktree tests..."
echo "=================================="

"$HYDRA_BIN" init --no-agent --trust >/dev/null
assert_success $? "project initializes"
project_id="$(sed -n '1p' .git/hydra/project-id)"
worktree_root="$(sed -n '1p' .git/hydra/worktree-root)"

# Worktrees the user created by hand: sibling names that look like Hydra's
# legacy hydra-<branch> layout, and a non-head directory under the root.
user_sibling="$parent/repo-c-tui-visualization"
user_legacy="$parent/hydra-user-thing"
user_in_root="$worktree_root/scratch-worktree"
git worktree add -q -b feature/c-tui-visualization "$user_sibling"
git worktree add -q -b user-thing "$user_legacy"
mkdir -p "$worktree_root"
git worktree add -q -b scratch "$user_in_root"

"$HYDRA_BIN" spawn orphan-live --no-agent >/dev/null
assert_success $? "live head spawns"
"$HYDRA_BIN" spawn gone-clean --no-agent >/dev/null
assert_success $? "clean head spawns"
"$HYDRA_BIN" spawn gone-dirty --no-agent >/dev/null
assert_success $? "dirty head spawns"
live_path="$("$HYDRA_BIN" path orphan-live)"
clean_path="$("$HYDRA_BIN" path gone-clean)"
dirty_path="$("$HYDRA_BIN" path gone-dirty)"
printf 'uncommitted\n' > "$dirty_path/notes.txt"

# Retire two heads by removing their records, leaving their worktrees behind.
for _gone in gone-clean gone-dirty; do
    tmux kill-session -t "$_gone" 2>/dev/null || true
done
rm -rf "$HYDRA_HOME/state/v2/projects/$project_id/heads/$(basename "$clean_path")"
rm -rf "$HYDRA_HOME/state/v2/projects/$project_id/heads/$(basename "$dirty_path")"

gc_dry="$("$HYDRA_BIN" gc --policy orphaned --dry-run)"
assert_success $? "gc dry-run succeeds"
contains "would-remove-orphan	$clean_path" "$gc_dry" "gc lists the clean leftover head worktree"
contains "preserved-dirty	$dirty_path" "$gc_dry" "gc lists the dirty leftover head worktree as preserved"
assert_equal 2 "$(printf '%s\n' "$gc_dry" | grep -c .)" "gc reports exactly the two leftover head worktrees"
for _path in "$user_sibling" "$user_legacy" "$user_in_root" "$live_path"; do
    lacks "$_path" "$gc_dry" "gc ignores $(basename "$_path")"
done

doctor_out="$("$HYDRA_BIN" doctor 2>&1)"
case "$doctor_out" in
    *"2 leftover worktrees from removed tasks, "[0-9]*"iB; no uncommitted changes in 1, uncommitted changes in 1"*)
        assert_success 0 "doctor reports the gc count, total size and change state" ;;
    *) assert_success 1 "doctor reports the gc count, total size and change state" ;;
esac
case "$doctor_out" in
    *"gone-clean ($clean_path); "[0-9]*"iB; no uncommitted changes"*) assert_success 0 "doctor names the clean leftover worktree and its size" ;;
    *) assert_success 1 "doctor names the clean leftover worktree and its size" ;;
esac
contains "gone-dirty ($dirty_path); " "$doctor_out" "doctor names the dirty leftover worktree"
contains "iB; has uncommitted changes" "$doctor_out" "doctor marks uncommitted changes"
contains "Next: hydra gc --policy orphaned --dry-run" "$doctor_out" "doctor suggests a working next action"
for _path in "$user_sibling" "$user_legacy" "$user_in_root"; do
    lacks "$_path" "$doctor_out" "doctor does not flag user worktree $(basename "$_path")"
done

# Size measurement is bounded and reports unknown rather than zero.
real_du="$(command -v du)"
mkdir -p "$test_root/du-fail" "$test_root/du-slow" "$test_root/du-count"
printf '#!/bin/sh\nexit 1\n' > "$test_root/du-fail/du"
printf '#!/bin/sh\nexec sleep 30\n' > "$test_root/du-slow/du"
printf '#!/bin/sh\nprintf x >> "%s"\nexec "%s" "$@"\n' "$test_root/du-calls" "$real_du" > "$test_root/du-count/du"
chmod +x "$test_root/du-fail/du" "$test_root/du-slow/du" "$test_root/du-count/du"
fail_out="$(PATH="$test_root/du-fail:$PATH" "$HYDRA_BIN" doctor 2>&1)"
contains "2 leftover worktrees from removed tasks, size unknown" "$fail_out" "unmeasurable total size is unknown"
contains "gone-clean ($clean_path); size unknown; no uncommitted changes" "$fail_out" "unmeasurable worktree size is unknown"
lacks " 0 KiB" "$fail_out" "an unmeasurable size is never reported as zero"
slow_started="$(date +%s)"
slow_out="$(PATH="$test_root/du-slow:$PATH" HYDRA_WORKTREE_SIZE_TIMEOUT=1 "$HYDRA_BIN" doctor 2>&1)"
slow_elapsed=$(($(date +%s) - slow_started))
contains "size unknown" "$slow_out" "a size measurement past its budget is unknown"
if [ "$slow_elapsed" -lt 20 ]; then
    assert_success 0 "a stalled size measurement does not stall doctor"
else
    assert_success 1 "a stalled size measurement does not stall doctor"
fi

cleanup_out="$("$HYDRA_BIN" cleanup 2>&1)"
assert_success $? "noninteractive cleanup succeeds"
contains "Found 2 leftover worktrees from removed tasks" "$cleanup_out" "cleanup reports the same leftover worktrees"
contains "gone-clean ($clean_path); " "$cleanup_out" "cleanup names the clean leftover worktree"
contains "branches are kept" "$cleanup_out" "cleanup says branches are kept"
contains "pass --include-dirty" "$cleanup_out" "cleanup keeps dirty worktrees without explicit opt-in"
contains "hydra gc --policy orphaned --apply" "$cleanup_out" "noninteractive cleanup points at the gc authority"
for _path in "$user_sibling" "$user_legacy" "$user_in_root"; do
    lacks "$_path" "$cleanup_out" "cleanup does not offer user worktree $(basename "$_path")"
done
auto_out="$("$HYDRA_BIN" cleanup --auto 2>&1)"
contains "Leftover worktrees were not removed" "$auto_out" "doctor --fix cleanup reports without removing"
for _path in "$clean_path" "$dirty_path" "$user_sibling" "$user_legacy" "$user_in_root" "$live_path"; do
    if [ -d "$_path" ]; then
        assert_success 0 "cleanup leaves $(basename "$_path") in place"
    else
        assert_success 1 "cleanup leaves $(basename "$_path") in place"
    fi
done

tui_rows="$(PATH="$test_root/du-count:$PATH" "$HYDRA_BIN" tui --data | grep '^R	orphan-worktree')"
case "$tui_rows" in
    *"R	orphan-worktree	gone-clean ($clean_path), "[0-9]*"iB	$clean_path	exact	hydra gc --policy orphaned --dry-run"*)
        assert_success 0 "TUI reports the clean leftover worktree, its size and a read-only gc review" ;;
    *) assert_success 1 "TUI reports the clean leftover worktree, its size and a read-only gc review" ;;
esac
contains "R	orphan-worktree-dirty	gone-dirty ($dirty_path), " "$tui_rows" "TUI reports the dirty leftover worktree"
assert_equal 2 "$(printf '%s\n' "$tui_rows" | grep -c .)" "TUI rows match gc"
for _path in "$user_sibling" "$user_legacy" "$user_in_root"; do
    lacks "$_path" "$tui_rows" "TUI does not flag user worktree $(basename "$_path")"
done
first_calls="$(wc -c < "$test_root/du-calls" | tr -d ' ')"
PATH="$test_root/du-count:$PATH" "$HYDRA_BIN" tui --data >/dev/null
assert_equal "$first_calls" "$(wc -c < "$test_root/du-calls" | tr -d ' ')" "TUI refreshes reuse cached sizes instead of re-measuring"
tui_action="$(printf '%s\n' "$tui_rows" | sed -n '1p' | awk -F '	' '{print $6}')"
# shellcheck disable=SC2086
"$HYDRA_BIN" ${tui_action#hydra } >/dev/null
assert_success $? "TUI suggested command runs successfully"

du_text="$("$HYDRA_BIN" du)"
assert_success $? "du succeeds with leftover worktrees"
contains "HEAD	BRANCH	WORKTREE_KIB	STATE_KIB	PATH	KIND" "$du_text" "du labels the row kind"
contains "	$live_path	head" "$du_text" "du keeps head rows"
case "$du_text" in
    *"-	gone-clean	"[0-9]*"	-	$clean_path	leftover"*) assert_success 0 "du lists the clean leftover worktree and its size" ;;
    *) assert_success 1 "du lists the clean leftover worktree and its size" ;;
esac
contains "	$dirty_path	leftover-dirty" "$du_text" "du marks the dirty leftover worktree"
for _path in "$user_sibling" "$user_legacy" "$user_in_root"; do
    lacks "$_path" "$du_text" "du does not list user worktree $(basename "$_path")"
done
du_json="$("$HYDRA_BIN" du --json)"
case "$du_json" in
    *'"heads":['*'"leftover":[{"branch":"gone-'*'"worktree_kib":'[0-9]*'"uncommitted_changes":'*) assert_success 0 "du JSON adds leftover worktrees" ;;
    *) assert_success 1 "du JSON adds leftover worktrees" ;;
esac
contains "\"uncommitted_changes\":true,\"path\":\"$dirty_path\"" "$du_json" "du JSON marks uncommitted changes"
unknown_json="$(PATH="$test_root/du-fail:$PATH" "$HYDRA_BIN" du --json 2>/dev/null)"
contains '"worktree_kib":null,"uncommitted_changes":false' "$unknown_json" "du JSON reports an unknown leftover size as null"


"$HYDRA_BIN" gc --dry-run >/dev/null 2>&1
assert_failure $? "gc without a policy exits nonzero"
"$HYDRA_BIN" gc --bogus >/dev/null 2>&1
assert_failure $? "gc with an unknown option exits nonzero"
policy_err="$("$HYDRA_BIN" gc --policy 2>&1)"
assert_failure $? "gc with a missing policy value exits nonzero"
contains "--policy requires a value" "$policy_err" "missing option values are explained"
"$HYDRA_BIN" gc --policy stopped --path "$clean_path" >/dev/null 2>&1
assert_failure $? "--path is limited to the orphaned policy"

"$HYDRA_BIN" gc --policy orphaned --apply --path "$user_sibling" >/dev/null 2>&1
assert_failure $? "gc refuses to scope removal to a user worktree"
check "refused path is untouched" [ -d "$user_sibling" ]
dirty_apply="$("$HYDRA_BIN" gc --policy orphaned --apply --path "$dirty_path")"
contains "preserved-dirty	$dirty_path" "$dirty_apply" "scoped apply keeps a dirty worktree without --include-dirty"
check "dirty work survives scoped apply" [ -f "$dirty_path/notes.txt" ]
clean_apply="$("$HYDRA_BIN" gc --policy orphaned --apply --path "$clean_path")"
assert_success $? "scoped apply removes one clean leftover worktree"
assert_equal "removed-orphan	$clean_path" "$clean_apply" "scoped apply touches only the named worktree"
check "the named worktree was removed" [ ! -d "$clean_path" ]
check "the other leftover worktree was not removed" [ -d "$dirty_path" ]
git rev-parse --verify -q refs/heads/gone-clean >/dev/null
assert_success $? "removal keeps the branch"
"$HYDRA_BIN" gc --policy orphaned --apply --include-dirty --path "$dirty_path" >/dev/null
assert_success $? "explicit --include-dirty removes the dirty leftover worktree"
check "dirty leftover worktree removed only after opt-in" [ ! -d "$dirty_path" ]

doctor_after="$("$HYDRA_BIN" doctor 2>&1)"
contains "No leftover worktrees" "$doctor_after" "doctor is clear once leftovers are removed"
for _path in "$user_sibling" "$user_legacy" "$user_in_root" "$live_path"; do
    if git worktree list --porcelain | grep -Fqx "worktree $_path" && [ -d "$_path" ]; then
        assert_success 0 "$(basename "$_path") survives every cleanup path"
    else
        assert_success 1 "$(basename "$_path") survives every cleanup path"
    fi
done

echo "=================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
