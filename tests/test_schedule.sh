#!/bin/sh
# Schedules: exact host-timer text, slot dedup tokens (double fire, run-now
# plus timer, competing firings), bounded missed-run policies, reboot
# recovery opt-in, firing through the public submission commands and JSON
# envelopes.

set -u
test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
base="$(cd "$base" && pwd -P)"
repo="$base/repo"
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
cleanup() {
    test_stop_processes_naming "$base"
    rm -rf "$base"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

json_field() {
    printf '%s\n' "$1" | sed -n "s/.*\"$2\":\"\{0,1\}\([^\",}]*\)\"\{0,1\}[,}].*/\1/p" | sed -n '1p'
}

assert_envelope() {
    case "$1" in
        *'"schema_version":1'*'"ok":'"$3"*'"command":"'"$2"'"'*) assert_success 0 "$4" ;;
        *) assert_success 1 "$4"; echo "  Output: $1" ;;
    esac
}

schedule_path() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/schedules/$1" | sed -n '1p'
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
git add README
git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
"$HYDRA_BIN" spawn --headless --no-agent sched-head >/dev/null 2>&1
assert_success $? "a headless head is available"
cat > "$base/quick.yml" <<EOF
version: 1
id: quick
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: mark
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: sched-head
      argv: [sh, -c, "printf w >> '$base/fired'"]
EOF
repo_path="$(cd "$repo" && pwd -P)"
home_path="$(cd "$HYDRA_HOME" && pwd -P)"
command_line="cd '$repo_path' && HYDRA_HOME='$home_path' HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 '$HYDRA_BIN' schedule run-now 'nightly'"

echo "Testing help, validation and exact timer text..."
"$HYDRA_BIN" schedule --help | grep -q 'hydra schedule run-now <name>'
assert_success $? "schedule --help documents run-now"
"$HYDRA_BIN" help | grep -q '^  schedule '
assert_success $? "hydra help lists schedule"
"$HYDRA_BIN" schedule add bad --every 7m --workflow "$base/quick.yml" >/dev/null 2>&1
assert_failure $? "a cadence that does not divide the hour is refused"
"$HYDRA_BIN" schedule add bad --every 15m >/dev/null 2>&1
assert_failure $? "a schedule needs a workflow or a pool"
"$HYDRA_BIN" schedule add bad --every 15m --workflow missing >/dev/null 2>&1
assert_failure $? "a missing workflow definition is refused"
out="$("$HYDRA_BIN" schedule add nightly --every 15m --workflow "$base/quick.yml" --format crontab)"
assert_success $? "schedule add succeeds"
assert_equal "# hydra schedule nightly: every 15m (install with crontab -e)
*/15 * * * * $command_line" "$(printf '%s\n' "$out" | sed -n '/^# hydra schedule/,$p')" "the crontab entry is exact"
printf '%s\n' "$out" | grep -q '@reboot'
assert_failure $? "no reboot entry without --reboot-recovery"
"$HYDRA_BIN" schedule add nightly --every 15m --workflow "$base/quick.yml" >/dev/null 2>&1
assert_failure $? "a duplicate schedule is refused"
sched_dir="$(schedule_path nightly)"
assert_equal "" "$(find "$sched_dir" ! -type l \( -perm -0020 -o -perm -0002 \) -print)" "schedule state is private"
assert_equal "$(git hash-object "$sched_dir/manifest.json")" "$(sed -n '1p' "$sched_dir/manifest-digest")" "the schedule manifest digest is recorded"
systemd_text="$("$HYDRA_BIN" schedule status nightly --format systemd | sed -n '/^# ~\/.config/,$p')"
assert_equal "# ~/.config/systemd/user/hydra-schedule-nightly.timer
[Unit]
Description=Hydra schedule nightly (every 15m)

[Timer]
OnCalendar=*-*-* *:0/15:00
Persistent=false
Unit=hydra-schedule-nightly.service

[Install]
WantedBy=timers.target

# ~/.config/systemd/user/hydra-schedule-nightly.service
[Unit]
Description=Hydra schedule nightly firing

[Service]
Type=oneshot
WorkingDirectory=$repo_path
Environment=HYDRA_HOME=$home_path
Environment=HYDRA_NONINTERACTIVE=1
Environment=HYDRA_SKIP_AI=1
ExecStart=$HYDRA_BIN schedule run-now nightly" "$systemd_text" "the systemd units are exact"
launchd_text="$("$HYDRA_BIN" schedule status nightly --format launchd | sed -n '/^<!-- /,$p')"
assert_equal "<!-- ~/Library/LaunchAgents/com.hydra.schedule.nightly.plist -->
<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
  <key>Label</key>
  <string>com.hydra.schedule.nightly</string>
  <key>ProgramArguments</key>
  <array>
    <string>$HYDRA_BIN</string>
    <string>schedule</string>
    <string>run-now</string>
    <string>nightly</string>
  </array>
  <key>WorkingDirectory</key>
  <string>$repo_path</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>HYDRA_HOME</key>
    <string>$home_path</string>
    <key>HYDRA_NONINTERACTIVE</key>
    <string>1</string>
    <key>HYDRA_SKIP_AI</key>
    <string>1</string>
  </dict>
  <key>StartInterval</key>
  <integer>900</integer>
  <key>RunAtLoad</key>
  <false/>
</dict>
</plist>" "$launchd_text" "the launchd plist is exact"
first_text="$("$HYDRA_BIN" schedule status nightly --format all)"
second_text="$("$HYDRA_BIN" schedule status nightly --format all)"
assert_equal "$first_text" "$second_text" "timer text is stable across calls"
out="$("$HYDRA_BIN" schedule add rebooting --every 6h --workflow "$base/quick.yml" --reboot-recovery --json)"
assert_envelope "$out" "schedule add" true "add emits the JSON envelope"
printf '%s' "$out" | grep -q '"reboot_recovery":true'
assert_success $? "reboot recovery is recorded"
printf '%s' "$out" | grep -q '\\n@reboot cd '
assert_success $? "the crontab entry adds @reboot only when opted in"
printf '%s' "$out" | grep -q 'Persistent=true'
assert_success $? "the systemd timer is persistent only when opted in"
printf '%s' "$out" | grep -q '<key>RunAtLoad</key>\\n  <true/>'
assert_success $? "the launchd plist runs at load only when opted in"
printf '%s' "$out" | grep -q '"crontab":"# hydra schedule rebooting: every 6h (install with crontab -e)\\n0 \*/6 \* \* \* cd '
assert_success $? "an hourly cadence maps to an exact cron hour field"
printf '%s' "$out" | grep -q 'OnCalendar=\*-\*-\* 0/6:00:00'
assert_success $? "an hourly cadence maps to an exact OnCalendar"
"$HYDRA_BIN" schedule add daily --every 1d --workflow "$base/quick.yml" --json | grep -q '"crontab":"# hydra schedule daily: every 1d (install with crontab -e)\\n0 0 \* \* \* cd '
assert_success $? "a daily cadence maps to midnight"
printf '%s' "$out" | grep -q '"execution_owner":"timer-process"'
assert_success $? "a workflow schedule names the timer process as execution owner"

echo "Testing dedup tokens..."
out="$("$HYDRA_BIN" schedule run-now nightly --json)"
assert_envelope "$out" "schedule run-now" true "run-now emits the JSON envelope"
slot="$(json_field "$out" slot)"
printf '%s' "$out" | grep -q "\"fired\":\\[{\"token\":\"nightly-$slot\",\"slot\":$slot,\"trigger\":\"run-now\",\"state\":\"finished\""
assert_success $? "the first firing submits with the slot token"
run_id="$(json_field "$out" run_id)"
case "$run_id" in run_*) assert_success 0 "the firing records the workflow run it owns" ;; *) assert_success 1 "the firing records the workflow run it owns ($out)" ;; esac
assert_equal w "$(cat "$base/fired")" "the firing ran the workflow through hydra workflow run"
out="$("$HYDRA_BIN" schedule run-now nightly --json)"
printf '%s' "$out" | grep -q "\"fired\":\\[\\],\"deduplicated\":\\[\"nightly-$slot\"\\]"
assert_success $? "a second trigger in the same slot is deduplicated"
assert_equal w "$(cat "$base/fired")" "the deduplicated trigger submitted nothing"
assert_equal "$slot" "$(sed -n '1p' "$sched_dir/last-slot")" "the last fired slot is recorded"
out="$("$HYDRA_BIN" schedule run-now nightly --token manual-1 --json)"
printf '%s' "$out" | grep -q '"fired":\[{"token":"manual-1"'
assert_success $? "an explicit token fires once"
"$HYDRA_BIN" schedule run-now nightly --token manual-1 --json | grep -q '"deduplicated":\["manual-1"\]'
assert_success $? "an explicit token is deduplicated afterwards"
assert_equal ww "$(cat "$base/fired")" "explicit tokens submit exactly once"
"$HYDRA_BIN" pool create inbox --max-outstanding 1 >/dev/null
"$HYDRA_BIN" schedule add enqueue --every 1h --pool inbox --workflow "$base/quick.yml" >/dev/null
i=1
while [ "$i" -le 4 ]; do
    HYDRA_POOL_LOCK_TRIES=400 "$HYDRA_BIN" schedule run-now enqueue --json > "$base/race-$i.json" 2>&1 &
    i=$((i + 1))
done
wait
assert_equal 1 "$(cat "$base"/race-*.json | grep -c '"fired":\[{')" "competing firings of one slot submit once"
assert_equal 3 "$(cat "$base"/race-*.json | grep -c '"deduplicated":\["enqueue-')" "the other competing firings are deduplicated"
out="$("$HYDRA_BIN" pool status inbox --json)"
printf '%s' "$out" | grep -q '"tasks":1,' && printf '%s' "$out" | grep -q '"task_id":"task-0001","state":"ready","kind":"workflow"'
assert_success $? "a pool schedule adds exactly one sealed task"
enqueue_dir="$(schedule_path enqueue)"
assert_equal task-0001 "$(cat "$enqueue_dir"/firings/*/task-id)" "the firing records the pool task it submitted"
"$HYDRA_BIN" schedule status enqueue --json | grep -q '"execution_owner":"pool-worker"'
assert_success $? "a pool-add schedule names the pool worker as execution owner"
"$HYDRA_BIN" schedule add drain --every 1h --pool inbox --json | grep -q '"kind":"pool-run"'
assert_success $? "a pool-only schedule runs the worker loop"
out="$("$HYDRA_BIN" schedule run-now drain --json)"
assert_success $? "a pool-run firing executes the queued task"
"$HYDRA_BIN" pool status inbox --json | grep -q '"task_id":"task-0001","state":"succeeded"'
assert_success $? "the queued task ran through the pool worker"

echo "Testing missed-run policies..."
interval=900
now="$(date +%s)"
current=$((now - now % interval))
printf '%s\n' "$((current - 5 * interval))" > "$sched_dir/last-slot"
out="$("$HYDRA_BIN" schedule run-now nightly --json)"
printf '%s' "$out" | grep -q '"missed":{"policy":"skip","detected":4,"caught_up":0,"skipped":4}'
assert_success $? "skip reports and skips every missed slot"
printf '%s' "$out" | grep -q '"fired":\[\],"deduplicated":\["nightly-'
assert_success $? "skip fires only the current slot (already fired here)"
assert_equal 4 "$(sed -n '1p' "$sched_dir/missed-total")" "skipped slots accumulate in the record"
"$HYDRA_BIN" schedule add catchup --every 15m --pool inbox --workflow "$base/quick.yml" --missed catch-up --catch-up-limit 2 >/dev/null
catch_dir="$(schedule_path catchup)"
printf '%s\n' "$((current - 5 * interval))" > "$catch_dir/last-slot"
out="$("$HYDRA_BIN" schedule run-now catchup --json)"
printf '%s' "$out" | grep -q '"missed":{"policy":"catch-up","detected":4,"caught_up":2,"skipped":2}'
assert_success $? "catch-up is bounded by the limit"
assert_equal "catchup-$((current - 2 * interval)) catchup-$((current - interval)) catchup-$current" \
    "$(printf '%s' "$out" | sed 's/},{/}\n{/g' | sed -n 's/.*"token":"\([^"]*\)".*/\1/p' | tr '\n' ' ' | sed 's/ $//')" "the newest missed slots are caught up in order, then the current slot"
assert_equal 2 "$(grep -c '"trigger":"catch-up"' <<EOF
$(printf '%s' "$out" | sed 's/},{/}\n{/g')
EOF
)" "caught-up firings are marked catch-up"
"$HYDRA_BIN" pool status inbox --json | grep -q '"tasks":4,'
assert_success $? "each caught-up firing submitted one task"
out="$("$HYDRA_BIN" schedule run-now catchup --json)"
printf '%s' "$out" | grep -q '"missed":{"policy":"catch-up","detected":0,"caught_up":0,"skipped":0}'
assert_success $? "nothing is missed once caught up"
grep -q '"type":"schedule.missed","detail":"policy=catch-up detected=4 caught_up=2 skipped=2"' "$catch_dir/events.jsonl"
assert_success $? "missed runs are recorded as events"

echo "Testing status, list and remove..."
out="$("$HYDRA_BIN" schedule status nightly --json)"
assert_envelope "$out" "schedule status" true "status emits the JSON envelope"
printf '%s' "$out" | grep -q '"firings":\[{"token":"manual-1"' && printf '%s' "$out" | grep -q "\"next_slot\":$((current + interval))"
assert_success $? "status lists firings and the next slot"
"$HYDRA_BIN" schedule status nightly | grep -q '^  firing manual-1: finished (slot '
assert_success $? "text status lists firings"
out="$("$HYDRA_BIN" schedule list --json)"
assert_envelope "$out" "schedule list" true "list emits the JSON envelope"
printf '%s' "$out" | grep -q '"schedule":"catchup","state":"enabled","every":"15m","kind":"pool-add"'
assert_success $? "list names schedules with their cadence and kind"
out="$("$HYDRA_BIN" schedule remove catchup --json)"
assert_envelope "$out" "schedule remove" true "remove emits the JSON envelope"
[ ! -d "$catch_dir" ]
assert_success $? "remove deletes the schedule record"
"$HYDRA_BIN" schedule run-now catchup >/dev/null 2>&1
assert_failure $? "a removed schedule cannot fire"
"$HYDRA_BIN" state verify >/dev/null 2>&1
assert_success $? "state verifies with schedule records present"
"$HYDRA_BIN" kill sched-head --force >/dev/null 2>&1 || true

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
