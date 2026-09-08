#!/bin/sh
# Public workflow DAG through real task acceptance, execution and collection.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_HOME HYDRA_FLEET_BIN HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    workflow_task_fixture_quiesce || return 1
    for workspace in "$HYDRA_HOME"/fleet/tasks/task_*/workspace; do
        [ -f "$workspace/.git/hydra/project-id" ] || continue
        (cd "$workspace" && "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
    done
    if [ "${passed:-0}" = 1 ]; then rm -rf "$fixture"; else printf 'Task DAG evidence: %s\n' "$fixture" >&2; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir -p "$fixture/source"
cd "$fixture/source"
git init -q
git config user.name Test
git config user.email test@example.invalid
cat > produce.sh <<'SCRIPT'
#!/bin/sh
set -eu
sleep "${HYDRA_DAG_TEST_DELAY:-0}"
printf 'producer output\n' > result.txt
SCRIPT
cat > consume.sh <<'SCRIPT'
#!/bin/sh
set -eu
cat "$HYDRA_TASK_INPUT_DIR/previous" > result.txt
printf 'consumer output\n' >> result.txt
SCRIPT
if [ "${HYDRA_TEST_DAG_CRASH:-0}" != 0 ]; then sed -i.bak 's/HYDRA_DAG_TEST_DELAY:-0/HYDRA_DAG_TEST_DELAY:-12/' produce.sh; rm produce.sh.bak; fi
git add .
git commit -qm 'task recipes'
commit="$(git rev-parse HEAD)"
"$root/bin/hydra" init --no-agent --trust >/dev/null
dag_host=local
if [ "${HYDRA_TEST_DAG_LOST_ACK:-0}" = 1 ]; then
    mkdir "$fixture/bin"
    HYDRA_TEST_DAG_CONTROL="$fixture"
    export HYDRA_TEST_DAG_CONTROL
    cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
request="$(mktemp)"
trap 'rm -f "$request"' EXIT
cat > "$request"
if [ -f "$HYDRA_TEST_DAG_CONTROL/lose-ack" ] && grep -q '"operation":"submit"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_DAG_CONTROL/lost-response"
    rm "$HYDRA_TEST_DAG_CONTROL/lose-ack"
    exit 255
fi
exec /bin/sh -c "$2" < "$request"
SSH
    chmod +x "$fixture/bin/ssh"
    PATH="$fixture/bin:$PATH"
    export PATH
    "$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$HYDRA_HOME" >/dev/null
    dag_host=build
    : > "$fixture/lose-ack"
fi
for node in produce consume; do
    selected='[]'
    [ "$node" != consume ] || selected='["previous"]'
    cat > "$node.json" <<JSON
{"schema_version":1,"host":"$dag_host","project":"$fixture/source","source":{"commit":"$commit"},"work":{"kind":"exec","argv":["sh","$node.sh"]},"inputs":$selected,"outputs":["result.txt"],"capabilities":["exec"],"completion":"command-exit","limits":{"transport_seconds":10,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}
JSON
done
cat > "$fixture/data.json" <<'JSON'
{"schema_version":1,"inputs":{"producer-recipe":{"path":"produce.json","type":"object","max_bytes":4096},"consumer-recipe":{"path":"consume.json","type":"object","max_bytes":4096}},"steps":{"produce":{"inputs":{"recipe":{"input":"producer-recipe"}},"outputs":{"result":{"path":"result.txt","type":"file","max_bytes":4096}}},"consume":{"inputs":{"recipe":{"input":"consumer-recipe"},"previous":{"step":"produce","output":"result"}},"outputs":{"result":{"path":"result.txt","type":"file","max_bytes":4096}}}}}
JSON
cat > "$fixture/workflow.yml" <<'YAML'
version: 1
id: task-handoff
data: data.json
parallelism: 2
resources:
  disk_mb: 1
  max_heads: 2
steps:
  - id: produce
    kind: task
    needs: []
    retry: 0
    idempotent: false
    args:
      task_input: recipe
  - id: consume
    kind: task
    needs: [produce]
    retry: 0
    idempotent: false
    args:
      task_input: recipe
YAML
"$root/bin/hydra" workflow validate "$fixture/workflow.yml" > "$fixture/validation"
run_code=0
if [ "${HYDRA_TEST_DAG_CRASH:-0}" != 0 ]; then
    # shellcheck source=/dev/null
    . "$root/tests/workflow_task_crash_cases.sh"
else
    "$root/bin/hydra" workflow run "$fixture/workflow.yml" > "$fixture/run.out" 2> "$fixture/run.err" || run_code=$?
fi
if [ "${HYDRA_TEST_DAG_LOST_ACK:-0}" = 1 ]; then
    [ "$run_code" = 3 ]
    run="$(sed -n '1p' "$fixture/run.out")"
    run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
    cp "$run_dir/steps/produce/attempt-1/remote/dispatch.json" "$fixture/original-dispatch"
    original_id="$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json -print | sed 's|/acceptance.json$||;s|.*/||')"
    if [ -n "${HYDRA_TEST_DAG_FAULT:-}" ]; then
        # shellcheck source=/dev/null
        . "$root/tests/workflow_task_fault_cases.sh"
    fi
    "$root/bin/hydra" workflow resume "$run" > "$fixture/resume.out" 2> "$fixture/resume.err"
    cmp "$fixture/original-dispatch" "$run_dir/steps/produce/attempt-1/remote/dispatch.json"
    grep -q "\"task_id\":\"$original_id\"" "$run_dir/steps/produce/attempt-1/remote/receipt.json"
else
    [ "$run_code" = 0 ]
fi
run="$(sed -n '1p' "$fixture/run.out")"
"$root/bin/hydra" workflow status "$run" --json > "$fixture/status"
grep -q '"state":"succeeded"' "$fixture/status"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
printf 'producer output\nconsumer output\n' > "$fixture/expected"
cmp "$fixture/expected" "$run_dir/steps/consume/attempt-1/artifacts/result"
[ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 2 ]
[ -s "$run_dir/steps/produce/attempt-1/remote/collection.json" ]
[ -s "$run_dir/steps/consume/attempt-1/remote/receipt.json" ]
passed=1
printf 'Task DAG: producer -> verified collection -> sealed input -> consumer passed\n'
