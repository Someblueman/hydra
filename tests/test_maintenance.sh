#!/bin/sh
# Unit tests for lib/maintenance.sh
# POSIX-compliant test framework

test_count=0
pass_count=0
fail_count=0
HYDRA_HOME="${TMPDIR:-/tmp}/hydra-maintenance-unused"

HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/output.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/identity.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state_v2.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/tmux.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/messages.sh"
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/maintenance.sh"
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Stub tmux for maintenance tests
# shellcheck disable=SC2317
# shellcheck disable=SC2329
tmux_session_exists() {
    case "$1" in
        alive-session) return 0 ;;
        *) return 1 ;;
    esac
}

setup_env() {
    TEST_DIR="$(mktemp -d)"
    HYDRA_HOME="$TEST_DIR"
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    TEST_PROJECT_ID=project_0123456789abcdefabcd
    export HYDRA_HOME HYDRA_STATE_V2_ROOT TEST_PROJECT_ID
    mkdir -p "$HYDRA_HOME/locks"
    state_v2_init_project "$TEST_PROJECT_ID" "$TEST_DIR/repo"
}

# shellcheck disable=SC2317,SC2329
hydra_get_project_id() { printf '%s\n' "$TEST_PROJECT_ID"; }

add_test_head() {
    state_v2_create_head "$TEST_PROJECT_ID" "$1" "$2" - - 100 - - "$TEST_DIR/repo" >/dev/null
}

cleanup_env() {
    rm -rf "$TEST_DIR"
}

test_stale_locks_require_owner_evidence() {
    echo "Testing stale locks require dead same-host owner evidence..."
    setup_env

    mkdir -p "$HYDRA_HOME/locks/fresh.lock"
    mkdir -p "$HYDRA_HOME/locks/old.lock"
    mkdir -p "$HYDRA_HOME/locks/dead.lock"
    touch -t 202001010000 "$HYDRA_HOME/locks/old.lock"
    printf '99999999\n' > "$HYDRA_HOME/locks/dead.lock/pid"
    hostname > "$HYDRA_HOME/locks/dead.lock/host"

    result="$(count_stale_locks)"
    assert_equal "1" "$result" "only dead same-host lock is stale"

    cleaned="$(clean_stale_locks)"
    assert_equal "1" "$cleaned" "clean_stale_locks removes only evidenced stale lock"

    if [ -d "$HYDRA_HOME/locks/fresh.lock" ]; then
        echo "[PASS] Fresh lock directory preserved"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Fresh lock directory preserved"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ -d "$HYDRA_HOME/locks/old.lock" ]; then
        echo "[PASS] Aged metadata-free lock preserved"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Aged metadata-free lock preserved"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if [ ! -d "$HYDRA_HOME/locks/dead.lock" ]; then
        echo "[PASS] Dead same-host lock removed"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Dead same-host lock removed"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_env
}

test_clean_dead_heads_cleans_messages() {
    echo "Testing clean_dead_heads removes message dirs..."
    setup_env

    add_test_head dead-branch dead-session
    add_test_head live-branch alive-session
    dead_messages="$(get_message_dir dead-branch)"
    ensure_message_dir dead-branch
    ensure_message_dir live-branch
    echo "msg" > "$dead_messages/queue/1"

    result="$(clean_dead_heads)"
    assert_equal "1" "$result" "One dead head stopped"

    if [ ! -f "$dead_messages/queue/1" ] && [ -f "$dead_messages/archive/1" ]; then
        echo "[PASS] Dead-head messages archived"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Dead-head messages archived"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    if state_list_heads | grep -q '^live-branch '; then
        echo "[PASS] Live head preserved"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Live head preserved"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_env
}

test_branch_has_active_head_matches_literal_branch() {
    echo "Testing branch_has_active_head uses literal branch identity..."
    setup_env

    add_test_head feature/x alive-session

    branch_has_active_head "feature/x"
    assert_success $? "Exact branch name is found"
    branch_has_active_head "feature.x"
    assert_failure $? "Regex-like branch name does not match a different branch"

    cleanup_env
}

echo "Running maintenance.sh unit tests..."
echo "================================"
test_stale_locks_require_owner_evidence
test_clean_dead_heads_cleans_messages
test_branch_has_active_head_matches_literal_branch
echo "================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
