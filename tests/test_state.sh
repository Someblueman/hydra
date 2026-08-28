#!/bin/sh
# Unit tests for lib/state.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test and its dependencies
# shellcheck disable=SC2034
HYDRA_LIB_DIR="$(cd "$(dirname "$0")/../lib" && pwd)"
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"  # Required for state.sh lock functions
# shellcheck source=../lib/state.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/state.sh"
# shellcheck source=../lib/git.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/git.sh"  # Required for validate_mappings
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh" # Required for validate_mappings

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# shellcheck disable=SC2317
# shellcheck disable=SC2329
assert_file_contains() {
    file="$1"
    pattern="$2"
    message="$3"
    
    test_count=$((test_count + 1))
    if [ -f "$file" ] && grep -q "$pattern" "$file"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
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
            echo "[FAIL] Mapping should be removed from file"
            fail_count=$((fail_count + 1))
        else
            echo "[PASS] Mapping should be removed from file"
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

# Test lock fail-closed and same-filesystem temps
test_add_mapping_fail_closed() {
    echo "Testing add_mapping fail-closed on lock contention..."

    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    HYDRA_HOME="$(dirname "$test_map")"
    HYDRA_LOCK_RETRIES=1
    export HYDRA_MAP HYDRA_HOME HYDRA_LOCK_RETRIES
    mkdir -p "$HYDRA_HOME/locks"

    echo "keep-branch keep-session claude g 1 - -" > "$HYDRA_MAP"
    try_lock "state_map"
    add_mapping "new-branch" "new-session" 2>/dev/null
    assert_failure $? "add_mapping should fail when state_map lock is held"
    if grep -q "new-branch" "$HYDRA_MAP"; then
        echo "[FAIL] Map must not mutate after lock failure"
        fail_count=$((fail_count + 1))
    else
        echo "[PASS] Map must not mutate after lock failure"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))
    release_lock "state_map"

    unset HYDRA_LOCK_RETRIES
    cleanup_test_state "$(dirname "$test_map")"
}

test_same_filesystem_temp() {
    echo "Testing mktemp_adjacent stays next to dest..."

    test_map="$(setup_test_state)"
    dest_dir="$(dirname "$test_map")"
    tmp="$(mktemp_adjacent "$test_map")"
    assert_equal "$dest_dir" "$(dirname "$tmp")" "temp file is on the same directory as dest"
    rm -f "$tmp"
    cleanup_test_state "$dest_dir"
}

test_get_duration_since_invalid() {
    echo "Testing get_duration_since rejects non-numeric input..."

    result="$(get_duration_since "1700000100 depA,depB 42")"
    assert_equal "0" "$result" "non-numeric timestamp yields 0 not arithmetic error"

    result="$(get_duration_since "-")"
    assert_equal "0" "$result" "placeholder timestamp yields 0"
}

test_state_cache_invalidation() {
    echo "Testing state cache invalidation after write..."

    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    HYDRA_HOME="$(dirname "$test_map")"
    export HYDRA_MAP HYDRA_HOME
    _STATE_CACHE_LOADED=""

    add_mapping "b1" "s1" "claude" "g" "100" "dep" "7"
    result="$(get_session_for_branch "b1")"
    assert_equal "s1" "$result" "cache returns session after add"

    add_mapping "b1" "s2" "claude" "g" "100" "dep" "7"
    result="$(get_session_for_branch "b1")"
    assert_equal "s2" "$result" "cache invalidates and returns new session"

    result="$(get_group_for_branch "b1")"
    assert_equal "g" "$result" "group preserved through rewrite"
    result="$(get_pr_for_branch "b1")"
    assert_equal "7" "$result" "pr preserved through rewrite"

    cleanup_test_state "$(dirname "$test_map")"
}

test_state_cache_distinct_keys() {
    echo "Testing state cache keeps distinct branch identities..."

    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    HYDRA_HOME="$(dirname "$test_map")"
    export HYDRA_MAP HYDRA_HOME
    _invalidate_state_cache

    printf '%s\n' \
        "foo-bar sess-dash - - 1 - -" \
        "foo_bar sess-under - - 2 - -" > "$HYDRA_MAP"

    result="$(get_session_for_branch "foo-bar")"
    assert_equal "sess-dash" "$result" "punctuation-distinct branches do not collide"
    result="$(get_session_for_branch "foo_bar")"
    assert_equal "sess-under" "$result" "underscore branch keeps its own session"

    cleanup_test_state "$(dirname "$test_map")"
}

test_state_cache_removal_invalidation() {
    echo "Testing state cache removes deleted mappings..."

    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    HYDRA_HOME="$(dirname "$test_map")"
    export HYDRA_MAP HYDRA_HOME
    _invalidate_state_cache

    add_mapping "removed-branch" "removed-session"
    _load_state_cache
    remove_mapping "removed-branch"

    get_session_for_branch "removed-branch" >/dev/null 2>&1
    assert_failure $? "removed mapping is absent after cache reload"

    cleanup_test_state "$(dirname "$test_map")"
}

test_regenerate_field_preservation() {
    echo "Testing regenerate-style metadata preservation..."

    test_map="$(setup_test_state)"
    HYDRA_MAP="$test_map"
    HYDRA_HOME="$(dirname "$test_map")"
    export HYDRA_MAP HYDRA_HOME
    _STATE_CACHE_LOADED=""

    add_mapping "feat" "old-sess" "aider" "backend" "123456" "dep1,dep2" "99"
    stored_ai="$(get_ai_for_branch "feat")"
    stored_group="$(get_group_for_branch "feat")"
    stored_ts="$(get_timestamp_for_branch "feat")"
    stored_deps="$(get_deps_for_branch "feat")"
    stored_pr="$(get_pr_for_branch "feat")"
    add_mapping "feat" "new-sess" "$stored_ai" "$stored_group" "$stored_ts" "$stored_deps" "$stored_pr"

    line="$(grep "^feat " "$HYDRA_MAP")"
    expected="feat new-sess aider backend 123456 dep1,dep2 99"
    assert_equal "$expected" "$line" "regenerate rewrite preserves metadata except session"

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
test_add_mapping_fail_closed
test_same_filesystem_temp
test_get_duration_since_invalid
test_state_cache_invalidation
test_state_cache_distinct_keys
test_state_cache_removal_invalidation
test_regenerate_field_preservation

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
