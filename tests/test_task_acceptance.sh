#!/bin/sh
# Public CLI over a controlled SSH boundary and the real receiver, state, and Git.
set -eu
# A common Ubuntu login umask must not create shared task-state ancestors.
umask 002
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
cleanup() {
    [ -z "${owned_group:-}" ] || kill -KILL "-$owned_group" 2>/dev/null || :
    [ -z "${owned_owner:-}" ] || kill -KILL "$owned_owner" 2>/dev/null || :
    for workspace in "$fixture"/host/fleet/tasks/task_*/workspace; do
        [ -f "$workspace/.git/hydra/project-id" ] || continue
        (cd "$workspace" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
    done
    # Public teardown may refuse the deliberately dirty fixture worktrees. Remove
    # only their remaining terminal instances before deleting disposable files.
    for cleanup_head in "$fixture"/host/state/v2/projects/*/heads/*; do
        [ -f "$cleanup_head/session" ] && [ -f "$cleanup_head/current-instance" ] || continue
        cleanup_session="$(cat "$cleanup_head/session")"
        cleanup_instance="$(cat "$cleanup_head/current-instance")"
        cleanup_id="$(tmux display-message -p -t "=$cleanup_session" '#{session_id}' 2>/dev/null)" || continue
        [ "$(tmux show-environment -t "$cleanup_id" HYDRA_INSTANCE_ID 2>/dev/null)" = "HYDRA_INSTANCE_ID=$cleanup_instance" ] || continue
        tmux kill-session -t "$cleanup_id" 2>/dev/null || :
    done
    rm -rf "$fixture"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
HYDRA_HOME="$fixture/client"
HYDRA_TEST_TRANSPORT="$fixture/transport"
export HYDRA_FLEET_BIN HYDRA_HOME HYDRA_TEST_TRANSPORT
HYDRA_TEST_GIT="$(command -v git)"
export HYDRA_TEST_GIT
mkdir -p "$fixture/source" "$fixture/receiver" "$fixture/transport" "$fixture/bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -f "$HYDRA_TEST_TRANSPORT/offline" ]; then exit 255; fi
request="$(mktemp "$HYDRA_TEST_TRANSPORT/request.XXXXXX")"
trap 'rm -f "$request"' 0
cat > "$request"
if [ -f "$HYDRA_TEST_TRANSPORT/lose-ack" ] && grep -Eq '"operation":"(submit|start|cancel)"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_TRANSPORT/lost-response"
    rm "$HYDRA_TEST_TRANSPORT/lose-ack"
    exit 255
fi
exec /bin/sh -c "$2" < "$request"
SSH
chmod +x "$fixture/bin/ssh"
cat > "$fixture/bin/git" <<'GIT'
#!/bin/sh
if [ -f "$HYDRA_TEST_TRANSPORT/slow-clone" ]; then
    case " $* " in *' clone '*) sleep 2 ;; esac
fi
exec "$HYDRA_TEST_GIT" "$@"
GIT
chmod +x "$fixture/bin/git"
PATH="$fixture/bin:$PATH"
export PATH
for repository in source receiver; do
    git -C "$fixture/$repository" init -q
    git -C "$fixture/$repository" config user.name 'Task test'
    git -C "$fixture/$repository" config user.email task@example.invalid
    git -C "$fixture/$repository" -c commit.gpgSign=false commit --allow-empty -qm initial
done
(cd "$fixture/receiver" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" init --no-agent --json) > "$fixture/initialized"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$fixture/host" >/dev/null
commit="$(git -C "$fixture/source" rev-parse HEAD)"
cat > "$fixture/spec" <<EOF
{"schema_version":1,"host":"build","project":"$fixture/receiver",
"source":{"commit":"$commit"},"work":{"kind":"exec","argv":["true"]},
"inputs":[],"outputs":[],"capabilities":["exec"],"completion":"command-exit",
"limits":{"transport_seconds":30,"queue_seconds":60,"startup_seconds":60,
"execution_seconds":120,"cancellation_seconds":10,"log_bytes":4096,"artifact_bytes":4096}}
EOF
task() { "$root/bin/hydra" fleet task "$@"; }
if printf '{"protocol":1,"action":"handshake"}\000trailing' | "$HYDRA_FLEET_BIN" fleet serve > "$fixture/error"; then exit 1; fi
grep -q '"code":"invalid_request"' "$fixture/error"
task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/package" > "$fixture/preview"
# Six simultaneous clients must converge on one immutable acceptance.
pids=""
for n in 1 2 3 4 5 6; do
    task submit build --input "$fixture/package" --key same-key > "$fixture/receipt-$n" &
    pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/receipt-1")"
[ -n "$id" ]
for n in 2 3 4 5 6; do cmp "$fixture/receipt-1" "$fixture/receipt-$n"; done
task status build --id "$id" > "$fixture/status"
grep -q '"state":"accepted"' "$fixture/status"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 1 ]
cmp "$fixture/package" "$fixture/host/fleet/tasks/$id/package.json"
cp "$fixture/host/fleet/tasks/$id/acceptance.json" "$fixture/original-acceptance"
# Lost acknowledgment is deliberately after the receiver has finished acceptance.
: > "$fixture/transport/lose-ack"
if task submit build --input "$fixture/package" --key lost-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
lost_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/transport/lost-response")"
[ -n "$lost_id" ]
task submit build --input "$fixture/package" --key lost-key > "$fixture/reconciled"
grep -q "$lost_id" "$fixture/reconciled"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 2 ]
# A new valid payload cannot take over an existing key.
sed 's/"true"/"false"/' "$fixture/spec" > "$fixture/changed-spec"
task prepare --source "$fixture/source" --spec "$fixture/changed-spec" --output "$fixture/changed-package" >/dev/null
if task submit build --input "$fixture/changed-package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"submission_conflict"' "$fixture/error"
cmp "$fixture/original-acceptance" "$fixture/host/fleet/tasks/$id/acceptance.json"
# Receipt lookup survives loss of the original mapping, including a lost first ack.
mv "$fixture/receiver" "$fixture/moved-receiver"
task submit build --input "$fixture/package" --key same-key > "$fixture/repeated"
grep -q "$id" "$fixture/repeated"
mv "$fixture/moved-receiver" "$fixture/receiver"
mv "$fixture/receiver/.git" "$fixture/receiver/.git-saved"
if task submit build --input "$fixture/package" --key stale-mapping > "$fixture/error"; then exit 1; fi
grep -q '"code":"unmapped_project"' "$fixture/error"
mv "$fixture/receiver/.git-saved" "$fixture/receiver/.git"
# An outage gives no new authority to replay work, and status never invents success.
: > "$fixture/transport/offline"
if task status build --id "$id" > "$fixture/error"; then exit 1; fi
grep -q '"code":"offline"' "$fixture/error"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
rm "$fixture/transport/offline"
task status build --id "$id" >/dev/null
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" -eq 2 ]
# Reject requirements and placements before acceptance.
sed 's/\["exec"\]/["exact-provider-resume"]/' "$fixture/spec" > "$fixture/bad-spec"
task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/bad-package" >/dev/null
if task submit build --input "$fixture/bad-package" --key missing-cap > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
sed "s@$fixture/receiver@$fixture/source@" "$fixture/spec" > "$fixture/unmapped-spec"
task prepare --source "$fixture/source" --spec "$fixture/unmapped-spec" --output "$fixture/unmapped-package" >/dev/null
if task submit build --input "$fixture/unmapped-package" --key unmapped > "$fixture/error"; then exit 1; fi
grep -q '"code":"unmapped_project"' "$fixture/error"
if task submit build --input "$fixture/unmapped-package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"submission_conflict"' "$fixture/error"
# A task-store symlink cannot redirect reads or acceptance publication.
chmod g+w "$fixture/host"
if task status build --id "$id" > "$fixture/error"; then exit 1; fi
grep -q '"code":"io_failed"' "$fixture/error"
[ -n "$(find "$fixture/host" -prune -perm -0020)" ]
chmod g-w "$fixture/host"
mv "$fixture/host/fleet/tasks" "$fixture/host/fleet/saved-tasks"
ln -s "$fixture/host/fleet/saved-tasks" "$fixture/host/fleet/tasks"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"io_failed"' "$fixture/error"
rm "$fixture/host/fleet/tasks"
mv "$fixture/host/fleet/saved-tasks" "$fixture/host/fleet/tasks"
# A detached execution outlives the start response and has one real exec attempt.
sed "s@\[\"true\"\]@[\"sh\",\"-c\",\"printf once >> $fixture/executions; sleep 2\"]@" "$fixture/spec" > "$fixture/execution-spec"
task prepare --source "$fixture/source" --spec "$fixture/execution-spec" --output "$fixture/execution-package" > "$fixture/execution-preview"
task submit build --input "$fixture/execution-package" --key execution > "$fixture/execution-receipt"
execution_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/execution-receipt")"
digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/execution-preview")"
if task start build --id "$execution_id" --trust-spec wrong > "$fixture/error"; then exit 1; fi
grep -q '"code":"trust_required"' "$fixture/error"
: > "$fixture/transport/lose-ack"
if task start build --id "$execution_id" --trust-spec "$digest" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
attempt=0
while [ "$attempt" -lt 100 ]; do
    task status build --id "$execution_id" > "$fixture/execution-status"
    if grep -q '"state":"succeeded"' "$fixture/execution-status"; then break; fi
    if grep -Eq '"state":"(failed|outcome_unknown)"' "$fixture/execution-status"; then cat "$fixture/execution-status"; exit 1; fi
    sleep 0.1; attempt=$((attempt + 1))
done
grep -q '"state":"succeeded"' "$fixture/execution-status"
grep -q '"run_id":"run_' "$fixture/execution-status"
[ "$(cat "$fixture/executions")" = once ]
task start build --id "$execution_id" --trust-spec "$digest" >/dev/null
[ "$(cat "$fixture/executions")" = once ]
[ -f "$fixture/host/fleet/tasks/$execution_id/provenance.json" ]
[ -f "$fixture/host/fleet/tasks/$execution_id/attempt.json" ]
# A complete workflow uses its recorded run and gate, with exact selected input.
mkdir -p "$fixture/source/.hydra/workflows"
cat > "$fixture/source/.hydra/workflows/remote.yml" <<'WORKFLOW'
version: 1
id: remote
parallelism: 1
resources:
  disk_mb: 1
  max_heads: 2
steps:
  - id: create
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: wf-worker
  - id: work
    kind: exec
    needs: [create]
    retry: 0
    idempotent: true
    args:
      head: wf-worker
      argv: [sh, task-work.sh]
  - id: verify
    kind: gate
    needs: [work]
    retry: 0
    idempotent: true
    args:
      head: wf-worker
      name: result
      argv: [test, -s, result.txt]
WORKFLOW
cat > "$fixture/source/task-work.sh" <<'WORK'
set -eu
cat "$HYDRA_TASK_INPUT_DIR/context" > result.txt
WORK
git -C "$fixture/source" add .hydra/workflows/remote.yml task-work.sh
git -C "$fixture/source" -c commit.gpgSign=false commit -qm workflow
workflow_commit="$(git -C "$fixture/source" rev-parse HEAD)"
printf 'selected task input\000binary tail\n' > "$fixture/source/context"
sed -e "s/$commit/$workflow_commit/" \
    -e 's@"work":{"kind":"exec","argv":\["true"\]}@"work":{"kind":"workflow","path":".hydra/workflows/remote.yml"}@' \
    -e 's@"inputs":\[\]@"inputs":["context"]@' \
    -e 's/"outputs":\[\]/"outputs":["result.txt"]/' \
    -e 's/command-exit/workflow-success/' "$fixture/spec" > "$fixture/workflow-spec"
task prepare --source "$fixture/source" --spec "$fixture/workflow-spec" --output "$fixture/workflow-package" > "$fixture/workflow-preview"
workflow_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/workflow-preview")"
task submit build --input "$fixture/workflow-package" --key workflow --trust-spec "$workflow_digest" > "$fixture/workflow-receipt"
workflow_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/workflow-receipt")"
task start build --id "$workflow_id" --trust-spec "$workflow_digest" >/dev/null
attempt=0
while [ "$attempt" -lt 150 ]; do
    task status build --id "$workflow_id" > "$fixture/workflow-status"
    if grep -q '"state":"succeeded"' "$fixture/workflow-status"; then break; fi
    if grep -Eq '"state":"(failed|outcome_unknown)"' "$fixture/workflow-status"; then cat "$fixture/workflow-status"; cat "$fixture/host/fleet/tasks/$workflow_id/stderr"; exit 1; fi
    sleep 0.1; attempt=$((attempt + 1))
done
grep -q '"state":"succeeded"' "$fixture/workflow-status"
grep -q '"run_id":"run_' "$fixture/workflow-status"
for output in "$fixture/host/fleet/tasks/$workflow_id"/heads/*/result.txt; do
    cmp "$fixture/source/context" "$output"
done
# The owner seals results before downloads; later edits cannot change that snapshot.
for sealed_id in "$workflow_id" "$execution_id"; do
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        task status build --id "$sealed_id" > "$fixture/seal-status"
        if grep -q '"result_state":"ready"' "$fixture/seal-status"; then break; fi
        if grep -q '"result_state":"unavailable"' "$fixture/seal-status"; then cat "$fixture/seal-status"; exit 1; fi
        sleep 0.1; attempt=$((attempt + 1))
    done
    grep -q '"result_state":"ready"' "$fixture/seal-status"
done
task result build --id "$workflow_id" > "$fixture/workflow-result"
grep -q '"result_sha256":' "$fixture/workflow-result"
grep -q '"path":"result.txt"' "$fixture/workflow-result"
grep -q '"dirty":true' "$fixture/workflow-result"
grep -q 'latest-head-commit' "$fixture/workflow-result"
for output in "$fixture/host/fleet/tasks/$workflow_id"/heads/*/result.txt; do
    printf changed > "$output"
done
task result build --id "$workflow_id" > "$fixture/repeated-result"
cmp "$fixture/workflow-result" "$fixture/repeated-result"
task result build --id "$execution_id" > "$fixture/exec-result"
grep -q '"path":"attempt.json"' "$fixture/exec-result"
grep -q '"dirty":false' "$fixture/exec-result"
task result build --id "$workflow_id" --output "$fixture/result-package" > "$fixture/result-preview"
task inspect-result --input "$fixture/result-package" > "$fixture/inspected-result"
if grep -Eq '"(hex|bundle_hex)":' "$fixture/inspected-result"; then exit 1; fi
# Simulate the final state write being lost after the immutable snapshot exists.
cp "$fixture/host/fleet/tasks/$workflow_id/state.json" "$fixture/sealed-state"
sed 's/"result_state":"ready"/"result_state":"sealing"/' "$fixture/sealed-state" > "$fixture/host/fleet/tasks/$workflow_id/state.json"
task status build --id "$workflow_id" > "$fixture/seal-recovery"
grep -q '"result_state":"unknown"' "$fixture/seal-recovery"
task result build --id "$workflow_id" --output "$fixture/recovered-result" >/dev/null
cmp "$fixture/result-package" "$fixture/recovered-result"
cp "$fixture/sealed-state" "$fixture/host/fleet/tasks/$workflow_id/state.json"
"$(dirname "$HYDRA_FLEET_BIN")/test-task-result" "$fixture/result-package"
sed 's/"result_sha256":"./"result_sha256":"z/' "$fixture/result-package" > "$fixture/bad-result"
if task inspect-result --input "$fixture/bad-result" > "$fixture/result-error"; then exit 1; fi
grep -q '"code":"invalid_result"' "$fixture/result-error"
if task result build --id "$workflow_id" --output "$fixture/result-package" > "$fixture/result-error"; then exit 1; fi
grep -q '"code":"io_failed"' "$fixture/result-error"

# A command that emits a symlink cannot produce a valid artifact snapshot.
sed -e 's@\["true"\]@["ln","-s","/dev/null","result.txt"]@' -e 's/"outputs":\[\]/"outputs":["result.txt"]/' "$fixture/spec" > "$fixture/unsafe-output-spec"
task prepare --source "$fixture/source" --spec "$fixture/unsafe-output-spec" --output "$fixture/unsafe-output-package" > "$fixture/unsafe-output-preview"
unsafe_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/unsafe-output-preview")"
task submit build --input "$fixture/unsafe-output-package" --key unsafe-output --trust-spec "$unsafe_digest" > "$fixture/unsafe-output-receipt"
unsafe_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/unsafe-output-receipt")"
attempt=0
while [ "$attempt" -lt 100 ]; do
    task status build --id "$unsafe_id" > "$fixture/unsafe-output-status"
    if grep -q '"result_state":"unavailable"' "$fixture/unsafe-output-status"; then break; fi
    sleep 0.1; attempt=$((attempt + 1))
done
grep -q '"result_state":"unavailable"' "$fixture/unsafe-output-status"
if task result build --id "$unsafe_id" > "$fixture/result-error"; then exit 1; fi
grep -q '"code":"result_unavailable"' "$fixture/result-error"
# This fixture extension is linted separately by make lint.
# shellcheck disable=SC1091
. "$root/tests/task_collection_cases.sh"
# Killing an owner cannot authorize a second execution. Stop it before terminating
# its child, so it cannot reap the child or publish a completion before the crash.
sed "s@\[\"true\"\]@[\"sh\",\"-c\",\"printf started > $fixture/crash-started; sleep 30\"]@" "$fixture/spec" > "$fixture/crash-spec"
task prepare --source "$fixture/source" --spec "$fixture/crash-spec" --output "$fixture/crash-package" > "$fixture/crash-preview"
task submit build --input "$fixture/crash-package" --key owner-crash > "$fixture/crash-receipt"
crash_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/crash-receipt")"
crash_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/crash-preview")"
task start build --id "$crash_id" --trust-spec "$crash_digest" >/dev/null
attempt=0
while [ ! -f "$fixture/crash-started" ] && [ "$attempt" -lt 100 ]; do sleep 0.1; attempt=$((attempt + 1)); done
[ -f "$fixture/crash-started" ]
task status build --id "$crash_id" > "$fixture/crash-status"
owner="$(sed -n 's/.*"owner_pid":\([0-9]*\).*/\1/p' "$fixture/crash-status")"
owned_group="$(ps -axo pid=,ppid= | awk -v owner="$owner" '$2 == owner {print $1; exit}')"
[ -n "$owned_group" ]
owned_owner="$owner"
kill -STOP "$owner"
kill -KILL "-$owned_group"
owned_group=""
kill -KILL "$owner"
owned_owner=""
sleep 0.1
task status build --id "$crash_id" > "$fixture/crash-status"
grep -q '"state":"outcome_unknown"' "$fixture/crash-status"
if task start build --id "$crash_id" --trust-spec "$crash_digest" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
# Queue expiry is distinct from an attempted execution.
sed 's/"queue_seconds":60/"queue_seconds":1/' "$fixture/spec" > "$fixture/queue-spec"
task prepare --source "$fixture/source" --spec "$fixture/queue-spec" --output "$fixture/queue-package" > "$fixture/queue-preview"
task submit build --input "$fixture/queue-package" --key queue-expiry > "$fixture/queue-receipt"
queue_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/queue-receipt")"
queue_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/queue-preview")"
sleep 1
task start build --id "$queue_id" --trust-spec "$queue_digest" > "$fixture/queue-status"
grep -q '"failure":"queue_deadline"' "$fixture/queue-status"
[ ! -e "$fixture/host/fleet/tasks/$queue_id/launch.json" ]
# Distinct startup and execution timers operate independently of transport.
for phase in startup execution; do
    phase_prefix="$fixture/deadline-$phase"
    sed -e "s/\"${phase}_seconds\":[0-9]*/\"${phase}_seconds\":1/" \
        -e 's/\["true"\]/["sleep","10"]/' "$fixture/spec" > "$phase_prefix-spec"
    task prepare --source "$fixture/source" --spec "$phase_prefix-spec" --output "$phase_prefix-package" > "$phase_prefix-preview"
    phase_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$phase_prefix-preview")"
    if [ "$phase" = startup ]; then : > "$fixture/transport/slow-clone"; fi
    task submit build --input "$phase_prefix-package" --key "$phase-deadline" --trust-spec "$phase_digest" > "$phase_prefix-receipt"
    phase_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$phase_prefix-receipt")"
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        task status build --id "$phase_id" > "$phase_prefix-status"
        if grep -q '"state":"failed"' "$phase_prefix-status"; then break; fi
        sleep 0.1; attempt=$((attempt + 1))
    done
    grep -q "\"failure\":\"${phase}_deadline\"" "$phase_prefix-status"
    rm -f "$fixture/transport/slow-clone"
done
# Storage corruption is recovery-required, never a reason to replace acceptance.
task cancel build --id "$lost_id" > "$fixture/pending-cancel"
grep -q '"state":"cancelled"' "$fixture/pending-cancel"
grep -q '"cancellation_scope":"not_launched"' "$fixture/pending-cancel"
[ ! -e "$fixture/host/fleet/tasks/$lost_id/launch.json" ]
task logs build --id "$lost_id" > "$fixture/no-logs"
grep -q '"available":false' "$fixture/no-logs"
task cancel build --id "$crash_id" > "$fixture/unknown-cancel"
grep -q '"cancellation":"unknown"' "$fixture/unknown-cancel"
grep -q '"cancel_requested_at":' "$fixture/unknown-cancel"

task logs build --id "$execution_id" --limit 16 > "$fixture/log-chunk"
expected="$(od -An -tx1 -N16 "$fixture/host/fleet/tasks/$execution_id/stdout" | tr -d ' \n')"
grep -q "\"hex\":\"$expected\"" "$fixture/log-chunk"
grep -q '"next_offset":16' "$fixture/log-chunk"
task logs build --id "$execution_id" --offset 16 --limit 16 > "$fixture/log-chunk"
grep -q '"next_offset":32' "$fixture/log-chunk"
if task logs build --id "$execution_id" --offset -1 >/dev/null; then exit 1; fi
if task logs build --id "$execution_id" --limit 65537 >/dev/null; then exit 1; fi
mv "$fixture/host/fleet/tasks/$execution_id/stderr" "$fixture/saved-stderr"
ln -s "$fixture/source/context" "$fixture/host/fleet/tasks/$execution_id/stderr"
if task logs build --id "$execution_id" --stream stderr > "$fixture/error"; then exit 1; fi
grep -q '"code":"invalid_log"' "$fixture/error"
rm "$fixture/host/fleet/tasks/$execution_id/stderr"
mv "$fixture/saved-stderr" "$fixture/host/fleet/tasks/$execution_id/stderr"

# Cancel real running commands and workflows without losing their worktrees.
printf "printf running > '%s'; sleep 30\n" "$fixture/wf-cancel-started" >> "$fixture/source/task-work.sh"
git -C "$fixture/source" add task-work.sh
git -C "$fixture/source" -c commit.gpgSign=false commit -qm cancellable-workflow
cancel_commit="$(git -C "$fixture/source" rev-parse HEAD)"
for kind in exec workflow; do
    if [ "$kind" = exec ]; then
        sed -e 's/crash-started/cancel-started/' -e 's@sleep 30@printf live-worker-output; head -c 5000 /dev/zero; exec 1>\&-; sleep 30@' -e 's/"cancellation_seconds":10/"cancellation_seconds":1/' "$fixture/crash-spec" > "$fixture/control-spec"
        marker="$fixture/cancel-started"
    else
        sed -e "s/$workflow_commit/$cancel_commit/" -e 's/"cancellation_seconds":10/"cancellation_seconds":2/' "$fixture/workflow-spec" > "$fixture/control-spec"
        marker="$fixture/wf-cancel-started"
    fi
    task prepare --source "$fixture/source" --spec "$fixture/control-spec" --output "$fixture/cancel-$kind-package" > "$fixture/control-preview"
    control_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/control-preview")"
    task submit build --input "$fixture/cancel-$kind-package" --key "cancel-$kind" --trust-spec "$control_digest" > "$fixture/control-receipt"
    control_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/control-receipt")"
    attempt=0
    while [ ! -f "$marker" ] && [ "$attempt" -lt 100 ]; do sleep 0.1; attempt=$((attempt + 1)); done
    [ -f "$marker" ]
    task logs build --id "$control_id" > "$fixture/live-logs"
    grep -q '"available":true' "$fixture/live-logs"
    if [ "$kind" = exec ]; then
        task logs build --id "$control_id" --source work > "$fixture/worker-logs"
        grep -q '"text_preview":"live-worker-output?' "$fixture/worker-logs"
        grep -q '"run_id":"run_' "$fixture/worker-logs"
        grep -q '"state":"running"' "$fixture/worker-logs"
        grep -q '"available_bytes":4096' "$fixture/worker-logs"
        grep -q '"limit_reached":true' "$fixture/worker-logs"
        grep -q '"hex":"6c6976652d776f726b65722d6f757470757400' "$fixture/worker-logs"
        task logs build --id "$control_id" --source work --offset 4096 > "$fixture/worker-end"
        grep -q '"hex":""' "$fixture/worker-end"
        if task logs build --id "$control_id" --source work --step invalid >/dev/null; then exit 1; fi
    else
        task logs build --id "$control_id" --source work --step work --attempt 1 > "$fixture/worker-logs"
        grep -q '"available":true' "$fixture/worker-logs"
        if task logs build --id "$control_id" --source work --step ../escape >/dev/null; then exit 1; fi
    fi
    : > "$fixture/transport/lose-ack"
    if task cancel build --id "$control_id" > "$fixture/error"; then exit 1; fi
    grep -q '"code":"outcome_unknown"' "$fixture/error"
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        task status build --id "$control_id" > "$fixture/control-status"
        if grep -q '"state":"cancelled"' "$fixture/control-status"; then break; fi
        sleep 0.1; attempt=$((attempt + 1))
    done
    grep -q '"cancellation":"confirmed_stopped"' "$fixture/control-status" || { cat "$fixture/control-status"; exit 1; }
    [ -d "$fixture/host/fleet/tasks/$control_id/workspace/.git" ]
    task cancel build --id "$control_id" > "$fixture/repeat-cancel"
    grep -q '"cancel_response":"already_terminal"' "$fixture/repeat-cancel"
done

# Storage corruption is recovery-required, never a reason to replace acceptance.
printf '{}' > "$fixture/host/fleet/tasks/$id/state.json"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"recovery_required"' "$fixture/error"
cmp "$fixture/original-acceptance" "$fixture/host/fleet/tasks/$id/acceptance.json"
printf 'Task acceptance and execution: deduplication, lost acknowledgments, exec/workflow attempts, selected inputs, gates, mapping, outages, and corruption passed\n'
