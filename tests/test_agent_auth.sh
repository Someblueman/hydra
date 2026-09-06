#!/bin/sh
# Public auth protocol with synthetic credentials and a real receiver.
set -eu
umask 077
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
export HYDRA_HOME="$fixture/hydra" HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export AUTH_TEST_HOME="$fixture/remote" AUTH_TEST_ROOT="$fixture"
mkdir "$fixture/bin" "$fixture/remote" "$fixture/local"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
set -eu
if [ "$1" = -t ]; then
    for arg do printf '%s\n' "$arg"; done > "$AUTH_TEST_ROOT/login-argv"
    exit 0
fi
while [ $# -gt 2 ]; do shift; done
request="$(cat)"
case "$request" in
    *'"operation":"copy"'*)
        case "${AUTH_TEST_MODE:-}" in
            echo) printf '%s' "$request" >&2; exit 1 ;;
            success-echo) printf '{"schema_version":1,"command":"fleet-auth","ok":true,"data":%s}' "$request"; exit 0 ;;
            partial) printf '{'; exit 1 ;;
        esac ;;
    *'"action":"handshake"'*)
        if [ "${AUTH_TEST_MODE:-}" = old ]; then
            printf '{"schema_version":1,"command":"fleet-handshake","ok":true,"data":{"hydra_version":"2.1.0","fleet_protocol":1,"state_schema":2,"event_schema":1,"json_schema":1,"capabilities":[]}}'
            exit 0
        fi ;;
esac
unset CODEX_HOME PI_CODING_AGENT_DIR XDG_DATA_HOME CLAUDE_CONFIG_DIR
export HOME="$AUTH_TEST_HOME"
printf '%s' "$request" | /bin/sh -c "$2"
SSH
chmod +x "$fixture/bin/ssh"
export PATH="$fixture/bin:$PATH"
hydra() { "$root/bin/hydra" "$@"; }
hydra remote add test test --hydra "$root/bin/hydra" >/dev/null
preview() { hydra fleet auth preview test --agent "$1" --source "$fixture/local/$1.json" ${2:+--provider} ${2:+"$2"} > "$fixture/preview"; }
approval() { sed -n 's/.*"approval_sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/preview"; }
reject() { if "$@" > "$fixture/error" 2>&1; then echo "Expected rejection: $*" >&2; exit 1; fi; }
printf '%s' '{"auth_mode":"apikey","OPENAI_API_KEY":"synthetic-codex-secret"}' > "$fixture/local/codex.json"
hydra fleet auth status test --agent codex > "$fixture/status"
grep -q '"credential_state":"absent"' "$fixture/status"
preview codex
hash="$(approval)"
[ "${#hash}" -eq 64 ]
reject hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$(printf '%064d' 0)"
[ ! -e "$fixture/remote/.codex/auth.json" ]
printf ' ' >> "$fixture/local/codex.json"
reject hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$hash"
preview codex
hash="$(approval)"
for mode in echo partial success-echo; do
    export AUTH_TEST_MODE="$mode"
    if [ "$mode" = success-echo ]; then
        hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$hash" > "$fixture/error"
    else
        reject hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$hash"
    fi
    if grep -q synthetic-codex-secret "$fixture/error"; then exit 1; fi
    [ ! -e "$fixture/remote/.codex/auth.json" ]
done
export AUTH_TEST_MODE=old
reject preview codex
unset AUTH_TEST_MODE
preview codex
hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$(approval)" > "$fixture/copy"
grep -q synthetic-codex-secret "$fixture/remote/.codex/auth.json"
if grep -q synthetic-codex-secret "$fixture/copy"; then exit 1; fi
# Existing destination rotation invalidates approval.
preview codex
printf ' ' >> "$fixture/remote/.codex/auth.json"
reject hydra fleet auth copy test --agent codex --source "$fixture/local/codex.json" --approve "$(approval)"
for agent in pi opencode; do
    if [ "$agent" = pi ]; then directory=.pi/agent; kind=api_key; else directory=.local/share/opencode; kind=api; fi
    mkdir -p "$fixture/remote/$directory"
    printf '{"unrelated":{"type":"%s","key":"keep-me"}}' "$kind" > "$fixture/remote/$directory/auth.json"
    printf '{"anthropic":{"type":"oauth","access":"synthetic-access","refresh":"synthetic-refresh","expires":999999},"other":{"type":"%s","key":"do-not-copy"}}' "$kind" > "$fixture/local/$agent.json"
    reject preview "$agent"
    preview "$agent" anthropic
    hydra fleet auth copy test --agent "$agent" --provider anthropic --source "$fixture/local/$agent.json" --approve "$(approval)" > "$fixture/copy"
    grep -q keep-me "$fixture/remote/$directory/auth.json"
    grep -q synthetic-refresh "$fixture/remote/$directory/auth.json"
    if grep -q do-not-copy "$fixture/remote/$directory/auth.json"; then exit 1; fi
    printf '{"anthropic":{"type":"%s","key":"!touch %s/executed"}}' "$kind" "$fixture" > "$fixture/local/$agent.json"
    reject preview "$agent" anthropic
    [ ! -e "$fixture/executed" ]
    printf '{"anthropic":{"type":"%s","key":"%s"}}' "$kind" "\$ENV_KEY" > "$fixture/local/$agent.json"
    reject preview "$agent" anthropic
done
printf '%s' '{"claudeAiOauth":{"accessToken":"synthetic-access","refreshToken":"synthetic-refresh"}}' > "$fixture/local/claude.json"
preview claude
hydra fleet auth copy test --agent claude --source "$fixture/local/claude.json" --approve "$(approval)" >/dev/null
# Private sources, regular files and safe destination directories are enforced.
chmod 644 "$fixture/local/codex.json"
reject preview codex
chmod 600 "$fixture/local/codex.json"
mv "$fixture/local/codex.json" "$fixture/local/real.json"
ln -s real.json "$fixture/local/codex.json"
reject preview codex
rm "$fixture/local/codex.json"
ln "$fixture/local/real.json" "$fixture/local/codex.json"
reject preview codex
rm "$fixture/local/codex.json"
mv "$fixture/local/real.json" "$fixture/local/codex.json"
chmod 777 "$fixture/remote/.codex"
reject preview codex
grep -q unsafe_store "$fixture/preview"
chmod 700 "$fixture/remote/.codex"
mv "$fixture/remote/.codex" "$fixture/remote/real"
ln -s real "$fixture/remote/.codex"
reject preview codex
rm "$fixture/remote/.codex"
mv "$fixture/remote/real" "$fixture/remote/.codex"
printf '%s\000' '{"OPENAI_API_KEY":"bad"}' > "$fixture/local/codex.json"
reject preview codex
for agent in codex pi opencode claude agy cursor; do
    hydra fleet auth login test --agent "$agent"
    grep -q -- '-t' "$fixture/login-argv"
    case "$agent" in cursor) grep -q "exec 'cursor-agent' login" "$fixture/login-argv" ;; *) grep -q "exec '$agent'" "$fixture/login-argv" ;; esac
done
for agent in agy cursor; do
    if hydra fleet auth preview test --agent "$agent" > "$fixture/unsupported-copy"; then exit 1; fi
    grep -q invalid_input "$fixture/unsupported-copy"
done
hydra fleet auth login test --agent codex --executable '/private/with space/codex'
grep -q "exec '/private/with space/codex' login --device-auth" "$fixture/login-argv"
printf 'Agent authentication acceptance passed\n'
