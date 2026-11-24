#!/bin/sh
# The observer's maintenance pass (session_maintain_due) over a project's heads:
# which heads it works on, what it writes and removes for each, and that a head
# with nothing to decide is left byte for byte as it was and is not locked.
# Three heads in three states: one whose session ended while an observer could
# still extend its coverage (a summary is due), one open session whose activity
# ages past the horizon (a checkpoint is due), and one whose activity is younger
# than the horizon when the first pass runs.
set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 HYDRA_SKIP_AI=1
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
hydra="$root/bin/hydra"
mkdir -p "$repo"
# Hydra refuses a group-writable home; the caller's umask may be 002.
(umask 077; mkdir -p "$HYDRA_HOME")
unset TMUX TMUX_PANE HYDRA_HEAD_ID HYDRA_INSTANCE_ID
# shellcheck source=/dev/null
. "$root/tests/helpers.sh"
# shellcheck source=/dev/null
. "$root/lib/output.sh"
# The pass's lock acquisitions are recorded: the library's acquire_lock is
# loaded under another name and wrapped.
sed 's/^acquire_lock() {$/real_acquire_lock() {/' "$root/lib/locks.sh" > "$test_root/locks_recording.sh"
# shellcheck source=/dev/null
. "$test_root/locks_recording.sh"
acquire_lock() {
    printf '%s\n' "$1" >> "$test_root/acquired"
    real_acquire_lock "$@"
}
# shellcheck source=/dev/null
. "$root/lib/identity.sh"
# shellcheck source=/dev/null
. "$root/lib/state_v2.sh"
# shellcheck source=/dev/null
. "$root/lib/events.sh"
# shellcheck source=/dev/null
. "$root/lib/workflow_sessions.sh"

cleanup() {
    tmux kill-server 2>/dev/null || true
    cd / 2>/dev/null || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name Test
git -C "$repo" commit --allow-empty -qm fixture
cd "$repo" || exit 1
"$hydra" init --no-agent --trust >/dev/null || exit 1
project="$(hydra_get_project_id)"
project_dir="$(state_v2_project_dir "$project")"
summaries="$project_dir/sessions"
stamp="$project_dir/activity/maintained"
tab="$(printf '\t')"

head_of() { state_v2_find_head_by_branch "$project" "$1"; }
head_dir_of() { state_v2_head_dir "$project" "$(head_of "$1")"; }
instance_of() { sed -n '1p' "$(head_dir_of "$1")/current-instance"; }
activity() {
    printf '{"schema_version":1,"instance_id":"%s","kind":"activity","state":"%s","since":%s}\n' \
        "$(instance_of "$1")" "$2" "$(($(date +%s) - ${3:-0}))" | "$hydra" adapter activity "$1" >/dev/null 2>&1
}
feed_row() { HYDRA_SESSIONS_NO_CACHE=1 "$hydra" workflow statistics-data 2>/dev/null | awk -F '\t' -v id="$1" '$1 == "H" && $3 == id'; }
sum_of() { cksum < "$1" | awk '{ print $1 }'; }
events_in() { if [ -f "$1" ]; then grep -c activity.changed "$1" || true; else echo 0; fi; }
# A pass at the given number of seconds from now, with the interval open.
pass() {
    rm -f "$stamp"
    : > "$test_root/acquired"
    pass_now=$(($(date +%s) + $1))
    session_maintain_due "$project" "$pass_now"
}
locked() { sort "$test_root/acquired" | tr '\n' ' '; }
files_under() { (cd "$1" && find . -type f | LC_ALL=C sort); }

echo "Running maintenance pass tests..."
echo "======================================"

"$hydra" spawn ended-soon --no-agent >/dev/null 2>&1
assert_success $? "the head whose session will end is spawned"
"$hydra" spawn aging --no-agent >/dev/null 2>&1
assert_success $? "the head whose session stays open is spawned"
"$hydra" spawn recent --no-agent >/dev/null 2>&1
assert_success $? "the head with recent activity is spawned"
ended_i="$(instance_of ended-soon)"
aging_i="$(instance_of aging)"
recent_i="$(instance_of recent)"
ended_dir="$(head_dir_of ended-soon)"
aging_dir="$(head_dir_of aging)"
recent_dir="$(head_dir_of recent)"
ended_log="$ended_dir/instances/$ended_i/activity.jsonl"
aging_log="$aging_dir/instances/$aging_i/activity.jsonl"
recent_log="$recent_dir/instances/$recent_i/activity.jsonl"
for name in ended-soon aging recent; do
    activity "$name" active 3
    activity "$name" quiet 1
done
# An observer reported ten seconds ago: it may still extend that interval over
# the end of the session that is about to end.
watched_until=$(($(date +%s) - 10))
printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n%s\t%s\n' $((watched_until - 20)) "$watched_until" > "$project_dir/activity/coverage"
"$hydra" kill ended-soon >/dev/null 2>&1
assert_success $? "the session ends while coverage may still grow"
assert_equal 2 "$(events_in "$ended_log")" "its activity is kept meanwhile"
assert_equal "" "$(ls "$summaries" 2>/dev/null)" "and no summary is written yet"
ended_row="$(feed_row "$ended_i")"
aging_sum="$(sum_of "$aging_log")"
recent_sum="$(sum_of "$recent_log")"

# First pass, two minutes on: coverage of the ended session is final, the open
# sessions' activity is far younger than the horizon.
pass 120
assert_success $? "the first pass runs"
locked_first="$(locked)"
assert_equal "activity_${project}_$(head_of ended-soon) events_${project}_$(head_of ended-soon) " "$locked_first" "only the head with a decision is locked, activity lock then events lock"
assert_equal "$ended_i" "$(ls "$summaries")" "the ended session is summarised"
assert_equal "ended" "$(awk -F '\t' '$1 == "state" { print $2 }' "$summaries/$ended_i")" "as ended"
assert_equal 600 "$(test_file_mode "$summaries/$ended_i")" "privately"
assert_equal 0 "$(events_in "$ended_log")" "the activity the summary covers is removed"
assert_equal "$ended_row" "$(feed_row "$ended_i")" "the feed shows the session as it did before the pass"
assert_equal "$aging_sum" "$(sum_of "$aging_log")" "an open session's activity log is untouched"
assert_equal "$recent_sum" "$(sum_of "$recent_log")" "and so is the recent one's"
assert_equal "" "$(find "$project_dir/heads" -name 'session-checkpoint*' -o -name '*.next.*')" "no checkpoint is written for activity within the horizon, and nothing is left beside a final name"
assert_equal "$pass_now" "$(sed -n '1p' "$stamp")" "the stamp records when the pass began"
assert_equal 3 "$(sed -n '2p' "$stamp")" "and that it went through the three heads that keep activity"

# Second pass, two hours on: both open sessions have activity older than the
# horizon, which is folded into a checkpoint and removed from the log.
pass 7200
assert_success $? "the second pass runs"
expected_locks="$(printf 'activity_%s_%s\nevents_%s_%s\nactivity_%s_%s\nevents_%s_%s\n' "$project" "$(head_of aging)" "$project" "$(head_of aging)" \
    "$project" "$(head_of recent)" "$project" "$(head_of recent)" | sort | tr '\n' ' ')"
assert_equal "$expected_locks" "$(locked)" "only the two open sessions' heads are locked, each by its two locks"
for pair in "aging:$aging_dir:$aging_i" "recent:$recent_dir:$recent_i"; do
    name="${pair%%:*}"
    rest="${pair#*:}"
    dir="${rest%%:*}"
    instance="${rest#*:}"
    assert_equal "HYDRA_SESSION_CHECKPOINT${tab}2" "$(sed -n '1p' "$dir/instances/$instance/session-checkpoint" | cut -f 1,2)" "$name: the open session is checkpointed"
    assert_equal 0 "$(events_in "$dir/instances/$instance/activity.jsonl")" "$name: the activity the checkpoint covers is removed"
done
assert_equal "$ended_i" "$(ls "$summaries")" "no other summary is written"
assert_equal "$ended_row" "$(feed_row "$ended_i")" "the ended session's row is unchanged"

# Third pass, same time: nothing is left to decide. Nothing is locked and
# nothing changes.
files_before="$(files_under "$project_dir/heads")"
sums_before="$(cd "$project_dir/heads" && find . -type f -print | LC_ALL=C sort | while read -r f; do printf '%s %s\n' "$(sum_of "$f")" "$f"; done)"
pass 7200
assert_success $? "a pass with nothing to decide runs"
assert_equal "" "$(locked)" "no head is locked"
assert_equal "$files_before" "$(files_under "$project_dir/heads")" "the same files remain"
assert_equal "$sums_before" "$(cd "$project_dir/heads" && find . -type f -print | LC_ALL=C sort | while read -r f; do printf '%s %s\n' "$(sum_of "$f")" "$f"; done)" "with the same content"
assert_equal 2 "$(sed -n '2p' "$stamp")" "the pass still goes through the two heads that keep a checkpoint"
assert_equal "" "$(find "$HYDRA_HOME/locks" -name 'activity_*' -o -name 'events_*' 2>/dev/null)" "no lock is left behind"

# A head's lock held by another process blocks that head's decision, not the
# pass: the head is left for the next one.
checkpoint_sum="$(sum_of "$aging_dir/instances/$aging_i/session-checkpoint")"
activity aging active 1
lock_name="activity_${project}_$(head_of aging)"
mkdir -p "$HYDRA_HOME/locks/$lock_name.lock"
printf '%s\n' "$$" > "$HYDRA_HOME/locks/$lock_name.lock/pid"
lock_load_host_name
printf '%s\n' "$_LOCK_HOST_NAME" > "$HYDRA_HOME/locks/$lock_name.lock/host"
date +%s > "$HYDRA_HOME/locks/$lock_name.lock/created_at"
printf 'other\n' > "$HYDRA_HOME/locks/$lock_name.lock/operation"
HYDRA_LOCK_RETRIES=1
export HYDRA_LOCK_RETRIES
pass 14400
assert_success $? "a pass runs while another process holds a head's lock"
unset HYDRA_LOCK_RETRIES
assert_equal "$checkpoint_sum" "$(sum_of "$aging_dir/instances/$aging_i/session-checkpoint")" "the head whose lock is held is not decided"
assert_equal 1 "$(events_in "$aging_log")" "and its activity stays"
rm -f "$HYDRA_HOME/locks/$lock_name.lock"/*
rmdir "$HYDRA_HOME/locks/$lock_name.lock"
pass 14400
if [ "$checkpoint_sum" != "$(sum_of "$aging_dir/instances/$aging_i/session-checkpoint")" ]; then status=0; else status=1; fi
assert_success "$status" "the next pass decides it"
assert_equal 0 "$(events_in "$aging_log")" "and removes the activity the checkpoint now covers"

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
