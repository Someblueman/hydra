#!/bin/sh
# Integration tests for bulk spawn functionality
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Original working directory
original_dir="$(pwd)"
test_dir=""

# Hydra binary path - use current directory's hydra
HYDRA_BIN="${HYDRA_BIN:-$original_dir/bin/hydra}"

# Test helper functions
assert_contains() {
    haystack="$1"
    needle="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if echo "$haystack" | grep -q "$needle"; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected to contain: '$needle'"
        echo "  Actual output: '$haystack'"
    fi
}

# Test setup and teardown
setup_test_env() {
    # Create temporary test directory
    test_dir="$(mktemp -d)" || exit 1
    export HYDRA_HOME="$test_dir/.hydra"
    export HYDRA_MAP="$HYDRA_HOME/map"
    
    # Initialize git repo
    cd "$test_dir" || exit 1
    git init >/dev/null 2>&1
    git config user.email "test@example.com"
    git config user.name "Test User"
    echo "test" > test.txt
    git add test.txt
    git commit -m "Initial commit" >/dev/null 2>&1
}

teardown_test_env() {
    # Kill any test tmux sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | grep '^test-' | while read -r session; do
        tmux kill-session -t "$session" 2>/dev/null || true
    done
    
    # Return to original directory
    cd "$original_dir" || true
    
    # Clean up test directory
    if [ -n "$test_dir" ] && [ -d "$test_dir" ]; then
        rm -rf "$test_dir"
    fi
}

# Test bulk spawn argument parsing
test_bulk_spawn_parsing() {
    echo "Testing bulk spawn argument parsing..."
    
    # Test count validation
    output="$("$HYDRA_BIN" spawn test-feature -n 0 2>&1 || true)"
    assert_contains "$output" "Count must be a number between 1 and 10" "Should reject count of 0"
    
    output="$("$HYDRA_BIN" spawn test-feature -n 11 2>&1 || true)"
    assert_contains "$output" "Count must be a number between 1 and 10" "Should reject count > 10"
    
    output="$("$HYDRA_BIN" spawn test-feature -n abc 2>&1 || true)"
    assert_contains "$output" "Count must be a number between 1 and 10" "Should reject non-numeric count"
    
    # Test mutually exclusive options
    output="$("$HYDRA_BIN" spawn test-feature --ai claude --agents 'claude:2' 2>&1 || true)"
    assert_contains "$output" "Cannot use both --ai and --agents" "Should reject both --ai and --agents"
}

# Test invalid agents specification
test_invalid_agents_spec() {
    echo "Testing invalid agents specification..."
    
    # Test invalid format
    output="$("$HYDRA_BIN" spawn test-invalid --agents 'claude2' 2>&1 || true)"
    assert_contains "$output" "Invalid agent specification" "Should reject missing colon"
    
    output="$("$HYDRA_BIN" spawn test-invalid --agents 'claude:' 2>&1 || true)"
    assert_contains "$output" "Invalid agent specification" "Should reject missing count"
    
    output="$("$HYDRA_BIN" spawn test-invalid --agents ':2' 2>&1 || true)"
    assert_contains "$output" "Invalid agent specification" "Should reject missing agent"
    
    output="$("$HYDRA_BIN" spawn test-invalid --agents 'invalid:2' 2>&1 || true)"
    assert_contains "$output" "Unsupported AI command" "Should reject invalid AI tool"
}

# Test valid AI tools including gemini
test_valid_ai_tools() {
    echo "Testing valid AI tools including gemini..."
    
    # Test gemini is accepted
    output="$("$HYDRA_BIN" spawn test-gemini --agents 'gemini:2' 2>&1 || true)"
    if echo "$output" | grep -q "Unsupported AI command"; then
        fail_count=$((fail_count + 1))
        test_count=$((test_count + 1))
        echo "✗ Should accept 'gemini' as valid AI tool"
        echo "  Output: $output"
    else
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
        echo "✓ Should accept 'gemini' as valid AI tool"
    fi
    
    # Test mixed agents with gemini
    output="$("$HYDRA_BIN" spawn test-mixed --agents 'claude:2,gemini:1' 2>&1 || true)"
    if echo "$output" | grep -q "Unsupported AI command"; then
        fail_count=$((fail_count + 1))
        test_count=$((test_count + 1))
        echo "✗ Should accept mixed agents with gemini"
        echo "  Output: $output"
    else
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
        echo "✓ Should accept mixed agents with gemini"
    fi
}

# Test confirmation prompts
test_bulk_spawn_confirmation() {
    echo "Testing bulk spawn confirmation prompts..."
    setup_test_env
    
    # Test rejection of bulk spawn
    output="$(echo 'n' | "$HYDRA_BIN" spawn test-confirm -n 5 2>&1 || true)"
    assert_contains "$output" "Are you sure you want to spawn 5 sessions?" "Should prompt for confirmation"
    assert_contains "$output" "Aborted" "Should abort on rejection"
    
    teardown_test_env
}

# Run all tests
echo "Running bulk spawn integration tests..."
echo ""

test_bulk_spawn_parsing
test_invalid_agents_spec
test_valid_ai_tools
test_bulk_spawn_confirmation

# Report results
echo ""
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"

# Exit with appropriate code
if [ "$fail_count" -gt 0 ]; then
    exit 1
else
    exit 0
fi