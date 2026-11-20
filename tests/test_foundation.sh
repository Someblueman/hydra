#!/bin/sh
# Identity, state v2, backup, event, and lock-contract tests.

set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo="$test_root/repo"
HYDRA_HOME="$test_root/home"
HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"
export HYDRA_HOME HYDRA_LIB_DIR
mkdir -p "$HYDRA_HOME" "$repo"

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
printf 'running\nstopped\n' > "$head_dir/desired-state"
state_v2_verify >/dev/null 2>&1
assert_failure $? "state verification rejects multiline scalar records"
state_v2_write_scalar "$head_dir/desired-state" running

spaced_head="$(state_v2_create_head "$project_id" 'feature with spaces' spaced-session - - 100 - - "$repo")"
spaced_dir="$(state_v2_head_dir "$project_id" "$spaced_head")"
assert_equal "feature with spaces" "$(sed -n '1p' "$spaced_dir/branch")" "per-head scalar layout preserves delimiter-bearing labels"
state_v2_write_scalar "$spaced_dir/task" "line one
line two" >/dev/null 2>&1
assert_failure $? "scalar layout rejects embedded newlines"

# A record write that fails must report failure and keep the previous value.
# The failure injected here is a real one: with the file size limit at zero
# and SIGXFSZ ignored, write(2) into the temporary file returns EFBIG, the way
# a full disk returns ENOSPC. The subshell keeps the limit away from the rest
# of this file; stderr goes to /dev/null because a log file is limited too.
failing_write() (
    trap '' XFSZ
    ulimit -f 0 || exit 99
    "$@" 2>/dev/null
)
write_probe="$test_root/write-probe"
write_probe_run() { printf 'probe\n' > "$write_probe"; }
failing_write write_probe_run
write_probe_status=$?
if [ "$write_probe_status" -gt 0 ] && [ "$write_probe_status" -lt 99 ] && [ ! -s "$write_probe" ]; then
    echo "write failure injection: file size limit"
else
    # No clean EFBIG on this platform (status $write_probe_status): stand in
    # for the failed write itself. The redirection still creates the empty
    # temporary file, as a failed write leaves it.
    failing_write() (
        # shellcheck disable=SC2317,SC2329 # Called by the library under test.
        printf() { return 1; }
        "$@" 2>/dev/null
    )
    echo "write failure injection: printf stub"
fi
rm -f "$write_probe"
failing_write state_v2_write_scalar "$head_dir/desired-state" stopped
assert_failure $? "a scalar write that cannot be written reports failure"
assert_equal running "$(cat "$head_dir/desired-state")" "a failed scalar write keeps the previous value"
assert_equal "" "$(find "$head_dir" -name '.hydra-tmp.*' -print)" "a failed scalar write leaves no temporary file"
state_v2_write_text "$test_root/text-record" 'first text'
assert_success $? "a text record is written"
failing_write state_v2_write_text "$test_root/text-record" 'second text'
assert_failure $? "a text write that cannot be written reports failure"
assert_equal 'first text' "$(cat "$test_root/text-record")" "a failed text write keeps the previous value"
assert_equal "" "$(find "$test_root" -maxdepth 1 -name '.hydra-tmp.*' -print)" "a failed text write leaves no temporary file"
state_v2_write_scalar "$head_dir/desired-state" stopped
assert_success $? "the scalar is writable again once writes succeed"

# Records are private (0600) whatever the umask, with no chmod after the
# write: mktemp_adjacent creates the temporary file that way.
for record_umask in 000 002 022 077; do
    (
        umask "$record_umask"
        state_v2_write_scalar "$test_root/mode-scalar-$record_umask" value &&
            state_v2_write_text "$test_root/mode-text-$record_umask" text &&
            mktemp_adjacent "$test_root/mode-temp-$record_umask" > "$test_root/mode-temp-path-$record_umask"
    )
    # ls -ld is the portable way to read one entry's mode (stat differs).
    # shellcheck disable=SC2012
    record_modes="$(ls -ld "$test_root/mode-scalar-$record_umask" "$test_root/mode-text-$record_umask" \
        "$(cat "$test_root/mode-temp-path-$record_umask")" | awk '{ printf "%s ", substr($1, 1, 10) }')"
    assert_equal "-rw------- -rw------- -rw------- " "$record_modes" \
        "records and their temporary files are 0600 under umask $record_umask"
done
umask_before="$(umask)"
mktemp_adjacent "$test_root/mode-direct" > /dev/null
assert_equal "$umask_before" "$(umask)" "mktemp_adjacent leaves the caller's umask alone"
# The temporary file is created where dirname puts the destination.
mkdir -p "$test_root/adjacent/sub"
for adjacent_dest in "$test_root/adjacent/sub/record" "$test_root/adjacent//sub/record" \
    "$test_root/adjacent/sub/" relative-record ./relative-record; do
    adjacent_tmp="$(cd "$test_root/adjacent" && mktemp_adjacent "$adjacent_dest")"
    assert_equal "$(dirname "$adjacent_dest")" "${adjacent_tmp%/.hydra-tmp.*}" \
        "the temporary file for $adjacent_dest is in that path's directory"
done
assert_equal stopped "$(cat "$head_dir/desired-state")" "a later scalar write replaces the value"
state_v2_write_scalar "$head_dir/desired-state" running

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
"$HYDRA_BIN" events retain --project "$project_id" --head "$head_id" --max-events 1 >/dev/null 2>&1
assert_failure $? "event retention refuses a concurrent writer"
assert_equal "2" "$(wc -l < "$event_file" | tr -d ' ')" "failed retention preserves concurrent event input"
release_lock "$event_lock"
unset HYDRA_LOCK_RETRIES
"$HYDRA_BIN" events retain --project "$project_id" --head "$head_id" --max-events 1 >/dev/null
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

# Backup and rollback use a fresh home.
HYDRA_HOME="$test_root/backup-home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
export HYDRA_HOME HYDRA_STATE_V2_ROOT
mkdir -p "$HYDRA_HOME"
state_v2_create_head "$project_id" backup-branch backup-session claude group 123 dep 9 "$repo" >/dev/null
backup_project_dir="$(state_v2_project_dir "$project_id")"
backup_head_id="$(state_v2_find_head_by_branch "$project_id" backup-branch)"

backup_count_before="$(find "$HYDRA_HOME/backups" -maxdepth 1 -type d -name 'state-*' 2>/dev/null | wc -l | tr -d ' ')"
# shellcheck disable=SC2317,SC2329
( cp() { return 1; }; state_v2_backup >/dev/null 2>&1 )
assert_failure $? "backup fails when a source cannot be copied"
backup_count_after="$(find "$HYDRA_HOME/backups" -maxdepth 1 -type d -name 'state-*' 2>/dev/null | wc -l | tr -d ' ')"
assert_equal "$backup_count_before" "$backup_count_after" "failed backup leaves no incomplete recovery point"

backup_output="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" state backup)"
assert_success $? "state backup succeeds"
backup_path="$(printf '%s\n' "$backup_output" | sed -n 's/^Backup: //p')"
assert_equal "state" "$(ls -A "$backup_path" 2>/dev/null)" "a backup holds the state tree and nothing else"
assert_equal 2 "$(sed -n '1p' "$backup_path/state/v2/schema-version" 2>/dev/null)" "the backed-up state names its schema"
state_v2_verify
assert_success $? "backed-up state verifies"
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
incomplete_backup="$HYDRA_HOME/backups/state-incomplete"
mkdir -p "$incomplete_backup"
state_v2_rollback "$incomplete_backup" >/dev/null 2>&1
assert_failure $? "rollback rejects a backup without durable state"
assert_equal 2 "$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/schema-version")" "incomplete backup rejection preserves current state"
try_lock "state_${project_id}" "test active state writer"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
state_v2_rollback "$backup_path" >/dev/null 2>&1
assert_failure $? "rollback refuses a concurrent state writer"
assert_equal 2 "$(sed -n '1p' "$HYDRA_STATE_V2_ROOT/schema-version")" "failed rollback preserves current state"
release_lock "state_${project_id}"
unset HYDRA_LOCK_RETRIES
printf 'changed-after-backup\n' > "$backup_project_dir/heads/$backup_head_id/group"
state_v2_rollback "$backup_path" >/dev/null
assert_success $? "rollback succeeds with generated backup"
assert_equal group "$(sed -n '1p' "$backup_project_dir/heads/$backup_head_id/group")" "rollback restores the backed-up records"

# A backup made by 4.0.0 also holds the marker "map.absent"; it restores as before.
: > "$backup_path/map.absent"
printf 'changed-again\n' > "$backup_project_dir/heads/$backup_head_id/group"
HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" state rollback "$backup_path" >/dev/null
assert_success $? "rollback accepts a backup made by 4.0.0"
assert_equal group "$(sed -n '1p' "$backup_project_dir/heads/$backup_head_id/group")" "a 4.0.0 backup restores the backed-up records"
rm -f "$backup_path/map.absent"

# Stores and backups that still hold what 1.x left behind are refused by name.
old_row='old-branch old-session claude group 123 dep 9'
printf 'changed-before-refusal\n' > "$backup_project_dir/heads/$backup_head_id/group"
printf '%s\n' "$old_row" > "$backup_path/map"
refusal="$(state_v2_rollback "$backup_path" 2>&1)"
assert_failure $? "rollback refuses a backup that holds a 1.x session map"
assert_equal "Error: the backup holds a Hydra 1.x session map, which this release cannot restore: $backup_path/map
Next: roll back with Hydra 4.0.0, or delete that file to restore the state without it" "$refusal" "the refused backup map is named with both ways out"
rm -f "$backup_path/map"
printf '%s\n' "$old_row" > "$backup_path/state/v2/projects/$project_id/compat-map"
refusal="$(state_v2_rollback "$backup_path" 2>&1)"
assert_failure $? "rollback refuses a backup that holds a 1.9 compatibility projection"
assert_equal "Error: the backup holds a Hydra 1.9 compatibility projection, which this release cannot restore: $backup_path/state/v2/projects/$project_id/compat-map
Next: roll back with Hydra 4.0.0, or delete that file to restore the state without it" "$refusal" "the refused backup projection is named with both ways out"
assert_equal changed-before-refusal "$(sed -n '1p' "$backup_project_dir/heads/$backup_head_id/group")" "a refused rollback leaves the current state untouched"
rm -f "$backup_path/state/v2/projects/$project_id/compat-map"

HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list >/dev/null 2>&1
assert_success $? "a store without 1.x leftovers is accepted"
printf '%s\n' "$old_row" > "$HYDRA_HOME/map"
for old_store_command in list status "state verify" "state backup" snapshot doctor "spawn refused-branch --no-agent"; do
    # shellcheck disable=SC2086 # the command and its arguments are separate words
    refusal="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" $old_store_command 2>&1)"
    assert_equal 1 "$?" "hydra $old_store_command stops on a 1.x session map"
    assert_equal "Error: $HYDRA_HOME/map is a Hydra 1.x session map, which this release does not read
Next: install Hydra 4.0.0 and run 'hydra state migrate' with it, or delete the file if nothing needs it" "$refusal" "hydra $old_store_command names the map and both ways out, and prints nothing else"
done
assert_equal "$old_row" "$(sed -n '1p' "$HYDRA_HOME/map")" "the refused map is left as it was"
for no_store_command in version help "completion bash"; do
    # shellcheck disable=SC2086
    HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" $no_store_command >/dev/null 2>&1
    assert_success $? "hydra $no_store_command needs no store and still answers"
done
rm -f "$HYDRA_HOME/map"
printf '%s\n' "$old_row" > "$backup_project_dir/compat-map"
refusal="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list 2>&1)"
assert_equal 1 "$?" "hydra list stops on a 1.9 compatibility projection"
assert_equal "Error: $backup_project_dir/compat-map is a Hydra 1.9 compatibility projection, which this release does not read
Next: install Hydra 4.0.0 and run 'hydra state migrate' with it, or delete the file if nothing needs it" "$refusal" "the projection is named with both ways out"
rm -f "$backup_project_dir/compat-map"
HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list >/dev/null 2>&1
assert_success $? "removing the leftover is enough to continue"
refusal="$(HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" state migrate 2>&1)"
assert_equal 1 "$?" "state migrate is not a state action any more"
assert_equal "Usage: hydra state <verify|backup|rollback>" "$refusal" "state migrate prints the state usage"

echo "====================================="
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

[ "$fail_count" -eq 0 ]
