#!/bin/sh

# Machine projections are delegated to hydra-core. This shell function is the
# presentation-only announcement path.
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
