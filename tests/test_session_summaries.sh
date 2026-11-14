#!/bin/sh
# Finished head sessions are summarised by the commands that end them:
# teardown, resume and reconciliation. A summary keeps the session's
# statistics row after its activity events are removed and after the head's
# state is gone, follows a late declared outcome, and is never written over a
# concurrent recorder. The observer's recorder maintains at most once per
# interval.
set -u

test_count=0
pass_count=0
fail_count=0
test_root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 HYDRA_SKIP_AI=1
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
hydra="$root/bin/hydra"
mkdir -p "$repo" "$HYDRA_HOME"
unset TMUX TMUX_PANE HYDRA_HEAD_ID HYDRA_INSTANCE_ID
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
tab="$(printf '\t')"

head_of() { state_v2_find_head_by_branch "$project" "$1"; }
head_dir_of() { state_v2_head_dir "$project" "$(head_of "$1")"; }
instance_of() { sed -n '1p' "$(head_dir_of "$1")/current-instance"; }
activity() {
    printf '{"schema_version":1,"instance_id":"%s","kind":"activity","state":"%s","since":%s}\n' \
        "$(instance_of "$1")" "$2" "$(($(date +%s) - ${3:-0}))" | "$hydra" adapter activity "$1" >/dev/null 2>&1
}
# Observer coverage that ended long ago: nothing can extend it now.
coverage_closed() {
    printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n%s\t%s\n' $(($1 - 600)) $(($1 - 300)) > "$project_dir/activity/coverage"
}
feed_row() { "$hydra" workflow statistics-data 2>/dev/null | awk -F '\t' -v id="$1" '$1 == "H" && $3 == id'; }
assert_contains() {
    case "$1" in *"$2"*) assert_success 0 "$3" ;; *) assert_success 1 "$3" ;; esac
}

echo "Running session summary tests..."
echo "======================================"

# Teardown summarises a session nobody is watching any more, and the
# activity it covers leaves the live logs.
"$hydra" spawn alpha --no-agent >/dev/null 2>&1
assert_success $? "a head is spawned"
alpha_i="$(instance_of alpha)"
alpha_dir="$(head_dir_of alpha)"
activity alpha active 3
activity alpha quiet 1
assert_equal 2 "$(grep -c activity.changed "$alpha_dir/instances/$alpha_i/activity.jsonl")" "activity is recorded in the instance log"
coverage_closed "$(date +%s)"
cp "$alpha_dir/instances/$alpha_i/activity.jsonl" "$test_root/alpha-activity.jsonl"
"$hydra" kill alpha >/dev/null 2>&1
assert_success $? "the head is removed"
if [ -f "$summaries/$alpha_i" ]; then status=0; else status=1; fi
assert_success "$status" "teardown writes the session's summary"
assert_equal 600 "$(stat -c %a "$summaries/$alpha_i" 2>/dev/null || stat -f %Lp "$summaries/$alpha_i")" "the summary is private"
assert_equal "HYDRA_SESSION_SUMMARY${tab}1" "$(sed -n '1p' "$summaries/$alpha_i")" "the summary is versioned"
assert_equal "ended" "$(awk -F '\t' '$1 == "state" { print $2 }' "$summaries/$alpha_i")" "the summary records how the session ended"
if [ -e "$alpha_dir/instances/$alpha_i/activity.jsonl" ]; then status=1; else status=0; fi
assert_success "$status" "the activity it covers is removed"
assert_equal 3 "$(sed -n '1p' "$alpha_dir/instances/$alpha_i/activity.jsonl.next-sequence")" "the activity log keeps its sequence cursor"
"$hydra" state verify >/dev/null 2>&1
assert_success $? "state still verifies"
summary_row="$(feed_row "$alpha_i")"
assert_contains "$summary_row" "${tab}alpha${tab}none${tab}interactive${tab}ended${tab}" "the feed shows the summarised session"
# The same row is derived from the events when the summary is set aside.
mv "$summaries/$alpha_i" "$test_root/alpha-summary"
cp "$test_root/alpha-activity.jsonl" "$alpha_dir/instances/$alpha_i/activity.jsonl"
assert_equal "$summary_row" "$(HYDRA_SESSIONS_NO_CACHE=1 feed_row "$alpha_i")" "the summary equals what the events add up to"
rm -f "$alpha_dir/instances/$alpha_i/activity.jsonl"
mv "$test_root/alpha-summary" "$summaries/$alpha_i"

# A later outcome reaches the summary.
"$hydra" outcome alpha "done" --actor human >/dev/null 2>&1
assert_success $? "an outcome is declared after the session ended"
assert_equal "done" "$(awk -F '\t' '$1 == "outcome" { print $2 }' "$summaries/$alpha_i")" "the summary takes the later outcome"
assert_contains "$(feed_row "$alpha_i")" "${tab}ended${tab}" "the feed still lists the session"
assert_equal "done" "$(feed_row "$alpha_i" | cut -f 10)" "the feed shows the later outcome"

# History outlives the head's state directory.
summary_row="$(feed_row "$alpha_i")"
mv "$alpha_dir" "$test_root/alpha-head"
assert_equal "$summary_row" "$(feed_row "$alpha_i")" "a summary is kept with the project, not the head"
mv "$test_root/alpha-head" "$alpha_dir"

# Resume supersedes an instance and summarises it.
"$hydra" spawn beta --no-agent >/dev/null 2>&1
beta_old="$(instance_of beta)"
activity beta active 2
tmux kill-session -t beta 2>/dev/null
coverage_closed "$(date +%s)"
"$hydra" resume beta >/dev/null 2>&1
assert_success $? "the head resumes"
beta_new="$(instance_of beta)"
assert_equal superseded "$(awk -F '\t' '$1 == "state" { print $2 }' "$summaries/$beta_old" 2>/dev/null)" "the replaced instance is summarised as superseded"
if [ -f "$summaries/$beta_new" ]; then status=1; else status=0; fi
assert_success "$status" "the replacing session stays open"
assert_contains "$(feed_row "$beta_new")" "${tab}open${tab}" "the replacing session is derived from events"

# Reconciliation records a head found without its terminal as ended.
"$hydra" spawn gamma --no-agent >/dev/null 2>&1
gamma_i="$(instance_of gamma)"
tmux kill-session -t gamma 2>/dev/null
coverage_closed "$(date +%s)"
"$hydra" cleanup >/dev/null 2>&1
grep -q '"type":"lifecycle.torn-down".*"payload":{"reconciled":true}' "$(head_dir_of gamma)/events/events.jsonl"
assert_success $? "cleanup records that the dead head's instance ended"
assert_equal ended "$(awk -F '\t' '$1 == "state" { print $2 }' "$summaries/$gamma_i" 2>/dev/null)" "and summarises its session"

# While an observer may still extend its coverage over the end, teardown
# leaves the session to a later pass; that pass summarises it.
"$hydra" spawn delta --no-agent >/dev/null 2>&1
delta_i="$(instance_of delta)"
delta_dir="$(head_dir_of delta)"
activity delta active 1
# An observer reported ten seconds ago: it may still extend that interval over
# the end. (A report in the same second as the end would already cover it.)
watched_until=$(($(date +%s) - 10))
printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n%s\t%s\n' $((watched_until - 20)) "$watched_until" > "$project_dir/activity/coverage"
"$hydra" kill delta >/dev/null 2>&1
if [ -f "$summaries/$delta_i" ]; then status=1; else status=0; fi
assert_success "$status" "a session whose coverage may still grow is not summarised yet"
if [ -f "$delta_dir/instances/$delta_i/activity.jsonl" ]; then status=0; else status=1; fi
assert_success "$status" "its activity is kept meanwhile"
session_maintain_head "$project" "$(head_of delta)" $(($(date +%s) + HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS + 1 + 60))
assert_success $? "a later pass runs"
if [ -f "$summaries/$delta_i" ] && [ ! -e "$delta_dir/instances/$delta_i/activity.jsonl" ]; then status=0; else status=1; fi
assert_success "$status" "once coverage is final the session is summarised and its activity removed"

# Concurrent recorders and teardown: nothing is recorded into a session after
# its end, and both logs stay verifiable.
"$hydra" spawn epsilon --no-agent >/dev/null 2>&1
epsilon_i="$(instance_of epsilon)"
epsilon_dir="$(head_dir_of epsilon)"
recorders=0
while [ "$recorders" -lt 6 ]; do
    if [ $((recorders % 2)) -eq 0 ]; then state=active; else state=quiet; fi
    printf '{"schema_version":1,"instance_id":"%s","kind":"activity","state":"%s","since":%s}\n' "$epsilon_i" "$state" "$(date +%s)" |
        HYDRA_LOCK_RETRIES=200 "$hydra" adapter activity epsilon >/dev/null 2>&1 &
    recorders=$((recorders + 1))
done
HYDRA_LOCK_RETRIES=200 "$hydra" kill epsilon >/dev/null 2>&1
assert_success $? "teardown completes while recorders run"
wait
event_verify_file "$epsilon_dir/events/events.jsonl" >/dev/null 2>&1
assert_success $? "the head log verifies"
epsilon_log="$epsilon_dir/instances/$epsilon_i/activity.jsonl"
if [ ! -f "$epsilon_log" ] || event_verify_file "$epsilon_log" >/dev/null 2>&1; then status=0; else status=1; fi
assert_success "$status" "the activity log verifies"
torn_down="$(grep '"type":"lifecycle.torn-down"' "$epsilon_dir/events/events.jsonl" | sed -n 's/.*"sequence":\([0-9]*\),.*/\1/p')"
late="$( [ -f "$epsilon_log" ] && awk -v end="$torn_down" 'match($0, /"head_next_sequence":[0-9]+/) && substr($0, RSTART + 21, RLENGTH - 21) + 0 > end + 0' "$epsilon_log")"
assert_equal "" "$late" "no activity is recorded after the session ended"
session_maintain_head "$project" "$(head_of epsilon)" $(($(date +%s) + HYDRA_ACTIVITY_COVERAGE_STALE_SECONDS + 61))
assert_equal ended "$(awk -F '\t' '$1 == "state" { print $2 }' "$summaries/$epsilon_i" 2>/dev/null)" "the session is summarised"
assert_equal "" "$(find "$HYDRA_HOME/locks" -name 'activity_*' -o -name 'events_*' 2>/dev/null)" "no lock is left behind"

# The recorder maintains a project's sessions at most once per interval.
rm -f "$project_dir/activity/maintained"
"$hydra" adapter watch >/dev/null 2>&1
stamp="$(sed -n '1p' "$project_dir/activity/maintained" 2>/dev/null)"
case "$stamp" in ''|*[!0-9]*) assert_success 1 "the recorder maintains sessions" ;; *) assert_success 0 "the recorder maintains sessions" ;; esac
printf '%s\n' $((stamp - 1)) > "$project_dir/activity/maintained"
"$hydra" adapter watch >/dev/null 2>&1
assert_equal $((stamp - 1)) "$(sed -n '1p' "$project_dir/activity/maintained")" "and not again within the interval"

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
