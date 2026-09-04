#!/bin/sh
# Hydra list command handler
# POSIX-compliant shell script

cmd_list() {
    # Parse arguments
    filter_group=""
    show_groups=""
    json_output=""
    show_deps=""
    no_pr_status=""
    refresh_pr_status=""
    show_git=""
    while [ $# -gt 0 ]; do
        case "$1" in
            -g|--group)
                [ $# -ge 2 ] || { cli_error list invalid_input "--group requires a name" "run hydra list --help"; return 1; }
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
            --git)
                show_git="1"
                shift
                ;;
            -*)
                cli_error list invalid_input "Unknown option '$1'" "run hydra help"
                return 1
                ;;
            *)
                cli_error list invalid_input "Unexpected argument '$1'" "run hydra help"
                return 1
                ;;
        esac
    done

    # Show dependency tree if requested
    if [ -n "$show_deps" ]; then
        [ -z "$json_output" ] || { cli_error list unsupported_combination "--deps does not have a JSON representation" "use hydra list --json without --deps"; return 1; }
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
                count="$(state_list_heads_for_group "$g" | wc -l | tr -d ' ')"
                printf '{"name": "%s", "count": %s}\n' "$(json_escape "$g")" "$count" >> "$tmpjson"
            done
            printf '{"schema_version":1,"ok":true,"command":"list","data":{"groups":['
            first=1
            while IFS= read -r line; do
                if [ "$first" -eq 1 ]; then
                    first=0
                else
                    printf ','
                fi
                printf '%s' "$line"
            done < "$tmpjson"
            printf ']}}\n'
            rm -f "$tmpjson"
            trap - EXIT
        else
            if [ -z "$groups" ]; then
                echo "No groups defined"
            else
                echo "Groups:"
                echo "$groups" | while read -r g; do
                    count="$(state_list_heads_for_group "$g" | wc -l | tr -d ' ')"
                    echo "  $g ($count sessions)"
                done
            fi
        fi
        return 0
    fi

    if ! state_has_heads; then
        if [ -n "$json_output" ]; then
            json_success list '{"sessions":[],"total":0,"active":0,"dead":0}'
        else
            echo "No active Hydra heads"
        fi
        return 0
    fi
    state_rows="$(state_list_heads)"

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
            if tmux_snapshot_has_session "$session"; then
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
            lifecycle_snapshot "$branch"
            git_json=null
            if [ -n "$show_git" ] && [ -n "$LIFECYCLE_SNAPSHOT_INSTANCE" ]; then
                _clg_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree" 2>/dev/null || true)"
                _clg_base="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref" 2>/dev/null || true)"
                if [ -d "$_clg_worktree" ] && [ -n "$_clg_base" ]; then
                    operations_git_counts "$_clg_worktree" "$_clg_base"
                    git_json="{\"base_ref\":\"$_clg_base\",\"ahead\":$OPERATIONS_AHEAD,\"behind\":$OPERATIONS_BEHIND,\"dirty_paths\":$OPERATIONS_DIRTY}"
                fi
            fi
            printf '{"branch": "%s", "session": "%s", "ai": %s, "group": %s, "status": "%s", "duration_seconds": %s, "duration_human": "%s", "timestamp": %s, "current": %s, "deps": %s, "pr": %s, "pr_status": %s, "instance_id": %s, "declared_outcome": %s, "observed_status": "%s", "observed_confidence": "%s", "liveness": "%s", "complete": %s, "git": %s}\n' \
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
                "$pr_status_json" \
                "$(json_string_or_null "$LIFECYCLE_SNAPSHOT_INSTANCE")" \
                "$(json_string_or_null "$LIFECYCLE_SNAPSHOT_OUTCOME")" \
                "$(json_escape "$LIFECYCLE_SNAPSHOT_OBSERVED")" \
                "$(json_escape "$LIFECYCLE_SNAPSHOT_CONFIDENCE")" \
                "$(json_escape "$LIFECYCLE_SNAPSHOT_LIVENESS")" \
                "$LIFECYCLE_SNAPSHOT_COMPLETE" \
                "$git_json" >> "$tmpjson"
        done <<EOF
$state_rows
EOF

        # Output JSON
        printf '{"schema_version":1,"ok":true,"command":"list","data":{"sessions":['
        first=1
        while IFS= read -r line; do
            if [ "$first" -eq 1 ]; then
                first=0
            else
                printf ','
            fi
            printf '%s' "$line"
        done < "$tmpjson"
        printf '],"total":%s,"active":%s,"dead":%s}}\n' "$total" "$active" "$dead"

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
            lifecycle_snapshot "$branch"
            status_line="$status_line [declared: ${LIFECYCLE_SNAPSHOT_OUTCOME:-none}] [observed: $LIFECYCLE_SNAPSHOT_OBSERVED/$LIFECYCLE_SNAPSHOT_CONFIDENCE] [live: $LIFECYCLE_SNAPSHOT_LIVENESS]"
            if [ -n "$show_git" ] && [ -n "$LIFECYCLE_SNAPSHOT_INSTANCE" ]; then
                _clg_worktree="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/worktree" 2>/dev/null || true)"
                _clg_base="$(sed -n '1p' "$LIFECYCLE_HEAD_DIR/base-ref" 2>/dev/null || true)"
                if [ -d "$_clg_worktree" ] && [ -n "$_clg_base" ]; then
                    operations_git_counts "$_clg_worktree" "$_clg_base"
                    status_line="$status_line [git: +$OPERATIONS_AHEAD/-$OPERATIONS_BEHIND dirty=$OPERATIONS_DIRTY]"
                else
                    status_line="$status_line [git: unavailable]"
                fi
            fi
            if tmux_snapshot_has_session "$session"; then
                # Check if it's the current session
                if [ "$session" = "$current_session" ]; then
                    echo "* $branch -> $session $status_line (current)"
                else
                    echo "  $branch -> $session $status_line"
                fi
            else
                echo "  $branch -> $session $status_line (dead)"
            fi
        done <<EOF
$state_rows
EOF
    fi
}
