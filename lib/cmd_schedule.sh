#!/bin/sh
# hydra schedule: host-timer schedules that submit work with a dedup token.

cmd_schedule_help() {
    # shellcheck disable=SC2016 # Command names are quoted literally.
    printf '%s\n' \
        'Usage: hydra schedule add <name> --every <N>m|<N>h|<N>d --workflow <id|path>' \
        '                           [--missed skip|catch-up] [--catch-up-limit N] [--reboot-recovery]' \
        '       hydra schedule add <name> --every <N>m|<N>h|<N>d --pool <pool> [--workflow <id|path>] [...]' \
        '       hydra schedule list' \
        '       hydra schedule remove <name>' \
        '       hydra schedule run-now <name> [--token <token>]' \
        '       hydra schedule status <name> [--format crontab|systemd|launchd|all]' \
        '' \
        'Every form accepts --json. `add` and `status` print the exact host-timer entry' \
        '(crontab line, systemd timer/service units, launchd plist) for you to install;' \
        'Hydra never installs a daemon. Each firing runs `hydra schedule run-now`, whose' \
        'dedup token is the current slot (floor(now / interval)), so a double timer fire' \
        'and a run-now in the same slot submit once. `--workflow` runs the workflow in' \
        'the firing process (the execution owner); `--pool P --workflow W` only adds a' \
        'sealed task to the pool; `--pool P` alone runs the pool worker loop.' \
        'Missed slots are skipped (default) or caught up, bounded by --catch-up-limit.' \
        '--reboot-recovery opts into @reboot / Persistent=true / RunAtLoad entries.'
}

cmd_schedule_timer_json() {
    printf '{"crontab":"%s","systemd":"%s","launchd":"%s"}' \
        "$(json_escape "$(schedule_timer_text "$1" crontab)")" \
        "$(json_escape "$(schedule_timer_text "$1" systemd)")" \
        "$(json_escape "$(schedule_timer_text "$1" launchd)")"
}

cmd_schedule_owner() {
    case "$(pool_read "$1/kind")" in
        pool-add) printf 'pool-worker\n' ;;
        *) printf 'timer-process\n' ;;
    esac
}

cmd_schedule_status_json() {
    _cssj_dir="$1"
    _cssj_firings="" _cssj_n=0
    for _cssj_fdir in $(find "$_cssj_dir/firings" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | LC_ALL=C sort); do
        _cssj_n=$((_cssj_n + 1))
        [ "$_cssj_n" -le 32 ] || break
        _cssj_firings="$_cssj_firings${_cssj_firings:+,}$(schedule_firing_json "$_cssj_dir" "$(basename "$_cssj_fdir")")"
    done
    _cssj_last="$(pool_read "$_cssj_dir/last-slot")"
    _cssj_interval="$(pool_read "$_cssj_dir/interval-seconds")"
    _cssj_now="$(pool_now)"
    _cssj_next=$((_cssj_now - _cssj_now % _cssj_interval + _cssj_interval))
    _cssj_workflow="$(pool_read "$_cssj_dir/workflow")" _cssj_pool="$(pool_read "$_cssj_dir/pool")"
    printf '{"schedule":"%s","state":"%s","created_at":%s,"manifest_digest":"%s","every":"%s","interval_seconds":%s,"submission":{"kind":"%s","workflow":%s,"pool":%s},"execution_owner":"%s","missed_policy":"%s","catch_up_limit":%s,"reboot_recovery":%s,"last_slot":%s,"next_slot":%s,"missed_total":%s,"firings":[%s],"timer":%s}' \
        "$(json_escape "$(pool_read "$_cssj_dir/schedule-id")")" "$(pool_read "$_cssj_dir/state")" "$(pool_read "$_cssj_dir/created-at")" \
        "$(pool_read "$_cssj_dir/manifest-digest")" "$(pool_read "$_cssj_dir/every")" "$_cssj_interval" "$(pool_read "$_cssj_dir/kind")" \
        "$(json_string_or_null "$([ "$_cssj_workflow" = - ] || printf '%s' "$_cssj_workflow")")" \
        "$(json_string_or_null "$([ "$_cssj_pool" = - ] || printf '%s' "$_cssj_pool")")" \
        "$(cmd_schedule_owner "$_cssj_dir")" "$(pool_read "$_cssj_dir/missed-policy")" "$(pool_read "$_cssj_dir/catch-up-limit")" \
        "$(pool_read "$_cssj_dir/reboot-recovery")" "${_cssj_last:-null}" "$_cssj_next" "$(pool_read "$_cssj_dir/missed-total")" \
        "$_cssj_firings" "$(cmd_schedule_timer_json "$_cssj_dir")"
}

cmd_schedule_status_text() {
    _csst_dir="$1" _csst_format="$2"
    printf 'Schedule %s: %s, every %s, %s' "$(pool_read "$_csst_dir/schedule-id")" "$(pool_read "$_csst_dir/state")" "$(pool_read "$_csst_dir/every")" "$(pool_read "$_csst_dir/kind")"
    [ "$(pool_read "$_csst_dir/workflow")" = - ] || printf ' workflow=%s' "$(pool_read "$_csst_dir/workflow")"
    [ "$(pool_read "$_csst_dir/pool")" = - ] || printf ' pool=%s' "$(pool_read "$_csst_dir/pool")"
    printf '\n  execution owner: %s; missed runs: %s (limit %s); reboot recovery: %s; last slot: %s; missed total: %s\n' \
        "$(cmd_schedule_owner "$_csst_dir")" "$(pool_read "$_csst_dir/missed-policy")" "$(pool_read "$_csst_dir/catch-up-limit")" \
        "$(pool_read "$_csst_dir/reboot-recovery")" "$(pool_read "$_csst_dir/last-slot" | sed 's/^$/none/')" "$(pool_read "$_csst_dir/missed-total")"
    for _csst_fdir in $(find "$_csst_dir/firings" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | LC_ALL=C sort); do
        printf '  firing %s: %s (slot %s, %s, exit %s, run %s, task %s)\n' "$(basename "$_csst_fdir")" "$(pool_read "$_csst_fdir/state")" \
            "$(pool_read "$_csst_fdir/slot")" "$(pool_read "$_csst_fdir/trigger")" "$(pool_read "$_csst_fdir/exit-code" | sed 's/^$/-/')" \
            "$(pool_read "$_csst_fdir/run-id" | sed 's/^$/-/')" "$(pool_read "$_csst_fdir/task-id" | sed 's/^$/-/')"
    done
    cmd_schedule_timer_print "$_csst_dir" "$_csst_format"
}

cmd_schedule_timer_print() {
    case "$2" in
        all) set -- "$1" crontab systemd launchd ;;
        default) set -- "$1" crontab "$(schedule_timer_default_format)" ;;
        *) set -- "$1" "$2" ;;
    esac
    _cstp_dir="$1"; shift
    for _cstp_format in "$@"; do
        printf '\nHost timer entry (%s), install it yourself:\n' "$_cstp_format"
        schedule_timer_text "$_cstp_dir" "$_cstp_format" || return 1
    done
}

cmd_schedule_format_ok() {
    case "$1" in crontab|systemd|launchd|all|default) return 0 ;; esac
    cli_error schedule invalid_input "--format must be crontab, systemd, launchd or all" "run hydra schedule --help"
    return 1
}

cmd_schedule_add() {
    _csa_name="${1:-}"; [ $# -eq 0 ] || shift
    _csa_every="" _csa_workflow="" _csa_pool="" _csa_policy=skip _csa_limit=4 _csa_reboot=false _csa_format=default
    while [ $# -gt 0 ]; do
        case "$1" in
            --every) [ -n "${2:-}" ] || return 1; _csa_every="$2"; shift 2 ;;
            --workflow) [ -n "${2:-}" ] || return 1; _csa_workflow="$2"; shift 2 ;;
            --pool) [ -n "${2:-}" ] || return 1; _csa_pool="$2"; shift 2 ;;
            --missed) case "${2:-}" in skip|catch-up) ;; *) cli_error schedule invalid_input "--missed must be skip or catch-up" "run hydra schedule --help"; return 1 ;; esac; _csa_policy="$2"; shift 2 ;;
            --catch-up-limit) pool_valid_number "${2:-}" 1 16 || { cli_error schedule invalid_input "--catch-up-limit must be between 1 and 16" "run hydra schedule --help"; return 1; }; _csa_limit="$2"; shift 2 ;;
            --reboot-recovery) _csa_reboot=true; shift ;;
            --format) cmd_schedule_format_ok "${2:-}" || return 1; _csa_format="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error schedule invalid_input "unknown option '$1'" "run hydra schedule --help"; return 1 ;;
        esac
    done
    pool_valid_name "$_csa_name" || { cli_error schedule invalid_input "invalid schedule name '$_csa_name'" "use lowercase letters, digits, single hyphens or underscores"; return 1; }
    schedule_parse_every "$_csa_every" || { cli_error schedule invalid_input "--every must be a divisor of the hour (e.g. 15m), of the day (e.g. 6h) or 1-28 days (e.g. 1d)" "run hydra schedule --help"; return 1; }
    if [ -n "$_csa_workflow" ]; then
        _csa_workflow="$(cd "$POOL_REPO_ROOT" && workflow_resolve "$_csa_workflow")" || return 1
        (cd "$POOL_REPO_ROOT" && workflow_require_trust "$_csa_workflow") || return 1
    fi
    if [ -n "$_csa_pool" ]; then
        pool_dir "$_csa_pool" >/dev/null || return 1
        if [ -n "$_csa_workflow" ]; then _csa_kind=pool-add; else _csa_kind=pool-run; fi
    elif [ -n "$_csa_workflow" ]; then
        _csa_kind=workflow
    else
        cli_error schedule invalid_input "--workflow and/or --pool is required" "run hydra schedule --help"
        return 1
    fi
    schedule_create "$_csa_name" "$_csa_every" "$_csa_kind" "${_csa_workflow:--}" "${_csa_pool:--}" "$_csa_policy" "$_csa_limit" "$_csa_reboot" || return 1
    _csa_dir="$SCHEDULE_ROOT/$_csa_name"
    if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then
        json_success "schedule add" "$(cmd_schedule_status_json "$_csa_dir")"
    else
        printf 'Created schedule %s (every %s, %s). Hydra installs no timer:\n' "$_csa_name" "$_csa_every" "$_csa_kind"
        cmd_schedule_timer_print "$_csa_dir" "$_csa_format"
    fi
}

cmd_schedule_run_now() {
    _csr_name="${1:-}"; [ $# -eq 0 ] || shift
    _csr_dir="$(schedule_dir "$_csr_name")" || return 1
    _csr_token=-
    while [ $# -gt 0 ]; do
        case "$1" in
            --token) [ -n "${2:-}" ] || return 1; _csr_token="$2"; shift 2 ;;
            -j|--json) shift ;;
            *) cli_error schedule invalid_input "unknown option '$1'" "run hydra schedule --help"; return 1 ;;
        esac
    done
    if [ "$_csr_token" != - ]; then
        case "$_csr_token" in
            *[!a-z0-9_-]*|[!a-z0-9]*) cli_error schedule invalid_input "invalid token '$_csr_token'" "use lowercase letters, digits, hyphens or underscores"; return 1 ;;
        esac
        [ "${#_csr_token}" -le 128 ] || { cli_error schedule invalid_input "token is longer than 128 characters" "use a shorter token"; return 1; }
    fi
    schedule_lock "$_csr_name" || return 1
    schedule_plan_firings "$_csr_dir" "$_csr_token" || { schedule_unlock "$_csr_name"; return 1; }
    schedule_unlock "$_csr_name"
    _csr_fired="" _csr_status=0
    for _csr_entry in $SCHEDULE_FIRE; do
        _csr_t="${_csr_entry%%:*}"
        schedule_submit "$_csr_dir" "$_csr_t" || { _csr_status=1; continue; }
        [ "$SCHEDULE_SUBMIT_CODE" -eq 0 ] || _csr_status=1
        _csr_fired="$_csr_fired${_csr_fired:+,}$(schedule_firing_json "$_csr_dir" "$_csr_t")"
        [ "$CMD_SCHEDULE_JSON" -eq 1 ] || printf 'Fired %s: exit %s%s%s\n' "$_csr_t" "$SCHEDULE_SUBMIT_CODE" "${SCHEDULE_SUBMIT_RUN:+ run }$SCHEDULE_SUBMIT_RUN" "${SCHEDULE_SUBMIT_TASK:+ task }$SCHEDULE_SUBMIT_TASK"
    done
    _csr_dedup=""
    for _csr_t in $SCHEDULE_DEDUP; do
        _csr_dedup="$_csr_dedup${_csr_dedup:+,}\"$(json_escape "$_csr_t")\""
        [ "$CMD_SCHEDULE_JSON" -eq 1 ] || printf 'Deduplicated %s: already fired for this slot\n' "$_csr_t"
    done
    if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then
        json_success "schedule run-now" "{\"schedule\":\"$(json_escape "$_csr_name")\",\"slot\":$SCHEDULE_SLOT,\"fired\":[$_csr_fired],\"deduplicated\":[$_csr_dedup],\"missed\":{\"policy\":\"$(pool_read "$_csr_dir/missed-policy")\",\"detected\":$SCHEDULE_MISSED,\"caught_up\":$SCHEDULE_CAUGHT_UP,\"skipped\":$SCHEDULE_SKIPPED}}"
    elif [ "$SCHEDULE_MISSED" -gt 0 ]; then
        printf 'Missed slots: %s detected, %s caught up, %s skipped (%s)\n' "$SCHEDULE_MISSED" "$SCHEDULE_CAUGHT_UP" "$SCHEDULE_SKIPPED" "$(pool_read "$_csr_dir/missed-policy")"
    fi
    return "$_csr_status"
}

cmd_schedule_list() {
    _csl_first=1
    [ "$CMD_SCHEDULE_JSON" -eq 1 ] && printf '{"schema_version":1,"ok":true,"command":"schedule list","data":{"schedules":['
    for _csl_dir in "$SCHEDULE_ROOT"/*; do
        [ -f "$_csl_dir/schedule-id" ] || continue
        if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then
            [ "$_csl_first" -eq 1 ] || printf ','
            printf '{"schedule":"%s","state":"%s","every":"%s","kind":"%s","last_slot":%s,"missed_total":%s}' \
                "$(json_escape "$(pool_read "$_csl_dir/schedule-id")")" "$(pool_read "$_csl_dir/state")" "$(pool_read "$_csl_dir/every")" \
                "$(pool_read "$_csl_dir/kind")" "$(pool_read "$_csl_dir/last-slot" | sed 's/^$/null/')" "$(pool_read "$_csl_dir/missed-total")"
        else
            printf '%s\t%s\tevery=%s\t%s\tlast-slot=%s\tmissed=%s\n' "$(pool_read "$_csl_dir/schedule-id")" "$(pool_read "$_csl_dir/state")" \
                "$(pool_read "$_csl_dir/every")" "$(pool_read "$_csl_dir/kind")" "$(pool_read "$_csl_dir/last-slot" | sed 's/^$/none/')" "$(pool_read "$_csl_dir/missed-total")"
        fi
        _csl_first=0
    done
    if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then printf ']}}\n'; elif [ "$_csl_first" -eq 1 ]; then echo "No schedules"; fi
}

cmd_schedule_remove() {
    _csrm_name="${1:-}"
    _csrm_dir="$(schedule_dir "$_csrm_name")" || return 1
    schedule_lock "$_csrm_name" || return 1
    rm -rf "$_csrm_dir"
    schedule_unlock "$_csrm_name"
    if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then
        json_success "schedule remove" "{\"schedule\":\"$(json_escape "$_csrm_name")\",\"removed\":true}"
    else
        printf 'Removed schedule %s. Remove its host timer entry yourself (crontab -e, systemctl --user disable --now hydra-schedule-%s.timer, or launchctl unload).\n' "$_csrm_name" "$_csrm_name"
    fi
}

cmd_schedule() {
    CMD_SCHEDULE_JSON=0
    for _cs_arg in "$@"; do
        case "$_cs_arg" in -j|--json) CMD_SCHEDULE_JSON=1 ;; esac
    done
    _cs_action="${1:-}"
    [ $# -eq 0 ] || shift
    case "$_cs_action" in
        -h|--help|'') cmd_schedule_help; [ -n "$_cs_action" ]; return ;;
        add|list|remove|run-now|status) ;;
        *) cli_error schedule invalid_input "unknown schedule command '$_cs_action'" "run hydra schedule --help"; return 1 ;;
    esac
    schedule_root || return 1
    _load_lib schedule_timer
    case "$_cs_action" in
        add) cmd_schedule_add "$@" ;;
        list) cmd_schedule_list ;;
        remove) cmd_schedule_remove "$@" ;;
        run-now) cmd_schedule_run_now "$@" ;;
        status)
            _cs_dir="$(schedule_dir "${1:-}")" || return 1
            [ $# -eq 0 ] || shift
            _cs_format=default
            while [ $# -gt 0 ]; do
                case "$1" in
                    --format) cmd_schedule_format_ok "${2:-}" || return 1; _cs_format="$2"; shift 2 ;;
                    -j|--json) shift ;;
                    *) cli_error schedule invalid_input "unknown option '$1'" "run hydra schedule --help"; return 1 ;;
                esac
            done
            if [ "$CMD_SCHEDULE_JSON" -eq 1 ]; then json_success "schedule status" "$(cmd_schedule_status_json "$_cs_dir")"; else cmd_schedule_status_text "$_cs_dir" "$_cs_format"; fi
            ;;
    esac
}
