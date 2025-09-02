#!/bin/sh
# Tests for Hydra TUI control panel (non-interactive mode)

test_count=0
pass_count=0
fail_count=0

HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"

assert_contains() {
    haystack="$1"
    needle="$2"
    message="$3"
    test_count=$((test_count + 1))
    if echo "$haystack" | grep -q "$needle"; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected to contain: '$needle'"
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
    export HYDRA_HOME="$base_dir/.hydra"
    mkdir -p "$HYDRA_HOME"
}

teardown() {
    # Kill any sessions created
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^panel-' | while read -r s; do
        tmux kill-session -t "$s" 2>/dev/null || true
    done
    rm -rf "$base_dir"
}

echo "Testing TUI control panel (non-interactive output)..."

# Skip if tmux is not available
if ! command -v tmux >/dev/null 2>&1; then
    echo "⚠ tmux not available - skipping panel tests"
    echo "Test Results:"
    echo "  Total: 0"
    echo "  Passed: 0"
    echo "  Failed: 0"
    exit 0
fi

setup

"$HYDRA_BIN" spawn panel-a >/dev/null 2>&1 || true
"$HYDRA_BIN" spawn panel-b >/dev/null 2>&1 || true

out="$(HYDRA_PANEL_NONINTERACTIVE=1 "$HYDRA_BIN" panel 2>/dev/null || true)"
assert_contains "$out" "Hydra Control Panel" "prints header"
assert_contains "$out" "panel-a" "lists first head"
assert_contains "$out" "panel-b" "lists second head"

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

