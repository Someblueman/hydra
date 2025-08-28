#!/bin/sh
# Tests for YAML session configuration

test_count=0
pass_count=0
fail_count=0

HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"

assert_true() {
    cond="$1"
    msg="$2"
    test_count=$((test_count + 1))
    if eval "$cond"; then
        pass_count=$((pass_count + 1))
        echo "✓ $msg"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $msg"
    fi
}

setup() {
    base_dir="$(mktemp -d)" || exit 1
    repo_dir="$base_dir/repo"
    mkdir -p "$repo_dir"
    cd "$repo_dir" || exit 1
    git init >/dev/null 2>&1
    git config user.email test@example.com
    git config user.name "Test User"
    echo hi > one.txt
    git add one.txt
    git commit -m init >/dev/null 2>&1
    mkdir -p .hydra
    export HYDRA_HOME="$base_dir/.hydra"
    mkdir -p "$HYDRA_HOME"
}

teardown() {
    # kill sessions created by this test
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^yaml-' | while read -r s; do
        tmux kill-session -t "$s" 2>/dev/null || true
    done
    rm -rf "$base_dir"
}

echo "Testing YAML session config..."
setup

# Prepare directories and YAML config
mkdir -p sub/project logs

cat > .hydra/config.yml <<'YAML'
windows:
  - name: yaml-editor
    dir: sub
    env:
      FOO: bar
    panes:
      - cmd: echo editor
      - cmd: pwd
        split: v
        dir: sub/project
  - name: yaml-server
    panes:
      - cmd: sh -lc "echo $FOO"
      - cmd: echo srv
        split: h
YAML

"$HYDRA_BIN" spawn yaml-test >/dev/null 2>&1 || true

# Check that windows were created
assert_true "tmux list-windows -t yaml-test 2>/dev/null | grep -q 'yaml-editor'" "yaml-editor window created"
assert_true "tmux list-windows -t yaml-test 2>/dev/null | grep -q 'yaml-server'" "yaml-server window created"

# Check pane counts
assert_true "[ \"$(tmux list-panes -t yaml-test:0 2>/dev/null | wc -l | tr -d ' ')\" -eq 2 ]" "editor window has 2 panes"
assert_true "[ \"$(tmux list-panes -t yaml-test:1 2>/dev/null | wc -l | tr -d ' ')\" -ge 2 ]" "server window has >=2 panes"

# Check that window env is visible in server window
sleep 0.2
content="$(tmux capture-pane -pt yaml-test:1.0 2>/dev/null || true)"
echo "$content" | grep -q "bar" && pass_count=$((pass_count+1)) || fail_count=$((fail_count+1))
test_count=$((test_count+1))

# YAML guard: disable YAML and ensure no extra windows from config applied
HYDRA_DISABLE_YAML=1 "$HYDRA_BIN" spawn yaml-guard >/dev/null 2>&1 || true
assert_true "[ \"$(tmux list-windows -t yaml-guard 2>/dev/null | wc -l | tr -d ' ')\" -eq 1 ]" "YAML disabled guard results in single window"

teardown

echo ""
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"

if [ "$fail_count" -gt 0 ]; then
    exit 1
else
    exit 0
fi
