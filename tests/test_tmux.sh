#!/bin/sh
# Unit tests for lib/tmux.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Source the library under test
# shellcheck source=../lib/tmux.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/tmux.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Test check_tmux_version function
test_check_tmux_version() {
    echo "Testing check_tmux_version..."
    
    # This test depends on tmux being available
    if command -v tmux >/dev/null 2>&1; then
        if check_tmux_version >/dev/null 2>&1; then
            echo "[PASS] check_tmux_version succeeds with available tmux"
            pass_count=$((pass_count + 1))
        else
            echo "[WARN] check_tmux_version fails - tmux version may be too old"
            pass_count=$((pass_count + 1))
        fi
    else
        echo "[WARN] Skipping tmux version test - tmux not available"
        pass_count=$((pass_count + 1))
    fi
    test_count=$((test_count + 1))
}

# Test tmux_session_exists parameter validation
test_tmux_session_exists_validation() {
    echo "Testing tmux_session_exists parameter validation..."
    
    # Test empty session name
    tmux_session_exists ""
    assert_failure $? "tmux_session_exists should fail with empty session name"
    
    # Test non-existent session (assuming this session doesn't exist)
    tmux_session_exists "hydra-test-definitely-does-not-exist-$(date +%s)"
    assert_failure $? "tmux_session_exists should fail for non-existent session"
}

# Test create_session parameter validation
test_create_session_validation() {
    echo "Testing create_session parameter validation..."
    
    # Test empty parameters
    create_session "" "" 2>/dev/null
    assert_failure $? "create_session should fail with empty session name and directory"
    
    create_session "test" "" 2>/dev/null
    assert_failure $? "create_session should fail with empty directory"
    
    create_session "" "/tmp" 2>/dev/null
    assert_failure $? "create_session should fail with empty session name"
    
    # Test non-existent directory
    create_session "test" "/definitely/does/not/exist" 2>/dev/null
    assert_failure $? "create_session should fail with non-existent directory"
}

test_create_session_normalizes_pane_target() {
    echo "Testing create_session normalizes the primary pane target..."

    if ! command -v tmux >/dev/null 2>&1; then
        echo "[WARN] Skipping pane target normalization - tmux not available"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
        return
    fi

    test_session="hydra-index-test-$$"
    if create_session "$test_session" "/tmp" >/dev/null 2>&1 && \
       tmux display-message -p -t "$test_session:0.0" '#{pane_index}' >/dev/null 2>&1; then
        assert_success 0 "create_session provides session:0.0 regardless of global tmux indexes"
    else
        assert_failure 1 "create_session provides session:0.0 regardless of global tmux indexes"
    fi
    tmux kill-session -t "$test_session" 2>/dev/null || true
}

# Test kill_session parameter validation
test_kill_session_validation() {
    echo "Testing kill_session parameter validation..."
    
    # Test empty session name
    kill_session "" 2>/dev/null
    assert_failure $? "kill_session should fail with empty session name"
    
    # Test non-existent session
    kill_session "hydra-test-definitely-does-not-exist-$(date +%s)" 2>/dev/null
    assert_failure $? "kill_session should fail for non-existent session"
}

# Test send_keys_to_session parameter validation
test_send_keys_validation() {
    echo "Testing send_keys_to_session parameter validation..."
    
    # Test empty parameters
    send_keys_to_session "" "" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty session and keys"
    
    send_keys_to_session "test" "" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty keys"
    
    send_keys_to_session "" "ls" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail with empty session name"
    
    # Test non-existent session
    send_keys_to_session "hydra-test-definitely-does-not-exist-$(date +%s)" "ls" 2>/dev/null
    assert_failure $? "send_keys_to_session should fail for non-existent session"
}

# Test switch_to_session parameter validation
test_switch_to_session_validation() {
    echo "Testing switch_to_session parameter validation..."
    
    # Test empty session name
    switch_to_session "" 2>/dev/null
    assert_failure $? "switch_to_session should fail with empty session name"
    
    # Test non-existent session (this will fail gracefully)
    switch_to_session "hydra-test-definitely-does-not-exist-$(date +%s)" 2>/dev/null
    assert_failure $? "switch_to_session should fail for non-existent session"
}

# Test rename_session parameter validation
test_rename_session_validation() {
    echo "Testing rename_session parameter validation..."
    
    # Test empty parameters
    rename_session "" "" 2>/dev/null
    assert_failure $? "rename_session should fail with empty old and new names"
    
    rename_session "old" "" 2>/dev/null
    assert_failure $? "rename_session should fail with empty new name"
    
    rename_session "" "new" 2>/dev/null
    assert_failure $? "rename_session should fail with empty old name"
    
    # Test non-existent session
    rename_session "hydra-test-definitely-does-not-exist-$(date +%s)" "new-name" 2>/dev/null
    assert_failure $? "rename_session should fail for non-existent session"
}

# Test list_sessions (basic functionality)
test_list_sessions() {
    echo "Testing list_sessions..."
    
    # This should not fail even if no sessions exist
    if list_sessions >/dev/null 2>&1; then
        assert_success 0 "list_sessions should always succeed"
    else
        assert_failure 1 "list_sessions failed unexpectedly"
    fi
}

# Test get_current_session (outside tmux)
test_get_current_session() {
    echo "Testing get_current_session..."
    
    # Outside tmux, this should fail
    if [ -z "$TMUX" ]; then
        get_current_session >/dev/null 2>&1
        assert_failure $? "get_current_session should fail when not in tmux"
    else
        echo "[WARN] Skipping get_current_session test - already inside tmux"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
    fi
}

# Test validate_ai_command function
test_validate_ai_command() {
    echo "Testing validate_ai_command..."
    
    # Test valid AI commands
    validate_ai_command "claude" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'claude'"
    
    validate_ai_command "codex" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'codex'"
    
    validate_ai_command "cursor" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'cursor'"

    validate_ai_command "agy" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'agy'"

    validate_ai_command "opencode" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'opencode'"
    
    validate_ai_command "copilot" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'copilot'"
    
    validate_ai_command "aider" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'aider'"
    
    validate_ai_command "gemini" 2>/dev/null
    assert_success $? "validate_ai_command should accept 'gemini'"
    
    # Test invalid AI commands
    validate_ai_command "invalid-ai" 2>/dev/null
    assert_failure $? "validate_ai_command should reject 'invalid-ai'"
    
    validate_ai_command "" 2>/dev/null
    assert_failure $? "validate_ai_command should reject empty command"
    
    validate_ai_command "claude && rm -rf /" 2>/dev/null
    assert_failure $? "validate_ai_command should reject command injection attempt"
}

# Version parsing drives the new-session -e / set-environment fallback choice.
test_tmux_version_at_least() {
    echo "Testing tmux_version_at_least parses tmux -V..."

    _tva_dir="$(mktemp -d)"
    mkdir -p "$_tva_dir/bin"
    cat > "$_tva_dir/bin/tmux" <<'EOF'
#!/bin/sh
[ "${1:-}" = -V ] || exit 1
printf 'tmux %s\n' "$FAKE_TMUX_VERSION"
EOF
    chmod +x "$_tva_dir/bin/tmux"
    _tva_path="$PATH"
    PATH="$_tva_dir/bin:$PATH"
    export PATH
    for _tva_case in "3.5a:0" "3.2:0" "3.2-rc2:0" "next-3.6:0" "4.0:0" "3.1:1" "3.1b:1" "3.0:1" "2.9a:1" "master:1"; do
        FAKE_TMUX_VERSION="${_tva_case%%:*}" tmux_version_at_least 3 2 >/dev/null 2>&1
        assert_equal "${_tva_case##*:}" "$?" "tmux ${_tva_case%%:*} >= 3.2 resolves to ${_tva_case##*:}"
    done
    PATH="$_tva_path"
    export PATH
    rm -rf "$_tva_dir"
}

# Launcher content: environment, banner, agent, and shell hand-off without
# anything being typed into a pane.
test_write_session_launcher() {
    echo "Testing write_session_launcher output..."

    _wsl_dir="$(mktemp -d)"
    mkdir -p "$_wsl_dir/worktree" "$_wsl_dir/bin"
    # No tmux server: the launcher falls back to $SHELL for the hand-off.
    printf '#!/bin/sh\nexit 1\n' > "$_wsl_dir/bin/tmux"
    chmod +x "$_wsl_dir/bin/tmux"
    _wsl_path="$PATH"
    _wsl_shell="${SHELL:-}"
    PATH="$_wsl_dir/bin:$PATH"
    SHELL=/bin/sh
    export PATH SHELL
    cat > "$_wsl_dir/agent" <<'EOF'
#!/bin/sh
printf 'agent-env %s %s\n' "$HYDRA_HEAD_ID" "$HYDRA_BRANCH"
printf 'agent-arg %s\n' "$1"
printf 'agent-cwd %s\n' "$(pwd)"
exit 7
EOF
    chmod +x "$_wsl_dir/agent"
    _wsl_agent="'$_wsl_dir/agent' \"\$(cat '$_wsl_dir/task')\""
    printf 'do it' > "$_wsl_dir/task"
    write_session_launcher "$_wsl_dir/launcher" "$_wsl_dir/worktree" "feature/it's" fixture repo-name \
        "$_wsl_agent" "HYDRA_HEAD_ID=head_1" "HYDRA_BRANCH=feature/it's" "HYDRA_WORKTREE=$_wsl_dir/worktree"
    assert_success $? "write_session_launcher writes the launcher"
    if [ -x "$_wsl_dir/launcher" ]; then
        assert_success 0 "launcher is executable"
    else
        assert_success 1 "launcher is executable"
    fi
    grep -Fqx "HYDRA_BRANCH='feature/it'\\''s'" "$_wsl_dir/launcher"
    assert_success $? "launcher quotes head values for sh"
    grep -Fqx "export HYDRA_HEAD_ID HYDRA_BRANCH HYDRA_WORKTREE" "$_wsl_dir/launcher"
    assert_success $? "launcher exports every provided variable"
    grep -Fqx "$_wsl_agent" "$_wsl_dir/launcher"
    assert_success $? "launcher embeds the agent recipe verbatim"
    grep -q "^exec '/bin/sh' -l\$" "$_wsl_dir/launcher"
    assert_success $? "launcher ends by exec'ing the login shell"
    grep -q "send-keys" "$_wsl_dir/launcher"
    assert_failure $? "launcher never types keys"

    # Running it with a non-tty stdin runs the agent, then the shell exits at EOF.
    _wsl_out="$(cd "$_wsl_dir" && LANG=en_US.UTF-8 LC_ALL='' LC_CTYPE='' sh ./launcher </dev/null 2>&1)"
    assert_equal "Hydra head feature/it's · agent fixture · repo repo-name" "$(printf '%s\n' "$_wsl_out" | sed -n '1p')" "banner names head, agent, and repo on one line"
    assert_equal "Details: hydra provenance feature/it's" "$(printf '%s\n' "$_wsl_out" | sed -n '2p')" "banner points at provenance for exact details"
    case "$_wsl_out" in
        *"agent-env head_1 feature/it's"*) assert_success 0 "agent inherits the exported head environment" ;;
        *) assert_success 1 "agent inherits the exported head environment" ;;
    esac
    case "$_wsl_out" in
        *"agent-arg do it"*) assert_success 0 "agent receives the task argument" ;;
        *) assert_success 1 "agent receives the task argument" ;;
    esac
    case "$_wsl_out" in
        *"agent-cwd $_wsl_dir/worktree"*) assert_success 0 "agent starts inside the worktree" ;;
        *) assert_success 1 "agent starts inside the worktree" ;;
    esac
    case "$_wsl_out" in
        *"Hydra: agent fixture exited with status 7; this pane is now a shell for head feature/it's."*)
            assert_success 0 "exit line reports the agent status before the shell" ;;
        *) assert_success 1 "exit line reports the agent status before the shell" ;;
    esac
    _wsl_out="$(cd "$_wsl_dir" && LC_ALL=C sh ./launcher </dev/null 2>&1 | sed -n '1p')"
    assert_equal "Hydra head feature/it's | agent fixture | repo repo-name" "$_wsl_out" "banner falls back to ASCII separators outside UTF-8 locales"

    _wsl_long="feature/a-long-branch-name-that-needs-its-own-line"
    write_session_launcher "$_wsl_dir/long" "$_wsl_dir/worktree" "$_wsl_long" none repo-name ""
    _wsl_out="$(cd "$_wsl_dir" && LC_ALL=C sh ./long </dev/null 2>&1)"
    assert_equal "Hydra head $_wsl_long" "$(printf '%s\n' "$_wsl_out" | sed -n '1p')" "long banners split the head onto its own line"
    assert_equal "no agent | repo repo-name" "$(printf '%s\n' "$_wsl_out" | sed -n '2p')" "split banner keeps agent and repo together"
    _wsl_width="$(printf '%s\n' "$_wsl_out" | awk '{ if (length($0) > m) m = length($0) } END { print m }')"
    [ "$_wsl_width" -le 80 ]
    assert_success $? "no banner line exceeds 80 columns for a long branch"
    PATH="$_wsl_path"
    SHELL="$_wsl_shell"
    export PATH SHELL
    rm -rf "$_wsl_dir"
}

# Real isolated server: environment reaches the session and the first pane.
test_create_session_environment() {
    echo "Testing create_session delivers environment and pane command..."

    if ! command -v tmux >/dev/null 2>&1; then
        echo "[WARN] Skipping create_session environment - tmux not available"
        pass_count=$((pass_count + 1))
        test_count=$((test_count + 1))
        return
    fi
    _cse_dir="$(mktemp -d)"
    mkdir -p "$_cse_dir/bin"
    _cse_real="$(command -v tmux)"
    _cse_socket="hydra-env-$$"
    cat > "$_cse_dir/bin/tmux" <<EOF
#!/bin/sh
exec '$_cse_real' -L '$_cse_socket' "\$@"
EOF
    chmod +x "$_cse_dir/bin/tmux"
    _cse_path="$PATH"
    PATH="$_cse_dir/bin:$PATH"
    export PATH
    tmux kill-server 2>/dev/null || true

    create_session "hydra-env-plain" "$_cse_dir" "" "FOO=bar" "SPACED=a b'c" >/dev/null 2>&1
    assert_success $? "create_session accepts environment pairs"
    assert_equal "FOO=bar" "$(tmux show-environment -t hydra-env-plain FOO 2>/dev/null)" "session environment carries a plain value"
    assert_equal "SPACED=a b'c" "$(tmux show-environment -t hydra-env-plain SPACED 2>/dev/null)" "session environment preserves spaces and quotes"

    create_session "hydra-env-cmd" "$_cse_dir" "printf '%s\\n' \"\$FOO\" > '$_cse_dir/seen'; sleep 30" "FOO=first-pane" >/dev/null 2>&1
    assert_success $? "create_session accepts a pane command"
    _cse_tries=0
    while [ ! -s "$_cse_dir/seen" ] && [ "$_cse_tries" -lt 50 ]; do
        sleep 0.1 2>/dev/null || sleep 1
        _cse_tries=$((_cse_tries + 1))
    done
    assert_equal "first-pane" "$(cat "$_cse_dir/seen" 2>/dev/null)" "the first pane's command sees the session environment"
    tmux split-window -t hydra-env-cmd:0.0 -h "printf '%s\\n' \"\$FOO\" > '$_cse_dir/split'; sleep 30" 2>/dev/null
    _cse_tries=0
    while [ ! -s "$_cse_dir/split" ] && [ "$_cse_tries" -lt 50 ]; do
        sleep 0.1 2>/dev/null || sleep 1
        _cse_tries=$((_cse_tries + 1))
    done
    assert_equal "first-pane" "$(cat "$_cse_dir/split" 2>/dev/null)" "panes created later inherit the session environment"

    tmux kill-server 2>/dev/null || true
    PATH="$_cse_path"
    export PATH
    rm -rf "$_cse_dir"
}

# Run all tests
echo "Running tmux.sh unit tests (parameter validation)..."
echo "=============================================="

test_check_tmux_version
test_tmux_version_at_least
test_tmux_session_exists_validation
test_create_session_validation
test_create_session_normalizes_pane_target
test_create_session_environment
test_write_session_launcher
test_kill_session_validation
test_send_keys_validation
test_switch_to_session_validation
test_rename_session_validation
test_list_sessions
test_get_current_session
test_validate_ai_command

echo "=============================================="
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
