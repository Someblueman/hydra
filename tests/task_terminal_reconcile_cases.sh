#!/bin/sh
# Sourced after the shared acceptance fixture's original identity/count checks.
# shellcheck disable=SC2154
# Simulate dependency availability only; no terminal session is created.
cat > "$fixture/bin/tmux" <<'TMUX'
#!/bin/sh
[ "$1" = -V ] || exit 99
printf 'tmux 3.3\n'
TMUX
chmod +x "$fixture/bin/tmux"
python3 - "$fixture/spec" "$fixture/terminal-live-spec" <<'PY'
import json, sys
spec = json.load(open(sys.argv[1]))
spec['capabilities'] = ['exec', 'tmux']
spec['work']['argv'] = ['sh', '-c', 'printf "executed\\n" >> "$HYDRA_HOME/terminal-executions"']
with open(sys.argv[2], 'w') as out:
    json.dump(spec, out)
PY
task prepare --source "$fixture/source" --spec "$fixture/terminal-live-spec" --output "$fixture/terminal-live-package" > "$fixture/terminal-live-preview"
terminal_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/terminal-live-preview")"
: > "$fixture/transport/lose-ack"
if task submit build --input "$fixture/terminal-live-package" --key terminal-lost --trust-spec "$terminal_digest" > "$fixture/terminal-lost"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/terminal-lost"
terminal_id="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/transport/lost-response")"
[ -n "$terminal_id" ]
rm "$fixture/bin/tmux"
! command -v tmux >/dev/null 2>&1
terminal_count="$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')"
task submit build --input "$fixture/terminal-live-package" --key terminal-lost --trust-spec "$terminal_digest" > "$fixture/terminal-reconciled"
grep -q "\"task_id\":\"$terminal_id\"" "$fixture/terminal-reconciled"
if task submit build --input "$fixture/execution-headless-package" --key terminal-lost > "$fixture/error"; then exit 1; fi
grep -q '"code":"submission_conflict"' "$fixture/error"
if task submit build --input "$fixture/terminal-live-package" --key terminal-new > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
[ "$(find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' ')" = "$terminal_count" ]
terminal_attempt=0
while [ "$terminal_attempt" -lt 150 ]; do
    task status build --id "$terminal_id" > "$fixture/terminal-status"
    if grep -q '"result_state":"ready"' "$fixture/terminal-status"; then break; fi
    sleep 0.1
    terminal_attempt=$((terminal_attempt + 1))
done
grep -q '"state":"succeeded"' "$fixture/terminal-status"
[ "$(cat "$fixture/host/terminal-executions")" = executed ]
[ "$(wc -l < "$fixture/host/terminal-executions" | tr -d ' ')" = 1 ]
printf '%s\n' 'Capability disappearance: lost-ack retry preserves identity and single execution; conflicting and new requests reject'
