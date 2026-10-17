#!/bin/sh
# Plan runs, the heads they create, and what the native TUI is told about them:
# run ownership, step roles, agent receipts (model, effort, tokens), headless
# step output, and verifier-head retirement for succeeded, failed and
# cancelled runs. Synthetic providers only; no tmux.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test or make test-one T=<name>}"
case "$FLEET_BIN" in /*) ;; *) FLEET_BIN="$REPO/$FLEET_BIN" ;; esac
export HYDRA_TEST_ROOT="$REPO"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
export CODEX_HOME="$root/codex-home"
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
# shellcheck source=/dev/null
. "$REPO/tests/headless_path.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    rm -f "$root/hold"
    if [ "$fail_count" -ne 0 ]; then printf 'Failure evidence: %s\n' "$root"; return; fi
    rm -rf "$root"
}
test_code=0
trap 'test_code=$?; cleanup; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

contains() {
    if printf '%s\n' "$2" | grep -Fq -- "$1"; then assert_success 0 "$3"; else assert_success 1 "$3"; fi
}

echo "Running workflow run head tests..."

# Readable provider events and receipt summaries.
view="$("$FLEET_BIN" agent-view stream-view codex-jsonl "$REPO/tests/fixtures/agents/codex-jsonl.jsonl" 10)"
contains 'assistant: fixture artifact' "$view" 'codex events render as readable assistant text'
contains 'turn completed: tokens in 17, cached 0, out 3' "$view" 'codex turn usage renders with its counters'
printf 'not json\n' > "$root/raw.jsonl"
contains 'not json' "$("$FLEET_BIN" agent-view stream-view codex-jsonl "$root/raw.jsonl" 5)" 'undecodable provider lines fall back to raw text'
"$FLEET_BIN" agent-view stream-view codex-jsonl "$root/absent.jsonl" 5 >/dev/null 2>&1
assert_failure $? 'a missing live stream is reported as unavailable'

mkdir -p "$root/bin" "$CODEX_HOME"
headless_path "$root/no-tmux"
PATH="$root/bin:$PATH"
export PATH
printf 'model = "fixture-model"\nmodel_reasoning_effort = "high"\n\n[profiles.other]\nmodel = "not-selected"\n' > "$CODEX_HOME/config.toml"
cat > "$root/bin/codex" <<'CODEX'
#!/bin/sh
# Synthetic codex exec --json: streams events; waits while $CODEX_FIXTURE_HOLD exists.
set -eu
case " $* " in
    *' --version '*) printf 'codex-cli 9.9.9-fixture\n'; exit 0 ;;
    *' --help '*) printf 'Usage: codex exec --json resume\n'; exit 0 ;;
esac
prompt="$(cat)"
printf '%s\n' '{"type":"thread.started","thread_id":"fixture-thread"}' '{"type":"turn.started"}'
printf '%s\n' '{"type":"item.completed","item":{"id":"i1","type":"command_execution","command":"sh -c true","exit_code":0,"status":"completed"}}'
if [ -n "${CODEX_FIXTURE_HOLD:-}" ]; then while [ -e "$CODEX_FIXTURE_HOLD" ]; do sleep 0.2; done; fi
printf 'worker change\n' >> NOTES.md
answer="$(printf '%s\n' "$prompt" | sed -n 's/^ARTIFACT=//p')"
printf '{"type":"item.completed","item":{"id":"i2","type":"agent_message","text":"%s"}}\n' "$answer"
printf '%s\n' '{"type":"turn.completed","usage":{"input_tokens":1200,"cached_input_tokens":800,"output_tokens":34}}'
CODEX
chmod +x "$root/bin/codex"

mkdir "$root/repo"
cd "$root/repo" || exit 1
cp "$REPO/tests/fixtures/plan/repo/check.sh" .
printf 'touch leftover.txt\n' > dirty-check.sh
cat check.sh >> dirty-check.sh
printf exact_artifact > expected.txt
printf '# notes\n' > NOTES.md
git init -q && git config user.name Test && git config user.email test@example.invalid
git add . && git -c commit.gpgSign=false commit -qm fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" spawn planner --headless --no-agent >/dev/null
project="$(cat .git/hydra/project-id)"
head_id="$(basename "$(dirname "$(grep -lx planner "$HYDRA_HOME/state/v2/projects/$project/heads"/*/branch)")")"
instance="$(sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/heads/$head_id/current-instance")"
runs="$HYDRA_HOME/state/v2/projects/$project/workflows/runs"
cat > "$root/policy.json" <<'JSON'
{"schema_version":1,"envelope":{"hosts":["local"],"tools":["sh","git","make","profile:codex"],"effects":["execute","worktree"],"writes":["@spawned:*"],"parallelism":1,"timeout_seconds":600,"artifact_bytes":65536,"max_heads":4,"disk_mb":1,"retry_budget":0,"repair_budget":0}}
JSON

# Usage: write_plan <name> <artifact> <check-script> <out>
# The verifier head is spawned first so a cancelled run has one to retire.
write_plan() {
    cat > "$4" <<JSON
{"schema_version":1,"id":"$1","objective":"Deliver a report and verify it","context":[],"assumptions":[],"questions":[],
 "envelope":{"hosts":["local"],"tools":["sh","git","make","profile:codex"],"effects":["worktree","execute"],"writes":[],"parallelism":1,"timeout_seconds":300,"artifact_bytes":4096,"max_heads":2,"disk_mb":1,"retry_budget":0,"repair_budget":0},
 "steps":[
  {"id":"spawn-check","role":"work","kind":"spawn","needs":[],"writes":[],"args":{"branch":"$1-check","terminal_mode":"headless"}},
  {"id":"spawn-worker","role":"work","kind":"spawn","needs":[],"writes":[],"args":{"branch":"$1-worker","terminal_mode":"headless"}},
  {"id":"implement","role":"compose","kind":"exec","needs":["spawn-worker","spawn-check"],"writes":[],"args":{"head":"$1-worker","profile":"codex","prompt":"Write the report.\nARTIFACT=$2\n","result_file":"report","timeout":120}},
  {"id":"verify","role":"verify","kind":"exec","needs":["implement"],"writes":[],"args":{"head":"$1-check","argv":["sh","$3"],"timeout":60}}],
 "deliverables":[{"id":"report","description":"Report","step":"implement","output":"report","destination":"run-artifact"}],
 "checks":[{"id":"check","method":"executable","definition":"Compare expected text","step":"verify","input":"subject","report":"check","deliverable":"report"}],
 "requirements":[{"id":"content","criterion":"Report has the expected contents","deliverable":"report","check":"check"}],
 "data":{"schema_version":1,"inputs":{"expected":{"path":"expected.txt","type":"file","max_bytes":128}},
  "steps":{"implement":{"outputs":{"report":{"type":"file","path":"report","max_bytes":1024}}},
   "verify":{"inputs":{"subject":{"step":"implement","output":"report"},"expected":{"input":"expected"}},
    "outputs":{"check":{"type":"object","path":"check.json","max_bytes":2048}}}}}}
JSON
}

# Usage: launch <name> -> runs the plan through the workspace owner; prints the run ID
launch() {
    "$HYDRA_BIN" workflow plan compile "$root/$1.json" "$root/policy.json" "$root/$1.compiled" > "$root/$1.compile" 2>&1 || return 1
    _l_digest="$("$HYDRA_BIN" workflow plan show "$root/$1.compiled" | sed -n 's/^Acceptance digest: //p')"
    "$HYDRA_BIN" workflow plan --workspace-owner "$_l_digest" "$head_id" "$instance" < "$root/$1.compiled" > "$root/$1.owner" 2>&1
    _l_status=$?
    sed -n '1p' "$HYDRA_HOME/state/v2/projects/$project/workflows/launches/$_l_digest/run-id" 2>/dev/null
    return "$_l_status"
}

branch_kept() { git show-ref --verify --quiet "refs/heads/$1"; }
head_active() { "$HYDRA_BIN" list 2>/dev/null | grep -q " $1 "; }

# --- A succeeded run: worker kept, verifier retired after its evidence is sealed.
write_plan ok exact_artifact check.sh "$root/ok.json"
ok_run="$(launch ok)"
assert_success $? 'the succeeded plan run completes'
data="$("$HYDRA_BIN" workflow tui-data)"
contains "$(printf 'W\t%s\tok\tsucceeded\tplan\tplanner\t' "$ok_run")" "$data" 'the run row names its kind and planning head'
contains "$(printf 'N\t%s\timplement\texec\tsucceeded\t1\tspawn-worker,spawn-check\tcompose\tok-worker\tcodex\t' "$ok_run")" "$data" 'the step row carries role, head and profile'
exec_row="$(printf '%s\n' "$data" | awk -F '\t' -v run="$ok_run" '$1 == "E" && $2 == run && $3 == "implement"')"
assert_equal codex-cli "$(printf '%s' "$exec_row" | cut -f10 | cut -d' ' -f1)" 'the receipt summary names the executable version'
assert_equal 'fixture-model configured high configured' "$(printf '%s' "$exec_row" | cut -f11-14 | tr '\t' ' ')" \
    'model and effort come from configuration and are labelled as configuration'
assert_equal '1200 800 34 -' "$(printf '%s' "$exec_row" | cut -f15-18 | tr '\t' ' ')" 'tokens are recorded and an unreported cost stays unknown'
contains "$(printf 'R\t%s\tok-worker\tspawn-worker\tworker\t-\t-' "$ok_run")" "$data" 'the worker head is kept for the user'
contains "$(printf 'R\t%s\tok-check\tspawn-check\tverifier\tretired\t' "$ok_run")" "$data" 'the verifier head is retired'
grep -q '"type":"head.retired"' "$runs/$ok_run/events.jsonl"
assert_success $? 'the retirement is recorded as a run event'
head_active ok-check
assert_failure $? 'the retired verifier head is no longer active'
branch_kept ok-check
assert_success $? 'the retired verifier keeps its branch'
head_active ok-worker
assert_success $? 'the worker head stays until the user lands or dismisses it'
sealed=1
[ ! -s "$runs/$ok_run/steps/verify/attempt-1/data-seal.json" ] || sealed=0
assert_success "$sealed" 'the verifier evidence stays sealed in the run record'
output="$("$HYDRA_BIN" tui --head-output ok-worker)"
contains 'Step implement (exec, codex) succeeded' "$output" 'headless output names the last step, its agent and state'
contains 'exact_artifact' "$output" 'a finished agent step shows its declared result'
output="$("$HYDRA_BIN" tui --head-output ok-check)"
contains 'Result (check)' "$output" 'a finished command step shows its sealed report'
contains 'No workflow step has run on planner yet.' "$("$HYDRA_BIN" tui --head-output planner)" 'a head without steps says so'
receipt="$(find "$HYDRA_HOME/state/v2/projects/$project/exec" -name agent.json | head -n 1)"
grep -q '"configuration":' "$receipt"
assert_success $? 'the agent receipt records the provider configuration it read'
if grep -q '"observed_model"' "$receipt"; then assert_success 1 'codex events that name no model record no observed model'; else assert_success 0 'codex events that name no model record no observed model'; fi
streams="$(find "$HYDRA_HOME/state/v2/projects/$project/exec" -name .provider-stdout)"
assert_equal '' "$streams" 'the live provider stream is removed when the step ends'

# --- A failed run: the verifier still retires; the worker holds what it made.
write_plan bad wrong_artifact check.sh "$root/bad.json"
bad_run="$(launch bad)"
assert_failure $? 'the failing plan run reports failure'
data="$("$HYDRA_BIN" workflow tui-data)"
contains "$(printf 'W\t%s\tbad\tfailed\t' "$bad_run")" "$data" 'the failed run is projected as failed'
contains "$(printf 'R\t%s\tbad-check\tspawn-check\tverifier\tretired\t' "$bad_run")" "$data" 'a failed run retires its verifier head'
head_active bad-worker
assert_success $? 'a failed run keeps its worker head'
bad_digest="$("$HYDRA_BIN" workflow plan show "$root/bad.compiled" | sed -n 's/^Acceptance digest: //p')"
assert_equal "$(printf 'HYDRA_PLAN_LAUNCH\t1\nL\t%s\tfinished\t%s\t1' "$bad_digest" "$bad_run")" \
    "$("$HYDRA_BIN" workflow plan --workspace-status "$bad_digest")" 'the launch owner of a failed run records that it finished and its exit code'

# --- A verifier that leaves files behind is kept for review.
write_plan dirty exact_artifact dirty-check.sh "$root/dirty.json"
dirty_run="$(launch dirty)"
data="$("$HYDRA_BIN" workflow tui-data)"
contains "$(printf 'R\t%s\tdirty-check\tspawn-check\tverifier\tkept\t' "$dirty_run")" "$data" 'a verifier head with changes is kept'
grep -q '"type":"head.retire_skipped"' "$runs/$dirty_run/events.jsonl"
assert_success $? 'the kept verifier is recorded as a skipped retirement'
head_active dirty-check
assert_success $? 'the dirty verifier head stays active'

# --- A cancelled run: live output while the agent runs, then retirement.
write_plan stop exact_artifact check.sh "$root/stop.json"
: > "$root/hold"
CODEX_FIXTURE_HOLD="$root/hold" launch stop > "$root/stop.run" 2>&1 &
owner=$!
waited=0
while [ "$waited" -lt 100 ]; do
    stop_run="$(find "$runs" -mindepth 1 -maxdepth 1 -newer "$root/stop.json" -name 'run_*' -exec basename {} \; 2>/dev/null | head -n 1)"
    [ -n "$stop_run" ] && [ -n "$(find "$HYDRA_HOME/state/v2/projects/$project/exec" -name .provider-stdout 2>/dev/null)" ] && break
    sleep 0.2; waited=$((waited + 1))
done
# The provider writes its events asynchronously; poll for the rendered event
# rather than sleeping a fixed time, then assert on the observed output.
# shellcheck disable=SC2329,SC2317 # Invoked through wait_for.
head_output_has_event() {
    "$HYDRA_BIN" tui --head-output stop-worker > "$root/stop-output" 2>&1 || :
    cat "$root/stop-output"
    grep -Fq -- '$ sh -c true -> exit 0' "$root/stop-output"
}
wait_for 'the running step to render its live provider event' head_output_has_event || :
output="$(cat "$root/stop-output")"
contains 'Step implement (exec, codex) running' "$output" 'a running agent step is named in the headless output'
contains 'Live output (read-only' "$output" 'the running step offers its live output'
contains '$ sh -c true -> exit 0' "$output" 'live provider events are rendered readably'
data="$("$HYDRA_BIN" workflow tui-data)"
contains "$(printf '\timplement\t1\tcodex\trunning\t')" "$data" 'a running agent step reports a running receipt'
"$HYDRA_BIN" workflow cancel "$stop_run" > "$root/cancel.out" 2>&1
rm -f "$root/hold"
wait "$owner" 2>/dev/null || true
# Cancellation signals the owner's shell; the run's driver publishes the
# result and then retires the verifier, so allow it a moment to finish.
waited=0
while [ ! -f "$runs/$stop_run/retirement/spawn-check/state" ] && [ "$waited" -lt 50 ]; do sleep 0.2; waited=$((waited + 1)); done
data="$("$HYDRA_BIN" workflow tui-data)"
contains "$(printf 'W\t%s\tstop\tcancelled\t' "$stop_run")" "$data" 'the run is cancelled'
contains "$(printf 'R\t%s\tstop-check\tspawn-check\tverifier\tretired\t' "$stop_run")" "$data" 'a cancelled run retires its verifier head'
branch_kept stop-check
assert_success $? 'the cancelled run keeps the verifier branch'

printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
