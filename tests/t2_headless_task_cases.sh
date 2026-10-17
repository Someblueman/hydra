#!/bin/sh
# Sourced by test_t2_headless.sh: one command task on the tmux-less receiver.
# Duplicate submission, lost start response, single execution, bounded logs,
# exact result collection on the coordinator.
: "${fixture:?}" "${root:?}" "${receiver_path:?}"
sed -e "s@\[\"true\"\]@[\"sh\",\"-c\",\"printf once >> $fixture/executions; printf live-worker-output; head -c 5000 /dev/zero; printf headless > result.txt\"]@" \
    -e 's/"outputs":\[\]/"outputs":["result.txt"]/' "$fixture/spec" > "$fixture/command-spec"
task prepare --source "$fixture/source" --spec "$fixture/command-spec" --output "$fixture/command-package" > "$fixture/command-preview"
command_digest="$(json_field spec_sha256 "$fixture/command-preview")"
# Six simultaneous duplicate submissions converge on one immutable acceptance.
pids=""
for n in 1 2 3 4 5 6; do
    task submit build --input "$fixture/command-package" --key command > "$fixture/command-receipt-$n" &
    pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
command_id="$(json_field task_id "$fixture/command-receipt-1")"
[ -n "$command_id" ]
for n in 2 3 4 5 6; do cmp "$fixture/command-receipt-1" "$fixture/command-receipt-$n"; done
[ "$(acceptances)" -eq 1 ]
task status build --id "$command_id" > "$fixture/command-status"
grep -q '"state":"accepted"' "$fixture/command-status"
# A lost start response is unknown, never a reason to launch twice.
if task start build --id "$command_id" --trust-spec wrong > "$fixture/error"; then exit 1; fi
grep -q '"code":"trust_required"' "$fixture/error"
: > "$fixture/transport/lose-ack"
if task start build --id "$command_id" --trust-spec "$command_digest" > "$fixture/error"; then exit 1; fi
grep -q '"code":"outcome_unknown"' "$fixture/error"
await "$command_id" '"state":"succeeded"' "$fixture/command-status"
grep -q '"run_id":"run_' "$fixture/command-status"
[ "$(cat "$fixture/executions")" = once ]
task start build --id "$command_id" --trust-spec "$command_digest" > "$fixture/command-restart"
grep -q '"state":"succeeded"' "$fixture/command-restart"
[ "$(cat "$fixture/executions")" = once ]
[ -f "$fixture/host/fleet/tasks/$command_id/provenance.json" ]
[ -f "$fixture/host/fleet/tasks/$command_id/attempt.json" ]
# The execution head is terminal-free in durable state, with no session.
execution_project="$(json_field execution_project_id "$fixture/host/fleet/tasks/$command_id/state.json")"
[ -n "$execution_project" ]
for mode in "$fixture/host/state/v2/projects/$execution_project/heads"/head_*/terminal-mode; do
    [ "$(sed -n '1p' "$mode")" = headless ]
    [ "$(sed -n '1p' "$(dirname "$mode")/session")" = - ]
done
# Run/step/attempt visibility through the CLI without attaching to anything.
task observe build --id "$command_id" > "$fixture/command-observation"
grep -q '"execution_state":"succeeded"' "$fixture/command-observation"
grep -q '"execution_owner":{"kind":"detached-owner"' "$fixture/command-observation"
grep -q '"run_id":"run_' "$fixture/command-observation"
# Bounded logs: owner stream by byte range, recorded worker stream capped at log_bytes.
task logs build --id "$command_id" --limit 16 > "$fixture/log-chunk"
expected="$(od -An -tx1 -N16 "$fixture/host/fleet/tasks/$command_id/stdout" | tr -d ' \n')"
grep -q "\"hex\":\"$expected\"" "$fixture/log-chunk"
grep -q '"next_offset":16' "$fixture/log-chunk"
task logs build --id "$command_id" --offset 16 --limit 16 > "$fixture/log-chunk"
grep -q '"next_offset":32' "$fixture/log-chunk"
if task logs build --id "$command_id" --offset -1 >/dev/null; then exit 1; fi
if task logs build --id "$command_id" --limit 65537 >/dev/null; then exit 1; fi
task logs build --id "$command_id" --source work > "$fixture/worker-logs"
grep -q '"available_bytes":4096' "$fixture/worker-logs"
grep -q '"limit_reached":true' "$fixture/worker-logs"
grep -q '"hex":"6c6976652d776f726b65722d6f757470757400' "$fixture/worker-logs"
task logs build --id "$command_id" --source work --offset 4096 > "$fixture/worker-end"
grep -q '"hex":""' "$fixture/worker-end"
# Exact output: sealed on the receiver, verified and collected on the coordinator.
await "$command_id" '"result_state":"ready"' "$fixture/command-status"
task result build --id "$command_id" > "$fixture/command-result-preview"
grep -q '"path":"result.txt"' "$fixture/command-result-preview"
grep -q '"path":"attempt.json"' "$fixture/command-result-preview"
grep -q '"dirty":true' "$fixture/command-result-preview"
task result build --id "$command_id" --output "$fixture/command-result" >/dev/null
task inspect-result --input "$fixture/command-result" > "$fixture/command-inspected"
grep -q '"path":"result.txt"' "$fixture/command-inspected"
if grep -Eq '"(hex|bundle_hex)":' "$fixture/command-inspected"; then exit 1; fi
git clone -q "$fixture/source" "$fixture/collector"
task collect --input "$fixture/command-result" --into "$fixture/collector" > "$fixture/collected" || { cat "$fixture/collected"; exit 1; }
collection_path="$(sed -n 's/.*"path":"\([^"]*\)".*/\1/p' "$fixture/collected" | sed 's@\\/@/@g')"
printf headless > "$fixture/expected-command-result"
cmp "$fixture/expected-command-result" "$collection_path/artifacts/result.txt"
# A result file edited after sealing never changes the snapshot the coordinator sees.
for output in "$fixture/host/fleet/tasks/$command_id"/heads/*/result.txt; do printf changed > "$output"; done
task result build --id "$command_id" --output "$fixture/command-result-again" > "$fixture/command-result-preview-again"
cmp "$fixture/command-result" "$fixture/command-result-again"
printf 'T2: command task deduplicated, lost start reconciled, single execution, bounded logs and exact collection passed\n'
