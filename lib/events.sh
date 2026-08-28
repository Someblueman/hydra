#!/bin/sh
# Versioned append-only JSONL lifecycle events.

HYDRA_EVENT_MAX_BYTES="${HYDRA_EVENT_MAX_BYTES:-32768}"

event_file_for_head() {
    _effh_dir="$(state_v2_head_dir "$1" "$2")" || return 1
    printf '%s/events/events.jsonl\n' "$_effh_dir"
}

event_line_valid() {
    _elv_line="$1"
    [ -n "$_elv_line" ] || return 1
    _elv_bytes="$(printf '%s' "$_elv_line" | LC_ALL=C wc -c | tr -d ' ')"
    [ "$_elv_bytes" -le "$HYDRA_EVENT_MAX_BYTES" ] || return 1
    case "$_elv_line" in \{*\}) ;; *) return 1 ;; esac
    printf '%s\n' "$_elv_line" | grep -F '"schema_version":1' >/dev/null || return 1
    printf '%s\n' "$_elv_line" | grep -F '"event_id":"evt_' >/dev/null || return 1
    printf '%s\n' "$_elv_line" | grep -F '"project_id":"project_' >/dev/null || return 1
    printf '%s\n' "$_elv_line" | grep -F '"head_id":"head_' >/dev/null || return 1
    printf '%s\n' "$_elv_line" | grep -F '"instance_id":"instance_' >/dev/null || return 1
}

event_verify_file() {
    _evf_file="$1"
    [ -f "$_evf_file" ] || { echo "events: missing $_evf_file" >&2; return 1; }
    _evf_line_no=0
    _evf_expected=""
    while IFS= read -r _evf_line || [ -n "$_evf_line" ]; do
        _evf_line_no=$((_evf_line_no + 1))
        if ! event_line_valid "$_evf_line"; then
            echo "events: malformed record at line $_evf_line_no" >&2
            return 1
        fi
        _evf_sequence="$(printf '%s\n' "$_evf_line" | sed -n 's/.*"sequence":\([0-9][0-9]*\).*/\1/p')"
        if [ -z "$_evf_expected" ]; then
            _evf_expected="$_evf_sequence"
        fi
        [ "$_evf_sequence" = "$_evf_expected" ] || {
            echo "events: unexpected sequence at line $_evf_line_no" >&2
            return 1
        }
        _evf_expected=$((_evf_expected + 1))
    done < "$_evf_file"
}

event_emit() {
    _ee_project="$1"
    _ee_head="$2"
    _ee_instance="$3"
    _ee_type="$4"
    _ee_actor_kind="${5:-hydra}"
    _ee_actor_id="${6:-local}"
    _ee_payload="${7:-}"
    [ -n "$_ee_payload" ] || _ee_payload='{}'
    hydra_valid_id "$_ee_project" && hydra_valid_id "$_ee_head" && \
        hydra_valid_id "$_ee_instance" || return 1
    case "$_ee_type" in ''|*[!a-z0-9._-]*) return 1 ;; esac
    case "$_ee_payload" in \{*\}) ;; *) return 1 ;; esac
    _ee_file="$(event_file_for_head "$_ee_project" "$_ee_head")" || return 1
    mkdir -p "$(dirname "$_ee_file")" || return 1
    [ -f "$_ee_file" ] || : > "$_ee_file"
    _ee_lock="events_${_ee_project}_${_ee_head}"
    acquire_lock "$_ee_lock" "event append" || return 1
    _ee_last_sequence="$(tail -n 1 "$_ee_file" 2>/dev/null | sed -n 's/.*"sequence":\([0-9][0-9]*\).*/\1/p')"
    if [ -n "$_ee_last_sequence" ]; then
        _ee_sequence=$((_ee_last_sequence + 1))
    else
        _ee_sequence=1
    fi
    _ee_id="$(hydra_new_id evt "$_ee_project|$_ee_head|$_ee_instance|$_ee_sequence")" || {
        release_lock "$_ee_lock"; return 1;
    }
    _ee_time="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    _ee_line="{\"schema_version\":1,\"event_id\":\"$_ee_id\",\"sequence\":$_ee_sequence,\"occurred_at\":\"$_ee_time\",\"project_id\":\"$_ee_project\",\"head_id\":\"$_ee_head\",\"instance_id\":\"$_ee_instance\",\"run_id\":null,\"type\":\"$(json_escape "$_ee_type")\",\"actor\":{\"kind\":\"$(json_escape "$_ee_actor_kind")\",\"id\":\"$(json_escape "$_ee_actor_id")\"},\"payload\":$_ee_payload}"
    _ee_bytes="$(printf '%s' "$_ee_line" | LC_ALL=C wc -c | tr -d ' ')"
    if [ "$_ee_bytes" -gt "$HYDRA_EVENT_MAX_BYTES" ]; then
        release_lock "$_ee_lock"
        echo "Error: event exceeds $HYDRA_EVENT_MAX_BYTES bytes" >&2
        return 1
    fi
    printf '%s\n' "$_ee_line" >> "$_ee_file" || { release_lock "$_ee_lock"; return 1; }
    release_lock "$_ee_lock"
    if command -v notify_event >/dev/null 2>&1; then
        notify_event "$_ee_type" "$_ee_project" "$_ee_head" || true
    fi
    printf '%s\n' "$_ee_id"
}

event_repair_file() {
    _erf_file="$1"
    [ -f "$_erf_file" ] || return 1
    _erf_tmp="$(mktemp_adjacent "$_erf_file")" || return 1
    _erf_expected=""
    while IFS= read -r _erf_line || [ -n "$_erf_line" ]; do
        event_line_valid "$_erf_line" || break
        _erf_sequence="$(printf '%s\n' "$_erf_line" | sed -n 's/.*"sequence":\([0-9][0-9]*\).*/\1/p')"
        if [ -z "$_erf_expected" ]; then
            _erf_expected="$_erf_sequence"
        fi
        [ "$_erf_sequence" = "$_erf_expected" ] || break
        printf '%s\n' "$_erf_line" >> "$_erf_tmp"
        _erf_expected=$((_erf_expected + 1))
    done < "$_erf_file"
    _erf_backup="$_erf_file.corrupt.$(date +%s)"
    cp "$_erf_file" "$_erf_backup" || { rm -f "$_erf_tmp"; return 1; }
    atomic_replace "$_erf_file" "$_erf_tmp" || return 1
    printf '%s\n' "$_erf_backup"
}

event_retain_file() {
    _ert_file="$1"
    _ert_max="$2"
    case "$_ert_max" in ''|*[!0-9]*) return 1 ;; esac
    _ert_count="$(awk 'END { print NR + 0 }' "$_ert_file")"
    [ "$_ert_count" -le "$_ert_max" ] && return 0
    _ert_archive_dir="$(dirname "$_ert_file")/archive"
    mkdir -p "$_ert_archive_dir" || return 1
    _ert_archive="$_ert_archive_dir/events-$(date +%Y%m%dT%H%M%S)-$$.jsonl"
    cp "$_ert_file" "$_ert_archive" || return 1
    _ert_tmp="$(mktemp_adjacent "$_ert_file")" || return 1
    tail -n "$_ert_max" "$_ert_file" > "$_ert_tmp" || { rm -f "$_ert_tmp"; return 1; }
    atomic_replace "$_ert_file" "$_ert_tmp" || return 1
    printf '%s\n' "$_ert_archive"
}
