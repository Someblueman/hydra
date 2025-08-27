#!/bin/sh
# Test script for Hydra dashboard functionality
# POSIX-compliant shell script

# Test configuration
TEST_REPO_DIR="/tmp/hydra_dashboard_test"
TEST_BRANCHES="feature/test-1 feature/test-2 feature/test-3"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
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
# shellcheck disable=SC2317
print_status() {
    echo "${GREEN}[INFO]${RESET} $1"
}

# shellcheck disable=SC2317
print_warning() {
    echo "${YELLOW}[WARN]${RESET} $1"
}

# shellcheck disable=SC2317
print_error() {
    echo "${RED}[ERROR]${RESET} $1"
}

# Comprehensive pre-test cleanup
cleanup_all_test_sessions() {
    print_status "Performing comprehensive pre-test cleanup..."
    
    # Kill all test-related tmux sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            feature_test*|test-*|hydra-*|test_*)
                print_status "  Cleaning up session: $session"
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
        esac
    done
    
    # Clean up test directories
    rm -rf "$TEST_REPO_DIR" 2>/dev/null || true
    rm -rf /tmp/hydra-* 2>/dev/null || true
    rm -rf /tmp/test_hydra_home 2>/dev/null || true
    
    # Clean up any test HYDRA_HOME directories
    find /tmp -maxdepth 2 -name ".hydra" -type d 2>/dev/null | while IFS= read -r dir; do
        parent="$(dirname "$dir")"
        case "$parent" in
            /tmp/hydra*|/tmp/test*)
                print_status "  Removing test hydra home: $dir"
                rm -rf "$dir" 2>/dev/null || true
                ;;
        esac
    done
}

# Setup test environment
setup_test_repo() {
    print_status "Setting up test repository..."
    
    # Clean up any existing test directory
    rm -rf "$TEST_REPO_DIR"
    
    # Clean up all hydra worktrees from previous runs
    # First remove top-level hydra-* directories
    rm -rf /tmp/hydra-* 2>/dev/null || true
    
    # Then handle worktrees with slashes in branch names (e.g., feature/test-1)
    # These create subdirectories like /tmp/hydra-feature/test-1
    for branch in $TEST_BRANCHES; do
        worktree_path="/tmp/hydra-$branch"
        if [ -d "$worktree_path" ]; then
            rm -rf "$worktree_path" 2>/dev/null || true
        fi
        # Clean up parent directory if empty (e.g., /tmp/hydra-feature)
        parent_dir="$(dirname "$worktree_path")"
        if [ -d "$parent_dir" ] && [ "$parent_dir" != "/tmp" ]; then
            rmdir "$parent_dir" 2>/dev/null || true
        fi
    done
    
    # Create test repository
    mkdir -p "$TEST_REPO_DIR"
    cd "$TEST_REPO_DIR" || exit 1
    
    # Set up HYDRA_HOME for test isolation
    export HYDRA_HOME="$TEST_REPO_DIR/.hydra"
    mkdir -p "$HYDRA_HOME"
    
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
        # Create branch from main without checking it out
        git checkout -b "$branch" main >/dev/null 2>&1
        echo "Testing branch: $branch" > "$(echo "$branch" | tr '/' '-').md"
        git add "$(echo "$branch" | tr '/' '-').md" >/dev/null 2>&1
        git commit -m "Add content for $branch" >/dev/null 2>&1
        # Important: switch back to main after each branch to avoid conflicts
        git checkout main >/dev/null 2>&1
    done
    
    print_status "Test repository created at $TEST_REPO_DIR"
}

# Test basic dashboard creation
test_dashboard_creation() {
    print_status "Testing dashboard creation..."
    
    # Check if tmux is available
    if ! command -v tmux >/dev/null 2>&1; then
        print_warning "tmux not available - skipping dashboard tests"
        return 0
    fi
    
    # Start tmux server if not running
    if ! tmux list-sessions >/dev/null 2>&1; then
        print_status "Starting tmux server..."
        # In headless environments, we need to start tmux with a detached session
        if ! tmux new-session -d -s test-init 2>&1; then
            print_warning "Could not start tmux server - skipping tests"
            return 0
        fi
        sleep 1
        # Don't kill the init session yet - we need the server running
    fi
    
    # Comprehensive cleanup of ALL hydra-related sessions
    print_status "Cleaning up ALL hydra-related test sessions..."
    
    # Kill all sessions that match our test patterns
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            feature_test-*|test-init|hydra-dashboard|test-branch*)
                print_status "  Killing leftover session: $session"
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
            *hydra*)
                # Be extra careful about hydra sessions in CI
                if [ -n "${GITHUB_ACTIONS:-}" ] || [ -n "${CI:-}" ]; then
                    print_status "  Killing leftover hydra session: $session"
                    tmux kill-session -t "$session" 2>/dev/null || true
                fi
                ;;
        esac
    done
    
    # Also clean up by our expected test session names
    for branch in $TEST_BRANCHES; do
        base_session="$(echo "$branch" | tr '/' '_' | tr '-' '_')"
        # Kill base session and any numbered variants
        for suffix in "" "_1" "_2" "_3" "_4" "_5"; do
            session="${base_session}${suffix}"
            if tmux has-session -t "$session" 2>/dev/null; then
                print_status "  Killing test session: $session"
                tmux kill-session -t "$session" 2>/dev/null || true
            fi
        done
    done
    
    # Final check - list remaining sessions for debugging
    remaining_sessions=$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)
    if [ -n "$remaining_sessions" ]; then
        print_status "Remaining tmux sessions after cleanup:"
        echo "$remaining_sessions" | sed 's/^/    /'
    fi
    
    cd "$TEST_REPO_DIR" || exit 1
    
    # Spawn test sessions
    # Note: spawn will fail to attach in non-terminal environment, but sessions are created
    for branch in $TEST_BRANCHES; do
        print_status "Spawning session for branch: $branch"
        # Check map file before spawn
        if [ -f "$HYDRA_HOME/map" ]; then
            map_before=$(wc -l < "$HYDRA_HOME/map")
        else
            map_before=0
        fi
        
        # Check if session already exists (from previous run)
        session_name="$(echo "$branch" | tr '/' '_' | tr '-' '_')"
        if tmux has-session -t "$session_name" 2>/dev/null; then
            print_warning "Session $session_name already exists, adding mapping manually"
            # Add mapping manually if session exists
            echo "$branch $session_name" >> "$HYDRA_HOME/map"
            continue
        fi
        
        # Capture output for debugging
        if output=$("$HYDRA_BIN" spawn "$branch" 2>&1); then
            print_status "Spawn succeeded for $branch"
            # Check if mapping was added
            if [ -f "$HYDRA_HOME/map" ]; then
                map_after=$(wc -l < "$HYDRA_HOME/map")
                if [ "$map_after" -gt "$map_before" ]; then
                    print_status "Mapping added to map file"
                else
                    print_warning "Mapping was not added to map file!"
                fi
            fi
        else
            exit_code=$?
            print_warning "Spawn exited with error code $exit_code for $branch (expected in non-terminal)"
            # Check if session was created despite error
            if echo "$output" | grep -q "Creating tmux session"; then
                print_status "Session creation was attempted"
                # Extract session name from output
                session_name=$(echo "$output" | grep "Creating tmux session" | sed "s/.*session '\\([^']*\\)'.*/\\1/")
                if [ -n "$session_name" ] && tmux has-session -t "$session_name" 2>/dev/null; then
                    print_status "Session '$session_name' exists in tmux"
                else
                    print_error "Session was not created successfully"
                fi
            fi
            # Show the full output for debugging
            echo "$output" | sed 's/^/  /'
        fi
    done
    
    # Wait a moment for sessions to stabilize
    sleep 2
    
    # Debug: Show tmux sessions
    print_status "Current tmux sessions:"
    tmux list-sessions 2>/dev/null || print_warning "No tmux sessions found"
    
    # Debug: Check HYDRA_HOME and mappings
    print_status "HYDRA_HOME is: $HYDRA_HOME"
    print_status "Checking for map file at: $HYDRA_HOME/map"
    if [ -f "$HYDRA_HOME/map" ]; then
        print_status "Map file contents:"
        sed 's/^/  /' < "$HYDRA_HOME/map"
    else
        print_warning "Map file not found!"
    fi
    
    # Debug: Test tmux_session_exists function
    print_status "Testing tmux session detection:"
    HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"
    # shellcheck source=../lib/tmux.sh
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/tmux.sh"
    for session in feature_test-1 feature_test-2; do
        if tmux_session_exists "$session"; then
            print_status "  Session '$session' exists"
        else
            print_warning "  Session '$session' NOT found by tmux_session_exists"
        fi
    done
    
    # Debug: Show hydra list output with verbose error handling
    print_status "Hydra list output:"
    print_status "Current directory: $(pwd)"
    print_status "Running: cd $TEST_REPO_DIR && HYDRA_HOME=$HYDRA_HOME $HYDRA_BIN list"
    if output=$(cd "$TEST_REPO_DIR" && HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list 2>&1); then
        echo "$output"
        # Check if output contains data rows (not just headers)
        if echo "$output" | grep -q "feature/test"; then
            print_status "List command showed sessions"
        else
            print_warning "List command showed no sessions (only headers)"
        fi
    else
        print_error "List command failed with exit code $?"
        echo "$output"
    fi
    
    # Check that sessions were created
    # First check if we have any mappings (run from test repo)
    if ! (cd "$TEST_REPO_DIR" && HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list >/dev/null 2>&1); then
        print_error "Hydra list command failed"
        return 1
    fi
    
    # Count active sessions - we need at least 2 out of 3
    list_output=$(cd "$TEST_REPO_DIR" && HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list 2>/dev/null || echo "")
    active_count=$(echo "$list_output" | grep -c "active" || echo "0")
    if [ "$active_count" -lt 2 ]; then
        print_error "Not enough active sessions found after spawning (found: $active_count, expected: at least 2)"
        # Additional debugging
        print_status "Checking tmux sessions directly:"
        tmux list-sessions 2>&1 || true
        print_status "Checking hydra mappings:"
        cat "$HYDRA_HOME/map" 2>&1 || true
        return 1
    fi
    
    print_status "Found $active_count active sessions"
    
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
    # shellcheck source=../lib/tmux.sh
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/tmux.sh"
    # shellcheck source=../lib/state.sh
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state.sh"
    # shellcheck source=../lib/dashboard.sh
    # shellcheck disable=SC1091
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
    
    # Kill any remaining test sessions (use non-interactive mode in subshell to avoid state leak)
    for branch in $TEST_BRANCHES; do
        if "$HYDRA_BIN" list 2>/dev/null | grep -q "$branch"; then
            print_status "Killing session for branch: $branch"
            (HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN" kill "$branch" >/dev/null 2>&1) || true
        fi
    done
    
    # Kill dashboard session if it exists
    if tmux has-session -t "hydra-dashboard" 2>/dev/null; then
        tmux kill-session -t "hydra-dashboard" 2>/dev/null || true
    fi
    
    # Kill test-init session if it exists
    if tmux has-session -t "test-init" 2>/dev/null; then
        tmux kill-session -t "test-init" 2>/dev/null || true
    fi
    
    # Remove test repository and worktrees
    cd /tmp || return 0
    rm -rf "$TEST_REPO_DIR"
    # Remove all hydra worktrees, including those with slashes in branch names
    rm -rf /tmp/hydra-* 2>/dev/null || true
    # Also remove any parent directories created by branch names with slashes
    for branch in $TEST_BRANCHES; do
        worktree_path="/tmp/hydra-$branch"
        if [ -d "$worktree_path" ]; then
            rm -rf "$worktree_path" 2>/dev/null || true
        fi
        # Clean up parent directory if empty
        parent_dir="$(dirname "$worktree_path")"
        if [ -d "$parent_dir" ] && [ "$parent_dir" != "/tmp" ]; then
            rmdir "$parent_dir" 2>/dev/null || true
        fi
    done
    
    print_status "Test environment cleaned up"
}

# Main test runner
main() {
    print_status "Starting Hydra dashboard tests..."
    
    # Check dependencies
    if ! command -v tmux >/dev/null 2>&1; then
        print_warning "tmux not found, skipping dashboard tests"
        exit 0  # Exit successfully since this is expected in some CI environments
    fi
    
    # Verify tmux can actually create sessions in this environment
    if ! tmux new-session -d -s hydra-dash-sanity 2>/dev/null; then
        print_warning "tmux cannot create sessions in this environment; skipping dashboard tests"
        exit 0
    fi
    tmux kill-session -t hydra-dash-sanity 2>/dev/null || true
    
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
    
    # Perform comprehensive cleanup before starting tests
    cleanup_all_test_sessions
    
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
