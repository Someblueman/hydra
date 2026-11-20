#!/bin/sh
# Workflow run driver: scheduling decisions and recorded event order, terminal
# state reporting, ownership of a live run, and which driver drives a run.
# The cases before the last section run with the driver the caller selects:
# the native one by default, the shell one under make test-shell-driver.

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

# The driver this case's runs get from the caller's environment.
default_driver=native
[ "${HYDRA_NATIVE_DRIVER:-}" != 0 ] || default_driver=shell

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
run.running - driver=$default_driver
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
assert_equal "Workflow $cascade_run: failed (source: recorded, driver: $default_driver)
  a [exec]: failed (attempts=2)
  b [exec]: cancelled (attempts=0)
  c [exec]: succeeded (attempts=1)
  d [exec]: cancelled (attempts=0)
  e [exec]: cancelled (attempts=0)
  f [exec]: succeeded (attempts=1)
  g [exec]: cancelled (attempts=0)" "$cascade_status" "status reports every step in graph order"
assert_equal "{\"schema_version\":1,\"ok\":true,\"command\":\"workflow status\",\"data\":{\"run_id\":\"$cascade_run\",\"state\":\"failed\",\"source\":\"recorded\",\"driver\":\"$default_driver\",\"evidence_expired\":false,\"steps\":[{\"step_id\":\"a\",\"state\":\"failed\",\"attempts\":2},{\"step_id\":\"b\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"c\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"d\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"e\",\"state\":\"cancelled\",\"attempts\":0},{\"step_id\":\"f\",\"state\":\"succeeded\",\"attempts\":1},{\"step_id\":\"g\",\"state\":\"cancelled\",\"attempts\":0}]}}" \
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
assert_equal "Workflow $plan_run: running (source: recorded, driver: $default_driver)" "$plan_state_line" "status reports a live driver's run as running"
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

# Which driver drives a run. The native driver is the default wherever a
# hydra-fleet that implements this run layout is found; the shell driver is
# the fallback without one and the explicit HYDRA_NATIVE_DRIVER=0. Every drive
# records its driver in its run.running event.
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
# Usage: choice_run <label> <expected-driver> [<variable>=<value>...]
# One run of the workflow with HYDRA_NATIVE_DRIVER unset and the assignments
# exported; sets choice_dir to its run directory.
choice_run() {
    choice_label="$1" choice_expected="$2"
    shift 2
    (
        unset HYDRA_NATIVE_DRIVER
        for choice_assignment in "$@"; do export "${choice_assignment?}"; done
        exec "$HYDRA_BIN" workflow run "$test_root/choice.yml"
    ) > "$test_root/choice.out" 2> "$test_root/choice.err"
    assert_success $? "$choice_label: the run succeeds"
    choice_dir="$(run_dir_for "$(sed -n '1p' "$test_root/choice.out")")"
    assert_equal "$choice_expected" "$(run_drivers "$choice_dir")" "$choice_label: the run records the driver that drove it"
    assert_equal succeeded "$(sed -n '1p' "$choice_dir/state")" "$choice_label: the run records success"
}
choice_run "no setting" 'native '
assert_equal "Workflow ${choice_dir##*/}: succeeded (source: recorded, driver: native)" \
    "$("$HYDRA_BIN" workflow status "${choice_dir##*/}" | sed -n '1p')" "status names the native driver"
choice_run "HYDRA_NATIVE_DRIVER=1, the opt-in of earlier releases" 'native ' HYDRA_NATIVE_DRIVER=1
choice_run "an empty HYDRA_NATIVE_DRIVER" 'native ' HYDRA_NATIVE_DRIVER=
choice_run "HYDRA_NATIVE_DRIVER=0" 'shell ' HYDRA_NATIVE_DRIVER=0
assert_equal "Workflow ${choice_dir##*/}: succeeded (source: recorded, driver: shell)" \
    "$("$HYDRA_BIN" workflow status "${choice_dir##*/}" | sed -n '1p')" "status names the shell driver"
case "$("$HYDRA_BIN" workflow status "${choice_dir##*/}" --json)" in
    *'"source":"recorded","driver":"shell","evidence_expired":false'*) choice_status=0 ;;
    *) choice_status=1 ;;
esac
assert_success "$choice_status" "status JSON names the driver"
choice_run "a hydra-fleet without the run driver" 'shell ' "HYDRA_FLEET_BIN=$test_root/old-fleet"
assert_equal 'workflow-drive --capability' "$(cat "$test_root/old-fleet.calls")" "a helper that fails the probe is never asked to drive"
choice_run "a hydra-fleet for another run layout" 'shell ' "HYDRA_FLEET_BIN=$test_root/other-layout-fleet"
assert_equal 'workflow-drive --capability' "$(cat "$test_root/other-layout-fleet.calls")" "a helper for another run layout is never asked to drive"
# Without hydra-fleet a plain YAML workflow still runs: the fallback's purpose.
choice_run "no hydra-fleet" 'shell ' "HYDRA_FLEET_BIN=$test_root/no-hydra-fleet"

# A run that one driver left waiting for approval is resumed by the other, as
# after hydra-fleet is installed or removed between two drives: both write the
# same run records, and the events keep one numbering.
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
# Usage: handover <first HYDRA_NATIVE_DRIVER> <second> <expected drivers>
handover() {
    HYDRA_NATIVE_DRIVER="$1" "$HYDRA_BIN" workflow run "$test_root/handover.yml" > "$test_root/handover.out" 2> "$test_root/handover.err"
    assert_equal 3 "$?" "$3: the first driver leaves the run waiting for approval"
    handover_run="$(sed -n '1p' "$test_root/handover.out")"
    handover_dir="$(run_dir_for "$handover_run")"
    "$HYDRA_BIN" workflow decide "$handover_run" "$(sed -n '1p' "$handover_dir/steps/approval/request-id")" approve --by driver-test >/dev/null
    assert_success $? "$3: the request is approved"
    HYDRA_NATIVE_DRIVER="$2" "$HYDRA_BIN" workflow resume "$handover_run" > "$test_root/handover-resume.out" 2>&1
    assert_success $? "$3: the second driver finishes the run"
    assert_equal succeeded "$(sed -n '1p' "$handover_dir/state")" "$3: the run records success"
    assert_equal "$3" "$(run_drivers "$handover_dir")" "$3: each drive records its driver"
    sed -n 's/^{"schema_version":1,"sequence":\([0-9]*\),.*/\1/p' "$handover_dir/events.jsonl" | awk '$1 != NR { bad = 1 } END { exit bad || NR == 0 }'
    assert_success $? "$3: the events of both drivers are numbered 1, 2, ..."
}
handover 0 1 'shell native '
handover 1 0 'native shell '

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
