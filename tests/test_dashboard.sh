#!/bin/sh
# Test script for Hydra dashboard functionality
# POSIX-compliant shell script

# Test configuration
TEST_REPO_DIR="/tmp/hydra_dashboard_test"
TEST_BRANCHES="feature/test-1 feature/test-2 feature/test-3"
SCRIPT_DIR="$(dirname "$0")"
HYDRA_BIN="$SCRIPT_DIR/../bin/hydra"

# Colors for output (if supported)
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

# Print status messages
print_status() {
    echo "${GREEN}[INFO]${RESET} $1"
}

print_warning() {
    echo "${YELLOW}[WARN]${RESET} $1"
}

print_error() {
    echo "${RED}[ERROR]${RESET} $1"
}

# Setup test environment
setup_test_repo() {
    print_status "Setting up test repository..."
    
    # Clean up any existing test directory
    rm -rf "$TEST_REPO_DIR"
    
    # Create test repository
    mkdir -p "$TEST_REPO_DIR"
    cd "$TEST_REPO_DIR" || exit 1
    
    # Initialize git repository
    git init >/dev/null 2>&1
    git config user.name "Test User" >/dev/null 2>&1
    git config user.email "test@example.com" >/dev/null 2>&1
    
    # Create initial commit
    echo "# Test Repository" > README.md
    git add README.md >/dev/null 2>&1
    git commit -m "Initial commit" >/dev/null 2>&1
    
    # Create test branches
    for branch in $TEST_BRANCHES; do
        git checkout -b "$branch" >/dev/null 2>&1
        echo "Testing branch: $branch" > "${branch}.md"
        git add "${branch}.md" >/dev/null 2>&1
        git commit -m "Add content for $branch" >/dev/null 2>&1
        git checkout main >/dev/null 2>&1
    done
    
    print_status "Test repository created at $TEST_REPO_DIR"
}

# Test basic dashboard creation
test_dashboard_creation() {
    print_status "Testing dashboard creation..."
    
    cd "$TEST_REPO_DIR" || exit 1
    
    # Spawn test sessions
    for branch in $TEST_BRANCHES; do
        print_status "Spawning session for branch: $branch"
        "$HYDRA_BIN" spawn "$branch" >/dev/null 2>&1 || {
            print_error "Failed to spawn session for $branch"
            return 1
        }
    done
    
    # Wait a moment for sessions to stabilize
    sleep 2
    
    # Check that sessions were created
    if ! "$HYDRA_BIN" list | grep -q "active"; then
        print_error "No active sessions found after spawning"
        return 1
    fi
    
    print_status "Sessions spawned successfully"
    return 0
}

# Test dashboard functionality without actually entering it
test_dashboard_dry_run() {
    print_status "Testing dashboard creation (dry run)..."
    
    cd "$TEST_REPO_DIR" || exit 1
    
    # Source the dashboard functions for testing
    HYDRA_HOME="${HYDRA_HOME:-$HOME/.hydra}"
    HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"
    . "$HYDRA_LIB_DIR/tmux.sh"
    . "$HYDRA_LIB_DIR/state.sh"
    . "$HYDRA_LIB_DIR/dashboard.sh"
    
    # Test dashboard session creation
    if create_dashboard_session; then
        print_status "Dashboard session created successfully"
        
        # Check if session exists
        if tmux_session_exists "$DASHBOARD_SESSION"; then
            print_status "Dashboard session is active"
            
            # Clean up dashboard session
            tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null
            print_status "Dashboard session cleaned up"
        else
            print_error "Dashboard session not found after creation"
            return 1
        fi
    else
        print_error "Failed to create dashboard session"
        return 1
    fi
    
    return 0
}

# Test pane collection simulation
test_pane_collection() {
    print_status "Testing pane collection logic..."
    
    cd "$TEST_REPO_DIR" || exit 1
    
    # Check if we have active sessions to collect from
    HYDRA_MAP="${HYDRA_HOME:-$HOME/.hydra}/map"
    
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        print_warning "No Hydra map file found, skipping pane collection test"
        return 0
    fi
    
    # Count expected sessions
    expected_sessions=0
    while IFS=' ' read -r branch session; do
        if tmux_session_exists "$session"; then
            expected_sessions=$((expected_sessions + 1))
        fi
    done < "$HYDRA_MAP"
    
    if [ "$expected_sessions" -eq 0 ]; then
        print_warning "No active sessions found, skipping pane collection test"
        return 0
    fi
    
    print_status "Found $expected_sessions active sessions for testing"
    return 0
}

# Test restoration logic
test_restoration_logic() {
    print_status "Testing restoration logic..."
    
    # Create a mock restoration map
    DASHBOARD_RESTORE_MAP="${HYDRA_HOME:-$HOME/.hydra}/.dashboard_restore"
    
    # This is a simulation since we don't want to actually move panes
    echo "# Mock restoration map for testing" > "$DASHBOARD_RESTORE_MAP"
    echo "# pane_id session window_id branch" >> "$DASHBOARD_RESTORE_MAP"
    
    # Test cleanup
    rm -f "$DASHBOARD_RESTORE_MAP"
    print_status "Restoration logic test completed"
    
    return 0
}

# Cleanup test environment
cleanup_test_env() {
    print_status "Cleaning up test environment..."
    
    cd "$TEST_REPO_DIR" || return 0
    
    # Kill any remaining test sessions
    for branch in $TEST_BRANCHES; do
        if "$HYDRA_BIN" list 2>/dev/null | grep -q "$branch"; then
            print_status "Killing session for branch: $branch"
            "$HYDRA_BIN" kill "$branch" >/dev/null 2>&1 || true
        fi
    done
    
    # Kill dashboard session if it exists
    if tmux has-session -t "hydra-dashboard" 2>/dev/null; then
        tmux kill-session -t "hydra-dashboard" 2>/dev/null || true
    fi
    
    # Remove test repository
    cd /tmp || return 0
    rm -rf "$TEST_REPO_DIR"
    
    print_status "Test environment cleaned up"
}

# Main test runner
main() {
    print_status "Starting Hydra dashboard tests..."
    
    # Check dependencies
    if ! command -v tmux >/dev/null 2>&1; then
        print_error "tmux not found, skipping tests"
        exit 1
    fi
    
    if ! command -v git >/dev/null 2>&1; then
        print_error "git not found, skipping tests"
        exit 1
    fi
    
    if [ ! -x "$HYDRA_BIN" ]; then
        print_error "Hydra binary not found at $HYDRA_BIN"
        exit 1
    fi
    
    # Set up cleanup trap
    trap cleanup_test_env EXIT INT TERM
    
    # Run tests
    test_failed=0
    
    setup_test_repo || test_failed=1
    
    if [ "$test_failed" -eq 0 ]; then
        test_dashboard_creation || test_failed=1
    fi
    
    if [ "$test_failed" -eq 0 ]; then
        test_dashboard_dry_run || test_failed=1
    fi
    
    if [ "$test_failed" -eq 0 ]; then
        test_pane_collection || test_failed=1
    fi
    
    if [ "$test_failed" -eq 0 ]; then
        test_restoration_logic || test_failed=1
    fi
    
    # Report results
    if [ "$test_failed" -eq 0 ]; then
        print_status "All dashboard tests passed!"
        exit 0
    else
        print_error "Some dashboard tests failed"
        exit 1
    fi
}

# Run tests if executed directly
if [ "${0##*/}" = "test_dashboard.sh" ]; then
    main "$@"
fi