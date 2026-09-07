#!/bin/sh
# One durable retry decision for live completion and recovered attempts.

workflow_attempt_result() {
    _war_dir="$1" _war_id="$2" _war_limit="$3" _war_idem="$4" _war_code="$5"
    _war_sd="$_war_dir/steps/$_war_id"
    _war_attempt="$(sed -n '1p' "$_war_sd/attempts")"
    _war_ad="$_war_sd/attempt-$_war_attempt"
    _war_class=failure
    case "$_war_code" in
        unknown) _war_class=interrupted ;;
        0) _war_class=success ;;
        124) _war_class=timeout ;;
        126|127) _war_class=configuration ;;
        *) [ "$_war_code" -le 128 ] || _war_class=interrupted ;;
    esac
    workflow_atomic_scalar "$_war_ad/failure-class" "$_war_class" || return 1
    _war_classes="$(awk -F '\t' -v id="$_war_id" '$1=="retry_policy" && $2==id {print $3; exit}' "$_war_dir/graph.tsv")"
    [ -n "$_war_classes" ] || _war_classes=failure,timeout,interrupted,configuration
    _war_allowed=0
    case ",$_war_classes," in *",$_war_class,"*) _war_allowed=1 ;; esac
    if [ "$_war_class" = success ]; then
        _war_state=succeeded
    elif [ "$_war_idem" = true ] && [ "$_war_attempt" -le "$_war_limit" ] && [ "$_war_allowed" -eq 1 ]; then
        _war_delay="$(awk -F '\t' -v id="$_war_id" '$1=="retry_policy" && $2==id {print $4; exit}' "$_war_dir/graph.tsv")"
        _war_delay="${_war_delay:-0}"
        _war_power=1
        while [ "$_war_power" -lt "$_war_attempt" ] && [ "$_war_delay" -lt 86400 ]; do
            _war_delay=$((_war_delay * 2))
            _war_power=$((_war_power + 1))
        done
        [ "$_war_delay" -le 86400 ] || _war_delay=86400
        # Persist once: restarting must not reset the backoff clock.
        if [ ! -f "$_war_ad/retry-at" ]; then
            _war_finished="$(sed -n '1p' "$_war_ad/completed-at" 2>/dev/null || true)"
            _war_finished="${_war_finished:-$(date +%s)}"
            workflow_atomic_scalar "$_war_ad/retry-at" "$((_war_finished + _war_delay))" || return 1
        fi
        _war_state=retrying
    elif [ "$_war_class" = interrupted ]; then
        _war_state=recovery-required
    else
        _war_state=failed
    fi
    case "$_war_state" in succeeded|failed)
        workflow_atomic_scalar "$_war_sd/authoritative-attempt" "$_war_attempt" || return 1 ;;
    esac
    workflow_atomic_scalar "$_war_sd/state" "$_war_state" || return 1
    workflow_event "$_war_dir" "$_war_id" "step.$_war_state" "attempt=$_war_attempt class=$_war_class"
}

workflow_retry_ready() {
    _wry_dir="$1"
    for _wry_sd in "$_wry_dir"/steps/*; do
        [ "$(sed -n '1p' "$_wry_sd/state")" = retrying ] || continue
        _wry_attempt="$(sed -n '1p' "$_wry_sd/attempts")"
        _wry_at="$(sed -n '1p' "$_wry_sd/attempt-$_wry_attempt/retry-at" 2>/dev/null || true)"
        case "$_wry_at" in ''|*[!0-9]*)
            workflow_atomic_scalar "$_wry_sd/state" recovery-required
            continue ;;
        esac
        if [ "$(date +%s)" -ge "$_wry_at" ]; then
            workflow_atomic_scalar "$_wry_sd/state" ready || return 1
        fi
    done
}
