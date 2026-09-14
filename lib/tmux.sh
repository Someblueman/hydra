#!/bin/sh
# tmux helper functions for Hydra
# POSIX-compliant shell script

# Validate AI command against allowlist
# Usage: validate_ai_command <command>
# Returns: 0 if valid, 1 if invalid
validate_ai_command() {
    command="$1"
    
    if [ -z "$command" ]; then
        echo "Error: AI command cannot be empty" >&2
        echo "Next: pass --no-agent for a shell-only head, or set HYDRA_AI_COMMAND" >&2
        return 1
    fi
    
    case "$command" in
        "claude"|"codex"|"cursor"|"agy"|"opencode"|"copilot"|"aider"|"gemini")
            return 0
            ;;
        *)
            echo "Error: Unsupported AI command: $command" >&2
            echo "Supported: claude, codex, cursor, agy, opencode, copilot, aider, gemini" >&2
            echo "Next: pass --no-agent for a shell-only head, or pass --ai with a supported tool" >&2
            return 1
            ;;
    esac
}

# Check if tmux is available and meets version requirement
# Usage: check_tmux_version
# Returns: 0 if tmux >= 3.0, 1 otherwise
check_tmux_version() {
    if ! command -v tmux >/dev/null 2>&1; then
        echo "Error: tmux is not installed or not on PATH" >&2
        echo "Next: install tmux 3.0 or newer (apt/brew), then run hydra doctor" >&2
        return 1
    fi
    
    # Get tmux version
    version="$(tmux -V | cut -d' ' -f2)"
    major="$(echo "$version" | cut -d'.' -f1)"
    
    # Convert to number for comparison (handle versions like "3.2a")
    major_num="$(echo "$major" | sed 's/[^0-9]//g')"
    
    if [ "$major_num" -lt 3 ]; then
        echo "Error: tmux version $version is too old (need >= 3.0)" >&2
        echo "Next: upgrade tmux to 3.0 or newer, then run hydra doctor" >&2
        return 1
    fi

    return 0
}

# Compare the running tmux version against a minimum.
# Usage: tmux_version_at_least <major> <minor>
# Returns: 0 when tmux -V reports at least major.minor; 1 when older or when
# the version cannot be parsed (callers then take the conservative path).
tmux_version_at_least() {
    _tva_parsed="$(tmux -V 2>/dev/null | sed -n '1s/^[^0-9]*\([0-9][0-9]*\)\.\([0-9][0-9]*\).*$/\1 \2/p')"
    [ -n "$_tva_parsed" ] || return 1
    _tva_major="${_tva_parsed% *}"
    _tva_minor="${_tva_parsed#* }"
    [ "$_tva_major" -gt "$1" ] && return 0
    [ "$_tva_major" -eq "$1" ] && [ "$_tva_minor" -ge "$2" ]
}

# Quote one string as a single POSIX sh word.
# Usage: tmux_shell_quote <string>
tmux_shell_quote() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

# Check if a tmux session exists
# Usage: tmux_session_exists <session_name>
# Returns: 0 if exists, 1 if not
tmux_session_exists() {
    session="$1"
    if [ -z "$session" ]; then
        return 1
    fi

    # Always live-probe. A loaded snapshot is for batch list/status/TUI
    # observation only; mutation paths (spawn, send-keys, kill) must not
    # miss a session created after the snapshot was taken.
    tmux has-session -t "$session" 2>/dev/null
}

# Snapshot lookup for observation loops that just called tmux_load_snapshot.
# Usage: tmux_snapshot_has_session <session_name>
# Returns: 0 if the snapshot lists the session, else live tmux_session_exists
tmux_snapshot_has_session() {
    session="$1"
    if [ -z "$session" ]; then
        return 1
    fi
    if [ -n "${_TMUX_SNAPSHOT_LOADED:-}" ]; then
        printf '%s\n' "$_TMUX_SNAPSHOT_SESSIONS" | grep -Fqx "$session"
        return $?
    fi
    tmux_session_exists "$session"
}

# Load a one-shot snapshot of sessions and panes for batch observation.
# Usage: tmux_load_snapshot
# Sets: _TMUX_SNAPSHOT_LOADED, _TMUX_SNAPSHOT_SESSIONS, _TMUX_SNAPSHOT_PANES
# Pane lines: session<TAB>window_index<TAB>pane_index<TAB>window_activity<TAB>pane_current_command<TAB>pane_dead
tmux_load_snapshot() {
    _TMUX_SNAPSHOT_SESSIONS="$(tmux list-sessions -F '#{session_name}' 2>/dev/null || true)"
    _TMUX_SNAPSHOT_PANES="$(tmux list-panes -a -F '#{session_name}	#{window_index}	#{pane_index}	#{window_activity}	#{pane_current_command}	#{pane_dead}' 2>/dev/null || true)"
    _TMUX_SNAPSHOT_LOADED=1
}

# Clear a previously loaded tmux snapshot (callers fall back to live probes).
# Usage: tmux_clear_snapshot
tmux_clear_snapshot() {
    _TMUX_SNAPSHOT_LOADED=""
    _TMUX_SNAPSHOT_SESSIONS=""
    _TMUX_SNAPSHOT_PANES=""
}

# Latest window_activity timestamp for a session from the snapshot.
# Usage: tmux_snapshot_window_activity <session>
# Returns: unix timestamp or 0
tmux_snapshot_window_activity() {
    _sa_session="$1"
    if [ -z "${_TMUX_SNAPSHOT_PANES:-}" ]; then
        printf '%s' "0"
        return 0
    fi
    printf '%s\n' "$_TMUX_SNAPSHOT_PANES" | awk -F '	' -v s="$_sa_session" '
        $1 == s {
            act = $4 + 0
            if (act > max) max = act
        }
        END { print max + 0 }
    '
}

# True if every pane in the session is marked pane_dead.
# Usage: tmux_snapshot_session_dead <session>
# Returns: 0 if all panes dead, 1 otherwise
tmux_snapshot_session_dead() {
    _sd_session="$1"
    if [ -z "${_TMUX_SNAPSHOT_PANES:-}" ]; then
        return 1
    fi
    printf '%s\n' "$_TMUX_SNAPSHOT_PANES" | awk -F '	' -v s="$_sd_session" '
        $1 == s { seen = 1; if ($6 != "1") live = 1 }
        END { if (seen && !live) exit 0; exit 1 }
    '
}

# WARNING: SECURITY SENSITIVE
# This function executes arbitrary commands in tmux sessions.
# Only call with trusted, validated input - never with user input.
# Commands are executed with the user's shell privileges.
#
# Send keys to an explicit tmux target (session:window.pane).
# Usage: send_keys_to_target <session_name> <pane_target> <keys>
# Returns: 0 on success, 1 on failure
send_keys_to_target() {
    session="$1"
    pane_target="$2"
    keys="$3"

    if [ -z "$session" ] || [ -z "$pane_target" ] || [ -z "$keys" ]; then
        echo "Error: Session name, pane target, and keys are required" >&2
        return 1
    fi

    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi

    tmux send-keys -t "$pane_target" "$keys" Enter || return 1

    return 0
}

# Send keys to the primary pane (session:0.0), never "whatever is active".
# Usage: send_keys_to_session <session_name> <keys>
# Returns: 0 on success, 1 on failure
send_keys_to_session() {
    session="$1"
    keys="$2"

    if [ -z "$session" ] || [ -z "$keys" ]; then
        echo "Error: Session name and keys are required" >&2
        return 1
    fi

    send_keys_to_target "$session" "${session}:0.0" "$keys"
}

# Resolve a shell pane for broadcast. Prefer a non-agent pane.
# Usage: find_broadcast_pane <session> [ai_tool]
# Prints session:window.pane on stdout. Returns 1 if only the agent pane exists.
find_broadcast_pane() {
    _fb_session="$1"
    _fb_ai="${2:-}"

    if [ -z "$_fb_session" ]; then
        return 1
    fi

    _fb_panes="$(tmux list-panes -t "$_fb_session" -F '#{window_index}.#{pane_index} #{pane_current_command}' 2>/dev/null || true)"
    if [ -z "$_fb_panes" ]; then
        return 1
    fi

    _fb_shell=""
    _fb_has_live_agent=0
    _fb_map_agent=0
    if [ -n "$_fb_ai" ] && [ "$_fb_ai" != "-" ]; then
        _fb_map_agent=1
    fi

    while IFS=' ' read -r _fb_idx _fb_cmd; do
        [ -n "$_fb_idx" ] || continue
        _fb_is_shell=0
        case "$_fb_cmd" in
            sh|bash|dash|zsh|fish|-sh|-bash|-dash|-zsh|-fish)
                _fb_is_shell=1
                ;;
        esac
        _fb_is_agent=0
        case "$_fb_cmd" in
            claude|codex|cursor|cursor-agent|agy|opencode|copilot|aider|gemini)
                _fb_is_agent=1
                ;;
        esac
        # Skip :0.0 only while a live agent occupies it. After the agent
        # exits back to a shell, that pane is a valid broadcast target even
        # if the map still records an AI tool. Unknown :0.0 commands still
        # honor stored AI so we do not type into a renamed agent process.
        if [ "$_fb_idx" = "0.0" ]; then
            if [ "$_fb_is_agent" -eq 1 ]; then
                _fb_has_live_agent=1
                continue
            fi
            if [ "$_fb_is_shell" -eq 0 ] && [ "$_fb_map_agent" -eq 1 ]; then
                _fb_has_live_agent=1
                continue
            fi
        fi
        if [ "$_fb_is_shell" -eq 1 ]; then
            _fb_shell="${_fb_session}:${_fb_idx}"
            break
        fi
    done <<EOF
$_fb_panes
EOF

    if [ -n "$_fb_shell" ]; then
        printf '%s\n' "$_fb_shell"
        return 0
    fi

    if [ "$_fb_has_live_agent" -eq 1 ]; then
        return 1
    fi

    printf '%s\n' "${_fb_session}:0.0"
    return 0
}

# Create a new tmux session
# Usage: create_session <session_name> <start_directory> [pane_command] [NAME=value ...]
# pane_command, when non-empty, runs in the first pane instead of the login
# shell. NAME=value pairs become the session environment before that pane
# starts, so every later window and pane inherits them: tmux 3.2+ receives
# them through `new-session -e`; tmux 3.0/3.1 receives `set-environment`
# immediately after creation, and the pane command must export them itself.
# Returns: 0 on success, 1 on failure
create_session() {
    session="$1"
    start_dir="$2"
    _cs_command="${3:-}"
    if [ $# -gt 3 ]; then
        shift 3
    else
        set --
    fi

    if [ -z "$session" ] || [ -z "$start_dir" ]; then
        echo "Error: Session name and directory are required" >&2
        return 1
    fi

    if ! [ -d "$start_dir" ]; then
        echo "Error: Directory does not exist: $start_dir" >&2
        return 1
    fi

    if tmux_session_exists "$session"; then
        echo "Error: Session already exists: $session" >&2
        return 1
    fi

    _cs_env_flags=0
    if [ $# -gt 0 ] && tmux_version_at_least 3 2; then
        _cs_env_flags=1
        # Rotate NAME=value into "-e NAME=value" argument pairs.
        for _cs_pair in "$@"; do
            set -- "$@" -e "$_cs_pair"
            shift
        done
    fi
    # Create detached session with specified working directory
    if [ "$_cs_env_flags" -eq 1 ]; then
        if [ -n "$_cs_command" ]; then
            tmux new-session -d -s "$session" -c "$start_dir" "$@" "$_cs_command" || return 1
        else
            tmux new-session -d -s "$session" -c "$start_dir" "$@" || return 1
        fi
    else
        if [ -n "$_cs_command" ]; then
            tmux new-session -d -s "$session" -c "$start_dir" "$_cs_command" || return 1
        else
            tmux new-session -d -s "$session" -c "$start_dir" || return 1
        fi
        for _cs_pair in "$@"; do
            tmux set-environment -t "$session" "${_cs_pair%%=*}" "${_cs_pair#*=}" || {
                tmux kill-session -t "$session" 2>/dev/null || true
                return 1
            }
        done
    fi

    # Hydra's public pane-target contract is session:0.0. Normalize indexes even
    # when the user's global tmux configuration starts windows or panes at 1.
    _created_window="$(tmux display-message -p -t "$session" '#{window_index}' 2>/dev/null)" || {
        tmux kill-session -t "$session" 2>/dev/null || true
        return 1
    }
    tmux set-option -t "$session" base-index 0 >/dev/null || {
        tmux kill-session -t "$session" 2>/dev/null || true
        return 1
    }
    tmux set-window-option -t "$session" pane-base-index 0 >/dev/null || {
        tmux kill-session -t "$session" 2>/dev/null || true
        return 1
    }
    if [ "$_created_window" != "0" ]; then
        tmux move-window -s "$session:$_created_window" -t "$session:0" || {
            tmux kill-session -t "$session" 2>/dev/null || true
            return 1
        }
    fi
    tmux_clear_snapshot
    return 0
}

# Write the POSIX sh launcher that a head session's first pane runs.
# Usage: write_session_launcher <file> <worktree> <branch> <agent_label> <repo_name> <agent_command> [NAME=value ...]
# The launcher exports the head environment, prints a short context banner,
# runs the agent command (if any) as the pane's foreground process group, and
# finally execs the user's interactive shell so the pane stays usable after the
# agent exits. Nothing is typed into the shell or recorded in its history;
# exact identities and paths stay in `hydra provenance` and the launcher file.
# Returns: 0 on success, 1 on failure
write_session_launcher() {
    _wsl_file="$1"
    _wsl_worktree="$2"
    _wsl_branch="$3"
    _wsl_agent_label="$4"
    _wsl_repo_name="$5"
    _wsl_agent_command="$6"
    if [ $# -gt 6 ]; then
        shift 6
    else
        set --
    fi
    if [ -z "$_wsl_file" ] || [ -z "$_wsl_worktree" ] || [ -z "$_wsl_branch" ]; then
        echo "Error: launcher file, worktree, and branch are required" >&2
        return 1
    fi

    # Reproduce what tmux would have started in this pane.
    _wsl_shell="$(tmux show-options -gv default-shell 2>/dev/null | sed -n '1p')"
    if [ -z "$_wsl_shell" ] || [ ! -x "$_wsl_shell" ]; then
        _wsl_shell="${SHELL:-/bin/sh}"
        [ -x "$_wsl_shell" ] || _wsl_shell=/bin/sh
    fi
    _wsl_default_command="$(tmux show-options -gqv default-command 2>/dev/null | sed -n '1p')"

    if [ -n "$_wsl_agent_label" ] && [ "$_wsl_agent_label" != none ]; then
        _wsl_label="agent $_wsl_agent_label"
    else
        _wsl_label="no agent"
    fi
    _wsl_q_worktree="$(tmux_shell_quote "$_wsl_worktree")"
    _wsl_q_branch="$(tmux_shell_quote "$_wsl_branch")"
    _wsl_q_head="$(tmux_shell_quote "Hydra head $_wsl_branch")"
    _wsl_q_label="$(tmux_shell_quote "$_wsl_label")"
    _wsl_q_repo="$(tmux_shell_quote "repo $_wsl_repo_name")"
    _wsl_q_details="$(tmux_shell_quote "Details: hydra provenance $_wsl_branch")"
    _wsl_q_shell="$(tmux_shell_quote "$_wsl_shell")"
    _wsl_banner="Hydra head $_wsl_branch | $_wsl_label | repo $_wsl_repo_name"

    _wsl_names=""
    {
        printf '#!/bin/sh\n'
        printf '# Hydra head launcher for %s. Identity and paths: hydra provenance %s\n' \
            "$_wsl_branch" "$_wsl_branch"
        for _wsl_pair in "$@"; do
            _wsl_name="${_wsl_pair%%=*}"
            printf '%s=%s\n' "$_wsl_name" "$(tmux_shell_quote "${_wsl_pair#*=}")"
            _wsl_names="$_wsl_names $_wsl_name"
        done
        [ -z "$_wsl_names" ] || printf 'export%s\n' "$_wsl_names"
        cat <<EOF
cd $_wsl_q_worktree 2>/dev/null || printf 'Hydra: worktree %s is unavailable\\n' $_wsl_q_worktree >&2
case "\${LC_ALL:-\${LC_CTYPE:-\${LANG:-}}}" in
    *[Uu][Tt][Ff]-8*|*[Uu][Tt][Ff]8*) _hydra_sep=' · ' ;;
    *) _hydra_sep=' | ' ;;
esac
EOF
        # Keep the banner on one line at 80 columns; split it otherwise.
        if [ "${#_wsl_banner}" -le 78 ]; then
            cat <<EOF
printf '%s%s%s%s%s\\n' $_wsl_q_head "\$_hydra_sep" $_wsl_q_label "\$_hydra_sep" $_wsl_q_repo
EOF
        else
            cat <<EOF
printf '%s\\n' $_wsl_q_head
printf '%s%s%s\\n' $_wsl_q_label "\$_hydra_sep" $_wsl_q_repo
EOF
        fi
        cat <<EOF
printf '%s\\n' $_wsl_q_details
unset _hydra_sep
EOF
        if [ -n "$_wsl_agent_command" ]; then
            cat <<EOF
# The agent owns the terminal: Ctrl-C reaches only the agent, Ctrl-Z is
# ignored, and this pane returns to a shell when the agent exits.
trap '' TSTP
trap : INT
set -m 2>/dev/null || :
$_wsl_agent_command
_hydra_status=\$?
trap - INT TSTP
printf 'Hydra: %s exited with status %s; this pane is now a shell for head %s.\\n' $_wsl_q_label "\$_hydra_status" $_wsl_q_branch
unset _hydra_status
EOF
        fi
        if [ -n "$_wsl_default_command" ]; then
            printf 'exec %s -c %s\n' "$_wsl_q_shell" "$(tmux_shell_quote "$_wsl_default_command")"
        else
            printf 'exec %s -l\n' "$_wsl_q_shell"
        fi
    } > "$_wsl_file" || return 1
    chmod 700 "$_wsl_file" || return 1
    return 0
}

# Pane command that runs a launcher written by write_session_launcher.
# Usage: session_launcher_command <file>
session_launcher_command() {
    printf 'exec /bin/sh %s\n' "$(tmux_shell_quote "$1")"
}

# Create a head session whose first pane runs the generated launcher and
# whose session environment carries the documented HYDRA_* identity.
# Usage: create_head_session <session> <launcher_file> <agent_command> \
#            <project_id> <head_id> <instance_id> <branch> <worktree> <head_dir> <profile> <repo_root>
# Returns: 0 on success, 1 on failure (no session is left behind)
create_head_session() {
    _chsn_session="$1"
    _chsn_launcher="$2"
    _chsn_agent="$3"
    _chsn_project="$4"
    _chsn_head="$5"
    _chsn_instance="$6"
    _chsn_branch="$7"
    _chsn_worktree="$8"
    _chsn_head_dir="$9"
    _chsn_profile="${10}"
    _chsn_repo_root="${11}"
    set -- "HYDRA_PROJECT_ID=$_chsn_project" \
        "HYDRA_HEAD_ID=$_chsn_head" \
        "HYDRA_INSTANCE_ID=$_chsn_instance" \
        "HYDRA_BRANCH=$_chsn_branch" \
        "HYDRA_WORKTREE=$_chsn_worktree" \
        "HYDRA_STATE_DIR=$_chsn_head_dir" \
        "HYDRA_TASK_FILE=$_chsn_head_dir/task"
    if ! write_session_launcher "$_chsn_launcher" "$_chsn_worktree" "$_chsn_branch" "$_chsn_profile" \
        "$(basename "$_chsn_repo_root")" "$_chsn_agent" "$@"; then
        echo "Error: Failed to write head launcher $_chsn_launcher" >&2
        return 1
    fi
    create_session "$_chsn_session" "$_chsn_worktree" "$(session_launcher_command "$_chsn_launcher")" "$@"
}

# Kill a tmux session
# Usage: kill_session <session_name>
# Returns: 0 on success, 1 on failure
kill_session() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi
    
    tmux kill-session -t "$session" || return 1
    tmux_clear_snapshot
    return 0
}

# List all tmux sessions
# Usage: list_sessions
# Returns: Session names on stdout
list_sessions() {
    tmux list-sessions -F '#{session_name}' 2>/dev/null || true
}

# Switch to a tmux session
# Usage: switch_to_session <session_name>
# Returns: 0 on success, 1 on failure
switch_to_session() {
    session="$1"
    
    if [ -z "$session" ]; then
        echo "Error: Session name is required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$session"; then
        echo "Error: Session does not exist: $session" >&2
        return 1
    fi
    
    # Check if we're inside tmux
    if [ -n "${TMUX:-}" ]; then
        # Inside tmux, use switch-client
        tmux switch-client -t "$session" || return 1
    else
        # Outside tmux, attach to session
        tmux attach-session -t "$session" || return 1
    fi
    
    return 0
}

# Get current tmux session name
# Usage: get_current_session
# Returns: Session name on stdout, empty if not in tmux
get_current_session() {
    if [ -z "${TMUX:-}" ]; then
        return 1
    fi
    
    tmux display-message -p '#{session_name}' 2>/dev/null || true
}

# Rename a tmux session
# Usage: rename_session <old_name> <new_name>
# Returns: 0 on success, 1 on failure
rename_session() {
    old_name="$1"
    new_name="$2"
    
    if [ -z "$old_name" ] || [ -z "$new_name" ]; then
        echo "Error: Both old and new session names are required" >&2
        return 1
    fi
    
    if ! tmux_session_exists "$old_name"; then
        echo "Error: Session does not exist: $old_name" >&2
        return 1
    fi
    
    if tmux_session_exists "$new_name"; then
        echo "Error: Target session name already exists: $new_name" >&2
        return 1
    fi
    
    tmux rename-session -t "$old_name" "$new_name" || return 1
    
    return 0
}
