#!/bin/sh

# Human and machine projections over the versioned statistics feed.  These
# helpers deliberately consume the feed instead of rereading run records so
# that every projection has the same bounded sample and evidence semantics.
workflow_observability_json() (
    _wo_tmp="$(mktemp "${TMPDIR:-/tmp}/hydra-statistics.XXXXXX")" || return 1
    trap 'rm -f "$_wo_tmp"' EXIT HUP INT TERM
    workflow_statistics_data >"$_wo_tmp" || {
        printf '{"schema_version":1,"ok":true,"availability":"unavailable","runs":[],"steps":[],"warnings":["statistics feed unavailable"]}\n'
        return 0
    }
    _wo_header='' _wo_snapshot='' _wo_runs=0 _wo_steps=0 _wo_warn='' _wo_first=1
    _wo_run_json='' _wo_step_json='' _wo_warn_json=''
    while IFS="$(printf '\t')" read -r _wo_tag _wo_a _wo_b _wo_c _wo_d _wo_e _wo_f _wo_g _wo_h _wo_i _wo_j _wo_k; do
        case "$_wo_tag" in
            HYDRA_STATISTICS) _wo_header=1; _wo_snapshot="$_wo_b" ;;
            R)
                [ "$_wo_first" -eq 0 ] && _wo_run_json="$_wo_run_json,"
                _wo_first=0
                _wo_run_json="$_wo_run_json{\"run_id\":\"$(json_escape "$_wo_a")\",\"workflow\":\"$(json_escape "$_wo_b")\",\"state\":\"$(json_escape "$_wo_c")\",\"project_id\":\"$(json_escape "$_wo_d")\",\"created_at\":\"$(json_escape "$_wo_e")\",\"completeness\":\"$(json_escape "$_wo_f")\",\"first_drive\":$(workflow_observability_value "$_wo_g"),\"terminal\":$(workflow_observability_value "$_wo_h"),\"verified\":$(workflow_observability_value "$_wo_i"),\"recoveries\":$(workflow_observability_value "$_wo_j"),\"compiled_plan\":$(workflow_observability_value "$_wo_k") }"
                _wo_runs=$((_wo_runs + 1)) ;;
            S)
                [ "$_wo_step_json" ] && _wo_step_json="$_wo_step_json,"
                _wo_step_json="$_wo_step_json{\"run_id\":\"$(json_escape "$_wo_a")\",\"step_id\":\"$(json_escape "$_wo_b")\",\"kind\":\"$(json_escape "$_wo_c")\",\"state\":\"$(json_escape "$_wo_d")\",\"attempts\":$(workflow_observability_value "$_wo_e"),\"latest_started\":$(workflow_observability_value "$_wo_f"),\"latest_completed\":$(workflow_observability_value "$_wo_g"),\"first_ready\":$(workflow_observability_value "$_wo_h"),\"first_started\":$(workflow_observability_value "$_wo_i") }"
                _wo_steps=$((_wo_steps + 1)) ;;
            X)
                [ -n "$_wo_warn_json" ] && _wo_warn_json="$_wo_warn_json,"
                _wo_warn_json="$_wo_warn_json\"$(json_escape "$_wo_a")\"" ;;
            Z) : ;;
        esac
    done <"$_wo_tmp"
    [ -n "$_wo_snapshot" ] || { printf '{"schema_version":1,"ok":true,"availability":"unavailable","runs":[],"steps":[],"warnings":["malformed statistics feed"]}\n'; return 0; }
    printf '{"schema_version":1,"ok":true,"availability":"known","snapshot":%s,"runs":[%s],"steps":[%s],"warnings":[%s],"coverage":{"runs":%s,"steps":%s}}\n' \
        "$(workflow_observability_value "$_wo_snapshot")" "$_wo_run_json" "$_wo_step_json" "$_wo_warn_json" "$_wo_runs" "$_wo_steps"
)

workflow_observability_value() {
    case "$1" in
        ''|-) printf 'null' ;;
        *[!0-9]*) printf '"%s"' "$(json_escape "$1")" ;;
        *) printf '%s' "$1" ;;
    esac
}

workflow_observability_announce() (
    _wo_tmp="$(mktemp "${TMPDIR:-/tmp}/hydra-statistics.XXXXXX")" || return 1
    trap 'rm -f "$_wo_tmp"' EXIT HUP INT TERM
    workflow_statistics_data >"$_wo_tmp" || { printf '%s\n' 'statistics unavailable'; return 0; }
    while IFS="$(printf '\t')" read -r _wo_tag _wo_a _wo_b _wo_c _wo_d _wo_e _wo_f _wo_g _wo_h _wo_i _wo_j _wo_k; do
        case "$_wo_tag" in
            HYDRA_STATISTICS) printf 'statistics snapshot=%s evidence=known\n' "$_wo_b" ;;
            R) printf 'run id=%s workflow=%s state=%s recoveries=%s evidence=%s\n' "$_wo_a" "$_wo_b" "$_wo_c" "${_wo_j:--}" "$( [ "$_wo_c" = - ] && printf unknown || printf known )" ;;
            X) printf 'warning %s evidence=unknown\n' "$_wo_a" ;;
            Z) printf 'coverage runs=%s steps=%s evidence=known\n' "$_wo_a" "$_wo_b" ;;
        esac
    done <"$_wo_tmp"
)

workflow_observability_compare() (
    [ "$#" -eq 2 ] || return 2
    for _wo_file in "$1" "$2"; do
        [ -f "$_wo_file" ] && [ ! -L "$_wo_file" ] || { printf '{"schema_version":1,"ok":true,"availability":"unavailable","reason":"comparison feed unavailable"}\n'; return 0; }
    done
    _wo_l_runs=0 _wo_l_steps=0 _wo_r_runs=0 _wo_r_steps=0
    while IFS="$(printf '\t')" read -r _wo_tag _wo_a _wo_b; do
        case "$_wo_tag" in R) _wo_l_runs=$((_wo_l_runs + 1));; S) _wo_l_steps=$((_wo_l_steps + 1));; esac
    done <"$1"
    while IFS="$(printf '\t')" read -r _wo_tag _wo_a _wo_b; do
        case "$_wo_tag" in R) _wo_r_runs=$((_wo_r_runs + 1));; S) _wo_r_steps=$((_wo_r_steps + 1));; esac
    done <"$2"
    printf '{"schema_version":1,"ok":true,"availability":"known","left":{"runs":%s,"steps":%s},"right":{"runs":%s,"steps":%s},"delta":{"runs":%s,"steps":%s},"evidence":"recorded_sample"}\n' \
        "$_wo_l_runs" "$_wo_l_steps" "$_wo_r_runs" "$_wo_r_steps" "$((_wo_r_runs - _wo_l_runs))" "$((_wo_r_steps - _wo_l_steps))"
)
