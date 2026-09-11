#!/bin/sh
# Public fleet CLI tests with a controlled SSH transport and real C server.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
TMUX_TMPDIR="$(mktemp -d /tmp/hydra-fleet.XXXXXX)"
TMUX_SOCKET="$TMUX_TMPDIR/tmux-$(id -u)/default"
unset TMUX TMUX_PANE
cleanup() {
    if [ -S "$TMUX_SOCKET" ]; then
        tmux -S "$TMUX_SOCKET" kill-server 2>/dev/null || true
    fi
    if [ -n "${coordinator:-}" ]; then
        kill "$coordinator" 2>/dev/null || true
    fi
    if [ -n "${ssh_pid:-}" ]; then
        kill "$ssh_pid" 2>/dev/null || true
    fi
    rm -rf "$fixture"
    rmdir "$TMUX_TMPDIR/tmux-$(id -u)" 2>/dev/null || true
    rmdir "$TMUX_TMPDIR" 2>/dev/null || true
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_HOME TMUX_TMPDIR TMUX_SOCKET HYDRA_FLEET_BIN
mkdir -p "$fixture/bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
while [ $# -gt 2 ]; do shift; done
if [ -n "${HYDRA_TEST_OFFLINE_FILE:-}" ] && [ -f "$HYDRA_TEST_OFFLINE_FILE" ] && [ "$1" = good ]; then exit 255; fi
case "$1" in
    key) echo 'Host key verification failed.' >&2; exit 255 ;;
    auth) echo 'Permission denied (publickey).' >&2; exit 255 ;;
    offline) echo 'Connection refused' >&2; exit 255 ;;
    slow) [ -z "${HYDRA_TEST_SSH_PID:-}" ] || echo $$ > "$HYDRA_TEST_SSH_PID"; exec sleep 30 ;;
    slowgood)
        sleep 2
        request="$(cat)"
        case "$request" in
            *'"action":"handshake"'*)
                printf '%s\n' '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"2.1.0","fleet_protocol":1,"state_schema":2,"event_schema":1,"json_schema":1,"capabilities":["list","overview"]}}' ;;
            *'"action":"list"'*)
                printf '%s\n' '{"schema_version":1,"ok":true,"command":"fleet-snapshot","data":{"heads":[{"project_path":"/srv/slowgood","branch":"delayed","head_id":"head_delayed","current_instance":"instance_delayed","desired_state":"running"}]}}' ;;
            *)
                printf '%s\n' '{"schema_version":1,"ok":true,"command":"fleet-overview","data":{"snapshot_schema_version":1,"receiver_observed_at":1,"tasks":[]}}' ;;
        esac
        exit 0 ;;
    malformed) echo '{bad json'; exit ;;
    skew) printf '{"schema_version":1,"ok":true,"command":"fleet-handshake","data":{"hydra_version":"2.1.0","fleet_protocol":9,"capabilities":[]}}\n'; exit ;;
esac
exec /bin/sh -c "$2"
SSH
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH
"$root/bin/hydra" remote add good good --hydra "$root/bin/hydra"
"$root/bin/hydra" fleet list --json > "$fixture/result"
grep -q '"heads":\[\]' "$fixture/result"
"$root/bin/hydra" fleet overview --json > "$fixture/overview"
grep -q '"snapshot_schema_version":1' "$fixture/overview"
grep -q '"state":"reachable"' "$fixture/overview"
grep -q '"state":"fresh"' "$fixture/overview"
[ -f "$HYDRA_HOME/fleet/observations/good.json" ]
HYDRA_TEST_OFFLINE_FILE="$fixture/offline"
export HYDRA_TEST_OFFLINE_FILE
: > "$HYDRA_TEST_OFFLINE_FILE"
"$root/bin/hydra" fleet overview --json > "$fixture/stale-overview"
grep -q '"cached":true' "$fixture/stale-overview"
grep -q '"state":"stale"' "$fixture/stale-overview"
grep -q '"state":"unreachable"' "$fixture/stale-overview"
rm "$HYDRA_TEST_OFFLINE_FILE"
if "$root/bin/hydra" remote add bad 'host;touch nope' >/dev/null 2>&1; then exit 1; fi
for host in key auth offline slow malformed skew; do
    "$root/bin/hydra" remote add "$host" "$host" >/dev/null
done
if "$root/bin/hydra" fleet list --json --timeout 1 --jobs 8 > "$fixture/result"; then exit 1; fi
for code in host_key_failed authentication_failed offline timeout invalid_response version_mismatch; do
    grep -q "\"code\":\"$code\"" "$fixture/result"
done
grep -q '"partial":true' "$fixture/result"
"$root/bin/hydra" fleet tui-data > "$fixture/tui-v1"
[ "$(head -n 1 "$fixture/tui-v1")" = "$(printf 'HYDRA_FLEET_TUI\t1')" ]
if grep -q '^T' "$fixture/tui-v1"; then echo 'legacy fleet data changed'; exit 1; fi
"$root/bin/hydra" remote add slowgood slowgood --hydra "$root/bin/hydra"
"$root/bin/hydra" fleet tui-visual-data > "$fixture/tui-v2"
awk -F '\t' '$1=="T" && $2=="good" && $3=="responded" && $4==0 {empty=1}
    $1=="T" && $2=="slowgood" && $3=="responded" && $4==1 && $6=="reachable" && $7=="fresh" {delayed=1}
    $1=="T" && $2=="offline" && $3=="failed" && $5=="offline" {failed=1}
    END {exit !(empty && delayed && failed)}' "$fixture/tui-v2"
awk -F '\t' '$1=="F" && $2=="slowgood" && $3=="/srv/slowgood" && $4=="delayed" && $5=="head_delayed" && $6=="instance_delayed" {found=1}
    END {exit !found}' "$fixture/tui-v2"
if [ -x "$root/build/hydra-tui" ]; then
    "$root/build/hydra-tui" --fleet --headless-fixture "$fixture/tui-v2" --view hosts --ascii --size 140x30 > "$fixture/hosts.out"
    grep -q 'good.*responded.*0 heads' "$fixture/hosts.out"
    grep -q 'offline.*failed' "$fixture/hosts.out"
    grep -q 'CPU / memory / load: unavailable' "$fixture/hosts.out"
fi
for host in key auth offline malformed skew good slowgood; do "$root/bin/hydra" remote remove "$host" >/dev/null; done
HYDRA_TEST_SSH_PID="$fixture/ssh-pid"
export HYDRA_TEST_SSH_PID
"$root/bin/hydra" fleet list --json --timeout 30 > "$fixture/cancel-result" &
coordinator=$!
attempt=0
while [ ! -f "$HYDRA_TEST_SSH_PID" ] && [ "$attempt" -lt 50 ]; do sleep 0.1; attempt=$((attempt + 1)); done
[ -f "$HYDRA_TEST_SSH_PID" ] || { kill "$coordinator"; exit 1; }
ssh_pid="$(cat "$HYDRA_TEST_SSH_PID")"
kill -TERM "$coordinator"
if wait "$coordinator"; then exit 1; fi
coordinator=""
if kill -0 "$ssh_pid" 2>/dev/null; then kill "$ssh_pid" 2>/dev/null || :; echo 'SSH survived cancellation'; exit 1; fi
ssh_pid=""
printf 'Fleet CLI transport, partial failures, and cancellation passed\n'

guard_repo="$fixture/guard-repo"
mkdir -p "$guard_repo"
git -C "$guard_repo" init -q
git -C "$guard_repo" config user.email hydra-test@example.invalid
git -C "$guard_repo" config user.name 'Hydra Fleet Test'
: > "$guard_repo/fixture"
git -C "$guard_repo" add fixture
git -C "$guard_repo" commit -qm fixture
(cd "$guard_repo" && "$root/bin/hydra" init --no-agent --trust >/dev/null)
(cd "$guard_repo" && HYDRA_NO_SWITCH=1 "$root/bin/hydra" spawn fleet-guard --no-agent >/dev/null)
guard_project_id="$(find "$HYDRA_HOME/state/v2/projects" -mindepth 1 -maxdepth 1 -type d -name 'project_*' -print -quit | sed 's#.*/##')"
guard_state="$HYDRA_HOME/state/v2/projects/$guard_project_id"
guard_head_dir="$(find "$guard_state/heads" -mindepth 1 -maxdepth 1 -type d -name 'head_*' -print -quit)"
guard_head_id="$(sed -n '1p' "$guard_head_dir/head-id")"
guard_instance="$(sed -n '1p' "$guard_head_dir/current-instance")"
guard_session="$(sed -n '1p' "$guard_head_dir/session")"
guard_session_id="$(tmux display-message -p -t "=$guard_session:" '#{session_id}')"

assert_attach_refused() {
    guard_output="$(cd "$guard_repo" && "$root/bin/hydra" fleet-local attach fleet-guard "$guard_instance" 2>&1)" || {
        echo "$guard_output" >&2
        echo 'fleet-local attach unexpectedly failed before identity guard' >&2
        exit 1
    }
    printf '%s\n' "$guard_output" | grep -Fq 'Hydra attach refused: session identity changed'
}

tmux set-environment -t "$guard_session" HYDRA_PROJECT_ID project_wrong
assert_attach_refused
tmux set-environment -t "$guard_session" HYDRA_PROJECT_ID "$guard_project_id"
tmux set-environment -t "$guard_session" HYDRA_HEAD_ID head_wrong
assert_attach_refused
tmux set-environment -t "$guard_session" HYDRA_HEAD_ID "$guard_head_id"
tmux set-environment -t "$guard_session" HYDRA_INSTANCE_ID instance_wrong
assert_attach_refused
tmux set-environment -t "$guard_session" HYDRA_INSTANCE_ID "$guard_instance"

guard_stale_instance=instance_stale
if (cd "$guard_repo" && "$root/bin/hydra" fleet-local attach fleet-guard "$guard_stale_instance" > "$fixture/stale-attach" 2>&1); then
    echo 'stale lifecycle instance was accepted' >&2
    exit 1
fi
grep -Fq 'head instance changed' "$fixture/stale-attach"

tmux -S "$TMUX_SOCKET" kill-server
tmux -f /dev/null new-session -d -s "$guard_session" /bin/sh
[ "$(tmux display-message -p -t "=$guard_session:" '#{session_id}')" = "$guard_session_id" ]
tmux set-environment -t "$guard_session" HYDRA_PROJECT_ID replacement_project
tmux set-environment -t "$guard_session" HYDRA_HEAD_ID replacement_head
tmux set-environment -t "$guard_session" HYDRA_INSTANCE_ID replacement_instance
assert_attach_refused
printf 'Fleet attach identity guard public-route regressions passed\n'
