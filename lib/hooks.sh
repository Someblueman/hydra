#!/bin/sh
# Hooks and custom session configuration for Hydra
# POSIX-compliant shell script

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
