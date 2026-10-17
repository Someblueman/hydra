#!/bin/sh
# Exact host-timer entries for a schedule: a crontab line, systemd timer and
# service units, and a launchd property list. Hydra prints them; the user
# installs them. Every entry runs the same public command,
# `hydra schedule run-now <name>`, from the repository with HYDRA_HOME set.

# Paths are embedded literally in three different quoting dialects; refuse
# characters any of them would reinterpret instead of guessing an escape.
schedule_timer_path_ok() {
    case "$1" in
        ''|*"'"*|*'"'*|*"\\"*|*'%'*|*'<'*|*'>'*|*'&'*|*'$'*|*'`'*|*' '*|*'	'*|*'
'*) return 1 ;;
    esac
}

schedule_timer_load() {
    _stl_dir="$1"
    SCHEDULE_TIMER_NAME="$(pool_read "$_stl_dir/schedule-id")"
    SCHEDULE_TIMER_EVERY="$(pool_read "$_stl_dir/every")"
    SCHEDULE_TIMER_INTERVAL="$(pool_read "$_stl_dir/interval-seconds")"
    SCHEDULE_TIMER_REBOOT="$(pool_read "$_stl_dir/reboot-recovery")"
    SCHEDULE_TIMER_REPO="$POOL_REPO_ROOT"
    SCHEDULE_TIMER_HOME="$HYDRA_HOME"
    SCHEDULE_TIMER_BIN="${HYDRA_BIN_PATH:-hydra}"
    schedule_parse_every "$SCHEDULE_TIMER_EVERY" || return 1
    for _stl_path in "$SCHEDULE_TIMER_REPO" "$SCHEDULE_TIMER_HOME" "$SCHEDULE_TIMER_BIN"; do
        schedule_timer_path_ok "$_stl_path" || {
            cli_error schedule unsupported_path "path contains characters timer entries cannot quote safely: $_stl_path" "move the repository, HYDRA_HOME or the hydra binary to a plain path"
            return 1
        }
    done
}

schedule_timer_cron_fields() {
    case "$SCHEDULE_UNIT" in
        m) printf '*/%s * * * *\n' "$SCHEDULE_COUNT" ;;
        h) if [ "$SCHEDULE_COUNT" -eq 1 ]; then printf '0 * * * *\n'; else printf '0 */%s * * *\n' "$SCHEDULE_COUNT"; fi ;;
        d) if [ "$SCHEDULE_COUNT" -eq 1 ]; then printf '0 0 * * *\n'; else printf '0 0 */%s * *\n' "$SCHEDULE_COUNT"; fi ;;
    esac
}

schedule_timer_on_calendar() {
    case "$SCHEDULE_UNIT" in
        m) printf '*-*-* *:0/%s:00\n' "$SCHEDULE_COUNT" ;;
        h) printf '*-*-* 0/%s:00:00\n' "$SCHEDULE_COUNT" ;;
        d) if [ "$SCHEDULE_COUNT" -eq 1 ]; then printf '*-*-* 00:00:00\n'; else printf '*-*-1/%s 00:00:00\n' "$SCHEDULE_COUNT"; fi ;;
    esac
}

schedule_timer_command() {
    printf "cd '%s' && HYDRA_HOME='%s' HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 '%s' schedule run-now '%s'" \
        "$SCHEDULE_TIMER_REPO" "$SCHEDULE_TIMER_HOME" "$SCHEDULE_TIMER_BIN" "$SCHEDULE_TIMER_NAME"
}

schedule_timer_crontab() {
    printf '# hydra schedule %s: every %s (install with crontab -e)\n' "$SCHEDULE_TIMER_NAME" "$SCHEDULE_TIMER_EVERY"
    printf '%s %s\n' "$(schedule_timer_cron_fields)" "$(schedule_timer_command)"
    [ "$SCHEDULE_TIMER_REBOOT" != true ] || printf '@reboot %s\n' "$(schedule_timer_command)"
}

schedule_timer_systemd() {
    _sts_persistent=false
    [ "$SCHEDULE_TIMER_REBOOT" != true ] || _sts_persistent=true
    printf '%s\n' \
        "# ~/.config/systemd/user/hydra-schedule-$SCHEDULE_TIMER_NAME.timer" \
        '[Unit]' \
        "Description=Hydra schedule $SCHEDULE_TIMER_NAME (every $SCHEDULE_TIMER_EVERY)" \
        '' \
        '[Timer]' \
        "OnCalendar=$(schedule_timer_on_calendar)" \
        "Persistent=$_sts_persistent" \
        "Unit=hydra-schedule-$SCHEDULE_TIMER_NAME.service" \
        '' \
        '[Install]' \
        'WantedBy=timers.target' \
        '' \
        "# ~/.config/systemd/user/hydra-schedule-$SCHEDULE_TIMER_NAME.service" \
        '[Unit]' \
        "Description=Hydra schedule $SCHEDULE_TIMER_NAME firing" \
        '' \
        '[Service]' \
        'Type=oneshot' \
        "WorkingDirectory=$SCHEDULE_TIMER_REPO" \
        "Environment=HYDRA_HOME=$SCHEDULE_TIMER_HOME" \
        'Environment=HYDRA_NONINTERACTIVE=1' \
        'Environment=HYDRA_SKIP_AI=1' \
        "ExecStart=$SCHEDULE_TIMER_BIN schedule run-now $SCHEDULE_TIMER_NAME"
}

schedule_timer_launchd() {
    _stld_load='<false/>'
    [ "$SCHEDULE_TIMER_REBOOT" != true ] || _stld_load='<true/>'
    printf '%s\n' \
        "<!-- ~/Library/LaunchAgents/com.hydra.schedule.$SCHEDULE_TIMER_NAME.plist -->" \
        '<?xml version="1.0" encoding="UTF-8"?>' \
        '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
        '<plist version="1.0">' \
        '<dict>' \
        '  <key>Label</key>' \
        "  <string>com.hydra.schedule.$SCHEDULE_TIMER_NAME</string>" \
        '  <key>ProgramArguments</key>' \
        '  <array>' \
        "    <string>$SCHEDULE_TIMER_BIN</string>" \
        '    <string>schedule</string>' \
        '    <string>run-now</string>' \
        "    <string>$SCHEDULE_TIMER_NAME</string>" \
        '  </array>' \
        '  <key>WorkingDirectory</key>' \
        "  <string>$SCHEDULE_TIMER_REPO</string>" \
        '  <key>EnvironmentVariables</key>' \
        '  <dict>' \
        '    <key>HYDRA_HOME</key>' \
        "    <string>$SCHEDULE_TIMER_HOME</string>" \
        '    <key>HYDRA_NONINTERACTIVE</key>' \
        '    <string>1</string>' \
        '    <key>HYDRA_SKIP_AI</key>' \
        '    <string>1</string>' \
        '  </dict>' \
        '  <key>StartInterval</key>' \
        "  <integer>$SCHEDULE_TIMER_INTERVAL</integer>" \
        '  <key>RunAtLoad</key>' \
        "  $_stld_load" \
        '</dict>' \
        '</plist>'
}

# Usage: schedule_timer_text <schedule-dir> <crontab|systemd|launchd>
schedule_timer_text() {
    schedule_timer_load "$1" || return 1
    case "$2" in
        crontab) schedule_timer_crontab ;;
        systemd) schedule_timer_systemd ;;
        launchd) schedule_timer_launchd ;;
        *) return 1 ;;
    esac
}

schedule_timer_default_format() {
    case "$(uname -s 2>/dev/null)" in
        Darwin) printf 'launchd\n' ;;
        *) printf 'systemd\n' ;;
    esac
}
