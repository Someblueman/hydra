#!/bin/sh
# Authoritative state-v2 query and mutation tests.

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo="$test_root/repo"
HYDRA_HOME="$test_root/home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
export HYDRA_HOME HYDRA_STATE_V2_ROOT HYDRA_LIB_DIR

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/output.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/identity.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state_v2.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state.sh"

cleanup() {
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$repo"
git -C "$repo" init -q
git -C "$repo" config user.name Test
git -C "$repo" config user.email test@example.com
git -C "$repo" commit --allow-empty -qm init
cd "$repo" || exit 1
project_id="$(hydra_ensure_project_id)"

echo "Running authoritative state tests..."
echo "===================================="

state_v2_create_head "$project_id" feature-one session-one claude backend 100 dep-a 7 "$repo" >/dev/null
assert_success $? "head record is created"
state_v2_create_head "$project_id" feature-two session-two aider - 200 - - "$repo" \
    "" "" "" "" "" "" "" observed-exit-zero >/dev/null
assert_success $? "second head record is created"
head_two="$(state_v2_find_head_by_branch "$project_id" feature-two)"
assert_equal observed-exit-zero \
    "$(sed -n '1p' "$(state_v2_head_dir "$project_id" "$head_two")/completion-policy")" \
    "head creation commits the requested completion policy"

rows="$(state_list_heads)"
assert_equal 2 "$(printf '%s\n' "$rows" | wc -l | tr -d ' ')" "active records are listed"
assert_equal session-one "$(get_session_for_branch feature-one)" "session lookup uses durable state"
assert_equal feature-two "$(get_branch_for_session session-two)" "branch lookup uses durable state"
assert_equal "feature-one session-one claude backend 100 dep-a 7" \
    "$(printf '%s\n' "$rows" | awk '$1 == "feature-one"')" \
    "a listed row carries profile, group, creation time, dependencies and PR"
assert_equal backend "$(get_group_for_branch feature-one)" "group lookup uses durable state"
assert_equal dep-a "$(get_deps_for_branch feature-one)" "dependency lookup uses durable state"
assert_equal 7 "$(get_pr_for_branch feature-one)" "PR lookup uses durable state"

set_group feature-two frontend
assert_success $? "group mutation succeeds"
set_pr_for_branch feature-two 12
assert_success $? "PR mutation succeeds"
assert_equal frontend "$(get_group_for_branch feature-two)" "group mutation persists"
assert_equal 12 "$(get_pr_for_branch feature-two)" "PR mutation persists"

assert_equal backend "$(list_groups | sed -n '1p')" "groups are sorted"
assert_equal frontend "$(list_groups | sed -n '2p')" "all groups are returned"
assert_equal feature-two "$(state_list_heads_for_group frontend | awk '{print $1}')" "group filtering is exact"

head_one="$(state_v2_find_head_by_branch "$project_id" feature-one)"
state_v2_write_scalar "$(state_v2_head_dir "$project_id" "$head_one")/desired-state" stopped
assert_equal feature-two "$(state_list_heads | awk '{print $1}')" "stopped records are excluded"

try_lock "state_${project_id}" "test contention"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
set_group feature-two blocked >/dev/null 2>&1
assert_failure $? "mutation fails closed during state-lock contention"
assert_equal frontend "$(get_group_for_branch feature-two)" "failed mutation preserves durable state"
release_lock "state_${project_id}"
unset HYDRA_LOCK_RETRIES

state_v2_verify
assert_success $? "mutated state remains valid"

head_two_dir="$(state_v2_head_dir "$project_id" "$head_two")"
mv "$head_two_dir/group" "$head_two_dir/group.missing"
state_v2_verify >/dev/null 2>&1
assert_failure $? "verification rejects a missing required scalar"
mv "$head_two_dir/group.missing" "$head_two_dir/group"
printf 'declared\n' > "$head_two_dir/completion-policy"
state_v2_verify >/dev/null 2>&1
assert_failure $? "verification rejects an unsupported completion policy"
printf 'observed-exit-zero\n' > "$head_two_dir/completion-policy"
state_v2_verify
assert_success $? "repaired required state verifies"

# state_v2_read_scalar gives what "$(sed -n 1p FILE 2>/dev/null || true)" gave.
scalars="$test_root/scalars"
mkdir -p "$scalars/directory"
printf 'plain\n' > "$scalars/plain"
printf 'first\nsecond\n' > "$scalars/two-lines"
printf 'value\r\n' > "$scalars/carriage-return"
printf 'unterminated' > "$scalars/no-newline"
: > "$scalars/empty"
printf '\nsecond\n' > "$scalars/empty-first-line"
printf '  padded  \n' > "$scalars/spaces"
printf 'back\\slash * ? [x] ~ #\n' > "$scalars/special"
for scalar_case in plain two-lines carriage-return no-newline empty empty-first-line spaces special \
    directory missing; do
    STATE_V2_SCALAR=stale
    state_v2_read_scalar "$scalars/$scalar_case"
    assert_equal "0:$(sed -n '1p' "$scalars/$scalar_case" 2>/dev/null || true)" "$?:$STATE_V2_SCALAR" \
        "the scalar reader agrees with sed for a $scalar_case record"
done
state_v2_read_scalar "$scalars/two-lines"
assert_equal first "$STATE_V2_SCALAR" "the scalar reader returns only the first line"
state_v2_read_scalar "$scalars/carriage-return"
assert_equal "$(printf 'value\r')" "$STATE_V2_SCALAR" "the scalar reader keeps a carriage return"
state_v2_read_scalar "$scalars/no-newline"
assert_equal unterminated "$STATE_V2_SCALAR" "the scalar reader returns a line that has no newline"
state_v2_read_scalar "$scalars/spaces"
assert_equal "  padded  " "$STATE_V2_SCALAR" "the scalar reader keeps leading and trailing spaces"
STATE_V2_SCALAR=stale
state_v2_read_scalar "$scalars/missing" 2> "$scalars/stderr"
assert_equal ":0" "$STATE_V2_SCALAR:$(wc -c < "$scalars/stderr" | tr -d ' ')" \
    "a missing record reads as empty and prints nothing"
(
    # No command can be found, so only the shell itself can do the reading.
    # shellcheck disable=SC2030,SC2123
    PATH=/nonexistent
    state_v2_read_scalar "$scalars/plain"
    [ "$STATE_V2_SCALAR" = plain ]
)
assert_success $? "the scalar reader starts no process"
(
    set -e
    state_v2_read_scalar "$scalars/missing"
    state_v2_lookup_head "$project_id" feature-two
    [ "$STATE_V2_HEAD_ID" = "$head_two" ]
)
assert_success $? "a missing record does not stop a set -e caller"
state_v2_lookup_head "$project_id" no-such-branch
assert_equal "1::" "$?:$STATE_V2_HEAD_ID:$STATE_V2_HEAD_DIR" "an unknown branch has no head and leaves no stale result"

# The project id lookup asks Git once per working directory, not per call.
other="$test_root/other"
mkdir -p "$other" "$test_root/plain" "$test_root/bin"
git -C "$other" init -q
git -C "$other" config user.name Test
git -C "$other" config user.email test@example.com
git -C "$other" commit --allow-empty -qm init
# A git on PATH that records each call before running the real one.
git_calls="$test_root/git-calls"
printf '#!/bin/sh\necho call >> "%s"\nexec "%s" "$@"\n' "$git_calls" "$(command -v git)" > "$test_root/bin/git"
chmod +x "$test_root/bin/git"
# shellcheck disable=SC2031 # the subshell above changed its own copy
saved_path="$PATH"
# shellcheck disable=SC2031
PATH="$test_root/bin:$PATH"
hydra_load_project_id
: > "$git_calls"
hydra_load_project_id && hydra_load_project_id && lookup_id="$(hydra_get_project_id)"
assert_equal "0:$project_id:0" "$?:$lookup_id:$(wc -l < "$git_calls" | tr -d ' ')" \
    "repeated project id lookups in one directory do not run Git again"
cd "$other" || exit 1
hydra_load_project_id
assert_equal "1:" "$?:$HYDRA_LOADED_PROJECT_ID" "another repository is not answered from the previous one's id"
other_id="$(hydra_ensure_project_id)"
hydra_load_project_id
assert_equal "0:$other_id" "$?:$HYDRA_LOADED_PROJECT_ID" "an id written after a failed lookup is seen"
[ "$other_id" != "$project_id" ]
assert_success $? "the two repositories have different ids"
cd "$test_root/plain" || exit 1
hydra_load_project_id
assert_equal "1:" "$?:$HYDRA_LOADED_PROJECT_ID" "a directory outside any repository has no project id"
cd "$repo" || exit 1
assert_equal "$other_id" "$(cd "$other" && hydra_get_project_id)" "a subshell that changes directory gets that repository's id"
hydra_load_project_id
assert_equal "0:$project_id" "$?:$HYDRA_LOADED_PROJECT_ID" "returning to the first repository returns its id"
PATH="$saved_path"

# Lookup by branch: the first record in name order wins whatever its state,
# and a record's branch is read once per process, not once per lookup.
for lookup_case in 1:gone-a 2:gone-b 3:again 4:gone-c 5:live; do
    state_v2_create_head "$other_id" "${lookup_case#*:}" "s-${lookup_case#*:}" - - 100 - - "$other" \
        "head_00000000000000000${lookup_case%%:*}00" >/dev/null
done
other_heads="$HYDRA_STATE_V2_ROOT/projects/$other_id/heads"
for lookup_case in 1 2 3 4; do
    state_v2_write_scalar "$other_heads/head_00000000000000000${lookup_case}00/desired-state" stopped
done
# A separate shell counts how often a lookup opens a branch record (the
# reader is replaced by a counting copy there).
cat > "$test_root/count-reads.sh" <<'EOF'
for library in output locks identity state_v2; do . "$HYDRA_LIB_DIR/$library.sh"; done
branch_reads=0
state_v2_read_scalar() {
    case "$1" in */branch) branch_reads=$((branch_reads + 1)) ;; esac
    STATE_V2_SCALAR=
    { IFS= read -r STATE_V2_SCALAR < "$1"; } 2>/dev/null || :
}
report() { printf '%s=%s:%s:%s ' "$1" "$2" "$STATE_V2_HEAD_ID" "$branch_reads"; branch_reads=0; }
state_v2_lookup_head "$1" live; report first "$?"
state_v2_lookup_head "$1" live; state_v2_lookup_head "$1" live; report repeated "$?"
state_v2_lookup_head "$1" again; report stopped "$?"
state_v2_lookup_head "$1" never-spawned; report unknown "$?"
EOF
assert_equal "first=0:head_00000000000000000500:5 repeated=0:head_00000000000000000500:2 stopped=0:head_00000000000000000300:1 unknown=1::0 " \
    "$(sh "$test_root/count-reads.sh" "$other_id")" \
    "a branch record is read once per process: repeated lookups confirm one record, and no stopped record is rescanned"
state_v2_lookup_head "$other_id" again
assert_equal "0:head_00000000000000000300:$other_heads/head_00000000000000000300" \
    "$?:$STATE_V2_HEAD_ID:$STATE_V2_HEAD_DIR" "a stopped head still owns its branch"
# Killed, then spawned again: the branch keeps its one record.
assert_equal head_00000000000000000300 \
    "$(state_v2_create_head "$other_id" again s-again - - 200 - - "$other")" \
    "spawning a stopped branch again resolves to its existing record"
state_v2_write_scalar "$other_heads/head_00000000000000000300/desired-state" running
assert_equal head_00000000000000000300 "$(state_v2_find_head_by_branch "$other_id" again)" \
    "a killed and respawned branch resolves to the same head"
state_v2_lookup_head "$other_id" live
state_v2_create_head "$other_id" late s-late - - 300 - - "$other" head_00000000000000000050 >/dev/null
state_v2_lookup_head "$other_id" late
assert_equal "0:head_00000000000000000050" "$?:$STATE_V2_HEAD_ID" "a head created after earlier lookups is found"
# Two records for one branch are invalid state (state_v2_verify reports them).
# The first in name order answers, also when it is stopped and also when it
# appears after this process first resolved the branch.
state_v2_lookup_head "$other_id" live
cp -R "$other_heads/head_00000000000000000500" "$other_heads/head_00000000000000000450"
state_v2_write_scalar "$other_heads/head_00000000000000000450/desired-state" stopped
state_v2_lookup_head "$other_id" live
first_lookup="$STATE_V2_HEAD_ID"
state_v2_lookup_head "$other_id" live
assert_equal "head_00000000000000000450:head_00000000000000000450" "$first_lookup:$STATE_V2_HEAD_ID" \
    "with two records for a branch the first in name order wins on every lookup"
rm -rf "$other_heads/head_00000000000000000450"
state_v2_lookup_head "$other_id" live
assert_equal head_00000000000000000500 "$STATE_V2_HEAD_ID" "a removed record is not answered from memory"
state_v2_lookup_head "$other_id" "$(printf 'live\thead_00000000000000000500')"
assert_equal "1:" "$?:$STATE_V2_HEAD_ID" "a name with a tab matches no record"
state_v2_lookup_head "$other_id" live
assert_equal head_00000000000000000500 "$STATE_V2_HEAD_ID" "lookups still resolve after a name that cannot be indexed"

# Two heads of one project on one branch: each is reported, by branch and
# project. A branch that only begins like another is not a duplicate.
head_one_dir="$(state_v2_head_dir "$project_id" "$head_one")"
project_dir="$(state_v2_project_dir "$project_id")"
printf 'feature-two\n' > "$head_one_dir/branch"
duplicate_output="$(state_v2_verify 2>&1)"
assert_failure $? "verification rejects two heads on one branch"
duplicate_line="state v2: duplicate branch identity 'feature-two' in $project_dir"
assert_equal "$duplicate_line
$duplicate_line" "$duplicate_output" "verification names the shared branch and its project once per head"
third_head="$(state_v2_create_head "$project_id" feature-three session-three - - 300 - - "$repo")"
assert_equal "$duplicate_line
$duplicate_line" "$(state_v2_verify 2>&1)" "a head on its own branch is not reported with the shared one"
printf 'feature-tw\n' > "$head_one_dir/branch"
state_v2_verify
assert_success $? "a branch that begins like another is not a duplicate"
rm "$head_one_dir/branch" "$(state_v2_head_dir "$project_id" "$third_head")/branch"
assert_equal 2 "$(state_v2_verify 2>&1 | grep -c "^state v2: duplicate branch identity '' in $project_dir\$")" \
    "heads without a branch record are reported as sharing the empty branch"
printf 'feature-one\n' > "$head_one_dir/branch"
printf 'feature-three\n' > "$(state_v2_head_dir "$project_id" "$third_head")/branch"
state_v2_verify
assert_success $? "distinct branches verify again"

echo "===================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
