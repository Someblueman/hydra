#!/bin/sh
# JSON helper unit cases sourced by test_json_output.sh

# =============================================================================
# Unit Tests for json_escape function
# =============================================================================

test_json_escape_plain_text() {
    echo ""
    echo "Testing json_escape with plain text..."

    result="$(json_escape "hello world")"
    assert_equal "hello world" "$result" "Plain text unchanged"
}

test_json_envelopes() {
    echo ""
    echo "Testing versioned JSON envelopes..."

    success="$(json_success fixture '{}')"
    assert_equal '{"schema_version":1,"ok":true,"command":"fixture","data":{}}' "$success" "Success envelope is stable JSON v1"
    validate_json "$success"
    assert_success $? "Success envelope validates"

    failure="$(json_error fixture invalid_input 'bad input' 'retry safely')"
    assert_equal '{"schema_version":1,"ok":false,"command":"fixture","error":{"code":"invalid_input","message":"bad input","recovery":"retry safely"}}' "$failure" "Error envelope is stable JSON v1"
    validate_json "$failure"
    assert_success $? "Error envelope validates"
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

    # Newlines should be escaped as \n
    assert_equal 'hello\nworld' "$result" "Newlines escaped as \\n"
}

test_json_escape_cr_ff_bs() {
    echo ""
    echo "Testing json_escape with CR, FF, and backspace..."

    input="$(printf 'a\rb')"
    result="$(json_escape "$input")"
    assert_equal 'a\rb' "$result" "CR escaped as \\r"

    input="$(printf 'a\fb')"
    result="$(json_escape "$input")"
    assert_equal 'a\fb' "$result" "FF escaped as \\f"

    input="$(printf 'a\bb')"
    result="$(json_escape "$input")"
    assert_equal 'a\bb' "$result" "BS escaped as \\b"
}

test_json_escape_other_c0() {
    echo ""
    echo "Testing json_escape with other C0 bytes..."

    input="$(printf 'a\001b')"
    result="$(json_escape "$input")"
    assert_equal 'a\u0001b' "$result" "SOH escaped as \\u0001"

    input="$(printf 'a\037b')"
    result="$(json_escape "$input")"
    assert_equal 'a\u001fb' "$result" "US escaped as \\u001f"
}

test_json_escape_mixed_special() {
    echo ""
    echo "Testing json_escape with mixed special characters..."

    # String with quotes and backslashes
    result="$(json_escape 'say "hello\\there"')"
    assert_equal 'say \"hello\\\\there\"' "$result" "Mixed quotes and backslashes escaped"
}

test_json_escape_utf8() {
    echo ""
    echo "Testing json_escape preserves UTF-8 bytes..."

    # café: c3 a9 for é. Must round-trip even in a UTF-8 locale.
    input="$(printf 'caf\303\251')"
    result="$(LC_ALL=en_US.UTF-8 json_escape "$input")"
    assert_equal "$input" "$result" "UTF-8 payload bytes are not recoded"

    quoted="$(printf '"caf\303\251"')"
    result="$(LC_ALL=en_US.UTF-8 json_escape "$quoted")"
    expected="$(printf '\\"caf\303\251\\"')"
    assert_equal "$expected" "$result" "UTF-8 with quotes still escapes quotes only"
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

