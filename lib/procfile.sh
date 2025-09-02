#!/bin/sh
# Procfile support for Hydra
# POSIX-compliant shell script

# Locate a Procfile with simple precedence
# Usage: locate_procfile <worktree> <repo_root>
# Echoes absolute path or nothing
locate_procfile() {
    wt="$1"; repo="$2"
    # Precedence: worktree/Procfile -> worktree/.hydra/Procfile -> repo/.hydra/Procfile -> HYDRA_HOME/Procfile
    if [ -n "$wt" ] && [ -f "$wt/Procfile" ]; then
        echo "$wt/Procfile"; return 0
    fi
    if [ -n "$wt" ] && [ -f "$wt/.hydra/Procfile" ]; then
        echo "$wt/.hydra/Procfile"; return 0
    fi
    if [ -n "$repo" ] && [ -f "$repo/.hydra/Procfile" ]; then
        echo "$repo/.hydra/Procfile"; return 0
    fi
    if [ -n "${HYDRA_HOME:-}" ] && [ -f "$HYDRA_HOME/Procfile" ]; then
        echo "$HYDRA_HOME/Procfile"; return 0
    fi
    return 1
}

# Sanitize a name for tmux window usage: keep [A-Za-z0-9_-], replace others with _
_hydra_sanitize_name() {
    printf '%s' "$1" | sed 's/[^a-zA-Z0-9_-]/_/g'
}

# Run processes from a Procfile into tmux windows
# Format: "name: command"
# Usage: run_procfile <procfile_path> <session> <worktree> <repo_root>
run_procfile() {
    pf="$1"; session="$2"; wt="$3"; _repo="$4"
    [ -f "$pf" ] || return 0

    prefix="${HYDRA_PROCFILE_WINDOW_PREFIX:-proc}"

    # Read each non-empty, non-comment line
    while IFS= read -r line || [ -n "$line" ]; do
        # Trim whitespace
        trimmed="$(printf '%s' "$line" | sed 's/^\s*//;s/\s*$//')"
        [ -z "$trimmed" ] && continue
        case "$trimmed" in
            \#*) continue ;;
        esac
        # Expect pattern: name: command
        case "$trimmed" in
            *:*)
                name="${trimmed%%:*}"
                cmd="${trimmed#*:}"
                # Trim again
                name="$(printf '%s' "$name" | sed 's/^\s*//;s/\s*$//')"
                cmd="$(printf '%s' "$cmd" | sed 's/^\s*//;s/\s*$//')"
                ;;
            *)
                # Ignore invalid lines
                continue
                ;;
        esac
        [ -z "$name" ] && continue
        [ -z "$cmd" ] && continue
        safe_name="$(_hydra_sanitize_name "$name")"
        win_name="$prefix-$safe_name"
        # Create a new window for the process; ignore errors but try best-effort
        tmux new-window -t "$session:" -n "$win_name" -c "$wt" 2>/dev/null || true
        # Send command to the window
        tmux send-keys -t "$session:$win_name" "$cmd" Enter 2>/dev/null || true
    done < "$pf"

    return 0
}

