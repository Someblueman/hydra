#!/bin/sh
# Interactive spawn stays in the control centre unless --attach is explicit,
# and a branch whose durable head still exists is refused consistently by
# spawn and spawn --dry-run, with --resume as the bounded way back.

test_count=0
pass_count=0
fail_count=0

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO_ROOT/bin/hydra"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_LOCK_RETRIES=1
# These tests exercise the attach decision itself, so the guard must be unset
# here and re-added explicitly per case. TMUX is unset so the non-TTY cases
# behave the same whether or not the test runner is inside tmux.
unset HYDRA_NO_SWITCH TMUX

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

assert_contains() {
    test_count=$((test_count + 1))
    if printf '%s\n' "$1" | grep -Fq -- "$2"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $3"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $3"
        echo "  Expected to contain: '$2'"
        echo "  Actual: $1"
    fi
}

assert_not_contains() {
    test_count=$((test_count + 1))
    if printf '%s\n' "$1" | grep -Fq -- "$2"; then
        fail_count=$((fail_count + 1))
        echo "[FAIL] $3"
        echo "  Expected not to contain: '$2'"
        echo "  Actual: $1"
    else
        pass_count=$((pass_count + 1))
        echo "[PASS] $3"
    fi
}

test_base_dir="$(mktemp -d)"
tty_server="hydra-spawn-ux-$$"
created_sessions=""
# shellcheck disable=SC2329  # invoked by the EXIT trap
cleanup() {
    tmux -L "$tty_server" kill-server 2>/dev/null || true
    for cleanup_session in $created_sessions; do
        tmux kill-session -t "$cleanup_session" 2>/dev/null || true
    done
    rm -rf "$test_base_dir"
}
trap cleanup EXIT
mkdir -p "$test_base_dir/repo"
cd "$test_base_dir/repo" || exit 1
export HYDRA_HOME="$test_base_dir/.hydra"
git init >/dev/null 2>&1
git config user.name "Test User"
git config user.email "test@example.com"
echo "# Test" > README.md
git add README.md
git commit -m init >/dev/null 2>&1
"$HYDRA_BIN" init --no-agent --trust >/dev/null

# Run a hydra invocation inside a detached pane of an isolated tmux server so
# stdin and stdout are real terminals, then read the pane's scrollback.
# Usage: run_in_tty <name> <command string>
run_in_tty() {
    _rit_name="$1"
    _rit_cmd="$2"
    tmux -L "$tty_server" new-session -d -s "$_rit_name" -x 220 -y 60 \
        "cd '$test_base_dir/repo' && HYDRA_HOME='$HYDRA_HOME' HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_LOCK_RETRIES=1 $_rit_cmd; printf '__HYDRA_RC=%s__\\n' \$?; sleep 300" \
        || return 1
    _rit_i=0
    while [ "$_rit_i" -lt 120 ]; do
        tty_output="$(tmux -L "$tty_server" capture-pane -p -t "$_rit_name" -S - 2>/dev/null || true)"
        case "$tty_output" in *__HYDRA_RC=*__*) break ;; esac
        sleep 0.5
        _rit_i=$((_rit_i + 1))
    done
    tty_rc="${tty_output##*__HYDRA_RC=}"
    tty_rc="${tty_rc%%__*}"
    tmux -L "$tty_server" kill-session -t "$_rit_name" 2>/dev/null || true
    [ -n "$tty_rc" ]
}

human_path() {
    case "${HOME:-}" in
        ''|/) printf '%s\n' "$1" ;;
        *) case "$1" in "$HOME"/*) printf '~%s\n' "${1#"$HOME"}" ;; *) printf '%s\n' "$1" ;; esac ;;
    esac
}

echo "Running spawn UX tests..."
echo "================================"

echo "Testing non-TTY spawn keeps its documented message..."
output="$("$HYDRA_BIN" spawn feature/tests --no-agent 2>&1)"
assert_success $? "non-TTY spawn succeeds"
created_sessions="$created_sessions feature_tests"
assert_contains "$output" "Session 'feature_tests' created successfully (not switching - not in terminal)" "non-TTY spawn message is unchanged"

echo "Testing spawn refuses a live head before touching resources..."
output="$("$HYDRA_BIN" spawn feature/tests --no-agent 2>&1)"
assert_failure $? "spawn on a live head fails"
assert_contains "$output" "feature/tests is already running in session 'feature_tests'" "live head error names the session"
output="$("$HYDRA_BIN" spawn feature/tests --no-agent --dry-run 2>&1)"
assert_failure $? "dry-run on a live head fails the same way"
assert_contains "$output" "feature/tests is already running" "dry-run reports the live head"
output="$("$HYDRA_BIN" spawn feature/tests --no-agent --resume 2>&1)"
assert_failure $? "--resume never creates a second owner for a live head"
assert_equal "1" "$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^feature_tests$')" "exactly one session remains for the live head"

echo "Testing spawn after kill explains the durable head..."
"$HYDRA_BIN" kill feature/tests --force >/dev/null 2>&1
assert_success $? "kill removes the head"
expected_msg="feature/tests was removed earlier. Start it again with 'hydra resume feature/tests', or pick a new branch name."
output="$("$HYDRA_BIN" spawn feature/tests --no-agent 2>&1)"
assert_failure $? "spawn after kill fails"
assert_contains "$output" "$expected_msg" "spawn after kill prints the rewritten message"
assert_not_contains "$output" "durable head state already exists" "old wording is gone"
dry_output="$("$HYDRA_BIN" spawn feature/tests --no-agent --dry-run 2>&1)"
assert_failure $? "dry-run after kill fails instead of claiming success"
assert_contains "$dry_output" "$expected_msg" "dry-run prints the same message as the real spawn"
assert_not_contains "$dry_output" "Hydra spawn plan" "dry-run does not print a plan it would refuse"

echo "Testing spawn --resume takes the resume path..."
output="$("$HYDRA_BIN" spawn feature/tests --no-agent --dry-run --resume 2>&1)"
assert_success $? "dry-run --resume succeeds on a stopped head"
assert_contains "$output" "action: resume existing head" "dry-run --resume describes the resume plan"
assert_contains "$output" "Hydra spawn plan (no changes will be made)" "dry-run --resume is still a plan"
output="$("$HYDRA_BIN" spawn feature/tests --no-agent --resume 2>&1)"
assert_success $? "spawn --resume resumes a stopped head"
assert_contains "$output" "Resumed feature/tests in feature_tests" "resume path created a new instance"
assert_contains "$output" "Session 'feature_tests' resumed successfully (not switching - not in terminal)" "resume keeps the non-TTY contract"
assert_equal "1" "$(tmux list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^feature_tests$')" "resume created exactly one session"
"$HYDRA_BIN" kill feature/tests --force >/dev/null 2>&1
output="$("$HYDRA_BIN" spawn feature/fresh --no-agent --resume 2>&1)"
assert_success $? "--resume on an unknown branch spawns it fresh"
created_sessions="$created_sessions feature_fresh"
assert_contains "$output" "Session 'feature_fresh' created successfully" "fresh --resume spawn reports creation"
output="$("$HYDRA_BIN" spawn feature/fresh --no-agent --resume --prompt task 2>&1)"
assert_failure $? "--resume rejects spawn-only task options"
output="$("$HYDRA_BIN" spawn feature/none --headless --attach 2>&1)"
assert_failure $? "--attach is rejected with --headless"
assert_contains "$output" "--attach cannot be combined with --headless" "headless attach error is explicit"

if tmux -L "$tty_server" -V >/dev/null 2>&1; then
    echo "Testing interactive spawn stays in the current terminal by default..."
    if run_in_tty ux-default "'$HYDRA_BIN' spawn tty/default --no-agent"; then
        assert_equal "0" "$tty_rc" "interactive spawn succeeds"
        assert_not_contains "$tty_output" "Switching to session" "interactive spawn does not attach by default"
        assert_not_contains "$tty_output" "not in terminal" "interactive spawn saw a terminal"
        assert_contains "$tty_output" "Head 'tty/default' created" "context block names the head"
        assert_contains "$tty_output" "agent:    none (plain shell)" "context block names the agent"
        assert_contains "$tty_output" "worktree: $(human_path "$("$HYDRA_BIN" path tty/default)")" "context block prints the worktree location"
        assert_contains "$tty_output" "session:  tty_default (tmux)" "context block names the tmux session"
        assert_contains "$tty_output" "hydra tui" "context block offers the control centre"
        assert_contains "$tty_output" "hydra switch tty/default" "context block offers direct attach"
        assert_equal "tty_default attached=0" "$(tmux -L "$tty_server" list-sessions -F '#{session_name} attached=#{session_attached}' 2>/dev/null | grep '^tty_default ')" "head session exists and is not attached"
    else
        assert_success 1 "interactive spawn produced a result"
    fi

    echo "Testing --attach restores direct attachment..."
    # The detached test pane has no tmux client, so the switch itself cannot
    # complete here; the decision to attach and the fallback are what matter.
    if run_in_tty ux-attach "'$HYDRA_BIN' spawn tty/attach --no-agent --attach"; then
        assert_contains "$tty_output" "Switching to session 'tty_attach'..." "--attach switches to the new session"
        assert_contains "$tty_output" "Could not attach to session 'tty_attach'; the head is still running." "failed attach explains that the head survived"
        assert_contains "$tty_output" "hydra switch tty/attach" "failed attach still prints the way back"
        assert_equal "1" "$(tmux -L "$tty_server" list-sessions -F '#{session_name}' 2>/dev/null | grep -c '^tty_attach$')" "--attach created exactly one head session"
    else
        assert_success 1 "interactive spawn --attach produced a result"
    fi

    echo "Testing HYDRA_NO_SWITCH still wins in a terminal..."
    if run_in_tty ux-noswitch "HYDRA_NO_SWITCH=1 '$HYDRA_BIN' spawn tty/noswitch --no-agent --attach"; then
        assert_equal "0" "$tty_rc" "HYDRA_NO_SWITCH spawn succeeds"
        assert_contains "$tty_output" "Session 'tty_noswitch' created (HYDRA_NO_SWITCH set; not attaching)" "HYDRA_NO_SWITCH message is unchanged"
        assert_contains "$tty_output" "--attach ignored because HYDRA_NO_SWITCH is set" "explicit --attach is reported as ignored"
        assert_not_contains "$tty_output" "Switching to session" "HYDRA_NO_SWITCH prevents attachment"
    else
        assert_success 1 "HYDRA_NO_SWITCH spawn produced a result"
    fi

    echo "Testing interactive spawn --resume prints the context block..."
    if run_in_tty ux-resume "'$HYDRA_BIN' kill tty/default --force >/dev/null 2>&1; '$HYDRA_BIN' spawn tty/default --no-agent --resume"; then
        assert_equal "0" "$tty_rc" "interactive spawn --resume succeeds"
        assert_contains "$tty_output" "Head 'tty/default' resumed" "resumed head prints the context block"
        assert_not_contains "$tty_output" "Switching to session" "resumed head does not attach by default"
    else
        assert_success 1 "interactive spawn --resume produced a result"
    fi
else
    echo "[SKIP] tmux is unavailable; interactive terminal cases not run"
fi

echo "================================"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
fi
echo "Some tests failed!"
exit 1
