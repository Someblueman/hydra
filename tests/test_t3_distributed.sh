#!/bin/sh
# T3 qualification: the distributed plan (fan-out, cross-host validation,
# evidence join, composition, final check) over two receiving hosts with tmux
# absent on both. The coordinator keeps its own PATH. HYDRA_TEST_T3_TRANSPORT
# selects the SSH boundary: "controlled" (default; the fault-injecting
# loopback the DAG fleet cases use) or "ssh" (two private loopback OpenSSH
# daemons; prints SKIP and exits 0 without an OpenSSH server).
# See docs/T2_T3_QUALIFICATION.md.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
transport="${HYDRA_TEST_T3_TRANSPORT:-controlled}"
case "$transport" in controlled|ssh) ;; *) printf 'HYDRA_TEST_T3_TRANSPORT must be controlled or ssh\n' >&2; exit 2 ;; esac
ssh_config="" sshd_pids="" launcher=""
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
fixture="$(mktemp -d "${TMPDIR:-/tmp}/hydra-t3.XXXXXX")"
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
cleanup() {
    [ -z "${launcher:-}" ] || kill -KILL "$launcher" 2>/dev/null || :
    for host in a b; do
        cleanup_home="$fixture/host-$host/home"
        [ -d "$cleanup_home" ] || continue
        workflow_task_fixture_quiesce "$cleanup_home" || return 1
        for workspace in "$cleanup_home"/fleet/tasks/task_*/workspace; do
            [ -f "$workspace/.git/hydra/project-id" ] || continue
            (cd "$workspace" && env PATH="$receiver_path" HYDRA_HOME="$cleanup_home" "$root/bin/hydra" kill --all --force) >/dev/null 2>&1 || :
        done
    done
    for sshd_pid in ${sshd_pids:-}; do kill "$sshd_pid" 2>/dev/null || :; done
    test_tmux_fixture_cleanup "$fixture" || return 1
    if [ "${passed:-0}" = 1 ] && [ "${HYDRA_TEST_KEEP_FIXTURE:-0}" != 1 ]; then
        rm -rf "$fixture"
    else
        printf 'T3 distributed evidence (%s): %s\n' "$transport" "$fixture" >&2
    fi
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-t3-distributed or make test-one T=<name>}"
HYDRA_HOME="$fixture/client"
export HYDRA_FLEET_BIN HYDRA_HOME HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1

# Both execution hosts share one tmux-less PATH with a loud tmux stand-in.
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
receiver_path="$fixture/receiver-path"
headless_receiver_path "$receiver_path" "$fixture/tmux-refused"
printf 'Coordinator tmux: %s\n' "$(tmux -V 2>/dev/null || printf 'absent')"
if [ "$transport" = ssh ]; then
    # shellcheck source=/dev/null
    . "$root/tests/t3_distributed_ssh.sh"
    if ! t3_ssh_available; then
        printf 'SKIP T3 distributed SSH: OpenSSH server or client unavailable\n'
        passed=1
        exit 0
    fi
fi

# One source repository on the coordinator; each host maps its own clone.
mkdir -p "$fixture/source"
cp "$root/tests/fixtures/plan-task/repo/"* "$fixture/source/"
git -C "$fixture/source" init -q
git -C "$fixture/source" config user.name 'T3 test'
git -C "$fixture/source" config user.email t3@example.invalid
git -C "$fixture/source" config commit.gpgSign false
git -C "$fixture/source" add .
git -C "$fixture/source" commit -qm recipes
commit="$(git -C "$fixture/source" rev-parse HEAD)"
for host in a b; do
    mkdir -p "$fixture/host-$host"
    git clone -q "$fixture/source" "$fixture/host-$host/project"
    (cd "$fixture/host-$host/project" && env PATH="$receiver_path" HYDRA_HOME="$fixture/host-$host/home" "$root/bin/hydra" init --no-agent --json) > "$fixture/host-$host/init"
done
(cd "$fixture/source" && "$root/bin/hydra" init --no-agent --trust) >/dev/null

if [ "$transport" = controlled ]; then
    mkdir -p "$fixture/bin" "$fixture/control"
    HYDRA_TEST_T3_CONTROL="$fixture/control" HYDRA_TEST_RECEIVER_PATH="$receiver_path"
    export HYDRA_TEST_T3_CONTROL HYDRA_TEST_RECEIVER_PATH
    cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ "$#" -gt 2 ]; do shift; done
case "$1" in loopback-a|loopback-b) ;; *) exit 255 ;; esac
command="$2"
request="$(mktemp "$HYDRA_TEST_T3_CONTROL/request.XXXXXX")"
trap 'rm -f "$request"' 0
cat > "$request"
receiver() { env PATH="$HYDRA_TEST_RECEIVER_PATH" /bin/sh -c "$command" < "$request"; }
if [ -f "$HYDRA_TEST_T3_CONTROL/lose-ack" ] && grep -q '"operation":"submit"' "$request"; then
    receiver > "$HYDRA_TEST_T3_CONTROL/lost-response"
    rm "$HYDRA_TEST_T3_CONTROL/lose-ack"
    exit 255
fi
if grep -q '"operation":"result"' "$request"; then
    if [ -f "$HYDRA_TEST_T3_CONTROL/record-result" ]; then
        receiver > "$HYDRA_TEST_T3_CONTROL/recorded-result"
        rm "$HYDRA_TEST_T3_CONTROL/record-result"
        cat "$HYDRA_TEST_T3_CONTROL/recorded-result"
        exit 0
    fi
    if [ -f "$HYDRA_TEST_T3_CONTROL/stale-result" ]; then
        # Replay an earlier task's sealed result for this request.
        rm "$HYDRA_TEST_T3_CONTROL/stale-result"
        cat "$HYDRA_TEST_T3_CONTROL/recorded-result"
        exit 0
    fi
    if [ -f "$HYDRA_TEST_T3_CONTROL/bad-result" ]; then
        rm "$HYDRA_TEST_T3_CONTROL/bad-result"
        receiver | sed 's/"result_sha256":"[a-f0-9]*/"result_sha256":"0000000000000000000000000000000000000000000000000000000000000000/'
        exit 0
    fi
fi
exec env PATH="$HYDRA_TEST_RECEIVER_PATH" /bin/sh -c "$command" < "$request"
SSH
    chmod +x "$fixture/bin/ssh"
    PATH="$fixture/bin:$PATH"
    export PATH
    for host in a b; do
        "$root/bin/hydra" remote add "host-$host" "loopback-$host" --hydra "$root/bin/hydra" --home "$fixture/host-$host/home" >/dev/null
    done
else
    t3_ssh_start
    for host in a b; do
        "$root/bin/hydra" remote add "host-$host" "host-$host" --hydra "$root/bin/hydra" --home "$fixture/host-$host/home" --ssh-config "$ssh_config" >/dev/null
    done
fi
# Both hosts answer through their aliases without a terminal capability.
"$root/bin/hydra" fleet list --json > "$fixture/fleet-list"
for host in a b; do
    grep -q "\"host\":\"host-$host\"" "$fixture/fleet-list"
    printf '{"protocol":1,"action":"handshake"}' | env PATH="$receiver_path" HYDRA_HOME="$fixture/host-$host/home" "$root/bin/hydra" fleet serve > "$fixture/host-$host/handshake"
    grep -q '"execution-headless"' "$fixture/host-$host/handshake"
    if grep -q '"tmux"' "$fixture/host-$host/handshake"; then exit 1; fi
done
grep -q '"ok":true' "$fixture/fleet-list"
if grep -q '"ok":false' "$fixture/fleet-list"; then cat "$fixture/fleet-list" >&2; exit 1; fi

# The checked task plan, placed across both hosts: produce and compose on
# host-a, each validated on host-b.
sed 's/"local"/"host-a", "host-b"/' "$root/tests/fixtures/plan-task/plan.json" > "$fixture/plan.json"
sed 's/"local"/"host-a", "host-b"/' "$root/tests/fixtures/plan-task/policy.json" > "$fixture/policy.json"
for node in produce inspect compose verify; do
    inputs='[]'; outputs='["result.txt"]'; argv="[\"sh\",\"$node.sh\"]"; host=a
    case "$node" in
        produce) argv='["sh","produce.sh","pass"]' ;;
        inspect) host=b; inputs='["subject","validation"]'; outputs='["report.json"]'; argv='["sh","check.sh","part-check","part-content","pass"]' ;;
        compose) inputs='["subject"]' ;;
        verify) host=b; inputs='["subject","validation"]'; outputs='["report.json"]'; argv='["sh","check.sh","final-check","final-content","pass"]' ;;
    esac
    cat > "$fixture/source/$node.json" <<JSON
{"schema_version":1,"host":"host-$host","project":"$fixture/host-$host/project","source":{"commit":"$commit"},"work":{"kind":"exec","argv":$argv},"inputs":$inputs,"outputs":$outputs,"capabilities":["exec","execution-headless"],"completion":"command-exit","limits":{"transport_seconds":20,"queue_seconds":30,"startup_seconds":30,"execution_seconds":30,"cancellation_seconds":5,"log_bytes":4096,"artifact_bytes":4096}}
JSON
done
(cd "$fixture/source" && "$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json") > "$fixture/compile.json"
plan_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile.json")"
[ -n "$plan_digest" ]
(cd "$fixture/source" && "$root/bin/hydra" workflow plan show "$fixture/compiled.json") > "$fixture/plan-preview"
grep -q 'host-a' "$fixture/plan-preview"
grep -q 'host-b' "$fixture/plan-preview"

# shellcheck source=/dev/null
. "$root/tests/t3_distributed_cases.sh"

if [ -s "$fixture/tmux-refused" ]; then cat "$fixture/tmux-refused" >&2; exit 1; fi
[ -s "$fixture/tmux-refused.probes" ]
passed=1
if [ "$transport" = ssh ]; then
    printf 'T3 distributed over loopback SSH passed: two tmux-less hosts, fan-out, cross-host validation, join, composition, final check, coordinator restart\n'
else
    printf 'T3 distributed: two tmux-less hosts, fan-out, cross-host validation, join, composition, final check, lost acknowledgment, coordinator restart and stale results passed\n'
fi
