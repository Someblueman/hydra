#!/bin/sh
# Bounded human-readable observations of the existing run and artifact records.

workflow_evidence_tail() {
    printf '\n%s (last 8192 bytes)\n' "$2"
    if [ -f "$1" ] && [ ! -L "$1" ]; then
        tail -c 8192 "$1"
        printf '\n'
    else printf 'Not recorded.\n'; fi
}

workflow_evidence_requests() (
    _wer_count=0
    for _wer_step in "$1"/steps/*; do
        [ -f "$_wer_step/request-id" ] || continue
        _wer_id="$(sed -n '1p' "$_wer_step/request-id")"
        hydra_valid_id "$_wer_id" || exit 1
        _wer_dir="$1/approvals/$_wer_id"
        _wer_expires="$(sed -n '1p' "$_wer_dir/expires-at" 2>/dev/null || printf unavailable)"
        [ "$_wer_expires" != 0 ] || _wer_expires='none'
        printf '\nRequest: %s\nStep: %s\nState: %s\nMessage: %s\n' \
            "$_wer_id" "$(basename "$_wer_step")" "$(sed -n '1p' "$_wer_dir/state")" "$(sed -n '1p' "$_wer_dir/message")"
        printf 'Binding: %s\nExpires (Unix seconds): %s\nEvidence: %s\nDecision: %s\n' \
            "$(sed -n '1p' "$_wer_dir/binding-hash")" "$_wer_expires" "$_wer_dir/binding.tsv" \
            "$(sed -n '1p' "$_wer_dir/decision/action" 2>/dev/null || printf none)"
        _wer_count=$((_wer_count + 1))
    done
    [ "$_wer_count" -gt 0 ] || printf 'No recorded input requests.\n'
)

workflow_evidence() (
    [ "$#" -eq 2 ] && hydra_valid_id "$1" || exit 2
    case "$2" in -) ;; ''|*[!a-z0-9_-]*|[-_0-9]*) exit 2 ;; esac
    _wev_dir="$(workflow_runs_dir)/$1"
    [ -d "$_wev_dir" ] || exit 1
    printf 'HYDRA_WORKFLOW_EVIDENCE\t1\t%s\t%s\n' "$1" "$2"
    cmd_workflow status "$1" || exit 1
    if [ -e "$_wev_dir/retention.json" ] || [ -L "$_wev_dir/retention.json" ]; then
        printf 'Evidence expired under the recorded retention policy.\nHYDRA_WORKFLOW_EVIDENCE_END\texpired\n'
        exit 0
    fi
    if [ "$2" != - ]; then
        _wev_step="$_wev_dir/steps/$2"
        [ -d "$_wev_step" ] || exit 1
        _wev_attempt="$(sed -n '1p' "$_wev_step/attempts")"
        printf '\nSelected step: %s\nObserved state: %s\nAttempt: %s\n' "$2" "$(sed -n '1p' "$_wev_step/state")" "$_wev_attempt"
        case "$_wev_attempt" in
            ''|*[!0-9]*|0) printf 'No execution attempt recorded.\n' ;;
            *)
                _wev_attempt_dir="$_wev_step/attempt-$_wev_attempt"
                printf 'Exit code: %s\n' "$(sed -n '1p' "$_wev_attempt_dir/exit-code" 2>/dev/null || printf unavailable)"
                workflow_evidence_tail "$_wev_attempt_dir/stdout" 'Standard output'
                workflow_evidence_tail "$_wev_attempt_dir/stderr" 'Standard error'
                ;;
        esac
    fi
    printf '\nInput requests (recorded binding, expiry and decision)\n'
    workflow_evidence_requests "$_wev_dir" || exit 1
    workflow_evidence_tail "$_wev_dir/workspace-controls.log" 'Workspace control results'
    printf '\nArtifact verification\n'
    _wev_verdict=unavailable
    if [ ! -f "$_wev_dir/compiled.json" ]; then
        printf 'No compiled-plan verification contract is recorded for this run.\n'
    elif [ "$(sed -n '1p' "$_wev_dir/state")" != succeeded ]; then
        _wev_verdict=pending
        printf 'Not verified: the run has not succeeded.\n'
    elif workflow_plan_tool result-view "$_wev_dir"; then
        _wev_verdict=verified
        printf 'VERIFIED: result retrieval rechecked the sealed artifacts.\n'
    else
        _wev_verdict=refused
        printf 'VERIFICATION REFUSED: recorded success is not sufficient evidence.\n'
    fi
    printf '\nHYDRA_WORKFLOW_EVIDENCE_END\t%s\n' "$_wev_verdict"
)
