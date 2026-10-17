#!/bin/sh
# Automatic placement over a controlled SSH boundary with three real receivers:
# filtering, deterministic ranking, drain, stale capacity, definitive refusal
# then re-placement, racing submitters, withdrawal with lost responses,
# generation fencing, validator separation, and replay identity.
set -eu
umask 002
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    for cleanup_home in "$fixture"/host-*; do
        [ -d "$cleanup_home" ] || continue
        workflow_task_fixture_quiesce "$cleanup_home" || return 1
    done
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then printf 'Preserved placement fixture: %s\n' "$fixture" >&2; else rm -rf "$fixture"; fi
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-fleet or make test-one T=placement}"
HYDRA_HOME="$fixture/client"
HYDRA_TEST_TRANSPORT="$fixture/transport"
HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1
export HYDRA_FLEET_BIN HYDRA_HOME HYDRA_TEST_TRANSPORT HYDRA_NONINTERACTIVE HYDRA_SKIP_AI
mkdir -p "$fixture/source" "$fixture/receiver" "$fixture/transport" "$fixture/bin"
# The fake ssh runs the remote command locally. Markers under the transport
# directory inject outages, lost acknowledgments and a queue that fills
# between observation and dispatch.
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -f "$HYDRA_TEST_TRANSPORT/offline-$1" ]; then echo 'Connection refused' >&2; exit 255; fi
request="$(mktemp "$HYDRA_TEST_TRANSPORT/request.XXXXXX")"
trap 'rm -f "$request"' 0
cat > "$request"
if [ -f "$HYDRA_TEST_TRANSPORT/fill-$1" ] && grep -q '"operation":"submit"' "$request"; then
    sh "$HYDRA_TEST_TRANSPORT/fill-$1" >/dev/null 2>&1
    rm -f "$HYDRA_TEST_TRANSPORT/fill-$1"
fi
if [ -f "$HYDRA_TEST_TRANSPORT/lose-ack" ] && grep -Eq '"operation":"(submit|start|withdraw)"' "$request"; then
    /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_TRANSPORT/lost-response"
    rm "$HYDRA_TEST_TRANSPORT/lose-ack"
    exit 255
fi
exec /bin/sh -c "$2" < "$request"
SSH
chmod +x "$fixture/bin/ssh"
# A receiver whose capacity observation is always stale.
cat > "$fixture/bin/stale-hydra" <<EOF
#!/bin/sh
"$root/bin/hydra" "\$@" | sed 's/"observed_at":[0-9]*/"observed_at":1/'
EOF
chmod +x "$fixture/bin/stale-hydra"
PATH="$fixture/bin:$PATH"
export PATH
for repository in source receiver; do
    git -C "$fixture/$repository" init -q
    git -C "$fixture/$repository" config user.name 'Placement test'
    git -C "$fixture/$repository" config user.email placement@example.invalid
    git -C "$fixture/$repository" -c commit.gpgSign=false commit --allow-empty -qm initial
done
for host in alpha bravo charlie delta; do
    (cd "$fixture/receiver" && HYDRA_HOME="$fixture/host-$host" "$root/bin/hydra" init --no-agent --json) > "$fixture/init-$host"
    "$root/bin/hydra" remote add "$host" "$host-host" --hydra "$root/bin/hydra" --home "$fixture/host-$host" >/dev/null
done
"$root/bin/hydra" remote add delta delta-host --hydra "$fixture/bin/stale-hydra" --home "$fixture/host-delta" >/dev/null
admission() { _h="$1"; shift; HYDRA_HOME="$fixture/host-$_h" "$root/bin/hydra" admission "$@"; }
admission alpha configure 2 2 0 4 gpu >/dev/null
admission bravo configure 2 2 0 4 gpu >/dev/null
admission charlie configure 1 1 0 4 - >/dev/null
admission delta configure 4 4 0 4 - >/dev/null
commit="$(git -C "$fixture/source" rev-parse HEAD)"
spec() {
    # $1 output, $2 host, $3 capabilities JSON, $4 argv JSON
    cat > "$1" <<EOF
{"schema_version":1,"host":"$2","project":"$fixture/receiver",
"source":{"commit":"$commit"},"work":{"kind":"exec","argv":$4},
"inputs":[],"outputs":[],"capabilities":$3,"completion":"command-exit",
"limits":{"transport_seconds":30,"queue_seconds":60,"startup_seconds":60,
"execution_seconds":120,"cancellation_seconds":10,"log_bytes":4096,"artifact_bytes":4096}}
EOF
}
task() { "$root/bin/hydra" fleet task "$@"; }
placement() { "$root/bin/hydra" fleet placement "$@"; }
field() { sed -n "s/.*\"$2\":\"\([^\"]*\)\".*/\1/p" "$1" | head -n 1; }
has() { grep -q "$2" "$1" || { printf 'Missing %s in %s:\n' "$2" "$1" >&2; cat "$1" >&2; exit 1; }; }
fails() { if "$@" > "$fixture/error" 2>&1; then printf 'Expected failure: %s\n' "$*" >&2; cat "$fixture/error" >&2; exit 1; fi; }
spec "$fixture/spec" alpha '["exec"]' '["true"]'
task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/package" > "$fixture/preview"
# shellcheck disable=SC2034 # used by the sourced transfer cases
digest="$(field "$fixture/preview" spec_sha256)"

# Default explicit submission is unchanged and records nothing.
task submit alpha --input "$fixture/package" --key pinned > "$fixture/pinned"
has "$fixture/pinned" '"state":"accepted"'
has "$fixture/pinned" '"assignment_generation":1'
pinned_id="$(field "$fixture/pinned" task_id)"
fails placement explain "$pinned_id"
has "$fixture/error" '"code":"not_found"'
fails task submit --input "$fixture/package" --key nohost
has "$fixture/error" '"code":"invalid_input"'
fails task submit bravo --input "$fixture/package" --key wrong --placement bravo
has "$fixture/error" '"code":"placement_mismatch"'

# Explicit pinning through --placement records an explicit decision.
task submit alpha --input "$fixture/package" --key explicit --placement alpha > "$fixture/explicit"
has "$fixture/explicit" '"placement":{"task_id":"task_'
has "$fixture/explicit" '"mode":"explicit"'
explicit_id="$(field "$fixture/explicit" task_id)"
placement explain "$explicit_id" > "$fixture/explain"
has "$fixture/explain" '"decision":{"host":"alpha","reason":"explicit"'
placement replay "$explicit_id" | grep -q '"identical":true'

# Automatic placement: alpha already holds one of two slots, delta is stale,
# charlie and bravo are idle; bravo wins the alias-order tie-break.
admission alpha request occupied project_fixture 600 - > /dev/null
task submit --placement auto --input "$fixture/package" --key auto-one > "$fixture/auto-one"
has "$fixture/auto-one" '"state":"accepted"'
has "$fixture/auto-one" '"assigned_host":"bravo"'
auto_one="$(field "$fixture/auto-one" task_id)"
[ -d "$fixture/host-bravo/fleet/tasks/$auto_one" ]
[ ! -d "$fixture/host-alpha/fleet/tasks/$auto_one" ]
placement explain "$auto_one" > "$fixture/explain"
has "$fixture/explain" '"schema_version":1'
has "$fixture/explain" '"policy_version":1'
has "$fixture/explain" '"host":"bravo","eligible":true,"reasons":\[\]'
has "$fixture/explain" '"host":"delta","eligible":false,"reasons":\["stale_observation"\]'
has "$fixture/explain" '"decision":{"host":"bravo","reason":"ranked"'
has "$fixture/explain" '"outcome":"accepted"'
has "$fixture/explain" '"used_in_ranking":false'
grep -o '"host":"[a-z]*","eligible":true,"reasons":\[\],"observation_age_seconds":[0-9]*,"score":{[^}]*},"estimates":{[^}]*},"rank":[0-9]' "$fixture/explain" > "$fixture/ranks"
grep -q '"host":"bravo".*"rank":1' "$fixture/ranks"
grep -q '"host":"charlie".*"rank":2' "$fixture/ranks"
grep -q '"host":"alpha".*"rank":3' "$fixture/ranks"
placement replay "$auto_one" > "$fixture/replay"
has "$fixture/replay" '"identical":true'
has "$fixture/replay" '"automatic_rounds":1'
# Resubmitting the same key reconciles with the assigned receiver only.
task submit --placement auto --input "$fixture/package" --key auto-one > "$fixture/auto-one-again"
has "$fixture/auto-one-again" "\"task_id\":\"$auto_one\""
has "$fixture/auto-one-again" '"assigned_host":"bravo"'
[ "$(find "$fixture"/host-*/fleet/tasks -maxdepth 1 -name "$auto_one" | wc -l | tr -d ' ')" -eq 1 ]
fails task submit --placement auto --input "$fixture/package" --key explicit
has "$fixture/error" '"code":"placement_conflict"'
# A tampered record is refused rather than trusted.
cp "$fixture/client/fleet/placements/$auto_one/placement.json" "$fixture/saved-record"
sed 's/"reason":"ranked"/"reason":"edited"/' "$fixture/saved-record" > "$fixture/client/fleet/placements/$auto_one/placement.json"
fails placement explain "$auto_one"
has "$fixture/error" '"code":"not_found"'
cp "$fixture/saved-record" "$fixture/client/fleet/placements/$auto_one/placement.json"

# Drain and outages exclude hosts; an idle incompatible host is not eligible.
placement drain bravo | grep -q '"drained":true'
: > "$fixture/transport/offline-charlie-host"
spec "$fixture/gpu-spec" alpha '["exec","label.gpu"]' '["true"]'
task prepare --source "$fixture/source" --spec "$fixture/gpu-spec" --output "$fixture/gpu-package" > "$fixture/gpu-preview"
task submit --placement auto --input "$fixture/gpu-package" --key auto-gpu > "$fixture/auto-gpu"
has "$fixture/auto-gpu" '"assigned_host":"alpha"'
auto_gpu="$(field "$fixture/auto-gpu" task_id)"
placement explain "$auto_gpu" > "$fixture/explain"
has "$fixture/explain" '"host":"bravo","eligible":false,"reasons":\["drained"\]'
has "$fixture/explain" '"host":"charlie","eligible":false,"reasons":\["offline"\]'
has "$fixture/explain" '"host":"delta","eligible":false,"reasons":\["label_unsupported","stale_observation"\]'
rm "$fixture/transport/offline-charlie-host"
placement undrain bravo | grep -q '"drained":false'
placement policy > "$fixture/policy"
has "$fixture/policy" '"max_queued":8'
has "$fixture/policy" '"validator_reserve_slots":0'
has "$fixture/policy" '"drained_hosts":\[\]'
printf '{"max_queued":"many"}\n' > "$fixture/client/fleet/placement-policy.json"
fails task submit --placement auto --input "$fixture/package" --key bad-policy
has "$fixture/error" '"code":"policy_invalid"'
printf '{"policy_version":1,"max_queued":0}\n' > "$fixture/client/fleet/placement-policy.json"
placement policy | grep -q '"max_queued":0'
rm "$fixture/client/fleet/placement-policy.json"
# shellcheck source=/dev/null
. "$root/tests/placement_transfer_cases.sh"
printf 'PASS placement: filtering, ranking, drain, stale capacity, refusal, racing, withdrawal, fencing, replay\n'
