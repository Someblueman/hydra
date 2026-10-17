#!/bin/sh
# Sourced by tests/test_placement.sh: receiver refusal, racing submitters,
# withdrawal transfer, lost withdrawal responses, generation fencing and
# validator separation. Expects the fixture helpers of the parent test.
# shellcheck disable=SC2154
wait_state() {
    # $1 host, $2 task, $3 expected runtime state
    _ws_tries=0
    while [ "$_ws_tries" -lt 150 ]; do
        task status "$1" --id "$2" > "$fixture/wait-status"
        if grep -q "\"runtime\":{\"schema_version\":1,\"state\":\"$3\"" "$fixture/wait-status"; then return 0; fi
        sleep 0.1; _ws_tries=$((_ws_tries + 1))
    done
    cat "$fixture/wait-status" >&2
    return 1
}
holders() { find "$fixture"/host-*/fleet/tasks -maxdepth 1 -name "$1" | wc -l | tr -d ' '; }
wait_sealed() {
    # $1 host, $2 task: the receiver-completion snapshot is sealed after success.
    _wsl_tries=0
    while [ "$_wsl_tries" -lt 150 ]; do
        task status "$1" --id "$2" > "$fixture/wait-status"
        if grep -q '"result_state":"ready"' "$fixture/wait-status"; then return 0; fi
        if grep -q '"result_state":"unavailable"' "$fixture/wait-status"; then break; fi
        sleep 0.1; _wsl_tries=$((_wsl_tries + 1))
    done
    cat "$fixture/wait-status" >&2
    return 1
}

# Receiver admission is final: bravo looks idle when observed, but its queue
# fills before the dispatch arrives. The definitive refusal is recorded and
# the next-ranked host takes the task.
admission alpha release occupied --confirmed >/dev/null
admission bravo configure 1 1 0 1 gpu >/dev/null
placement drain alpha >/dev/null
cat > "$fixture/transport/fill-bravo-host" <<EOF
HYDRA_HOME="$fixture/host-bravo" "$root/bin/hydra" admission request filler-one project_fill 600 -
HYDRA_HOME="$fixture/host-bravo" "$root/bin/hydra" admission request filler-two project_fill 600 -
EOF
task submit --placement auto --input "$fixture/package" --key refused --trust-spec "$digest" > "$fixture/refused"
has "$fixture/refused" '"assigned_host":"charlie"'
refused_id="$(field "$fixture/refused" task_id)"
placement explain "$refused_id" > "$fixture/explain"
has "$fixture/explain" '"host":"bravo","outcome":"refused"'
has "$fixture/explain" '"code":"queue_full","task_state":"failed"'
has "$fixture/explain" '"host":"charlie","outcome":"accepted"'
has "$fixture/explain" '"reservation":"accepted"'
[ "$(holders "$refused_id")" -eq 2 ]
wait_state charlie "$refused_id" succeeded
task status bravo --id "$refused_id" | grep -q '"failure":"queue_full"'
placement replay "$refused_id" | grep -q '"identical":true'
admission bravo release filler-one --confirmed >/dev/null
admission bravo cancel filler-two >/dev/null
admission bravo configure 1 1 0 4 gpu >/dev/null
placement undrain alpha >/dev/null

# Concurrent submitters with different keys all land exactly once; a shared
# key converges on one receiver because submissions for a key serialize.
pids=""
for n in 1 2 3 4; do
    task submit --placement auto --input "$fixture/package" --key "race-$n" > "$fixture/race-$n" &
    pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
for n in 1 2 3 4; do
    has "$fixture/race-$n" '"state":"accepted"'
    race_id="$(field "$fixture/race-$n" task_id)"
    [ "$(holders "$race_id")" -eq 1 ]
    [ -d "$fixture/host-$(field "$fixture/race-$n" assigned_host)/fleet/tasks/$race_id" ]
    placement replay "$race_id" | grep -q '"identical":true'
done
pids=""
for n in 1 2 3; do
    task submit --placement auto --input "$fixture/package" --key race-same > "$fixture/same-$n" &
    pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
same_id="$(field "$fixture/same-1" task_id)"
[ -n "$same_id" ]
for n in 2 3; do [ "$(field "$fixture/same-$n" task_id)" = "$same_id" ]; done
[ "$(holders "$same_id")" -eq 1 ]
placement explain "$same_id" > "$fixture/explain"
[ "$(grep -o '"outcome":"accepted"' "$fixture/explain" | wc -l | tr -d ' ')" -ge 1 ]
has "$fixture/explain" '"generation":1'

# Rebalancing accepted-not-started work: a durable withdrawal blocks the old
# receiver, then a new generation is placed elsewhere and fenced.
task submit --placement auto --input "$fixture/package" --key move > "$fixture/move"
move_id="$(field "$fixture/move" task_id)"
first_host="$(field "$fixture/move" assigned_host)"
[ "$first_host" = alpha ]
placement withdraw "$move_id" > "$fixture/withdraw"
has "$fixture/withdraw" '"state":"withdrawn"'
has "$fixture/withdraw" '"withdrawal":"confirmed"'
[ -f "$fixture/host-alpha/fleet/tasks/$move_id/withdrawal.json" ]
fails task start alpha --id "$move_id" --trust-spec "$digest"
has "$fixture/error" '"code":"withdrawn"'
[ ! -e "$fixture/host-alpha/fleet/tasks/$move_id/launch.json" ]
fails placement withdraw "$move_id"
has "$fixture/error" '"code":"invalid_state"'
task submit --placement auto --input "$fixture/package" --key move > "$fixture/move-two"
has "$fixture/move-two" '"generation":2'
second_host="$(field "$fixture/move-two" assigned_host)"
[ "$second_host" = bravo ]
placement explain "$move_id" > "$fixture/explain"
has "$fixture/explain" '"host":"alpha","eligible":false,"reasons":\["previous_generation","not_observed"\]'
has "$fixture/explain" '"generation":2'
bound="$(field "$fixture/explain" bound_spec_sha256)"
[ "$bound" != "$digest" ]
task status bravo --id "$move_id" | grep -q '"assignment_generation":2'
# Stale-update rejection at the receiver, independent of the client record.
printf '{"protocol":1,"action":"task","operation":"start","task_id":"%s","trust_spec":"%s","assignment_generation":1}' "$move_id" "$bound" |
    HYDRA_HOME="$fixture/host-bravo" "$HYDRA_FLEET_BIN" fleet serve > "$fixture/stale" || true
has "$fixture/stale" '"code":"stale_generation"'
[ ! -e "$fixture/host-bravo/fleet/tasks/$move_id/launch.json" ]
task start bravo --id "$move_id" --trust-spec "$bound" > "$fixture/started"
wait_state bravo "$move_id" succeeded
fails placement withdraw "$move_id"
has "$fixture/error" '"code":"execution_owned"'
placement explain "$move_id" | grep -q '"assigned_host":"bravo"'
wait_sealed bravo "$move_id"
task collect bravo --id "$move_id" --into "$fixture/source" > "$fixture/collected"
has "$fixture/collected" '"collection_id":"collection_'
placement replay "$move_id" | grep -q '"automatic_rounds":2'

# A lost withdrawal response blocks transfer until a rerun confirms it.
task submit --placement auto --input "$fixture/package" --key lost > "$fixture/lost"
lost_id="$(field "$fixture/lost" task_id)"
: > "$fixture/transport/lose-ack"
fails placement withdraw "$lost_id"
has "$fixture/error" '"code":"outcome_unknown"'
placement explain "$lost_id" | grep -q '"withdrawal":{"state":"unknown"'
fails task submit --placement auto --input "$fixture/package" --key lost
has "$fixture/error" '"code":"withdrawal_unconfirmed"'
[ "$(holders "$lost_id")" -eq 1 ]
placement withdraw "$lost_id" | grep -q '"withdrawal":"confirmed"'
task submit --placement auto --input "$fixture/package" --key lost | grep -q '"generation":2'
[ "$(holders "$lost_id")" -eq 2 ]

# Validator separation keeps a validator off its producer's host.
task submit --placement auto --input "$fixture/package" --key producer > "$fixture/producer"
producer_id="$(field "$fixture/producer" task_id)"
producer_host="$(field "$fixture/producer" assigned_host)"
spec "$fixture/validator-spec" alpha '["exec","role.validator"]' '["true"]'
task prepare --source "$fixture/source" --spec "$fixture/validator-spec" --output "$fixture/validator-package" > "$fixture/validator-preview"
task submit --placement auto --input "$fixture/validator-package" --key validator --separate-from "$producer_id" > "$fixture/validator"
[ "$(field "$fixture/validator" assigned_host)" != "$producer_host" ]
placement explain "$(field "$fixture/validator" task_id)" > "$fixture/explain"
has "$fixture/explain" "\"host\":\"$producer_host\",\"eligible\":false,\"reasons\":\[\"validator_separation\"\]"
has "$fixture/explain" '"task_class":"validator"'
fails task submit --placement auto --input "$fixture/validator-package" --key orphan --separate-from "$pinned_id"
has "$fixture/error" '"code":"separation_unresolved"'
