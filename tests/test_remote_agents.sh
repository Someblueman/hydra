#!/bin/sh
# Remote agent setup (U10 S4/S5) over a fake SSH boundary with a real remote
# Hydra receiver: inventory through agent-inventory, location records,
# approval-gated installers (declined and mismatched approvals run nothing, a
# failing installer keeps state, interrupted installs reconcile without
# re-running), local recipe overrides, and native sign-in over `ssh -t` with
# provider-status verification, failure/retry and user confirmation. No
# network: a fake curl serves a fake installer. Runs under umask 022 and 002.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
fixture="$(test_mktemp_dir)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
fleet="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-fleet or make test-one T=remote_agents}"
AGENTS_REAL_SSH="$(command -v ssh)"
export HYDRA_FLEET_BIN="$fleet" AGENTS_TEST_ROOT="$fixture" AGENTS_REAL_SSH
unset CI HYDRA_NONINTERACTIVE AGENTS_TEST_INSTALL AGENTS_LOGIN_EXIT

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
for dir in /usr/bin /bin /usr/local/bin /opt/homebrew/bin /home/linuxbrew/.linuxbrew/bin /snap/bin; do
    for agent in claude agy opencode; do
        [ ! -e "$dir/$agent" ] || { printf 'SKIP: %s/%s exists on this host\n' "$dir" "$agent"; exit 0; }
    done
done

# Fake SSH: -G goes to OpenSSH; -t commands are logged; every command runs
# locally with the remote HOME and a minimal non-interactive PATH.
mkdir "$fixture/bin" "$fixture/remote-bin"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
for arg do [ "$arg" = -G ] && exec "$AGENTS_REAL_SSH" "$@"; done
tty=0
[ "$1" != -t ] || tty=1
while [ $# -gt 1 ]; do shift; done
[ "$tty" = 0 ] || printf '%s\n' "$1" >> "$AGENTS_TEST_ROOT/tty-commands"
PATH="$AGENTS_TEST_ROOT/remote-bin:/usr/bin:/bin"
HOME="$AGENTS_REMOTE_HOME"
export PATH HOME
[ -z "${AGENTS_REMOTE_UMASK:-}" ] || umask "$AGENTS_REMOTE_UMASK"
exec /bin/sh -c "$1"
SSH
# Fake curl: serves the fixture installer (or a failing one) for any URL. The
# installer's files take the session umask, like a real provider installer.
cat > "$fixture/remote-bin/curl" <<'CURL'
#!/bin/sh
printf '%s\n' "$*" >> "$AGENTS_TEST_ROOT/curl-calls"
if [ "${AGENTS_TEST_INSTALL:-ok}" = fail ]; then echo 'echo "fixture installer failed" >&2; exit 7'; exit 0; fi
printf 'mkdir -p "$HOME/.local/bin"\ncat "%s" > "$HOME/.local/bin/claude"\nchmod a+x "$HOME/.local/bin/claude"\n' "$AGENTS_TEST_ROOT/claude"
CURL
# Fake agents: help tokens satisfy the built-in probes; claude auth status is
# 0 when signed in and 1 otherwise, as documented by Claude Code.
cat > "$fixture/claude" <<'AGENT'
#!/bin/sh
case "${1:-}" in
    --version) echo "fixture-claude 2.0"; exit 0 ;;
    --help) echo "--print --session-id --resume stream-json"; exit 0 ;;
esac
if [ "${1:-}" = auth ] && [ "${2:-}" = login ]; then
    echo login >> "$AGENTS_TEST_ROOT/logins"
    [ "${AGENTS_LOGIN_EXIT:-0}" != 0 ] || : > "$HOME/.claude-signed-in"
    exit "${AGENTS_LOGIN_EXIT:-0}"
fi
if [ "${1:-}" = auth ] && [ "${2:-}" = status ]; then [ -e "$HOME/.claude-signed-in" ]; exit; fi
exit 0
AGENT
cat > "$fixture/agy" <<'AGENT'
#!/bin/sh
case "${1:-}" in
    --version) echo "fixture-agy 1.0"; exit 0 ;;
    --help) echo "--print --conversation --disable-slash-commands stream-json"; exit 0 ;;
esac
echo agy-login >> "$AGENTS_TEST_ROOT/logins"
exit 0
AGENT
chmod 755 "$fixture/bin/ssh" "$fixture/remote-bin/curl" "$fixture/claude" "$fixture/agy"
PATH="$fixture/bin:$PATH"
export PATH

# run EXPECTED OUT ARGS...: hydra remote ARGS without a terminal.
run() {
    expected=$1 out=$2; shift 2
    code=0
    "$fleet" remote "$@" > "$out" 2> "$out.err" < /dev/null || code=$?
    [ "$expected" = any ] || [ "$code" -eq "$expected" ] || { cat "$out" "$out.err" >&2; fail "remote $* exited $code, expected $expected"; }
}
# feed INPUT OUT: types INPUT once OUT shows a y/N prompt (typed-ahead input
# would reach the terminal before Hydra asks).
feed() {
    [ -n "$1" ] || return 0
    WAIT_FOR_TIMEOUT=20 wait_for "a y/N prompt" grep -q "y/N\]" "$2" || :
    printf '%s' "$1"
    sleep 1
}
# in_tty INPUT OUT ARGS...: hydra remote ARGS on a pseudo-terminal fed INPUT.
# shellcheck disable=SC2094 # feed only polls OUT for the prompt script writes
in_tty() {
    input=$1 out=$2; shift 2
    rm -f "$out"
    case "$(uname -s)" in
        Darwin) feed "$input" "$out" | script -q /dev/null "$fleet" remote "$@" > "$out" 2>&1 || : ;;
        *)
            command="'$fleet' remote"
            for arg do command="$command '$arg'"; done
            feed "$input" "$out" | script -q -e -c "$command" /dev/null > "$out" 2>&1 || : ;;
    esac
}
has() { grep -q -- "$2" "$1" || { cat "$1" >&2; [ ! -f "$1.err" ] || cat "$1.err" >&2; fail "$1 lacks $2"; }; }
lacks() { if grep -q -- "$2" "$1"; then cat "$1" >&2; fail "$1 unexpectedly has $2"; fi; }
lines() { if [ -f "$1" ]; then wc -l < "$1" | tr -d ' '; else echo 0; fi; }
state() { printf '%s/fleet/setup/%s.json' "$HYDRA_HOME" "$1"; }
step_status() { sed -n "s/.*\"$2\":{\"status\":\"\([a-z_]*\)\".*/\1/p" "$(state "$1")"; }
# write_state NAME STEPS: provisioned remote using this checkout's Hydra.
write_state() {
    printf '{"schema_version":1,"kind":"remote-setup","name":"%s","destination":"user@%s","ssh_config":"","steps":{%s},"remote":{"target":"user@%s","ssh_config":"","hydra":"%s","home":"%s","principal":"","project":"","accepted_host_key":"SHA256:fixture","multiplex":false}}' \
        "$1" "$1" "$2" "$1" "$root/bin/hydra" "$AGENTS_REMOTE_HOME/.hydra" > "$(state "$1")"
    chmod 600 "$(state "$1")"
}
# set_step NAME STEP STATUS: rewrite one recorded step status in place.
set_step() {
    sed "s/\"$2\":{\"status\":\"[a-z_]*\"/\"$2\":{\"status\":\"$3\"/" "$(state "$1")" > "$fixture/state.tmp"
    cat "$fixture/state.tmp" > "$(state "$1")"
}
json_value() { sed -n "s/.*\"$2\":\"\([^\"]*\)\".*/\1/p" "$1" | sed -n 1p; }

cases() {
    export HYDRA_HOME="$fixture/local-$1" AGENTS_REMOTE_HOME="$fixture/remote-$1"
    (umask 077; mkdir -p "$HYDRA_HOME/fleet/setup" "$AGENTS_REMOTE_HOME/.hydra")
    rm -f "$fixture/tty-commands" "$fixture/curl-calls" "$fixture/logins"
    out="$fixture/out"
    base='"host_key":{"status":"done"},"preflight":{"status":"done"},"provision":{"status":"done"}'
    write_state n1 "$base"

    # Inventory through the receiver; nothing selected without a terminal.
    run 0 "$out" agents n1 --json
    has "$out" '"source":"receiver"'
    has "$out" '"profile":"claude","executable":"claude","on_path":null,"candidates":\[\],"recorded":null,"status":"missing"'
    has "$out" '"agent":"claude","status":"missing","install_argv":\["hydra","remote","install-agent","n1","--agent","claude","--json"\]'
    has "$out" '"selected":\[\]'
    [ "$(step_status n1 agents)" = "done" ] || fail "agents step not done"

    # No recipe for launch-only agents: manual instructions, nothing selected.
    run 1 "$out" install-agent n1 --agent copilot --json
    has "$out" '"code":"recipe_unavailable"'; lacks "$(state n1)" 'install_agent:copilot'

    # The plan binds the exact command and effect; nothing runs without approval.
    run 3 "$out" install-agent n1 --agent claude --json
    has "$out" '"code":"approval_required"'
    has "$out" '"command":"umask 022; curl -fsSL https:\\/\\/claude.ai\\/install.sh | bash"'
    has "$out" '"effect":"runs on the remote as your user over ssh -t; no sudo; Hydra adds no symlinks and no PATH changes"'
    has "$out" '"docs_url":"https:\\/\\/code.claude.com\\/docs\\/en\\/setup"'; has "$out" '"source":"built-in"'
    has "$out" '"id":"install_agent:claude","status":"approval_required"'
    has "$(state n1)" '"selected":\["claude"\]'
    hash="$(json_value "$out" plan_sha256)"
    [ "${#hash}" -eq 64 ] || fail "no plan hash"
    [ ! -e "$fixture/curl-calls" ] && [ ! -e "$fixture/tty-commands" ] || fail "an unapproved installer ran"
    run 1 "$out" install-agent n1 --agent claude --approve "$(printf '%064d' 0)" --json
    has "$out" '"code":"approval_mismatch"'
    in_tty 'n
' "$out" install-agent n1 --agent claude
    has "$out" 'approval_declined'
    [ ! -e "$fixture/curl-calls" ] && [ ! -e "$fixture/tty-commands" ] || fail "a declined installer ran"

    # A failing installer is recorded and changes nothing else.
    export AGENTS_TEST_INSTALL=fail
    run 1 "$out" install-agent n1 --agent claude --approve "$hash" --json
    unset AGENTS_TEST_INSTALL
    has "$out" '"code":"install_failed"'; has "$out" '"exit_status":7'
    [ "$(step_status n1 install_agent:claude)" = failed ] || fail "failed installer not recorded"
    has "$(state n1)" '"install_agent:claude":{"status":"failed","detail":{[^}]*"error":{"code":"install_failed"'
    [ ! -e "$AGENTS_REMOTE_HOME/.local/bin/claude" ] || fail "failed installer left claude"
    has "$fixture/tty-commands" "exec /bin/sh -c 'umask 022; curl -fsSL https://claude.ai/install.sh | bash'"

    # An interrupted attempt reconciles by inventory and never re-runs.
    set_step n1 install_agent:claude outcome_unknown
    before="$(lines "$fixture/tty-commands")"
    run 1 "$out" install-agent n1 --agent claude --approve "$hash" --json
    has "$out" '"code":"install_failed"'; has "$out" 'interrupted installer'
    [ "$(lines "$fixture/tty-commands")" = "$before" ] || fail "reconcile re-ran the installer"

    # Approved install: the fake installer drops claude in ~/.local/bin. The
    # remote account's umask is 002 (the Ubuntu default); the approved umask
    # 022 prefix keeps the new executable from being group-writable, so the
    # inventory and probe accept it.
    AGENTS_REMOTE_UMASK=002 run 0 "$out" install-agent n1 --agent claude --approve "$hash" --json
    path="$AGENTS_REMOTE_HOME/.local/bin/claude"
    [ -x "$path" ] || fail "installer did not create claude"
    [ -n "$(find "$path" -prune -perm 755)" ] || fail "installer under remote umask 002 left claude group-writable"
    has "$out" '"installed_by_hydra":true'; has "$out" '"recorded":false'
    has "$out" "\"next\":{\"step\":\"agents\",\"argv\":\[\"hydra\",\"remote\",\"agents\",\"n1\",\"--record\",\"claude=$(printf '%s' "$path" | sed 's|/|\\\\/|g')\",\"--json\"\]"
    [ "$(step_status n1 install_agent:claude)" = "done" ] || fail "install not done"
    before="$(lines "$fixture/tty-commands")"
    run 0 "$out" install-agent n1 --agent claude --json
    [ "$(lines "$fixture/tty-commands")" = "$before" ] || fail "a finished install ran again"

    # Record the off-PATH location through the receiver.
    run 1 "$out" agents n1 --record "claude=$fixture/claude-elsewhere/claude" --json
    has "$out" '"code":"location_invalid"'
    run 0 "$out" agents n1 --record "claude=$path" --json
    has "$out" "\"recorded\":\"$(printf '%s' "$path" | sed 's|/|\\\\/|g')\",\"status\":\"recorded\""
    [ "$(cat "$AGENTS_REMOTE_HOME/.hydra/agents/locations/claude")" = "$path" ] || fail "remote record missing"
    has "$out" '"selected":\["claude"\]'

    # An in_progress install whose agent is present reconciles to done.
    set_step n1 install_agent:claude in_progress
    before="$(lines "$fixture/tty-commands")"
    run 0 "$out" install-agent n1 --agent claude --json
    [ "$(lines "$fixture/tty-commands")" = "$before" ] || fail "reconcile re-ran the installer"
    [ "$(step_status n1 install_agent:claude)" = "done" ] || fail "reconciled install not done"

    # Sign-in needs a terminal and runs the resolved absolute executable.
    run 1 "$out" sign-in n1 --agent claude --json
    has "$out" '"code":"tty_required"'
    [ ! -e "$fixture/logins" ] || fail "sign-in ran without a terminal"
    export AGENTS_LOGIN_EXIT=1
    in_tty "" "$out" sign-in n1 --agent claude --json
    unset AGENTS_LOGIN_EXIT
    has "$out" '"code":"sign_in_failed"'
    [ "$(step_status n1 sign_in:claude)" = failed ] || fail "failed sign-in not recorded"
    # The terminal's envelope is gone; status keeps the recorded failure.
    run 0 "$out" setup status n1 --json
    has "$out" '"id":"sign_in:claude","status":"failed","detail":"[^"]*","error":{"code":"sign_in_failed","message":"'
    lacks "$out" '"id":"install_agent:claude","status":"done","detail":"[^"]*","error"'
    has "$fixture/tty-commands" "exec '$path' 'auth' 'login'"
    in_tty '' "$out" sign-in n1 --agent claude --json
    has "$out" '"verified_by":"provider status command"'; has "$out" 'Hydra copied nothing'
    [ "$(step_status n1 sign_in:claude)" = "done" ] || fail "retried sign-in not done"
    run 0 "$out" setup status n1 --json; lacks "$out" '"error":{"code":"sign_in_failed"'
    [ "$(lines "$fixture/logins")" = 2 ] || fail "sign-in did not run twice"

    # Unverifiable sign-in (agy) needs the user's confirmation.
    cp "$fixture/agy" "$AGENTS_REMOTE_HOME/.local/bin/agy"
    in_tty 'n
' "$out" sign-in n1 --agent agy --json
    has "$out" '"code":"sign_in_unverified"'
    [ "$(step_status n1 sign_in:agy)" = failed ] || fail "unverified sign-in not failed"
    in_tty 'y
' "$out" sign-in n1 --agent agy --json
    has "$out" '"verified_by":"user confirmation"'
    has "$fixture/tty-commands" "exec '$AGENTS_REMOTE_HOME/.local/bin/agy'"

    # A private local recipe replaces the built-in one and is labelled local.
    recipes="$HYDRA_HOME/fleet/agent-recipes.json"
    printf '%s' '{"schema":"agent-recipes","schema_version":1,"recipes":[{"agent":"opencode","executable":"opencode","command":"curl -fsSL https://example.invalid/opencode.sh | sh","requires":["curl","hydra-missing-tool"],"expected_dirs":["~/.opencode/bin"],"login_args":["auth","login"],"docs_url":"https://example.invalid/docs","verified_on":"2026-09-28"}]}' > "$recipes"
    chmod 600 "$recipes"
    run 1 "$out" install-agent n1 --agent opencode --json
    has "$out" '"code":"prerequisite_missing"'; has "$out" '"missing":\["hydra-missing-tool"\]'
    sed 's/,"hydra-missing-tool"//' "$recipes" > "$fixture/recipes.tmp"; cat "$fixture/recipes.tmp" > "$recipes"
    run 3 "$out" install-agent n1 --agent opencode --json
    has "$out" '"source":"local"'; has "$out" 'example.invalid\\/opencode.sh'
    chmod 644 "$recipes"
    run 1 "$out" install-agent n1 --agent opencode --json
    has "$out" '"code":"recipe_unavailable"'
    printf '%s' '{"schema":"agent-recipes","schema_version":1,"recipes":[],"extra":1}' > "$recipes"; chmod 600 "$recipes"
    run 1 "$out" install-agent n1 --agent claude --json
    has "$out" '"code":"recipe_unavailable"'
    rm -f "$recipes"
    lacks "$fixture/curl-calls" 'example.invalid'

    # The guided flow walks the selected agent's steps: an installed agent is
    # skipped, sign-in stops without a terminal and finishes on one.
    write_state n2 "$base"',"agents":{"status":"done","detail":{"selected":["claude"]}}'
    run 1 "$out" setup n2 --json
    has "$out" '"id":"install_agent:claude","status":"skipped"'; has "$out" '"code":"tty_required"'
    in_tty '' "$out" setup n2 --json
    has "$out" '"id":"sign_in:claude","status":"done"'; has "$out" '"id":"alias","status":"done"'
}

(umask 022; cases 022)
(umask 002; cases 002)
printf 'Remote agent setup acceptance passed\n'
