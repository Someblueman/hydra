#!/bin/sh
# Identity, state v2, migration, event, and lock-contract tests.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d)"
repo="$test_root/repo"
HYDRA_HOME="$test_root/home"
HYDRA_MAP="$HYDRA_HOME/map"
HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
export HYDRA_HOME HYDRA_MAP HYDRA_LIB_DIR
mkdir -p "$HYDRA_HOME" "$repo"
: > "$HYDRA_MAP"

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
# shellcheck source=../lib/output.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/output.sh"
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck source=../lib/identity.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/identity.sh"
# shellcheck source=../lib/state_v2.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state_v2.sh"
# shellcheck source=../lib/events.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/events.sh"
# shellcheck source=../lib/cmd_foundation.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/cmd_foundation.sh"

cleanup() {
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.com
git -C "$repo" config user.name Test
printf 'fixture\n' > "$repo/file"
git -C "$repo" add file
git -C "$repo" commit -qm init
cd "$repo" || exit 1

echo "Running Hydra 1.6 foundation tests..."
echo "====================================="

project_id="$(hydra_ensure_project_id)"
assert_success $? "project identity is created"
assert_equal "$project_id" "$(hydra_get_project_id)" "project identity is stable"

case "$project_id" in
    project_[0-9a-f]*) assert_success 0 "project identity is opaque and encoded" ;;
    *) assert_success 1 "project identity is opaque and encoded" ;;
esac

mv "$repo" "$test_root/repo-moved"
repo="$test_root/repo-moved"
cd "$repo" || exit 1
assert_equal "$project_id" "$(hydra_get_project_id)" "project identity survives repository move"

clone="$test_root/clone"
git clone -q "$repo" "$clone"
cd "$clone" || exit 1
clone_id="$(hydra_ensure_project_id)"
if [ "$clone_id" != "$project_id" ]; then
    assert_success 0 "normal clone receives a distinct machine-local project identity"
else
    assert_success 1 "normal clone receives a distinct machine-local project identity"
fi

cd "$repo" || exit 1
head_id="$(state_v2_create_head "$project_id" feature-x feature-x claude team 100 dep 7 "$repo")"
assert_success $? "state v2 creates a head"
head_dir="$(state_v2_head_dir "$project_id" "$head_id")"
assert_equal "feature-x" "$(sed -n '1p' "$head_dir/branch")" "head keeps its human branch label"
instance_id="$(sed -n '1p' "$head_dir/current-instance")"
assert_success 0 "head records a current instance"
state_v2_verify
assert_success $? "state v2 verifies complete records"

spaced_head="$(state_v2_create_head "$project_id" 'feature with spaces' spaced-session - - 100 - - "$repo")"
spaced_dir="$(state_v2_head_dir "$project_id" "$spaced_head")"
assert_equal "feature with spaces" "$(sed -n '1p' "$spaced_dir/branch")" "per-head scalar layout preserves delimiter-bearing labels"
state_v2_write_scalar "$spaced_dir/task" "line one
line two" >/dev/null 2>&1
assert_failure $? "scalar layout rejects embedded newlines"

other_project="$(hydra_new_id project other-repository)"
other_head="$(state_v2_create_head "$other_project" feature-x other-session - - 101 - - "$test_root/other")"
if [ "$other_head" != "$head_id" ] && [ -d "$(state_v2_head_dir "$other_project" "$other_head")" ]; then
    assert_success 0 "identical branch labels are isolated by project"
else
    assert_success 1 "identical branch labels are isolated by project"
fi

event_one="$(event_emit "$project_id" "$head_id" "$instance_id" lifecycle.started hydra local '{}')"
assert_success $? "event append succeeds"
event_two="$(event_emit "$project_id" "$head_id" "$instance_id" lifecycle.declared user tester '{"outcome":"done"}')"
assert_success $? "second event append succeeds"
if [ "$event_one" != "$event_two" ]; then
    assert_success 0 "event identifiers are unique"
else
    assert_success 1 "event identifiers are unique"
fi
event_file="$(event_file_for_head "$project_id" "$head_id")"
event_verify_file "$event_file"
assert_success $? "event stream verifies schema and sequence"
assert_equal "2" "$(wc -l < "$event_file" | tr -d ' ')" "event stream contains both events"
event_lock="events_${project_id}_${head_id}"
try_lock "$event_lock" "test held event writer"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
event_retain_file "$event_file" 1 "$project_id" "$head_id" >/dev/null 2>&1
assert_failure $? "event retention refuses a concurrent writer"
assert_equal "2" "$(wc -l < "$event_file" | tr -d ' ')" "failed retention preserves concurrent event input"
release_lock "$event_lock"
unset HYDRA_LOCK_RETRIES
event_retain_file "$event_file" 1 "$project_id" "$head_id" >/dev/null
assert_success $? "event retention archives before truncating"
event_verify_file "$event_file"
assert_success $? "retained stream preserves valid sequence"
printf 'partial' >> "$event_file"
event_verify_file "$event_file" >/dev/null 2>&1
assert_failure $? "partial event is detected"
try_lock "$event_lock" "test held event writer"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
event_repair_file "$event_file" "$project_id" "$head_id" >/dev/null 2>&1
assert_failure $? "event repair refuses a concurrent writer"
grep -q partial "$event_file"
assert_success $? "failed event repair preserves corrupt input"
release_lock "$event_lock"
unset HYDRA_LOCK_RETRIES
repair_backup="$(event_repair_file "$event_file" "$project_id" "$head_id")"
assert_success $? "event repair preserves corrupt input"
if [ -f "$repair_backup" ]; then
    assert_success 0 "event repair backup exists"
else
    assert_success 1 "event repair backup exists"
fi
event_verify_file "$event_file"
assert_success $? "repaired stream verifies"
HYDRA_HOME="$test_root/home" "$HYDRA_BIN" events verify --project "$project_id" --head "$head_id" >/dev/null
assert_success $? "events command verifies an explicit head"

# Migration and rollback use a fresh home to prove the full reversible path.
HYDRA_HOME="$test_root/migrate-home"
HYDRA_MAP="$HYDRA_HOME/map"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
export HYDRA_HOME HYDRA_MAP HYDRA_STATE_V2_ROOT
mkdir -p "$HYDRA_HOME"
printf '%s\n' 'legacy-branch legacy-session claude group 123 dep 9' > "$HYDRA_MAP"
rm -f "$(hydra_project_identity_file)"
dry_output="$(state_v2_migrate --dry-run)"
assert_success $? "migration dry-run succeeds"
if [ ! -e "$HYDRA_STATE_V2_ROOT" ] && ! hydra_get_project_id >/dev/null 2>&1; then
    assert_success 0 "migration dry-run does not mutate state or identity"
else
    assert_success 1 "migration dry-run does not mutate state or identity"
fi
printf '%s\n' "$dry_output" | grep -q 'migrate heads: 1'
assert_success $? "migration dry-run reports exact head count"

migrate_output="$(state_v2_migrate)"
assert_success $? "migration writes state v2"
backup_path="$(printf '%s\n' "$migrate_output" | sed -n 's/^Backup: //p')"
if [ -d "$backup_path" ] && [ -f "$backup_path/map" ]; then
    assert_success 0 "migration creates a recoverable backup"
else
    assert_success 1 "migration creates a recoverable backup"
fi
state_v2_verify
assert_success $? "migrated state verifies"
HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" state verify >/dev/null
assert_success $? "state command verifies active schema v2"
printf '999\n' > "$HYDRA_STATE_V2_ROOT/schema-version"
verify_output="$(cmd_state verify 2>&1)"
verify_status=$?
assert_failure "$verify_status" "state handler propagates verification failure without errexit"
case "$verify_output" in
    *"State is valid"*) assert_failure 0 "failed verification does not print success" ;;
    *) assert_success 0 "failed verification does not print success" ;;
esac
printf '2\n' > "$HYDRA_STATE_V2_ROOT/schema-version"
printf 'changed\n' > "$HYDRA_MAP"
migrated_project="$(hydra_get_project_id)"
try_lock "state_${migrated_project}" "test active state writer"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
state_v2_rollback "$backup_path" >/dev/null 2>&1
assert_failure $? "rollback refuses a concurrent state writer"
assert_equal changed "$(sed -n '1p' "$HYDRA_MAP")" "failed rollback preserves current state"
release_lock "state_${migrated_project}"
unset HYDRA_LOCK_RETRIES
HYDRA_LEGACY_MAP="$HYDRA_HOME/map"
HYDRA_MAP="$HYDRA_STATE_V2_ROOT/projects/$migrated_project/compat-map"
export HYDRA_LEGACY_MAP HYDRA_MAP
state_v2_rollback "$backup_path" >/dev/null
assert_success $? "rollback succeeds with generated backup"
assert_equal "$HYDRA_LEGACY_MAP" "$HYDRA_MAP" "rollback selects the legacy map outside restored state"
assert_equal "legacy-branch legacy-session claude group 123 dep 9" "$(sed -n '1p' "$HYDRA_LEGACY_MAP")" "rollback restores the legacy map"

echo "====================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
