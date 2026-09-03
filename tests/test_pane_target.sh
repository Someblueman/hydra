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

test_find_broadcast_pane_detects_live_agent_when_map_blank() {
    echo "Testing find_broadcast_pane detects agent on :0.0 when map AI is -..."

    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 claude 0)"
    export HYDRA_TEST_TMUX_SESSIONS HYDRA_TEST_TMUX_PANES

    find_broadcast_pane "hydra-feat" "-" >/dev/null 2>&1
    assert_failure $? "broadcast refuses agent-only session even when map AI is -"

    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 claude 0 \
        hydra-feat 0 1 1700000000 bash 0)"
    export HYDRA_TEST_TMUX_PANES
    result="$(find_broadcast_pane "hydra-feat" "-")"
    assert_equal "hydra-feat:0.1" "$result" "blank map AI still prefers non-agent shell pane"

    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 bash 0)"
    export HYDRA_TEST_TMUX_PANES
    result="$(find_broadcast_pane "hydra-feat" "-")"
    assert_equal "hydra-feat:0.0" "$result" "no live agent allows :0.0 when map AI is -"
}

test_find_broadcast_pane_uses_shell_after_agent_exits() {
    echo "Testing find_broadcast_pane uses :0.0 after agent exits..."

    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 bash 0)"
    export HYDRA_TEST_TMUX_SESSIONS HYDRA_TEST_TMUX_PANES

    result="$(find_broadcast_pane "hydra-feat" "claude")"
    assert_equal "hydra-feat:0.0" "$result" "stored AI does not skip a live shell on :0.0"
}

test_find_broadcast_pane_refuses_non_shell() {
    echo "Testing find_broadcast_pane refuses vim/repl panes..."

    HYDRA_TEST_TMUX_SESSIONS="hydra-feat"
    HYDRA_TEST_TMUX_PANES="$(printf '%s\t%s\t%s\t%s\t%s\t%s\n%s\t%s\t%s\t%s\t%s\t%s\n' \
        hydra-feat 0 0 1700000000 claude 0 \
        hydra-feat 0 1 1700000000 vim 0)"
    export HYDRA_TEST_TMUX_SESSIONS HYDRA_TEST_TMUX_PANES

    find_broadcast_pane "hydra-feat" "claude" >/dev/null 2>&1
    assert_failure $? "broadcast refuses when the other pane is not a shell"
}

test_send_keys_ignores_stale_snapshot() {
    echo "Testing send_keys_to_session live-probes after snapshot..."

    log="$(mktemp)"
    HYDRA_TEST_TMUX_LOG="$log"
    HYDRA_TEST_TMUX_SESSIONS="hydra-old"
    export HYDRA_TEST_TMUX_LOG HYDRA_TEST_TMUX_SESSIONS
    tmux_load_snapshot

    HYDRA_TEST_TMUX_SESSIONS="$(printf '%s\n%s\n' hydra-old hydra-new)"
    export HYDRA_TEST_TMUX_SESSIONS
    send_keys_to_session "hydra-new" "claude"
    assert_success $? "send_keys should see a session created after the snapshot"

    if grep -q "send-keys -t hydra-new:0.0 claude" "$log"; then
        echo "[PASS] send-keys reached the newly created session"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] send-keys reached the newly created session"
        echo "  log: $(cat "$log")"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
    tmux_clear_snapshot
    rm -f "$log"
}

test_broadcast_session_qualified_pane() {
    echo "Testing broadcast --pane session:target stays on that session..."

    TEST_HOME="$(mktemp -d)"
    HYDRA_HOME="$TEST_HOME"
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    export HYDRA_HOME HYDRA_STATE_V2_ROOT

    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/output.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/locks.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/identity.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state_v2.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/cmd_session_ops.sh"

    TEST_PROJECT_ID=project_0123456789abcdefabcd
    export HYDRA_HOME HYDRA_STATE_V2_ROOT TEST_PROJECT_ID
    # shellcheck disable=SC2329
    hydra_get_project_id() { printf '%s\n' "$TEST_PROJECT_ID"; }
    state_v2_create_head "$TEST_PROJECT_ID" b1 hydra-a - - 100 - - "$TEST_HOME/repo" >/dev/null
    state_v2_create_head "$TEST_PROJECT_ID" b2 hydra-b - - 100 - - "$TEST_HOME/repo" >/dev/null

    log="$(mktemp)"
    HYDRA_TEST_TMUX_LOG="$log"
    HYDRA_TEST_TMUX_SESSIONS="$(printf '%s\n%s\n' hydra-a hydra-b)"
    export HYDRA_TEST_TMUX_LOG HYDRA_TEST_TMUX_SESSIONS

    cmd_broadcast --pane hydra-a:0.1 "echo hi" >/dev/null

    hits="$(grep -c 'send-keys -t hydra-a:0.1' "$log" || true)"
    assert_equal "1" "$hits" "session-qualified --pane sends once to that session"
    if grep -q 'send-keys -t hydra-b' "$log"; then
        echo "[FAIL] session-qualified --pane must not target other sessions"
        fail_count=$((fail_count + 1))
    else
        echo "[PASS] session-qualified --pane must not target other sessions"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    rm -f "$log"
    rm -rf "$TEST_HOME"
}

echo "Running pane targeting tests..."
echo "================================"
test_send_keys_uses_primary_pane
test_find_broadcast_pane_prefers_shell
test_find_broadcast_pane_refuses_agent_only
test_find_broadcast_pane_detects_live_agent_when_map_blank
test_find_broadcast_pane_uses_shell_after_agent_exits
test_find_broadcast_pane_refuses_non_shell
test_send_keys_ignores_stale_snapshot
test_broadcast_session_qualified_pane
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
