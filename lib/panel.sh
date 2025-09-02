#!/bin/sh
# TUI Control Panel for Hydra (POSIX shell)

# Render a summary table of current repo heads
panel_print_summary() {
    # Header
    echo "Hydra Control Panel"
    echo "===================="
    # Repo info
    if git rev-parse --git-dir >/dev/null 2>&1; then
        repo_root="$(git rev-parse --show-toplevel 2>/dev/null || true)"
        [ -n "$repo_root" ] && echo "Repo: $repo_root"
    fi
    echo ""
    # Table header
    printf '%-4s %-30s %-24s %-8s %s\n' "#" "Branch" "Session" "Status" "AI"
    printf '%s\n' "--------------------------------------------------------------------------------"

    idx=1
    # Prefer reading raw map file to be compatible across versions
    list=""
    if [ -n "${HYDRA_MAP:-}" ] && [ -f "$HYDRA_MAP" ]; then
        list="$(cat "$HYDRA_MAP" 2>/dev/null || true)"
    else
        list="$(list_mappings 2>/dev/null || true)"
    fi
    if [ -z "$list" ]; then
        echo "No heads for this repo. Use 'hydra spawn <branch>'."
        return 0
    fi
    echo "$list" | while IFS=' ' read -r c1 c2 c3 c4; do
        # Parse either repo-scoped lines (repo:ID branch session [ai]) or legacy (branch session [ai])
        if [ "${c1#repo:}" != "$c1" ]; then
            branch="$c2"; session="$c3"; ai="$c4"
        else
            branch="$c1"; session="$c2"; ai="$c3"
        fi
        [ -z "$branch" ] && continue
        status="dead"
        if tmux_session_exists "$session"; then
            status="alive"
        fi
        printf '%-4s %-30s %-24s %-8s %s\n' "$idx" "$branch" "$session" "$status" "${ai:-}"
        idx=$((idx + 1))
    done
    return 0
}

# Interactive control panel loop (line-based input for POSIX)
run_control_panel() {
    while : ; do
        clear 2>/dev/null || printf '\033c' 2>/dev/null || true
        panel_print_summary
        echo ""
        echo "Actions:"
        echo "  <n>        switch/attach to head #n"
        echo "  k <n>      kill head #n"
        echo "  d          open dashboard"
        echo "  r          refresh"
        echo "  q          quit"
        printf '> '
        read -r cmd arg || break

        case "${cmd:-}" in
            q|quit|exit)
                break ;;
            r|refresh|"")
                continue ;;
            d)
                # Open dashboard; do not attach if requested
                if [ -n "${HYDRA_DASHBOARD_NO_ATTACH:-}" ]; then
                    :
                fi
                "$HYDRA_BIN_CMD" dashboard 2>/dev/null || true
                continue ;;
            k)
                if echo "${arg:-}" | grep -q '^[0-9][0-9]*$'; then
                    entry="$(list_mappings_current_repo | sed -n "${arg}p")"
                    branch="${entry%% *}"
                    if [ -n "$branch" ]; then
                        # Kill selected head (non-interactive)
                        HYDRA_NONINTERACTIVE=1 "$HYDRA_BIN_CMD" kill "$branch" 2>/dev/null || true
                    fi
                fi
                continue ;;
            *)
                # If numeric, switch to session
                if echo "$cmd" | grep -q '^[0-9][0-9]*$'; then
                    entry="$(list_mappings_current_repo | sed -n "${cmd}p")"
                    # entry: branch session [ai]
                    branch="${entry%% *}"
                    session="$(printf '%s' "$entry" | awk '{print $2}')"
                    if [ -n "$session" ]; then
                        switch_to_session "$session" 2>/dev/null || true
                    fi
                    continue
                fi
                ;;
        esac
    done
    return 0
}
