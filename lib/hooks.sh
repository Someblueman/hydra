#!/bin/sh
# Hooks and custom session configuration for Hydra
# POSIX-compliant shell script

# =============================================================================
# Environment Setup Automation
# =============================================================================
# Runs setup commands from .hydra/config.yml BEFORE session creation.
# This enables automatic dependency installation (npm install, etc.)
#
# Config format:
# setup:
#   - npm install
#   - cp .env.example .env
#   - docker-compose up -d db
#
# Environment:
#   HYDRA_SKIP_SETUP=1      Skip all setup commands
#   HYDRA_SETUP_CONTINUE=1  Continue spawn even if setup fails

# Parse setup commands from YAML config file
# Usage: parse_setup_commands <config_path>
# Returns: One command per line on stdout
parse_setup_commands() {
    _cfg="$1"
    [ -f "$_cfg" ] || return 0

    awk '
      BEGIN { in_setup=0 }
      /^setup:/ { in_setup=1; next }
      /^[a-zA-Z_]+:/ && !/^  / { in_setup=0 }
      in_setup && /^[[:space:]]*-[[:space:]]/ {
        cmd=$0
        sub(/^[[:space:]]*-[[:space:]]*/, "", cmd)
        # Trim trailing whitespace
        sub(/[[:space:]]*$/, "", cmd)
        if (cmd != "") print cmd
      }
    ' "$_cfg"
}

# Run environment setup commands from YAML config (blocking)
# Usage: run_setup_commands <worktree_path> <repo_root>
# Returns: 0 on success, 1 on failure
# Note: Runs BEFORE session creation in worktree directory
run_setup_commands() {
    _wt="$1"
    _repo="$2"

    # Check skip flag
    if [ -n "${HYDRA_SKIP_SETUP:-}" ]; then
        return 0
    fi

    # Locate YAML config
    _cfgpath=""
    # Check worktree first
    if [ -n "$_wt" ] && [ -f "$_wt/.hydra/config.yml" ]; then
        _cfgpath="$_wt/.hydra/config.yml"
    elif [ -n "$_wt" ] && [ -f "$_wt/.hydra/config.yaml" ]; then
        _cfgpath="$_wt/.hydra/config.yaml"
    # Then repo root
    elif [ -n "$_repo" ] && [ -f "$_repo/.hydra/config.yml" ]; then
        _cfgpath="$_repo/.hydra/config.yml"
    elif [ -n "$_repo" ] && [ -f "$_repo/.hydra/config.yaml" ]; then
        _cfgpath="$_repo/.hydra/config.yaml"
    # Then HYDRA_HOME
    elif [ -n "${HYDRA_HOME:-}" ] && [ -f "$HYDRA_HOME/config.yml" ]; then
        _cfgpath="$HYDRA_HOME/config.yml"
    elif [ -n "${HYDRA_HOME:-}" ] && [ -f "$HYDRA_HOME/config.yaml" ]; then
        _cfgpath="$HYDRA_HOME/config.yaml"
    fi

    # No config found
    if [ -z "$_cfgpath" ]; then
        return 0
    fi

    # Parse setup commands to temp file (avoids subshell issues with while loop)
    _tmpfile="$(mktemp)"
    parse_setup_commands "$_cfgpath" > "$_tmpfile"

    # Check if any commands to run
    if [ ! -s "$_tmpfile" ]; then
        rm -f "$_tmpfile"
        return 0
    fi

    # Count commands for progress
    _total="$(wc -l < "$_tmpfile" | tr -d ' ')"
    _current=0
    _failed=0

    echo "[setup] Running $_total setup command(s)..." >&2

    # Execute each command in worktree directory
    # Use file redirection instead of pipe to avoid subshell
    # shellcheck disable=SC2094
    while IFS= read -r _cmd; do
        [ -z "$_cmd" ] && continue
        _current=$((_current + 1))

        echo "[setup] ($_current/$_total) $_cmd" >&2

        # Run command in worktree directory
        # Use temp file to capture output while preserving exit code
        _outfile="$(mktemp)"
        (cd "$_wt" && sh -c "$_cmd") >"$_outfile" 2>&1
        _exit_code=$?

        # Display output with prefix
        if [ -s "$_outfile" ]; then
            sed 's/^/[setup]   /' < "$_outfile" >&2
        fi
        rm -f "$_outfile"

        if [ "$_exit_code" -ne 0 ]; then
            _failed=1
            echo "[setup] Command failed (exit code $_exit_code): $_cmd" >&2

            if [ -z "${HYDRA_SETUP_CONTINUE:-}" ]; then
                echo "[setup] Aborting (set HYDRA_SETUP_CONTINUE=1 to continue on failure)" >&2
                rm -f "$_tmpfile"
                return 1
            fi
            echo "[setup] Continuing despite failure (HYDRA_SETUP_CONTINUE set)" >&2
        fi
    done < "$_tmpfile"

    rm -f "$_tmpfile"

    if [ "$_failed" -eq 1 ] && [ -z "${HYDRA_SETUP_CONTINUE:-}" ]; then
        return 1
    fi

    echo "[setup] Complete ($_total command(s) executed)" >&2
    return 0
}

# =============================================================================
# Configuration Directory Discovery
# =============================================================================

# Locate the highest-precedence config directory for a worktree
# Precedence: worktree/.hydra -> repo_root/.hydra -> HYDRA_HOME
# Usage: locate_config_dir <worktree_path> <repo_root>
# Echoes the directory path or empty if none
locate_config_dir() {
    wt="$1"
    repo="$2"

    if [ -n "$wt" ] && [ -d "$wt/.hydra" ]; then
        echo "$wt/.hydra"
        return 0
    fi
    if [ -n "$repo" ] && [ -d "$repo/.hydra" ]; then
        echo "$repo/.hydra"
        return 0
    fi
    if [ -n "${HYDRA_HOME:-}" ] && [ -d "$HYDRA_HOME" ]; then
        echo "$HYDRA_HOME"
        return 0
    fi
    return 1
}

# Run a hook script if present: hooks/<name>
# Usage: run_hook <name> <worktree_path> <repo_root> <session> <branch>
# Hook runs in non-interactive shell, with env HYDRA_SESSION, HYDRA_WORKTREE, HYDRA_BRANCH
run_hook() {
    name="$1"
    wt="$2"
    repo="$3"
    hook_session="$4"
    branch="$5"

    confdir="$(locate_config_dir "$wt" "$repo" 2>/dev/null || true)"
    if [ -z "$confdir" ]; then
        return 0
    fi
    hook="$confdir/hooks/$name"
    if [ -f "$hook" ]; then
        HYDRA_SESSION="$hook_session" HYDRA_WORKTREE="$wt" HYDRA_BRANCH="$branch" sh "$hook" || true
    fi
}

# Apply custom layout if hooks/layout exists; otherwise fall back to built-in layout
# Usage: apply_custom_layout_or_default <layout> <session> <worktree_path> <repo_root>
apply_custom_layout_or_default() {
    layout="$1"
    target_session="$2"
    wt="$3"
    repo="$4"

    confdir="$(locate_config_dir "$wt" "$repo" 2>/dev/null || true)"
    if [ -n "$confdir" ] && [ -f "$confdir/hooks/layout" ]; then
        HYDRA_SESSION="$target_session" HYDRA_WORKTREE="$wt" sh "$confdir/hooks/layout" || true
        return 0
    fi
    # Fallback to built-in layouts: split panes anchored to worktree path
    case "$layout" in
        default)
            # Single pane; create_session already starts at worktree
            ;;
        dev)
            tmux kill-pane -a -t "$target_session:0.0" 2>/dev/null || true
            tmux split-window -t "$target_session:0.0" -h -p 30 -c "$wt"
            tmux select-pane -t "$target_session:0.0"
            ;;
        full)
            tmux kill-pane -a -t "$target_session:0.0" 2>/dev/null || true
            tmux split-window -t "$target_session:0.0" -h -p 30 -c "$wt"
            tmux split-window -t "$target_session:0.1" -v -p 30 -c "$wt"
            tmux select-pane -t "$target_session:0.0"
            ;;
        *)
            :
            ;;
    esac
}

# Send startup commands from config file to the session
# File name: startup (one command per line; lines starting with # or blank are ignored)
# Usage: run_startup_commands <session> <worktree_path> <repo_root>
run_startup_commands() {
    target_session="$1"
    wt="$2"
    repo="$3"
    confdir="$(locate_config_dir "$wt" "$repo" 2>/dev/null || true)"
    if [ -z "$confdir" ]; then
        return 0
    fi
    start_file="$confdir/startup"
    if [ ! -f "$start_file" ]; then
        return 0
    fi
    # Read and send each non-empty, non-comment line
    while IFS= read -r line || [ -n "$line" ]; do
        # Trim leading/trailing whitespace
        trimmed="$(printf '%s' "$line" | sed 's/^\s*//;s/\s*$//')"
        # Skip blank and comment lines
        [ -z "$trimmed" ] && continue
        case "$trimmed" in
            \#*) continue ;;
        esac
        send_keys_to_session "$target_session" "$trimmed"
    done < "$start_file"
}
