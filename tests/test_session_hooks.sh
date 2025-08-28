#!/bin/sh
# Tests for Hydra session hooks and startup commands

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
        echo "  Actual: $haystack"
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
    echo hi > file.txt
    git add file.txt
    git commit -m init >/dev/null 2>&1
    export HYDRA_HOME="$base_dir/.hydra"
    mkdir -p "$HYDRA_HOME"
}

teardown() {
    # Kill any sessions created
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^hooks-' | while read -r s; do
        tmux kill-session -t "$s" 2>/dev/null || true
    done
    rm -rf "$base_dir"
}

echo "Testing session hooks and startup..."
setup

# Create project .hydra with hooks
mkdir -p .hydra/hooks
printf '%s\n' "echo pre > \"$HYDRA_HOME/pre.txt\"" > .hydra/hooks/pre-spawn
chmod +x .hydra/hooks/pre-spawn

printf '%s\n' "echo post > \"$HYDRA_HOME/post.txt\"" > .hydra/hooks/post-spawn
chmod +x .hydra/hooks/post-spawn

# Custom layout hook (should not error)
printf '%s\n' "# no-op layout" > .hydra/hooks/layout
chmod +x .hydra/hooks/layout

# Startup commands file (not asserting execution due to non-interactive)
printf '%s\n' "echo from-startup" > .hydra/startup

out="$("$HYDRA_BIN" spawn hooks-test 2>&1 || true)"
assert_contains "$out" "Creating worktree for branch 'hooks-test'" "spawn started"

# Verify hooks side-effects
if [ -f "$HYDRA_HOME/pre.txt" ]; then
    pass_count=$((pass_count + 1)); echo "✓ pre-spawn hook executed"; else fail_count=$((fail_count + 1)); echo "✗ pre-spawn hook not executed"; fi
test_count=$((test_count + 1))
if [ -f "$HYDRA_HOME/post.txt" ]; then
    pass_count=$((pass_count + 1)); echo "✓ post-spawn hook executed"; else fail_count=$((fail_count + 1)); echo "✗ post-spawn hook not executed"; fi
test_count=$((test_count + 1))

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

