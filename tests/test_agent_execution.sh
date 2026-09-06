#!/bin/sh
# Public headless exec with real process supervision and a declared fixture agent.
set -eu
# Artifact receipt directories must remain private under a collaborative umask.
umask 002
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
cleanup() {
    if [ -n "${owner:-}" ]; then kill -TERM "$owner" 2>/dev/null || :; wait "$owner" 2>/dev/null || :; fi
    canonical="$(cd "$fixture" && pwd -P)"
    tmux list-sessions -F '#{session_name}|#{session_path}' 2>/dev/null | while IFS='|' read -r session path; do
        case "$path" in "$fixture"/*|"$canonical"/*) tmux kill-session -t "$session" 2>/dev/null || : ;; esac
    done
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then
        printf 'Preserved agent fixture: %s\n' "$fixture" >&2
    else
        rm -rf "$fixture"
    fi
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mkdir "$fixture/repo"
cd "$fixture/repo"
git init -q
git config user.name Test
git config user.email test@example.invalid
git -c commit.gpgSign=false commit --allow-empty -qm base
hydra() { "$root/bin/hydra" "$@"; }
hydra init --no-agent --trust >/dev/null
hydra spawn agent-fixture --no-agent >/dev/null
worker="$(hydra path agent-fixture)"
# shellcheck disable=SC1091
. "$root/tests/agent_builtin_cases.sh"
cat > "$fixture/provider" <<'PROVIDER'
#!/bin/sh
case "$1" in --help|--version) echo fixture-1.0; exit 0 ;; esac
case "$1" in
    --session) session="$2"; shift 2; printf %s "$session" > session-id ;;
    --resume) session="$2"; shift 2; [ "$(cat session-id)" = "$session" ] || exit 9 ;;
esac
[ -z "${session:-}" ] || printf '{"schema_version":1,"type":"session","session_id":"%s"}\n' "$session"
printf %s "$1" > prompt.txt
printf started >> starts
case "$1" in
    partial) printf '{"schema_version":1'; exit 0 ;;
    malformed) printf 'not json\n'; sleep 10; exit 0 ;;
    oversized) head -c 33000 /dev/zero | tr '\000' x; exit 0 ;;
    nul) printf '{"schema_version":1,"type":"observation","status":"failed"}\000x\n'; exit 0 ;;
    complete-only) printf '{"schema_version":1,"type":"observation","status":"idle"}\n'; exit 0 ;;
    permission) printf '{"schema_version":1,"type":"permission","request_id":"request_1"}\n'; sleep 10; exit 0 ;;
    cancel|stale) sleep 20; exit 0 ;;
    *) printf '{"schema_version":1,"type":"observation","status":"running"}\n' ;;
esac
printf '{"schema_version":1,"type":"result","text":"exact artifact"}\n'
printf '{"schema_version":1,"type":"observation","status":"idle"}\n'
PROVIDER
chmod +x "$fixture/provider"
cat > "$fixture/profile.json" <<EOF
{"schema_version":1,"executable":"$fixture/provider","argv":[{"input":"prompt"}],"prompt":"argument","session":"none","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["fixture-1.0"]}
EOF
hydra agent import fixture "$fixture/profile.json" >/dev/null
printf proof > "$fixture/prompt"
hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --require prompt,observations --result-file answer.txt --exit-code --json > "$fixture/run"
[ "$(cat "$worker/answer.txt")" = 'exact artifact' ]
run_id="$(sed -n 's/.*"run_id":"\([^" ]*\)".*/\1/p' "$fixture/run")"
record="$(find "$HYDRA_HOME/state/v2/projects" -path "*/exec/$run_id/*/agent.json" -print)"
grep -q '"verification_passed":false' "$record"
grep -q '"usage":null' "$record"
if grep -q 'exact artifact' "$fixture/run"; then exit 1; fi
[ -z "$(find "$HYDRA_HOME/state/v2/projects" -name agent-payloads -print)" ]
before="$(cat "$worker/starts")"
if hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --require resume > "$fixture/error" 2>&1; then exit 1; fi
[ "$(cat "$worker/starts")" = "$before" ]
observed_file="$(find "$HYDRA_HOME/state/v2/projects" -name observed-status -print)"
observed_before="$(cat "$observed_file")"
for mode in partial malformed oversized nul; do
    printf %s "$mode" > "$fixture/prompt"
    if hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --exit-code --json > "$fixture/$mode"; then exit 1; else code=$?; fi
    [ "$code" -eq 125 ]
    grep -q malformed_output "$fixture/$mode"
    [ "$(cat "$observed_file")" = "$observed_before" ]
done
printf complete-only > "$fixture/prompt"
if hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --result-file absent.txt --exit-code > "$fixture/missing"; then exit 1; fi
[ ! -e "$worker/absent.txt" ]
printf proof > "$fixture/prompt"
hydra send --delivery safe-point agent-fixture 'queued instruction' >/dev/null
hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --exit-code --retain-raw --json > "$fixture/steered"
grep -q 'queued instruction' "$worker/prompt.txt"
hydra recv --receipts agent-fixture --json > "$fixture/receipts"
grep -q '"status":"delivered"' "$fixture/receipts"
payload="$(find "$HYDRA_HOME/state/v2/projects" -path '*/agent-payloads/*/stdout' -print)"
grep -q 'exact artifact' "$payload"
printf permission > "$fixture/prompt"
if hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --exit-code --json > "$fixture/permission"; then exit 1; else code=$?; fi
[ "$code" -eq 3 ]
grep -q permission_required "$fixture/permission"
printf cancel > "$fixture/prompt"
if hydra exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --timeout 1 --exit-code > "$fixture/cancel"; then exit 1; else code=$?; fi
[ "$code" -eq 124 ]
for boundary in cancel stale; do
    printf %s "$boundary" > "$fixture/prompt"
    before="$(cat "$worker/starts")"
    "$root/bin/hydra" exec --branch agent-fixture --profile fixture --prompt-file "$fixture/prompt" --timeout 30 --exit-code --json > "$fixture/$boundary-live" &
    owner=$!
    attempt=0
    while [ "$(cat "$worker/starts")" = "$before" ] && [ "$attempt" -lt 100 ]; do sleep 0.05; attempt=$((attempt + 1)); done
    [ "$(cat "$worker/starts")" != "$before" ]
    if [ "$boundary" = cancel ]; then kill -TERM "$owner"; else
        current="$(find "$HYDRA_HOME/state/v2/projects" -path '*/heads/*/current-instance' -print)"
        instance="$(cat "$current")"
        printf 'instance_replacement\n' > "$current"
    fi
    if wait "$owner"; then exit 1; else code=$?; fi
    owner=""
    if [ "$boundary" = cancel ]; then [ "$code" -eq 143 ]; else
        printf '%s\n' "$instance" > "$current"
        [ "$code" -eq 125 ]
        grep -q stale_instance "$fixture/stale-live"
    fi
done
cat > "$fixture/session.json" <<EOF
{"schema_version":1,"executable":"$fixture/provider","argv":["--session",{"input":"session_id"},{"input":"prompt"}],"resume_argv":["--resume",{"input":"session_id"},{"input":"prompt"}],"prompt":"argument","session":"generated","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["fixture-1.0"]}
EOF
hydra agent import session-fixture "$fixture/session.json" >/dev/null
printf session-test > "$fixture/prompt"
hydra exec --branch agent-fixture --profile session-fixture --prompt-file "$fixture/prompt" --require resume --exit-code --json > "$fixture/session"
resume_id="$(sed -n 's/.*"run_id":"\([^"]*\)".*/\1/p' "$fixture/session")"
session_id="$(cat "$worker/session-id")"
hydra exec --branch agent-fixture --profile session-fixture --prompt-file "$fixture/prompt" --resume-run "$resume_id" --exit-code --json > "$fixture/resumed"
[ "$(cat "$worker/session-id")" = "$session_id" ]
grep -q "$session_id" "$fixture/resumed"
hydra spawn agent-consumer --no-agent >/dev/null
consumer="$(hydra path agent-consumer)"
printf 'produce an exact artifact' > initial-prompt
cat > "$fixture/data.json" <<'DATA'
{"schema_version":1,"inputs":{"initial":{"path":"initial-prompt","type":"file","max_bytes":1024}},"steps":{"producer":{"inputs":{"prompt":{"input":"initial"}},"outputs":{"product":{"path":"product.txt","type":"file","max_bytes":1024}}},"consumer":{"inputs":{"previous":{"step":"producer","output":"product"}},"outputs":{"copy":{"path":"copy.txt","type":"file","max_bytes":1024}}}}}
DATA
cat > "$fixture/agents.yml" <<'WORKFLOW'
version: 1
id: agent-artifacts
data: data.json
resources:
  disk_mb: 1
steps:
  - id: producer
    kind: exec
    idempotent: false
    args:
      head: agent-fixture
      profile: fixture
      prompt_input: prompt
      result_file: product.txt
      requires: [prompt, observations]
  - id: consumer
    kind: exec
    needs: [producer]
    idempotent: false
    args:
      head: agent-consumer
      profile: fixture
      prompt_input: previous
      result_file: copy.txt
      requires: [prompt]
  - id: independent
    kind: gate
    needs: [consumer]
    idempotent: true
    args:
      head: agent-consumer
      name: independent
      argv: [test, -f, independently-verified]
WORKFLOW
if hydra workflow run "$fixture/agents.yml" > "$fixture/workflow"; then exit 1; fi
workflow_id="$(sed -n '1p' "$fixture/workflow")"
workflow_dir="$(find "$HYDRA_HOME/state/v2/projects" -path "*/workflows/runs/$workflow_id" -type d -print)"
[ "$(cat "$workflow_dir/steps/producer/state")" = succeeded ]
[ "$(cat "$workflow_dir/steps/consumer/state")" = succeeded ]
[ "$(cat "$workflow_dir/steps/independent/state")" = failed ]
cmp "$workflow_dir/steps/producer/attempt-1/artifacts/product" "$consumer/prompt.txt"
cmp "$workflow_dir/steps/producer/attempt-1/artifacts/product" "$workflow_dir/steps/consumer/attempt-1/artifacts/copy"
printf 'Agent execution: exact prompt/result, unknown usage, independent verification, capability refusal and malformed/partial events passed\n'
