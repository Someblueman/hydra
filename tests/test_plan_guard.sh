#!/bin/sh
# The check before each step guards the plan as approved and the inputs the run
# copied, not the live checkout: an untracked file or a new commit in the
# source while a plan runs does not stop it, while a changed compiled plan or
# copied input does.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    for case_name in untracked compiled input; do
        (cd "$root/$case_name" 2>/dev/null && "$HYDRA_BIN" kill "guard-$case_name" --force) >/dev/null 2>&1
    done
    test_stop_processes_naming "$root" >/dev/null 2>&1 || :
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Usage: start_case <name>
# A repository whose compose step announces that it started and then waits for
# a release file, so the test can change things while the run is in progress.
start_case() {
    _dir="$root/$1"
    mkdir -p "$_dir"
    cp "$REPO/tests/fixtures/plan/repo/"* "$_dir/"
    {
        printf '#!/bin/sh\nset -eu\n: > %s\n' "$root/$1.started"
        printf 'while [ ! -f %s ]; do sleep 1; done\n' "$root/$1.release"
        printf "printf 'Delivered report\\\\n' > \"\$HYDRA_WORKFLOW_OUTPUTS_DIR/report.txt\"\n"
    } > "$_dir/compose.sh"
    (cd "$_dir" && git init -q && git config user.name Test && git config user.email test@example.invalid &&
        git add . && git -c commit.gpgSign=false commit -qm base && "$HYDRA_BIN" init --no-agent --trust >/dev/null) || return 1
    sed -e "s/\"branch\": \"plan-smoke\"/\"branch\": \"guard-$1\", \"terminal_mode\": \"headless\"/" \
        -e "s/\"head\": \"plan-smoke\"/\"head\": \"guard-$1\"/g" -e 's/"timeout_seconds": 180/"timeout_seconds": 180/' \
        "$REPO/tests/fixtures/plan/plan.json" > "$root/$1.plan.json"
    (cd "$_dir" && "$HYDRA_BIN" workflow plan compile "$root/$1.plan.json" "$REPO/tests/fixtures/plan/policy.json" "$root/$1.compiled.json") > "$root/$1.compile.out" || return 1
    _digest="$(sed -n 's/.*"sha256":"\([0-9a-f]\{64\}\)".*/\1/p' "$root/$1.compile.out")"
    (cd "$_dir" && exec "$HYDRA_BIN" workflow plan run "$root/$1.compiled.json" --accept "$_digest") > "$root/$1.run.out" 2> "$root/$1.run.err" &
    case_pid=$!
    _tries=0
    until [ -f "$root/$1.started" ] || [ "$_tries" -ge 60 ]; do _tries=$((_tries + 1)); sleep 1; done
    run_dir="$(find "$HYDRA_HOME/state/v2/projects" -path "*/workflows/runs/*" -name compiled.json -newer "$root/$1.compile.out" 2>/dev/null |
        while read -r _path; do _run="$(dirname "$_path")"; [ "$(sed -n '1p' "$_run/workflow-id")" = plan-fixture ] && grep -q "guard-$1" "$_run/graph.tsv" && printf '%s\n' "$_run"; done | head -n 1)"
    [ -f "$root/$1.started" ] && [ -n "$run_dir" ]
}
finish_case() {
    : > "$root/$1.release"
    wait "$case_pid"
    case_status=$?
}

start_case untracked
assert_success $? 'the first plan is running its compose step'
printf 'scratch notes\n' > "$root/untracked/notes.txt"
printf 'more\n' >> "$root/untracked/expected.txt"
(cd "$root/untracked" && git -c commit.gpgSign=false commit -qam 'moved on')
finish_case untracked
assert_equal 0 "$case_status" 'an untracked file and a new commit in the source do not stop a running plan'
assert_equal succeeded "$(sed -n '1p' "$run_dir/state")" 'the run succeeds'
grep -q '^source_moved' "$run_dir/plan-warnings" 2>/dev/null
assert_success $? 'the moved source commit is recorded as a warning'
(cd "$root/untracked" && "$HYDRA_BIN" workflow plan result "$(basename "$run_dir")") > "$root/untracked.result" 2>&1
grep -q '"verdict":"pass"' "$root/untracked.result"
assert_success $? 'its result verifies against what the run copied at admission'

start_case compiled
assert_success $? 'the second plan is running its compose step'
sed 's/Deliver a report and verify its contents/Deliver something else/' "$run_dir/compiled.json" > "$root/changed.json"
cat "$root/changed.json" > "$run_dir/compiled.json"
finish_case compiled
assert_failure "$case_status" 'a changed compiled plan stops the run'
grep -q '"step_id":"verify","type":"step.recovery_required","detail":"stale_plan"' "$run_dir/events.jsonl"
assert_success $? 'the next step is refused as a stale plan'

start_case input
assert_success $? 'the third plan is running its compose step'
chmod u+w "$run_dir/artifacts/expected" 2>/dev/null
printf 'tampered\n' > "$run_dir/artifacts/expected"
finish_case input
assert_failure "$case_status" 'a changed copied input stops the run'
grep -q '"step_id":"verify","type":"step.recovery_required","detail":"stale_plan"' "$run_dir/events.jsonl"
assert_success $? 'the step after the change is refused'

cd "$root" || exit 1
printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
