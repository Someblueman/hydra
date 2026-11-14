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

# Activity logs, session checkpoints and summaries. Two heads record the same
# 100 hours of an observed session: a change every 100 seconds in the
# instance's activity log, and each hour an adapter report that holds for 20
# minutes in the head's log. Only "long" is maintained; "twin" is always read
# from all of its events, so their rows must agree.
# simulate <head> <instance> <from-offset> <seconds>
simulate() {
    _sim_dir="$(state_v2_head_dir "$project" "$1")"
    _sim_events="$_sim_dir/events/events.jsonl"
    _sim_log="$_sim_dir/instances/$2/activity.jsonl"
    _sim_head="$(sed -n '1p' "$_sim_events.next-sequence" 2>/dev/null)"
    [ -n "$_sim_head" ] || _sim_head=$(($(wc -l < "$_sim_events" | tr -d ' ') + 1))
    _sim_own="$(sed -n '1p' "$_sim_log.next-sequence" 2>/dev/null)"
    [ -n "$_sim_own" ] || _sim_own=1
    awk -v from=$((base + $3)) -v span="$4" -v hseq="$_sim_head" -v aseq="$_sim_own" -v project="$project" \
        -v head="$1" -v instance="$2" -v events="$_sim_events" -v alog="$_sim_log" '
        function iso(t,    days, secs, z, era, doe, yoe, y, doy, mp, d, m) {
            days = int(t / 86400); secs = t - days * 86400
            z = days + 719468; era = int(z / 146097); doe = z - era * 146097
            yoe = int((doe - int(doe / 1460) + int(doe / 36524) - int(doe / 146096)) / 365)
            y = yoe + era * 400; doy = doe - (365 * yoe + int(yoe / 4) - int(yoe / 100)); mp = int((5 * doy + 2) / 153)
            d = doy - int((153 * mp + 2) / 5) + 1; m = (mp < 10) ? mp + 3 : mp - 9; if (m <= 2) y++
            return sprintf("%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, int(secs / 3600), int((secs % 3600) / 60), secs % 60)
        }
        function observed(t, actor) {
            printf "{\"schema_version\":1,\"event_id\":\"evt_%020d\",\"sequence\":%d,\"occurred_at\":\"%s\",\"project_id\":\"%s\",\"head_id\":\"%s\",\"instance_id\":\"%s\",\"run_id\":null,\"type\":\"lifecycle.observed\",\"actor\":{\"kind\":\"adapter\",\"id\":\"%s\"},\"payload\":{\"status\":\"running\",\"confidence\":\"reported\",\"exit_code\":null}}\n", hseq, hseq, iso(t), project, head, instance, actor >> events
            hseq++
        }
        BEGIN {
            state = "quiet"
            for (t = from; t < from + span; t += 100) {
                if ((t - from) % 3600 == 0) observed(t, "adapter")
                if ((t - from) % 3600 == 1200) observed(t, "hydra")
                state = (state == "active") ? "quiet" : "active"
                printf "{\"schema_version\":1,\"event_id\":\"evt_a%019d\",\"sequence\":%d,\"head_next_sequence\":%d,\"occurred_at\":\"%s\",\"project_id\":\"%s\",\"head_id\":\"%s\",\"instance_id\":\"%s\",\"run_id\":null,\"type\":\"activity.changed\",\"actor\":{\"kind\":\"observer\",\"id\":\"tmux\"},\"payload\":{\"state\":\"%s\",\"since\":%d,\"source\":\"tmux\",\"confidence\":\"inferred\"}}\n", aseq, aseq, hseq, iso(t + 3), project, head, instance, state, t >> alog
                aseq++
            }
            print hseq > (events ".next-sequence"); print aseq > (alog ".next-sequence")
        }'
}
# Coverage: watched for the first half of every hour of the simulation.
watched_hours() {
    awk -v from=$((base + $1)) -v hours="$2" 'BEGIN {
        print "HYDRA_ACTIVITY_COVERAGE\t1\t0"
        for (h = 0; h < hours; h++) print from + h * 3600 "\t" from + h * 3600 + 1800 }' > "$project_dir/activity/coverage"
}
pair() { printf '%s\n%s\n' "$(row_for long "$1" | cut -f 5-)" "$(row_for twin "$1" | cut -f 5-)"; }
same() { [ "$(row_for long "$1" | cut -f 5-)" = "$(row_for twin "$1" | cut -f 5-)" ] && [ -n "$(row_for long "$1")" ]; }
maintain() { session_maintain_head "$project" "$1" $((base + $2)); }
t0=200000
hour=3600
rm -rf "$HYDRA_HOME/cache"
new_head long; long=$head_id; long_i=$instance_id
new_head twin; twin=$head_id; twin_i=$instance_id
long_log="$(event_activity_file "$project" "$long" "$long_i")"
event "$long" "$long_i" "$t0" lifecycle.started local '{"profile":"claude"}'
event "$twin" "$twin_i" "$t0" lifecycle.started local '{"profile":"claude"}'
simulate "$long" "$long_i" "$t0" $((100 * hour))
simulate "$twin" "$twin_i" "$t0" $((100 * hour))
watched_hours "$t0" 101
at=$((t0 + 100 * hour))
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows "$at")"
if same "$feed"; then status=0; else status=1; pair "$feed"; fi
assert_success "$status" "two heads with the same events have the same row"
before_bytes="$(wc -c < "$long_log" | tr -d ' ')"
before_events="$(wc -l < "$long_log" | tr -d ' ')"
echo "  100 simulated head-hours: activity log $before_events events, $before_bytes bytes before maintenance"
maintain "$long" "$at"
assert_success $? "an open session is maintained"
after_bytes="$(wc -c < "$long_log" | tr -d ' ')"
after_events="$(wc -l < "$long_log" | tr -d ' ')"
echo "  after maintenance: $after_events events, $after_bytes bytes"
if [ "$after_events" -le 40 ] && [ "$after_bytes" -le 32768 ]; then status=0; else status=1; fi
assert_success "$status" "the live activity log holds at most the last hour"
event_verify_file "$long_log" >/dev/null 2>&1
assert_success $? "the trimmed activity log verifies"
if [ -x "${HYDRA_CORE:-}" ]; then
    "$HYDRA_CORE" validate-events "$long_log" >/dev/null 2>&1
    assert_success $? "the native validator accepts the trimmed log"
fi
assert_equal "$before_events" "$(($(sed -n '1p' "$long_log.next-sequence") - 1))" "the sequence cursor still counts every event"
assert_equal "HYDRA_SESSION_CHECKPOINT${tab}2" "$(sed -n '1p' "$(dirname "$long_log")/session-checkpoint" | cut -f 1-2)" "the removed activity is folded into a session checkpoint"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows "$at")"
if same "$feed"; then status=0; else status=1; pair "$feed"; fi
assert_success "$status" "the row is the same after the activity log is trimmed"
feed="$(rows "$at")"
if same "$feed" && same "$(rows "$at")"; then status=0; else status=1; pair "$feed"; fi
assert_success "$status" "and with the feed's own checkpoints"
simulate "$long" "$long_i" $((t0 + 100 * hour)) "$hour"
simulate "$twin" "$twin_i" $((t0 + 100 * hour)) "$hour"
at=$((t0 + 101 * hour))
maintain "$long" "$at"
feed="$(rows "$at")"
if same "$feed" && same "$(HYDRA_SESSIONS_NO_CACHE=1 rows "$at")"; then status=0; else status=1; pair "$feed"; fi
assert_success "$status" "a second pass extends the checkpoint and the row still agrees"
if [ "$(wc -l < "$long_log" | tr -d ' ')" -le 40 ]; then status=0; else status=1; fi
assert_success "$status" "the log stays bounded"

# The session ends. While the newest coverage interval reaches to within the
# stale window of now, it may still grow over the end: no summary yet.
end=$((t0 + 101 * hour + 10))
event "$long" "$long_i" "$end" lifecycle.torn-down local '{}'
event "$twin" "$twin_i" "$end" lifecycle.torn-down local '{}'
printf '%s\t%s\n' $((base + end - 30)) $((base + end - 5)) >> "$project_dir/activity/coverage"
maintain "$long" $((end + 10))
if [ -f "$project_dir/sessions/$long_i" ]; then status=1; else status=0; fi
assert_success "$status" "a session whose coverage may still grow is not summarised"
maintain "$long" $((end + 60))
if [ -f "$project_dir/sessions/$long_i" ]; then status=0; else status=1; fi
assert_success "$status" "it is summarised once its coverage is final"
if [ ! -e "$long_log" ] && [ ! -e "$(dirname "$long_log")/session-checkpoint" ]; then status=0; else status=1; fi
assert_success "$status" "the summary replaces its checkpoint and activity log"
feed="$(rows $((end + 60)))"
if same "$feed" && same "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 60)))"; then status=0; else status=1; pair "$feed"; fi
assert_success "$status" "the summarised row equals the one derived from every event"
summarised_row="$(row_for long "$feed")"
twin_row="$(row_for twin "$feed")"
long_events="$(state_v2_head_dir "$project" "$long")/events/events.jsonl"
mv "$long_events" "$test_root/long-events.jsonl"
assert_equal "$summarised_row" "$(row_for long "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 9000)))")" "the summary is the row with no events left"
mv "$test_root/long-events.jsonl" "$long_events"
cp "$project_dir/activity/coverage" "$test_root/coverage-kept"
printf 'HYDRA_ACTIVITY_COVERAGE\t1\t%s\n' $((base + end + 100)) > "$project_dir/activity/coverage"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 200)))"
assert_equal "-${tab}-${tab}-${tab}-" "$(row_for twin "$feed" | cut -f 11-)" "once coverage history moves past a session its events no longer say"
assert_equal "$summarised_row" "$(row_for long "$feed")" "but its summary keeps the times"
cp "$test_root/coverage-kept" "$project_dir/activity/coverage"

# A session that ended before summaries existed (activity in the head log,
# no summary) is derived from its events, and a pass over its head
# summarises it with the same row. alpha's was written by events retain above.
if [ -f "$project_dir/sessions/$alpha_i" ]; then status=0; else status=1; fi
assert_success "$status" "event retention summarises the head's finished sessions"
if [ -e "$project_dir/sessions/$delta_i" ]; then status=1; else status=0; fi
assert_success "$status" "an old session has no summary"
old_row="$(row_for delta "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 60)))")"
assert_contains "$old_row" "${tab}ended${tab}" "and is derived from its events"
maintain "$delta" $((end + 60))
if [ -f "$project_dir/sessions/$delta_i" ]; then status=0; else status=1; fi
assert_success "$status" "a pass over its head summarises it"
assert_equal "$old_row" "$(row_for delta "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 60)))")" "with the row it had"

# Summaries read are bounded, most recently written first; the rest are
# announced and never derived again from what is left of their events.
feed="$(HYDRA_SESSIONS_NO_CACHE=1 HYDRA_SESSIONS_MAX_SUMMARIES=1 rows $((end + 60)))"
assert_contains "$feed" "X${tab}More than 1 finished sessions summarised; reading the most recent" "the summary limit is announced"
assert_equal "$delta_i" "$(printf '%s\n' "$feed" | awk -F '\t' '$1 == "H" && ($3 == a || $3 == l || $3 == d) { print $3 }' \
    a="$alpha_i" l="$long_i" d="$delta_i")" "the most recent summary is read and the others are not re-derived"

# A summary or session checkpoint that cannot be read is announced; the
# session's times are never made up from what remains.
cp "$project_dir/sessions/$long_i" "$test_root/long-summary"
printf 'HYDRA_SESSION_SUMMARY\t1\nhead\tnot-a-head\n' > "$project_dir/sessions/$long_i"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 60)))"
assert_contains "$feed" "X${tab}Skipped an unreadable session summary" "an unreadable summary is announced"
assert_equal "" "$(row_for long "$feed")" "and its session is not derived from what remains"
cp "$test_root/long-summary" "$project_dir/sessions/$long_i"
new_head open; open=$head_id; open_i=$instance_id
event "$open" "$open_i" "$t0" lifecycle.started local '{"profile":"codex"}'
simulate "$open" "$open_i" "$t0" $((3 * hour))
maintain "$open" $((t0 + 3 * hour))
open_checkpoint="$(state_v2_head_dir "$project" "$open")/instances/$open_i/session-checkpoint"
printf 'HYDRA_SESSION_CHECKPOINT\t2\t1\nI\tgarbage\nZ\t1\n' > "$open_checkpoint"
feed="$(HYDRA_SESSIONS_NO_CACHE=1 rows $((t0 + 3 * hour)))"
assert_contains "$feed" "X${tab}A session checkpoint is missing or unreadable; its times are unknown" "an unreadable checkpoint is announced"
assert_equal "-${tab}-${tab}-${tab}-" "$(row_for open "$feed" | cut -f 11-)" "and the session's times are unknown"
rm -f "$open_checkpoint"
assert_equal "-${tab}-${tab}-${tab}-" "$(row_for open "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((t0 + 3 * hour)))" | cut -f 11-)" \
    "a missing checkpoint leaves them unknown too"

# The recorder's pass is throttled and takes heads in turn: with room for one
# head per pass, the two heads that keep activity logs (open and twin) are
# each maintained once over two passes.
stamp="$project_dir/activity/maintained"
rm -f "$stamp"
HYDRA_SESSIONS_MAINTAIN_HEADS=1 session_maintain_due "$project" $((base + end + 1000))
assert_equal "$((base + end + 1000)) 1" "$(tr '\n' ' ' < "$stamp" | sed 's/ $//')" "a pass records when it ran and where the next starts"
HYDRA_SESSIONS_MAINTAIN_HEADS=1 session_maintain_due "$project" $((base + end + 1010))
assert_equal "$((base + end + 1000)) 1" "$(tr '\n' ' ' < "$stamp" | sed 's/ $//')" "no pass runs within the interval"
HYDRA_SESSIONS_MAINTAIN_HEADS=1 session_maintain_due "$project" $((base + end + 1000 + HYDRA_SESSIONS_MAINTAIN_SECONDS))
assert_equal 2 "$(sed -n '2p' "$stamp")" "the next pass takes the next head"
if [ -f "$project_dir/sessions/$twin_i" ] && [ ! -e "$(event_activity_file "$project" "$twin" "$twin_i")" ]; then status=0; else status=1; fi
assert_success "$status" "between them both heads are maintained"
assert_equal "$twin_row" "$(row_for twin "$(HYDRA_SESSIONS_NO_CACHE=1 rows $((end + 60)))")" "and the summarised twin keeps its row"

echo "======================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
