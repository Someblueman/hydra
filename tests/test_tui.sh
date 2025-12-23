#!/bin/sh
# Test script for TUI functionality
# POSIX-compliant shell script

set -eu

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HYDRA_BIN="$SCRIPT_DIR/../bin/hydra"
HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"

# Colors for test output (if supported)
if [ -t 1 ] && command -v tput >/dev/null 2>&1; then
    RED="$(tput setaf 1)"
    GREEN="$(tput setaf 2)"
    YELLOW="$(tput setaf 3)"
    RESET="$(tput sgr0)"
else
    RED=""
    GREEN=""
    YELLOW=""
    RESET=""
fi

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Print test result
print_pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo "${GREEN}[PASS]${RESET} $1"
}

print_fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo "${RED}[FAIL]${RESET} $1"
}

print_skip() {
    echo "${YELLOW}[SKIP]${RESET} $1"
}

# Source libraries for unit testing
# shellcheck disable=SC1091
source_libs() {
    # Set up required globals
    HYDRA_HOME="${HYDRA_HOME:-$HOME/.hydra}"
    HYDRA_MAP="$HYDRA_HOME/map"

    # Source output first (no deps)
    . "$HYDRA_LIB_DIR/output.sh"

    # Source paths (no deps)
    . "$HYDRA_LIB_DIR/paths.sh"

    # Source locks (no deps)
    . "$HYDRA_LIB_DIR/locks.sh"

    # Source tmux (no deps)
    . "$HYDRA_LIB_DIR/tmux.sh"

    # Source state (depends on locks)
    . "$HYDRA_LIB_DIR/state.sh"

    # Source TUI library
    . "$HYDRA_LIB_DIR/tui.sh"
}

# Test: TUI requires interactive terminal
test_tui_requires_terminal() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Run TUI with stdin from /dev/null (non-interactive)
    output=$(echo "" | "$HYDRA_BIN" tui 2>&1 || true)

    if echo "$output" | grep -qi "interactive terminal\|requires.*terminal\|not.*terminal"; then
        print_pass "TUI requires interactive terminal"
    else
        print_fail "TUI should require interactive terminal, got: $output"
    fi
}

# Test: TUI command exists in help
test_tui_in_help() {
    TESTS_RUN=$((TESTS_RUN + 1))

    output=$("$HYDRA_BIN" help 2>&1 || true)

    if echo "$output" | grep -q "tui"; then
        print_pass "TUI command listed in help"
    else
        print_fail "TUI command should be listed in help"
    fi
}

# Test: tput availability check
test_tput_check() {
    TESTS_RUN=$((TESTS_RUN + 1))

    if command -v tput >/dev/null 2>&1; then
        print_pass "tput is available"
    else
        print_skip "tput not available (TUI will have limited formatting)"
    fi
}

# Test: TUI color initialization
test_tui_init_colors() {
    TESTS_RUN=$((TESTS_RUN + 1))

    source_libs

    # Initialize colors
    tui_init_colors

    # Check that color variables are set (even if empty on non-tput systems)
    if [ -n "${TUI_RESET+x}" ]; then
        print_pass "TUI color variables initialized"
    else
        print_fail "TUI color variables should be initialized"
    fi
}

# Test: Session list building
test_tui_build_list() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Create temporary test environment
    TEST_HOME="$(mktemp -d)"
    HYDRA_HOME="$TEST_HOME"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"

    # Create mock session data
    echo "test-branch-1 test_session_1" > "$HYDRA_MAP"
    echo "test-branch-2 test_session_2 claude" >> "$HYDRA_MAP"

    source_libs

    # Create temp file for list
    TUI_TEMP_LIST="$(mktemp)"
    TUI_ITEM_COUNT=0
    TUI_SELECTED=0

    # Build the list (sessions won't exist in tmux, so status will be DEAD)
    tui_build_list

    # Check item count
    if [ "$TUI_ITEM_COUNT" -eq 2 ]; then
        print_pass "TUI builds session list correctly (count=$TUI_ITEM_COUNT)"
    else
        print_fail "TUI should build 2 items, got $TUI_ITEM_COUNT"
    fi

    # Cleanup
    rm -f "$TUI_TEMP_LIST"
    rm -rf "$TEST_HOME"
}

# Test: Selection bounds checking
test_tui_selection_bounds() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Create temporary test environment
    TEST_HOME="$(mktemp -d)"
    HYDRA_HOME="$TEST_HOME"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"

    # Create mock session data
    echo "branch-1 session_1" > "$HYDRA_MAP"
    echo "branch-2 session_2" >> "$HYDRA_MAP"

    source_libs

    # Initialize state (TUI_OFFSET and TUI_ROWS used by tui_build_list)
    TUI_TEMP_LIST="$(mktemp)"
    TUI_ITEM_COUNT=0
    TUI_SELECTED=5  # Start out of bounds
    # shellcheck disable=SC2034
    TUI_OFFSET=0
    # shellcheck disable=SC2034
    TUI_ROWS=24

    # Build list - should adjust selection
    tui_build_list

    # Selection should be clamped to valid range
    if [ "$TUI_SELECTED" -lt "$TUI_ITEM_COUNT" ] && [ "$TUI_SELECTED" -ge 0 ]; then
        print_pass "TUI selection bounds checking works"
    else
        print_fail "TUI selection out of bounds: $TUI_SELECTED (count=$TUI_ITEM_COUNT)"
    fi

    # Cleanup
    rm -f "$TUI_TEMP_LIST"
    rm -rf "$TEST_HOME"
}

# Test: Empty session list handling
test_tui_empty_list() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Create temporary test environment with empty map
    TEST_HOME="$(mktemp -d)"
    HYDRA_HOME="$TEST_HOME"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"
    touch "$HYDRA_MAP"  # Empty file

    source_libs

    # Initialize state
    TUI_TEMP_LIST="$(mktemp)"
    TUI_ITEM_COUNT=0
    TUI_SELECTED=0

    # Build list
    tui_build_list

    # Should handle empty list gracefully
    if [ "$TUI_ITEM_COUNT" -eq 0 ]; then
        print_pass "TUI handles empty session list"
    else
        print_fail "TUI should have 0 items for empty map"
    fi

    # Cleanup
    rm -f "$TUI_TEMP_LIST"
    rm -rf "$TEST_HOME"
}

# Test: Key handling - quit
test_tui_key_quit() {
    TESTS_RUN=$((TESTS_RUN + 1))

    source_libs

    # Initialize minimal state (variables used by tui_handle_key)
    TUI_ITEM_COUNT=0
    TUI_SELECTED=0
    # shellcheck disable=SC2034
    TUI_OFFSET=0
    # shellcheck disable=SC2034
    TUI_ROWS=24

    # Test quit key returns 1 (exit signal)
    if ! tui_handle_key "q"; then
        print_pass "TUI quit key (q) signals exit"
    else
        print_fail "TUI quit key should return non-zero"
    fi
}

# Test: Key handling - navigation
test_tui_key_navigation() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Create temporary test environment
    TEST_HOME="$(mktemp -d)"
    HYDRA_HOME="$TEST_HOME"
    HYDRA_MAP="$HYDRA_HOME/map"
    mkdir -p "$HYDRA_HOME"

    # Create mock sessions
    echo "branch-1 session_1" > "$HYDRA_MAP"
    echo "branch-2 session_2" >> "$HYDRA_MAP"
    echo "branch-3 session_3" >> "$HYDRA_MAP"

    source_libs

    # Initialize state (variables used by tui_build_list and tui_handle_key)
    TUI_TEMP_LIST="$(mktemp)"
    TUI_ITEM_COUNT=0
    TUI_SELECTED=0
    # shellcheck disable=SC2034
    TUI_OFFSET=0
    # shellcheck disable=SC2034
    TUI_ROWS=24

    tui_build_list

    # Test move down
    tui_handle_key "j"
    if [ "$TUI_SELECTED" -eq 1 ]; then
        # Test move up
        tui_handle_key "k"
        if [ "$TUI_SELECTED" -eq 0 ]; then
            print_pass "TUI navigation keys (j/k) work"
        else
            print_fail "TUI k key should move up"
        fi
    else
        print_fail "TUI j key should move down"
    fi

    # Cleanup
    rm -f "$TUI_TEMP_LIST"
    rm -rf "$TEST_HOME"
}

# Test: Terminal state save/restore pattern
test_tui_stty_pattern() {
    TESTS_RUN=$((TESTS_RUN + 1))

    # Check that stty is available
    if ! command -v stty >/dev/null 2>&1; then
        print_skip "stty not available"
        return 0
    fi

    # Save current state
    saved="$(stty -g 2>/dev/null || true)"

    if [ -n "$saved" ]; then
        # Verify we can restore it
        if stty "$saved" 2>/dev/null; then
            print_pass "Terminal state save/restore works"
        else
            print_fail "Could not restore terminal state"
        fi
    else
        print_skip "Could not save terminal state"
    fi
}

# Main test runner
main() {
    echo "Running Hydra TUI tests..."
    echo "=============================="
    echo ""

    # Check if TUI library exists
    if [ ! -f "$HYDRA_LIB_DIR/tui.sh" ]; then
        echo "${RED}[ERROR]${RESET} TUI library not found at $HYDRA_LIB_DIR/tui.sh"
        echo "Please create the TUI library first"
        exit 1
    fi

    # Run tests
    test_tui_requires_terminal
    test_tui_in_help
    test_tput_check
    test_tui_init_colors
    test_tui_build_list
    test_tui_selection_bounds
    test_tui_empty_list
    test_tui_key_quit
    test_tui_key_navigation
    test_tui_stty_pattern

    # Summary
    echo ""
    echo "=============================="
    echo "Tests run: $TESTS_RUN"
    echo "Passed: ${GREEN}$TESTS_PASSED${RESET}"
    echo "Failed: ${RED}$TESTS_FAILED${RESET}"

    if [ "$TESTS_FAILED" -gt 0 ]; then
        exit 1
    fi

    exit 0
}

# Run tests if executed directly
if [ "${0##*/}" = "test_tui.sh" ]; then
    main "$@"
fi
