#!/bin/sh
# Guardrails for scripts/bench.sh (does not run the benchmark).

test_count=0
pass_count=0
fail_count=0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH="$SCRIPT_DIR/../scripts/bench.sh"

# shellcheck disable=SC1091
. "$SCRIPT_DIR/helpers.sh"

echo "Testing bench.sh isolation..."
echo "================================"

if grep -q "grep '^bench-'" "$BENCH"; then
    echo "[FAIL] bench.sh must not sweep the tmux server by bench-* prefix"
    fail_count=$((fail_count + 1))
else
    echo "[PASS] bench.sh must not sweep the tmux server by bench-* prefix"
    pass_count=$((pass_count + 1))
fi
test_count=$((test_count + 1))

# Literal pattern: bench.sh must contain cd "$repo"
# shellcheck disable=SC2016
if grep -q 'cd "$repo"' "$BENCH"; then
    echo "[PASS] bench.sh runs hydra from the throwaway repo"
    pass_count=$((pass_count + 1))
else
    echo "[FAIL] bench.sh runs hydra from the throwaway repo"
    fail_count=$((fail_count + 1))
fi
test_count=$((test_count + 1))

echo "================================"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
fi
echo "Some tests failed!"
exit 1
