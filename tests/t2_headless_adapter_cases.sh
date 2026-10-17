#!/bin/sh
# Sourced by test_t2_headless.sh: a headless adapter (the deterministic session
# fixture, never a live provider) runs on the tmux-less receiver inside a
# workflow task; the coordinator disconnects, reconnects, and collects the
# exact artifact that a dependent gate already consumed on the receiver.
: "${fixture:?}" "${root:?}" "${receiver_path:?}" "${commit:?}"
cat > "$fixture/agent-contract.json" <<EOF
{"schema_version":1,"executable":"$root/tests/fixtures/agents/echo-worker.sh","argv":["--new",{"input":"session_id"}],"resume_argv":["--resume",{"input":"session_id"}],"prompt":"stdin","session":"generated","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["echo-worker-v1"]}
EOF
receiver_hydra agent import remote-echo "$fixture/agent-contract.json" >/dev/null
mkdir -p "$fixture/source/.hydra/workflows"
cat > "$fixture/source/.hydra/workflows/agents.yml" <<'WORKFLOW'
version: 1
id: remote-agents
data: agents.json
resources:
  disk_mb: 1
steps:
  - id: create
    kind: spawn
    idempotent: false
    args:
      branch: remote-agent
      terminal_mode: headless
  - id: first
    kind: exec
    needs: [create]
    idempotent: false
    args:
      head: remote-agent
      profile: remote-echo
      prompt_input: prompt
      result_file: answer.txt
      requires: [prompt, observations]
  - id: recall
    kind: exec
    needs: [first]
    idempotent: false
    args:
      head: remote-agent
      profile: remote-echo
      prompt_input: followup
      result_file: recalled.txt
      resume_from: first
      requires: [resume]
  - id: verify
    kind: gate
    needs: [first, recall]
    idempotent: true
    args:
      head: remote-agent
      name: prompt
      argv: [sh, .hydra/workflows/verify-agent.sh]
WORKFLOW
cat > "$fixture/source/.hydra/workflows/verify-agent.sh" <<'VERIFY'
#!/bin/sh
set -eu
cmp "$HYDRA_WORKFLOW_INPUTS_DIR/answer" "$HYDRA_WORKFLOW_INPUTS_DIR/recalled"
[ "$(cat "$HYDRA_WORKFLOW_INPUTS_DIR/answer")" = remote_exact_artifact ]
test -s received-prompt
# The dependent gate publishes the consumed value as the task's declared output.
cp "$HYDRA_WORKFLOW_INPUTS_DIR/answer" answer.txt
VERIFY
cat > "$fixture/source/.hydra/workflows/agents.json" <<'DATA'
{"schema_version":1,"inputs":{"first":{"source":"task","path":"agent-prompt","type":"file","max_bytes":1024},"followup":{"source":"task","path":"agent-followup","type":"file","max_bytes":1024}},"steps":{"first":{"inputs":{"prompt":{"input":"first"}},"outputs":{"answer":{"path":"answer.txt","type":"file","max_bytes":1024}}},"recall":{"inputs":{"followup":{"input":"followup"}},"outputs":{"recalled":{"path":"recalled.txt","type":"file","max_bytes":1024}}},"verify":{"inputs":{"answer":{"step":"first","output":"answer"},"recalled":{"step":"recall","output":"recalled"}}}}}
DATA
git -C "$fixture/source" add .hydra/workflows
git -C "$fixture/source" commit -qm headless-agent-workflow
agent_commit="$(git -C "$fixture/source" rev-parse HEAD)"
printf 'ARTIFACT=remote_exact_artifact\n' > "$fixture/source/agent-prompt"
printf 'Recall the previous artifact without receiving its value again\n' > "$fixture/source/agent-followup"
sed -e "s/$commit/$agent_commit/" \
    -e 's@"work":{"kind":"exec","argv":\["true"\]}@"work":{"kind":"workflow","path":".hydra/workflows/agents.yml"}@' \
    -e 's/"capabilities":\["exec","execution-headless"\]/"capabilities":["workflow","agent-headless","workflow-data","execution-headless"]/' \
    -e 's/"inputs":\[\]/"inputs":["agent-prompt","agent-followup"]/' \
    -e 's/"outputs":\[\]/"outputs":["answer.txt"]/' \
    -e 's/command-exit/workflow-success/' "$fixture/spec" > "$fixture/agent-spec"
task prepare --source "$fixture/source" --spec "$fixture/agent-spec" --output "$fixture/agent-package" > "$fixture/agent-preview"
agent_digest="$(json_field spec_sha256 "$fixture/agent-preview")"
task submit build --input "$fixture/agent-package" --key agent-headless --trust-spec "$agent_digest" > "$fixture/agent-receipt"
agent_id="$(json_field task_id "$fixture/agent-receipt")"
[ -n "$agent_id" ]
# Disconnect: the coordinator loses the host while the adapter keeps running.
: > "$fixture/transport/offline"
if task status build --id "$agent_id" > "$fixture/agent-offline"; then exit 1; fi
grep -q '"code":"offline"' "$fixture/agent-offline"
if task logs build --id "$agent_id" > "$fixture/agent-offline-logs"; then exit 1; fi
rm "$fixture/transport/offline"
# Reconnect: the detached owner finished the run without any terminal.
await "$agent_id" '"result_state":"ready"' "$fixture/agent-status"
grep -q '"state":"succeeded"' "$fixture/agent-status"
agent_run="$(json_field run_id "$fixture/agent-status")"
agent_run_dir="$(find "$fixture/host/state/v2/projects" -path "*/workflows/runs/$agent_run" -type d -print)"
[ -d "$agent_run_dir" ]
cmp "$agent_run_dir/steps/first/attempt-1/artifacts/answer" "$agent_run_dir/steps/recall/attempt-1/artifacts/recalled"
[ "$(cat "$agent_run_dir/steps/recall/attempt-1/artifacts/recalled")" = remote_exact_artifact ]
[ "$(sed -n '1p' "$agent_run_dir/steps/verify/state")" = succeeded ]
# Step and attempt logs are readable without attaching to the adapter's stdin.
task logs build --id "$agent_id" --source work --step first --attempt 1 > "$fixture/agent-step-logs"
grep -q '"available":true' "$fixture/agent-step-logs"
if task logs build --id "$agent_id" --source work --step ../escape >/dev/null; then exit 1; fi
task observe build --id "$agent_id" --event-limit 4 > "$fixture/agent-observation"
grep -q '"attempt_history"' "$fixture/agent-observation"
grep -q '"process_exit":"0"' "$fixture/agent-observation"
grep -q '"provider_observations"' "$fixture/agent-observation"
# The exact adapter artifact reaches the coordinator through result collection.
task result build --id "$agent_id" > "$fixture/agent-result-preview"
sed 's,\\/,/,g' "$fixture/agent-result-preview" | grep -q 'steps/first/attempt-1/stdout'
grep -q '"path":"answer.txt"' "$fixture/agent-result-preview"
task result build --id "$agent_id" --output "$fixture/agent-result" >/dev/null
task collect --input "$fixture/agent-result" --into "$fixture/collector" > "$fixture/agent-collected" || { cat "$fixture/agent-collected"; exit 1; }
agent_collection="$(sed -n 's/.*"path":"\([^"]*\)".*/\1/p' "$fixture/agent-collected" | sed 's@\\/@/@g')"
[ "$(cat "$agent_collection/artifacts/answer.txt")" = remote_exact_artifact ]
printf 'T2: headless adapter task, disconnect/reconnect, exact recall, dependent gate, step logs and collected artifact passed\n'
