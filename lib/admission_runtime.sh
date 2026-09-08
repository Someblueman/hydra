#!/bin/sh
# Admission waits run inside an existing execution owner, never a scheduler.

admission_wait_cleanup() {
    [ "$_aw_granted" -eq 0 ] && [ "$_aw_owned" -eq 1 ] || return 0
    # No command has started in this helper. A signal may arrive after the
    # authority grants a slot but before its response is consumed.
    # A fresh shell is required here: Bash 3.2 does not run a subshell's EXIT
    # trap when the subshell is invoked from an EXIT trap already in progress.
    if ! "$HYDRA_BIN_PATH" admission cancel "$_aw_id" >/dev/null 2>&1; then
        "$HYDRA_BIN_PATH" admission release "$_aw_id" --confirmed >/dev/null 2>&1 || true
    fi
}

admission_wait() (
    _aw_id="$1" _aw_project="$2" _aw_response="$3"
    _aw_seconds="${HYDRA_ADMISSION_QUEUE_SECONDS:-60}"
    _aw_labels="${HYDRA_ADMISSION_LABELS:--}"
    _aw_granted=0 _aw_owned=0
    trap 'admission_wait_cleanup' 0
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    if ! cmd_admission request "$_aw_id" "$_aw_project" "$_aw_seconds" "$_aw_labels" > "$_aw_response"; then
        cat "$_aw_response" >&2
        exit 125
    fi
    _aw_owned=1
    while :; do
        # This closed shell-owned response has only restricted-token scalars.
        _aw_state="$(sed -n 's/.*"state":"\([^"]*\)".*/\1/p' "$_aw_response")"
        case "$_aw_state" in
            reserved) _aw_granted=1; exit 0 ;;
            queued) ;;
            *) cat "$_aw_response" >&2; exit 125 ;;
        esac
        sleep 0.2
        if ! cmd_admission claim "$_aw_id" > "$_aw_response"; then
            cat "$_aw_response" >&2
            exit 125
        fi
    done
)
