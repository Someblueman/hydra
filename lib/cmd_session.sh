#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script


cmd_switch() {
    if [ "$#" -gt 1 ]; then
        cli_error switch invalid_input "Expected at most one branch" "run hydra switch [branch]"
        return 1
    fi

    # Interactive session switcher
    if ! state_has_heads; then
        echo "No active Hydra heads to switch to"
        return 1
    fi

    if [ "$#" -eq 1 ]; then
        _cs_branch="$1"
        _cs_session="$(get_session_for_branch "$_cs_branch" 2>/dev/null || true)"
        if [ -z "$_cs_session" ]; then
            cli_error switch not_found "No Hydra head found for branch '$_cs_branch'" "run hydra list"
            return 1
        fi
        switch_to_session "$_cs_session"
        return $?
    fi

    # If inside tmux, use tmux's interactive switcher
    if [ -n "${TMUX:-}" ]; then
        # Batch tmux observation for this command
        tmux_load_snapshot

        # Build session list for fzf or simple menu (tab-separated: branch, session)
        sessions=""
        while IFS=' ' read -r branch session _rest; do
            if tmux_snapshot_has_session "$session"; then
                sessions="${sessions}${branch}	${session}
"
            fi
        done <<EOF
$(state_list_heads)
EOF
        
        if [ -z "$sessions" ]; then
            echo "No active sessions found"
            return 1
        fi
        
        # Use fzf if available, otherwise simple menu
        if command -v fzf >/dev/null 2>&1; then
            selection="$(printf '%s' "$sessions" | fzf --prompt="Switch to: " --height=10 --with-nth=1 --delimiter="$(printf '\t')" --preview='cut -f2')"
        else
            echo "Active sessions:"
            i=1
            printf '%s' "$sessions" | while IFS='	' read -r branch session; do
                [ -z "$branch" ] && continue
                echo "$i) $branch ($session)"
                i=$((i + 1))
            done
            session_count="$(printf '%s' "$sessions" | grep -c . || true)"
            printf "Select session (1-%d): " "$session_count"
            read -r choice

            # Validate numeric input
            case "$choice" in
                ''|*[!0-9]*)
                    echo "Invalid selection: must be a number" >&2
                    return 1
                    ;;
            esac

            # Validate range
            if [ "$choice" -lt 1 ] || [ "$choice" -gt "$session_count" ]; then
                echo "Invalid selection: must be between 1 and $session_count" >&2
                return 1
            fi

            selection="$(printf '%s' "$sessions" | sed -n "${choice}p")"
        fi
        
        if [ -n "$selection" ]; then
            session_name="$(printf '%s' "$selection" | cut -f2)"
            if [ -n "$session_name" ]; then
                switch_to_session "$session_name"
            fi
        fi
    else
        echo "Error: Not inside a tmux session"
        echo "Use 'tmux attach -t <session>' to attach to a session"
        return 1
    fi
}

cmd_kill() {
    # Parse arguments
    branch=""
    kill_all=false
    force=false
    kill_group=""
    transcript_policy=none

    while [ $# -gt 0 ]; do
        case "$1" in
            --all)
                kill_all=true
                shift
                ;;
            --force)
                force=true
                shift
                ;;
            --transcript)
                [ $# -ge 2 ] || { echo "Error: --transcript requires none, redacted, or full" >&2; return 1; }
                transcript_policy="$2"
                case "$transcript_policy" in none|redacted|full) ;; *) echo "Error: invalid transcript policy '$transcript_policy'" >&2; return 1 ;; esac
                shift 2
                ;;
            -g|--group)
                shift
                kill_group="$1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra kill <branch>" >&2
                echo "       hydra kill --all [--force]" >&2
                echo "       hydra kill -g|--group <name> [--force]" >&2
                return 1
                ;;
            *)
                if [ -z "$branch" ]; then
                    branch="$1"
                else
                    echo "Error: Too many arguments" >&2
                    echo "Usage: hydra kill <branch>" >&2
                    echo "       hydra kill --all [--force]" >&2
                    echo "       hydra kill -g|--group <name> [--force]" >&2
                    return 1
                fi
                shift
                ;;
        esac
    done
    HYDRA_TEARDOWN_TRANSCRIPT_POLICY="$transcript_policy"
    export HYDRA_TEARDOWN_TRANSCRIPT_POLICY

    # Check mutual exclusivity
    if [ "$kill_all" = true ] && [ -n "$branch" ]; then
        echo "Error: Cannot specify both branch name and --all" >&2
        echo "Usage: hydra kill <branch>" >&2
        echo "       hydra kill --all [--force]" >&2
        echo "       hydra kill -g|--group <name> [--force]" >&2
        return 1
    fi

    if [ -n "$kill_group" ] && [ -n "$branch" ]; then
        echo "Error: Cannot specify both branch name and --group" >&2
        return 1
    fi

    if [ -n "$kill_group" ] && [ "$kill_all" = true ]; then
        echo "Error: Cannot specify both --all and --group" >&2
        return 1
    fi

    # If --all flag is set, delegate to kill_all_sessions
    if [ "$kill_all" = true ]; then
        kill_all_sessions "$force"
        return $?
    fi

    # If --group is set, kill all sessions in the group
    if [ -n "$kill_group" ]; then
        mappings="$(state_list_heads_for_group "$kill_group")"
        if [ -z "$mappings" ]; then
            echo "No sessions found in group '$kill_group'"
            return 1
        fi

        count="$(echo "$mappings" | wc -l | tr -d ' ')"

        # Confirm unless forced
        if [ "$force" != true ] && [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
            printf "Kill all %s sessions in group '%s'? [y/N] " "$count" "$kill_group"
            read -r response
            case "$response" in
                [yY][eE][sS]|[yY]) ;;
                *)
                    echo "Aborted"
                    return 0
                    ;;
            esac
        fi

        echo "Killing $count sessions in group '$kill_group'..."
        echo "$mappings" | while IFS=' ' read -r b _s _a _g; do
            echo "  Killing $b..."
            kill_single_head "$b" "$_s" 2>/dev/null || true
        done
        echo "Done"
        return 0
    fi

    # Original single branch kill logic
    if [ -z "$branch" ]; then
        echo "Error: Branch name required" >&2
        echo "Usage: hydra kill <branch>" >&2
        echo "       hydra kill --all [--force]" >&2
        echo "       hydra kill -g|--group <name> [--force]" >&2
        return 1
    fi
    
    # Get session for branch
    session="$(get_session_for_branch "$branch" 2>/dev/null || true)"
    
    if [ -z "$session" ]; then
        echo "No session found for branch '$branch'"
        return 1
    fi
    
    # Skip confirmation in non-interactive environments (CI, tests)
    if [ "$force" != true ] && [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
        # Interactive mode - ask for confirmation
        printf "Kill hydra head '%s' (session: %s)? [y/N] " "$branch" "$session"
        read -r response
        case "$response" in
            [yY][eE][sS]|[yY])
                ;;
            *)
                echo "Aborted"
                return 0
                ;;
        esac
    else
        # Non-interactive mode - proceed without confirmation
        echo "Killing hydra head '$branch' (session: $session) [non-interactive mode]"
    fi
    
    # Use kill_single_head helper for the actual kill
    if kill_single_head "$branch" "$session"; then
        echo "Hydra head '$branch' has been killed"
    else
        echo "Failed to kill hydra head '$branch'" >&2
        return 1
    fi
}

cmd_status() {
    # Parse arguments
    json_output=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -j|--json)
                json_output="1"
                shift
                ;;
            -*)
                cli_error status invalid_input "Unknown option '$1'" "run hydra status --json"
                return 1
                ;;
            *)
                cli_error status invalid_input "Unexpected argument '$1'" "run hydra status --json"
                return 1
                ;;
        esac
    done

    # Collect system info
    tmux_ver="$(tmux -V 2>/dev/null || echo "Not installed")"
    git_ver="$(git --version 2>/dev/null | sed 's/git version //' || echo "Not installed")"

    # Repository info
    repo_path=""
    current_branch=""
    if git rev-parse --git-dir >/dev/null 2>&1; then
        repo_path="$(git rev-parse --show-toplevel)"
        current_branch="$(git branch --show-current 2>/dev/null || echo "Unknown")"
    fi

    if [ -n "$json_output" ]; then
        # JSON output mode
        tmpjson="$(mktemp)"
        trap 'rm -f "$tmpjson"' EXIT
        active=0
        dead=0

        if state_has_heads; then
            tmux_load_snapshot
            while IFS=' ' read -r branch session ai _group timestamp _deps _pr; do
                # Calculate duration
                duration_secs=0
                if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                    duration_secs="$(get_duration_since "$timestamp")"
                fi

                # Determine status
                if tmux_snapshot_has_session "$session"; then
                    status="active"
                    active=$((active + 1))
                else
                    status="dead"
                    dead=$((dead + 1))
                fi

                # Handle null/empty values
                ai_json="null"
                if [ -n "$ai" ] && [ "$ai" != "-" ]; then
                    ai_json="\"$(json_escape "$ai")\""
                fi

                lifecycle_snapshot "$branch"
                printf '{"branch": "%s", "session": "%s", "ai": %s, "status": "%s", "duration_seconds": %s, "instance_id": %s, "declared_outcome": %s, "observed_status": "%s", "observed_confidence": "%s", "liveness": "%s", "complete": %s}\n' \
                    "$(json_escape "$branch")" \
                    "$(json_escape "$session")" \
                    "$ai_json" \
                    "$status" \
                    "$duration_secs" \
                    "$(json_string_or_null "$LIFECYCLE_SNAPSHOT_INSTANCE")" \
                    "$(json_string_or_null "$LIFECYCLE_SNAPSHOT_OUTCOME")" \
                    "$(json_escape "$LIFECYCLE_SNAPSHOT_OBSERVED")" \
                    "$(json_escape "$LIFECYCLE_SNAPSHOT_CONFIDENCE")" \
                    "$(json_escape "$LIFECYCLE_SNAPSHOT_LIVENESS")" \
                    "$LIFECYCLE_SNAPSHOT_COMPLETE" >> "$tmpjson"
            done <<EOF
$(state_list_heads)
EOF
        fi

        # Build final JSON
        printf '{"schema_version":1,"ok":true,"command":"status","data":{'
        printf '"system": {"hydra_version": "%s", "tmux_version": "%s", "git_version": "%s"}' \
            "$(json_escape "$HYDRA_VERSION")" \
            "$(json_escape "$tmux_ver")" \
            "$(json_escape "$git_ver")"
        printf ', "repository": {"path": "%s", "branch": "%s"}' \
            "$(json_escape "$repo_path")" \
            "$(json_escape "$current_branch")"
        printf ', "sessions": ['

        first=1
        if [ -f "$tmpjson" ]; then
            while IFS= read -r line; do
                if [ "$first" -eq 1 ]; then
                    first=0
                else
                    printf ','
                fi
                printf '%s' "$line"
            done < "$tmpjson"
        fi

        printf '], "summary": {"active": %s, "dead": %s}' "$active" "$dead"
        printf '}}\n'

        rm -f "$tmpjson"
        trap - EXIT
    else
        # Human-readable output
        echo "Hydra Status Report"
        echo "=================="
        echo ""

        echo "System Information:"
        echo "  Hydra Version: $HYDRA_VERSION"
        echo "  tmux Version: $tmux_ver"
        echo "  Git Version: $git_ver"
        echo ""

        if [ -n "$repo_path" ]; then
            echo "Repository:"
            echo "  Path: $repo_path"
            echo "  Current Branch: $current_branch"
            echo ""
        fi

        if ! state_has_heads; then
            echo "No active Hydra heads"
            return 0
        fi

        echo "Active Heads:"
        active=0
        dead=0

        tmux_load_snapshot
        while IFS=' ' read -r branch session ai _group timestamp _deps _pr; do
            # Calculate duration if timestamp exists
            duration_str=""
            if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                duration_secs="$(get_duration_since "$timestamp")"
                duration_str="$(format_duration "$duration_secs")"
            fi

            # Build info string
            info=""
            if [ -n "$duration_str" ]; then
                info="($duration_str)"
            fi
            if [ -n "$ai" ] && [ "$ai" != "-" ]; then
                if [ -n "$info" ]; then
                    info="$info [ai: $ai]"
                else
                    info="[ai: $ai]"
                fi
            fi

            lifecycle_snapshot "$branch"
            lifecycle_info="[declared: ${LIFECYCLE_SNAPSHOT_OUTCOME:-none}] [observed: $LIFECYCLE_SNAPSHOT_OBSERVED/$LIFECYCLE_SNAPSHOT_CONFIDENCE] [live: $LIFECYCLE_SNAPSHOT_LIVENESS]"
            if tmux_snapshot_has_session "$session"; then
                echo "  [OK] $branch -> $session $info $lifecycle_info"
                active=$((active + 1))
            else
                echo "  [DEAD] $branch -> $session $info $lifecycle_info"
                dead=$((dead + 1))
            fi
        done <<EOF
$(state_list_heads)
EOF

        echo ""
        echo "Summary:"
        echo "  Active Sessions: $active"
        echo "  Dead Sessions: $dead"

        if [ "$dead" -gt 0 ]; then
            echo ""
            echo "Note: Dead sessions can be regenerated with 'hydra regenerate'"
        fi
    fi
}

cmd_cycle_layout() {
    cycle_layout
}
