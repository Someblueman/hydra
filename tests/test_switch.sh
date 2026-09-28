#!/bin/sh
# shellcheck disable=SC1091
. "$(CDPATH='' cd -- "$(dirname "$0")" && pwd)/helpers.sh"
# Tests for cmd_switch input validation
# POSIX-compliant test framework

set -eu

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Get the absolute path to hydra binary
HYDRA_BIN="$(cd "$(dirname "$0")/.." && pwd)/bin/hydra"

# Test helper functions



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

# Setup test environment
TEST_DIR=""
setup_test_env() {
    TEST_DIR="$(test_mktemp_dir)" || {
        echo "Error: Failed to create temporary directory" >&2
        exit 1
    }
    HYDRA_HOME="$TEST_DIR/.hydra"
    export HYDRA_HOME HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
    mkdir -p "$HYDRA_HOME" "$TEST_DIR/repo" || exit 1
    # Never fall through to spawning in the source checkout.
    cd "$TEST_DIR/repo" || exit 1
    git init -q
    git config user.name Test
    git config user.email test@example.com
    git commit --allow-empty -qm init
    "$HYDRA_BIN" init --no-agent --trust >/dev/null
}

cleanup_test_env() {
    test_dir="$1"
    # Kill any test sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            test-switch-*)
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
        esac
    done
    cd "$(dirname "$HYDRA_BIN")"
    rm -rf "$test_dir"
    unset HYDRA_HOME
    TEST_DIR=""
}

# Create mock sessions for testing
setup_mock_sessions() {
    # Create 3 test sessions
    for i in 1 2 3; do
        "$HYDRA_BIN" spawn "test-switch-$i" --no-agent >/dev/null
    done
}

# The numbered menu (used when fzf is unavailable) must reject every invalid
# choice through the real CLI. TMUX names this case's private server, so no
# pane or fixed delay is needed to be "inside tmux".
test_switch_menu_rejects_invalid_choices() {
    echo ""
    echo "Testing the numbered switch menu rejects invalid choices..."

    setup_test_env
    test_dir="$TEST_DIR"
    setup_mock_sessions
    socket="$(tmux list-sessions -F '#{socket_path}' | sed -n '1p')"

    # fzf would replace the numbered menu: drop PATH entries that provide it and
    # link their other tools into a private directory instead.
    no_fzf="$test_dir/no-fzf-bin"
    mkdir -p "$no_fzf"
    menu_path=""
    saved_ifs=$IFS
    IFS=:
    for dir in $PATH; do
        if [ -x "$dir/fzf" ]; then
            for tool in "$dir"/*; do
                name=${tool##*/}
                [ "$name" = fzf ] || [ -e "$no_fzf/$name" ] || ln -s "$tool" "$no_fzf/$name"
            done
        else
            menu_path="${menu_path:+$menu_path:}$dir"
        fi
    done
    IFS=$saved_ifs

    for choice_case in 'abc:must be a number' ':must be a number' '-1:must be a number' \
        '0:must be between 1 and 3' '999:must be between 1 and 3'; do
        choice=${choice_case%%:*}
        expected=${choice_case#*:}
        code=0
        output="$(printf '%s\n' "$choice" | TMUX="$socket,$$,0" PATH="$no_fzf:$menu_path" \
            "$HYDRA_BIN" switch 2>&1)" || code=$?
        assert_failure "$code" "switch menu rejects choice '$choice'"
        assert_contains "$output" "$expected" "switch menu explains why '$choice' is rejected"
    done

    cleanup_test_env "$test_dir"
}

# Test: Input validation helper function directly
test_validate_choice_helper() {
    echo ""
    echo "Testing choice validation logic directly..."

    # Test case function that mimics the validation logic
    validate_choice() {
        choice="$1"
        session_count="$2"

        # Check for empty or non-numeric
        case "$choice" in
            ''|*[!0-9]*)
                echo "Invalid selection: must be a number"
                return 1
                ;;
        esac

        # Check range
        if [ "$choice" -lt 1 ] || [ "$choice" -gt "$session_count" ]; then
            echo "Invalid selection: must be between 1 and $session_count"
            return 1
        fi

        return 0
    }

    # Test: empty input
    output="$(validate_choice "" 3 2>&1)" || true
    assert_contains "$output" "must be a number" "Empty input validation"

    # Test: non-numeric input
    output="$(validate_choice "abc" 3 2>&1)" || true
    assert_contains "$output" "must be a number" "Non-numeric validation"

    # Test: zero
    output="$(validate_choice "0" 3 2>&1)" || true
    assert_contains "$output" "between 1 and 3" "Zero validation"

    # Test: negative (caught by non-numeric check)
    output="$(validate_choice "-1" 3 2>&1)" || true
    assert_contains "$output" "must be a number" "Negative validation"

    # Test: too high
    output="$(validate_choice "5" 3 2>&1)" || true
    assert_contains "$output" "between 1 and 3" "Too high validation"

    # Test: valid input
    output="$(validate_choice "2" 3 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "Valid input (2 of 3) accepted"

    # Test: boundary - first
    output="$(validate_choice "1" 3 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "Boundary input (1 of 3) accepted"

    # Test: boundary - last
    output="$(validate_choice "3" 3 2>&1)"
    exit_code=$?
    assert_success "$exit_code" "Boundary input (3 of 3) accepted"
}

test_direct_branch_switch() {
    echo ""
    echo "Testing direct branch switching..."

    setup_test_env
    test_dir="$TEST_DIR"
    fake_bin="$test_dir/bin"
    tmux_log="$test_dir/tmux.log"
    mkdir -p "$fake_bin"
    "$HYDRA_BIN" spawn feature/direct --no-agent >/dev/null
    tmux kill-session -t feature_direct 2>/dev/null || true
    project_id="$(sed -n '1p' .git/hydra/project-id)"
    head_dir="$(find "$HYDRA_HOME/state/v2/projects/$project_id/heads" -type f -name branch -exec dirname {} \; | sed -n '1p')"
    printf 'target-session\n' > "$head_dir/session"
    cat > "$fake_bin/tmux" <<'SCRIPT'
#!/bin/sh
case "$1" in
    has-session) exit 0 ;;
    switch-client|attach-session) printf '%s\n' "$*" >> "$TMUX_LOG" ;;
    *) exit 1 ;;
esac
SCRIPT
    chmod +x "$fake_bin/tmux"

    TMUX=inside TMUX_LOG="$tmux_log" PATH="$fake_bin:$PATH" \
        "$HYDRA_BIN" switch feature/direct
    assert_contains "$(sed -n '1p' "$tmux_log")" "switch-client -t target-session" \
        "Direct switch targets the selected branch inside tmux"

    TMUX='' TMUX_LOG="$tmux_log" PATH="$fake_bin:$PATH" \
        "$HYDRA_BIN" switch feature/direct
    assert_contains "$(sed -n '2p' "$tmux_log")" "attach-session -t target-session" \
        "Direct switch attaches to the selected branch outside tmux"

    cleanup_test_env "$test_dir"
}

# Run all tests
main() {
    echo "=========================================="
    echo "Running cmd_switch validation tests"
    echo "=========================================="

    # Unit tests for validation logic
    test_validate_choice_helper
    test_direct_branch_switch

    # Integration test through the real CLI (requires tmux)
    if command -v tmux >/dev/null 2>&1; then
        test_switch_menu_rejects_invalid_choices
    else
        echo ""
        echo "[SKIP] tmux not available, skipping integration tests"
    fi

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
