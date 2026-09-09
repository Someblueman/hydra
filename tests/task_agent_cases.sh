#!/bin/sh
# Sourced with the real task receiver behind test_task_acceptance's SSH fixture.
# shellcheck disable=SC2154
cat > "$fixture/agent-contract.json" <<EOF
{"schema_version":1,"executable":"$root/tests/fixtures/agents/echo-worker.sh","argv":["--new",{"input":"session_id"}],"resume_argv":["--resume",{"input":"session_id"}],"prompt":"stdin","session":"generated","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["echo-worker-v1"]}
EOF
HYDRA_HOME="$fixture/host" "$root/bin/hydra" agent import remote-echo "$fixture/agent-contract.json" >/dev/null
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
VERIFY
cat > "$fixture/source/.hydra/workflows/agents.json" <<'DATA'
{"schema_version":1,"inputs":{"first":{"source":"task","path":"agent-prompt","type":"file","max_bytes":1024},"followup":{"source":"task","path":"agent-followup","type":"file","max_bytes":1024}},"steps":{"first":{"inputs":{"prompt":{"input":"first"}},"outputs":{"answer":{"path":"answer.txt","type":"file","max_bytes":1024}}},"recall":{"inputs":{"followup":{"input":"followup"}},"outputs":{"recalled":{"path":"recalled.txt","type":"file","max_bytes":1024}}},"verify":{"inputs":{"answer":{"step":"first","output":"answer"},"recalled":{"step":"recall","output":"recalled"}}}}}
DATA
git -C "$fixture/source" add .hydra/workflows/agents.yml .hydra/workflows/agents.json .hydra/workflows/verify-agent.sh
git -C "$fixture/source" -c commit.gpgSign=false commit -qm headless-agent-workflow
agent_commit="$(git -C "$fixture/source" rev-parse HEAD)"
printf 'ARTIFACT=remote_exact_artifact\n' > "$fixture/source/agent-prompt"
printf 'Recall the previous artifact without receiving its value again\n' > "$fixture/source/agent-followup"
sed -e "s/$workflow_commit/$agent_commit/" -e 's@remote.yml@agents.yml@' \
    -e 's/"capabilities":\["exec"\]/"capabilities":["workflow","agent-headless","workflow-data"]/' \
    -e 's/"inputs":\["context"\]/"inputs":["agent-prompt","agent-followup"]/' \
    -e 's/"outputs":\["result.txt"\]/"outputs":[]/' "$fixture/workflow-spec" > "$fixture/agent-spec"
task prepare --source "$fixture/source" --spec "$fixture/agent-spec" --output "$fixture/agent-package" > "$fixture/agent-preview"
agent_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/agent-preview")"
task submit build --input "$fixture/agent-package" --key agent-resume --trust-spec "$agent_digest" > "$fixture/agent-receipt"
agent_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/agent-receipt")"
: > "$fixture/transport/offline"
if task status build --id "$agent_id" > "$fixture/agent-offline"; then exit 1; fi
grep -q '"code":"offline"' "$fixture/agent-offline"
rm "$fixture/transport/offline"
attempt=0
while [ "$attempt" -lt 200 ]; do
    task status build --id "$agent_id" > "$fixture/agent-status"
    if grep -q '"result_state":"ready"' "$fixture/agent-status"; then break; fi
    if grep -Eq '"state":"(failed|outcome_unknown)"|"result_state":"unavailable"' "$fixture/agent-status"; then cat "$fixture/agent-status"; cat "$fixture/host/fleet/tasks/$agent_id/stderr"; exit 1; fi
    sleep 0.1; attempt=$((attempt + 1))
done
grep -q '"state":"succeeded"' "$fixture/agent-status"
grep -q '"result_state":"ready"' "$fixture/agent-status"
agent_run="$(sed -n 's/.*"run_id":"\([^"]*\)".*/\1/p' "$fixture/agent-status")"
agent_run_dir="$(find "$fixture/host/state/v2/projects" -path "*/workflows/runs/$agent_run" -type d -print)"
cmp "$agent_run_dir/steps/first/attempt-1/artifacts/answer" "$agent_run_dir/steps/recall/attempt-1/artifacts/recalled"
[ "$(cat "$agent_run_dir/steps/recall/attempt-1/artifacts/recalled")" = remote_exact_artifact ]
task result build --id "$agent_id" > "$fixture/agent-result"
# The provider receipt is carried losslessly in the workflow stdout evidence.
sed 's,\\/,/,g' "$fixture/agent-result" | grep -q 'steps/first/attempt-1/stdout'
grep -q '70726f66696c655f736861323536' "$fixture/agent-result"
printf 'Remote agents: selected prompt, exact recorded-session recall, disconnect/reconnect, sealed artifact consumption, independent gate and collected metadata passed\n'
