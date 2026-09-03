#!/bin/sh
# Test script for Hydra dashboard functionality
# POSIX-compliant shell script

# Test configuration
TEST_REPO_DIR="/tmp/hydra_dashboard_test"
TEST_BRANCHES="feature/test-1 feature/test-2 feature/test-3"
UNRELATED_TEST_DIR="/tmp/hydra-unrelated-dashboard-$$"
UNRELATED_TEST_SESSION="hydra-unrelated-dashboard-$$"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HYDRA_BIN="$SCRIPT_DIR/../bin/hydra"
export HYDRA_SKIP_AI=1
export HYDRA_NONINTERACTIVE=1

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

cleanup_test_directories() {
    rm -rf "$TEST_REPO_DIR" /tmp/test_hydra_home 2>/dev/null || true

    for branch in $TEST_BRANCHES; do
        worktree_path="/tmp/hydra-$branch"
        rm -rf "$worktree_path" 2>/dev/null || true

        # Slash-containing branch names share a test-owned parent.
        parent_dir="$(dirname "$worktree_path")"
        if [ "$parent_dir" != "/tmp" ]; then
            rmdir "$parent_dir" 2>/dev/null || true
        fi
    done
}

# Comprehensive pre-test cleanup
cleanup_all_test_sessions() {
    print_status "Performing comprehensive pre-test cleanup..."
    
    # Kill all test-related tmux sessions
    tmux list-sessions -F '#{session_name}' 2>/dev/null | while IFS= read -r session; do
        case "$session" in
            feature_test-1|feature_test-2|feature_test-3|hydra-dashboard|hydra-dash-sanity|test-init)
                print_status "  Cleaning up session: $session"
                tmux kill-session -t "$session" 2>/dev/null || true
                ;;
        esac
    done

    cleanup_test_directories
}

# Setup test environment
setup_test_repo() {
    print_status "Setting up test repository..."

    cleanup_test_directories
    
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

# shellcheck disable=SC1091
. "$SCRIPT_DIR/dashboard_cases.sh"

# Cleanup test environment
cleanup_test_env() {
    print_status "Cleaning up test environment..."

    # Remove only the unrelated sentinels created by this test.
    tmux kill-session -t "$UNRELATED_TEST_SESSION" 2>/dev/null || true
    rmdir "$UNRELATED_TEST_DIR" 2>/dev/null || true
    
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
    cleanup_test_directories
    
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

    # A dashboard test run must not sweep unrelated hydra-* resources.
    mkdir -p "$UNRELATED_TEST_DIR"
    tmux new-session -d -s "$UNRELATED_TEST_SESSION"
    
    # Perform comprehensive cleanup before starting tests
    cleanup_all_test_sessions
    if [ ! -d "$UNRELATED_TEST_DIR" ]; then
        print_error "Pre-test cleanup removed an unrelated hydra-* directory"
        exit 1
    fi
    if ! tmux has-session -t "$UNRELATED_TEST_SESSION" 2>/dev/null; then
        print_error "Pre-test cleanup killed an unrelated hydra-* tmux session"
        exit 1
    fi
    tmux kill-session -t "$UNRELATED_TEST_SESSION" 2>/dev/null || true
    rmdir "$UNRELATED_TEST_DIR" 2>/dev/null || true
    
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
        test_pane_collection_and_restore || test_failed=1
    fi
    if [ "$test_failed" -eq 0 ]; then
        test_multi_pane_collection_env || test_failed=1
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
