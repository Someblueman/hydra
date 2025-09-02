#!/bin/sh
# Tests for project-scoped mappings to avoid cross-repo collisions

test_count=0
pass_count=0
fail_count=0

# Source state lib
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"

assert_equal() {
  exp="$1"; got="$2"; msg="$3"
  test_count=$((test_count+1))
  if [ "$exp" = "$got" ]; then
    pass_count=$((pass_count+1)); echo "✓ $msg"
  else
    fail_count=$((fail_count+1)); echo "✗ $msg (expected '$exp', got '$got')"
  fi
}

assert_true() {
  cond="$1"; msg="$2"
  test_count=$((test_count+1))
  if eval "$cond"; then
    pass_count=$((pass_count+1)); echo "✓ $msg"
  else
    fail_count=$((fail_count+1)); echo "✗ $msg"
  fi
}

setup() {
  BASE_DIR="$(mktemp -d)" || exit 1
  export HYDRA_HOME="$BASE_DIR/.hydra"
  mkdir -p "$HYDRA_HOME"
  export HYDRA_MAP="$HYDRA_HOME/map"
  mkdir -p "$BASE_DIR/repoA" "$BASE_DIR/repoB"
  # Initialize two repos
  (cd "$BASE_DIR/repoA" && git init >/dev/null 2>&1 && git config user.email t@e && git config user.name t && echo a>f && git add f && git commit -m a >/dev/null 2>&1)
  (cd "$BASE_DIR/repoB" && git init >/dev/null 2>&1 && git config user.email t@e && git config user.name t && echo b>g && git add g && git commit -m b >/dev/null 2>&1)
}

teardown() {
  rm -rf "$BASE_DIR"
}

echo "Testing project-scoped mappings..."
setup

# In repoA, add mapping for branch 'main'
cd "$BASE_DIR/repoA" || exit 1
add_mapping main sessA
assert_true "grep -q 'sessA' \"$HYDRA_MAP\"" "repoA mapping written"

# In repoB, add mapping for same branch name but different session
cd "$BASE_DIR/repoB" || exit 1
add_mapping main sessB
assert_true "grep -q 'sessB' \"$HYDRA_MAP\"" "repoB mapping written"

# Verify lookups resolve to correct session per repo
cd "$BASE_DIR/repoA" || exit 1
gotA="$(get_session_for_branch main 2>/dev/null || true)"
assert_equal "sessA" "$gotA" "get_session_for_branch is repo-scoped (A)"

cd "$BASE_DIR/repoB" || exit 1
gotB="$(get_session_for_branch main 2>/dev/null || true)"
assert_equal "sessB" "$gotB" "get_session_for_branch is repo-scoped (B)"

# Verify list_mappings_current_repo filters
cd "$BASE_DIR/repoA" || exit 1
cntA="$(list_mappings_current_repo | wc -l | tr -d ' ')"
assert_equal "1" "$cntA" "list_mappings_current_repo returns only A entries"

cd "$BASE_DIR/repoB" || exit 1
cntB="$(list_mappings_current_repo | wc -l | tr -d ' ')"
assert_equal "1" "$cntB" "list_mappings_current_repo returns only B entries"

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
