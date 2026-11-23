#!/bin/sh
# Workflow run driver (hydra-fleet workflow-drive): scheduling decisions and
# recorded event order, terminal state reporting, ownership of a live run, the
# refusal to run without hydra-fleet, and runs that the removed shell driver
# of earlier releases started.

set -u

test_count=0
pass_count=0
fail_count=0
REPO="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
export HYDRA_TEST_ROOT="$REPO"
# shellcheck source=/dev/null
. "$REPO/tests/fixture-tools.sh"
test_root="$(test_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")" && cd "$test_dir" && pwd -P)"
repo="$test_root/repo"
export HYDRA_HOME="$test_root/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    test_stop_processes_naming "$test_root"
    (cd "$test_root/plan/repo" 2>/dev/null && "$HYDRA_BIN" kill plan-smoke --force >/dev/null 2>&1) || true
    (cd "$repo" 2>/dev/null && "$HYDRA_BIN" kill driver-head --force >/dev/null 2>&1) || true
    if [ -d "$repo/.git" ]; then
        git -C "$repo" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | while IFS= read -r _twd_worktree; do
            [ "$_twd_worktree" = "$repo" ] || git -C "$repo" worktree remove --force "$_twd_worktree" 2>/dev/null || true
        done
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

run_dir_for() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$1" -print | sed -n '1p'
}

# The recorded order of transitions without timestamps: "type step detail".
event_trace() {
    sed -n 's/.*"step_id":\([^,]*\),"type":"\([^"]*\)","detail":"\([^"]*\)".*/\2 \1 \3/p' "$1/events.jsonl" |
        sed 's/ "\([^"]*\)" / \1 /; s/ null / - /; s/ $//'
}

mkdir -p "$repo"
cd "$repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked.txt
git add tracked.txt
git commit -qm base
git branch -M main
"$HYDRA_BIN" init --no-agent --trust --write-shared-config >/dev/null
git add .hydra/config.yml
git commit -qm config
"$HYDRA_BIN" spawn driver-head --headless --no-agent >/dev/null
assert_success $? "fixture head spawns"

# A failure cascades through dependents in graph order: within one pass a
# cancellation is visible to every later step, so d is cancelled before e.
cat > "$test_root/cascade.yml" <<'EOF'
version: 1
id: driver-cascade
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: a
    kind: exec
    needs: []
    retry: 1
    idempotent: true
    args:
      head: driver-head
      argv: ["false"]
  - id: b
    kind: exec
    needs: [a]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: c
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: d
    kind: exec
    needs: [b]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: e
    kind: exec
    needs: [a]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: f
    kind: exec
    needs: [c]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
  - id: g
    kind: exec
    needs: [d, f]
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
EOF
"$HYDRA_BIN" workflow run "$test_root/cascade.yml" > "$test_root/cascade.out" 2> "$test_root/cascade.err"
assert_failure $? "a failed step fails the run"
cascade_run="$(sed -n '1p' "$test_root/cascade.out")"
cascade_dir="$(run_dir_for "$cascade_run")"
assert_equal failed "$(sed -n '1p' "$cascade_dir/state")" "the run records a terminal failure"
expected_trace="run.created -
run.running - driver=native
step.ready a
step.ready c
step.running a attempt=1
step.retrying a attempt=1 class=failure
step.running a attempt=2
step.failed a attempt=2 class=failure
step.cancelled b dependency_failed
step.cancelled d dependency_failed
step.cancelled e dependency_failed
step.cancelled g dependency_failed
step.running c attempt=1
step.succeeded c attempt=1 class=success
step.ready f
step.running f attempt=1
step.succeeded f attempt=1 class=success
run.failed -"
assert_equal "$expected_trace" "$(event_trace "$cascade_dir")" "transitions are decided and recorded in graph order"
cascade_status="$("$HYDRA_BIN" workflow status "$cascade_run")"
assert_equal "Workflow $cascade_run: failed (source: recorded, driver: native)
  a [exec]: failed (attempts=2)
  b [exec]: cancelled (attempts=0)
  c [exec]: succeeded (attempts=1)
  d [exec]: cancelled (attempts=0)
  e [exec]: cancelled (attempts=0)
  f [exec]: succeeded (attempts=1)
  g [exec]: cancelled (attempts=0)" "$cascade_status" "status reports every step in graph order"
assert_equal "{\"schema_version\":1,\"ok\":true,\"command\":\"workflow status\",\"data\":{\"run_id\":\"$cascade_run\",\"state\":\"failed\",\"source\":\"recorded\",\"driver\":\"native\",\"evidence_expired\":false,\"steps\":[{\"step_id\":\"a\",\"state\":\"failed\",\"attempts\":2},{\"step_id\":\"b\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"c\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"d\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"e\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"f\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"g\",\"state\":\"cancelled\",\"attempts\":0}]}}" \
    "$("$HYDRA_BIN" workflow status "$cascade_run" --json)" "status JSON reports every step in graph order"

# Deciding the terminal state of a failed run reads every step state without
# leaving a reader to die on a closed pipe and report it on stderr.
{
    printf 'version: 1\nid: driver-fanout-failure\nparallelism: 1\nresources:\n  disk_mb: 1\n  max_heads: 1\nsteps:\n'
    printf '  - id: root\n    kind: exec\n    needs: []\n    retry: 0\n    idempotent: true\n    args:\n      head: driver-head\n      argv: ["false"]\n'
    for fanout_step in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
        printf '  - id: leaf-%s\n    kind: exec\n    needs: [root]\n    retry: 0\n    idempotent: true\n    args:\n      head: driver-head\n      argv: ["true"]\n' "$fanout_step"
    done
} > "$test_root/fanout.yml"
"$HYDRA_BIN" workflow run "$test_root/fanout.yml" > "$test_root/fanout.out" 2> "$test_root/fanout.err"
assert_failure $? "a failed root fails the fan-out run"
assert_equal failed "$(sed -n '1p' "$(run_dir_for "$(sed -n '1p' "$test_root/fanout.out")")/state")" "the fan-out run records a terminal failure"
assert_equal "" "$(cat "$test_root/fanout.err")" "a failed run reports no state-reader errors"

# Sixteen exec steps on one head start together, and each one's admission
# request and release contend for the host's admission lock. A step waits for
# the lock instead of failing when it is busy for more than two seconds.
# Released records of earlier executions stay in the admission directory and
# lengthen every locked scan; 48 of them stand for a long plan run's history.
history_sequence=1
while [ "$history_sequence" -le 48 ]; do
    printf 'project_history released 1700000000 1700000060 %s - termination_confirmed\n' "$history_sequence" \
        > "$HYDRA_HOME/admission/history-$history_sequence.request"
    history_sequence=$((history_sequence + 1))
done
{
    printf 'version: 1\nid: driver-parallel-exec\nparallelism: 16\nresources:\n  disk_mb: 1\n  max_heads: 1\nsteps:\n'
    for parallel_step in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
        printf '  - id: exec-%s\n    kind: exec\n    needs: []\n    retry: 0\n    idempotent: true\n    args:\n      head: driver-head\n      timeout: 120\n      argv: ["true"]\n' "$parallel_step"
    done
} > "$test_root/parallel.yml"
"$HYDRA_BIN" workflow run "$test_root/parallel.yml" > "$test_root/parallel.out" 2> "$test_root/parallel.err"
assert_success $? "sixteen parallel exec steps on one head all succeed"
parallel_dir="$(run_dir_for "$(sed -n '1p' "$test_root/parallel.out")")"
assert_equal 16 "$(grep -c '"type":"step.succeeded"' "$parallel_dir/events.jsonl")" "every parallel exec step succeeds"
if grep -q state_unavailable "$parallel_dir"/steps/*/attempt-1/stdout "$parallel_dir"/steps/*/attempt-1/stderr; then parallel_status=1; else parallel_status=0; fi
assert_success "$parallel_status" "no parallel exec step reports unavailable admission storage"
"$HYDRA_BIN" admission status --json > "$test_root/admission.json"
grep -q '"reserved":0,"queued":0' "$test_root/admission.json"
assert_success $? "every parallel exec step releases its admission reservation"

# A plan run drives in a subshell of the CLI process. The run's recorded owner
# is that driver, so the run stays owned when the CLI process itself is gone:
# status does not call it stale and resume does not start a second driver.
plan_root="$test_root/plan"
mkdir -p "$plan_root/repo"
cp "$REPO/tests/fixtures/plan/repo/"* "$plan_root/repo/"
cp "$REPO/tests/fixtures/plan/policy.json" "$plan_root/policy.json"
cp "$REPO/tests/fixtures/plan/plan.json" "$plan_root/plan.json"
fixture_json headless-plan "$plan_root/plan.json"
{
    printf '#!/bin/sh\nset -eu\n: > "%s/compose-started"\ncount=0\n' "$plan_root"
    # shellcheck disable=SC2016 # The loop is written into the step script.
    printf 'while [ ! -f "%s/compose-release" ] && [ "$count" -lt 200 ]; do sleep 0.1; count=$((count + 1)); done\n' "$plan_root"
    tail -n +3 "$REPO/tests/fixtures/plan/repo/compose.sh"
} > "$plan_root/repo/compose.sh"
cd "$plan_root/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.com
git add . && git commit -qm fixture
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" workflow plan compile "$plan_root/plan.json" "$plan_root/policy.json" "$plan_root/compiled.json" > "$plan_root/compile.json"
assert_success $? "the plan fixture compiles"
plan_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$plan_root/compile.json")"
"$HYDRA_BIN" workflow plan run "$plan_root/compiled.json" --accept "$plan_digest" > "$plan_root/run.out" 2> "$plan_root/run.err" &
plan_cli=$!
WAIT_FOR_TIMEOUT=60 wait_for "the compose step to start" test -f "$plan_root/compose-started"
plan_run="$(sed -n '1p' "$plan_root/run.out")"
plan_dir="$(run_dir_for "$plan_run")"
plan_owner="$(sed -n '1p' "$plan_dir/owner-pid")"
kill -TERM "$plan_cli" 2>/dev/null
wait "$plan_cli" 2>/dev/null
if [ -n "$plan_owner" ] && kill -0 "$plan_owner" 2>/dev/null; then owner_status=0; else owner_status=1; fi
assert_success "$owner_status" "the recorded owner is the live driver, not the CLI process that started it"
plan_state_line="$("$HYDRA_BIN" workflow status "$plan_run" | sed -n '1p')"
assert_equal "Workflow $plan_run: running (source: recorded, driver: native)" "$plan_state_line" "status reports a live driver's run as running"
"$HYDRA_BIN" workflow resume "$plan_run" > "$plan_root/resume.out" 2>&1 &
plan_resume=$!
resume_polls=0
while kill -0 "$plan_resume" 2>/dev/null && [ "$resume_polls" -lt 100 ]; do sleep 0.1; resume_polls=$((resume_polls + 1)); done
if kill -0 "$plan_resume" 2>/dev/null; then resume_status=0; else wait "$plan_resume"; resume_status=$?; fi
assert_failure "$resume_status" "resume refuses a run whose driver is alive"
grep -q 'workflow owner is still active' "$plan_root/resume.out"
assert_success $? "resume names the live owner as the reason"
: > "$plan_root/compose-release"
plan_succeeded() { [ "$(sed -n '1p' "$plan_dir/state")" = succeeded ]; }
WAIT_FOR_TIMEOUT=120 wait_for "the plan run to finish" plan_succeeded
assert_success $? "the original driver completes the run"
wait "$plan_resume" 2>/dev/null || true
# The driver publishes the state before it appends the terminal event.
plan_terminal_event() { grep -q '"type":"run.succeeded"' "$plan_dir/events.jsonl"; }
WAIT_FOR_TIMEOUT=30 wait_for "the run's terminal event" plan_terminal_event
assert_equal 1 "$(grep -c '"type":"run.running"' "$plan_dir/events.jsonl")" "one driver ran the plan"
assert_equal 0 "$(grep -c '"type":"run.recovered"' "$plan_dir/events.jsonl")" "the live run was never recovered by another driver"
assert_equal 1 "$(grep -c '"type":"run.succeeded"' "$plan_dir/events.jsonl")" "the run records one terminal event"
cd "$repo" || exit 1

# Workflows need hydra-fleet: the native driver is the only run driver. Without
# a hydra-fleet that implements this run layout, or under HYDRA_NATIVE_DRIVER=0,
# `workflow run` exits non-zero with one error before any run record exists.
# Every drive records its driver in its run.running event.
cat > "$test_root/choice.yml" <<'EOF'
version: 1
id: driver-choice
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: only
    kind: exec
    needs: []
    retry: 0
    idempotent: true
    args:
      head: driver-head
      argv: ["true"]
EOF
# Two helpers that must not be asked to drive: a hydra-fleet from before the
# run driver, and one for another run layout. Each logs its workflow-drive
# calls; everything else is the real helper.
cat > "$test_root/old-fleet" <<EOF
#!/bin/sh
if [ "\${1:-}" = workflow-drive ]; then
    printf '%s\n' "\$*" >> "$test_root/old-fleet.calls"
    echo 'unknown command' >&2
    exit 2
fi
exec "$HYDRA_FLEET_BIN" "\$@"
EOF
cat > "$test_root/other-layout-fleet" <<EOF
#!/bin/sh
if [ "\${1:-}" = workflow-drive ]; then
    printf '%s\n' "\$*" >> "$test_root/other-layout-fleet.calls"
    if [ "\${2:-}" = --capability ]; then echo 'workflow-drive 2'; exit 0; fi
    exit 2
fi
exec "$HYDRA_FLEET_BIN" "\$@"
EOF
chmod +x "$test_root/old-fleet" "$test_root/other-layout-fleet"
# The driver of each drive of a run, in order, as its run.running events name them.
run_drivers() {
    sed -n 's/.*"type":"run.running","detail":"driver=\([a-z]*\)".*/\1/p' "$1/events.jsonl" | tr '\n' ' '
}
# The run directories of this project, and how many events the runs hold.
run_count() {
    find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/run_*" -prune -print | wc -l | tr -d ' '
}
# Usage: choice_run <label> [<variable>=<value>...]
# One run of the workflow, with HYDRA_NATIVE_DRIVER unset and the assignments
# exported; it must succeed under the native driver.
choice_run() {
    choice_label="$1"
    shift
    (
        unset HYDRA_NATIVE_DRIVER
        for choice_assignment in "$@"; do export "${choice_assignment?}"; done
        exec "$HYDRA_BIN" workflow run "$test_root/choice.yml"
    ) > "$test_root/choice.out" 2> "$test_root/choice.err"
    assert_success $? "$choice_label: the run succeeds"
    choice_dir="$(run_dir_for "$(sed -n '1p' "$test_root/choice.out")")"
    assert_equal "native " "$(run_drivers "$choice_dir")" "$choice_label: the native driver drives the run"
    assert_equal succeeded "$(sed -n '1p' "$choice_dir/state")" "$choice_label: the run records success"
}
# Usage: refused_run <label> <expected message fragment> [<variable>=<value>...]
# One run that must be refused: non-zero, no stdout, one error on stderr that
# names the helper and how to get it, and no run record created.
refused_run() {
    refused_label="$1" refused_fragment="$2"
    shift 2
    refused_before="$(run_count)"
    (
        unset HYDRA_NATIVE_DRIVER
        for refused_assignment in "$@"; do export "${refused_assignment?}"; done
        exec "$HYDRA_BIN" workflow run "$test_root/choice.yml"
    ) > "$test_root/refused.out" 2> "$test_root/refused.err"
    assert_failure $? "$refused_label: the run is refused"
    assert_equal "" "$(cat "$test_root/refused.out")" "$refused_label: no run ID is printed"
    assert_equal "$refused_before" "$(run_count)" "$refused_label: no run record is created"
    assert_equal 1 "$(grep -c '^Error:' "$test_root/refused.err")" "$refused_label: one error"
    assert_equal 1 "$(grep -c '^Next:' "$test_root/refused.err")" "$refused_label: one next step"
    grep -q 'workflows need the hydra-fleet helper' "$test_root/refused.err"
    assert_success $? "$refused_label: the error says workflows need hydra-fleet"
    grep -q "$refused_fragment" "$test_root/refused.err"
    assert_success $? "$refused_label: the error says why"
    grep 'make build-fleet' "$test_root/refused.err" | grep -q 'JSON-C' && grep 'make build-fleet' "$test_root/refused.err" | grep -q 'pkg-config'
    assert_success $? "$refused_label: the next step names make build-fleet and its build dependencies"
    grep -q 'libexec/hydra/hydra-fleet' "$test_root/refused.err"
    assert_success $? "$refused_label: the next step names the install path"
}
choice_run "no setting"
assert_equal "Workflow ${choice_dir##*/}: succeeded (source: recorded, driver: native)" \
    "$("$HYDRA_BIN" workflow status "${choice_dir##*/}" | sed -n '1p')" "status names the native driver"
case "$("$HYDRA_BIN" workflow status "${choice_dir##*/}" --json)" in
    *'"source":"recorded","driver":"native","evidence_expired":false'*) choice_status=0 ;;
    *) choice_status=1 ;;
esac
assert_success "$choice_status" "status JSON names the driver"
choice_run "HYDRA_NATIVE_DRIVER=1, the opt-in of earlier releases" HYDRA_NATIVE_DRIVER=1
choice_run "an empty HYDRA_NATIVE_DRIVER" HYDRA_NATIVE_DRIVER=
refused_run "HYDRA_NATIVE_DRIVER=0" 'shell workflow driver was removed' HYDRA_NATIVE_DRIVER=0
refused_run "HYDRA_NATIVE_DRIVER=0 with a capable helper still refuses" 'HYDRA_NATIVE_DRIVER=0 no longer selects it' HYDRA_NATIVE_DRIVER=0 "HYDRA_FLEET_BIN=$HYDRA_FLEET_BIN"
refused_run "no hydra-fleet" 'was not found' "HYDRA_FLEET_BIN=$test_root/no-hydra-fleet"
if grep -q 'HYDRA_NATIVE_DRIVER' "$test_root/refused.err"; then blamed_switch=1; else blamed_switch=0; fi
assert_success "$blamed_switch" "no hydra-fleet: the error does not blame HYDRA_NATIVE_DRIVER"
refused_run "a hydra-fleet without the run driver" 'too old or for another run layout' "HYDRA_FLEET_BIN=$test_root/old-fleet"
assert_equal 'workflow-drive --capability' "$(cat "$test_root/old-fleet.calls")" "a helper that fails the probe is never asked to drive"
refused_run "a hydra-fleet for another run layout" 'too old or for another run layout' "HYDRA_FLEET_BIN=$test_root/other-layout-fleet"
assert_equal 'workflow-drive --capability' "$(cat "$test_root/other-layout-fleet.calls")" "a helper for another run layout is never asked to drive"

# Runs that an earlier release's shell driver started. Their records say
# driver=shell in a run.running event. status reads them as ever; resume and
# cancel hand them to hydra-fleet when there is one, and without one leave the
# run exactly as it was, with the same refusal as workflow run.
cat > "$test_root/handover.yml" <<'EOF'
version: 1
id: driver-handover
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: before
    kind: exec
    needs: []
    retry: 0
    idempotent: false
    args:
      head: driver-head
      argv: ["true"]
  - id: approval
    kind: approval-wait
    needs: [before]
    idempotent: false
    args:
      head: driver-head
      name: review
      message: Review before continuing
  - id: after
    kind: exec
    needs: [approval]
    retry: 0
    idempotent: false
    args:
      head: driver-head
      argv: ["true"]
EOF
# Usage: shell_run
# Leaves a run waiting for approval and rewrites its events as the shell driver
# wrote them; sets shell_run_id and shell_run_dir.
shell_run() {
    "$HYDRA_BIN" workflow run "$test_root/handover.yml" > "$test_root/handover.out" 2> "$test_root/handover.err"
    assert_equal 3 "$?" "the first drive leaves the run waiting for approval"
    shell_run_id="$(sed -n '1p' "$test_root/handover.out")"
    shell_run_dir="$(run_dir_for "$shell_run_id")"
    sed 's/"detail":"driver=native"/"detail":"driver=shell"/' "$shell_run_dir/events.jsonl" > "$shell_run_dir/events.rewritten"
    mv "$shell_run_dir/events.rewritten" "$shell_run_dir/events.jsonl"
    assert_equal 'shell ' "$(run_drivers "$shell_run_dir")" "the record says the shell driver drove the run"
}
events_of() { wc -l < "$1/events.jsonl" | tr -d ' '; }

shell_run
assert_equal "Workflow $shell_run_id: waiting-approval (source: recorded, driver: shell)" \
    "$("$HYDRA_BIN" workflow status "$shell_run_id" | sed -n '1p')" "status reads a shell-driven run"
"$HYDRA_BIN" workflow status "$shell_run_id" | grep -q 'removed shell driver; hydra workflow resume'
assert_success $? "status of an unfinished shell-driven run says how to continue it"
case "$("$HYDRA_BIN" workflow status "$shell_run_id" --json)" in
    *'"state":"waiting-approval","source":"recorded","driver":"shell"'*) shell_json=0 ;;
    *) shell_json=1 ;;
esac
assert_success "$shell_json" "status JSON of a shell-driven run names the driver"
"$HYDRA_BIN" workflow decide "$shell_run_id" "$(sed -n '1p' "$shell_run_dir/steps/approval/request-id")" approve --by driver-test >/dev/null
assert_success $? "the request is approved"
shell_events="$(events_of "$shell_run_dir")"
HYDRA_FLEET_BIN="$test_root/no-hydra-fleet" "$HYDRA_BIN" workflow resume "$shell_run_id" > "$test_root/handover-resume.out" 2> "$test_root/handover-resume.err"
assert_failure $? "resume of a shell-driven run without hydra-fleet is refused"
grep -q 'workflows need the hydra-fleet helper' "$test_root/handover-resume.err"
assert_success $? "resume names hydra-fleet as the reason"
assert_equal waiting-approval "$(sed -n '1p' "$shell_run_dir/state")" "a refused resume leaves the run state alone"
assert_equal "$shell_events" "$(events_of "$shell_run_dir")" "a refused resume appends no event"
HYDRA_FLEET_BIN="$test_root/no-hydra-fleet" "$HYDRA_BIN" workflow cancel "$shell_run_id" > "$test_root/handover-cancel.out" 2> "$test_root/handover-cancel.err"
assert_failure $? "cancel of a shell-driven run without hydra-fleet is refused"
grep -q 'workflows need the hydra-fleet helper' "$test_root/handover-cancel.err"
assert_success $? "cancel names hydra-fleet as the reason"
if [ -e "$shell_run_dir/cancel-requested" ]; then cancel_marked=1; else cancel_marked=0; fi
assert_success "$cancel_marked" "a refused cancel records no cancellation request"
assert_equal "$shell_events" "$(events_of "$shell_run_dir")" "a refused cancel appends no event"
"$HYDRA_BIN" workflow resume "$shell_run_id" > "$test_root/handover-resume.out" 2>&1
assert_success $? "hydra-fleet finishes a run the shell driver started"
assert_equal succeeded "$(sed -n '1p' "$shell_run_dir/state")" "the resumed run records success"
assert_equal 'shell native ' "$(run_drivers "$shell_run_dir")" "each drive records its driver"
sed -n 's/^{"schema_version":1,"sequence":\([0-9]*\),.*/\1/p' "$shell_run_dir/events.jsonl" | awk '$1 != NR { bad = 1 } END { exit bad || NR == 0 }'
assert_success $? "the events of both drivers are numbered 1, 2, ..."
assert_equal "Workflow $shell_run_id: succeeded (source: recorded, driver: native)" \
    "$("$HYDRA_BIN" workflow status "$shell_run_id" | sed -n '1p')" "status names the latest driver"
if "$HYDRA_BIN" workflow status "$shell_run_id" | grep -q 'removed shell driver'; then finished_hint=1; else finished_hint=0; fi
assert_success "$finished_hint" "status of a finished run has no shell driver note"

# Cancelling a shell-driven run waiting for approval: with hydra-fleet it is
# cancelled by the native driver.
shell_run
HYDRA_FLEET_BIN="$test_root/old-fleet" "$HYDRA_BIN" workflow cancel "$shell_run_id" > /dev/null 2>&1
assert_failure $? "cancel with a helper that cannot drive is refused"
assert_equal waiting-approval "$(sed -n '1p' "$shell_run_dir/state")" "the refused cancel leaves the run waiting"
"$HYDRA_BIN" workflow cancel "$shell_run_id" > "$test_root/handover-cancel.out" 2>&1
assert_success $? "hydra-fleet cancels a run the shell driver started"
assert_equal cancelled "$(sed -n '1p' "$shell_run_dir/state")" "the cancelled run records its state"

# A shell driver that died (or was killed by the upgrade) leaves a running run
# with a dead owner: status calls it stale, and hydra-fleet recovers it.
shell_run
dead_pid="$(test_dead_pid)"
printf '%s\n' "$dead_pid" > "$shell_run_dir/owner-pid"
printf '%s\n' running > "$shell_run_dir/state"
printf '%s\n' 1 > "$shell_run_dir/heartbeat-at"
assert_equal "Workflow $shell_run_id: stale (source: recorded, driver: shell)" \
    "$("$HYDRA_BIN" workflow status "$shell_run_id" | sed -n '1p')" "status calls a run whose shell driver died stale"
HYDRA_FLEET_BIN="$test_root/no-hydra-fleet" "$HYDRA_BIN" workflow resume "$shell_run_id" > /dev/null 2>&1
assert_failure $? "resume of a stale run without hydra-fleet is refused"
assert_equal running "$(sed -n '1p' "$shell_run_dir/state")" "the refused resume leaves the stale run as it was"
"$HYDRA_BIN" workflow resume "$shell_run_id" > "$test_root/handover-resume.out" 2>&1
assert_equal 3 "$?" "hydra-fleet recovers the stale run, which waits for its approval again"
assert_equal waiting-approval "$(sed -n '1p' "$shell_run_dir/state")" "the recovered run is waiting for approval"
assert_equal 'shell native ' "$(run_drivers "$shell_run_dir")" "the recovery records the native driver"
"$HYDRA_BIN" workflow cancel "$shell_run_id" > /dev/null 2>&1
assert_success $? "the recovered run can be cancelled"

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
