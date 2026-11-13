#!/bin/sh
# Statistics schema 5: a recorded run whose step was dispatched as a remote
# task appears in `workflow statistics-data` as a T row and in
# `workflow statistics-json` as remote cohorts and telemetry, using only the
# records the fleet protocol wrote. Unmeasured values stay "-"/null with a
# reason; local steps form their own cohort and are never measured as zero.

test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
repo="$base/repo"
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
export HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=workflow_statistics}"
export HYDRA_CORE="${HYDRA_CORE:?HYDRA_CORE is required: run via make test or make test-one T=workflow_statistics}"
trap 'rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
tab="$(printf '\t')"

echo "Testing remote workflow cohorts in statistics..."

mkdir -p "$repo/.hydra/workflows"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
printf '%s\n' \
    'version: 1' \
    'id: local-run' \
    'parallelism: 1' \
    'resources:' \
    '  disk_mb: 1' \
    '  max_heads: 1' \
    'steps:' \
    '  - id: spawn-head' \
    '    kind: spawn' \
    '    needs: []' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      branch: statistics-workflow' \
    '      terminal_mode: headless' \
    '  - id: write' \
    '    kind: exec' \
    '    needs: [spawn-head]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: statistics-workflow' \
    '      argv: [sh, -c, "printf local > local.txt"]' > .hydra/workflows/local-run.yml
git add README .hydra
git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "init records host-local trust"

run="$("$HYDRA_BIN" workflow run local-run 2>/dev/null)"
assert_success $? "a local workflow run completes"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print | sed -n '1p')"
assert_equal succeeded "$(sed -n '1p' "$run_dir/state" 2>/dev/null)" "the local run succeeded"

"$HYDRA_BIN" workflow statistics-data > "$base/local.tsv" 2>/dev/null
assert_equal "HYDRA_STATISTICS${tab}5" "$(sed -n '1p' "$base/local.tsv" | cut -f1-2)" "statistics-data is schema 5"
assert_equal 0 "$(grep -c "^T${tab}" "$base/local.tsv")" "a local run has no T rows"
"$HYDRA_BIN" workflow statistics-json > "$base/local.json" 2>/dev/null
assert_success $? "statistics-json exports the local run"
grep -q '"schema_version":3' "$base/local.json"
assert_success $? "the export is schema 3"
grep -q '"remote":{"state":"unavailable","remote_steps":0,"transfer_bytes":null' "$base/local.json"
assert_success $? "remote evidence is unavailable, not zero, for local steps"
grep -q '"local_steps":"no task receipt: CPU, memory and transport are not measured for local steps"' "$base/local.json"
assert_success $? "the export says why local steps are unmeasured"
grep -q '{"key":"local","runs":1,"steps":2' "$base/local.json"
assert_success $? "local steps form the local cohort"

# Record a dispatched attempt for the exec step the way the coordinator does:
# a task binding, the receipt and observation, transport metrics and a
# placement record. Nothing here is executed; the reader only reads records.
task_id="task_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
remote="$run_dir/steps/write/attempt-1/remote"
mkdir -m 0700 "$remote"
assert_success $? "the attempt gains a private remote record directory"
printf '{"schema_version":1,"steps":{"write":{"destination":{"kind":"ssh","alias":"alpha"}}}}\n' > "$run_dir/tasks.json"
chmod 0600 "$run_dir/tasks.json"
receipt="{\"task_id\":\"$task_id\",\"spec_sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"submission_key\":\"fixture\",\"assignment_generation\":2,\"runtime\":{\"schema_version\":1,\"state\":\"succeeded\",\"launch_intent\":\"started\",\"run_id\":\"run_receiver\",\"exit_status\":0"
printf '%s}}\n' "$receipt" > "$remote/receipt.json"
printf '%s,"telemetry":{"schema_version":1,"measured_by":"task_owner","scope":"execution_phase","clock":"monotonic","cpu_source":"getrusage_children","rss_source":"getrusage_children_maxrss","invocations":1,"wall_seconds":12,"cpu_user_microseconds":300000,"cpu_system_microseconds":50000,"unmeasured":{"peak_rss_bytes":"rss_not_distinguishable"}}}}\n' "$receipt" > "$remote/observation.json"
printf '{"schema_version":1,"calls":2,"request_bytes":10,"response_bytes":20,"complete":true}\n' > "$remote/transport-metrics.json"
chmod 0600 "$remote"/*.json

"$HYDRA_BIN" workflow statistics-data > "$base/remote.tsv" 2>/dev/null
assert_success $? "statistics-data reads the dispatched attempt"
grep -q "^T${tab}$run${tab}write${tab}alpha${tab}ssh${tab}pinned${tab}2${tab}succeeded${tab}12${tab}350000${tab}-${tab}2${tab}30${tab}peak_rss_bytes=rss_not_distinguishable\$" "$base/remote.tsv"
assert_success $? "the T row carries host, pinned placement, generation, telemetry, transport and the unmeasured reason"
"$HYDRA_BIN" workflow statistics-json > "$base/remote.json" 2>/dev/null
assert_success $? "statistics-json exports the remote cohorts"
grep -q '"remote":{"state":"partial","remote_steps":1,"transfer_bytes":30,"provider_cost":null,"cpu_seconds":0.350000,"memory_bytes":null,"tokens":null}' "$base/remote.json"
assert_success $? "the remote block reports measured bytes and CPU and null peak RSS"
grep -q '{"key":"alpha","runs":1,"steps":1' "$base/remote.json"
assert_success $? "the host cohort names the alias"
grep -q '{"key":"pinned","runs":1,"steps":1' "$base/remote.json"
assert_success $? "a workflow binding is a pinned placement"
grep -q '{"key":"generation 2"' "$base/remote.json"
assert_success $? "the receipt generation names the generation cohort"
grep -q '"peak_rss_bytes":{"state":"unknown","eligible":1,"known":0,"max":null}' "$base/remote.json"
assert_success $? "an unmeasured peak RSS is null, never zero"
grep -q '"unmeasured_steps":1,"unmeasured_reason":"peak_rss_bytes=rss_not_distinguishable"' "$base/remote.json"
assert_success $? "the cohort names the recorded reason"
grep -q '{"key":"local","runs":1,"steps":1' "$base/remote.json"
assert_success $? "the spawn step stays in the local cohort"

# An automatic placement record (digest-sealed by the fleet helper) makes the
# cohort auto; a tampered record is refused and the step stays pinned.
placement="$HYDRA_HOME/fleet/placements/$task_id"
for placement_dir in "$HYDRA_HOME/fleet" "$HYDRA_HOME/fleet/placements" "$placement"; do
    [ -d "$placement_dir" ] || mkdir -m 0700 "$placement_dir"
done
printf '{"schema_version":1,"task_id":"%s","submission_key":"fixture","mode":"auto","generation":2,"assigned_host":"alpha","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}\n' "$task_id" > "$placement/placement.json"
chmod 0600 "$placement/placement.json"
"$HYDRA_BIN" workflow statistics-data 2>/dev/null | grep -q "^T${tab}$run${tab}write${tab}alpha${tab}ssh${tab}pinned${tab}"
assert_success $? "a placement record whose digest does not match is ignored"
"$HYDRA_BIN" fleet placement explain "$task_id" >/dev/null 2>&1 && placement_status=0 || placement_status=$?
assert_failure "$placement_status" "the tampered placement record is refused by placement explain"

# Without a telemetry block the receipt leaves every measurement unknown.
printf '%s}}\n' "$receipt" > "$remote/observation.json"
"$HYDRA_BIN" workflow statistics-data 2>/dev/null | grep -q "^T${tab}$run${tab}write${tab}alpha${tab}ssh${tab}pinned${tab}2${tab}succeeded${tab}-${tab}-${tab}-${tab}2${tab}30${tab}no_telemetry_block\$"
assert_success $? "a receipt without telemetry leaves wall, CPU and RSS unknown"
"$HYDRA_BIN" workflow statistics-json 2>/dev/null | grep -q '"remote_steps_without_telemetry":1'
assert_success $? "the export counts receipts without a telemetry block"

# Recorded runs stay readable when the remote record is unverifiable.
printf '{"task_id":"task_cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"}\n' > "$remote/observation.json"
"$HYDRA_BIN" workflow statistics-data 2>/dev/null | grep -q "^T${tab}$run${tab}write${tab}alpha${tab}ssh${tab}pinned${tab}-${tab}-${tab}-${tab}-${tab}-${tab}2${tab}30${tab}receipt_unverified\$"
assert_success $? "an observation that differs from its receipt is reported unverified; only the coordinator's transport record remains"
"$HYDRA_BIN" workflow statistics-json > "$base/unverified.json" 2>/dev/null
assert_success $? "statistics-json still exports the run"
grep -q '"unmeasured_reason":"remote receipt could not be verified against its dispatch"' "$base/unverified.json"
assert_success $? "the cohort says the receipt could not be verified"

"$HYDRA_BIN" kill --all --force >/dev/null 2>&1 || true

echo ""
echo "Tests run: $test_count, Passed: $pass_count, Failed: $fail_count"
[ "$fail_count" -eq 0 ]
