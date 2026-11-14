#!/bin/sh
# T2 qualification: remote execution and planning with tmux absent on the
# execution host. The coordinator keeps its own PATH (tmux may be present);
# every receiver command runs with a private PATH that has no tmux and a tmux
# stand-in that refuses terminal operations loudly. The SSH boundary is the
# controlled transport the DAG fleet cases use; see docs/T2_T3_QUALIFICATION.md.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
fixture="$(mktemp -d "${TMPDIR:-/tmp}/hydra-t2.XXXXXX")"
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    [ -z "${owned_group:-}" ] || kill -KILL "-$owned_group" 2>/dev/null || :
    [ -z "${owned_owner:-}" ] || kill -KILL "$owned_owner" 2>/dev/null || :
    for cleanup_home in "$fixture/host" "$fixture/local-home"; do
        [ -d "$cleanup_home" ] || continue
        workflow_task_fixture_quiesce "$cleanup_home" || return 1
        for workspace in "$cleanup_home"/fleet/tasks/task_*/workspace; do
            [ -f "$workspace/.git/hydra/project-id" ] || continue
            (cd "$workspace" && env PATH="$receiver_path" HYDRA_HOME="$cleanup_home" "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
        done
    done
    test_tmux_fixture_cleanup "$fixture" || return 1
    if [ "${passed:-0}" = 1 ] && [ "${HYDRA_TEST_KEEP_FIXTURE:-0}" != 1 ]; then
        rm -rf "$fixture"
    else
        printf 'T2 headless evidence: %s\n' "$fixture" >&2
    fi
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-t2-headless or make test-one T=<name>}"
HYDRA_HOME="$fixture/client"
export HYDRA_FLEET_BIN HYDRA_HOME HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1

# Split PATH: the receiver side never sees a working tmux.
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
receiver_path="$fixture/receiver-path"
headless_receiver_path "$receiver_path" "$fixture/tmux-refused"
coordinator_tmux="$(tmux -V 2>/dev/null || printf 'absent')"
printf 'Coordinator tmux: %s\n' "$coordinator_tmux"
[ "$(env PATH="$receiver_path" sh -c 'command -v tmux')" = "$receiver_path/tmux" ]
if env PATH="$receiver_path" tmux -V >/dev/null 2>&1; then exit 1; fi
if env PATH="$receiver_path" tmux new-session -d -s must-not-run >/dev/null 2>&1; then exit 1; fi
grep -q '^tmux new-session' "$fixture/tmux-refused"
: > "$fixture/tmux-refused"

# Controlled SSH boundary: the remote command runs under the receiver PATH.
mkdir -p "$fixture/bin" "$fixture/transport" "$fixture/source" "$fixture/receiver"
HYDRA_TEST_TRANSPORT="$fixture/transport" HYDRA_TEST_RECEIVER_PATH="$receiver_path"
export HYDRA_TEST_TRANSPORT HYDRA_TEST_RECEIVER_PATH
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
if [ -f "$HYDRA_TEST_TRANSPORT/offline" ]; then exit 255; fi
request="$(mktemp "$HYDRA_TEST_TRANSPORT/request.XXXXXX")"
trap 'rm -f "$request"' 0
cat > "$request"
if [ -f "$HYDRA_TEST_TRANSPORT/lose-ack" ] && grep -Eq '"operation":"(submit|start|cancel)"' "$request"; then
    env PATH="$HYDRA_TEST_RECEIVER_PATH" /bin/sh -c "$2" < "$request" > "$HYDRA_TEST_TRANSPORT/lost-response"
    rm "$HYDRA_TEST_TRANSPORT/lose-ack"
    exit 255
fi
exec env PATH="$HYDRA_TEST_RECEIVER_PATH" /bin/sh -c "$2" < "$request"
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH

task() { "$root/bin/hydra" fleet task "$@"; }
receiver_hydra() { env PATH="$receiver_path" HYDRA_HOME="$fixture/host" "$root/bin/hydra" "$@"; }
json_field() { sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p" "$2" | sed -n '1p'; }
acceptances() { find "$fixture/host/fleet/tasks" -name acceptance.json | wc -l | tr -d ' '; }
# Poll the receiver through the alias until a pattern appears in its status.
await() {
    _aw_polls=0
    while [ "$_aw_polls" -lt "${4:-300}" ]; do
        task status build --id "$1" > "$3"
        if grep -q "$2" "$3"; then return 0; fi
        if grep -Eq '"state":"(failed|outcome_unknown)"|"result_state":"unavailable"' "$3"; then break; fi
        sleep 0.2
        _aw_polls=$((_aw_polls + 1))
    done
    cat "$3" >&2
    [ ! -f "$fixture/host/fleet/tasks/$1/stderr" ] || cat "$fixture/host/fleet/tasks/$1/stderr" >&2
    return 1
}

for repository in source receiver; do
    git -C "$fixture/$repository" init -q
    git -C "$fixture/$repository" config user.name 'T2 test'
    git -C "$fixture/$repository" config user.email t2@example.invalid
    git -C "$fixture/$repository" config commit.gpgSign false
done
cp "$root/tests/fixtures/plan-task/repo/"* "$fixture/source/"
git -C "$fixture/source" add .
git -C "$fixture/source" commit -qm recipes
git -C "$fixture/receiver" commit --allow-empty -qm initial
commit="$(git -C "$fixture/source" rev-parse HEAD)"
# The receiving host initialises its project with tmux absent.
(cd "$fixture/receiver" && receiver_hydra init --no-agent --json) > "$fixture/receiver-init"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$fixture/host" >/dev/null
cat > "$fixture/spec" <<EOF
{"schema_version":1,"host":"build","project":"$fixture/receiver",
"source":{"commit":"$commit"},"work":{"kind":"exec","argv":["true"]},
"inputs":[],"outputs":[],"capabilities":["exec","execution-headless"],"completion":"command-exit",
"limits":{"transport_seconds":30,"queue_seconds":60,"startup_seconds":60,
"execution_seconds":120,"cancellation_seconds":10,"log_bytes":4096,"artifact_bytes":4096}}
EOF

# Capability negotiation: the tmux-less receiver advertises headless execution
# and no terminal; the coordinator reaches it through the alias.
printf '{"protocol":1,"action":"handshake"}' | receiver_hydra fleet serve > "$fixture/receiver-handshake"
grep -q '"execution-headless"' "$fixture/receiver-handshake"
if grep -q '"tmux"' "$fixture/receiver-handshake"; then exit 1; fi
"$root/bin/hydra" fleet list --json > "$fixture/fleet-list"
grep -q '"host":"build"' "$fixture/fleet-list"
grep -q '"ok":true' "$fixture/fleet-list"
(cd "$fixture/receiver" && receiver_hydra doctor) > "$fixture/receiver-doctor" || :
grep -q 'headless execution does not require tmux' "$fixture/receiver-doctor"
# Interactive terminals are refused before any terminal operation.
if (cd "$fixture/receiver" && receiver_hydra spawn --no-agent interactive-head) > "$fixture/interactive-spawn" 2>&1; then exit 1; fi
grep -q "tmux" "$fixture/interactive-spawn"
[ ! -s "$fixture/tmux-refused" ]
# A terminal requirement is a typed refusal before acceptance; headless is explicit.
sed 's/"capabilities":\["exec","execution-headless"\]/"capabilities":["exec","tmux"]/' "$fixture/spec" > "$fixture/terminal-spec"
task prepare --source "$fixture/source" --spec "$fixture/terminal-spec" --output "$fixture/terminal-package" >/dev/null
if task submit build --input "$fixture/terminal-package" --key terminal-required > "$fixture/error"; then exit 1; fi
grep -q '"code":"capability_unavailable"' "$fixture/error"
[ "$(acceptances)" -eq 0 ]
[ ! -s "$fixture/tmux-refused" ]
printf 'T2: terminal capability refused before launch; headless receiver negotiated\n'

# shellcheck source=/dev/null
. "$root/tests/t2_headless_task_cases.sh"
# shellcheck source=/dev/null
. "$root/tests/t2_headless_adapter_cases.sh"
# shellcheck source=/dev/null
. "$root/tests/t2_headless_flow_cases.sh"
# shellcheck source=/dev/null
. "$root/tests/t2_headless_control_cases.sh"

# No receiver-side command performed a terminal operation; dependency probes
# (tmux -V) did run and reported absence.
if [ -s "$fixture/tmux-refused" ]; then cat "$fixture/tmux-refused" >&2; exit 1; fi
[ -s "$fixture/tmux-refused.probes" ]
passed=1
printf 'T2 headless: command and adapter tasks, reconnect, exact consumption, local compiled plan, duplicate submission, lost start, owner death, cancellation and bounded logs passed with tmux absent on the execution host\n'
