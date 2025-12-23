#!/bin/sh
# Unit tests for lib/completion.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/completion.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/completion.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Test generate_completion dispatcher with bash
test_generate_completion_bash() {
    echo "Testing generate_completion bash..."

    output=$(generate_completion bash)
    assert_success $? "generate_completion bash should succeed"

    # Check for bash completion header
    case "$output" in
        *"Bash completion for hydra"*)
            echo "[PASS] Bash output contains expected header"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Bash output should contain 'Bash completion for hydra'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    # Check for function definition
    case "$output" in
        *"_hydra_completion"*)
            echo "[PASS] Bash output contains completion function"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Bash output should contain '_hydra_completion'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test generate_completion dispatcher with zsh
test_generate_completion_zsh() {
    echo "Testing generate_completion zsh..."

    output=$(generate_completion zsh)
    assert_success $? "generate_completion zsh should succeed"

    # Check for zsh completion header
    case "$output" in
        *"#compdef hydra"*)
            echo "[PASS] Zsh output contains compdef header"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Zsh output should contain '#compdef hydra'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    # Check for function definition
    case "$output" in
        *"_hydra()"*)
            echo "[PASS] Zsh output contains _hydra function"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Zsh output should contain '_hydra()'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test generate_completion dispatcher with fish
test_generate_completion_fish() {
    echo "Testing generate_completion fish..."

    output=$(generate_completion fish)
    assert_success $? "generate_completion fish should succeed"

    # Check for fish completion header
    case "$output" in
        *"Fish completion for hydra"*)
            echo "[PASS] Fish output contains expected header"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Fish output should contain 'Fish completion for hydra'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    # Check for complete command
    case "$output" in
        *"complete -c hydra"*)
            echo "[PASS] Fish output contains complete command"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Fish output should contain 'complete -c hydra'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test generate_completion with default (no argument)
test_generate_completion_default() {
    echo "Testing generate_completion with no argument (default to bash)..."

    output=$(generate_completion)
    assert_success $? "generate_completion with no args should succeed"

    # Should default to bash
    case "$output" in
        *"Bash completion for hydra"*)
            echo "[PASS] Default output is bash completion"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Default should be bash completion"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test generate_completion with unknown shell
test_generate_completion_unknown() {
    echo "Testing generate_completion with unknown shell..."

    output=$(generate_completion unknown 2>&1)
    exit_code=$?
    assert_failure $exit_code "generate_completion unknown should fail"

    # Check for error message
    case "$output" in
        *"Unknown shell"*)
            echo "[PASS] Error message mentions unknown shell"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Error message should mention 'Unknown shell'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))

    # Check for supported shells list
    case "$output" in
        *"bash, zsh, fish"*)
            echo "[PASS] Error message lists supported shells"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Error message should list 'bash, zsh, fish'"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Test generate_completion with empty string
test_generate_completion_empty() {
    echo "Testing generate_completion with empty string..."

    output=$(generate_completion "")
    assert_success $? "generate_completion with empty string should succeed (default to bash)"

    # Should treat empty as default (bash)
    case "$output" in
        *"Bash completion for hydra"*)
            echo "[PASS] Empty string defaults to bash"
            pass_count=$((pass_count + 1))
            ;;
        *)
            echo "[FAIL] Empty string should default to bash"
            fail_count=$((fail_count + 1))
            ;;
    esac
    test_count=$((test_count + 1))
}

# Run all tests
echo "=== Completion Tests ==="
echo ""

test_generate_completion_bash
echo ""
test_generate_completion_zsh
echo ""
test_generate_completion_fish
echo ""
test_generate_completion_default
echo ""
test_generate_completion_unknown
echo ""
test_generate_completion_empty
echo ""

# Print summary
echo "=== Test Summary ==="
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

# Exit with failure if any tests failed
if [ "$fail_count" -gt 0 ]; then
    exit 1
fi
exit 0
