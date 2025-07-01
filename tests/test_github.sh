#!/bin/sh
# Test GitHub integration functions
# POSIX-compliant test script

set -eu

# Source the GitHub library
# shellcheck source=../lib/github.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/github.sh"

# Test counter
tests=0
passed=0

# Test validate_issue_number
test_validate_issue_number() {
    echo "Testing validate_issue_number..."
    tests=$((tests + 1))
    
    # Valid numbers
    if validate_issue_number "1" 2>/dev/null; then
        echo "  ✓ Accepts valid number '1'"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to accept valid number '1'"
    fi
    
    tests=$((tests + 1))
    if validate_issue_number "123" 2>/dev/null; then
        echo "  ✓ Accepts valid number '123'"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to accept valid number '123'"
    fi
    
    # Invalid numbers
    tests=$((tests + 1))
    if ! validate_issue_number "" 2>/dev/null; then
        echo "  ✓ Rejects empty string"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject empty string"
    fi
    
    tests=$((tests + 1))
    if ! validate_issue_number "0" 2>/dev/null; then
        echo "  ✓ Rejects zero"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject zero"
    fi
    
    tests=$((tests + 1))
    if ! validate_issue_number "-1" 2>/dev/null; then
        echo "  ✓ Rejects negative number"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject negative number"
    fi
    
    tests=$((tests + 1))
    if ! validate_issue_number "abc" 2>/dev/null; then
        echo "  ✓ Rejects non-numeric string"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject non-numeric string"
    fi
    
    tests=$((tests + 1))
    if ! validate_issue_number "12.3" 2>/dev/null; then
        echo "  ✓ Rejects decimal number"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject decimal number"
    fi
}

# Test sanitize_branch_name
test_sanitize_branch_name() {
    echo "Testing sanitize_branch_name..."
    
    # Test basic sanitization
    tests=$((tests + 1))
    result="$(sanitize_branch_name "Hello World")"
    if [ "$result" = "hello-world" ]; then
        echo "  ✓ Converts spaces to hyphens"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed space conversion: got '$result'"
    fi
    
    # Test special character removal
    tests=$((tests + 1))
    result="$(sanitize_branch_name "Feature: Add @mentions & #hashtags!")"
    if [ "$result" = "feature-add-mentions-hashtags" ]; then
        echo "  ✓ Removes special characters"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed special char removal: got '$result'"
    fi
    
    # Test multiple hyphens
    tests=$((tests + 1))
    result="$(sanitize_branch_name "Fix -- Multiple   Spaces")"
    if [ "$result" = "fix-multiple-spaces" ]; then
        echo "  ✓ Collapses multiple hyphens"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed hyphen collapse: got '$result'"
    fi
    
    # Test leading/trailing hyphens
    tests=$((tests + 1))
    result="$(sanitize_branch_name "  Trim Me  ")"
    if [ "$result" = "trim-me" ]; then
        echo "  ✓ Trims leading/trailing spaces"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed trimming: got '$result'"
    fi
    
    # Test truncation
    tests=$((tests + 1))
    long_text="This is a very long issue title that exceeds the maximum allowed length for branch names"
    result="$(sanitize_branch_name "$long_text")"
    if [ ${#result} -le 50 ]; then
        echo "  ✓ Truncates to max length (${#result} chars)"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to truncate: got ${#result} chars"
    fi
}

# Test generate_branch_from_issue
test_generate_branch_from_issue() {
    echo "Testing generate_branch_from_issue..."
    
    # Test basic generation
    tests=$((tests + 1))
    result="$(generate_branch_from_issue "123" "Fix bug in parser")"
    if [ "$result" = "issue-123-fix-bug-in-parser" ]; then
        echo "  ✓ Generates correct branch name"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed generation: got '$result'"
    fi
    
    # Test with special characters
    tests=$((tests + 1))
    result="$(generate_branch_from_issue "456" "[BUG] Can't handle & symbols")"
    if [ "$result" = "issue-456-bug-cant-handle-symbols" ]; then
        echo "  ✓ Handles special characters in title"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed with special chars: got '$result'"
    fi
    
    # Test empty inputs
    tests=$((tests + 1))
    if ! generate_branch_from_issue "" "Title" 2>/dev/null; then
        echo "  ✓ Rejects empty issue number"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject empty issue number"
    fi
    
    tests=$((tests + 1))
    if ! generate_branch_from_issue "123" "" 2>/dev/null; then
        echo "  ✓ Rejects empty title"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to reject empty title"
    fi
}

# Test parse_json_value
test_parse_json_value() {
    echo "Testing parse_json_value..."
    
    json='{"number":10,"title":"Test Issue","state":"OPEN"}'
    
    # Test parsing different fields
    tests=$((tests + 1))
    result="$(parse_json_value "$json" "number")"
    if [ "$result" = "10" ]; then
        echo "  ✓ Parses number field"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to parse number: got '$result'"
    fi
    
    tests=$((tests + 1))
    result="$(parse_json_value "$json" "title")"
    if [ "$result" = "Test Issue" ]; then
        echo "  ✓ Parses title field"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to parse title: got '$result'"
    fi
    
    tests=$((tests + 1))
    result="$(parse_json_value "$json" "state")"
    if [ "$result" = "OPEN" ]; then
        echo "  ✓ Parses state field"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed to parse state: got '$result'"
    fi
    
    # Test missing field
    tests=$((tests + 1))
    result="$(parse_json_value "$json" "missing")"
    if [ -z "$result" ]; then
        echo "  ✓ Returns empty for missing field"
        passed=$((passed + 1))
    else
        echo "  ✗ Failed missing field test: got '$result'"
    fi
}

# Test check_gh_cli
test_check_gh_cli() {
    echo "Testing check_gh_cli..."
    
    tests=$((tests + 1))
    if command -v gh >/dev/null 2>&1; then
        # If gh is installed, it should pass or fail based on auth
        if check_gh_cli 2>/dev/null; then
            echo "  ✓ GitHub CLI check passed (authenticated)"
            passed=$((passed + 1))
        else
            echo "  ⚠ GitHub CLI found but not authenticated"
            passed=$((passed + 1))  # Still pass the test
        fi
    else
        # If gh is not installed, it should fail
        if ! check_gh_cli 2>/dev/null; then
            echo "  ✓ Correctly reports missing GitHub CLI"
            passed=$((passed + 1))
        else
            echo "  ✗ Failed to detect missing GitHub CLI"
        fi
    fi
}

# Run all tests
echo "Running GitHub integration tests..."
echo "=================================="
echo ""

test_validate_issue_number
echo ""

test_sanitize_branch_name
echo ""

test_generate_branch_from_issue
echo ""

test_parse_json_value
echo ""

test_check_gh_cli
echo ""

# Summary
echo "=================================="
echo "Tests passed: $passed/$tests"

if [ "$passed" -eq "$tests" ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi