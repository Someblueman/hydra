#!/bin/sh
# Rate-limited local lifecycle notification sinks.

notify_config_file() {
    printf '%s/notifications\n' "$(project_host_dir)"
}

notify_validate_event() {
    case "$1" in
        lifecycle.started|lifecycle.declared|lifecycle.observed|lifecycle.resumed|lifecycle.teardown-requested|lifecycle.torn-down|lifecycle.spawn-failed) return 0 ;;
    esac
    return 1
}

notify_enable() {
    _ne_event="$1"
    _ne_sink="$2"
    _ne_interval="$3"
    notify_validate_event "$_ne_event" || return 1
    case "$_ne_sink" in terminal|desktop) ;; *) return 1 ;; esac
    case "$_ne_interval" in ''|*[!0-9]*) return 1 ;; esac
    _ne_file="$(notify_config_file)" || return 1
    mkdir -p "$(dirname "$_ne_file")" || return 1
    _ne_lock="notify_config_$(printf '%s' "$_ne_file" | cksum | cut -d' ' -f1)"
    acquire_lock "$_ne_lock" "notification configuration" || return 1
    _ne_tmp="$(mktemp_adjacent "$_ne_file")" || { release_lock "$_ne_lock"; return 1; }
    if [ -f "$_ne_file" ]; then
        if ! awk -v event="$_ne_event" '$1 != event { print }' "$_ne_file" > "$_ne_tmp"; then
            rm -f "$_ne_tmp"
            release_lock "$_ne_lock"
            return 1
        fi
    fi
    printf '%s %s %s\n' "$_ne_event" "$_ne_sink" "$_ne_interval" >> "$_ne_tmp"
    chmod 600 "$_ne_tmp" 2>/dev/null || true
    if ! atomic_replace "$_ne_file" "$_ne_tmp"; then
        release_lock "$_ne_lock"
        return 1
    fi
    release_lock "$_ne_lock"
}

notify_disable() {
    _nd_event="$1"
    notify_validate_event "$_nd_event" || return 1
    _nd_file="$(notify_config_file)" || return 1
    [ -f "$_nd_file" ] || return 0
    _nd_lock="notify_config_$(printf '%s' "$_nd_file" | cksum | cut -d' ' -f1)"
    acquire_lock "$_nd_lock" "notification configuration" || return 1
    _nd_tmp="$(mktemp_adjacent "$_nd_file")" || { release_lock "$_nd_lock"; return 1; }
    if ! awk -v event="$_nd_event" '$1 != event { print }' "$_nd_file" > "$_nd_tmp"; then
        rm -f "$_nd_tmp"
        release_lock "$_nd_lock"
        return 1
    fi
    chmod 600 "$_nd_tmp" 2>/dev/null || true
    if ! atomic_replace "$_nd_file" "$_nd_tmp"; then
        release_lock "$_nd_lock"
        return 1
    fi
    release_lock "$_nd_lock"
}

notify_deliver() {
    _ndl_sink="$1"
    _ndl_message="$2"
    case "$_ndl_sink" in
        terminal) printf '[notify] %s\n' "$_ndl_message" >&2 ;;
        desktop)
            if command -v osascript >/dev/null 2>&1; then
                osascript - "$_ndl_message" >/dev/null 2>&1 <<'APPLESCRIPT'
on run argv
  display notification (item 1 of argv) with title "Hydra"
end run
APPLESCRIPT
            elif command -v notify-send >/dev/null 2>&1; then
                notify-send Hydra "$_ndl_message" >/dev/null 2>&1
            else
                printf '[notify] %s\n' "$_ndl_message" >&2
            fi
            ;;
    esac
}

notify_event() {
    _nev_event="$1"
    _nev_project="$2"
    _nev_head="$3"
    notify_validate_event "$_nev_event" || return 0
    _nev_file="$(notify_config_file 2>/dev/null || true)"
    [ -f "$_nev_file" ] || return 0
    _nev_head_dir="$(state_v2_head_dir "$_nev_project" "$_nev_head" 2>/dev/null || true)"
    _nev_branch="$(sed -n '1p' "$_nev_head_dir/branch" 2>/dev/null || true)"
    while IFS=' ' read -r _nev_config_event _nev_sink _nev_interval _nev_extra; do
        [ "$_nev_config_event" = "$_nev_event" ] || continue
        [ -z "$_nev_extra" ] || continue
        _nev_stamp_dir="$HYDRA_STATE_V2_ROOT/projects/$_nev_project/notifications"
        mkdir -p "$_nev_stamp_dir" || continue
        _nev_key="$(printf '%s|%s' "$_nev_event" "$_nev_sink" | cksum | cut -d' ' -f1)"
        _nev_stamp="$_nev_stamp_dir/$_nev_key"
        _nev_now="$(date +%s)"
        _nev_last="$(sed -n '1p' "$_nev_stamp" 2>/dev/null || echo 0)"
        case "$_nev_last:$_nev_interval" in *[!0-9:]*) continue ;; esac
        [ $((_nev_now - _nev_last)) -ge "$_nev_interval" ] || continue
        _nev_lock="notify_${_nev_project}_${_nev_key}"
        acquire_lock "$_nev_lock" "notification rate limit" "$_nev_head" || continue
        state_v2_write_scalar "$_nev_stamp" "$_nev_now" || { release_lock "$_nev_lock"; continue; }
        release_lock "$_nev_lock"
        notify_deliver "$_nev_sink" "${_nev_branch:-$_nev_head}: $_nev_event" || true
    done < "$_nev_file"
}

cmd_notify() {
    _cn_action="${1:-list}"
    [ $# -eq 0 ] || shift
    case "$_cn_action" in
        enable)
            _cn_event="${1:-}"
            [ -n "$_cn_event" ] || { echo "Usage: hydra notify enable <event> [--sink terminal|desktop] [--interval seconds]" >&2; return 1; }
            shift
            _cn_sink=desktop
            _cn_interval=60
            while [ $# -gt 0 ]; do
                case "$1" in
                    --sink) [ $# -ge 2 ] || return 1; _cn_sink="$2"; shift 2 ;;
                    --interval) [ $# -ge 2 ] || return 1; _cn_interval="$2"; shift 2 ;;
                    *) return 1 ;;
                esac
            done
            notify_enable "$_cn_event" "$_cn_sink" "$_cn_interval" || { echo "Error: invalid notification configuration" >&2; return 1; }
            echo "Enabled $_cn_event -> $_cn_sink (minimum ${_cn_interval}s)"
            ;;
        disable)
            [ $# -eq 1 ] || { echo "Usage: hydra notify disable <event>" >&2; return 1; }
            notify_disable "$1" || return 1
            echo "Disabled $1"
            ;;
        list)
            [ $# -eq 0 ] || return 1
            _cn_file="$(notify_config_file)" || return 1
            if [ -s "$_cn_file" ]; then cat "$_cn_file"; else echo "No lifecycle notifications enabled"; fi
            ;;
        test)
            [ $# -eq 1 ] || { echo "Usage: hydra notify test <terminal|desktop>" >&2; return 1; }
            case "$1" in terminal|desktop) notify_deliver "$1" "notification test" ;; *) return 1 ;; esac
            ;;
        *) echo "Usage: hydra notify <enable|disable|list|test>" >&2; return 1 ;;
    esac
}
