#!/bin/sh
# The control centre reads the local head snapshot from the state store in its
# own process ('hydra-tui --data' prints that read). It must print exactly the
# rows of the shell reader, 'hydra tui --data', for the same records: every
# case below builds records, runs both readers and compares their output byte
# for byte, then checks the rows for the values the case was built to produce.

set -u

test_count=0
pass_count=0
fail_count=0
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
tui="${HYDRA_TUI_BIN:?HYDRA_TUI_BIN is required: run via make test-parity or make test-one T=tui_store_parity}"
hydra="$repo_root/bin/hydra"

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
test_root="$(cd "$test_root" && pwd -P)"
cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

# Live sessions come from the fixture tmux, never from a real server.
PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH"
GIT_CEILING_DIRECTORIES="$test_root"
export PATH GIT_CEILING_DIRECTORIES
unset TMUX TMUX_PANE HYDRA_STATE_V2_ROOT HYDRA_TEST_TMUX_SOCKET

echo "Running state store reader parity tests..."
echo "=========================================="

put() { printf '%s\n' "$2" > "$1"; }

# Usage: same_rows <message> [NAME=value ...]
# Runs both readers in $repo against $home and compares what they print.
same_rows() {
    _sr_message="$1"
    shift
    (cd "$repo" && env HYDRA_HOME="$home" "$@" "$hydra" tui --data) > "$test_root/shell.tsv" 2> "$test_root/shell.err"
    _sr_shell=$?
    (cd "$repo" && env HYDRA_HOME="$home" "$@" "$tui" --hydra "$hydra" --data) > "$test_root/native.tsv" 2> "$test_root/native.err"
    _sr_native=$?
    if [ "$_sr_shell" -eq 0 ] && [ "$_sr_native" -eq 0 ] && cmp -s "$test_root/shell.tsv" "$test_root/native.tsv"; then
        assert_success 0 "$_sr_message"
    else
        assert_success 1 "$_sr_message"
        echo "  shell reader status $_sr_shell, store reader status $_sr_native; shell rows (<) against store rows (>):"
        diff "$test_root/shell.tsv" "$test_root/native.tsv" | sed -n '1,20p'
        sed -n '1,5p' "$test_root/native.err"
    fi
}

# Usage: row_fields <branch> <field-number>... -> those fields of the branch's
# H row, separated by spaces
row_fields() {
    _rf_branch="$1"
    shift
    awk -F '\t' -v branch="$_rf_branch" -v wanted="$*" '
        BEGIN { count = split(wanted, column, " ") }
        $1 == "H" && $2 == branch {
            line = ""
            for (i = 1; i <= count; i++) line = line (i > 1 ? " " : "") $(column[i])
            print line
        }' "$test_root/native.tsv"
}

rows_of() {
    awk -F '\t' -v kind="$1" '$1 == "R" && $2 == kind { n++ } END { print n + 0 }' "$test_root/native.tsv"
}

# Usage: make_head <head-id> <branch> <session> <profile> [interactive|headless]
make_head() {
    _mh_dir="$heads/$1"
    _mh_instance="instance_${1#head_}"
    _mh_mode="${5:-interactive}"
    mkdir -p "$_mh_dir/instances/$_mh_instance" "$_mh_dir/events"
    put "$_mh_dir/head-id" "$1"
    put "$_mh_dir/branch" "$2"
    put "$_mh_dir/session" "$3"
    put "$_mh_dir/terminal-mode" "$_mh_mode"
    put "$_mh_dir/profile" "$4"
    put "$_mh_dir/group" -
    put "$_mh_dir/created-at" 100
    put "$_mh_dir/dependencies" -
    put "$_mh_dir/pr" -
    put "$_mh_dir/current-instance" "$_mh_instance"
    if [ "$_mh_mode" = headless ]; then put "$_mh_dir/desired-state" headless; else put "$_mh_dir/desired-state" running; fi
    put "$_mh_dir/worktree" ""
    put "$_mh_dir/instances/$_mh_instance/instance-id" "$_mh_instance"
    put "$_mh_dir/instances/$_mh_instance/session" "$3"
    put "$_mh_dir/instances/$_mh_instance/terminal-mode" "$_mh_mode"
    put "$_mh_dir/instances/$_mh_instance/observed-status" starting
    put "$_mh_dir/instances/$_mh_instance/observed-confidence" exact
    : > "$_mh_dir/events/events.jsonl"
}

# Usage: make_project <name> -> sets repo, home, project, project_dir, heads
make_project() {
    repo="$test_root/$1/repo"
    home="$test_root/$1/home"
    project="project_$(printf '%s' "$1" | cksum | awk '{ printf "%016x", $1 }')"
    mkdir -p "$repo" "$home"
    git -C "$repo" init -q -b main
    printf 'one\n' > "$repo/tracked.txt"
    git -C "$repo" add tracked.txt
    git -C "$repo" -c user.name=Test -c user.email=test@example.com commit -q -m fixture
    mkdir -p "$repo/.git/hydra"
    put "$repo/.git/hydra/project-id" "$project"
    project_dir="$home/state/v2/projects/$project"
    heads="$project_dir/heads"
    mkdir -p "$heads"
    put "$home/state/v2/schema-version" 2
    put "$project_dir/project-id" "$project"
    put "$project_dir/repo-root" "$repo"
}

# A lock directory owned by <pid> on <host>.
make_lock() {
    mkdir -p "$1"
    put "$1/pid" "$2"
    put "$1/host" "$3"
}

this_host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"
sh -c 'exit 0' &
dead_pid=$!
wait "$dead_pid"

# ---- no repository, and a repository Hydra does not know ----------------------

repo="$test_root/plain/dir"
home="$test_root/plain/home"
mkdir -p "$repo" "$home/locks/nested"
make_lock "$home/locks/gone.lock" "$dead_pid" "$this_host"
make_lock "$home/locks/held.lock" "$$" "$this_host"
make_lock "$home/locks/elsewhere.lock" "$dead_pid" "another-machine.invalid"
make_lock "$home/locks/unreadable.lock" "not-a-pid" "$this_host"
make_lock "$home/locks/nested/inner.lock" "$dead_pid" "$this_host"
: > "$home/locks/file.lock"
same_rows "outside a repository both readers print the header and stale locks"
assert_equal "2" "$(rows_of stale-lock)" "a lock is stale only when its owner on this machine is gone"
assert_equal "3" "$(awk 'END { print NR }' "$test_root/native.tsv")" "nothing but the header and the stale locks is printed"

repo="$test_root/unknown/repo"
home="$test_root/unknown/home"
mkdir -p "$repo" "$home"
git -C "$repo" init -q -b main
same_rows "a repository without a Hydra project prints only the header"
assert_equal "HYDRA_TUI	2" "$(cat "$test_root/native.tsv")" "the header is the whole document"

# ---- one project with every kind of record -----------------------------------

make_project rich
mkdir -p "$home/profiles/custom" "$home/profiles/blocked" "$home/profiles/claude" "$home/profiles/partial"
put "$home/profiles/custom/executable" /usr/bin/true
put "$home/profiles/custom/adapter" hook-v1
put "$home/profiles/custom/confidence" user-declared
put "$home/profiles/blocked/executable" /usr/bin/true
put "$home/profiles/blocked/adapter.json" '{}'
put "$home/profiles/claude/executable" /usr/bin/true
put "$home/profiles/claude/adapter" replaced
put "$home/profiles/partial/executable" /usr/bin/true
printf '%s\n' 'lifecycle.declared terminal 60' 'lifecycle.observed desktop 120' 'two fields' '' \
    > "$repo/.git/hydra/notifications"

make_head head_0000000000000001 plain session-plain none
make_head head_0000000000000002 rich session-rich custom
make_head head_0000000000000003 dead session-dead codex
make_head head_0000000000000004 headless - aider headless
make_head head_0000000000000005 stopping session-stopping gemini
make_head head_0000000000000006 stopped session-stopped none
make_head head_0000000000000007 spaced session-spaced -
make_head head_0000000000000008 unknown session-unknown nosuch
make_head head_0000000000000009 no-instance session-no-instance none
make_head head_000000000000000a blocked session-blocked blocked
make_head head_000000000000000b empty-profile session-empty none
make_head head_000000000000000c override session-override claude
make_head head_000000000000000d partial session-partial partial
make_head head_000000000000000e plain-dir session-plain-dir none
make_head head_00000000000000f0 twice - none headless
make_head head_00000000000000f1 twice session-twice none
make_head head_00000000000000f2 legacy session-legacy none
make_head head_00000000000000f3 misbound session-misbound none

put "$heads/head_0000000000000005/desired-state" stopping
put "$heads/head_0000000000000006/desired-state" stopped
put "$heads/head_0000000000000007/group" "two words"
rm "$heads/head_0000000000000008/instances/instance_0000000000000008/observed-status"
rm "$heads/head_0000000000000008/instances/instance_0000000000000008/observed-confidence"
rm -r "$heads/head_0000000000000009/instances"
: > "$heads/head_000000000000000b/profile"
mkdir -p "$test_root/rich/not-a-repository"
put "$heads/head_000000000000000e/worktree" "$test_root/rich/not-a-repository"
put "$heads/head_0000000000000003/worktree" "$test_root/rich/missing-worktree"
# The first directory that records a branch names the row, stopped or not.
put "$heads/head_00000000000000f0/desired-state" stopped
rm "$heads/head_00000000000000f2/terminal-mode" "$heads/head_00000000000000f2/instances/instance_00000000000000f2/terminal-mode"
put "$heads/head_00000000000000f3/head-id" head_bad
put "$heads/head_00000000000000f3/instances/instance_00000000000000f3/instance-id" instance_bad
put "$heads/head_00000000000000f3/terminal-mode" broken

# The rich head: every counted record, with entries that must not be counted.
rich="$heads/head_0000000000000002"
rich_instance="$rich/instances/instance_0000000000000002"
put "$rich/group" team
put "$rich/pr" 42
put "$rich_instance/declared-outcome" "done"
put "$rich_instance/observed-status" idle
put "$rich_instance/observed-confidence" reported
printf '%s\n' '{"type":"lifecycle.started"}' '{"type":"signal.idle"}' '{"type":"message.sent"}' \
    '{"note":"\"type\":\"signalx"}' > "$rich/events/events.jsonl"
printf '%s' '{"type":"signal.done"}' >> "$rich/events/events.jsonl"
mkdir -p "$rich/messages/queue/folder"
: > "$rich/messages/queue/one"
: > "$rich/messages/queue/two"
: > "$rich/messages/queue/.hidden"
printf '%s\n' src/ docs/ > "$rich/scopes"
mkdir -p "$rich/gates/review" "$rich/gates/deploy" "$rich/gates/audit/inner" "$rich/gates/.hidden"
: > "$rich/gates/review/approved-at"
: > "$rich/gates/audit/inner/approved-at"
: > "$rich/gates/note"
mkdir -p "$project_dir/claims/claim_01" "$project_dir/claims/claim_02/deep" "$project_dir/claims/claim_03"
put "$project_dir/claims/claim_01/owner-head" head_0000000000000002
printf '%s\n' head_0000000000000002 head_0000000000000002 > "$project_dir/claims/claim_02/deep/owner-head"
put "$project_dir/claims/claim_03/owner-head" head_0000000000000001
mkdir -p "$project_dir/resources/head_0000000000000002"
printf '%s\n' 8080 8081 > "$project_dir/resources/head_0000000000000002/ports"
put "$project_dir/resources/head_0000000000000002/compose-project" rich-compose
: > "$project_dir/resources/head_0000000000000002/database"
mkdir -p "$project_dir/queue"
printf '%s\n' prompt=x branch=rich > "$project_dir/queue/1.queue"
printf '%s\n' branch=rich-other > "$project_dir/queue/2.queue"
printf '%s\n' branch=rich > "$project_dir/queue/3.txt"
printf '%s\n' branch=rich > "$project_dir/queue/.4.queue"
printf '%s\n' ' branch=rich' 'branch=rich ' > "$project_dir/queue/5.queue"
printf '%s' branch=rich > "$project_dir/queue/6.queue"
worktrees="$test_root/rich/.hydra-worktrees/$project"
mkdir -p "$worktrees"
git -C "$repo" worktree add -q "$worktrees/head_0000000000000002" -b rich
printf 'two\n' >> "$worktrees/head_0000000000000002/tracked.txt"
: > "$worktrees/head_0000000000000002/untracked-one"
: > "$worktrees/head_0000000000000002/untracked-two"
put "$rich/worktree" "$worktrees/head_0000000000000002"

mkdir -p "$home/locks"
make_lock "$home/locks/state_gone.lock" "$dead_pid" "$this_host"

live="session-plain,session-rich,session-stopping,session-spaced,session-unknown,session-no-instance,session-blocked,session-empty,session-override,session-partial,session-plain-dir,session-twice,session-legacy,session-misbound"
same_rows "a project with every kind of record reads the same in both readers" FAKE_TMUX_SESSIONS="$live"
assert_equal "16" "$(awk -F '\t' '$1 == "H" { n++ } END { print n + 0 }' "$test_root/native.tsv")" "every active head has a row and the stopped one has none"
assert_equal "session-rich custom team 42 active live done idle reported" \
    "$(row_fields rich 3 4 5 6 7 8 9 10 11)" "the recorded scalars of a head"
assert_equal "5 2 3 3 2 2 3 3 4 2" \
    "$(row_fields rich 13 14 15 16 17 18 19 20 21 22)" \
    "events, signals, messages, claims, scopes, queue, resources, changed files, gates and approvals are counted"
assert_equal "hook-v1 user-declared $home/profiles/custom 2 $repo/.git/hydra/notifications interactive" \
    "$(row_fields rich 26 27 28 29 30 31)" "a custom profile's adapter, the notification rules and the terminal mode"
assert_equal "dead stopped 0" "$(row_fields dead 7 8 20)" "a head whose session is gone is dead and its missing worktree has no changes"
assert_equal "active unavailable headless" "$(row_fields headless 7 8 31)" "a headless head is active with unknown liveness"
assert_equal "unavailable unavailable" "$(row_fields unknown 10 11)" "missing observation records read as unavailable"
assert_equal "unavailable unavailable" "$(row_fields unknown 26 27)" "an unknown profile has no adapter"
assert_equal "unavailable unavailable" "$(row_fields blocked 26 27)" "a profile with an adapter.json is not a launch profile"
assert_equal "replaced unavailable" "$(row_fields override 26 27)" "a custom profile named like a built-in one is read from its files"
assert_equal "none verified-local-help" "$(row_fields dead 26 27)" "a built-in profile reports its fixed adapter and confidence"
assert_equal "head_00000000000000f0 headless stopped" "$(row_fields twice 25 31 23)" \
    "a branch recorded twice is bound to its first directory"
assert_equal "active live interactive" "$(row_fields legacy 7 8 31)" "a record without a terminal mode is interactive"
assert_equal "1" "$(rows_of dead-session)" "the dead session is a recovery finding"
assert_equal "1" "$(rows_of teardown-failure)" "a head left stopping is a teardown finding"
assert_equal "1" "$(rows_of interrupted-lifecycle)" "a head left stopping is an interrupted lifecycle"
assert_equal "3" "$(rows_of malformed-state)" "a field with a space, a missing instance and a wrong binding are malformed state"
assert_equal "1" "$(rows_of stale-lock)" "the stale lock is a recovery finding"
assert_equal "0" "$(rows_of orphan-worktree)" "a recorded worktree is not a leftover"

same_rows "with no live session every terminal head is dead in both readers"
assert_equal "14" "$(rows_of dead-session)" "every terminal head is reported once"

# HYDRA_HOME through a symbolic link is reported by its physical path.
ln -s "$home" "$test_root/rich/home-link"
real_home="$home"
home="$test_root/rich/home-link"
same_rows "a linked HYDRA_HOME reads the same in both readers" FAKE_TMUX_SESSIONS="$live"
assert_equal "$real_home/profiles/custom" "$(row_fields rich 28)" "paths are reported under the physical home"
home="$real_home"

# Run from a subdirectory and from a linked worktree of the same repository.
mkdir -p "$repo/sub/dir"
top="$repo"
repo="$top/sub/dir"
same_rows "a subdirectory of the repository reads the same in both readers" FAKE_TMUX_SESSIONS="$live"
repo="$worktrees/head_0000000000000002"
same_rows "a linked worktree of the repository reads the same in both readers" FAKE_TMUX_SESSIONS="$live"
assert_equal "16" "$(awk -F '\t' '$1 == "H" { n++ } END { print n + 0 }' "$test_root/native.tsv")" "the worktree sees its project's heads"
repo="$top"

# ---- leftover worktrees --------------------------------------------------------

git -C "$repo" worktree add -q "$worktrees/head_00000000000000aa" -b leftover-clean
git -C "$repo" worktree add -q "$worktrees/head_00000000000000bb" -b leftover-dirty
: > "$worktrees/head_00000000000000bb/uncommitted"
mkdir -p "$worktrees/head_00000000000000cc" "$worktrees/not-a-head"
same_rows "leftover worktrees are reported the same by both readers" FAKE_TMUX_SESSIONS="$live"
assert_equal "1" "$(rows_of orphan-worktree)" "a clean leftover worktree is reported"
assert_equal "1" "$(rows_of orphan-worktree-dirty)" "a leftover worktree with uncommitted files is reported as dirty"
if grep -q "leftover-clean ($worktrees/head_00000000000000aa), [0-9]" "$test_root/native.tsv"; then
    assert_success 0 "a leftover row names its branch, path and measured size"
else
    assert_success 1 "a leftover row names its branch, path and measured size"
fi

# A stored worktree root replaces the default one.
mkdir -p "$test_root/rich/elsewhere"
put "$repo/.git/hydra/worktree-root" "$test_root/rich/elsewhere"
same_rows "a stored worktree root is honoured by both readers" FAKE_TMUX_SESSIONS="$live"
assert_equal "0" "$(rows_of orphan-worktree)" "worktrees outside the stored root are not leftovers"
rm "$repo/.git/hydra/worktree-root"

# ---- names that are not identities ---------------------------------------------

make_project odd
make_head head_0000000000000010 odd session-odd none
make_head head_0000000000000011 later session-later none
cp -R "$heads/head_0000000000000010" "$heads/head_000 bad\\name"
put "$heads/head_000 bad\\name/branch" later
put "$heads/head_000 bad\\name/head-id" "head_000 bad\\name"
same_rows "a directory whose name is no identity reads the same in both readers" FAKE_TMUX_SESSIONS="session-odd,session-later"
# Both active records of the branch are rows, and both carry the first name.
assert_equal "2 unavailable" "$(awk -F '\t' '$1 == "H" && $2 == "later" && $25 == "head_000 bad\\name" && $24 == "" { n++; desired = $23 }
    END { print n + 0, desired }' "$test_root/native.tsv")" \
    "a branch first recorded under an invalid name keeps that name and no location"
if grep -q 'head_0000000000000011' "$test_root/native.tsv"; then
    assert_success 1 "the invalid first match does not fall through to a later valid head"
else
    assert_success 0 "the invalid first match does not fall through to a later valid head"
fi

# ---- a state root outside HYDRA_HOME -------------------------------------------

make_project moved
moved_root="$test_root/moved/elsewhere/v2"
mkdir -p "$moved_root/projects"
mv "$project_dir" "$moved_root/projects/$project"
heads="$moved_root/projects/$project/heads"
make_head head_0000000000000020 moved session-moved none
# Queue entries are read under HYDRA_HOME whatever the state root is.
mkdir -p "$home/state/v2/projects/$project/queue"
printf '%s\n' branch=moved > "$home/state/v2/projects/$project/queue/1.queue"
same_rows "a state root outside HYDRA_HOME reads the same in both readers" \
    FAKE_TMUX_SESSIONS=session-moved HYDRA_STATE_V2_ROOT="$moved_root"
assert_equal "1 $moved_root/projects/$project/heads/head_0000000000000020" "$(row_fields moved 18 24)" \
    "records come from the state root and queue entries from HYDRA_HOME"

# ---- a project without workflow runs --------------------------------------------

# The control centre answers the workflow projection and the workspace links
# itself for a project that records no run (workflow_shortcut in
# src/tui/observations.c). These are the documents it writes: the CLI must
# print exactly them.
make_project norun
make_head head_0000000000000030 norun session-norun none
(cd "$repo" && HYDRA_HOME="$home" "$hydra" workflow tui-data) > "$test_root/workflow.tsv" 2> /dev/null
assert_equal "HYDRA_WORKFLOW_TUI	2" "$(cat "$test_root/workflow.tsv")" "without runs the workflow projection is its header"
(cd "$repo" && HYDRA_HOME="$home" "$hydra" workflow --workspace-links) > "$test_root/links.tsv" 2> /dev/null
assert_equal "HYDRA_WORKSPACE_LINKS	1
P	$project	$repo
Z" "$(cat "$test_root/links.tsv")" "without runs the workspace links are the project and its root"
mkdir -p "$project_dir/workflows/runs"
(cd "$repo" && HYDRA_HOME="$home" "$hydra" workflow tui-data) > "$test_root/workflow.tsv" 2> /dev/null
assert_equal "HYDRA_WORKFLOW_TUI	2" "$(cat "$test_root/workflow.tsv")" "an empty runs directory reads the same"
(cd "$test_root/unknown/repo" && HYDRA_HOME="$test_root/unknown/home" "$hydra" workflow --workspace-links) > "$test_root/links.tsv" 2> /dev/null
assert_failure $? "outside a project the workspace links reader fails"
assert_equal "" "$(cat "$test_root/links.tsv")" "and prints nothing"

# ---- the control centre starts its helper on the store --------------------------

cat > "$test_root/helper" <<'HELPER'
#!/bin/sh
if [ "${1:-}" = --version ]; then printf 'Hydra TUI %s protocol 2\n' "$HYDRA_VERSION"; exit 0; fi
printf '%s\n' "$@" > "$HELPER_ARGV"
HELPER
chmod +x "$test_root/helper"
(cd "$repo" && HYDRA_HOME="$home" HYDRA_TUI_BIN="$test_root/helper" HELPER_ARGV="$test_root/helper.argv" "$hydra" tui --view heads) > /dev/null 2>&1
assert_equal "--hydra $hydra --store --view heads" "$(tr '\n' ' ' < "$test_root/helper.argv" | sed 's/ $//')" \
    "hydra tui tells the helper to read the store itself"
(cd "$repo" && HYDRA_HOME="$home" "$hydra" tui --orphan-rows extra) > /dev/null 2>&1
assert_equal "2" "$?" "the leftover-rows entry takes no arguments"

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
