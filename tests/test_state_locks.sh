#!/bin/sh
# Tests for cleanup_stale_locks in lib/state.sh

test_count=0
pass_count=0
fail_count=0

# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"

assert_true() {
  cond="$1"; msg="$2"
  test_count=$((test_count + 1))
  if eval "$cond"; then
    pass_count=$((pass_count + 1)); echo "✓ $msg"
  else
    fail_count=$((fail_count + 1)); echo "✗ $msg"
  fi
}

setup() {
  BASE_DIR="$(mktemp -d)" || exit 1
  export HYDRA_HOME="$BASE_DIR/.hydra"
  mkdir -p "$HYDRA_HOME/locks"
}

teardown() {
  rm -rf "$BASE_DIR"
}

echo "Testing cleanup_stale_locks..."
setup

# Create an old lock dir and a fresh one
mkdir -p "$HYDRA_HOME/locks/old.lock" "$HYDRA_HOME/locks/fresh.lock"
# Backdate old.lock far in the past (POSIX touch -t is allowed)
touch -t 200001010000 "$HYDRA_HOME/locks/old.lock" 2>/dev/null || true

# Run cleanup
cleanup_stale_locks

# Validate: old removed (if find is available), fresh remains
if command -v find >/dev/null 2>&1; then
  assert_true "[ ! -d \"$HYDRA_HOME/locks/old.lock\" ]" "old lock removed"
else
  # No find: we don't remove anything; just ensure no crash and dirs still exist
  assert_true "[ -d \"$HYDRA_HOME/locks/old.lock\" ]" "old lock preserved without find"
fi
assert_true "[ -d \"$HYDRA_HOME/locks/fresh.lock\" ]" "fresh lock preserved"

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

