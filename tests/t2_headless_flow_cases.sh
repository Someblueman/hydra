#!/bin/sh
# Sourced by test_t2_headless.sh: exact outputs consumed by a dependent step
# through the public workflow DAG (coordinator with its own PATH, receiver
# without tmux), then the corresponding compiled plan run locally on a host
# without tmux through the public plan interfaces.
: "${fixture:?}" "${root:?}" "${receiver_path:?}"
cat > "$fixture/source/produce.sh" <<'SCRIPT'
#!/bin/sh
set -eu
printf 'producer output\n' > result.txt
SCRIPT
cat > "$fixture/source/consume.sh" <<'SCRIPT'
#!/bin/sh
set -eu
cat "$HYDRA_TASK_INPUT_DIR/previous" > result.txt
printf 'consumer output\n' >> result.txt
SCRIPT
git -C "$fixture/source" add produce.sh consume.sh
git -C "$fixture/source" commit -qm 'dag recipes'
dag_commit="$(git -C "$fixture/source" rev-parse HEAD)"
(cd "$fixture/source" && "$root/bin/hydra" init --no-agent --trust) >/dev/null
for node in produce consume; do
    selected='[]'
    [ "$node" != consume ] || selected='["previous"]'
    cat > "$fixture/source/$node.json" <<JSON
{"schema_version":1,"host":"build","project":"$fixture/receiver","source":{"commit":"$dag_commit"},"work":{"kind":"exec","argv":["sh","$node.sh"]},"inputs":$selected,"outputs":["result.txt"],"capabilities":["exec","execution-headless"],"completion":"command-exit","limits":{"transport_seconds":10,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}
JSON
done
mkdir -p "$fixture/flow"
cat > "$fixture/flow/data.json" <<'JSON'
{"schema_version":1,"inputs":{"producer-recipe":{"path":"produce.json","type":"object","max_bytes":4096},"consumer-recipe":{"path":"consume.json","type":"object","max_bytes":4096}},"steps":{"produce":{"inputs":{"recipe":{"input":"producer-recipe"}},"outputs":{"result":{"path":"result.txt","type":"file","max_bytes":4096}}},"consume":{"inputs":{"recipe":{"input":"consumer-recipe"},"previous":{"step":"produce","output":"result"}},"outputs":{"result":{"path":"result.txt","type":"file","max_bytes":4096}}}}}
JSON
cat > "$fixture/flow/workflow.yml" <<'YAML'
version: 1
id: headless-handoff
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
before_flow="$(acceptances)"
(cd "$fixture/source" && "$root/bin/hydra" workflow validate "$fixture/flow/workflow.yml") > "$fixture/flow/validation"
flow_code=0
(cd "$fixture/source" && "$root/bin/hydra" workflow run "$fixture/flow/workflow.yml") > "$fixture/flow/run.out" 2> "$fixture/flow/run.err" || flow_code=$?
[ "$flow_code" = 0 ] || { cat "$fixture/flow/run.out" "$fixture/flow/run.err" >&2; exit 1; }
flow_run="$(sed -n '1p' "$fixture/flow/run.out")"
flow_runs="$(cd "$HYDRA_HOME" && pwd -P)/state/v2/projects/$(cat "$fixture/source/.git/hydra/project-id")/workflows/runs"
flow_dir="$flow_runs/$flow_run"
(cd "$fixture/source" && "$root/bin/hydra" workflow status "$flow_run" --json) > "$fixture/flow/status"
grep -q '"state":"succeeded"' "$fixture/flow/status"
grep -q '{"step_id":"produce","state":"succeeded","attempts":1}' "$fixture/flow/status"
grep -q '{"step_id":"consume","state":"succeeded","attempts":1}' "$fixture/flow/status"
printf 'producer output\nconsumer output\n' > "$fixture/flow/expected"
cmp "$fixture/flow/expected" "$flow_dir/steps/consume/attempt-1/artifacts/result"
[ "$(acceptances)" -eq $((before_flow + 2)) ]
[ -s "$flow_dir/steps/produce/attempt-1/remote/collection.json" ]
[ -s "$flow_dir/steps/consume/attempt-1/remote/receipt.json" ]
# Viewing the producer's logs through the alias is not attachment.
produce_task="$(json_field task_id "$flow_dir/steps/produce/attempt-1/remote/receipt.json")"
task logs build --id "$produce_task" --source work > "$fixture/flow/produce-logs"
grep -q '"available":true' "$fixture/flow/produce-logs"
grep -q '"state":"succeeded"' "$fixture/flow/produce-logs"
# A workflow recipe that requires a terminal on the tmux-less host fails
# before launch with the typed capability error: no acceptance, no mode change.
sed 's/"capabilities":\["exec","execution-headless"\]/"capabilities":["exec","tmux"]/' "$fixture/source/produce.json" > "$fixture/source/produce-terminal.json"
mv "$fixture/source/produce-terminal.json" "$fixture/source/produce.json"
if (cd "$fixture/source" && "$root/bin/hydra" workflow run "$fixture/flow/workflow.yml") > "$fixture/flow/terminal.out" 2> "$fixture/flow/terminal.err"; then exit 1; fi
terminal_run="$(sed -n '1p' "$fixture/flow/terminal.out")"
[ -n "$terminal_run" ]
[ "$(cat "$flow_runs/$terminal_run/state")" = failed ]
[ "$(cat "$flow_runs/$terminal_run/steps/produce/state")" = failed ]
grep -q '"code":"capability_unavailable"' "$flow_runs/$terminal_run/steps/produce/attempt-1/remote-response.json"
(cd "$fixture/source" && "$root/bin/hydra" workflow status "$terminal_run" --json) | grep -q '{"step_id":"produce","state":"failed","attempts":1}'
[ ! -e "$flow_runs/$terminal_run/steps/produce/attempt-1/remote/receipt.json" ]
[ ! -d "$flow_runs/$terminal_run/steps/consume/attempt-1" ]
[ "$(acceptances)" -eq $((before_flow + 2)) ]
printf 'T2: workflow DAG consumed exact receiver output in a dependent step without a terminal; terminal recipe refused before launch\n'

# The corresponding compiled plan runs locally on a host without tmux. The
# whole coordinator runs under the receiver PATH here: local placement means
# every task owner, workspace and head is created on the tmux-less host.
local_hydra() { env PATH="$receiver_path" HYDRA_HOME="$fixture/local-home" "$root/bin/hydra" "$@"; }
mkdir "$fixture/plan-source"
cp "$root/tests/fixtures/plan-task/repo/"* "$fixture/plan-source/"
cp "$root/tests/fixtures/plan-task/plan.json" "$fixture/plan.json"
cp "$root/tests/fixtures/plan-task/policy.json" "$fixture/policy.json"
git -C "$fixture/plan-source" init -q
git -C "$fixture/plan-source" config user.name 'T2 test'
git -C "$fixture/plan-source" config user.email t2@example.invalid
git -C "$fixture/plan-source" config commit.gpgSign false
git -C "$fixture/plan-source" add .
git -C "$fixture/plan-source" commit -qm recipes
plan_commit="$(git -C "$fixture/plan-source" rev-parse HEAD)"
(cd "$fixture/plan-source" && local_hydra init --no-agent --trust) >/dev/null
for node in produce inspect compose verify; do
    inputs='[]'; outputs='["result.txt"]'; argv="[\"sh\",\"$node.sh\"]"
    case "$node" in
        produce) argv='["sh","produce.sh","pass"]' ;;
        inspect) inputs='["subject","validation"]'; outputs='["report.json"]'; argv='["sh","check.sh","part-check","part-content","pass"]' ;;
        compose) inputs='["subject"]' ;;
        verify) inputs='["subject","validation"]'; outputs='["report.json"]'; argv='["sh","check.sh","final-check","final-content","pass"]' ;;
    esac
    cat > "$fixture/plan-source/$node.json" <<JSON
{"schema_version":1,"host":"local","project":"$fixture/plan-source","source":{"commit":"$plan_commit"},"work":{"kind":"exec","argv":$argv},"inputs":$inputs,"outputs":$outputs,"capabilities":["exec","execution-headless"],"completion":"command-exit","limits":{"transport_seconds":10,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}
JSON
done
(cd "$fixture/plan-source" && local_hydra workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json") > "$fixture/compile.json"
plan_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile.json")"
[ -n "$plan_digest" ]
(cd "$fixture/plan-source" && local_hydra workflow plan show "$fixture/compiled.json") > "$fixture/plan-preview"
grep -q 'local' "$fixture/plan-preview"
plan_code=0
(cd "$fixture/plan-source" && local_hydra workflow plan run "$fixture/compiled.json" --accept "$plan_digest") > "$fixture/plan-run.out" 2> "$fixture/plan-run.err" || plan_code=$?
[ "$plan_code" = 0 ] || { cat "$fixture/plan-run.out" "$fixture/plan-run.err" >&2; exit 1; }
plan_run="$(sed -n '1p' "$fixture/plan-run.out")"
plan_dir="$(find "$fixture/local-home/state/v2/projects" -type d -path "*/workflows/runs/$plan_run" -print)"
[ "$(cat "$plan_dir/state")" = succeeded ]
(cd "$fixture/plan-source" && local_hydra workflow plan result "$plan_run") > "$fixture/plan-result.json"
(cd "$fixture/plan-source" && local_hydra workflow status "$plan_run" --json) > "$fixture/plan-status"
grep -q '"state":"succeeded"' "$fixture/plan-status"
for step in produce inspect compose verify; do grep -q "{\"step_id\":\"$step\",\"state\":\"succeeded\",\"attempts\":1}" "$fixture/plan-status"; done
printf 'candidate\ncomposed\n' > "$fixture/plan-expected"
cmp "$fixture/plan-expected" "$plan_dir/steps/compose/attempt-1/artifacts/result"
[ "$(find "$fixture/local-home/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 4 ]
for mode in "$fixture/local-home/state/v2/projects"/*/heads/head_*/terminal-mode; do [ "$(sed -n '1p' "$mode")" = headless ]; done
# A compiled plan admits only exec and explicit headless execution: a recipe
# that requires a terminal is a typed compiler diagnostic, so nothing is
# dispatched and no compiled artifact exists to accept.
sed 's/"capabilities":\["exec","execution-headless"\]/"capabilities":["exec","tmux"]/' "$fixture/plan-source/produce.json" > "$fixture/plan-source/produce-terminal.json"
mv "$fixture/plan-source/produce-terminal.json" "$fixture/plan-source/produce.json"
if (cd "$fixture/plan-source" && local_hydra workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled-terminal.json") > "$fixture/compile-terminal.json"; then exit 1; fi
grep -q '"code":"invalid_task_policy"' "$fixture/compile-terminal.json"
[ ! -e "$fixture/compiled-terminal.json" ]
[ "$(find "$fixture/local-home/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = 4 ]
printf 'T2: local compiled plan ran through public interfaces without tmux; terminal requirement refused at compilation\n'
