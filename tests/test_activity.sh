#!/bin/sh
# Inferred head activity: the adapter activity and watch entry points, the
# event they append, duplicate suppression and observer coverage.
set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
hydra="$root/bin/hydra"
mkdir -p "$repo" "$HYDRA_HOME"
# shellcheck source=/dev/null
. "$root/tests/helpers.sh"
# shellcheck source=/dev/null
. "$root/lib/output.sh"
# shellcheck source=/dev/null
. "$root/lib/locks.sh"
# shellcheck source=/dev/null
. "$root/lib/identity.sh"
# shellcheck source=/dev/null
. "$root/lib/state_v2.sh"
# shellcheck source=/dev/null
. "$root/lib/events.sh"
# shellcheck source=/dev/null
. "$root/lib/lifecycle.sh"
# shellcheck source=/dev/null
. "$root/lib/activity.sh"

cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name Test
git -C "$repo" commit --allow-empty -qm fixture
cd "$repo" || exit 1
project="$(hydra_ensure_project_id)"
started=$(($(date +%s) - 600))
head="$(state_v2_create_head "$project" watched watched-session - - "$started" - - "$repo")"
head_dir="$(state_v2_head_dir "$project" "$head")"
instance="$(sed -n '1p' "$head_dir/current-instance")"
event_file="$(event_file_for_head "$project" "$head")"
activity_file="$(event_activity_file "$project" "$head" "$instance")"
event_emit "$project" "$head" "$instance" lifecycle.started hydra local '{"profile":"none"}' >/dev/null || exit 1

activity_json() {
    printf '{"schema_version":1,"instance_id":"%s","kind":"activity","state":"%s","since":%s}\n' "$1" "$2" "$3"
}
activity_events() { if [ -f "$activity_file" ]; then grep -c '"type":"activity.changed"' "$activity_file"; else echo 0; fi; }
assert_contains() {
    case "$1" in *"$2"*) assert_success 0 "$3" ;; *) assert_success 1 "$3" ;; esac
}

echo "Running activity tests..."
echo "======================================"

now="$(date +%s)"
before="$(wc -l < "$event_file" | tr -d ' ')"
for bad in 'not-json' \
    "{\"schema_version\":2,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":$now}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"observed\",\"state\":\"active\",\"since\":$now}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"idle\",\"since\":$now}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":0}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":-5}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":\"$now\"}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":99999999999}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":$now,\"source\":\"me\"}" \
    "{\"schema_version\":1,\"instance_id\":\"../$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":$now}" \
    "{\"schema_version\":1,\"instance_id\":\"$instance\",\"kind\":\"activity\",\"state\":\"active\",\"since\":$((now + 3600))}"; do
    printf '%s\n' "$bad" | "$hydra" adapter activity watched >/dev/null 2>&1
    assert_failure $? "rejects: $(printf '%s' "$bad" | cut -c 1-70)"
done
{ activity_json "$instance" active "$now"; awk 'BEGIN { for (i = 0; i < 1100; i++) printf " " }'; } |
    "$hydra" adapter activity watched >/dev/null 2>&1
assert_failure $? "rejects input over 1024 bytes"
activity_json "$instance" active "$now" | "$hydra" adapter activity >/dev/null 2>&1
assert_failure $? "requires a branch"
activity_json "$instance" active "$now" | "$hydra" adapter activity missing-branch >/dev/null 2>&1
assert_failure $? "rejects an unknown head"
assert_equal "$before" "$(wc -l < "$event_file" | tr -d ' ')" "rejected input appends nothing"
if [ -e "$head_dir/instances/$instance/activity-state" ]; then status=1; else status=0; fi
assert_success "$status" "rejected input records no state"

output="$(activity_json "$instance" active $((now - 30)) | "$hydra" adapter activity watched 2>&1)"
assert_success $? "an activity change is recorded"
assert_contains "$output" "inferred from tmux" "the confirmation names the inference"
assert_equal 1 "$(activity_events)" "one event is appended to the instance's activity log"
assert_equal "$before" "$(wc -l < "$event_file" | tr -d ' ')" "the head's event log keeps only lifecycle events"
last="$(tail -n 1 "$activity_file")"
assert_contains "$last" "\"sequence\":1,\"head_next_sequence\":$((before + 1))," \
    "the event is numbered in its own log and placed before the head's next event"
assert_contains "$last" "\"payload\":{\"state\":\"active\",\"since\":$((now - 30)),\"source\":\"tmux\",\"confidence\":\"inferred\"}" \
    "the event carries state, since, source and confidence"
assert_contains "$last" "\"instance_id\":\"$instance\"" "the event is correlated to the instance"
assert_contains "$last" '"actor":{"kind":"observer","id":"tmux"}' "the event names its observer"
assert_equal active "$(sed -n '1p' "$head_dir/instances/$instance/activity-state")" "the last recorded state is kept"
assert_equal starting "$(sed -n '1p' "$head_dir/instances/$instance/observed-status")" "observed status is not overloaded"

output="$(activity_json "$instance" active $((now - 5)) | "$hydra" adapter activity watched 2>&1)"
assert_success $? "a repeated state is accepted"
assert_contains "$output" "Unchanged" "a repeated state is reported unchanged"
assert_equal 1 "$(activity_events)" "a repeated state appends nothing"

# Several observers reporting the same change record it once.
observers=0
while [ "$observers" -lt 4 ]; do
    activity_json "$instance" quiet $((now - 3)) | HYDRA_LOCK_RETRIES=200 "$hydra" adapter activity watched >/dev/null 2>&1 &
    observers=$((observers + 1))
done
wait
assert_equal 2 "$(activity_events)" "concurrent observers record one change"
assert_equal quiet "$(sed -n '1p' "$head_dir/instances/$instance/activity-state")" "the change is the recorded state"

activity_json "$instance" active $((started - 100)) | "$hydra" adapter activity watched >/dev/null 2>&1
assert_success $? "a state older than its instance is accepted"
assert_contains "$(tail -n 1 "$activity_file")" "\"since\":$started," "and begins with the instance"

event_verify_file "$event_file" >/dev/null 2>&1
assert_success $? "the event log still verifies"
event_verify_file "$activity_file" >/dev/null 2>&1
assert_success $? "the activity log verifies"
if [ -x "${HYDRA_CORE:-}" ]; then
    "$HYDRA_CORE" validate-events "$event_file" >/dev/null 2>&1
    assert_success $? "the native validator accepts the event log"
    "$HYDRA_CORE" validate-events "$activity_file" >/dev/null 2>&1
    assert_success $? "the native validator accepts the activity log"
fi
# Head event retention archives lifecycle events; the activity of a session
# that is still open and recent stays in its log.
event_emit "$project" "$head" "$instance" lifecycle.observed adapter hydra '{"status":"running","confidence":"exact","exit_code":null}' >/dev/null
"$hydra" events retain --project "$project" --head "$head" --max-events 1 --archive-keep-seconds 60 --apply >/dev/null 2>&1
assert_success $? "head events archive with activity kept apart"
assert_equal 1 "$(wc -l < "$event_file" | tr -d ' ')" "the head log holds its tail"
assert_equal 3 "$(activity_events)" "recent activity of an open session is kept"
activity_json "$instance" quiet "$now" | "$hydra" adapter activity watched >/dev/null 2>&1
assert_success $? "recording continues after archiving"
event_verify_file "$activity_file" >/dev/null 2>&1
assert_success $? "the activity log still verifies"
assert_contains "$(tail -n 1 "$activity_file")" "\"sequence\":4,\"head_next_sequence\":3," \
    "the activity log continues its sequence and follows the archived head events"

# The new event type is not a notification.
"$hydra" notify enable activity.changed --sink terminal --interval 0 >/dev/null 2>&1
assert_failure $? "activity changes cannot be enabled as notifications"
"$hydra" notify enable lifecycle.observed --sink terminal --interval 0 >/dev/null 2>&1
output="$(activity_json "$instance" active "$now" | "$hydra" adapter activity watched 2>&1)"
case "$output" in *'[notify]'*) assert_success 1 "an activity change notifies nobody" ;; *) assert_success 0 "an activity change notifies nobody" ;; esac
"$hydra" notify disable lifecycle.observed >/dev/null 2>&1

# Resume supersedes the instance; its observer's late report is stale.
(lifecycle_new_instance watched watched-session-2 '' resume >/dev/null && lifecycle_commit_new_instance)
new_instance="$(sed -n '1p' "$head_dir/current-instance")"
count="$(activity_events)"
activity_json "$instance" quiet "$now" | "$hydra" adapter activity watched >/dev/null 2>&1
assert_failure $? "a stale instance is rejected"
assert_equal "$count" "$(activity_events)" "a stale instance appends nothing"
activity_json "$new_instance" quiet "$now" | "$hydra" adapter activity watched >/dev/null 2>&1
assert_success $? "the current instance is accepted after resume"

# Terminal-free and stopped heads are not observed.
headless="$(state_v2_create_head "$project" quiet-worker - - - "$started" - - "$repo" '' '' '' '' '' '' '' declared-done headless)"
headless_dir="$(state_v2_head_dir "$project" "$headless")"
headless_instance="$(sed -n '1p' "$headless_dir/current-instance")"
activity_json "$headless_instance" active "$now" | "$hydra" adapter activity quiet-worker >/dev/null 2>&1
assert_failure $? "a headless head is not observed"
assert_equal 0 "$(grep -c 'activity.changed' "$headless_dir/events/events.jsonl")" "a headless head records no activity"
state_v2_write_scalar "$head_dir/desired-state" stopped
activity_json "$new_instance" active "$now" | "$hydra" adapter activity watched >/dev/null 2>&1
assert_failure $? "a stopped head is not observed"
state_v2_write_scalar "$head_dir/desired-state" running

# Observer coverage: one interval while the observer keeps reporting, a new
# one after it was away, bounded, and never silently trusted when damaged.
coverage="$(activity_coverage_file "$project")"
if [ -f "$coverage" ]; then status=0; else status=1; fi
assert_success "$status" "recording activity also records coverage"
rm -f "$coverage"
"$hydra" adapter watch >/dev/null 2>&1
assert_success $? "adapter watch records coverage"
"$hydra" adapter watch extra >/dev/null 2>&1
assert_failure $? "adapter watch takes no arguments"
rm -f "$coverage"
activity_observer_beat "$project" 1000
activity_observer_beat "$project" 1010
activity_observer_beat "$project" $((1010 + HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS))
assert_equal "$(printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n1000\t%s\n' $((1010 + HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS)))" \
    "$(cat "$coverage")" "reports within the stale window extend one interval"
activity_observer_beat "$project" 2000
activity_observer_beat "$project" 1500
assert_equal "$(printf '2000\t2000\n')" "$(tail -n 1 "$coverage")" "a report after a gap starts a new interval"
assert_equal 3 "$(wc -l < "$coverage" | tr -d ' ')" "an out-of-order report changes nothing"
assert_equal 600 "$(stat -c %a "$coverage" 2>/dev/null || stat -f %Lp "$coverage")" "coverage is private"
# shellcheck disable=SC2034 # Read by activity_observer_beat.
(HYDRA_ACTIVITY_COVERAGE_MAX_INTERVALS=2; activity_observer_beat "$project" 3000)
assert_equal "$(printf 'HYDRA_ACTIVITY_COVERAGE\t1\t%s\n2000\t2000\n3000\t3000\n' $((1010 + HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS)))" \
    "$(cat "$coverage")" "dropped intervals leave earlier time unknown"
printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n5000\t4000\n' > "$coverage"
activity_observer_beat "$project" 6000
assert_equal "$(printf 'HYDRA_ACTIVITY_COVERAGE\t1\t6000\n6000\t6000\n')" "$(cat "$coverage")" \
    "damaged coverage is set aside and earlier time is unknown"
[ -n "$(find "$(dirname "$coverage")" -name 'coverage.corrupt.*')" ]
assert_success $? "damaged coverage is kept for inspection"
rm -f "$coverage"
ln -s "$test_root/elsewhere" "$coverage"
activity_observer_beat "$project" 7000 2>/dev/null
assert_failure $? "symlinked coverage is refused"
[ ! -e "$test_root/elsewhere" ]
assert_success $? "nothing is written through the link"
rm -f "$coverage"

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
