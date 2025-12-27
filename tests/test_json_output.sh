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

# Validate JSON syntax (simple check for balanced braces)
validate_json() {
    json="$1"

    # Check for balanced curly braces
    open_curly="$(printf '%s' "$json" | tr -cd '{' | wc -c | tr -d ' ')"
    close_curly="$(printf '%s' "$json" | tr -cd '}' | wc -c | tr -d ' ')"

    # Check for balanced square brackets
    open_bracket="$(printf '%s' "$json" | tr -cd '[' | wc -c | tr -d ' ')"
    close_bracket="$(printf '%s' "$json" | tr -cd ']' | wc -c | tr -d ' ')"

    if [ "$open_curly" -eq "$close_curly" ] && [ "$open_bracket" -eq "$close_bracket" ]; then
        return 0
    else
        return 1
    fi
}

# =============================================================================
# Unit Tests for json_escape function
# =============================================================================

test_json_escape_plain_text() {
    echo ""
    echo "Testing json_escape with plain text..."

    result="$(json_escape "hello world")"
    assert_equal "hello world" "$result" "Plain text unchanged"
}

test_json_escape_double_quotes() {
    echo ""
    echo "Testing json_escape with double quotes..."

    result="$(json_escape 'hello "world"')"
    assert_equal 'hello \"world\"' "$result" "Double quotes escaped"
}

test_json_escape_backslashes() {
    echo ""
    echo "Testing json_escape with backslashes..."

    result="$(json_escape 'path\to\file')"
    assert_equal 'path\\to\\file' "$result" "Backslashes escaped"
}

test_json_escape_tabs() {
    echo ""
    echo "Testing json_escape with tabs..."

    # Create a string with a tab
    input="$(printf 'hello\tworld')"
    result="$(json_escape "$input")"
    assert_equal 'hello\tworld' "$result" "Tabs escaped"
}

test_json_escape_newlines() {
    echo ""
    echo "Testing json_escape with newlines..."

    # Create a string with a newline
    input="$(printf 'hello\nworld')"
    result="$(json_escape "$input")"

    # Newlines should be converted to spaces
    assert_equal "hello world" "$result" "Newlines converted to spaces"
}

test_json_escape_mixed_special() {
    echo ""
    echo "Testing json_escape with mixed special characters..."

    # String with quotes and backslashes
    result="$(json_escape 'say "hello\\there"')"
    assert_equal 'say \"hello\\\\there\"' "$result" "Mixed quotes and backslashes escaped"
}

# =============================================================================
# Unit Tests for JSON helper functions
# =============================================================================

test_json_kv() {
    echo ""
    echo "Testing json_kv..."

    result="$(json_kv "name" "test-branch")"
    assert_equal '"name": "test-branch"' "$result" "Basic key-value pair"
}

test_json_kv_with_quotes() {
    echo ""
    echo "Testing json_kv with quotes in value..."

    result="$(json_kv "message" 'say "hello"')"
    assert_equal '"message": "say \"hello\""' "$result" "Key-value with escaped quotes"
}

test_json_kv_num() {
    echo ""
    echo "Testing json_kv_num..."

    result="$(json_kv_num "count" 42)"
    assert_equal '"count": 42' "$result" "Numeric key-value pair"
}

test_json_kv_bool() {
    echo ""
    echo "Testing json_kv_bool..."

    result_true="$(json_kv_bool "active" "true")"
    result_false="$(json_kv_bool "active" "false")"

    assert_equal '"active": true' "$result_true" "Boolean true"
    assert_equal '"active": false' "$result_false" "Boolean false"
}

test_json_kv_null() {
    echo ""
    echo "Testing json_kv_null..."

    result="$(json_kv_null "group")"
    assert_equal '"group": null' "$result" "Null value"
}

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
    export HYDRA_HOME
    mkdir -p "$HYDRA_HOME"
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
    HYDRA_MAP="$HYDRA_HOME/map"

    # Create mock session
    session_name="test-json-session-1"
    tmux new-session -d -s "$session_name" 2>/dev/null || true

    # Add to map file with timestamp
    timestamp="$(date +%s)"
    echo "test-branch $session_name claude default $timestamp" >> "$HYDRA_MAP"

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
    test_json_escape_mixed_special

    # Unit tests for JSON helpers
    test_json_kv
    test_json_kv_with_quotes
    test_json_kv_num
    test_json_kv_bool
    test_json_kv_null

    # Integration tests
    echo ""
    echo "=========================================="
    echo "Integration tests"
    echo "=========================================="
    test_list_json_empty
    test_list_json_with_sessions
    test_status_json

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
