#!/bin/sh
# Record this checkout's native TUI and a real SSH fleet host.
set -eu
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
: "${HYDRA_DEMO_HOST:?Set HYDRA_DEMO_HOST to a trusted SSH target}"
: "${HYDRA_DEMO_REMOTE_HYDRA:?Set HYDRA_DEMO_REMOTE_HYDRA to its qualified absolute Hydra path}"
case "$HYDRA_DEMO_REMOTE_HYDRA" in /*) ;; *) exit 2 ;; esac
case "$HYDRA_DEMO_REMOTE_HYDRA" in *[!a-zA-Z0-9/_.-]*) echo 'Unsupported remote path characters' >&2; exit 2 ;; esac
for tool in git tmux vhs ffmpeg ttyd codex ssh; do
    command -v "$tool" >/dev/null || { echo "Missing recording tool: $tool" >&2; exit 1; }
done
make -C "$repo" build-tui build-fleet
scratch=$(mktemp -d /tmp/hydra-record.XXXXXX)
HYDRA_DEMO_SSH=$(command -v ssh)
HYDRA_DEMO_TMUX=$(command -v tmux)
HYDRA_DEMO_SOCKET="$scratch/tmux.sock"
export HYDRA_DEMO_TMUX HYDRA_DEMO_SOCKET
remote=''
remote_run() { "$HYDRA_DEMO_SSH" -o BatchMode=yes -o StrictHostKeyChecking=yes "$HYDRA_DEMO_HOST" "$@"; }
cleanup() {
    "$HYDRA_DEMO_TMUX" -S "$HYDRA_DEMO_SOCKET" kill-server 2>/dev/null || true
    if [ -n "$remote" ]; then
        remote_run "sh -s -- '$HYDRA_DEMO_REMOTE_HYDRA' '$remote'" <<'REMOTE' || echo "Remote cleanup failed: inspect $remote" >&2
set -eu
export HYDRA_HOME="$2/state" HYDRA_NONINTERACTIVE=1
cd "$2/project"
"$1" kill "fleet-${2##*.}" || exit 1
[ "$(git worktree list --porcelain | grep -c '^worktree ')" -eq 1 ]
cd /
rm -rf "$2"
REMOTE
    fi
    rm -rf "$scratch"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
remote=$(remote_run 'mktemp -d /tmp/hydra-record.XXXXXX')
case "$remote" in /tmp/hydra-record.*) ;; *) remote=''; exit 1 ;; esac
case "$remote" in *[!a-zA-Z0-9/_.-]*) remote=''; exit 1 ;; esac
remote_run "sh -s -- '$remote'" <<'REMOTE'
set -eu
mkdir "$1/project"
cd "$1/project"
git init -q -b main
git config user.name 'Hydra Demo'
git config user.email 'demo@example.invalid'
printf '# Remote build project\n' > README.md
git add README.md
git commit -qm 'Initial project'
REMOTE
mkdir -p "$scratch/bin" "$scratch/project" "$repo/build/demo"
# Resolve the supplied host privately; only the neutral alias reaches the recording.
HYDRA_DEMO_SSH_CONFIG="$scratch/ssh-config"
export HYDRA_DEMO_SSH HYDRA_DEMO_SSH_CONFIG
"$HYDRA_DEMO_SSH" -G "$HYDRA_DEMO_HOST" > "$scratch/ssh-resolved"
sed '1s/.*/Host hydra-demo/' "$scratch/ssh-resolved" > "$HYDRA_DEMO_SSH_CONFIG"
chmod 600 "$HYDRA_DEMO_SSH_CONFIG"
cat > "$scratch/bin/ssh" <<'SSH'
#!/bin/sh
exec "$HYDRA_DEMO_SSH" -F "$HYDRA_DEMO_SSH_CONFIG" -o LogLevel=ERROR "$@"
SSH
chmod +x "$scratch/bin/ssh"
cat > "$scratch/bin/tmux" <<'WRAPPER'
#!/bin/sh
exec "$HYDRA_DEMO_TMUX" -f /dev/null -S "$HYDRA_DEMO_SOCKET" "$@"
WRAPPER
cat > "$scratch/bin/codex-plan" <<'AGENT'
#!/bin/sh
codex exec --ephemeral --sandbox read-only --color never \
    --output-last-message "$HYDRA_DEMO_RESULT" "$1"
status=$?
if [ "$status" -eq 0 ]; then
    printf '\033[2J\033[H'
    cat "$HYDRA_DEMO_RESULT"
fi
printf '%s\n' "$status" > "$HYDRA_DEMO_RESULT.status"
exit "$status"
AGENT
chmod +x "$scratch/bin/tmux" "$scratch/bin/codex-plan"
export HYDRA_ROOT="$repo" HYDRA_HOME="$scratch/state" HYDRA_TUI_BIN="$repo/build/hydra-tui"
export PATH="$scratch/bin:$repo/bin:$PATH"
export HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
unset HYDRA_SKIP_AI TMUX ENV BASH_ENV ZDOTDIR
# This recording explicitly demonstrates color; normal NO_COLOR behavior is unchanged.
unset NO_COLOR
export HYDRA_TUI_THEME=terminal
export HYDRA_DEMO_PROJECT="$scratch/project" HYDRA_DEMO_RESULT="$scratch/agent-result"
export REMOTE_PROJECT="$remote/project" REMOTE_HEAD="fleet-${remote##*.}"
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export PS1='$ ' PROMPT_COMMAND=''
cd "$HYDRA_DEMO_PROJECT"
git init -q -b main
git config user.name 'Hydra Demo'
git config user.email 'demo@example.invalid'
printf '# Search service\n\nA small C project for indexing and searching local text files.\n' > README.md
git add README.md
git commit -qm 'Initial project'
hydra init --no-agent --trust --worktree-root "$scratch/heads"
hydra agent init codex-plan --executable "$scratch/bin/codex-plan" --prompt-mode task-file
hydra spawn search --profile codex-plan --prompt 'Read README.md. Give a three-bullet implementation plan for search in this C project. Do not modify files or run tests. Keep the answer under 90 words.'
hydra spawn tests --no-agent --prompt 'Review the search test strategy'
hydra remote add build hydra-demo --hydra "$HYDRA_DEMO_REMOTE_HYDRA" --home "$remote/state"
hydra fleet init build --project "$REMOTE_PROJECT" -- --no-agent --trust --worktree-root "$remote/heads"
# Bound the real agent task; never replace an unavailable agent with canned output.
remaining=120
while [ ! -f "$HYDRA_DEMO_RESULT.status" ] && [ "$remaining" -gt 0 ]; do
    sleep 1
    remaining=$((remaining - 1))
done
[ "$(cat "$HYDRA_DEMO_RESULT.status")" = 0 ]
[ -s "$HYDRA_DEMO_RESULT" ]
tmux clear-history -t search
cp "$HYDRA_DEMO_RESULT" "$repo/build/demo/agent-result.txt"
hydra tui --capabilities > "$repo/build/demo/tui-capabilities.txt"
cd "$repo"
vhs assets/demos/quick-tour.tape
hydra fleet tui-data > "$repo/build/demo/fleet.tsv"
grep -F "$REMOTE_HEAD" "$repo/build/demo/fleet.tsv" >/dev/null
cd "$HYDRA_DEMO_PROJECT"
hydra kill search
hydra kill tests
[ "$(git worktree list --porcelain | grep -c '^worktree ')" -eq 1 ]
printf 'Verified: real agent output, remote fleet head, and local cleanup.\n'
