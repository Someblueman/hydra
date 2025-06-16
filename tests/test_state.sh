#!/bin/sh
# Unit tests for lib/state.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
. "$(dirname "$0")/../lib/state.sh"
. "$(dirname "$0")/../lib/git.sh"  # Required for validate_mappings
. "$(dirname "$0")/../lib/tmux.sh" # Required for validate_mappings

# Test helper functions
assert_equal() {
    expected="$1"
    actual="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if [ "$expected" = "$actual" ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
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
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: success (exit code 0)"
        echo "  Actual:   failure (exit code $exit_code)"
    fi
}

assert_failure() {
    exit_code="$1"
    message="$2"
    
    test_count=$((test_count + 1))
    if [ "$exit_code" -ne 0 ]; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  Expected: failure (non-zero exit code)"
        echo "  Actual:   success (exit code 0)"
    fi
}

assert_file_contains() {
    file="$1"
    pattern="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if [ -f "$file" ] && grep -q "$pattern" "$file"; then
        pass_count=$((pass_count + 1))
        echo "✓ $message"
    else
        fail_count=$((fail_count + 1))
        echo "✗ $message"
        echo "  File '$file' does not contain '$pattern'"
    fi
}

# Setup test environment
setup_test_state() {
    test_dir="$(mktemp -d)"
    test_map="$test_dir/test_map"
    echo "$test_map"
}

cleanup_test_state() {
    test_dir="$1"
    rm -rf "$test_dir"
}

# Test add_mapping function
test_add_mapping() {
    echo "Testing add_mapping..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Test successful mapping
    add_mapping "feature-branch" "feature-sess"
    assert_success $? "add_mapping should succeed with valid parameters"
    
    assert_file_contains "$HYDRA_MAP" "feature-branch feature-sess" "Mapping should be written to file"
    
    # Test parameter validation
    add_mapping "" "session" 2>/dev/null
    assert_failure $? "add_mapping should fail with empty branch"
    
    add_mapping "branch" "" 2>/dev/null
    assert_failure $? "add_mapping should fail with empty session"
    
    add_mapping "" "" 2>/dev/null
    assert_failure $? "add_mapping should fail with empty branch and session"
    
    # Test replacing existing mapping
    add_mapping "feature-branch" "new-session"
    assert_success $? "add_mapping should succeed when replacing existing mapping"
    
    # Check that old mapping is removed and new one added
    if [ -f "$HYDRA_MAP" ]; then
        count="$(grep -c "feature-branch" "$HYDRA_MAP")"
        assert_equal "1" "$count" "Should only have one mapping per branch"
        assert_file_contains "$HYDRA_MAP" "feature-branch new-session" "Should contain new mapping"
    fi
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Test add_mapping without HYDRA_MAP
test_add_mapping_no_env() {
    echo "Testing add_mapping without HYDRA_MAP..."
    
    # Temporarily unset HYDRA_MAP
    old_map="$HYDRA_MAP"
    unset HYDRA_MAP
    
    add_mapping "branch" "session" 2>/dev/null
    assert_failure $? "add_mapping should fail when HYDRA_MAP is not set"
    
    # Restore HYDRA_MAP
    HYDRA_MAP="$old_map"
    export HYDRA_MAP
}

# Test remove_mapping function
test_remove_mapping() {
    echo "Testing remove_mapping..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Add some test mappings
    echo "branch1 session1" > "$HYDRA_MAP"
    echo "branch2 session2" >> "$HYDRA_MAP"
    echo "branch3 session3" >> "$HYDRA_MAP"
    
    # Test successful removal
    remove_mapping "branch2"
    assert_success $? "remove_mapping should succeed with existing branch"
    
    if [ -f "$HYDRA_MAP" ]; then
        if grep -q "branch2" "$HYDRA_MAP"; then
            echo "✗ Mapping should be removed from file"
            fail_count=$((fail_count + 1))
        else
            echo "✓ Mapping should be removed from file"
            pass_count=$((pass_count + 1))
        fi
        test_count=$((test_count + 1))
        
        # Check other mappings are preserved
        assert_file_contains "$HYDRA_MAP" "branch1 session1" "Other mappings should be preserved"
        assert_file_contains "$HYDRA_MAP" "branch3 session3" "Other mappings should be preserved"
    fi
    
    # Test parameter validation
    remove_mapping "" 2>/dev/null
    assert_failure $? "remove_mapping should fail with empty branch"
    
    # Test removing non-existent branch (should succeed)
    remove_mapping "non-existent-branch"
    assert_success $? "remove_mapping should succeed even for non-existent branch"
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Test get_session_for_branch function
test_get_session_for_branch() {
    echo "Testing get_session_for_branch..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Add some test mappings
    echo "branch1 session1" > "$HYDRA_MAP"
    echo "branch2 session2" >> "$HYDRA_MAP"
    
    # Test successful lookup
    result="$(get_session_for_branch "branch1")"
    assert_equal "session1" "$result" "get_session_for_branch should return correct session"
    
    # Test non-existent branch
    get_session_for_branch "non-existent-branch" >/dev/null 2>&1
    assert_failure $? "get_session_for_branch should fail for non-existent branch"
    
    # Test parameter validation
    get_session_for_branch "" >/dev/null 2>&1
    assert_failure $? "get_session_for_branch should fail with empty branch"
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Test get_branch_for_session function
test_get_branch_for_session() {
    echo "Testing get_branch_for_session..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Add some test mappings
    echo "branch1 session1" > "$HYDRA_MAP"
    echo "branch2 session2" >> "$HYDRA_MAP"
    
    # Test successful lookup
    result="$(get_branch_for_session "session2")"
    assert_equal "branch2" "$result" "get_branch_for_session should return correct branch"
    
    # Test non-existent session
    get_branch_for_session "non-existent-session" >/dev/null 2>&1
    assert_failure $? "get_branch_for_session should fail for non-existent session"
    
    # Test parameter validation
    get_branch_for_session "" >/dev/null 2>&1
    assert_failure $? "get_branch_for_session should fail with empty session"
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Test list_mappings function
test_list_mappings() {
    echo "Testing list_mappings..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Test with empty file
    touch "$HYDRA_MAP"
    output="$(list_mappings)"
    assert_equal "" "$output" "list_mappings should return empty for empty file"
    
    # Test with mappings
    echo "branch1 session1" > "$HYDRA_MAP"
    echo "branch2 session2" >> "$HYDRA_MAP"
    
    output="$(list_mappings)"
    expected="branch1 session1
branch2 session2"
    assert_equal "$expected" "$output" "list_mappings should return all mappings"
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Test generate_session_name function
test_generate_session_name() {
    echo "Testing generate_session_name..."
    
    # Test parameter validation
    generate_session_name "" 2>/dev/null
    assert_failure $? "generate_session_name should fail with empty branch"
    
    # Test basic name generation
    result="$(generate_session_name "feature/test-branch")"
    expected="feature_test-branch"
    assert_equal "$expected" "$result" "generate_session_name should clean special characters"
    
    # Test with simple branch name
    result="$(generate_session_name "simple-branch")"
    assert_equal "simple-branch" "$result" "generate_session_name should preserve valid characters"
}

# Test functions that don't require tmux/git to be available
test_without_external_deps() {
    echo "Testing functions without external dependencies..."
    
    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    export HYDRA_MAP
    
    # Test validate_mappings with non-existent file
    validate_mappings >/dev/null 2>&1
    assert_success $? "validate_mappings should succeed with non-existent file"
    
    # Test cleanup_mappings with non-existent file
    cleanup_mappings >/dev/null 2>&1
    assert_success $? "cleanup_mappings should succeed with non-existent file"
    
    cleanup_test_state "$(dirname "$test_map")"
}

# Run all tests
echo "Running state.sh unit tests..."
echo "================================"

test_add_mapping
test_add_mapping_no_env
test_remove_mapping
test_get_session_for_branch
test_get_branch_for_session
test_list_mappings
test_generate_session_name
test_without_external_deps

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