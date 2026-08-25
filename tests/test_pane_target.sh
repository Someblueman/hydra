#!/bin/sh
# Pane targeting tests using tests/bin/tmux

test_count=0
pass_count=0
fail_count=0

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PATH="$SCRIPT_DIR/bin:$PATH"
export PATH

HYDRA_LIB_DIR="$(cd "$SCRIPT_DIR/../lib" && pwd)"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/tmux.sh"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/helpers.sh"

test_send_keys_uses_primary_pane() {
    echo "Testing send_keys_to_session targets :0.0..."

    log="$(mktemp)"
    HYDRA_TEST_TMUX_LOG="$log"
    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    export HYDRA_TEST_TMUX_LOG HYDRA_TEST_TMUX_SESSIONS

    send_keys_to_session "hydra-feat" "claude"
    assert_success $? "send_keys_to_session should succeed against stub"

    if grep -q "send-keys -t hydra-feat:0.0 claude" "$log"; then
        echo "[PASS] Agent/startup keys go to session:0.0"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Agent/startup keys go to session:0.0"
        echo "  log: $(cat "$log")"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
    rm -f "$log"
}

test_find_broadcast_pane_prefers_shell() {
    echo "Testing find_broadcast_pane skips agent pane..."

    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 claude 0 \
        hydra-feat 0 1 1700000000 bash 0)"
    export HYDRA_TEST_TMUX_SESSIONS HYDRA_TEST_TMUX_PANES

    result="$(find_broadcast_pane "hydra-feat" "claude")"
    assert_equal "hydra-feat:0.1" "$result" "broadcast prefers non-agent shell pane"
}

test_find_broadcast_pane_refuses_agent_only() {
    echo "Testing find_broadcast_pane refuses agent-only session..."

    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 claude 0)"
    export HYDRA_TEST_TMUX_SESSIONS HYDRA_TEST_TMUX_PANES

    find_broadcast_pane "hydra-feat" "claude" >/dev/null 2>&1
    assert_failure $? "broadcast refuses when only the agent pane exists"
}

echo "Running pane targeting tests..."
echo "================================"
test_send_keys_uses_primary_pane
test_find_broadcast_pane_prefers_shell
test_find_broadcast_pane_refuses_agent_only
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
