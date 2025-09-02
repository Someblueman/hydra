#!/bin/sh
# Tests for Procfile-based process launching

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
    printf '%s\n' one > one.txt
    git add one.txt
    git commit -m init >/dev/null 2>&1
    mkdir -p .hydra
    export HYDRA_HOME="$base_dir/.hydra"
    mkdir -p "$HYDRA_HOME"
}

teardown() {
    # kill sessions created by this test
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^pf-' | while read -r s; do
        tmux kill-session -t "$s" 2>/dev/null || true
    done
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^pfguard-' | while read -r s; do
        tmux kill-session -t "$s" 2>/dev/null || true
    done
    rm -rf "$base_dir"
}

echo "Testing Procfile process launch..."

# Skip if tmux is not available
if ! command -v tmux >/dev/null 2>&1; then
    echo "⚠ tmux not available - skipping Procfile tests"
    echo "Test Results:"
    echo "  Total: 0"
    echo "  Passed: 0"
    echo "  Failed: 0"
    exit 0
fi

setup

# Create a Procfile in project config
cat > .hydra/Procfile <<'PF'
web: echo web
worker: echo worker
PF

# Spawn a head; expect two process windows
"$HYDRA_BIN" spawn pf-test >/dev/null 2>&1 || true

assert_true "tmux list-windows -t pf-test 2>/dev/null | grep -q 'proc-web'" "proc-web window created"
assert_true "tmux list-windows -t pf-test 2>/dev/null | grep -q 'proc-worker'" "proc-worker window created"

# Guard: disabling Procfile should result in single default window
HYDRA_DISABLE_PROCFILE=1 "$HYDRA_BIN" spawn pfguard-test >/dev/null 2>&1 || true
assert_true "[ \"$(tmux list-windows -t pfguard-test 2>/dev/null | wc -l | tr -d ' ')\" -eq 1 ]" "Procfile disabled yields single window"

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

