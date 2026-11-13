#!/bin/sh
# Concurrent supervised headless runs report provider status together. Every
# run must still complete with its result: status observations are heuristic,
# so one the project state cannot record in time is kept in the receipt with
# its reason instead of stopping a healthy worker (item 10 finding 1).
#
# HYDRA_TEST_OBSERVATION_HEADS (default 10) and HYDRA_TEST_OBSERVATION_LINES
# (status lines per run after the shared start, default 40) scale the case.

test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

heads="${HYDRA_TEST_OBSERVATION_HEADS:-10}"
lines="${HYDRA_TEST_OBSERVATION_LINES:-40}"
case "$heads:$lines" in *[!0-9:]*|:*|*:) echo "heads and lines must be integers" >&2; exit 2 ;; esac

base="$(test_mktemp_dir)"
repo="$base/repo"
holder=""
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
export HYDRA_SETUP_CONTINUE=1
export HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=concurrent_headless_observation}"
cleanup() {
    [ -z "$holder" ] || kill "$holder" 2>/dev/null || true
    test_stop_processes_naming "$base"
    rm -rf "$base"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

mkdir -p "$repo" "$base/ready" "$base/runs"
git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name "Hydra test"
printf 'observation\n' > "$repo/README"
git -C "$repo" add README
git -C "$repo" commit -qm initial
cd "$repo" || exit 1

# Each run reports running, waits for the shared start, then emits a burst of
# status lines and its result, and reports idle: all heads observe together.
cat > "$base/provider" <<PROVIDER
#!/bin/sh
case "\${1:-}" in --help|--version) printf '%s\n' observation-fixture; exit 0 ;; esac
running='{"schema_version":1,"type":"observation","status":"running"}'
printf '%s\n' "\$running"
name="\$(basename "\$PWD")"
: > "$base/ready/\$name"
waited=0
while [ ! -e "$base/go" ]; do
    waited=\$((waited + 1))
    [ "\$waited" -lt 2400 ] || exit 3
    sleep 0.05
done
line=0
while [ "\$line" -lt $lines ]; do
    printf '%s\n' "\$running"
    line=\$((line + 1))
done
printf '{"schema_version":1,"type":"result","text":"done-%s"}\n' "\$name"
printf '%s\n' '{"schema_version":1,"type":"observation","status":"idle"}'
PROVIDER
chmod +x "$base/provider"
cat > "$base/profile.json" <<EOF
{"schema_version":1,"executable":"$base/provider","argv":[{"input":"prompt"}],"prompt":"argument","session":"none","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["observation-fixture"]}
EOF
printf 'observe\n' > "$base/prompt"

"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "init the fixture project"
"$HYDRA_BIN" agent import observation-fixture "$base/profile.json" >/dev/null 2>&1
assert_success $? "import the observation fixture profile"
project="$(sed -n '1p' .git/hydra/project-id)"

spawned=0
i=1
while [ "$i" -le "$heads" ]; do
    "$HYDRA_BIN" spawn --headless --no-agent "obs-$i" >/dev/null 2>&1 && spawned=$((spawned + 1))
    i=$((i + 1))
done
assert_equal "$heads" "$spawned" "spawn $heads headless heads"

# Print fixture evidence after a failed assertion so the log says why.
show_failure() {
    [ "$fail_count" -eq "$1" ] && return 0
    shift
    for _sf_file in "$@"; do
        printf '  --- %s\n' "$_sf_file"
        head -c 3000 "$_sf_file" 2>/dev/null | sed 's/^/  /'
        printf '\n'
    done
}
receipt_of() {
    _ro_run="$(sed -n 's/.*"run_id":"\(run_[a-z0-9]*\)".*/\1/p' "$base/runs/$1.json")"
    printf '%s\n' "$HYDRA_HOME/state/v2/projects/$project/exec/$_ro_run/$2/agent.json"
}
ready_count() { [ "$(find "$base/ready" -type f | wc -l | tr -d ' ')" -ge "$1" ]; }
run_exec() {
    "$HYDRA_BIN" exec --branch "$1" --profile observation-fixture --prompt-file "$base/prompt" \
        --require prompt,observations --result-file result.txt --timeout 300 --exit-code --json \
        > "$base/runs/$1.json" 2> "$base/runs/$1.stderr"
}

# Start owners one at a time, as a user or the item 10 fixture does, so only
# the observations coincide; the burst begins together after the last is ready.
pids=""
started=0
i=1
while [ "$i" -le "$heads" ]; do
    run_exec "obs-$i" &
    pids="$pids $!"
    WAIT_FOR_TIMEOUT=60 wait_for "worker obs-$i ready" ready_count "$i" && started=$((started + 1))
    i=$((i + 1))
done
assert_equal "$heads" "$started" "every supervised worker reached the shared start"
: > "$base/go"
succeeded=0
for pid in $pids; do
    wait "$pid" && succeeded=$((succeeded + 1))
done
assert_equal "$heads" "$succeeded" "every concurrent supervised run exits 0"
[ "$succeeded" -eq "$heads" ] || for f in "$base/runs"/*.json; do grep -q '"exit_code":0' "$f" || show_failure -1 "$f"; done

completed=0 results=0 settled=0 explained=0 lost=0
i=1
while [ "$i" -le "$heads" ]; do
    head_dir="$(dirname "$(grep -lx "obs-$i" "$HYDRA_HOME/state/v2/projects/$project/heads"/head_*/branch)")"
    head="$(basename "$head_dir")"
    worktree="$(sed -n '1p' "$head_dir/worktree")"
    instance="$(sed -n '1p' "$head_dir/current-instance")"
    receipt="$(receipt_of "obs-$i" "$head")"
    grep -Fq '"state":"completed"' "$receipt" 2>/dev/null && completed=$((completed + 1))
    [ "$(cat "$worktree/result.txt" 2>/dev/null)" = "done-$(basename "$worktree")" ] && results=$((results + 1))
    # The final status is either recorded or named in the receipt with why.
    observed="$(sed -n '1p' "$head_dir/instances/$instance/observed-status" 2>/dev/null)"
    if grep -Fq '"observation":{' "$receipt" 2>/dev/null; then
        lost=$((lost + 1))
        printf '%s\n' "$(sed -n 's/.*\("observation":{[^}]*}[^}]*}\).*/  obs-'"$i"' \1/p' "$receipt")"
        grep -Eq '"observation":\{"failures":[1-9][0-9]*,"exit_status":[0-9]+,"timed_out":(true|false),"stderr":' "$receipt" &&
            explained=$((explained + 1))
    fi
    if [ "$observed" = idle ] || grep -Fq '"unrecorded_status":"idle"' "$receipt" 2>/dev/null; then settled=$((settled + 1)); fi
    i=$((i + 1))
done
assert_equal "$heads" "$completed" "every receipt records a completed run"
assert_equal "$heads" "$results" "every run kept its final result"
assert_equal "$lost" "$explained" "every lost observation records its ingest exit status and stderr"
assert_equal "$heads" "$settled" "every final idle status is recorded or named as unrecorded"
echo "  observations lost in $lost of $heads runs"
grep -l observation_unavailable "$HYDRA_HOME/state/v2/projects/$project/exec"/*/*/agent.json >/dev/null 2>&1
assert_failure $? "no run ends observation_unavailable"
leftover="$(find "$HYDRA_HOME/locks" -name 'state_*.lock' -o -name 'events_*.lock' 2>/dev/null)"
assert_equal "" "$leftover" "no state or event lock is left behind"

# A held project state lock stands in for an overloaded store: every status
# ingest fails, and the run still completes and says why in its receipt.
head_dir="$(dirname "$(grep -lx obs-1 "$HYDRA_HOME/state/v2/projects/$project/heads"/head_*/branch)")"
head="$(basename "$head_dir")"
instance="$(sed -n '1p' "$head_dir/current-instance")"
sleep 300 &
holder=$!
lock="$HYDRA_HOME/locks/state_$project.lock"
mkdir "$lock" && printf '%s\n' "$holder" > "$lock/pid" && uname -n > "$lock/host"
printf '{"schema_version":1,"instance_id":"%s","kind":"observed","status":"running"}' "$instance" |
    HYDRA_LOCK_RETRIES=2 "$HYDRA_BIN" adapter ingest obs-1 > /dev/null 2> "$base/busy.stderr"
assert_failure $? "an ingest refused by a busy state lock fails"
failed_before="$fail_count"
grep -Fq 'project state lock is busy' "$base/busy.stderr"
assert_success $? "the ingest names the busy project state lock"
show_failure "$failed_before" "$base/busy.stderr"
printf '{"schema_version":1,"instance_id":"%s","kind":"observed","status":"working"}' "$instance" |
    "$HYDRA_BIN" adapter ingest obs-1 > /dev/null 2> "$base/status.stderr"
grep -Fq "unsupported adapter status 'working'" "$base/status.stderr"
assert_success $? "an unsupported status is reported before any lock is taken"
failed_before="$fail_count"
rm -f "$(sed -n '1p' "$head_dir/worktree")/result.txt"
run_exec obs-1
assert_success $? "a run whose status cannot be recorded still exits 0"
receipt="$(receipt_of obs-1 "$head")"
grep -Fq '"state":"completed"' "$receipt"
assert_success $? "the run with a busy store records completed"
grep -Eq '"observation":\{"failures":[1-9][0-9]*,"exit_status":1,"timed_out":false,"stderr":\{"text":"Error: project state lock is busy' "$receipt"
assert_success $? "the receipt keeps the failed ingest's exit status and stderr"
grep -Fq '"unrecorded_status":"idle"' "$receipt"
assert_success $? "the receipt names the final status that was not recorded"
# Lines repeating a status and lines inside the retry interval cost no ingest:
# $lines + 2 status lines lead to a few attempts, not one per line.
attempts="$(sed -n 's/.*"observation":{"failures":\([0-9]*\),.*/\1/p' "$receipt")"
[ "${attempts:-0}" -ge 1 ] && [ "${attempts:-0}" -le 3 ]
assert_success $? "a busy store costs at most three status ingests ($attempts)"
show_failure "$failed_before" "$base/runs/obs-1.json" "$base/runs/obs-1.stderr" "$receipt"
kill "$holder" 2>/dev/null || true
wait "$holder" 2>/dev/null || true
holder=""
rm -f "$lock/pid" "$lock/host"
rmdir "$lock"

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
