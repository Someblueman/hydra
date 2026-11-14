#!/bin/sh
# Head sessions in the statistics feed, from constructed event logs: ended,
# open and superseded sessions, gaps in observer coverage, adapter reports
# that override inferred activity, limits, malformed input and checkpoints.
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
. "$root/lib/workflow_sessions.sh"

cleanup() { rm -rf "$test_root"; }
trap cleanup EXIT HUP INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name Test
git -C "$repo" commit --allow-empty -qm fixture
cd "$repo" || exit 1
project="$(hydra_ensure_project_id)"
project_dir="$(state_v2_project_dir "$project")"
tab="$(printf '\t')"
# Every time below is an offset from this base.
base=1700000000

iso() { date -u -d "@$1" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date -u -r "$1" +%Y-%m-%dT%H:%M:%SZ; }

# new_head <branch> [terminal-mode]: sets head_id and instance_id.
new_head() {
    if [ "${2:-interactive}" = headless ]; then
        head_id="$(state_v2_create_head "$project" "$1" - - - "$base" - - "$repo" '' '' '' '' '' '' '' declared-done headless)"
    else
        head_id="$(state_v2_create_head "$project" "$1" "session-$1" - - "$base" - - "$repo")"
    fi
    instance_id="$(sed -n '1p' "$(state_v2_head_dir "$project" "$head_id")/current-instance")"
}

# event <head> <instance> <offset> <type> <actor-id> <payload>: appends one
# well-formed event dated base+offset to the head's log.
event() {
    _ev_file="$(state_v2_head_dir "$project" "$1")/events/events.jsonl"
    _ev_sequence=$(($(wc -l < "$_ev_file" | tr -d ' ') + ${event_offset:-0} + 1))
    printf '{"schema_version":1,"event_id":"evt_%020x","sequence":%s,"occurred_at":"%s","project_id":"%s","head_id":"%s","instance_id":"%s","run_id":null,"type":"%s","actor":{"kind":"adapter","id":"%s"},"payload":%s}\n' \
        "$_ev_sequence" "$_ev_sequence" "$(iso $((base + $3)))" "$project" "$1" "$2" "$4" "$5" "$6" >> "$_ev_file"
}
activity() { event "$1" "$2" "$3" activity.changed tmux "{\"state\":\"$4\",\"since\":$((base + $5)),\"source\":\"tmux\",\"confidence\":\"inferred\"}"; }
observed() { event "$1" "$2" "$3" lifecycle.observed "$4" "{\"status\":\"$5\",\"confidence\":\"$6\",\"exit_code\":null}"; }
coverage() {
    mkdir -p "$project_dir/activity"
    { printf 'HYDRA_ACTIVITY_COVERAGE\t1\t%s\n' "$1"; shift
      while [ $# -ge 2 ]; do printf '%s\t%s\n' $((base + $1)) $((base + $2)); shift 2; done
    } > "$project_dir/activity/coverage"
}
rows() { workflow_sessions_rows $((base + $1)); }
row_for() { printf '%s\n' "$2" | awk -F '\t' -v branch="$1" '$1 == "H" && $4 == branch' ; }
assert_row() {
    assert_equal "$2" "$(row_for "$1" "$feed" | cut -f 5-)" "$3"
}
assert_contains() {
    case "$1" in *"$2"*) assert_success 0 "$3" ;; *) assert_success 1 "$3" ;; esac
}

echo "Running statistics session tests..."
echo "======================================"

assert_equal "" "$(rows 0)" "a project without heads has no sessions"

# alpha: an ended session watched with a gap. Quiet from 10, active from 100,
# quiet from 400, torn down at 600; observed during [50,300) and [350,1000).
new_head alpha; alpha=$head_id; alpha_i=$instance_id
event "$alpha" "$alpha_i" 0 lifecycle.started local '{"profile":"codex"}'
observed "$alpha" "$alpha_i" 0 hydra running exact
activity "$alpha" "$alpha_i" 20 quiet 10
activity "$alpha" "$alpha_i" 104 active 100
activity "$alpha" "$alpha_i" 410 quiet 400
event "$alpha" "$alpha_i" 500 lifecycle.declared local '{"outcome":"done"}'
event "$alpha" "$alpha_i" 600 lifecycle.teardown-requested local '{"transcript_policy":"none"}'
observed "$alpha" "$alpha_i" 600 hydra exited exact
event "$alpha" "$alpha_i" 600 lifecycle.torn-down local '{}'

# beta: an open session. An adapter reports running at 1100 and idle at 1300
# (exact, whatever was inferred or watched), then stops reporting at 1400.
new_head beta; beta=$head_id; beta_i=$instance_id
event "$beta" "$beta_i" 1000 lifecycle.started local '{"profile":"claude"}'
activity "$beta" "$beta_i" 1012 quiet 1000
observed "$beta" "$beta_i" 1100 adapter running reported
activity "$beta" "$beta_i" 1154 active 1150
observed "$beta" "$beta_i" 1300 adapter idle reported
observed "$beta" "$beta_i" 1400 adapter unavailable reported

# gamma: torn down, resumed, then resumed again without a teardown.
new_head gamma; gamma=$head_id; gamma_1=$instance_id
gamma_2=instance_00000000000000000002; gamma_3=instance_00000000000000000003
event "$gamma" "$gamma_1" 2000 lifecycle.started local '{"profile":"none"}'
event "$gamma" "$gamma_1" 2100 lifecycle.torn-down local '{}'
event "$gamma" "$gamma_2" 2200 lifecycle.resumed local "{\"previous_instance_id\":\"$gamma_1\",\"profile\":\"none\"}"
event "$gamma" "$gamma_3" 2300 lifecycle.resumed local "{\"previous_instance_id\":\"$gamma_2\",\"profile\":\"none\"}"

# delta: a headless session; epsilon: a spawn that failed and never started.
new_head delta headless; delta=$head_id; delta_i=$instance_id
event "$delta" "$delta_i" 100 lifecycle.started local '{"profile":"codex","terminal_mode":"headless"}'
event "$delta" "$delta_i" 200 lifecycle.torn-down local '{}'
new_head epsilon; epsilon=$head_id; epsilon_i=$instance_id
observed "$epsilon" "$epsilon_i" 50 hydra failed exact
event "$epsilon" "$epsilon_i" 50 lifecycle.spawn-failed local '{}'
event "$epsilon" "$epsilon_i" 50 lifecycle.teardown-requested local '{"transcript_policy":"none"}'
observed "$epsilon" "$epsilon_i" 50 hydra exited exact
event "$epsilon" "$epsilon_i" 50 lifecycle.torn-down local '{}'

# zeta: the start of the session is no longer in the log.
new_head zeta; zeta=$head_id; zeta_i=$instance_id
event_offset=40
activity "$zeta" "$zeta_i" 850 quiet 840
event "$zeta" "$zeta_i" 900 lifecycle.torn-down local '{}'
event_offset=0

coverage 0 50 300 350 1000 1050 1450
state_before="$(find "$HYDRA_HOME/state" -type f -exec cksum {} \; | sort | cksum)"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_equal 7 "$(printf '%s\n' "$feed" | grep -c "^H$tab")" "one row per session"
assert_equal "" "$(printf '%s\n' "$feed" | grep "^X$tab")" "complete history carries no warning"
assert_row alpha "codex${tab}interactive${tab}ended${tab}$base${tab}$((base + 600))${tab}done${tab}250${tab}250${tab}100${tab}0" \
    "an ended session counts active and quiet only while watched"
assert_row beta "claude${tab}interactive${tab}open${tab}$((base + 1000))${tab}-${tab}-${tab}250${tab}150${tab}1100${tab}300" \
    "an adapter report overrides inferred activity and needs no observer"
assert_equal "none${tab}interactive${tab}ended${tab}$((base + 2000))${tab}$((base + 2100))${tab}-${tab}-${tab}-${tab}100${tab}0
none${tab}interactive${tab}superseded${tab}$((base + 2200))${tab}$((base + 2300))${tab}-${tab}-${tab}-${tab}100${tab}0
none${tab}interactive${tab}open${tab}$((base + 2300))${tab}-${tab}-${tab}-${tab}-${tab}200${tab}0" \
    "$(row_for gamma "$feed" | sort -t "$tab" -k 8 | cut -f 5-)" "each instance is a session; never-observed time is unobserved, not quiet"
assert_row delta "codex${tab}headless${tab}ended${tab}$((base + 100))${tab}$((base + 200))${tab}-${tab}-${tab}-${tab}100${tab}0" \
    "a headless session is listed and left unobserved"
assert_equal "" "$(row_for epsilon "$feed")" "a failed spawn is not a session"
assert_row zeta "-${tab}interactive${tab}ended${tab}-${tab}$((base + 900))${tab}-${tab}-${tab}-${tab}-${tab}-" \
    "a session whose start is gone stays countable with unknown times"
assert_equal "$gamma_3" "$(printf '%s\n' "$feed" | sed -n '1p' | cut -f 3)" "the newest session comes first"
assert_equal "$state_before" "$(find "$HYDRA_HOME/state" -type f -exec cksum {} \; | sort | cksum)" "reading sessions leaves state untouched"

# An open session grows with the clock; nothing observed after the last report.
assert_equal "250${tab}150${tab}1600${tab}300" "$(row_for beta "$(HYDRA_SESSIONS_NO_CACHE=1 rows 3000)" | cut -f 11-)" \
    "an open session is measured up to now"

# Coverage: without a file nothing was watched; time before the history
# begins is unknown; a damaged or symlinked file is never trusted.
mv "$project_dir/activity/coverage" "$test_root/coverage"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_row alpha "codex${tab}interactive${tab}ended${tab}$base${tab}$((base + 600))${tab}done${tab}-${tab}-${tab}600${tab}0" \
    "without coverage the session is unobserved and its activity unknown"
assert_row beta "claude${tab}interactive${tab}open${tab}$((base + 1000))${tab}-${tab}-${tab}200${tab}100${tab}1200${tab}300" \
    "adapter reports count without coverage"
coverage $((base + 700)) 50 300 350 1000 1050 1450
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_row alpha "codex${tab}interactive${tab}ended${tab}$base${tab}$((base + 600))${tab}done${tab}-${tab}-${tab}-${tab}-" \
    "a session older than the coverage history has unknown times"
assert_row beta "claude${tab}interactive${tab}open${tab}$((base + 1000))${tab}-${tab}-${tab}250${tab}150${tab}1100${tab}300" \
    "a session inside the coverage history is measured"
printf 'HYDRA_ACTIVITY_COVERAGE\t1\t0\n%s\t%s\n' $((base + 300)) $((base + 50)) > "$project_dir/activity/coverage"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_contains "$feed" "X${tab}Activity coverage unreadable" "damaged coverage is reported"
assert_row alpha "codex${tab}interactive${tab}ended${tab}$base${tab}$((base + 600))${tab}done${tab}-${tab}-${tab}-${tab}-" \
    "damaged coverage makes times unknown"
rm -f "$project_dir/activity/coverage"
ln -s "$test_root/coverage" "$project_dir/activity/coverage"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_contains "$feed" "X${tab}Skipped symlinked activity coverage" "symlinked coverage is not followed"
assert_row alpha "codex${tab}interactive${tab}ended${tab}$base${tab}$((base + 600))${tab}done${tab}-${tab}-${tab}600${tab}0" \
    "symlinked coverage counts as none"
rm -f "$project_dir/activity/coverage"
coverage 0 50 300 350 1000 1050 1450
settled_rows="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"

# Malformed input: a torn last line, garbage, a bad time, an oversized line
# and unrelated events are skipped; the rest still counts.
alpha_log="$(state_v2_head_dir "$project" "$alpha")/events/events.jsonl"
cp "$alpha_log" "$test_root/alpha.jsonl"
{
    printf 'not json at all\n'
    printf '{"schema_version":1,"event_id":"evt_x","sequence":90,"occurred_at":"2023-13-45T00:00:00Z","instance_id":"%s","type":"activity.changed","actor":{"kind":"observer","id":"tmux"},"payload":{"state":"active","since":%s}}\n' "$alpha_i" $((base + 5))
    printf '{"schema_version":1,"event_id":"evt_y","sequence":91,"occurred_at":"%s","instance_id":"../%s","type":"lifecycle.started","actor":{"kind":"hydra","id":"local"},"payload":{"profile":"x"}}\n' "$(iso $((base + 5)))" "$alpha_i"
    printf '{"schema_version":1,"event_id":"evt_z","sequence":92,"occurred_at":"%s","instance_id":"%s","type":"exec.completed","actor":{"kind":"hydra","id":"local"},"payload":{"padding":"%s"}}\n' "$(iso $((base + 5)))" "$alpha_i" "$(awk 'BEGIN { for (i = 0; i < 33000; i++) printf "x" }')"
    printf '{"schema_version":1,"event_id":"evt_w","sequence":93,"occurred_at":"%s","instance_id":"%s","type":"activity.changed","actor":{"kind":"observer","id":"tmux"},"payload":{"state":"acti' "$(iso $((base + 550)))" "$alpha_i"
} >> "$alpha_log"
assert_equal "$settled_rows" "$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)" "malformed and torn lines are discarded"
cp "$test_root/alpha.jsonl" "$alpha_log"

# Symbolic links are not followed: a linked log contributes no session.
mv "$alpha_log" "$test_root/alpha-real.jsonl"
ln -s "$test_root/alpha-real.jsonl" "$alpha_log"
assert_equal "" "$(row_for alpha "$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)")" "a symlinked event log is not read"
rm -f "$alpha_log"
mv "$test_root/alpha-real.jsonl" "$alpha_log"

# Limits: the newest sessions are kept and the cut is announced.
feed="$(HYDRA_SESSIONS_NO_CACHE=1 HYDRA_SESSIONS_MAX_ROWS=2 rows 2500)"
assert_equal 2 "$(printf '%s\n' "$feed" | grep -c "^H$tab")" "the row limit holds"
assert_contains "$feed" "X${tab}More than 2 head sessions; showing the newest" "exceeding the row limit is announced"
assert_equal "$gamma_3 $gamma_2" "$(printf '%s\n' "$feed" | grep "^H$tab" | cut -f 3 | tr '\n' ' ' | sed 's/ $//')" "the newest sessions are the ones kept"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 HYDRA_SESSIONS_MAX_HEADS=3 rows 2500)"
assert_contains "$feed" "X${tab}More than 3 heads; sessions are a partial sample" "exceeding the head limit is announced"
dd if=/dev/zero bs=1024 count=9000 2>/dev/null | tr '\0' 'x' > "$(dirname "$alpha_log")/archive/events-big.jsonl"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)"
assert_contains "$feed" "X${tab}Skipped an event log over 8 MiB" "an oversized event file is skipped and announced"
assert_equal "$(row_for alpha "$settled_rows")" "$(row_for alpha "$feed")" "the head's other files still count"
rm -f "$(dirname "$alpha_log")/archive/events-big.jsonl"

# Archived events are read: a finished session survives rotation.
"$hydra" events retain --project "$project" --head "$alpha" --max-events 2 --archive-keep-seconds 3600 --apply >/dev/null 2>&1
assert_success $? "the head's older events are archived"
assert_equal 2 "$(wc -l < "$alpha_log" | tr -d ' ')" "the live log holds only its tail"
assert_equal "$settled_rows" "$(HYDRA_SESSIONS_NO_CACHE=1 rows 2500)" "sessions read archived events"

# Checkpoints: the same rows with and without them, when they are first
# written, reused, extended by new events, damaged, or outrun by a repair.
# shellcheck disable=SC2034 # Read by workflow_sessions_rows.
HYDRA_SESSIONS_CHECKPOINT_EVENTS=1
cache="$HYDRA_HOME/cache/sessions/$project"
assert_equal "$settled_rows" "$(rows 2500)" "the first cached read agrees"
if [ -f "$cache/$alpha.tsv" ] && [ -f "$cache/$beta.tsv" ]; then status=0; else status=1; fi
assert_success "$status" "settled history is checkpointed"
assert_equal 600 "$(stat -c %a "$cache/$alpha.tsv" 2>/dev/null || stat -f %Lp "$cache/$alpha.tsv")" "checkpoints are private"
assert_equal "" "$(find "$cache" -name '*.next.*')" "no partial checkpoint is left"
assert_equal "$settled_rows" "$(rows 2500)" "a read from checkpoints agrees"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9000)" "$(rows 9000)" "checkpoints agree at a later time"
activity "$beta" "$beta_i" 8000 quiet 7990
observed "$beta" "$beta_i" 8200 adapter running reported
event "$gamma" "$gamma_3" 8300 lifecycle.torn-down local '{}'
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9000)" "$(rows 9000)" "events after a checkpoint are added to it"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9100)" "$(rows 9100)" "and are checkpointed in turn"
printf 'HYDRA_SESSION_CHECKPOINT\t1\t3\nI\tgarbage\nZ\t1\n' > "$cache/$beta.tsv"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9100)" "$(rows 9100)" "a damaged checkpoint is ignored"
sed '$d' "$cache/$gamma.tsv" > "$cache/$gamma.tsv.cut" && mv "$cache/$gamma.tsv.cut" "$cache/$gamma.tsv"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9100)" "$(rows 9100)" "a torn checkpoint is ignored"
beta_log="$(state_v2_head_dir "$project" "$beta")/events/events.jsonl"
sed -n '1,3p' "$beta_log" > "$beta_log.cut" && mv "$beta_log.cut" "$beta_log"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9100)" "$(rows 9100)" "a log shorter than its checkpoint is read again"
rm -rf "$HYDRA_HOME/cache"
ln -s "$test_root/elsewhere" "$HYDRA_HOME/cache"
assert_equal "$(HYDRA_SESSIONS_NO_CACHE=1 rows 9100)" "$(rows 9100)" "a symlinked cache is not used"
if [ -e "$test_root/elsewhere" ]; then status=1; else status=0; fi
assert_success "$status" "nothing is written through a symlinked cache"
rm -f "$HYDRA_HOME/cache"

# The command: schema 5, session rows before the closing counts, and the
# native reader and exporter accept it.
feed="$("$hydra" workflow statistics-data 2>/dev/null)"
assert_equal "HYDRA_STATISTICS${tab}5" "$(printf '%s\n' "$feed" | sed -n '1p' | cut -f 1-2)" "the feed is schema 5"
assert_equal "Z${tab}0${tab}0${tab}7" "$(printf '%s\n' "$feed" | tail -n 1)" "the closing row counts the sessions"
assert_equal 7 "$(printf '%s\n' "$feed" | grep -c "^H$tab")" "the feed lists the sessions"
if [ -x "${HYDRA_CORE:-}" ]; then
    json="$("$hydra" workflow statistics-json 2>/dev/null)"
    assert_success $? "the session feed exports as JSON"
    assert_contains "$json" '"sessions":{"scope":"head_sessions","count":7,' "the export counts the sessions"
    assert_contains "$json" '"inference":{"source":"tmux","confidence":"inferred"' "the export labels the inference"
    printf '%s\n' "$feed" > "$test_root/left.tsv"
    "$hydra" workflow statistics-compare "$test_root/left.tsv" "$root/tests/fixtures/tui/statistics-v4.tsv" --format text > "$test_root/compare.txt" 2>/dev/null
    assert_success $? "a session feed compares with an earlier one"
    assert_contains "$(cat "$test_root/compare.txt")" "left sessions count=7 " "the comparison describes the sessions"
fi

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
