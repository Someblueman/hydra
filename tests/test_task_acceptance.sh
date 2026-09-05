#!/bin/sh
# Public CLI over a controlled SSH boundary and the real receiver, state, and Git.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
cleanup() {
    [ -z "${owned_group:-}" ] || kill -KILL "-$owned_group" 2>/dev/null || :
    [ -z "${owned_owner:-}" ] || kill -KILL "$owned_owner" 2>/dev/null || :
    for workspace in "$fixture"/host/fleet/tasks/task_*/workspace; do
        [ -f "$workspace/.git/hydra/project-id" ] || continue
        (cd "$workspace" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
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
if [ -f "$HYDRA_TEST_TRANSPORT/lose-ack" ] && grep -Eq '"operation":"(submit|start)"' "$request"; then
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
printf 'selected task input\n' > "$fixture/source/context"
sed -e "s/$commit/$workflow_commit/" \
    -e 's@"work":{"kind":"exec","argv":\["true"\]}@"work":{"kind":"workflow","path":".hydra/workflows/remote.yml"}@' \
    -e 's@"inputs":\[\]@"inputs":["context"]@' \
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
printf '{}' > "$fixture/host/fleet/tasks/$id/state.json"
if task submit build --input "$fixture/package" --key same-key > "$fixture/error"; then exit 1; fi
grep -q '"code":"recovery_required"' "$fixture/error"
cmp "$fixture/original-acceptance" "$fixture/host/fleet/tasks/$id/acceptance.json"
printf 'Task acceptance and execution: deduplication, lost acknowledgments, exec/workflow attempts, selected inputs, gates, mapping, outages, and corruption passed\n'
