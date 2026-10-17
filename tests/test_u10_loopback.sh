#!/bin/sh
# U10 loopback acceptance: a fresh throwaway account behind a private sshd
# on a loopback port, with no Hydra on its PATH, is onboarded through the
# control centre's Hosts tab driven over a PTY (tests/fixtures/u10/drive.py):
# typed-yes host-key trust, preflight, provisioning from this checkout's own
# helper through the documented --binary path, the deterministic agent
# fixture installed as codex by the real recipe machinery, a sign-in that is
# killed mid-way and resumed without being repeated, a sign-in whose verify
# step fails and is retried, then a real fixture-agent task launched on the
# host, found again after the client reconnects; a second setup of the same
# account reuses the pin, and a host whose sshd hides git shows the explained
# requirement with its suggestion and continues once git is back. Unrelated
# files in the account stay byte-identical and no local secret is copied.
# Needs root (or passwordless sudo) for useradd and sshd; otherwise SKIP.
# Assertions decide the outcome (no -e): a failed check is reported, not fatal.
set -u
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fleet="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-u10-loopback}"
tui="${HYDRA_TUI_BIN:?HYDRA_TUI_BIN is required: run via make test-u10-loopback}"
build="${BUILD_DIR:?BUILD_DIR is required: run via make test-u10-loopback}"
cells="$build/termviz-cells"

skip() { printf 'SKIP U10 loopback: %s\n' "$1"; exit 0; }
if [ "$(id -u)" -ne 0 ]; then
    if [ -z "${U10_ELEVATED:-}" ] && command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        exec sudo -n -E env "PATH=$PATH" U10_ELEVATED=1 sh "$0" "$@"
    fi
    skip "needs root or passwordless sudo to create a throwaway account and run sshd"
fi
for tool in /usr/sbin/sshd ssh ssh-keygen useradd userdel python3 tmux git; do
    command -v "$tool" >/dev/null 2>&1 || skip "$tool is unavailable"
done
[ -x "$cells" ] || skip "$cells is missing (make test-u10-loopback builds it)"
case "$(uname -s)" in Linux) ;; *) skip "useradd-based throwaway accounts are exercised on Linux only" ;; esac

# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
test_count=0; pass_count=0; fail_count=0

base="$(test_mktemp_dir)"
base="$(cd "$base" && pwd -P)"
# The throwaway account must traverse the staging directory (its installer
# source); the per-case TMPDIR is private, so the stage lives under /tmp.
stage="$(mktemp -d /tmp/hydra-u10-stage.XXXXXX)"
chmod 755 "$stage"
user="hydrau10$$"
evidence="$build/u10-loopback-evidence"
rm -rf "$evidence"; mkdir -p "$evidence"
sshd_pids=""
mkdir -p /run/sshd

cleanup() {
    for pid in $sshd_pids; do kill "$pid" 2>/dev/null || true; done
    # The evidence is written as root; hand it to the user who invoked sudo so
    # the CI upload (and that user) can read it. Modes stay as written: the
    # copied setup record keeps 0600 and Hydra's own state is not touched.
    case "${SUDO_UID:-}:${SUDO_GID:-}" in
        *[!0-9:]*|:*|*:) ;;
        *) chown -R "$SUDO_UID:$SUDO_GID" "$evidence" 2>/dev/null || true ;;
    esac
    if id "$user" >/dev/null 2>&1; then
        pkill -KILL -u "$user" 2>/dev/null || true
        sleep 0.3
        userdel -r "$user" 2>/dev/null || true
    fi
    rm -rf "$stage" "$base"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

echo "U10 loopback acceptance"
echo "======================="

# ---- the throwaway account and its sshd ----
useradd -m -s /bin/sh -p '*' "$user" || exit 1
home="$(getent passwd "$user" | cut -d: -f6)"
ssh-keygen -q -t ed25519 -N '' -f "$base/client" || exit 1
ssh-keygen -q -t ed25519 -N '' -f "$base/host" || exit 1
mkdir -m 700 "$home/.ssh" || exit 1
cp "$base/client.pub" "$home/.ssh/authorized_keys"
chmod 600 "$home/.ssh/authorized_keys"
chown -R "$user:$user" "$home/.ssh"
fingerprint="$(ssh-keygen -lf "$base/host.pub" | awk '{ print $2 }')"
# A restricted remote PATH without git for the missing-requirements host.
mkdir -p "$stage/restricted"
for tool in sh dash uname awk df env cat tmux mktemp head tail sha256sum shasum cp mkdir chmod ls rm sed grep python3 tr sort find; do
    path="$(command -v "$tool" 2>/dev/null || true)"
    [ -n "$path" ] && ln -sf "$path" "$stage/restricted/$tool"
done
free_port() { python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
port_full="$(free_port)"
port_restricted="$(free_port)"
# sshd_config PORT FILE [EXTRA]
sshd_config() {
    {
        printf 'Port %s\nListenAddress 127.0.0.1\nHostKey %s\nPidFile %s.pid\n' "$1" "$base/host" "$2"
        printf 'AuthorizedKeysFile .ssh/authorized_keys\nStrictModes no\nUsePAM no\nPasswordAuthentication no\n'
        printf 'KbdInteractiveAuthentication no\nAllowUsers %s\nLogLevel ERROR\n' "$user"
        [ -z "${3:-}" ] || printf '%s\n' "$3"
    } > "$2"
}
# start_sshd CONFIG PORT PIDFILE: a listener in the background, up once the port answers.
start_sshd() {
    /usr/sbin/sshd -D -e -f "$1" >> "$evidence/sshd.log" 2>&1 &
    printf '%s\n' "$!" > "$3"
    sshd_pids="$sshd_pids $!"
    tries=0
    while ! python3 -c "import socket, sys; s = socket.socket(); s.settimeout(1); sys.exit(s.connect_ex(('127.0.0.1', $2)))" 2>/dev/null; do
        tries=$((tries + 1)); [ "$tries" -lt 100 ] || { echo "sshd on port $2 did not start" >&2; exit 1; }
        sleep 0.1
    done
}
sshd_config "$port_full" "$base/sshd_full.conf"
sshd_config "$port_restricted" "$base/sshd_restricted.conf" "SetEnv PATH=$stage/restricted"
start_sshd "$base/sshd_full.conf" "$port_full" "$base/full.pid"
start_sshd "$base/sshd_restricted.conf" "$port_restricted" "$base/restricted.pid"
restricted_pid="$(cat "$base/restricted.pid")"
cat > "$base/ssh_config" <<CONFIG
Host u10 u10x
  HostName 127.0.0.1
  User $user
  IdentityFile $base/client
  IdentitiesOnly yes
  UserKnownHostsFile $base/known_hosts
  GlobalKnownHostsFile /dev/null
  ConnectTimeout 10
Host u10
  Port $port_full
Host u10x
  Port $port_restricted
CONFIG
# remote CMD: a shell command in the account over the trusted main sshd (used
# for seeding and inspection only; Hydra uses its own strict configuration).
remote() { ssh -F "$base/ssh_config" -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile="$base/inspect_known_hosts" u10 "$1"; }

# ---- the agent fixture and its private recipe ----
sed "s|@STAGE@|$stage|" "$root/tests/fixtures/u10/codex" > "$stage/codex"
cp "$root/tests/fixtures/usability/provider.py" "$stage/provider.py"
chmod 755 "$stage/codex" "$stage/provider.py"
: > "$stage/hold"
chmod 644 "$stage/hold"

# ---- the client side: private HOME and HYDRA_HOME, a local secret that must stay here ----
export HOME="$base/home"
export HYDRA_HOME="$base/hydra-home"
mkdir -m 700 "$HOME" "$HYDRA_HOME" "$HYDRA_HOME/fleet" "$HOME/.codex" || exit 1
secret="LOCAL-SECRET-$(od -An -N8 -tx1 /dev/urandom | tr -d ' \n')"
printf '{"token":"%s"}\n' "$secret" > "$HOME/.codex/auth.json"
chmod 600 "$HOME/.codex/auth.json"
cat > "$HYDRA_HOME/fleet/agent-recipes.json" <<RECIPES
{"schema":"agent-recipes","schema_version":1,"recipes":[{"agent":"codex","executable":"codex",
"command":"mkdir -p ~/.local/bin && cp $stage/codex $stage/provider.py ~/.local/bin/ && chmod 755 ~/.local/bin/codex ~/.local/bin/provider.py",
"requires":["cp"],"expected_dirs":["~/.local/bin"],"login_args":["login"],"auth_status_argv":["auth","status"],
"docs_url":"https://example.invalid/u10-fixture","verified_on":"2026-10-17"}]}
RECIPES
chmod 600 "$HYDRA_HOME/fleet/agent-recipes.json"
project="$base/project"
mkdir "$project"
cd "$project" || exit 1
git init -q
git config user.email u10@example.invalid
git config user.name "U10 fixture"
printf 'local project\n' > README
git add README
git -c commit.gpgSign=false commit -qm fixture
helper="$build/static/hydra-fleet-linux-$(uname -m)"
[ -x "$helper" ] || helper="$fleet"
export HYDRA_FLEET_BIN="$fleet" HYDRA_TUI_BIN="$tui" HYDRA_CELLS_BIN="$cells" HYDRA_BIN_CMD="$root/bin/hydra"
export HYDRA_REMOTE_SETUP_BINARY="$helper" PATH="$root/bin:$PATH" TERM=xterm-256color
export U10_EVIDENCE="$evidence" U10_SSH_CONFIG="$base/ssh_config" U10_FINGERPRINT="$fingerprint"
export U10_HOLD="$stage/hold" U10_LOGIN_LOG="$home/.codex/login.log" U10_SYNC="$base/sync"
mkdir "$U10_SYNC"
unset CI HYDRA_NONINTERACTIVE HYDRA_SKIP_AI TMUX TMUX_PANE
hydra="$root/bin/hydra"

# ---- unrelated files in the account, recorded before anything runs ----
remote 'printf "keep me\n" > ~/notes.txt; mkdir -p ~/.config/other-tool; printf "theirs\n" > ~/.config/other-tool/settings; printf "x\n" > ~/.profile-extra'
remote 'command -v hydra || echo no-hydra' > "$base/remote-hydra.txt"
grep -q no-hydra "$base/remote-hydra.txt"
assert_success $? "the throwaway account has no Hydra on its PATH"
remote 'cd ~ && find . -type f ! -path "./.ssh/*" -exec sha256sum {} + | sort' > "$base/before.txt"

# drive LABEL JOURNEY: one control-centre journey over a PTY; its screens and
# log land in the evidence directory under LABEL.
drive() {
    _d_label="$1"; _d_journey="$2"; shift 2
    _d_status=0
    U10_JOURNEY_LABEL="$_d_label" timeout 900 python3 "$root/tests/fixtures/u10/drive.py" "$_d_journey" > "$evidence/$_d_label.log" 2>&1 || _d_status=$?
    cat "$evidence/$_d_label.log"
    assert_success "$_d_status" "journey $_d_label through the control centre"
}

# ---- 1. onboarding through the Hosts tab ----
drive onboard onboard
"$hydra" remote setup status u10 --json > "$evidence/status.json" 2>/dev/null || true
grep -q '"next":null' "$evidence/status.json"
assert_success $? "setup status reports u10 complete (no next step)"
"$hydra" remote list > "$base/remote-list.txt" 2>&1 || true
grep -q u10 "$base/remote-list.txt"
assert_success $? "the alias u10 is enrolled"
pins="$(remote 'ls ~/.local/share/hydra/fleet 2>/dev/null | wc -l')"
assert_equal "1" "$(printf '%s' "$pins" | tr -d ' ')" "exactly one pinned Hydra install exists on the host"
remote 'test -x ~/.local/bin/codex' >/dev/null 2>&1
assert_success $? "the fixture agent was installed into ~/.local/bin by the recipe"
remote 'cat ~/.codex/login.log' > "$base/login.log"
assert_equal "start start done start done" "$(tr '\n' ' ' < "$base/login.log" | sed 's/ $//')" "login attempts: interrupted, failed verification, verified"
# The setup record (json-c escapes slashes) keeps the install step's outcome:
# Hydra ran the installer and recorded the agent's per-user location.
cp "$HYDRA_HOME/fleet/setup/u10.json" "$evidence/setup-u10.json" 2>/dev/null || true
sed 's,\\/,/,g' "$HYDRA_HOME/fleet/setup/u10.json" | grep -q '"installed_by_hydra":true'
assert_success $? "the install step records that Hydra ran the installer"
sed 's,\\/,/,g' "$HYDRA_HOME/fleet/setup/u10.json" | grep -q '\.local/bin/codex'
assert_success $? "the install step recorded the agent's per-user location off PATH"
grep -q '"trust":"unpinned"' "$HYDRA_HOME/fleet/setup/u10.json"
assert_success $? "the provisioning plan recorded the helper as unpinned (--binary)"

# ---- 2. a real fixture-agent task on the host, found again after reconnecting ----
remote 'mkdir -p ~/project && cd ~/project && git init -q && git config user.email u10@example.invalid && git config user.name u10 && printf "remote project\n" > README && git add README && git -c commit.gpgSign=false commit -qm fixture'
"$hydra" fleet init u10 --project "$home/project" -- --no-agent --json > "$evidence/init.json" 2>&1 || true
grep -q '"ok":true' "$evidence/init.json"
assert_success $? "fleet init prepared the remote project"
"$hydra" fleet init u10 --project "$home/project" -- --trust --json > "$evidence/trust.json" 2>&1 || true
grep -q '"ok":true' "$evidence/trust.json"
assert_success $? "the remote project was trusted explicitly"
"$hydra" fleet spawn u10 --project "$home/project" -- u10-task --profile codex > "$evidence/spawn.json" 2>&1
spawn_status=$?
cat "$evidence/spawn.json"
assert_success "$spawn_status" "fleet spawn launched u10-task with the codex profile on the host"
if [ "$spawn_status" -ne 0 ]; then
    # What the remote admission and state checks saw (modes, ACLs, umask, disk).
    remote 'umask; id; ls -ld ~ ~/.hydra ~/.hydra/admission ~/.hydra/locks ~/.hydra/state; if command -v getfacl >/dev/null 2>&1; then getfacl -p ~ ~/.hydra ~/.hydra/admission; fi; df -Pk ~' \
        > "$evidence/remote-state-after-spawn-failure.txt" 2>&1 || true
fi
tries=0
until remote 'tmux capture-pane -p -t u10-task 2>/dev/null' | grep -q 'ready to discuss'; do
    tries=$((tries + 1)); [ "$tries" -lt 150 ] || break; sleep 0.2
done
remote 'tmux list-sessions 2>&1' > "$evidence/remote-tmux-sessions.txt" || true
remote 'tmux capture-pane -p -t u10-task 2>/dev/null' > "$evidence/remote-agent-pane.txt" || true
grep -q 'ready to discuss' "$evidence/remote-agent-pane.txt"
assert_success $? "the fixture agent is running in the task's terminal on the host"
drive reconnect-1 reconnect
drive reconnect-2 reconnect
"$hydra" fleet list u10 --json > "$evidence/list.json" 2>&1 || true
grep -q 'u10-task' "$evidence/list.json"
assert_success $? "fleet list observes u10-task on the host"

# ---- 3. an existing installation: the same account under a second name ----
drive again again
pins="$(remote 'ls ~/.local/share/hydra/fleet | wc -l')"
assert_equal "1" "$(printf '%s' "$pins" | tr -d ' ')" "the second setup reused the pin instead of installing again"
first="$(sed -n 's/.*"hydra":"\([^"]*\)".*/\1/p' "$HYDRA_HOME/fleet/remotes/u10.json")"
second="$(sed -n 's/.*"hydra":"\([^"]*\)".*/\1/p' "$HYDRA_HOME/fleet/remotes/u10-again.json" 2>/dev/null || true)"
assert_equal "$first" "$second" "both aliases use the same pinned install"

# ---- 4. missing prerequisites: git hidden from the remote PATH, then restored ----
(
    tries=0
    while [ ! -e "$U10_SYNC/blocked" ] && [ "$tries" -lt 900 ]; do tries=$((tries + 1)); sleep 0.2; done
    kill "$restricted_pid" 2>/dev/null || true
    sleep 0.3
    sshd_config "$port_restricted" "$base/sshd_restored.conf"
    /usr/sbin/sshd -D -e -f "$base/sshd_restored.conf" >> "$evidence/sshd.log" 2>&1 &
    echo "$!" > "$base/restored.pid"
    sleep 0.5
    : > "$U10_SYNC/fixed"
) &
fixer=$!
drive missing missing
wait "$fixer" || true
[ ! -f "$base/restored.pid" ] || sshd_pids="$sshd_pids $(cat "$base/restored.pid")"
grep -q 'sudo apt-get install git' "$evidence/missing-07-requirements-blocked.txt" 2>/dev/null
assert_success $? "the blocked requirements screen named git with its suggested command"
remote 'test ! -e ~/.local/share/hydra/fleet/extra' >/dev/null 2>&1
pins="$(remote 'ls ~/.local/share/hydra/fleet | wc -l')"
assert_equal "1" "$(printf '%s' "$pins" | tr -d ' ')" "the declined plan installed nothing"

# ---- 5. unrelated files untouched, no secret copied ----
remote 'cd ~ && find . -type f ! -path "./.ssh/*" -exec sha256sum {} + | sort' > "$base/after.txt"
missing_lines=0
while IFS= read -r line; do
    grep -Fqx "$line" "$base/after.txt" || { missing_lines=$((missing_lines + 1)); printf 'changed: %s\n' "$line"; }
done < "$base/before.txt"
assert_equal "0" "$missing_lines" "every file that was in the account before setup is byte-identical"
if remote "grep -rl '$secret' ~ 2>/dev/null" | grep -q .; then
    assert_success 1 "the local credential was not copied to the host"
else
    assert_success 0 "the local credential was not copied to the host"
fi
if grep -rl "$secret" "$HYDRA_HOME/fleet/packages" "$HYDRA_HOME/fleet/setup" 2>/dev/null | grep -q .; then
    assert_success 1 "no package or setup record carries the local credential"
else
    assert_success 0 "no package or setup record carries the local credential"
fi
cp "$base/before.txt" "$base/after.txt" "$evidence/" 2>/dev/null || true

# ---- teardown of the task before the account goes ----
"$hydra" fleet cancel u10 --project "$home/project" --instance "$(sed -n 's/.*"current_instance":"\([^"]*\)".*/\1/p' "$evidence/list.json" | head -n 1)" -- u10-task --json > /dev/null 2>&1 || true
remote 'tmux kill-server 2>/dev/null || true' >/dev/null 2>&1 || true

printf '\nTests: %d, Passed: %d, Failed: %d\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ] || exit 1
echo "U10 loopback acceptance passed (evidence in $evidence)"
