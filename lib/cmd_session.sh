#!/bin/sh
# Hydra command handlers
# POSIX-compliant shell script

cmd_list() {
    # Parse arguments
    filter_group=""
    show_groups=""
    json_output=""
    show_deps=""
    no_pr_status=""
    refresh_pr_status=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -g|--group)
                shift
                filter_group="$1"
                shift
                ;;
            --groups)
                show_groups="1"
                shift
                ;;
            -j|--json)
                json_output="1"
                shift
                ;;
            --deps)
                show_deps="1"
                shift
                ;;
            --no-pr-status)
                no_pr_status="1"
                shift
                ;;
            --refresh-pr-status)
                refresh_pr_status="1"
                shift
                ;;
            -*)
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra list [-g|--group <name>] [--groups] [--json] [--deps] [--no-pr-status] [--refresh-pr-status]" >&2
                exit 1
                ;;
            *)
                echo "Error: Unexpected argument '$1'" >&2
                exit 1
                ;;
        esac
    done

    # Show dependency tree if requested
    if [ -n "$show_deps" ]; then
        _load_lib deps
        echo "Session Dependencies:"
        echo ""
        build_full_dep_tree
        return 0
    fi

    # List all groups if requested
    if [ -n "$show_groups" ]; then
        groups="$(list_groups)"
        if [ -n "$json_output" ]; then
            # JSON output for groups - collect in temp file to avoid subshell issue
            tmpjson="$(mktemp)"
            trap 'rm -f "$tmpjson"' EXIT
            echo "$groups" | while read -r g; do
                [ -z "$g" ] && continue
                count="$(list_mappings_for_group "$g" | wc -l | tr -d ' ')"
                printf '{"name": "%s", "count": %s}\n' "$(json_escape "$g")" "$count" >> "$tmpjson"
            done
            printf '{"groups": ['
            first=1
            while IFS= read -r line; do
                if [ "$first" -eq 1 ]; then
                    first=0
                else
                    printf ','
                fi
                printf '%s' "$line"
            done < "$tmpjson"
            printf ']}\n'
            rm -f "$tmpjson"
            trap - EXIT
        else
            if [ -z "$groups" ]; then
                echo "No groups defined"
            else
                echo "Groups:"
                echo "$groups" | while read -r g; do
                    count="$(list_mappings_for_group "$g" | wc -l | tr -d ' ')"
                    echo "  $g ($count sessions)"
                done
            fi
        fi
        return 0
    fi

    # Check if we have any mappings
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        if [ -n "$json_output" ]; then
            printf '{"sessions": [], "total": 0, "active": 0, "dead": 0}\n'
        else
            echo "No active Hydra heads"
        fi
        return 0
    fi

    # Cache current session once before the loop (perf: issue #36)
    current_session="$(tmux display-message -p '#{session_name}' 2>/dev/null || true)"

    # Batch tmux observation for this command
    tmux_load_snapshot

    if [ -n "$json_output" ]; then
        # JSON output mode
        tmpjson="$(mktemp)"
        trap 'rm -f "$tmpjson"' EXIT
        total=0
        active=0
        dead=0

        while IFS=' ' read -r branch session ai group timestamp deps pr; do
            # Filter by group if specified
            if [ -n "$filter_group" ]; then
                if [ "$group" != "$filter_group" ]; then
                    continue
                fi
            fi

            total=$((total + 1))

            # Calculate duration (validated numeric subtraction)
            duration_secs=0
            if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                duration_secs="$(get_duration_since "$timestamp")"
            fi
            duration_human="$(format_duration "$duration_secs")"

            # Determine status (use snapshot loaded above)
            if tmux_session_exists "$session"; then
                status="active"
                active=$((active + 1))
            else
                status="dead"
                dead=$((dead + 1))
            fi

            # Check if current
            is_current="false"
            if [ "$session" = "$current_session" ]; then
                is_current="true"
            fi

            # Handle null/empty values for JSON
            ai_json="null"
            if [ -n "$ai" ] && [ "$ai" != "-" ]; then
                ai_json="\"$(json_escape "$ai")\""
            fi
            group_json="null"
            if [ -n "$group" ] && [ "$group" != "-" ]; then
                group_json="\"$(json_escape "$group")\""
            fi
            ts_json="null"
            if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                ts_json="$timestamp"
            fi
            deps_json="null"
            if [ -n "$deps" ] && [ "$deps" != "-" ]; then
                deps_json="\"$(json_escape "$deps")\""
            fi
            pr_json="null"
            pr_status_json="null"
            if [ -n "$pr" ] && [ "$pr" != "-" ]; then
                pr_json="$pr"
                # Fetch PR status if not disabled
                if [ -z "$no_pr_status" ]; then
                    _load_lib github
                    _pr_st="$(get_pr_status_cached "$pr" "$refresh_pr_status" 2>/dev/null || echo "")"
                    if [ -n "$_pr_st" ]; then
                        pr_status_json="\"$(json_escape "$_pr_st")\""
                    fi
                fi
            fi

            # Build JSON object
            printf '{"branch": "%s", "session": "%s", "ai": %s, "group": %s, "status": "%s", "duration_seconds": %s, "duration_human": "%s", "timestamp": %s, "current": %s, "deps": %s, "pr": %s, "pr_status": %s}\n' \
                "$(json_escape "$branch")" \
                "$(json_escape "$session")" \
                "$ai_json" \
                "$group_json" \
                "$status" \
                "$duration_secs" \
                "$duration_human" \
                "$ts_json" \
                "$is_current" \
                "$deps_json" \
                "$pr_json" \
                "$pr_status_json" >> "$tmpjson"
        done < "$HYDRA_MAP"

        # Output JSON
        printf '{"sessions": ['
        first=1
        while IFS= read -r line; do
            if [ "$first" -eq 1 ]; then
                first=0
            else
                printf ','
            fi
            printf '%s' "$line"
        done < "$tmpjson"
        printf '], "total": %s, "active": %s, "dead": %s}\n' "$total" "$active" "$dead"

        rm -f "$tmpjson"
        trap - EXIT
    else
        # Human-readable output mode
        if [ -n "$filter_group" ]; then
            echo "Active Hydra heads in group '$filter_group':"
        else
            echo "Active Hydra heads:"
        fi
        echo ""

        while IFS=' ' read -r branch session ai group timestamp deps pr; do
            # Filter by group if specified
            if [ -n "$filter_group" ]; then
                if [ "$group" != "$filter_group" ]; then
                    continue
                fi
            fi

            # Calculate duration if timestamp exists
            duration_str=""
            if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                duration_secs="$(get_duration_since "$timestamp")"
                duration_str="$(format_duration "$duration_secs")"
            fi

            # Build status line
            status_line=""
            if [ -n "$duration_str" ]; then
                status_line="($duration_str)"
            fi
            if [ -n "$ai" ] && [ "$ai" != "-" ]; then
                if [ -n "$status_line" ]; then
                    status_line="$status_line [ai: $ai]"
                else
                    status_line="[ai: $ai]"
                fi
            fi
            if [ -n "$group" ] && [ "$group" != "-" ]; then
                if [ -n "$status_line" ]; then
                    status_line="$status_line [group: $group]"
                else
                    status_line="[group: $group]"
                fi
            fi
            # Show dependencies if present
            if [ -n "$deps" ] && [ "$deps" != "-" ]; then
                if [ -n "$status_line" ]; then
                    status_line="$status_line --after $deps"
                else
                    status_line="--after $deps"
                fi
            fi
            # Show PR if linked (with status if available)
            if [ -n "$pr" ] && [ "$pr" != "-" ]; then
                pr_display="PR #$pr"
                if [ -z "$no_pr_status" ]; then
                    _load_lib github
                    _pr_st="$(get_pr_status_cached "$pr" "$refresh_pr_status" 2>/dev/null || echo "")"
                    if [ -n "$_pr_st" ] && [ "$_pr_st" != "UNKNOWN" ]; then
                        pr_display="PR #$pr $_pr_st"
                    fi
                fi
                if [ -n "$status_line" ]; then
                    status_line="$status_line [$pr_display]"
                else
                    status_line="[$pr_display]"
                fi
            fi

            # Check if session still exists (use snapshot loaded above)
            if tmux_session_exists "$session"; then
                # Check if it's the current session
                if [ "$session" = "$current_session" ]; then
                    echo "* $branch -> $session $status_line (current)"
                else
                    echo "  $branch -> $session $status_line"
                fi
            else
                echo "  $branch -> $session $status_line (dead)"
            fi
        done < "$HYDRA_MAP"
    fi
}

cmd_switch() {
    # Interactive session switcher
    if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
        echo "No active Hydra heads to switch to"
        return 1
    fi

    # If inside tmux, use tmux's interactive switcher
    if [ -n "${TMUX:-}" ]; then
        # Batch tmux observation for this command
        tmux_load_snapshot

        # Build session list for fzf or simple menu (tab-separated: branch, session)
        sessions=""
        while IFS=' ' read -r branch session _rest; do
            if tmux_session_exists "$session"; then
                sessions="${sessions}${branch}	${session}
"
            fi
        done < "$HYDRA_MAP"
        
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
        mappings="$(list_mappings_for_group "$kill_group")"
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
            kill_single_head "$b" 2>/dev/null || true
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
    if [ -t 0 ] && [ -z "${CI:-}" ] && [ -z "${HYDRA_NONINTERACTIVE:-}" ]; then
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
                echo "Error: Unknown option '$1'" >&2
                echo "Usage: hydra status [--json]" >&2
                exit 1
                ;;
            *)
                echo "Error: Unexpected argument '$1'" >&2
                exit 1
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

        if [ -f "$HYDRA_MAP" ] && [ -s "$HYDRA_MAP" ]; then
            tmux_load_snapshot
            while IFS=' ' read -r branch session ai group timestamp deps pr; do
                # Calculate duration
                duration_secs=0
                if [ -n "$timestamp" ] && [ "$timestamp" != "-" ]; then
                    duration_secs="$(get_duration_since "$timestamp")"
                fi

                # Determine status
                if tmux_session_exists "$session"; then
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

                printf '{"branch": "%s", "session": "%s", "ai": %s, "status": "%s", "duration_seconds": %s}\n' \
                    "$(json_escape "$branch")" \
                    "$(json_escape "$session")" \
                    "$ai_json" \
                    "$status" \
                    "$duration_secs" >> "$tmpjson"
            done < "$HYDRA_MAP"
        fi

        # Build final JSON
        printf '{'
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
        printf '}\n'

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

        if [ ! -f "$HYDRA_MAP" ] || [ ! -s "$HYDRA_MAP" ]; then
            echo "No active Hydra heads"
            return 0
        fi

        echo "Active Heads:"
        active=0
        dead=0

        tmux_load_snapshot
        while IFS=' ' read -r branch session ai group timestamp deps pr; do
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

            if tmux_session_exists "$session"; then
                echo "  [OK] $branch -> $session $info"
                active=$((active + 1))
            else
                echo "  [DEAD] $branch -> $session $info"
                dead=$((dead + 1))
            fi
        done < "$HYDRA_MAP"

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

