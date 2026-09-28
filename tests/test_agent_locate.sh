#!/bin/sh
# Agent inventory beyond PATH (hydra agent locate): search directories,
# private location records and their validity rules, PATH precedence, shell
# (profile_executable_path) and native (agent probe / agent-run) resolver
# parity, spawn with a recorded location, and the receiver's agent-inventory
# and agent-locate-record actions. Every case runs under umask 022 and 002.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
fixture="$(cd "$fixture" && pwd -P)"
fleet="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_FLEET_BIN="$fleet"
# A private tmux server: spawn below must never touch the user's sessions.
mkdir "$fixture/tmux"
export TMUX_TMPDIR="$fixture/tmux"
cleanup() {
    for socket in "$fixture"/tmux/tmux-*/*; do
        [ -S "$socket" ] && tmux -S "$socket" kill-server 2>/dev/null || :
    done
    rm -rf "$fixture"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
export HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1 LOCATE_TEST_ROOT="$fixture"
unset HYDRA_SKIP_AI CI

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
has() { grep -q -- "$2" "$1" || { cat "$1" >&2; fail "$1 lacks $2"; }; }
exact_mode() { [ -n "$(find "$1" -prune -perm "$2")" ] || fail "$1 is not mode $2"; }
hydra() { "$root/bin/hydra" "$@"; }

# Real agents installed on this machine must not leak into the fixture: keep
# only tmux beside the system directories, and skip if a searched system
# directory already provides the fixture's agent.
mkdir "$fixture/tools"
tmux_bin="$(command -v tmux)" || fail "tmux is required"
ln -s "$tmux_bin" "$fixture/tools/tmux"
base_path="$fixture/tools:/usr/bin:/bin:/usr/sbin:/sbin"
for dir in /usr/local/bin /opt/homebrew/bin /home/linuxbrew/.linuxbrew/bin /snap/bin /usr/bin /bin; do
    if [ -e "$dir/claude" ]; then
        printf 'SKIP: %s/claude exists on this host\n' "$dir"
        exit 0
    fi
done

# A fixture Claude that satisfies the built-in help probe and marks launches.
write_agent() {
    mkdir -p "$(dirname "$1")"
    cat > "$1" <<'AGENT'
#!/bin/sh
case "${1:-}" in
    --version) echo "fixture-claude 1.0"; exit 0 ;;
    --help) echo "--print --session-id --resume stream-json"; exit 0 ;;
esac
printf '%s\n' "$0" >> "$LOCATE_TEST_ROOT/launched"
AGENT
    chmod 755 "$1"
}
shell_path() { hydra agent show claude | sed -n 's/^  resolved_path: //p'; }
native_path() {
    hydra agent probe claude > "$fixture/probe.json" || return 0
    sed -n 's/.*"executable_path":"\([^"]*\)".*/\1/p' "$fixture/probe.json" | sed 's|\\/|/|g'
}
# parity EXPECTED: both resolvers agree on EXPECTED ("unavailable" for none).
parity() {
    shell="$(shell_path)" native="$(native_path)"
    [ -n "$native" ] || native=unavailable
    [ "$shell" = "$1" ] || fail "shell resolved '$shell', expected '$1'"
    [ "$native" = "$1" ] || fail "native resolved '$native', expected '$1'"
}

cases() {
    home="$fixture/home-$1"
    export HOME="$home" HYDRA_HOME="$home/.hydra" PATH="$base_path"
    # A private home until R1 makes Hydra state private under umask 002.
    (umask 077; mkdir -p "$HYDRA_HOME")
    out="$fixture/out"
    local_bin="$home/.local/bin"

    # Missing everywhere.
    hydra agent locate --json > "$out"
    has "$out" '"schema":"agent-inventory"'; has "$out" '"~/.local/bin"'; has "$out" '"~/.nvm/versions/node/\*/bin"'
    has "$out" '"profile":"claude","executable":"claude","on_path":null,"candidates":\[\],"recorded":null,"status":"missing"'
    has "$out" '"profile":"cursor","executable":"cursor-agent"'
    parity unavailable

    # Only in ~/.local/bin: found, not yet used.
    write_agent "$local_bin/claude"
    hydra agent locate --json > "$out"
    has "$out" "\"candidates\":\[{\"path\":\"$local_bin/claude\",\"source\":\"search\",\"version\":\"fixture-claude 1.0\",\"recordable\":true}\],\"recorded\":null,\"status\":\"found_off_path\""
    hydra agent locate > "$out"
    has "$out" "found_off_path  $local_bin/claude (record: hydra agent locate --record claude $local_bin/claude)"
    parity unavailable

    # Several nvm versions: ambiguous.
    write_agent "$home/.nvm/versions/node/v20/bin/claude"
    write_agent "$home/.nvm/versions/node/v22/bin/claude"
    hydra agent locate --json > "$out"
    has "$out" '"status":"ambiguous"'
    rm -rf "$home/.nvm"

    # Refused records leave nothing behind.
    for bad in "claude relative/claude" "claude $local_bin/missing/claude" "claude-x $local_bin/claude" "../x $local_bin/claude"; do
        # shellcheck disable=SC2086
        if hydra agent locate --json --record $bad > "$out"; then fail "recorded $bad"; fi
        has "$out" '"ok":false'
    done
    has "$out" '"code":"invalid_input"'
    printf '#!/bin/sh\n' > "$fixture/claude"; chmod 644 "$fixture/claude"
    if hydra agent locate --json --record claude "$fixture/claude" > "$out"; then fail "recorded a non-executable"; fi
    has "$out" '"code":"location_invalid"'
    chmod 775 "$local_bin/claude"
    if hydra agent locate --json --record claude "$local_bin/claude" > "$out"; then fail "recorded a group-writable file"; fi
    has "$out" 'group/world-writable'
    chmod 757 "$local_bin/claude"
    if hydra agent locate --json --record claude "$local_bin/claude" > "$out"; then fail "recorded a world-writable file"; fi
    chmod 755 "$local_bin/claude"
    [ ! -e "$HYDRA_HOME/agents/locations/claude" ] || fail "a refused record was written"
    if [ "$(id -u)" = 0 ]; then
        write_agent "$fixture/foreign/claude"; chown nobody "$fixture/foreign/claude"
        if hydra agent locate --json --record claude "$fixture/foreign/claude" > "$out"; then fail "recorded a foreign file"; fi
    fi

    # A valid record: private files whatever the umask, used by both resolvers.
    hydra agent locate --json --record claude "$local_bin/claude" > "$out"
    has "$out" "\"recorded\":\"$local_bin/claude\""
    exact_mode "$HYDRA_HOME/agents" 700; exact_mode "$HYDRA_HOME/agents/locations" 700
    exact_mode "$HYDRA_HOME/agents/locations/claude" 600
    hydra agent locate --json > "$out"
    has "$out" "\"recorded\":\"$local_bin/claude\",\"status\":\"recorded\""
    parity "$local_bin/claude"
    hydra agent list > "$out"; has "$out" 'claude       yes'
    has "$fixture/probe.json" '"invocation_help":true'
    printf 'prompt\n' > "$fixture/prompt"
    "$fleet" agent-run preflight claude "$fixture/prompt" "" > "$out" || { cat "$out" >&2; fail "agent-run preflight did not resolve the record"; }
    has "$out" "\"executable_path\":\"$(printf '%s' "$local_bin/claude" | sed 's|/|\\\\/|g')\""

    # Records apply to executables, not profiles: cursor resolves cursor-agent.
    write_agent "$local_bin/cursor-agent"
    hydra agent locate --record cursor-agent "$local_bin/cursor-agent" > "$out"
    [ "$(hydra agent show cursor | sed -n 's/^  resolved_path: //p')" = "$local_bin/cursor-agent" ] || fail "cursor did not use the cursor-agent record"
    hydra agent locate --forget cursor-agent > "$out"
    [ ! -e "$HYDRA_HOME/agents/locations/cursor-agent" ] || fail "forget kept the record"

    # Built-in names stay reserved: a custom profile cannot claim claude.
    if hydra agent init claude --executable "$local_bin/claude" > "$out" 2>&1; then fail "custom profile named claude was created"; fi
    [ ! -e "$HYDRA_HOME/profiles/claude" ] || fail "custom claude profile directory exists"

    # Invalid records and targets are ignored by both resolvers alike.
    chmod 775 "$local_bin/claude"; parity unavailable; chmod 755 "$local_bin/claude"
    chmod 620 "$HYDRA_HOME/agents/locations/claude"; parity unavailable; chmod 600 "$HYDRA_HOME/agents/locations/claude"
    mv "$HYDRA_HOME/agents/locations/claude" "$fixture/record"
    ln -s "$fixture/record" "$HYDRA_HOME/agents/locations/claude"; parity unavailable
    rm "$HYDRA_HOME/agents/locations/claude"; mv "$fixture/record" "$HYDRA_HOME/agents/locations/claude"
    parity "$local_bin/claude"
    # A symlinked location keeps its invoked name.
    write_agent "$home/.local/share/claude/versions/1.0/claude-bin"
    mv "$local_bin/claude" "$fixture/claude-plain"
    ln -s "$home/.local/share/claude/versions/1.0/claude-bin" "$local_bin/claude"
    parity "$local_bin/claude"
    rm "$local_bin/claude"; mv "$fixture/claude-plain" "$local_bin/claude"

    # PATH always wins over a record.
    write_agent "$fixture/pathbin-$1/claude"
    PATH="$fixture/pathbin-$1:$base_path"
    hydra agent locate --json > "$out"
    has "$out" "\"on_path\":\"$fixture/pathbin-$1/claude\""
    has "$out" '"status":"on_path"'
    parity "$fixture/pathbin-$1/claude"
    # A non-executable file earlier in PATH hides nothing.
    mkdir "$fixture/shadow-$1"; printf '#!/bin/sh\n' > "$fixture/shadow-$1/claude"; chmod 644 "$fixture/shadow-$1/claude"
    PATH="$fixture/shadow-$1:$fixture/pathbin-$1:$base_path"
    parity "$fixture/pathbin-$1/claude"
    PATH="$fixture/shadow-$1:$base_path"
    parity "$local_bin/claude"
    PATH="$base_path"

    # The receiver: read-only inventory, probe at a path, record delegation.
    printf '{"protocol":1,"action":"agent-inventory"}' | hydra fleet serve > "$out" || :
    has "$out" '"command":"agent-locate"'; has "$out" '"status":"recorded"'
    printf '{"protocol":1,"action":"agent-inventory","args":["probe","claude","%s"]}' "$local_bin/claude" | hydra fleet serve > "$out" || :
    has "$out" '"invocation_help":true'
    chmod 775 "$local_bin/claude"
    printf '{"protocol":1,"action":"agent-inventory","args":["probe","claude","%s"]}' "$local_bin/claude" | hydra fleet serve > "$out" || :
    has "$out" '"code":"location_invalid"'
    chmod 755 "$local_bin/claude"
    write_agent "$local_bin/codex"
    printf '{"protocol":1,"action":"agent-locate-record","args":["codex","%s"]}' "$local_bin/codex" | hydra fleet serve > "$out" || :
    has "$out" '"ok":true'
    [ "$(cat "$HYDRA_HOME/agents/locations/codex")" = "$local_bin/codex" ] || fail "receiver did not record codex"
    printf '{"protocol":1,"action":"agent-locate-record","args":["codex","relative"]}' | hydra fleet serve > "$out" || :
    has "$out" '"code":"location_invalid"'
    printf '{"protocol":1,"action":"handshake"}' | hydra fleet serve > "$out" || :
    has "$out" '"agent-inventory","agent-locate-record"'; has "$out" '"platform":{"os":"'
    hydra agent locate --forget codex > /dev/null

    # spawn launches the recorded executable by absolute path.
    repo="$fixture/repo-$1"
    mkdir "$repo"
    (
        cd "$repo"
        git init -q
        git config user.name Test; git config user.email test@example.invalid
        git -c commit.gpgSign=false commit --allow-empty -qm base
        hydra init --no-agent --trust > /dev/null
        rm -f "$fixture/launched"
        hydra spawn "locate-$1" --profile claude > "$out" 2>&1 || { cat "$out" >&2; exit 1; }
    ) || fail "spawn with a recorded agent failed"
    tries=0
    while [ ! -s "$fixture/launched" ] && [ "$tries" -lt 50 ]; do sleep 0.1; tries=$((tries + 1)); done
    [ "$(sed -n 1p "$fixture/launched")" = "$local_bin/claude" ] || fail "spawn did not launch the recorded claude"
    (cd "$repo" && HYDRA_NONINTERACTIVE=1 hydra kill "locate-$1" --force > /dev/null 2>&1) || :
}

(umask 022; cases 022)
(umask 002; cases 002)
printf 'Agent locate acceptance passed\n'
