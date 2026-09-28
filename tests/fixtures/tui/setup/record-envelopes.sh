#!/bin/sh
# Records the remote setup envelopes the native TUI tests replay, from the
# real `hydra remote ... --json` over a fake SSH boundary (the same approach as
# tests/test_remote_provision.sh and tests/test_remote_agents.sh): no network,
# a local "remote" HOME, a fake provider installer and a fake claude.
#
#   HYDRA_FLEET_BIN=build/hydra-fleet sh tests/fixtures/tui/setup/record-envelopes.sh
#
# Machine paths are normalised (remote HOME -> /home/deploy, local
# HYDRA_HOME -> /Users/you/.hydra, known_hosts -> /Users/you/.ssh/known_hosts);
# everything else, including plan hashes and the provisioning platform of the
# recording host, is the CLI's own output. Preflight envelopes come from a
# remote whose uname reports Linux x86_64. Host-key fixtures need a loopback
# sshd and are recorded separately (see tests/native/test_remote_setup_ssh.c).
set -eu
here="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
root="$(CDPATH='' cd -- "$here/../../../.." && pwd)"
fleet="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN names the hydra-fleet helper to record with}"
out_dir="${RECORD_OUT:-$here}"
work="$(mktemp -d "${TMPDIR:-/tmp}/hydra-record.XXXXXX")"
work="$(cd "$work" && pwd -P)"
trap 'rm -rf "$work"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
version="$(sed -n 's/^#define F_VERSION "\(.*\)"$/\1/p' "$root/src/fleet/fleet.h")"
os="$(uname -s | tr '[:upper:]' '[:lower:]')"
case "$(uname -m)" in arm64|aarch64) arch=aarch64 ;; *) arch=x86_64 ;; esac
asset="hydra-fleet-$version-$os-$arch"
unset CI HYDRA_NONINTERACTIVE HYDRA_ROOT
RECORD_ROOT="$work"
export RECORD_ROOT HYDRA_FLEET_BIN="$fleet"

# Fake SSH: `ssh -G` is OpenSSH's own answer; anything else runs locally as
# the remote user. RECORD_REMOTE_PATH replaces the remote PATH and
# RECORD_DROP loses the first install's response after it ran.
mkdir -p "$work/bin" "$work/remote-bin" "$work/linux-bin" "$work/assets"
cat > "$work/bin/ssh" <<'SSH'
#!/bin/sh
for arg do [ "$arg" = -G ] && exec "$RECORD_REAL_SSH" "$@"; done
while [ $# -gt 1 ]; do shift; done
unset HYDRA_HOME HYDRA_FLEET_BIN HYDRA_ROOT HYDRA_BIN_DIR HYDRA_LIB_DIR HYDRA_BIN_CMD HYDRA_FLEET_ASSETS_FILE HYDRA_FLEET_ASSET_BASE
HOME="$RECORD_REMOTE_HOME"
PATH="${RECORD_REMOTE_PATH:-$RECORD_ROOT/remote-bin:/usr/bin:/bin}"
export HOME PATH
umask "${RECORD_REMOTE_UMASK:-022}"
if [ -f "$RECORD_ROOT/drop" ]; then
    case "$1" in *"install '"*) rm -f "$RECORD_ROOT/drop"; /bin/sh -c "$1" >/dev/null 2>&1 || true; exit 255 ;; esac
fi
exec /bin/sh -c "$1"
SSH
cat > "$work/linux-bin/uname" <<'UNAME'
#!/bin/sh
case "${1:-}" in -m) echo x86_64 ;; *) echo Linux ;; esac
UNAME
# The provider installer drops the fake claude into ~/.local/bin.
cat > "$work/remote-bin/curl" <<'CURL'
#!/bin/sh
printf 'mkdir -p "$HOME/.local/bin"\ncat "%s" > "$HOME/.local/bin/claude"\nchmod 755 "$HOME/.local/bin/claude"\n' "$RECORD_ROOT/claude"
CURL
cat > "$work/claude" <<'AGENT'
#!/bin/sh
case "${1:-}" in
    --version) echo "2.1.3 (Claude Code)"; exit 0 ;;
    --help) echo "--print --session-id --resume stream-json"; exit 0 ;;
esac
if [ "${1:-}" = auth ] && [ "${2:-}" = login ]; then
    [ ! -f "$RECORD_ROOT/login-fails" ] || exit 1
    : > "$HOME/.claude-signed-in"; exit 0
fi
if [ "${1:-}" = auth ] && [ "${2:-}" = status ]; then [ -e "$HOME/.claude-signed-in" ]; exit; fi
exit 0
AGENT
cat > "$work/codex" <<'AGENT'
#!/bin/sh
[ "${1:-}" != --version ] || echo "codex-cli 0.46.0"
exit 0
AGENT
cat > "$work/cursor-agent" <<'AGENT'
#!/bin/sh
[ "${1:-}" != --version ] || echo "2025.09.18-a1b2c3d"
exit 0
AGENT
chmod 755 "$work/bin/ssh" "$work/linux-bin/uname" "$work/remote-bin/curl" "$work/claude" "$work/codex" "$work/cursor-agent"
cp "$work/codex" "$work/remote-bin/codex"
# link DIR TOOL...: a remote PATH directory holding only these tools.
link() {
    target=$1; shift
    for tool do
        found="$(command -v "$tool" 2>/dev/null)" || continue
        case "$found" in /*) ln -sf "$found" "$target/$tool" ;; esac
    done
}
link "$work/remote-bin" tmux
link "$work/linux-bin" awk df cat env mktemp head tail shasum sha256sum curl
mkdir -p "$work/linux-git"
link "$work/linux-git" git tmux
cp "$fleet" "$work/assets/$asset"
sha="$(shasum -a 256 "$fleet" 2>/dev/null || sha256sum "$fleet")"
printf 'version\tplatform\tsha256\tfilename\n%s\t%s-%s\t%s\t%s\n' "$version" "$os" "$arch" "${sha%% *}" "$asset" > "$work/assets.tsv"
printf 'version\tplatform\tsha256\tfilename\n' > "$work/no-assets.tsv"
cat > "$work/ssh_config" <<CONFIG
Host *
  UserKnownHostsFile $work/known_hosts
  GlobalKnownHostsFile /dev/null
CONFIG
RECORD_REAL_SSH="$(command -v ssh)"
PATH="$work/bin:$PATH"
HYDRA_FLEET_ASSET_BASE="file://$work/assets" HYDRA_FLEET_ASSETS_FILE="$work/assets.tsv"
export RECORD_REAL_SSH PATH HYDRA_FLEET_ASSET_BASE HYDRA_FLEET_ASSETS_FILE

# host NAME: a fresh local HYDRA_HOME and remote HOME for one recording.
host() {
    HYDRA_HOME="$work/$1/local" RECORD_REMOTE_HOME="$work/$1/remote"
    mkdir -p "$HYDRA_HOME" "$RECORD_REMOTE_HOME"
    chmod 700 "$HYDRA_HOME"
    export HYDRA_HOME RECORD_REMOTE_HOME
    unset RECORD_REMOTE_PATH RECORD_REMOTE_UMASK
}
# normalise: machine paths in (slash-escaped) JSON become stable examples.
normalise() {
    escaped_work="$(printf '%s' "$work" | sed 's|/|\\\\/|g')"
    sed -e "s|$escaped_work\\\\/[a-z0-9-]*\\\\/remote|\\\\/home\\\\/deploy|g" \
        -e "s|$escaped_work\\\\/[a-z0-9-]*\\\\/local|\\\\/Users\\\\/you\\\\/.hydra|g" \
        -e "s|$escaped_work\\\\/known_hosts|\\\\/Users\\\\/you\\\\/.ssh\\\\/known_hosts|g" \
        -e "s|$escaped_work\\\\/[a-z-]*bin|\\\\/usr\\\\/local\\\\/bin|g" \
        -e "s|$escaped_work\\\\/linux-git|\\\\/usr\\\\/bin|g"
}
# record FILE EXPECTED ARGS...: hydra remote ARGS --json without a terminal.
record() {
    file=$1 expected=$2; shift 2
    code=0
    "$root/bin/hydra" remote "$@" < /dev/null > "$work/out" 2> "$work/err" || code=$?
    if [ "$code" -ne "$expected" ]; then
        cat "$work/out" "$work/err" >&2
        printf 'record: remote %s exited %s, expected %s\n' "$*" "$code" "$expected" >&2
        exit 1
    fi
    normalise < "$work/out" > "$out_dir/$file"
}
# record_tty FILE ARGS...: the same on a pseudo-terminal; keeps the envelope.
record_tty() {
    file=$1; shift
    case "$(uname -s)" in
        Darwin) script -q /dev/null "$root/bin/hydra" remote "$@" < /dev/null > "$work/tty" 2>&1 || : ;;
        *)
            command="'$root/bin/hydra' remote"
            for arg do command="$command '$arg'"; done
            script -q -e -c "$command" /dev/null < /dev/null > "$work/tty" 2>&1 || : ;;
    esac
    tr -d '\r' < "$work/tty" | sed -n 's/^[^{]*\({"schema_version".*\)$/\1/p' | normalise > "$out_dir/$file"
    [ -s "$out_dir/$file" ] || { cat "$work/tty" >&2; printf 'record: no envelope from remote %s\n' "$*" >&2; exit 1; }
}
plan_hash() { sed -n 's/.*"plan_sha256":"\([0-9a-f]*\)".*/\1/p' "$out_dir/$1"; }

# Blocking and passing preflight on a Linux remote with a group-writable umask.
host preflight
RECORD_REMOTE_UMASK=002 RECORD_REMOTE_PATH="$work/linux-bin"
export RECORD_REMOTE_UMASK RECORD_REMOTE_PATH
record preflight-blocked.json 1 setup ovh deploy@ovh.example.net --ssh-config "$work/ssh_config" --json
RECORD_REMOTE_PATH="$work/linux-git:$work/linux-bin"
record preflight-ok.json 0 preflight ovh --json
record status-provision-pending.json 0 setup status ovh --json
# The same remote without tmux: a warning that means heads cannot run there.
mkdir -p "$work/git-bin"
link "$work/git-bin" git
RECORD_REMOTE_PATH="$work/git-bin:$work/linux-bin"
record preflight-tmux.json 0 preflight ovh --json

# The guided flow: pinned provisioning, the agent inventory, an approved
# installer, a failed then successful sign-in on a terminal, and completion.
host main
mkdir -p "$RECORD_REMOTE_HOME/.local/bin"
cp "$work/cursor-agent" "$RECORD_REMOTE_HOME/.local/bin/cursor-agent"
record provision-approval.json 3 setup ovh deploy@ovh.example.net --ssh-config "$work/ssh_config" --json
record status-provision-approval.json 0 setup status ovh --json
record provision-done.json 0 provision ovh --approve "$(plan_hash provision-approval.json)" --json
record agents.json 0 agents ovh --record "cursor-agent=$RECORD_REMOTE_HOME/.local/bin/cursor-agent" --json
record install-approval.json 3 install-agent ovh --agent claude --json
record install-done.json 0 install-agent ovh --agent claude --approve "$(plan_hash install-approval.json)" --json
record status-installed.json 0 setup status ovh --json
record sign-in-tty.json 1 setup ovh --json
: > "$work/login-fails"
record_tty sign-in-failed.json sign-in ovh --agent claude --json
rm -f "$work/login-fails"
record status-sign-failed.json 0 setup status ovh --json
record_tty sign-in-done.json sign-in ovh --agent claude --json
record status-signed.json 0 setup status ovh --json
record done.json 0 setup ovh --json
record status-done.json 0 setup status ovh --json
record list-done.json 0 setup list --json

# A lost install response, and an unpinned helper when no release row matches.
host unknown
record list-empty.json 0 setup list --json
record provision-unknown-plan.json 3 setup ovh deploy@ovh.example.net --ssh-config "$work/ssh_config" --json
: > "$work/drop"
record outcome-unknown.json 4 provision ovh --approve "$(plan_hash provision-unknown-plan.json)" --json
record list-progress.json 0 setup list --json
rm -f "$out_dir/provision-unknown-plan.json"
host unpinned
HYDRA_FLEET_ASSETS_FILE="$work/no-assets.tsv" record provision-approval-unpinned.json 3 setup ovh deploy@ovh.example.net --ssh-config "$work/ssh_config" --json
printf 'recorded remote setup envelopes in %s\n' "$out_dir"
