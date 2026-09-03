#!/bin/sh
# Tests for JSON output functionality
# POSIX-compliant test framework
# shellcheck disable=SC1091

set -eu

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Get the absolute path to hydra binary and lib
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$SCRIPT_DIR/bin/hydra"

# Source the output library for testing
. "$SCRIPT_DIR/lib/output.sh"

# Test helper functions
assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"

    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Expected: '$expected'"
        echo "  Actual:   '$actual'"
    fi
}

assert_success() {
    exit_code="$1"
    message="$2"

    test_count=$((test_count + 1))
    if [ "$exit_code" -eq 0 ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code $exit_code)"
    fi
}

assert_contains() {
    text="$1"
    pattern="$2"
    message="$3"

    test_count=$((test_count + 1))
    if echo "$text" | grep -q "$pattern"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Text does not contain: '$pattern'"
        echo "  Actual text: '$text'"
    fi
}

assert_envelope() {
    output="$1"
    command="$2"
    ok="$3"
    message="$4"
    if validate_json "$output" && \
       printf '%s' "$output" | grep -Fq '"schema_version":1' && \
       printf '%s' "$output" | grep -Fq "\"ok\":$ok" && \
       printf '%s' "$output" | grep -Fq "\"command\":\"$command\""; then
        assert_success 0 "$message"
    else
        echo "  Output: $output"
        assert_success 1 "$message"
    fi
}

# Validate JSON syntax (improved checks beyond just balanced braces)
validate_json() {
    json="$1"

    # Empty or whitespace-only is invalid
    case "$json" in
        ''|*[!\ ]*) ;;
        *) return 1 ;;
    esac

    # Must start with { or [ (after trimming whitespace)
    trimmed="$(printf '%s' "$json" | sed 's/^[[:space:]]*//')"
    case "$trimmed" in
        '{'*|'['*) ;;
        *) return 1 ;;
    esac

    # Check for balanced curly braces
    open_curly="$(printf '%s' "$json" | tr -cd '{' | wc -c | tr -d ' ')"
    close_curly="$(printf '%s' "$json" | tr -cd '}' | wc -c | tr -d ' ')"

    # Check for balanced square brackets
    open_bracket="$(printf '%s' "$json" | tr -cd '[' | wc -c | tr -d ' ')"
    close_bracket="$(printf '%s' "$json" | tr -cd ']' | wc -c | tr -d ' ')"

    # Check for balanced double quotes (should be even number)
    quote_count="$(printf '%s' "$json" | tr -cd '"' | wc -c | tr -d ' ')"
    quote_remainder=$((quote_count % 2))

    if [ "$open_curly" -eq "$close_curly" ] && \
       [ "$open_bracket" -eq "$close_bracket" ] && \
       [ "$quote_remainder" -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

# shellcheck disable=SC1091
. "$SCRIPT_DIR/tests/json_output_units.sh"

# =============================================================================
# Integration Tests for hydra --json output
# =============================================================================

# Global test directory (set by each test)
_TEST_DIR=""

setup_test_env() {
    _TEST_DIR="$(mktemp -d)" || {
        echo "Error: Failed to create temporary directory" >&2
        return 1
    }
    HYDRA_HOME="$_TEST_DIR/.hydra"
    HYDRA_NONINTERACTIVE=1
    HYDRA_NO_SWITCH=1
    export HYDRA_HOME HYDRA_NONINTERACTIVE HYDRA_NO_SWITCH
    mkdir -p "$HYDRA_HOME" "$_TEST_DIR/repo"
    git -C "$_TEST_DIR/repo" init -q
    git -C "$_TEST_DIR/repo" config user.name Test
    git -C "$_TEST_DIR/repo" config user.email test@example.com
    git -C "$_TEST_DIR/repo" commit --allow-empty -qm init
    cd "$_TEST_DIR/repo"
    "$HYDRA_BIN" init --no-agent --trust >/dev/null
}

cleanup_test_env() {
    # Kill any test sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            test-json-*)
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
        esac
    done
    if [ -n "$_TEST_DIR" ] && [ -d "$_TEST_DIR" ]; then
        cd "$SCRIPT_DIR"
        rm -rf "$_TEST_DIR"
    fi
    _TEST_DIR=""
}

test_list_json_empty() {
    echo ""
    echo "Testing hydra list --json with no sessions..."

    setup_test_env

    output="$("$HYDRA_BIN" list --json 2>&1)" || true

    # Should output empty array or handle gracefully
    if validate_json "$output" || echo "$output" | grep -qi "no.*heads\|no.*sessions\|\[\]"; then
        echo "[PASS] Empty list --json handles gracefully"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Empty list --json should produce valid output"
        echo "  Output: $output"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env
}

test_list_json_with_sessions() {
    echo ""
    echo "Testing hydra list --json with mock sessions..."

    setup_test_env
    "$HYDRA_BIN" spawn test-json-session-1 --no-agent >/dev/null

    output="$("$HYDRA_BIN" list --json 2>&1)" || true

    # Verify output is valid JSON structure
    if validate_json "$output"; then
        echo "[PASS] list --json produces balanced JSON"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] list --json should produce valid JSON"
        echo "  Output: $output"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    # Verify it contains expected fields
    assert_contains "$output" "branch" "JSON contains 'branch' field"
    assert_contains "$output" "session" "JSON contains 'session' field"

    cleanup_test_env
}

test_status_json() {
    echo ""
    echo "Testing hydra status --json..."

    setup_test_env

    output="$("$HYDRA_BIN" status --json 2>&1)" || true

    # Verify output is valid JSON structure
    if validate_json "$output"; then
        echo "[PASS] status --json produces balanced JSON"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] status --json should produce valid JSON"
        echo "  Output: $output"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    cleanup_test_env
}

test_status_json_with_dead_head() {
    echo ""
    echo "Testing hydra status --json with a dead durable head..."

    setup_test_env
    "$HYDRA_BIN" spawn stat-branch --no-agent >/dev/null
    tmux kill-session -t stat-branch 2>/dev/null || true

    output="$(env HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" status --json 2>&1)" || true

    if echo "$output" | grep -q "Illegal number"; then
        echo "[FAIL] status must not print Illegal number"
        fail_count=$((fail_count + 1))
    else
        echo "[PASS] status must not print Illegal number"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))

    if validate_json "$output"; then
        echo "[PASS] status --json with a dead head is valid JSON"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] status --json with a dead head is valid JSON"
        echo "  Output: $output"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    assert_contains "$output" "duration_seconds" "JSON contains duration_seconds"

    cleanup_test_env
}

test_versioned_command_envelopes() {
    echo ""
    echo "Testing versioned command envelopes..."

    setup_test_env

    output="$("$HYDRA_BIN" list --json)"
    assert_envelope "$output" list true "list success uses JSON v1"

    output="$("$HYDRA_BIN" status --json)"
    assert_envelope "$output" status true "status success uses JSON v1"

    output="$("$HYDRA_BIN" queue --json)"
    assert_envelope "$output" queue true "queue success uses JSON v1"

    output="$("$HYDRA_BIN" group status empty --json)"
    assert_envelope "$output" "group status" true "group status success uses JSON v1"

    output="$("$HYDRA_BIN" list --bad --json 2>/dev/null)" || list_code=$?
    list_code="${list_code:-0}"
    if [ "$list_code" -ne 0 ]; then assert_success 0 "JSON error exits nonzero"; else assert_success 1 "JSON error exits nonzero"; fi
    assert_envelope "$output" list false "list validation failure uses JSON v1"

    output="$(TMUX='' "$HYDRA_BIN" recv --json 2>/dev/null)" || recv_code=$?
    recv_code="${recv_code:-0}"
    if [ "$recv_code" -ne 0 ]; then assert_success 0 "recv JSON error exits nonzero"; else assert_success 1 "recv JSON error exits nonzero"; fi
    assert_envelope "$output" recv false "recv failure uses JSON v1"

    output="$("$HYDRA_BIN" queue remove absent --json 2>/dev/null)" || queue_code=$?
    queue_code="${queue_code:-0}"
    if [ "$queue_code" -ne 0 ]; then assert_success 0 "queue JSON error exits nonzero"; else assert_success 1 "queue JSON error exits nonzero"; fi
    assert_envelope "$output" queue false "queue failure uses JSON v1"

    output="$("$HYDRA_BIN" lifecycle absent --json 2>/dev/null)" || lifecycle_code=$?
    lifecycle_code="${lifecycle_code:-0}"
    if [ "$lifecycle_code" -ne 0 ]; then assert_success 0 "lifecycle JSON error exits nonzero"; else assert_success 1 "lifecycle JSON error exits nonzero"; fi
    assert_envelope "$output" lifecycle false "lifecycle failure uses JSON v1"

    output="$("$HYDRA_BIN" wait absent --json 2>/dev/null)" || wait_code=$?
    wait_code="${wait_code:-0}"
    if [ "$wait_code" -ne 0 ]; then assert_success 0 "wait JSON error exits nonzero"; else assert_success 1 "wait JSON error exits nonzero"; fi
    assert_envelope "$output" wait false "wait failure uses JSON v1"

    output="$("$HYDRA_BIN" exec --json --jobs invalid -- true 2>/dev/null)" || exec_code=$?
    exec_code="${exec_code:-0}"
    if [ "$exec_code" -ne 0 ]; then assert_success 0 "exec JSON error exits nonzero"; else assert_success 1 "exec JSON error exits nonzero"; fi
    assert_envelope "$output" exec false "exec validation failure uses JSON v1"

    mkdir -p "$_TEST_DIR/not-a-repo"
    output="$(cd "$_TEST_DIR/not-a-repo" && "$HYDRA_BIN" queue --json 2>/dev/null)" || queue_project_code=$?
    queue_project_code="${queue_project_code:-0}"
    if [ "$queue_project_code" -ne 0 ]; then assert_success 0 "projectless queue JSON exits nonzero"; else assert_success 1 "projectless queue JSON exits nonzero"; fi
    assert_envelope "$output" queue false "projectless queue failure uses JSON v1"

    output="$(cd "$_TEST_DIR/not-a-repo" && "$HYDRA_BIN" init --json 2>/dev/null)" || init_code=$?
    init_code="${init_code:-0}"
    if [ "$init_code" -ne 0 ]; then assert_success 0 "init JSON error exits nonzero"; else assert_success 1 "init JSON error exits nonzero"; fi
    assert_envelope "$output" init false "init failure uses JSON v1"

    cleanup_test_env
}

# =============================================================================
# Main test runner
# =============================================================================

main() {
    echo "=========================================="
    echo "Running JSON output tests"
    echo "=========================================="

    # Unit tests for json_escape
    test_json_escape_plain_text
    test_json_escape_double_quotes
    test_json_escape_backslashes
    test_json_escape_tabs
    test_json_escape_newlines
    test_json_escape_cr_ff_bs
    test_json_escape_other_c0
    test_json_escape_mixed_special
    test_json_escape_utf8

    # Unit tests for JSON helpers
    test_json_kv
    test_json_kv_with_quotes
    test_json_kv_num
    test_json_kv_bool
    test_json_kv_null
    test_json_envelopes

    # Integration tests
    echo ""
    echo "=========================================="
    echo "Integration tests"
    echo "=========================================="
    test_list_json_empty
    test_list_json_with_sessions
    test_status_json
test_status_json_with_dead_head
    test_versioned_command_envelopes

    # Report results
    echo ""
    echo "=========================================="
    echo "Test Results: $pass_count/$test_count passed"
    echo "=========================================="

    if [ "$fail_count" -gt 0 ]; then
        echo "$fail_count test(s) failed"
        return 1
    fi

    return 0
}

main "$@"
