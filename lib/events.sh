#!/bin/sh
# Versioned append-only JSONL lifecycle events.

HYDRA_EVENT_MAX_BYTES="${HYDRA_EVENT_MAX_BYTES:-32768}"
HYDRA_EVENT_ARCHIVE_MIN_SECONDS=1
HYDRA_EVENT_ARCHIVE_MAX_SECONDS=31536000
HYDRA_EVENT_ARCHIVE_MIN_COUNT=1
HYDRA_EVENT_ARCHIVE_MAX_COUNT=256
HYDRA_EVENT_ARCHIVE_MIN_BYTES=1024
HYDRA_EVENT_ARCHIVE_MAX_BYTES=67108864

event_file_for_head() {
    _effh_dir="$(state_v2_head_dir "$1" "$2")" || return 1
    printf '%s/events/events.jsonl\n' "$_effh_dir"
}

_event_sequence() {
    awk 'match($0, /"sequence":[0-9]+[,}]/) { print substr($0, RSTART + 11, RLENGTH - 12); exit }'
}

event_line_valid() {
    _elv_line="$1"
    [ -n "$_elv_line" ] || return 1
    _elv_bytes="$(printf '%s' "$_elv_line" | LC_ALL=C wc -c | tr -d ' ')"
    [ "$_elv_bytes" -le "$HYDRA_EVENT_MAX_BYTES" ] || return 1
    case "$_elv_line" in \{*\}) ;; *) return 1 ;; esac
    _elv_sequence="$(printf '%s\n' "$_elv_line" | _event_sequence)"
    _event_uint "$_elv_sequence" 4294967294 && [ "$_elv_sequence" -gt 0 ] || return 1
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
        _evf_sequence="$(printf '%s\n' "$_evf_line" | _event_sequence)"
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
    if [ -s "$_ee_file" ] && ! event_line_valid "$(tail -n 1 "$_ee_file")"; then
        release_lock "$_ee_lock"; return 1
    fi
    _ee_last_sequence="$(tail -n 1 "$_ee_file" 2>/dev/null | _event_sequence)"
    if [ -n "$_ee_last_sequence" ]; then
        _event_uint "$_ee_last_sequence" 4294967293 || { release_lock "$_ee_lock"; return 1; }
        _ee_sequence=$((_ee_last_sequence + 1))
    else
        _ee_sequence="$(_event_sequence_after_empty "$_ee_file")" || {
            release_lock "$_ee_lock"
            echo "Error: event sequence cursor or archive history is invalid; repair or reconcile the stream before appending" >&2
            return 1
        }
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
    _event_write_next_sequence "$_ee_file" $((_ee_sequence + 1)) || { release_lock "$_ee_lock"; return 1; }
    release_lock "$_ee_lock"
    if command -v notify_event >/dev/null 2>&1; then
        notify_event "$_ee_type" "$_ee_project" "$_ee_head" || true
    fi
    printf '%s\n' "$_ee_id"
}

_event_write_next_sequence() {
    _ewns_file="$1" _ewns_next="$2"
    _ewns_tmp="$(mktemp_adjacent "$_ewns_file.next-sequence")" || return 1
    printf '%s\n' "$_ewns_next" > "$_ewns_tmp" || { rm -f "$_ewns_tmp"; return 1; }
    atomic_replace "$_ewns_file.next-sequence" "$_ewns_tmp"
}

_event_digest() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}

# Sequence allocation never guesses past a lost rotation cursor. A retained or
# expired archive directory proves this is not a brand-new empty stream.
_event_uint() {
    case "$1" in ''|*[!0-9]*|0[0-9]*) return 1 ;; esac
    [ "${#1}" -le 10 ] && [ "$1" -le "$2" ]
}

_event_sequence_after_empty() {
    _ese_file="$1"
    if [ -e "$_ese_file.next-sequence" ] || [ -L "$_ese_file.next-sequence" ]; then
        [ -f "$_ese_file.next-sequence" ] && [ ! -L "$_ese_file.next-sequence" ] || return 1
        _ese_cursor="$(cat "$_ese_file.next-sequence")" || return 1
        _event_uint "$_ese_cursor" 4294967294 && [ "$_ese_cursor" -gt 0 ] || return 1
        printf '%s\n' "$_ese_cursor"
        return
    fi
    _ese_archive="$(dirname "$_ese_file")/archive"
    [ ! -L "$_ese_archive" ] || return 1
    [ ! -e "$_ese_archive" ] || [ -d "$_ese_archive" ] || return 1
    [ -z "$(find "$_ese_archive" -mindepth 1 -maxdepth 1 -print 2>/dev/null | head -n 1)" ] || return 1
    printf '1\n'
}

_event_repair_file_locked() {
    _erf_file="$1"
    [ -f "$_erf_file" ] || return 1
    _erf_tmp="$(mktemp_adjacent "$_erf_file")" || return 1
    _erf_expected=""
    while IFS= read -r _erf_line || [ -n "$_erf_line" ]; do
        event_line_valid "$_erf_line" || break
        _erf_sequence="$(printf '%s\n' "$_erf_line" | _event_sequence)"
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

event_repair_file() {
    _erf_file="$1"
    _erf_project="$2"
    _erf_head="$3"
    hydra_valid_id "$_erf_project" || return 1
    hydra_valid_id "$_erf_head" || return 1
    _erf_lock="events_${_erf_project}_${_erf_head}"
    acquire_lock "$_erf_lock" "event repair" "$_erf_head" || return 1
    _event_repair_file_locked "$_erf_file"
    _erf_status=$?
    release_lock "$_erf_lock"
    return "$_erf_status"
}

_event_stream_matches() {
    awk -v stream="$2" '
        function field(line, key, pieces) {
            if (!match(line, "\"" key "\":\"[a-z0-9_-]+\"")) return "";
            split(substr(line, RSTART, RLENGTH), pieces, "\""); return pieces[4]
        }
        { if (field($0, "project_id") "/" field($0, "head_id") != stream) exit 1 }' "$1"
}

_event_meta_valid() {
    _emv_meta="$1" _emv_archive="$2" _emv_stream="$3"
    [ -f "$_emv_meta" ] && [ ! -L "$_emv_meta" ] && [ -f "$_emv_archive" ] && [ ! -L "$_emv_archive" ] || return 1
    [ "$(wc -c < "$_emv_meta" | tr -d ' ')" -le 2048 ] || return 1
    case "${_emv_archive##*/}" in events-*[!a-zA-Z0-9._-]*|[!e]*|'') return 1 ;; events-*.jsonl) ;; *) return 1 ;; esac
    awk -F= 'BEGIN { n["schema_version"]; n["stream_id"]; n["first_sequence"]; n["last_sequence"]; n["created_at"]; n["expires_at"]; n["bytes"]; n["event_count"]; n["sha256"] }
        NF != 2 || !($1 in n) || seen[$1]++ { bad=1 }
        END { for (key in n) if (!seen[key]) bad=1; exit bad }' "$_emv_meta" || return 1
    [ "$(sed -n 's/^schema_version=//p' "$_emv_meta")" = 1 ] &&
        [ "$(sed -n 's/^stream_id=//p' "$_emv_meta")" = "$_emv_stream" ] || return 1
    _emv_created="$(sed -n 's/^created_at=//p' "$_emv_meta")" _emv_expiry="$(sed -n 's/^expires_at=//p' "$_emv_meta")"
    _emv_bytes="$(sed -n 's/^bytes=//p' "$_emv_meta")" _emv_count="$(sed -n 's/^event_count=//p' "$_emv_meta")"
    _emv_first="$(sed -n 's/^first_sequence=//p' "$_emv_meta")" _emv_last="$(sed -n 's/^last_sequence=//p' "$_emv_meta")"
    _emv_hash="$(sed -n 's/^sha256=//p' "$_emv_meta")"
    for _emv_value in "$_emv_created" "$_emv_expiry" "$_emv_bytes" "$_emv_count" "$_emv_first" "$_emv_last"; do
        _event_uint "$_emv_value" 4294967294 || return 1
    done
    case "$_emv_hash" in *[!0-9a-f]*|'') return 1 ;; esac
    [ "${#_emv_hash}" -eq 64 ] && [ "$_emv_created" -gt 0 ] && [ "$_emv_expiry" -gt "$_emv_created" ] &&
        [ "$((_emv_expiry - _emv_created))" -le "$HYDRA_EVENT_ARCHIVE_MAX_SECONDS" ] &&
        [ "$_emv_bytes" -le "$HYDRA_EVENT_ARCHIVE_MAX_BYTES" ] && [ "$_emv_count" -gt 0 ] &&
        [ "$_emv_first" -gt 0 ] && [ "$_emv_last" -ge "$_emv_first" ] || return 1
    event_verify_file "$_emv_archive" >/dev/null 2>&1 || return 1
    _event_stream_matches "$_emv_archive" "$_emv_stream" || return 1
    [ "$(LC_ALL=C wc -c < "$_emv_archive" | tr -d ' ')" = "$_emv_bytes" ] || return 1
    [ "$(awk 'END {print NR + 0}' "$_emv_archive")" = "$_emv_count" ] || return 1
    [ "$(head -n 1 "$_emv_archive" | _event_sequence)" = "$_emv_first" ] || return 1
    [ "$(tail -n 1 "$_emv_archive" | _event_sequence)" = "$_emv_last" ] || return 1
    [ "$(_event_digest "$_emv_archive")" = "$_emv_hash" ]
}

_event_retain_file_locked() {
    _ert_file="$1" _ert_max="$2" _ert_project="$3" _ert_head="$4"
    _ert_count_limit="${5:-32}" _ert_bytes_limit="${6:-67108864}"
    _ert_keep_seconds="${7:-2592000}" _ert_dry_run="${8:-0}"
    for _ert_value in "$_ert_max" "$_ert_count_limit" "$_ert_bytes_limit" "$_ert_keep_seconds"; do
        _event_uint "$_ert_value" 4294967294 || return 1
    done
    [ -f "$_ert_file" ] && [ ! -L "$_ert_file" ] && [ ! -L "$(dirname "$_ert_file")/archive" ] || return 1
    event_verify_file "$_ert_file" >/dev/null 2>&1 || return 1
    _event_stream_matches "$_ert_file" "$_ert_project/$_ert_head" || return 1
    [ "$_ert_count_limit" -ge "$HYDRA_EVENT_ARCHIVE_MIN_COUNT" ] && [ "$_ert_count_limit" -le "$HYDRA_EVENT_ARCHIVE_MAX_COUNT" ] || return 1
    [ "$_ert_bytes_limit" -ge "$HYDRA_EVENT_ARCHIVE_MIN_BYTES" ] && [ "$_ert_bytes_limit" -le "$HYDRA_EVENT_ARCHIVE_MAX_BYTES" ] || return 1
    [ "$_ert_keep_seconds" -ge "$HYDRA_EVENT_ARCHIVE_MIN_SECONDS" ] && [ "$_ert_keep_seconds" -le "$HYDRA_EVENT_ARCHIVE_MAX_SECONDS" ] || return 1
    _ert_count="$(awk 'END { print NR + 0 }' "$_ert_file")"
    [ "$_ert_count" -le "$_ert_max" ] && return 0
    _ert_archive_dir="$(dirname "$_ert_file")/archive"; mkdir -p "$_ert_archive_dir" || return 1
    _ert_remove=$((_ert_count - _ert_max)) _ert_now="$(date +%s)"
    _ert_expiry=$((_ert_now + _ert_keep_seconds))
    _ert_full_last="$(tail -n 1 "$_ert_file" | _event_sequence)"
    _event_uint "$_ert_full_last" 4294967293 && _event_uint "$_ert_expiry" 4294967294 || return 1
    _ert_prefix="$(mktemp_adjacent "$_ert_file")" || return 1
    head -n "$_ert_remove" "$_ert_file" > "$_ert_prefix" || { rm -f "$_ert_prefix"; return 1; }
    _ert_bytes="$(LC_ALL=C wc -c < "$_ert_prefix" | tr -d ' ')" _ert_event_count="$_ert_remove"
    _ert_first="$(head -n 1 "$_ert_prefix" | _event_sequence)" _ert_last="$(tail -n 1 "$_ert_prefix" | _event_sequence)"
    _ert_hash="$(_event_digest "$_ert_prefix")" || { rm -f "$_ert_prefix"; return 1; }
    _ert_protected_count=0 _ert_protected_bytes=0
    # shellcheck disable=SC2044 # Generated archive names contain no whitespace.
    for _ert_archive in $(find "$_ert_archive_dir" \( -type f -o -type l \) -name '*.jsonl' -print 2>/dev/null); do
        [ "$(basename "$_ert_archive")" = expiry-summary.jsonl ] && continue
        _ert_meta="$_ert_archive.meta"
        if _event_meta_valid "$_ert_meta" "$_ert_archive" "$_ert_project/$_ert_head"; then
            if [ "$_emv_expiry" -le "$_ert_now" ]; then
                [ "$_ert_dry_run" -eq 1 ] || _event_expire_archive "$_ert_archive" "$_ert_meta" "$_ert_archive_dir" || { rm -f "$_ert_prefix"; return 1; }
            else
                _ert_protected_count=$((_ert_protected_count + 1)); _ert_protected_bytes=$((_ert_protected_bytes + $(LC_ALL=C wc -c < "$_ert_archive" | tr -d ' ')))
            fi
        else
            _ert_protected_count=$((_ert_protected_count + 1)); _ert_protected_bytes=$_ert_bytes_limit
        fi
    done
    # shellcheck disable=SC2044 # Generated metadata names contain no whitespace.
    for _ert_meta in $(find "$_ert_archive_dir" \( -type f -o -type l \) -name '*.meta' -print 2>/dev/null); do
        [ -e "${_ert_meta%.meta}" ] && continue
        _ert_protected_count=$((_ert_protected_count + 1)); _ert_protected_bytes=$_ert_bytes_limit
    done
    if [ "$_ert_protected_count" -ge "$_ert_count_limit" ] || [ $((_ert_protected_bytes + _ert_bytes)) -gt "$_ert_bytes_limit" ]; then
        rm -f "$_ert_prefix"
        echo "Error: event archive capacity is protected by unexpired or unverifiable archives; increase limits or reconcile them" >&2
        return 1
    fi
    _ert_archive="$_ert_archive_dir/events-$(date +%Y%m%dT%H%M%S)-$$-$_ert_first-$_ert_last.jsonl"
    _ert_suffix=0
    while [ -e "$_ert_archive" ] || [ -e "$_ert_archive.meta" ]; do _ert_suffix=$((_ert_suffix + 1)); _ert_archive="$_ert_archive_dir/events-$(date +%Y%m%dT%H%M%S)-$$-$_ert_first-$_ert_last-$_ert_suffix.jsonl"; done
    if [ "$_ert_dry_run" -eq 1 ]; then
        rm -f "$_ert_prefix"; printf 'would-archive\t%s\t%s\t%s\n' "$_ert_archive" "$_ert_first-$_ert_last" "$_ert_expiry"; return 0
    fi
    mv "$_ert_prefix" "$_ert_archive" || { rm -f "$_ert_prefix"; return 1; }
    {
        printf 'schema_version=1\nstream_id=%s/%s\nfirst_sequence=%s\nlast_sequence=%s\n' "$_ert_project" "$_ert_head" "$_ert_first" "$_ert_last"
        printf 'created_at=%s\nexpires_at=%s\nbytes=%s\nevent_count=%s\nsha256=%s\n' "$_ert_now" "$_ert_expiry" "$_ert_bytes" "$_ert_event_count" "$_ert_hash"
    } > "$_ert_archive.meta" || return 1
    _ert_tmp="$(mktemp_adjacent "$_ert_file")" || return 1
    tail -n "$_ert_max" "$_ert_file" > "$_ert_tmp" || { rm -f "$_ert_tmp"; return 1; }
    _event_write_next_sequence "$_ert_file" $((_ert_full_last + 1)) || { rm -f "$_ert_tmp"; return 1; }
    atomic_replace "$_ert_file" "$_ert_tmp" || return 1
    printf '%s\n' "$_ert_archive"
}

_event_expire_archive() {
    _eea_archive="$1" _eea_meta="$2" _eea_dir="$3"
    [ -f "$_eea_archive" ] && [ ! -L "$_eea_archive" ] && [ ! -L "$_eea_dir/expiry-summary.jsonl" ] || return 1
    _eea_summary="$_eea_dir/expiry-summary.jsonl"
    printf '{"schema_version":1,"status":"expired","archive":"%s","first_sequence":%s,"last_sequence":%s,"sha256":"%s","expired_at":%s}\n' \
        "$(basename "$_eea_archive")" "$(sed -n 's/^first_sequence=//p' "$_eea_meta" | head -n 1)" \
        "$(sed -n 's/^last_sequence=//p' "$_eea_meta" | head -n 1)" "$(sed -n 's/^sha256=//p' "$_eea_meta" | head -n 1)" "$(date +%s)" >> "$_eea_summary" || return 1
    _eea_tmp="$(mktemp_adjacent "$_eea_summary")" || return 1
    if ! tail -n 64 "$_eea_summary" > "$_eea_tmp" || ! atomic_replace "$_eea_summary" "$_eea_tmp"; then
        rm -f "$_eea_tmp"; return 1
    fi
    rm -f "$_eea_archive" "$_eea_meta"
}

event_retain_file() {
    _ert_file="$1"
    _ert_max="$2"
    _ert_project="$3"
    _ert_head="$4"
    hydra_valid_id "$_ert_project" || return 1
    hydra_valid_id "$_ert_head" || return 1
    _ert_lock="events_${_ert_project}_${_ert_head}"
    acquire_lock "$_ert_lock" "event retention" "$_ert_head" || return 1
    _event_retain_file_locked "$_ert_file" "$_ert_max" "$_ert_project" "$_ert_head"
    _ert_status=$?
    release_lock "$_ert_lock"
    return "$_ert_status"
}
