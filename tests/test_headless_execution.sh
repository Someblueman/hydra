#!/bin/sh
# Terminal-free workspace, lifecycle, execution, and teardown acceptance.

set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
base="$(mktemp -d)"
repo="$base/repo"
home="$base/hydra"
fake="$base/bin"
tmux_log="$base/tmux.log"
mkdir -p "$repo" "$fake"
trap 'rm -rf "$base"' EXIT INT TERM

git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name "Hydra test"
printf 'headless\n' > "$repo/README"
git -C "$repo" add README
git -C "$repo" commit -qm initial

cat > "$fake/tmux" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> "$tmux_log"
exit 99
EOF
chmod +x "$fake/tmux"

export PATH="$fake:$PATH"
export HYDRA_HOME="$home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SETUP_CONTINUE=1
export HYDRA_FLEET_BIN="$root/build/hydra-fleet"
cd "$repo" || exit 1

tests=0
fail=0
check() {
    tests=$((tests + 1))
    if "$@"; then printf '[PASS] %s\n' "$*"; else printf '[FAIL] %s\n' "$*"; fail=$((fail + 1)); fi
}

check "$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
check "$root/bin/hydra" spawn --headless --no-agent headless-check >/dev/null 2>&1

project="$(sed -n '1p' .git/hydra/project-id)"
head_dir="$(find "$home/state/v2/projects/$project/heads" -type d -name 'head_*' | sed -n '1p')"
check test "$(sed -n '1p' "$head_dir/terminal-mode")" = headless
check test "$(sed -n '1p' "$head_dir/session")" = -
check test "$(sed -n '1p' "$head_dir/desired-state")" = headless
check "$root/bin/hydra" state verify >/dev/null 2>&1
check "$root/bin/hydra" list --json >/dev/null 2>&1
check "$root/bin/hydra" status --json >/dev/null 2>&1

wt="$(sed -n '1p' "$head_dir/worktree")"
check "$root/bin/hydra" exec --branch headless-check -- sh -c 'printf artifact > result.txt' >/dev/null 2>&1
check test -s "$wt/result.txt"

# A declared adapter uses the same terminal-free head and must not fall back
# to tmux for prompt delivery, observation, or artifact capture.
provider="$base/provider"
cat > "$provider" <<'PROVIDER'
#!/bin/sh
case "$1" in
    --help|--version) printf '%s\n' adapter-fixture; exit 0 ;;
esac
printf '%s\n' '{"schema_version":1,"type":"observation","status":"running"}'
printf '%s\n' '{"schema_version":1,"type":"result","text":"adapter artifact"}'
printf '%s\n' '{"schema_version":1,"type":"observation","status":"idle"}'
PROVIDER
chmod +x "$provider"
cat > "$base/profile.json" <<EOF
{"schema_version":1,"executable":"$provider","argv":[{"input":"prompt"}],"prompt":"argument","session":"none","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["adapter-fixture"]}
EOF
check "$root/bin/hydra" agent import adapter-fixture "$base/profile.json" >/dev/null 2>&1
printf adapter-prompt > "$base/prompt"
check "$root/bin/hydra" exec --branch headless-check --profile adapter-fixture \
    --prompt-file "$base/prompt" --require prompt,observations --result-file adapter.txt \
    --exit-code --json >/dev/null 2>&1
check test "$(cat "$wt/adapter.txt" 2>/dev/null || true)" = 'adapter artifact'
check "$root/bin/hydra" tui --data >/dev/null 2>&1
check "$root/bin/hydra" kill --force headless-check >/dev/null 2>&1
check test "$(sed -n '1p' "$head_dir/desired-state")" = stopped
check test ! -d "$wt"
check test ! -s "$tmux_log"

printf 'Tests: %s\n' "$((tests - fail))/$tests passed"
exit "$fail"
