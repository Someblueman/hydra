#!/bin/sh
# Canonical shell snapshot plus bounded dispatch to the optional read-only core.

HYDRA_CORE_PROTOCOL=1
HYDRA_CORE_TIMEOUT_SECONDS="${HYDRA_CORE_TIMEOUT_SECONDS:-2}"

hydra_snapshot_shell() {
    state_v2_verify >/dev/null || return 1
    _hss_projects=0
    _hss_first=1
    for _hss_project_dir in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_hss_project_dir" ] || continue
        _hss_projects=$((_hss_projects + 1))
    done
    printf '{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":%s,"heads":[' "$_hss_projects"
    for _hss_project_dir in "$HYDRA_STATE_V2_ROOT"/projects/project_*; do
        [ -d "$_hss_project_dir" ] || continue
        _hss_project_id="$(basename "$_hss_project_dir")"
        for _hss_head_dir in "$_hss_project_dir"/heads/head_*; do
            [ -d "$_hss_head_dir" ] || continue
            _hss_head_id="$(basename "$_hss_head_dir")"
            _hss_branch="$(sed -n '1p' "$_hss_head_dir/branch")" || return 1
            _hss_desired="$(sed -n '1p' "$_hss_head_dir/desired-state")" || return 1
            _hss_instance="$(sed -n '1p' "$_hss_head_dir/current-instance")" || return 1
            if [ "$_hss_first" -eq 0 ]; then printf ','; fi
            _hss_first=0
            printf '{"project_id":"%s","head_id":"%s","branch":"%s","desired_state":"%s","current_instance":"%s"}' \
                "$(json_escape "$_hss_project_id")" "$(json_escape "$_hss_head_id")" \
                "$(json_escape "$_hss_branch")" "$(json_escape "$_hss_desired")" \
                "$(json_escape "$_hss_instance")"
        done
    done
    printf ']}}\n'
}

hydra_core_path() {
    if [ -n "${HYDRA_CORE:-}" ]; then
        printf '%s\n' "$HYDRA_CORE"
        return 0
    fi
    for _hcp_candidate in \
        "$HYDRA_BIN_DIR/../build/hydra-core" \
        "$HYDRA_BIN_DIR/../libexec/hydra/hydra-core"; do
        [ -f "$_hcp_candidate" ] || continue
        printf '%s\n' "$_hcp_candidate"
        return 0
    done
    return 1
}

# Writes captured stdout/stderr into the supplied directory and returns 124 on timeout.
hydra_core_signal_tree() (
    _hcst_pid="$1"
    _hcst_signal="$2"
    for _hcst_child in $(ps -eo pid=,ppid= 2>/dev/null | awk -v parent="$_hcst_pid" '$2 == parent { print $1 }'); do
        hydra_core_signal_tree "$_hcst_child" "$_hcst_signal"
    done
    kill "-$_hcst_signal" "$_hcst_pid" 2>/dev/null || true
)

hydra_core_run_bounded() {
    _hcr_dir="$1"
    shift
    case "$HYDRA_CORE_TIMEOUT_SECONDS" in
        ''|*[!0-9]*|0) HYDRA_CORE_TIMEOUT_SECONDS=2 ;;
    esac
    "$@" > "$_hcr_dir/stdout" 2> "$_hcr_dir/stderr" &
    _hcr_pid=$!
    (
        _hcr_sleep=""
        trap '[ -z "$_hcr_sleep" ] || kill "$_hcr_sleep" 2>/dev/null; exit 0' TERM HUP INT
        sleep "$HYDRA_CORE_TIMEOUT_SECONDS" &
        _hcr_sleep=$!
        wait "$_hcr_sleep" || exit 0
        if kill -0 "$_hcr_pid" 2>/dev/null; then
            : > "$_hcr_dir/timed-out"
            hydra_core_signal_tree "$_hcr_pid" TERM
            sleep 1
            hydra_core_signal_tree "$_hcr_pid" KILL
        fi
    ) >/dev/null 2>&1 &
    _hcr_watchdog=$!
    if wait "$_hcr_pid"; then _hcr_status=0; else _hcr_status=$?; fi
    kill "$_hcr_watchdog" 2>/dev/null || true
    wait "$_hcr_watchdog" 2>/dev/null || true
    if [ -f "$_hcr_dir/timed-out" ]; then return 124; fi
    return "$_hcr_status"
}

hydra_core_snapshot_valid() {
    _hcsv_file="$1"
    [ "$(wc -l < "$_hcsv_file" | tr -d ' ')" = "1" ] || return 1
    _hcsv_line="$(sed -n '1p' "$_hcsv_file")"
    case "$_hcsv_line" in
        '{"schema_version":1,"ok":true,"command":"snapshot","data":{"state_schema":2,"projects":'*',"heads":['*']}}') return 0 ;;
        *) return 1 ;;
    esac
}

hydra_snapshot_native() {
    _hsn_core="$(hydra_core_path 2>/dev/null || true)"
    if [ -z "$_hsn_core" ] || [ ! -e "$_hsn_core" ]; then
        echo "hydra: native snapshot fallback: helper-unavailable" >&2
        LC_ALL=C hydra_snapshot_shell
        return
    fi
    if [ ! -x "$_hsn_core" ]; then
        echo "hydra: native snapshot fallback: helper-not-executable" >&2
        LC_ALL=C hydra_snapshot_shell
        return
    fi
    _hsn_dir="$(mktemp -d "${TMPDIR:-/tmp}/hydra-core.XXXXXX")" || return 1
    if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" --protocol-version; then
        _hsn_status=0
    else
        _hsn_status=$?
        if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=protocol-timeout; else _hsn_reason=protocol-failure; fi
    fi
    if [ "$_hsn_status" -ne 0 ]; then
        :
    elif [ "$(sed -n '1p' "$_hsn_dir/stdout")" != "$HYDRA_CORE_PROTOCOL" ] || \
         [ "$(wc -l < "$_hsn_dir/stdout" | tr -d ' ')" != "1" ]; then
        _hsn_reason=protocol-skew
    else
        rm -f "$_hsn_dir/stdout" "$_hsn_dir/stderr" "$_hsn_dir/timed-out"
        if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" --version; then
            _hsn_status=0
        else
            _hsn_status=$?
            if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=version-timeout; else _hsn_reason=version-failure; fi
        fi
        if [ "$_hsn_status" -ne 0 ]; then
            :
        elif [ "$(sed -n '1p' "$_hsn_dir/stdout")" != "Hydra core $HYDRA_VERSION protocol $HYDRA_CORE_PROTOCOL" ] || \
             [ "$(wc -l < "$_hsn_dir/stdout" | tr -d ' ')" != 1 ]; then
            _hsn_reason=version-skew
        else
            rm -f "$_hsn_dir/stdout" "$_hsn_dir/stderr" "$_hsn_dir/timed-out"
            if hydra_core_run_bounded "$_hsn_dir" "$_hsn_core" snapshot "$HYDRA_STATE_V2_ROOT"; then
                _hsn_status=0
            else
                _hsn_status=$?
                if [ "$_hsn_status" -eq 124 ]; then _hsn_reason=snapshot-timeout; else _hsn_reason=snapshot-failure; fi
            fi
            if [ "$_hsn_status" -ne 0 ]; then
                :
            elif ! hydra_core_snapshot_valid "$_hsn_dir/stdout"; then
                _hsn_reason=malformed-output
            else
                cat "$_hsn_dir/stdout"
                rm -rf "$_hsn_dir"
                return 0
            fi
        fi
    fi
    echo "hydra: native snapshot fallback: $_hsn_reason" >&2
    rm -rf "$_hsn_dir"
    LC_ALL=C hydra_snapshot_shell
}
