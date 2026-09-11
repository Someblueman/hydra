#!/bin/sh
# Public workflow DAG through real task acceptance, execution and collection.
set -eu
if [ "${HYDRA_TEST_DAG_PARALLELISM:-0}" = 1 ]; then
    HYDRA_TEST_DAG_LOST_ACK=1
    export HYDRA_TEST_DAG_LOST_ACK
fi
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
fixture="$(mktemp -d "${HYDRA_TEST_FIXTURE_ROOT:-${TMPDIR:-/tmp}}/hydra-task.XXXXXX")"
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_HOME HYDRA_FLEET_BIN HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    workflow_task_fixture_quiesce || return 1
    if [ "${HYDRA_TEST_KEEP_FIXTURE:-0}" = 1 ]; then printf 'Task DAG evidence: %s\n' "$fixture" >&2; return 0; fi
    for workspace in "$HYDRA_HOME"/fleet/tasks/task_*/workspace; do
        [ -f "$workspace/.git/hydra/project-id" ] || continue
        (cd "$workspace" && "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
    done
    test_tmux_fixture_cleanup "$fixture" || return 1
    if [ "${passed:-0}" = 1 ]; then rm -rf "$fixture"; else printf 'Task DAG evidence: %s\n' "$fixture" >&2; fi
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
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
if [ "${HYDRA_TEST_DAG_SOURCE:-0}" = 1 ]; then
    cat >> produce.sh <<'SOURCE'
printf 'source-commit\n' > marker
 git add marker result.txt
 git -c user.name=Producer -c user.email=producer@example.invalid commit -qm candidate
SOURCE
    cat >> consume.sh <<'SOURCE'
[ "$(git show HEAD:marker)" = source-commit ]
SOURCE
fi
if [ "${HYDRA_TEST_DAG_CRASH:-0}" != 0 ] || [ "${HYDRA_TEST_DAG_FAULT:-}" = cancel ]; then sed -i.bak 's/HYDRA_DAG_TEST_DELAY:-0/HYDRA_DAG_TEST_DELAY:-12/' produce.sh; rm produce.sh.bak; fi
git add .
git commit -qm 'task recipes'
commit="$(git rev-parse HEAD)"
"$root/bin/hydra" init --no-agent --trust >/dev/null
workflow_runs="$(cd "$HYDRA_HOME" && pwd -P)/state/v2/projects/$(cat .git/hydra/project-id)/workflows/runs"
dag_host=local
if [ "${HYDRA_TEST_DAG_LOST_ACK:-0}" = 1 ] || [ "${HYDRA_TEST_DAG_RESULT_LOST:-0}" = 1 ] || [ "${HYDRA_TEST_DAG_RESULT_BAD:-0}" = 1 ]; then
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
if [ -f "$HYDRA_TEST_DAG_CONTROL/lose-result" ] && grep -q '"operation":"result"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_DAG_CONTROL/lost-response"
    rm "$HYDRA_TEST_DAG_CONTROL/lose-result"
    exit 255
fi
if [ -f "$HYDRA_TEST_DAG_CONTROL/bad-result" ] && grep -q '"operation":"result"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_DAG_CONTROL/lost-response"
    sed 's/"result_sha256":"[a-f0-9]*/"result_sha256":"0000000000000000000000000000000000000000000000000000000000000000/' "$HYDRA_TEST_DAG_CONTROL/lost-response" > "$HYDRA_TEST_DAG_CONTROL/bad-response"
    mv "$HYDRA_TEST_DAG_CONTROL/bad-response" "$HYDRA_TEST_DAG_CONTROL/lost-response"
    rm "$HYDRA_TEST_DAG_CONTROL/bad-result"
    cat "$HYDRA_TEST_DAG_CONTROL/lost-response"
    exit 0
fi
exec /bin/sh -c "$2" < "$request"
SSH
    chmod +x "$fixture/bin/ssh"
    PATH="$fixture/bin:$PATH"
    export PATH
    "$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$HYDRA_HOME" >/dev/null
    dag_host=build
    if [ "${HYDRA_TEST_DAG_LOST_ACK:-0}" = 1 ]; then : > "$fixture/lose-ack"; fi
    if [ "${HYDRA_TEST_DAG_RESULT_LOST:-0}" = 1 ]; then : > "$fixture/lose-result"; fi
    if [ "${HYDRA_TEST_DAG_RESULT_BAD:-0}" = 1 ]; then : > "$fixture/bad-result"; fi
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
if [ "${HYDRA_TEST_DAG_SOURCE:-0}" = 1 ]; then printf '      source_step: produce\n' >> "$fixture/workflow.yml"; fi
if [ "${HYDRA_TEST_DAG_PARALLELISM:-0}" = 1 ]; then
    sed 's/parallelism: 2/parallelism: 1/' "$fixture/workflow.yml" > "$fixture/serial.yml"
    mv "$fixture/serial.yml" "$fixture/workflow.yml"
    cat >> "$fixture/workflow.yml" <<'YAML'
  - id: independent
    kind: task
    needs: []
    retry: 0
    idempotent: false
    args:
      task_input: recipe
YAML
    sed 's/"steps":{/"steps":{"independent":{"inputs":{"recipe":{"input":"producer-recipe"}},"outputs":{"result":{"path":"result.txt","type":"file","max_bytes":4096}}},/' "$fixture/data.json" > "$fixture/independent.json"
    mv "$fixture/independent.json" "$fixture/data.json"
fi
if [ "${HYDRA_TEST_DAG_SOURCE_TAMPER:-0}" = 1 ]; then
    # shellcheck source=/dev/null
    . "$root/tests/workflow_task_source_cases.sh"
    workflow_source_fault_setup
fi
"$root/bin/hydra" workflow validate "$fixture/workflow.yml" > "$fixture/validation"
run_code=0
if [ "${HYDRA_TEST_DAG_CRASH:-0}" != 0 ]; then
    # shellcheck source=/dev/null
    . "$root/tests/workflow_task_crash_cases.sh"
else
    "$root/bin/hydra" workflow run "$fixture/workflow.yml" > "$fixture/run.out" 2> "$fixture/run.err" || run_code=$?
fi
if [ "${HYDRA_TEST_DAG_SOURCE_TAMPER:-0}" = 1 ]; then
    run="$(sed -n '1p' "$fixture/run.out")"
    run_dir="$workflow_runs/$run"
    workflow_source_fault_assert
    exit 0
fi
if [ "${HYDRA_TEST_DAG_LOST_ACK:-0}" = 1 ] || [ "${HYDRA_TEST_DAG_RESULT_LOST:-0}" = 1 ] || [ "${HYDRA_TEST_DAG_RESULT_BAD:-0}" = 1 ]; then
    if [ "${HYDRA_TEST_DAG_RESULT_BAD:-0}" = 1 ]; then [ "$run_code" = 1 ]; else [ "$run_code" = 3 ]; fi
    run="$(sed -n '1p' "$fixture/run.out")"
    run_dir="$workflow_runs/$run"
    cp "$run_dir/steps/produce/attempt-1/remote/dispatch.json" "$fixture/original-dispatch"
    original_id="$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json -print | sed 's|/acceptance.json$||;s|.*/||')"
    if [ "${HYDRA_TEST_DAG_PARALLELISM:-0}" = 1 ]; then
        [ "$(cat "$run_dir/steps/independent/state")" = ready ]
        [ ! -d "$run_dir/steps/independent/attempt-1" ]
        [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 1 ]
    fi
    if [ "${HYDRA_TEST_DAG_RESULT_LOST:-0}" = 1 ]; then
        "$root/bin/hydra" fleet task status build --id "$original_id" > "$fixture/result-status"
        grep -q '"state":"succeeded"' "$fixture/result-status"
        grep -q '"result_state":"ready"' "$fixture/result-status"
    fi
    if [ "${HYDRA_TEST_DAG_RESULT_BAD:-0}" = 1 ]; then
        [ "$(cat "$run_dir/state")" = recovery-required ]
        [ "$(cat "$run_dir/steps/produce/attempt-1/exit-code")" = 76 ]
        [ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 1 ]
        [ ! -d "$run_dir/steps/consume/attempt-1" ]
        passed=1
        printf 'Task DAG: malformed result remains recovery-required without downstream acceptance\n'
        exit 0
    fi
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
run_dir="$workflow_runs/$run"
printf 'producer output\nconsumer output\n' > "$fixture/expected"
cmp "$fixture/expected" "$run_dir/steps/consume/attempt-1/artifacts/result"
expected_tasks=2
if [ "${HYDRA_TEST_DAG_PARALLELISM:-0}" = 1 ]; then expected_tasks=3; fi
[ "$(find "$HYDRA_HOME/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = "$expected_tasks" ]
[ -s "$run_dir/steps/produce/attempt-1/remote/collection.json" ]
[ -s "$run_dir/steps/consume/attempt-1/remote/receipt.json" ]
transport_records="$(find "$run_dir/steps" -path '*/attempt-*/remote/transport-metrics.json' -type f | wc -l | tr -d ' ')"
[ "$transport_records" -gt 0 ]
fixture_json transport-metrics "$run_dir" "${HYDRA_TEST_DAG_LOST_ACK:-0}"
"$root/bin/hydra" workflow statistics-data > "$fixture/statistics.tsv"
awk -F '\t' -v id="$run" '$1 == "R" && $2 == id {found=1; if ($13 == "-" || $14 == "-" || $15 == "-") bad=1} END {exit !found || bad}' "$fixture/statistics.tsv"
if [ "${HYDRA_TEST_DAG_REPLAY:-0}" = 1 ]; then
    "$root/bin/hydra" workflow replay "$run" > "$fixture/replay-1.json"
    "$root/bin/hydra" workflow replay "$run" > "$fixture/replay-2.json"
    cmp "$fixture/replay-1.json" "$fixture/replay-2.json"
    grep -q '"decisions":\["produce"' "$fixture/replay-1.json"
    cp "$run_dir/schedule.jsonl" "$fixture/original-journal"
    sed 's/"decision":"produce"/"decision":"consume"/' "$fixture/original-journal" > "$run_dir/schedule.jsonl"
    if "$root/bin/hydra" workflow replay "$run" > "$fixture/rejected-replay.json"; then exit 1; fi
    cp "$fixture/original-journal" "$run_dir/schedule.jsonl"
    printf '{"schema_version":' >> "$run_dir/schedule.jsonl"
    if "$root/bin/hydra" workflow replay "$run" > "$fixture/partial-replay.json"; then exit 1; fi
    cp "$fixture/original-journal" "$run_dir/schedule.jsonl"
fi
passed=1
printf 'Task DAG: producer -> verified collection -> sealed input -> consumer passed\n'
