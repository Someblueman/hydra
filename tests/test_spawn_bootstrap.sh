#!/bin/sh
# Head session bootstrap: the HYDRA_* environment and the agent launch reach a
# real tmux session without being typed into the shell or its history.

test_count=0
pass_count=0
fail_count=0

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO_ROOT/bin/hydra"

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

if ! command -v tmux >/dev/null 2>&1; then
    echo "tmux unavailable; skipping spawn bootstrap tests"
    exit 0
fi

base="$(mktemp -d)"
base="$(cd "$base" && pwd -P)"
socket="hydra-bootstrap-$$"
real_tmux="$(command -v tmux)"
export HYDRA_HOME="$base/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
export BOOTSTRAP_LOG="$base/tmux.log" BOOTSTRAP_SOCKET="$socket" BOOTSTRAP_REAL_TMUX="$real_tmux"
export BOOTSTRAP_FAKE_VERSION="" FAKE_AGENT_OUT="$base/agent.out"

# shellcheck disable=SC2329,SC2317
cleanup() {
    "$real_tmux" -L "$socket" kill-server 2>/dev/null || true
    rm -rf "$base"
}
trap cleanup EXIT HUP INT TERM

# Every tmux call is logged and served by an isolated server. With
# BOOTSTRAP_FAKE_VERSION set, the wrapper reports that version and rejects
# `new-session -e`, which is how tmux 3.0/3.1 behave.
mkdir -p "$base/bin"
cat > "$base/bin/tmux" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$BOOTSTRAP_LOG"
if [ -n "${BOOTSTRAP_FAKE_VERSION:-}" ]; then
    if [ "${1:-}" = -V ]; then printf 'tmux %s\n' "$BOOTSTRAP_FAKE_VERSION"; exit 0; fi
    if [ "${1:-}" = new-session ]; then
        for arg in "$@"; do [ "$arg" != -e ] || { echo "usage: new-session [-AdDEPX] ..." >&2; exit 1; }; done
    fi
fi
exec "$BOOTSTRAP_REAL_TMUX" -L "$BOOTSTRAP_SOCKET" "$@"
EOF
chmod +x "$base/bin/tmux"
# The fake agent records what it saw, then keeps the pane busy as `sleep`.
cat > "$base/bin/fakeagent" <<'EOF'
#!/bin/sh
{
    printf 'head=%s\ninstance=%s\nbranch=%s\ntask_file=%s\n' \
        "$HYDRA_HEAD_ID" "$HYDRA_INSTANCE_ID" "$HYDRA_BRANCH" "$HYDRA_TASK_FILE"
    printf 'argc=%s\narg1=%s\ncwd=%s\n' "$#" "${1:-}" "$(pwd)"
} > "$FAKE_AGENT_OUT"
exec sleep 4
EOF
chmod +x "$base/bin/fakeagent"
PATH="$base/bin:$PATH"
export PATH

mkdir -p "$base/repo"
cd "$base/repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
git commit -q --allow-empty -m init
"$HYDRA_BIN" init --no-agent --trust >/dev/null || exit 1
"$HYDRA_BIN" agent init fake --executable "$base/bin/fakeagent" --prompt-mode task-file >/dev/null || exit 1

wait_for_file() {
    _wff_tries=0
    while [ ! -s "$1" ] && [ "$_wff_tries" -lt 60 ]; do
        sleep 0.1 2>/dev/null || sleep 1
        _wff_tries=$((_wff_tries + 1))
    done
    [ -s "$1" ]
}

head_dir_for() {
    for _hdf_dir in "$HYDRA_HOME"/state/v2/projects/*/heads/head_*; do
        [ "$(sed -n '1p' "$_hdf_dir/branch" 2>/dev/null)" = "$1" ] || continue
        printf '%s\n' "$_hdf_dir"
        return 0
    done
    return 1
}

is_shell_command() {
    case "$1" in sh|bash|dash|zsh|fish|-sh|-bash|-dash|-zsh|-fish) return 0 ;; esac
    return 1
}

pane_value() {
    # Ask the interactive shell in session:0.0 for one variable, via a file.
    rm -f "$base/pane-value"
    tmux send-keys -t "$1:0.0" "printf '%s\\n' \"\$$2\" > '$base/pane-value'" Enter
    wait_for_file "$base/pane-value" && sed -n '1p' "$base/pane-value"
}

echo "Running spawn bootstrap tests..."
echo "======================================"

# --- shell-only head -------------------------------------------------------
"$HYDRA_BIN" spawn plain --no-agent >/dev/null 2>&1
assert_success $? "shell-only head spawns"
head_dir="$(head_dir_for plain)"
head_id="$(sed -n '1p' "$head_dir/head-id")"
instance_id="$(sed -n '1p' "$head_dir/current-instance")"
worktree="$(sed -n '1p' "$head_dir/worktree")"
assert_equal "HYDRA_HEAD_ID=$head_id" "$(tmux show-environment -t plain HYDRA_HEAD_ID 2>/dev/null)" "session environment carries the head id"
assert_equal "HYDRA_INSTANCE_ID=$instance_id" "$(tmux show-environment -t plain HYDRA_INSTANCE_ID 2>/dev/null)" "session environment carries the instance id"
assert_equal "HYDRA_BRANCH=plain" "$(tmux show-environment -t plain HYDRA_BRANCH 2>/dev/null)" "session environment carries the branch"
assert_equal "HYDRA_WORKTREE=$worktree" "$(tmux show-environment -t plain HYDRA_WORKTREE 2>/dev/null)" "session environment carries the worktree"
assert_equal "HYDRA_STATE_DIR=$head_dir" "$(tmux show-environment -t plain HYDRA_STATE_DIR 2>/dev/null)" "session environment carries the state dir"
assert_equal "HYDRA_TASK_FILE=$head_dir/task" "$(tmux show-environment -t plain HYDRA_TASK_FILE 2>/dev/null)" "session environment carries the task file"
case "$(tmux show-environment -t plain HYDRA_PROJECT_ID 2>/dev/null)" in
    HYDRA_PROJECT_ID=project_*) assert_success 0 "session environment carries the project id" ;;
    *) assert_success 1 "session environment carries the project id" ;;
esac
sleep 1
is_shell_command "$(tmux display-message -p -t plain:0.0 '#{pane_current_command}')"
assert_success $? "shell-only head opens an interactive shell in session:0.0"
assert_equal "$head_id" "$(pane_value plain HYDRA_HEAD_ID)" "the interactive shell inherits the head id"
assert_equal "$worktree" "$(pane_value plain PWD)" "the interactive shell starts in the worktree"
screen="$(tmux capture-pane -p -t plain:0.0)"
case "$screen" in
    *"Hydra head plain"*"no agent"*"repo repo"*) assert_success 0 "banner shows head, agent, and repository" ;;
    *) assert_success 1 "banner shows head, agent, and repository" ;;
esac
case "$screen" in
    *"Details: hydra provenance plain"*) assert_success 0 "banner points at provenance for exact details" ;;
    *) assert_success 1 "banner points at provenance for exact details" ;;
esac
case "$screen" in
    *"export HYDRA_"*|*"$head_id"*|*"$head_dir"*) assert_success 1 "transcript shows no bootstrap exports, ids, or state paths" ;;
    *) assert_success 0 "transcript shows no bootstrap exports, ids, or state paths" ;;
esac
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'export HYDRA_'
assert_failure $? "no HYDRA_ export is typed with send-keys"
grep -q '^new-session .* -e HYDRA_HEAD_ID=' "$BOOTSTRAP_LOG"
assert_success $? "tmux 3.2+ receives the environment at new-session"
if [ -x "$head_dir/instances/$instance_id/launcher" ]; then
    assert_success 0 "instance records its launcher"
else
    assert_success 1 "instance records its launcher"
fi
"$HYDRA_BIN" provenance plain | grep -q "^  launcher: $head_dir/instances/$instance_id/launcher\$"
assert_success $? "provenance lists the launcher path"
"$HYDRA_BIN" provenance plain | grep -q "^  worktree: $worktree\$"
assert_success $? "provenance lists the exact worktree"

# --- agent head --------------------------------------------------------------
: > "$BOOTSTRAP_LOG"
HYDRA_SKIP_AI='' "$HYDRA_BIN" spawn agent --profile fake --prompt "Do the thing" >/dev/null 2>&1
assert_success $? "agent head spawns"
agent_dir="$(head_dir_for agent)"
agent_head="$(sed -n '1p' "$agent_dir/head-id")"
agent_instance="$(sed -n '1p' "$agent_dir/current-instance")"
agent_worktree="$(sed -n '1p' "$agent_dir/worktree")"
wait_for_file "$FAKE_AGENT_OUT"
assert_success $? "agent starts without a typed launch"
assert_equal "head=$agent_head" "$(sed -n '1p' "$FAKE_AGENT_OUT")" "agent sees the head id"
assert_equal "instance=$agent_instance" "$(sed -n '2p' "$FAKE_AGENT_OUT")" "agent sees the instance id"
assert_equal "branch=agent" "$(sed -n '3p' "$FAKE_AGENT_OUT")" "agent sees the branch"
assert_equal "task_file=$agent_dir/task" "$(sed -n '4p' "$FAKE_AGENT_OUT")" "agent sees the task file"
assert_equal "argc=1" "$(sed -n '5p' "$FAKE_AGENT_OUT")" "task is delivered as one argument"
assert_equal "arg1=Do the thing" "$(sed -n '6p' "$FAKE_AGENT_OUT")" "task bytes reach the agent"
assert_equal "cwd=$agent_worktree" "$(sed -n '7p' "$FAKE_AGENT_OUT")" "agent starts inside the worktree"
assert_equal sleep "$(tmux display-message -p -t agent:0.0 '#{pane_current_command}')" "the agent is the pane's foreground process"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q fakeagent
assert_failure $? "the agent command is not typed with send-keys"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'export HYDRA_'
assert_failure $? "agent heads type no HYDRA_ export either"
assert_equal "$base/bin/fakeagent" "$(sed -n '1p' "$agent_dir/instances/$agent_instance/profile-executable")" "provenance records the resolved executable"
grep -Fq "'$base/bin/fakeagent'" "$agent_dir/instances/$agent_instance/launcher"
assert_success $? "launcher runs the executable provenance reported, by absolute path"
tmux split-window -t agent:0.0 -h -c "$agent_worktree" 2>/dev/null
sleep 1
rm -f "$base/pane-value"
tmux send-keys -t agent:0.1 "printf '%s\\n' \"\$HYDRA_HEAD_ID\" > '$base/pane-value'" Enter
wait_for_file "$base/pane-value"
assert_equal "$agent_head" "$(sed -n '1p' "$base/pane-value")" "panes created after spawn inherit the head environment"
sleep 4
is_shell_command "$(tmux display-message -p -t agent:0.0 '#{pane_current_command}')"
assert_success $? "session:0.0 returns to a shell after the agent exits"
case "$(tmux capture-pane -p -t agent:0.0)" in
    *"agent fake exited with status 0"*) assert_success 0 "the pane reports the agent exit before the prompt" ;;
    *) assert_success 1 "the pane reports the agent exit before the prompt" ;;
esac
assert_equal "$agent_head" "$(pane_value agent HYDRA_HEAD_ID)" "the shell after the agent keeps the head environment"

# --- tmux without new-session -e (3.0/3.1) -----------------------------------
: > "$BOOTSTRAP_LOG"
BOOTSTRAP_FAKE_VERSION=3.1 "$HYDRA_BIN" spawn oldtmux --no-agent >/dev/null 2>&1
assert_success $? "head spawns when tmux lacks new-session -e"
old_dir="$(head_dir_for oldtmux)"
old_head="$(sed -n '1p' "$old_dir/head-id")"
grep -q '^new-session .* -e ' "$BOOTSTRAP_LOG"
assert_failure $? "older tmux never receives -e"
grep -q "^set-environment -t oldtmux HYDRA_HEAD_ID $old_head\$" "$BOOTSTRAP_LOG"
assert_success $? "older tmux receives set-environment for later panes"
assert_equal "HYDRA_HEAD_ID=$old_head" "$(tmux show-environment -t oldtmux HYDRA_HEAD_ID 2>/dev/null)" "older tmux session environment carries the head id"
sleep 1
assert_equal "$old_head" "$(pane_value oldtmux HYDRA_HEAD_ID)" "older tmux first pane still inherits the head id from the launcher"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'export HYDRA_'
assert_failure $? "older tmux types no HYDRA_ export"

# --- resume ------------------------------------------------------------------
"$HYDRA_BIN" kill plain --force >/dev/null 2>&1
assert_success $? "shell-only head tears down"
: > "$BOOTSTRAP_LOG"
"$HYDRA_BIN" resume plain >/dev/null 2>&1
assert_success $? "shell-only head resumes"
resumed_instance="$(sed -n '1p' "$head_dir/current-instance")"
if [ "$resumed_instance" != "$instance_id" ]; then
    assert_success 0 "resume creates a new instance"
else
    assert_success 1 "resume creates a new instance"
fi
assert_equal "HYDRA_INSTANCE_ID=$resumed_instance" "$(tmux show-environment -t plain HYDRA_INSTANCE_ID 2>/dev/null)" "resumed session environment carries the new instance id"
sleep 1
assert_equal "$resumed_instance" "$(pane_value plain HYDRA_INSTANCE_ID)" "resumed shell inherits the new instance id"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'export HYDRA_'
assert_failure $? "resume types no HYDRA_ export"
if [ -x "$head_dir/instances/$resumed_instance/launcher" ]; then
    assert_success 0 "resumed instance records its own launcher"
else
    assert_success 1 "resumed instance records its own launcher"
fi

# --- typed YAML startup commands keep their order ----------------------------
mkdir -p .hydra
printf 'startup:\n  - echo from-startup\n' >> .hydra/config.yml
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "changed repository config is re-trusted"
: > "$BOOTSTRAP_LOG"
rm -f "$FAKE_AGENT_OUT"
HYDRA_SKIP_AI='' "$HYDRA_BIN" spawn typed --profile fake >/dev/null 2>&1
assert_success $? "agent head spawns with YAML startup commands"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'from-startup'
assert_success $? "YAML startup commands are still typed into the main pane"
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q fakeagent
assert_success $? "an agent that must follow typed commands is typed after them"
case "$(grep '^send-keys' "$BOOTSTRAP_LOG" | grep 'from-startup\|fakeagent' | sed -n '1p')" in
    *from-startup*) assert_success 0 "startup commands precede the typed agent launch" ;;
    *) assert_success 1 "startup commands precede the typed agent launch" ;;
esac
grep '^send-keys' "$BOOTSTRAP_LOG" | grep -q 'export HYDRA_'
assert_failure $? "startup mode still types no HYDRA_ export"
wait_for_file "$FAKE_AGENT_OUT"
assert_success $? "typed agent launch still starts the agent"
assert_equal "head=$(sed -n '1p' "$(head_dir_for typed)/head-id")" "$(sed -n '1p' "$FAKE_AGENT_OUT")" "typed agent launch still sees the head environment"

"$HYDRA_BIN" kill --all --force >/dev/null 2>&1

echo "======================================"
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
