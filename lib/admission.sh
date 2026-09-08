#!/bin/sh
# Single-host admission authority. Callers hold the lock for every observation
# and mutation. Records contain restricted tokens, never executable shell input.

admission_token() {
    case "$1" in ''|*[!a-zA-Z0-9_.-]*|[!a-zA-Z0-9]*) return 1 ;; esac
    [ "${#1}" -le 160 ]
}

admission_number() {
    case "$1" in ''|*[!0-9]*|0[0-9]*) return 1 ;; esac
    [ "${#1}" -le 12 ]
}

admission_labels() (
    [ "${#1}" -le 2048 ] || exit 1
    [ "$1" = - ] && exit 0
    case "$1" in ''|,*|*,|*,,*) exit 1 ;; esac
    _ad_labels="$1"
    while :; do
        _ad_label="${_ad_labels%%,*}"
        admission_token "$_ad_label" || exit 1
        case "$_ad_labels" in *,*) _ad_labels="${_ad_labels#*,}" ;; *) break ;; esac
    done
)

admission_error() {
    json_error admission "$1" "$2" 'inspect hydra admission status --json'
    return 1
}

admission_open() {
    _ad_root="${HYDRA_HOME:-$HOME/.hydra}/admission"
    [ "$(find "${HYDRA_HOME:-$HOME/.hydra}" -prune -user "$(id -u)" ! -perm -0020 ! -perm -0002 -exec printf yes \;)" = yes ] || return 1
    [ ! -L "$_ad_root" ] || return 1
    (umask 077; mkdir -p "$_ad_root") || return 1
    [ -d "$_ad_root" ] || return 1
    [ "$(find "$_ad_root" -prune -user "$(id -u)" ! -perm -0020 ! -perm -0002 -exec printf yes \;)" = yes ] || return 1
    # This lock is deliberately not swept by generic stale-lock cleanup. A
    # crashed writer needs inspection; it must never permit concurrent writers.
    _ad_tries=0
    until mkdir "$_ad_root/lock" 2>/dev/null; do
        _ad_tries=$((_ad_tries + 1))
        [ "$_ad_tries" -lt 40 ] || return 1
        sleep 0.05
    done
    trap 'rmdir "$_ad_root/lock" 2>/dev/null || true' 0
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    _ad_now="$(date +%s)"
    _ad_host_limit=0 _ad_project_limit=0 _ad_disk_floor_kb=0 _ad_queue_limit=128 _ad_host_labels=-
    if [ -e "$_ad_root/policy" ] || [ -L "$_ad_root/policy" ]; then
        [ -f "$_ad_root/policy" ] && [ ! -L "$_ad_root/policy" ] || return 1
        _ad_seen=' '
        while IFS='=' read -r _ad_key _ad_value; do
            case "$_ad_seen" in *" $_ad_key "*) return 1 ;; esac
            _ad_seen="$_ad_seen$_ad_key "
            case "$_ad_key" in
                host_limit) admission_number "$_ad_value" || return 1; _ad_host_limit="$_ad_value" ;;
                project_limit) admission_number "$_ad_value" || return 1; _ad_project_limit="$_ad_value" ;;
                disk_floor_kb) admission_number "$_ad_value" || return 1; _ad_disk_floor_kb="$_ad_value" ;;
                queue_limit) admission_number "$_ad_value" || return 1; _ad_queue_limit="$_ad_value" ;;
                labels) admission_labels "$_ad_value" || return 1; _ad_host_labels="$_ad_value" ;;
                *) return 1 ;;
            esac
        done < "$_ad_root/policy"
    fi
    _ad_disk_free_kb="$(LC_ALL=C df -Pk "$_ad_root" | awk 'END {print $4}')"
    admission_number "$_ad_disk_free_kb" || return 1
}

# Record: project state requested deadline sequence labels reason.
admission_read() {
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    IFS=' ' read -r _ad_project _ad_state _ad_requested _ad_deadline _ad_sequence _ad_required _ad_reason _ad_extra < "$1" || return 1
    [ -z "$_ad_extra" ] && [ "$(wc -l < "$1" | tr -d ' ')" = 1 ] || return 1
    admission_token "$_ad_project" && admission_token "$_ad_reason" &&
        admission_number "$_ad_requested" && admission_number "$_ad_deadline" &&
        admission_number "$_ad_sequence" && admission_labels "$_ad_required" || return 1
    case "$_ad_state" in queued|reserved|unknown|released|cancelled|expired|refused) ;; *) return 1 ;; esac
}

admission_write() {
    _ad_tmp="$(mktemp "$_ad_root/.record.XXXXXX")" || return 1
    if ! printf '%s %s %s %s %s %s %s\n' "$_ad_project" "$_ad_state" "$_ad_requested" \
        "$_ad_deadline" "$_ad_sequence" "$_ad_required" "$_ad_reason" > "$_ad_tmp" ||
        ! mv "$_ad_tmp" "$1"; then
        rm -f "$_ad_tmp"
        return 1
    fi
}

admission_scan() {
    _ad_reserved=0 _ad_queued=0 _ad_project_reserved=0 _ad_first=0 _ad_next=1
    for _ad_file in "$_ad_root"/*.request; do
        [ -e "$_ad_file" ] || [ -L "$_ad_file" ] || continue
        admission_read "$_ad_file" || return 1
        if [ "$_ad_state" = queued ] && { [ "$_ad_now" -ge "$_ad_deadline" ] || [ "$_ad_now" -lt "$_ad_requested" ]; }; then
            _ad_state=expired _ad_reason=queue_deadline
            admission_write "$_ad_file" || return 1
        fi
        [ "$_ad_sequence" -lt "$_ad_next" ] || _ad_next=$((_ad_sequence + 1))
        case "$_ad_state" in
            reserved|unknown)
                _ad_reserved=$((_ad_reserved + 1))
                [ "$_ad_project" != "${_ad_target_project:-}" ] || _ad_project_reserved=$((_ad_project_reserved + 1))
                ;;
            queued)
                _ad_queued=$((_ad_queued + 1))
                if [ "$_ad_first" -eq 0 ] || [ "$_ad_sequence" -lt "$_ad_first" ]; then _ad_first="$_ad_sequence"; fi
                ;;
        esac
    done
}

admission_compatible() (
    [ "$_ad_required" = - ] && exit 0
    _ad_remaining="$_ad_required"
    while :; do
        _ad_item="${_ad_remaining%%,*}"
        case ",$_ad_host_labels," in *",$_ad_item,"*) ;; *) exit 1 ;; esac
        case "$_ad_remaining" in *,*) _ad_remaining="${_ad_remaining#*,}" ;; *) break ;; esac
    done
)

admission_claim() {
    admission_read "$_ad_path" || return 1
    [ "$_ad_state" = queued ] || return 0
    if ! admission_compatible; then
        _ad_state=refused _ad_reason=capability_unavailable
    elif [ "$_ad_first" -ne "$_ad_sequence" ]; then _ad_reason=fifo
    elif [ "$_ad_host_limit" -gt 0 ] && [ "$_ad_reserved" -ge "$_ad_host_limit" ]; then _ad_reason=host_limit
    elif [ "$_ad_project_limit" -gt 0 ] && [ "$_ad_project_reserved" -ge "$_ad_project_limit" ]; then _ad_reason=project_limit
    elif [ "$_ad_disk_free_kb" -lt "$_ad_disk_floor_kb" ]; then _ad_reason=disk_floor
    else _ad_state=reserved _ad_reason=admitted
    fi
    admission_write "$_ad_path"
}

admission_record_json() {
    _ad_age=$((_ad_now - _ad_requested))
    [ "$_ad_age" -ge 0 ] || _ad_age=0
    printf '{"id":"%s","project":"%s","state":"%s","requested_at":%s,"deadline":%s,"sequence":%s,"required_labels":"%s","reason":"%s","age_seconds":%s}' \
        "$_ad_id" "$_ad_project" "$_ad_state" "$_ad_requested" "$_ad_deadline" "$_ad_sequence" "$_ad_required" "$_ad_reason" "$_ad_age"
}

cmd_admission() (
    umask 077
    _ad_action="${1:-status}"
    [ $# -eq 0 ] || shift
    if [ "$_ad_action" = --help ]; then
        printf '%s\n' 'hydra admission status [--json|--summary]' \
            'hydra admission configure <host-limit> <project-limit> <disk-floor-kb> <queue-limit> <labels|->' \
            'hydra admission request <id> <project-id> <queue-seconds> <labels|->' \
            'hydra admission <inspect|claim|cancel|unknown> <id>' \
            'hydra admission release <id> --confirmed'
        exit 0
    fi
    admission_open || { admission_error state_unavailable 'Admission storage, policy, or lock is unavailable; inspect before retrying'; exit 1; }
    _ad_target_project=''
    case "$_ad_action" in
        configure)
            if ! { [ $# -eq 5 ] && admission_number "$1" && admission_number "$2" &&
                admission_number "$3" && admission_number "$4" && admission_labels "$5"; }; then
                admission_error invalid_input 'Expected host limit, project limit, disk floor KiB, queue limit, and comma-separated labels'; exit 1;
            fi
            _ad_tmp="$(mktemp "$_ad_root/.policy.XXXXXX")" || exit 1
            printf 'host_limit=%s\nproject_limit=%s\ndisk_floor_kb=%s\nqueue_limit=%s\nlabels=%s\n' "$1" "$2" "$3" "$4" "$5" > "$_ad_tmp" &&
                mv "$_ad_tmp" "$_ad_root/policy" || exit 1
            json_success admission '{"configured":true}'
            exit 0
            ;;
        status)
            _ad_summary=0
            case "$#:${1:-}" in 0:|1:--json) ;; 1:--summary) _ad_summary=1 ;; *) exit 1 ;; esac
            admission_scan || { admission_error recovery_required 'Invalid admission record'; exit 1; }
            printf '{"schema_version":1,"ok":true,"command":"admission","data":{"observed_at":%s,"observation_age_seconds":0,"host_limit":%s,"project_limit":%s,"disk_floor_kb":%s,"disk_free_kb":%s,"queue_limit":%s,"labels":"%s","reserved":%s,"queued":%s,"requests":[' \
                "$_ad_now" "$_ad_host_limit" "$_ad_project_limit" "$_ad_disk_floor_kb" "$_ad_disk_free_kb" "$_ad_queue_limit" "$_ad_host_labels" "$_ad_reserved" "$_ad_queued"
            _ad_comma=''
            for _ad_file in "$_ad_root"/*.request; do
                [ "$_ad_summary" -eq 0 ] || break
                [ -f "$_ad_file" ] || continue
                admission_read "$_ad_file" || exit 1
                _ad_id="${_ad_file##*/}"; _ad_id="${_ad_id%.request}"
                admission_token "$_ad_id" || exit 1
                printf '%s' "$_ad_comma"; admission_record_json; _ad_comma=,
            done
            printf '],"requests_omitted":%s}}\n' "$( [ "$_ad_summary" -eq 1 ] && printf true || printf false )"
            exit 0
            ;;
        request|inspect|claim|cancel|unknown|release) ;;
        *) admission_error invalid_input 'Unknown admission action'; exit 1 ;;
    esac
    if ! { [ $# -ge 1 ] && admission_token "$1"; }; then admission_error invalid_input 'A restricted request ID is required'; exit 1; fi
    _ad_id="$1"; shift
    _ad_path="$_ad_root/$_ad_id.request"
    if [ "$_ad_action" = request ]; then
        if ! { [ $# -eq 3 ] && admission_token "$1" && admission_number "$2" && [ "$2" -gt 0 ] &&
            [ "$2" -le 604800 ] && admission_labels "$3"; }; then admission_error invalid_input 'Expected project ID, queue seconds (1..604800), and labels'; exit 1; fi
        _ad_target_project="$1"
    else
        if [ "$_ad_action" = release ]; then
            [ $# -eq 1 ] && [ "$1" = --confirmed ] || { admission_error confirmation_required 'Release requires confirmed execution termination'; exit 1; }
        else [ $# -eq 0 ] || { admission_error invalid_input 'Unexpected arguments'; exit 1; }
        fi
        admission_read "$_ad_path" || { admission_error not_found 'Admission request unavailable'; exit 1; }
        _ad_target_project="$_ad_project"
    fi
    admission_scan || { admission_error recovery_required 'Invalid admission record'; exit 1; }
    if [ "$_ad_action" = request ]; then
        if [ -e "$_ad_path" ] || [ -L "$_ad_path" ]; then
            admission_read "$_ad_path" && [ "$_ad_project" = "$1" ] && [ "$_ad_required" = "$3" ] &&
                [ "$((_ad_deadline - _ad_requested))" -eq "$2" ] || { admission_error submission_conflict 'Request ID already binds different or invalid inputs'; exit 1; }
        else
            [ "$_ad_queued" -lt "$_ad_queue_limit" ] || { admission_error queue_full 'Host admission queue is full'; exit 1; }
            _ad_project="$1" _ad_requested="$_ad_now" _ad_deadline=$((_ad_now + $2)) _ad_sequence="$_ad_next" _ad_required="$3" _ad_state=queued _ad_reason=pending
            admission_write "$_ad_path" || exit 1
            [ "$_ad_first" -ne 0 ] || _ad_first="$_ad_sequence"
        fi
        admission_claim || exit 1
    else
        admission_read "$_ad_path" || exit 1
        case "$_ad_action" in
            claim) admission_claim || exit 1 ;;
            cancel)
                case "$_ad_state" in
                    queued) _ad_state=cancelled _ad_reason=cancelled; admission_write "$_ad_path" || exit 1 ;;
                    reserved|unknown) admission_error execution_owned 'Cancel the execution owner; its reservation remains held'; exit 1 ;;
                esac ;;
            unknown)
                [ "$_ad_state" = reserved ] || [ "$_ad_state" = unknown ] || exit 1
                _ad_state=unknown _ad_reason=outcome_unknown; admission_write "$_ad_path" || exit 1 ;;
            release)
                case "$_ad_state" in reserved|unknown|released) ;; *) admission_error invalid_state 'Only an execution reservation may be released'; exit 1 ;; esac
                _ad_state=released _ad_reason=termination_confirmed; admission_write "$_ad_path" || exit 1 ;;
        esac
    fi
    admission_read "$_ad_path" || exit 1
    json_success admission "$(admission_record_json)"
)
