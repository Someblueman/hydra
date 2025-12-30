#!/bin/sh
# Unit tests for lib/template.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/template.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/template.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Setup test environment
setup_test_env() {
    test_dir="$(mktemp -d)"
    export HYDRA_HOME="$test_dir"
    export HYDRA_TEMPLATES_DIR="$test_dir/templates"
    export HYDRA_NONINTERACTIVE=1
}

# Cleanup test environment
cleanup_test_env() {
    rm -rf "$test_dir"
}

# =============================================================================
# Test: init_templates_dir
# =============================================================================
test_init_templates_dir() {
    echo "Testing init_templates_dir..."
    setup_test_env

    # Test directory creation
    init_templates_dir
    if [ -d "$HYDRA_TEMPLATES_DIR" ]; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] init_templates_dir creates directory"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] init_templates_dir should create directory"
    fi

    # Test idempotent (can call again)
    init_templates_dir
    assert_success $? "init_templates_dir is idempotent"

    cleanup_test_env
}

# =============================================================================
# Test: validate_template_name
# =============================================================================
test_validate_template_name() {
    echo ""
    echo "Testing validate_template_name..."
    setup_test_env

    # Valid names
    validate_template_name "my-template" >/dev/null 2>&1
    assert_success $? "validate_template_name accepts alphanumeric with dash"

    validate_template_name "my_template" >/dev/null 2>&1
    assert_success $? "validate_template_name accepts alphanumeric with underscore"

    validate_template_name "Template123" >/dev/null 2>&1
    assert_success $? "validate_template_name accepts mixed case and numbers"

    # Invalid names
    validate_template_name "" >/dev/null 2>&1
    assert_failure $? "validate_template_name rejects empty name"

    validate_template_name "my template" >/dev/null 2>&1
    assert_failure $? "validate_template_name rejects spaces"

    validate_template_name "my.template" >/dev/null 2>&1
    assert_failure $? "validate_template_name rejects dots"

    validate_template_name "my/template" >/dev/null 2>&1
    assert_failure $? "validate_template_name rejects slashes"

    validate_template_name "../etc/passwd" >/dev/null 2>&1
    assert_failure $? "validate_template_name rejects path traversal"

    cleanup_test_env
}

# =============================================================================
# Test: create_template
# =============================================================================
test_create_template() {
    echo ""
    echo "Testing create_template..."
    setup_test_env

    # Create minimal template
    create_template "test-tpl" >/dev/null 2>&1
    assert_success $? "create_template succeeds"

    if [ -f "$HYDRA_TEMPLATES_DIR/test-tpl.yml" ]; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] create_template creates .yml file"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] create_template should create .yml file"
    fi

    # Check template has expected content
    if grep -q "layout: default" "$HYDRA_TEMPLATES_DIR/test-tpl.yml"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] create_template includes default layout"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] create_template should include default layout"
    fi

    # Create from source file
    source_file="$test_dir/source.yml"
    printf 'layout: dev\nai_tool: aider\n' > "$source_file"
    create_template "from-source" "$source_file" >/dev/null 2>&1
    assert_success $? "create_template from source succeeds"

    if grep -q "layout: dev" "$HYDRA_TEMPLATES_DIR/from-source.yml"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] create_template copies source content"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] create_template should copy source content"
    fi

    # Invalid name should fail
    create_template "" >/dev/null 2>&1
    assert_failure $? "create_template rejects empty name"

    cleanup_test_env
}

# =============================================================================
# Test: list_templates
# =============================================================================
test_list_templates() {
    echo ""
    echo "Testing list_templates..."
    setup_test_env

    # Empty directory
    init_templates_dir
    result="$(list_templates)"
    assert_equal "" "$result" "list_templates returns empty for no templates"

    # Create some templates
    printf 'layout: default\n' > "$HYDRA_TEMPLATES_DIR/alpha.yml"
    printf 'layout: dev\n' > "$HYDRA_TEMPLATES_DIR/beta.yaml"
    printf 'layout: full\n' > "$HYDRA_TEMPLATES_DIR/gamma.yml"

    result="$(list_templates)"
    if echo "$result" | grep -q "alpha"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] list_templates includes alpha.yml"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] list_templates should include alpha.yml"
    fi

    if echo "$result" | grep -q "beta"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] list_templates includes beta.yaml"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] list_templates should include beta.yaml"
    fi

    # Should strip extension
    if echo "$result" | grep -q "\.yml"; then
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] list_templates should strip .yml extension"
    else
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] list_templates strips extensions"
    fi

    cleanup_test_env
}

# =============================================================================
# Test: get_template_path and template_exists
# =============================================================================
test_get_template_path() {
    echo ""
    echo "Testing get_template_path and template_exists..."
    setup_test_env
    init_templates_dir

    # Create test template
    printf 'layout: default\n' > "$HYDRA_TEMPLATES_DIR/mytest.yml"

    # Test get_template_path
    result="$(get_template_path "mytest")"
    assert_equal "$HYDRA_TEMPLATES_DIR/mytest.yml" "$result" "get_template_path returns correct path"

    get_template_path "mytest" >/dev/null 2>&1
    assert_success $? "get_template_path succeeds for existing template"

    get_template_path "nonexistent" >/dev/null 2>&1
    assert_failure $? "get_template_path fails for missing template"

    # Test template_exists
    template_exists "mytest"
    assert_success $? "template_exists returns true for existing"

    template_exists "nonexistent"
    assert_failure $? "template_exists returns false for missing"

    # Test .yaml extension support
    printf 'layout: dev\n' > "$HYDRA_TEMPLATES_DIR/yamltest.yaml"
    result="$(get_template_path "yamltest")"
    assert_equal "$HYDRA_TEMPLATES_DIR/yamltest.yaml" "$result" "get_template_path finds .yaml files"

    cleanup_test_env
}

# =============================================================================
# Test: show_template
# =============================================================================
test_show_template() {
    echo ""
    echo "Testing show_template..."
    setup_test_env
    init_templates_dir

    # Create test template
    printf 'layout: dev\nai_tool: claude\n' > "$HYDRA_TEMPLATES_DIR/showtest.yml"

    # Test show_template
    result="$(show_template "showtest")"
    if echo "$result" | grep -q "layout: dev"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] show_template displays content"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] show_template should display content"
    fi

    # Non-existent template
    show_template "nonexistent" >/dev/null 2>&1
    assert_failure $? "show_template fails for missing template"

    cleanup_test_env
}

# =============================================================================
# Test: delete_template
# =============================================================================
test_delete_template() {
    echo ""
    echo "Testing delete_template..."
    setup_test_env
    init_templates_dir

    # Create and delete template
    printf 'layout: default\n' > "$HYDRA_TEMPLATES_DIR/todelete.yml"
    delete_template "todelete" --force >/dev/null 2>&1
    assert_success $? "delete_template succeeds"

    if [ ! -f "$HYDRA_TEMPLATES_DIR/todelete.yml" ]; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] delete_template removes file"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] delete_template should remove file"
    fi

    # Delete non-existent
    delete_template "nonexistent" --force >/dev/null 2>&1
    assert_failure $? "delete_template fails for missing template"

    cleanup_test_env
}

# =============================================================================
# Test: get_template_field
# =============================================================================
test_get_template_field() {
    echo ""
    echo "Testing get_template_field..."
    setup_test_env
    init_templates_dir

    # Create test template with various fields
    cat > "$HYDRA_TEMPLATES_DIR/fields.yml" << 'EOF'
# Comment line
layout: dev
ai_tool: claude
description: "My test template"
nested:
  - item1
  - item2
EOF

    result="$(get_template_field "$HYDRA_TEMPLATES_DIR/fields.yml" "layout")"
    assert_equal "dev" "$result" "get_template_field extracts layout"

    result="$(get_template_field "$HYDRA_TEMPLATES_DIR/fields.yml" "ai_tool")"
    assert_equal "claude" "$result" "get_template_field extracts ai_tool"

    result="$(get_template_field "$HYDRA_TEMPLATES_DIR/fields.yml" "description")"
    assert_equal "My test template" "$result" "get_template_field extracts quoted value"

    result="$(get_template_field "$HYDRA_TEMPLATES_DIR/fields.yml" "nonexistent")"
    assert_equal "" "$result" "get_template_field returns empty for missing field"

    cleanup_test_env
}

# =============================================================================
# Test: expand_template_vars
# =============================================================================
test_expand_template_vars() {
    echo ""
    echo "Testing expand_template_vars..."
    setup_test_env
    init_templates_dir

    # Create template with variables
    cat > "$HYDRA_TEMPLATES_DIR/vars.yml" << 'EOF'
description: "Template for ${BRANCH}"
worktree: "${WORKTREE}"
session: "${SESSION}"
repo: "${REPO_ROOT}"
home: "${HYDRA_HOME}"
EOF

    result="$(expand_template_vars "$HYDRA_TEMPLATES_DIR/vars.yml" "my-branch" "my-session" "/path/to/wt" "/path/to/repo")"

    if echo "$result" | grep -q "Template for my-branch"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] expand_template_vars expands BRANCH"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] expand_template_vars should expand BRANCH"
    fi

    if echo "$result" | grep -q "worktree: \"/path/to/wt\""; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] expand_template_vars expands WORKTREE"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] expand_template_vars should expand WORKTREE"
    fi

    if echo "$result" | grep -q "session: \"my-session\""; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] expand_template_vars expands SESSION"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] expand_template_vars should expand SESSION"
    fi

    cleanup_test_env
}

# =============================================================================
# Test: apply_template
# =============================================================================
test_apply_template() {
    echo ""
    echo "Testing apply_template..."
    setup_test_env
    init_templates_dir

    # Create template
    cat > "$HYDRA_TEMPLATES_DIR/apply.yml" << 'EOF'
layout: dev
ai_tool: claude
branch_var: "${BRANCH}"
EOF

    # Apply template
    merged_file="$(apply_template "apply" "/tmp/wt" "/tmp/repo" "test-branch" "test-session")"
    if [ -f "$merged_file" ]; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] apply_template creates merged file"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] apply_template should create merged file"
    fi

    # Check variables are expanded
    if grep -q "branch_var: \"test-branch\"" "$merged_file"; then
        test_count=$((test_count + 1))
        pass_count=$((pass_count + 1))
        echo "[PASS] apply_template expands variables in merged file"
    else
        test_count=$((test_count + 1))
        fail_count=$((fail_count + 1))
        echo "[FAIL] apply_template should expand variables"
    fi

    # Cleanup merged file
    rm -f "$merged_file"

    # Non-existent template
    apply_template "nonexistent" "/tmp/wt" "/tmp/repo" "branch" "session" >/dev/null 2>&1
    assert_failure $? "apply_template fails for missing template"

    cleanup_test_env
}

# =============================================================================
# Run all tests
# =============================================================================
echo "Running template.sh unit tests..."
echo "================================"
echo ""

test_init_templates_dir
test_validate_template_name
test_create_template
test_list_templates
test_get_template_path
test_show_template
test_delete_template
test_get_template_field
test_expand_template_vars
test_apply_template

echo ""
echo "================================"
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"
echo ""

if [ "$fail_count" -gt 0 ]; then
    echo "Some tests failed!"
    exit 1
else
    echo "All tests passed!"
    exit 0
fi
