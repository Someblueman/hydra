#!/bin/sh
# Dashboard integration test cases sourced by test_dashboard.sh
# shellcheck disable=SC2317

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
        session_name="$(echo "$branch" | tr '/' '_' | tr '-' '_')"
        if tmux has-session -t "$session_name" 2>/dev/null; then
            print_warning "Removing leftover session $session_name before spawn"
            tmux kill-session -t "$session_name"
        fi
        
        # Capture output for debugging
        if output=$("$HYDRA_BIN" spawn "$branch" 2>&1); then
            print_status "Spawn succeeded for $branch"
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
    
    # Debug: Check HYDRA_HOME and durable state
    print_status "HYDRA_HOME is: $HYDRA_HOME"
    print_status "Durable project state:"
    find "$HYDRA_HOME/state/v2/projects" -maxdepth 4 -type f -print 2>/dev/null | sed 's/^/  /'
    
    # Debug: Test tmux_session_exists function
    print_status "Testing tmux session detection:"
    HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/output.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/locks.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/identity.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state_v2.sh"
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
    # Confirm the durable project state can be listed from the test repository.
    if ! (cd "$TEST_REPO_DIR" && HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list >/dev/null 2>&1); then
        print_error "Hydra list command failed"
        return 1
    fi
    
    # Count active sessions - we need at least 2 out of 3
    list_output=$(cd "$TEST_REPO_DIR" && HYDRA_HOME="$HYDRA_HOME" "$HYDRA_BIN" list 2>/dev/null || echo "")
    # Count listed heads and coerce an empty result to zero.
    active_count=$(printf '%s' "$list_output" | awk '/ -> /{c++} END{ if(c=="" || c==0){print 0}else{print c} }')
    if [ "$active_count" -lt 2 ]; then
        print_error "Not enough active sessions found after spawning (found: $active_count, expected: at least 2)"
        # Additional debugging
        print_status "Checking tmux sessions directly:"
        tmux list-sessions 2>&1 || true
        print_status "Checking Hydra durable state:"
        (cd "$TEST_REPO_DIR" && "$HYDRA_BIN" list --json) 2>&1 || true
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
test_pane_collection_and_restore() {
    print_status "Testing pane collection and restoration..."
    cd "$TEST_REPO_DIR" || exit 1

    # Source libs
    HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/tmux.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/dashboard.sh"

    state_rows="$(state_list_heads)"
    if [ -z "$state_rows" ]; then
        print_warning "No active Hydra heads found, skipping pane collection test"
        return 0
    fi

    # Count active source sessions
    expected_sessions=0
    while IFS=' ' read -r branch session _ai _group _ts; do
        if tmux_session_exists "$session"; then
            expected_sessions=$((expected_sessions + 1))
        fi
    done <<EOF
$state_rows
EOF
    if [ "$expected_sessions" -lt 1 ]; then
        print_warning "No active sessions found, skipping pane collection test"
        return 0
    fi

    # Create dashboard session
    if ! create_dashboard_session; then
        print_error "Failed to create dashboard session"
        return 1
    fi

    # Record initial pane count in the dashboard (usually 1)
    initial_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')

    # Ensure source sessions survive collection by adding an extra pane to each
    while IFS=' ' read -r branch session _ai _group _ts; do
        if tmux_session_exists "$session"; then
            # Split a new pane in first window to keep the session alive
            tmux split-window -t "$session" -d 2>/dev/null || true
        fi
    done <<EOF
$state_rows
EOF

    # Collect panes
    if ! collect_session_panes; then
        print_error "collect_session_panes failed"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    # Verify restoration map populated
    if [ ! -s "$DASHBOARD_RESTORE_MAP" ]; then
        print_error "Restoration map not created or empty"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi
    collected_lines=$(wc -l < "$DASHBOARD_RESTORE_MAP" | tr -d ' ')

    # Verify panes joined into dashboard increased by collected count
    after_collect_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')
    expected_after=$((initial_panes + collected_lines))
    if [ "$after_collect_panes" -ne "$expected_after" ]; then
        print_error "Unexpected pane count in dashboard (got $after_collect_panes, expected $expected_after)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    print_status "Collected $collected_lines panes into dashboard (initial $initial_panes -> $after_collect_panes)"

    # Restore panes back
    if ! restore_panes; then
        print_error "restore_panes reported failures"
        # proceed to check counts but mark failure
        failed_restore=1
    else
        failed_restore=0
    fi

    # After restore, the dashboard should be back to its initial pane count
    after_restore_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$after_restore_panes" -ne "$initial_panes" ]; then
        print_error "Dashboard pane count after restore is $after_restore_panes (expected $initial_panes)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    # Cleanup dashboard session
    tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true

    if [ "$failed_restore" -ne 0 ]; then
        return 1
    fi
    print_status "Pane collection and restoration verified"
    return 0
}

# Test multi-pane collection via env
test_multi_pane_collection_env() {
    print_status "Testing multi-pane collection (env: 2 panes per session)..."
    cd "$TEST_REPO_DIR" || exit 1

    HYDRA_LIB_DIR="$SCRIPT_DIR/../lib"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/output.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/locks.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/identity.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state_v2.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/tmux.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/state.sh"
    # shellcheck disable=SC1091
    . "$HYDRA_LIB_DIR/dashboard.sh"

    state_rows="$(state_list_heads)"
    if [ -z "$state_rows" ]; then
        print_warning "No active Hydra heads found, skipping multi-pane test"
        return 0
    fi

    # Ensure each source session has at least 3 panes so we can collect 2 and leave 1 behind
    expected_sessions=0
    while IFS=' ' read -r branch session _ai _group _ts; do
        if tmux_session_exists "$session"; then
            expected_sessions=$((expected_sessions + 1))
            # Ensure exactly >=3 panes by splitting until 3
            pcnt=$(tmux list-panes -t "$session" 2>/dev/null | wc -l | tr -d ' ')
            while [ "${pcnt:-0}" -lt 3 ]; do
                tmux split-window -t "$session" -d 2>/dev/null || true
                pcnt=$(tmux list-panes -t "$session" 2>/dev/null | wc -l | tr -d ' ')
            done
        fi
    done <<EOF
$state_rows
EOF
    if [ "$expected_sessions" -lt 1 ]; then
        print_warning "No active sessions found, skipping multi-pane test"
        return 0
    fi

    # Create a new dashboard session
    if ! create_dashboard_session; then
        print_error "Failed to create dashboard session"
        return 1
    fi

    initial_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')

    # Collect up to 2 panes per session
    export HYDRA_DASHBOARD_PANES_PER_SESSION=2
    if ! collect_session_panes; then
        print_error "collect_session_panes failed (env=2)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    collected_lines=$(wc -l < "$DASHBOARD_RESTORE_MAP" | tr -d ' ')
    # Compute expected collected panes with cap=2 per session (leaving one behind)
    expected_collected=0
    while IFS=' ' read -r branch session _ai _group _ts; do
        if tmux_session_exists "$session"; then
            pcnt=$(tmux list-panes -t "$session" 2>/dev/null | wc -l | tr -d ' ')
            if [ "${pcnt:-0}" -gt 1 ]; then
                avail=$((pcnt - 1))
                [ "$avail" -gt 2 ] && avail=2
                expected_collected=$((expected_collected + avail))
            fi
        fi
    done <<EOF
$state_rows
EOF

    if [ "$collected_lines" -ne "$expected_collected" ]; then
        print_error "Expected to collect $expected_collected panes, got $collected_lines"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    after_collect_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')
    expected_after=$((initial_panes + collected_lines))
    if [ "$after_collect_panes" -ne "$expected_after" ]; then
        print_error "Unexpected pane count in dashboard (got $after_collect_panes, expected $expected_after)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    # Restore
    if ! restore_panes; then
        print_error "restore_panes reported failures (env=2)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    # After restore, initial pane count persists
    after_restore_panes=$(tmux list-panes -t "$DASHBOARD_SESSION" 2>/dev/null | wc -l | tr -d ' ')
    if [ "$after_restore_panes" -ne "$initial_panes" ]; then
        print_error "Dashboard pane count after restore is $after_restore_panes (expected $initial_panes)"
        tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
        return 1
    fi

    # Clean up dashboard session
    tmux kill-session -t "$DASHBOARD_SESSION" 2>/dev/null || true
    unset HYDRA_DASHBOARD_PANES_PER_SESSION
    print_status "Multi-pane collection via env verified"
    return 0
}
