#!/bin/sh
# Close only terminals created inside this disposable fixture. Normal Hydra
# teardown correctly preserves dirty worktrees; fixture teardown must still
# release their PTYs before removing files or retaining failure evidence.
test_tmux_fixture_cleanup() {
    _tfc_fixture="${1:?fixture directory required}"
    [ -d "$_tfc_fixture" ] || return 0
    _tfc_canonical="$(CDPATH='' cd -- "$_tfc_fixture" && pwd -P)" || return 1
    case "$_tfc_canonical" in /|/tmp|/private/tmp|"$HOME")
        printf 'Refusing broad terminal cleanup: %s\n' "$_tfc_canonical" >&2
        return 1 ;;
    esac
    command -v tmux >/dev/null 2>&1 || return 0
    _tfc_sessions="$(tmux list-sessions -F '#{session_id} #{session_path}' 2>/dev/null)" || return 0
    _tfc_failed=0
    while IFS=' ' read -r _tfc_id _tfc_path; do
        [ -n "$_tfc_id" ] || continue
        case "$_tfc_path" in
            "$_tfc_fixture"|"$_tfc_fixture"/*|"$_tfc_canonical"|"$_tfc_canonical"/*)
                # Stable session IDs avoid prefix matches and name reuse. A
                # vanished session is already clean; a surviving one is failure.
                tmux kill-session -t "$_tfc_id" 2>/dev/null || :
                if tmux has-session -t "$_tfc_id" 2>/dev/null; then
                    printf 'Fixture terminal cleanup failed: %s (%s)\n' "$_tfc_id" "$_tfc_path" >&2
                    _tfc_failed=1
                fi ;;
        esac
    done <<EOF
$_tfc_sessions
EOF
    return "$_tfc_failed"
}
