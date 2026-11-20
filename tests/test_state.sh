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
