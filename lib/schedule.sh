#!/bin/sh
# Host-timer schedules: durable records, slot-based dedup tokens, bounded
# missed-run policies and firing through the public submission commands.
#
# Hydra installs no daemon. The user installs the printed crontab, systemd or
# launchd entry; each firing runs `hydra schedule run-now <name>`, whose
# dedup token is the schedule's current slot, so a double timer fire or a
# manual run-now in the same slot submits exactly once.
#
# Results are handed to callers through SCHEDULE_* variables.
# shellcheck disable=SC2034

schedule_root() {
    pool_project || return 1
    SCHEDULE_ROOT="$POOL_PROJECT_DIR/schedules"
    export SCHEDULE_ROOT
}

schedule_dir() {
    pool_valid_name "$1" || {
        cli_error schedule invalid_input "invalid schedule name '$1'" "use lowercase letters, digits, single hyphens or underscores"
        return 1
    }
    [ -d "$SCHEDULE_ROOT/$1" ] || {
        cli_error schedule not_found "schedule '$1' does not exist" "create it with hydra schedule add $1"
        return 1
    }
    printf '%s/%s\n' "$SCHEDULE_ROOT" "$1"
}

schedule_lock() {
    _sl_name="schedule_${POOL_PROJECT}_$1"
    _sl_i=0
    while :; do
        try_lock "$_sl_name" "schedule $1" && return 0
        remove_stale_lock_dir "$HYDRA_HOME/locks/$_sl_name.lock" 2>/dev/null && continue
        _sl_i=$((_sl_i + 1))
        [ "$_sl_i" -lt "${HYDRA_POOL_LOCK_TRIES:-200}" ] || break
        sleep 0.05 2>/dev/null || sleep 1
    done
    cli_error schedule busy "schedule '$1' is being fired or changed by another process" "retry when it finishes"
    return 1
}

schedule_unlock() {
    release_lock "schedule_${POOL_PROJECT}_$1"
}

schedule_event() {
    _se_dir="$1" _se_type="$2" _se_detail="${3:-}"
    _se_file="$_se_dir/events.jsonl"
    hydra_private_touch "$_se_file" || return 1
    _se_seq="$(awk 'END { print NR + 1 }' "$_se_file" 2>/dev/null || printf 1)"
    printf '{"schema_version":1,"sequence":%s,"occurred_at":"%s","schedule":"%s","type":"%s","detail":"%s"}\n' \
        "$_se_seq" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(json_escape "$(pool_read "$_se_dir/schedule-id")")" \
        "$(json_escape "$_se_type")" "$(json_escape "$_se_detail")" >> "$_se_file"
}

# Parse `--every` into SCHEDULE_INTERVAL (seconds), SCHEDULE_UNIT and
# SCHEDULE_COUNT. Units must divide their parent so host timers can express
# the same cadence exactly.
schedule_parse_every() {
    case "$1" in
        *[!0-9mhd]*|''|[!1-9]*) return 1 ;;
    esac
    SCHEDULE_COUNT="${1%?}" SCHEDULE_UNIT="${1#"${1%?}"}"
    case "$SCHEDULE_COUNT" in ''|*[!0-9]*) return 1 ;; esac
    case "$SCHEDULE_UNIT" in
        m) [ "$SCHEDULE_COUNT" -lt 60 ] && [ $((60 % SCHEDULE_COUNT)) -eq 0 ] || return 1; SCHEDULE_INTERVAL=$((SCHEDULE_COUNT * 60)) ;;
        h) [ "$SCHEDULE_COUNT" -lt 24 ] && [ $((24 % SCHEDULE_COUNT)) -eq 0 ] || return 1; SCHEDULE_INTERVAL=$((SCHEDULE_COUNT * 3600)) ;;
        d) [ "$SCHEDULE_COUNT" -le 28 ] || return 1; SCHEDULE_INTERVAL=$((SCHEDULE_COUNT * 86400)) ;;
        *) return 1 ;;
    esac
}

# Usage: schedule_create <name> <every> <kind> <workflow|-> <pool|-> <missed-policy> <catch-up-limit> <reboot-recovery>
schedule_create() {
    _sc_name="$1" _sc_every="$2" _sc_kind="$3" _sc_workflow="$4" _sc_pool="$5" _sc_policy="$6" _sc_limit="$7" _sc_reboot="$8"
    schedule_parse_every "$_sc_every" || return 1
    hydra_private_mkdir "$SCHEDULE_ROOT" || return 1
    [ ! -e "$SCHEDULE_ROOT/$_sc_name" ] || {
        cli_error schedule already_exists "schedule '$_sc_name' already exists" "remove it first or choose another name"
        return 1
    }
    _sc_tmp="$(mktemp -d "$SCHEDULE_ROOT/.schedule.XXXXXX")" || return 1
    chmod 700 "$_sc_tmp" 2>/dev/null || true
    _sc_created="$(pool_now)"
    hydra_private_mkdir "$_sc_tmp/firings" || { rm -rf "$_sc_tmp"; return 1; }
    hydra_private_touch "$_sc_tmp/events.jsonl" "$_sc_tmp/manifest.json" || { rm -rf "$_sc_tmp"; return 1; }
    printf '{"schema_version":1,"schedule":"%s","project_id":"%s","created_at":%s,"every":"%s","interval_seconds":%s,"submission":{"kind":"%s","workflow":%s,"pool":%s},"missed_policy":"%s","catch_up_limit":%s,"reboot_recovery":%s}\n' \
        "$_sc_name" "$POOL_PROJECT" "$_sc_created" "$_sc_every" "$SCHEDULE_INTERVAL" "$_sc_kind" \
        "$(json_string_or_null "$([ "$_sc_workflow" = - ] || printf '%s' "$_sc_workflow")")" \
        "$(json_string_or_null "$([ "$_sc_pool" = - ] || printf '%s' "$_sc_pool")")" \
        "$_sc_policy" "$_sc_limit" "$_sc_reboot" > "$_sc_tmp/manifest.json"
    if ! pool_write "$_sc_tmp/schema-version" 1 ||
        ! pool_write "$_sc_tmp/schedule-id" "$_sc_name" ||
        ! pool_write "$_sc_tmp/project-id" "$POOL_PROJECT" ||
        ! pool_write "$_sc_tmp/state" enabled ||
        ! pool_write "$_sc_tmp/created-at" "$_sc_created" ||
        ! pool_write "$_sc_tmp/every" "$_sc_every" ||
        ! pool_write "$_sc_tmp/interval-seconds" "$SCHEDULE_INTERVAL" ||
        ! pool_write "$_sc_tmp/kind" "$_sc_kind" ||
        ! pool_write "$_sc_tmp/workflow" "$_sc_workflow" ||
        ! pool_write "$_sc_tmp/pool" "$_sc_pool" ||
        ! pool_write "$_sc_tmp/missed-policy" "$_sc_policy" ||
        ! pool_write "$_sc_tmp/catch-up-limit" "$_sc_limit" ||
        ! pool_write "$_sc_tmp/reboot-recovery" "$_sc_reboot" ||
        ! pool_write "$_sc_tmp/last-slot" "" ||
        ! pool_write "$_sc_tmp/missed-total" 0 ||
        ! pool_write "$_sc_tmp/manifest-digest" "$(hydra_hash < "$_sc_tmp/manifest.json")" ||
        ! mv "$_sc_tmp" "$SCHEDULE_ROOT/$_sc_name"; then
        rm -rf "$_sc_tmp"
        return 1
    fi
    schedule_event "$SCHEDULE_ROOT/$_sc_name" schedule.created "every=$_sc_every kind=$_sc_kind"
}

# Record one firing slot under the schedule lock. Sets SCHEDULE_FIRING_NEW to
# 1 when this call created it and 0 when the token already existed.
schedule_firing_record() {
    _sfr_dir="$1" _sfr_token="$2" _sfr_slot="$3" _sfr_trigger="$4"
    SCHEDULE_FIRING_NEW=0
    [ ! -e "$_sfr_dir/firings/$_sfr_token" ] || return 0
    hydra_private_mkdir "$_sfr_dir/firings/$_sfr_token" || return 1
    pool_write "$_sfr_dir/firings/$_sfr_token/slot" "$_sfr_slot" || return 1
    pool_write "$_sfr_dir/firings/$_sfr_token/fired-at" "$(pool_now)" || return 1
    pool_write "$_sfr_dir/firings/$_sfr_token/trigger" "$_sfr_trigger" || return 1
    pool_write "$_sfr_dir/firings/$_sfr_token/state" submitting || return 1
    pool_write "$_sfr_dir/firings/$_sfr_token/owner-pid" "$$" || return 1
    schedule_event "$_sfr_dir" schedule.fired "token=$_sfr_token slot=$_sfr_slot trigger=$_sfr_trigger"
    SCHEDULE_FIRING_NEW=1
}

# Usage: schedule_plan_firings <schedule-dir> <explicit-token|->
# Decides which slots fire now, under the lock. Sets SCHEDULE_FIRE (space
# separated "token:slot:trigger" entries), SCHEDULE_DEDUP (tokens), and the
# missed-run accounting SCHEDULE_MISSED, SCHEDULE_CAUGHT_UP, SCHEDULE_SKIPPED.
schedule_plan_firings() {
    _spf_dir="$1" _spf_token="$2"
    _spf_name="$(pool_read "$_spf_dir/schedule-id")"
    _spf_interval="$(pool_read "$_spf_dir/interval-seconds")"
    _spf_now="$(pool_now)"
    SCHEDULE_SLOT=$((_spf_now - _spf_now % _spf_interval))
    SCHEDULE_FIRE="" SCHEDULE_DEDUP="" SCHEDULE_MISSED=0 SCHEDULE_CAUGHT_UP=0 SCHEDULE_SKIPPED=0
    _spf_candidates=""
    if [ "$_spf_token" != - ]; then
        _spf_candidates="$_spf_token:$SCHEDULE_SLOT:run-now"
    else
        _spf_last="$(pool_read "$_spf_dir/last-slot")"
        _spf_policy="$(pool_read "$_spf_dir/missed-policy")"
        _spf_limit="$(pool_read "$_spf_dir/catch-up-limit")"
        if [ -n "$_spf_last" ] && [ "$_spf_last" -lt "$SCHEDULE_SLOT" ]; then
            SCHEDULE_MISSED=$(((SCHEDULE_SLOT - _spf_last) / _spf_interval - 1))
            [ "$SCHEDULE_MISSED" -ge 0 ] || SCHEDULE_MISSED=0
        fi
        if [ "$SCHEDULE_MISSED" -gt 0 ]; then
            if [ "$_spf_policy" = catch-up ]; then
                SCHEDULE_CAUGHT_UP="$SCHEDULE_MISSED"
                [ "$SCHEDULE_CAUGHT_UP" -le "$_spf_limit" ] || SCHEDULE_CAUGHT_UP="$_spf_limit"
            fi
            SCHEDULE_SKIPPED=$((SCHEDULE_MISSED - SCHEDULE_CAUGHT_UP))
            _spf_slot=$((SCHEDULE_SLOT - SCHEDULE_CAUGHT_UP * _spf_interval))
            while [ "$_spf_slot" -lt "$SCHEDULE_SLOT" ]; do
                _spf_candidates="$_spf_candidates${_spf_candidates:+ }$_spf_name-$_spf_slot:$_spf_slot:catch-up"
                _spf_slot=$((_spf_slot + _spf_interval))
            done
            pool_write "$_spf_dir/missed-total" "$(($(pool_read "$_spf_dir/missed-total") + SCHEDULE_SKIPPED))" || return 1
            schedule_event "$_spf_dir" schedule.missed "policy=$_spf_policy detected=$SCHEDULE_MISSED caught_up=$SCHEDULE_CAUGHT_UP skipped=$SCHEDULE_SKIPPED"
        fi
        _spf_candidates="$_spf_candidates${_spf_candidates:+ }$_spf_name-$SCHEDULE_SLOT:$SCHEDULE_SLOT:run-now"
    fi
    for _spf_entry in $_spf_candidates; do
        _spf_rest="${_spf_entry#*:}"
        schedule_firing_record "$_spf_dir" "${_spf_entry%%:*}" "${_spf_rest%%:*}" "${_spf_rest#*:}" || return 1
        if [ "$SCHEDULE_FIRING_NEW" -eq 1 ]; then
            SCHEDULE_FIRE="$SCHEDULE_FIRE${SCHEDULE_FIRE:+ }$_spf_entry"
        else
            SCHEDULE_DEDUP="$SCHEDULE_DEDUP${SCHEDULE_DEDUP:+ }${_spf_entry%%:*}"
            schedule_event "$_spf_dir" schedule.deduplicated "token=${_spf_entry%%:*}"
        fi
    done
    _spf_last="$(pool_read "$_spf_dir/last-slot")"
    if [ -z "$_spf_last" ] || [ "$_spf_last" -lt "$SCHEDULE_SLOT" ]; then
        pool_write "$_spf_dir/last-slot" "$SCHEDULE_SLOT" || return 1
    fi
}

# Submit one recorded firing through the public commands. The firing process
# is the execution owner of a workflow run; a pool submission only enqueues.
# Sets SCHEDULE_SUBMIT_CODE, SCHEDULE_SUBMIT_RUN and SCHEDULE_SUBMIT_TASK.
schedule_submit() {
    _ss_dir="$1" _ss_token="$2"
    _ss_fdir="$_ss_dir/firings/$_ss_token"
    _ss_kind="$(pool_read "$_ss_dir/kind")"
    _ss_workflow="$(pool_read "$_ss_dir/workflow")"
    _ss_pool="$(pool_read "$_ss_dir/pool")"
    SCHEDULE_SUBMIT_RUN="" SCHEDULE_SUBMIT_TASK=""
    hydra_private_touch "$_ss_fdir/stdout" "$_ss_fdir/stderr" || return 1
    case "$_ss_kind" in
        workflow) set -- workflow run "$_ss_workflow" ;;
        pool-add) set -- pool add "$_ss_pool" --workflow "$_ss_workflow" --seal --json ;;
        pool-run) set -- pool run "$_ss_pool" --worker "schedule-$(pool_read "$_ss_dir/schedule-id")" --json ;;
        *) return 1 ;;
    esac
    if (cd "$POOL_REPO_ROOT" && "$HYDRA_BIN_PATH" "$@") > "$_ss_fdir/stdout" 2> "$_ss_fdir/stderr"; then
        SCHEDULE_SUBMIT_CODE=0
    else
        SCHEDULE_SUBMIT_CODE=$?
    fi
    case "$_ss_kind" in
        workflow)
            _ss_run="$(sed -n '1p' "$_ss_fdir/stdout")"
            if hydra_valid_id "$_ss_run" 2>/dev/null; then
                SCHEDULE_SUBMIT_RUN="$_ss_run"
                pool_write "$_ss_fdir/run-id" "$_ss_run" || return 1
            fi
            ;;
        pool-add)
            SCHEDULE_SUBMIT_TASK="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$_ss_fdir/stdout" | sed -n '1p')"
            [ -z "$SCHEDULE_SUBMIT_TASK" ] || pool_write "$_ss_fdir/task-id" "$SCHEDULE_SUBMIT_TASK" || return 1
            ;;
    esac
    pool_write "$_ss_fdir/exit-code" "$SCHEDULE_SUBMIT_CODE" || return 1
    pool_write "$_ss_fdir/completed-at" "$(pool_now)" || return 1
    if [ "$SCHEDULE_SUBMIT_CODE" -eq 0 ]; then
        pool_write "$_ss_fdir/state" finished || return 1
    else
        pool_write "$_ss_fdir/state" failed || return 1
    fi
    schedule_event "$_ss_dir" schedule.submitted "token=$_ss_token exit=$SCHEDULE_SUBMIT_CODE run=${SCHEDULE_SUBMIT_RUN:--} task=${SCHEDULE_SUBMIT_TASK:--}"
}

schedule_firing_json() {
    _sfj_fdir="$1/firings/$2"
    printf '{"token":"%s","slot":%s,"trigger":"%s","state":"%s","fired_at":%s,"exit_code":%s,"run_id":%s,"task_id":%s}' \
        "$(json_escape "$2")" "$(pool_read "$_sfj_fdir/slot")" "$(pool_read "$_sfj_fdir/trigger")" "$(pool_read "$_sfj_fdir/state")" \
        "$(pool_read "$_sfj_fdir/fired-at")" "$(json_string_or_null "$(pool_read "$_sfj_fdir/exit-code")" | sed 's/"//g')" \
        "$(json_string_or_null "$(pool_read "$_sfj_fdir/run-id")")" "$(json_string_or_null "$(pool_read "$_sfj_fdir/task-id")")"
}
