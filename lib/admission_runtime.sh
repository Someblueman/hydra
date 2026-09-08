#!/bin/sh
# Admission waits run inside an existing execution owner, never a scheduler.

# Receiver-owned Git metadata binds isolated workspaces to their mapped project.
# This is scalar data, not sourced shell code. IDs retain a task prefix so that
# confirmed receiver cancellation can reconcile only that task's reservations.
admission_binding() {
    ADMISSION_ID="$1" ADMISSION_PROJECT="$2"
    _ab_common="$(hydra_git_common_dir 2>/dev/null)" || return 1
    _ab_context="$_ab_common/hydra/admission-context"
    [ -e "$_ab_context" ] || [ -L "$_ab_context" ] || return 0
    [ -f "$_ab_context" ] && [ ! -L "$_ab_context" ] || return 1
    IFS=' ' read -r _ab_task _ab_project _ab_seconds _ab_labels _ab_extra < "$_ab_context" || return 1
    [ -z "$_ab_extra" ] && [ "$(wc -l < "$_ab_context" | tr -d ' ')" = 1 ] || return 1
    case "$_ab_task" in task_*) ;; *) return 1 ;; esac
    admission_token "$_ab_task" && admission_token "$_ab_project" &&
        admission_number "$_ab_seconds" && admission_labels "$_ab_labels" || return 1
    ADMISSION_ID="$_ab_task.$1" ADMISSION_PROJECT="$_ab_project"
    admission_token "$ADMISSION_ID" || return 1
    HYDRA_ADMISSION_QUEUE_SECONDS="$_ab_seconds" HYDRA_ADMISSION_LABELS="$_ab_labels"
}

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

# Run a synchronous command after admission; an interrupted owner never executes
# the release below. The caller supplies a fresh execution identity and directory.
admission_command() (
    admission_binding "$1" "$2" || exit 125
    _ac_id="$ADMISSION_ID" _ac_project="$ADMISSION_PROJECT" _ac_evidence="$3"
    shift 3
    admission_wait "$_ac_id" "$_ac_project" "$_ac_evidence/admission.json" || exit 125
    if "$@"; then _ac_status=0; else _ac_status=$?; fi
    cmd_admission release "$_ac_id" --confirmed > "$_ac_evidence/admission-release.json" || true
    exit "$_ac_status"
)

# The lifecycle callback binds HEAD_ADMISSION_ID before launching an agent, and
# marks HEAD_ADMISSION_EFFECTS before setup or agent execution can begin.
admission_head() (
    admission_binding "$1" "$2" || exit 125
    HEAD_ADMISSION_ID="$ADMISSION_ID" _ah_project="$ADMISSION_PROJECT" _ah_profile="$3"
    shift 3
    HEAD_ADMISSION_EFFECTS=0
    _ah_tmp="$(mktemp -d "$HYDRA_HOME/.admission-head.XXXXXX")" || exit 1
    trap 'rm -rf "$_ah_tmp"' 0
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    admission_wait "$HEAD_ADMISSION_ID" "$_ah_project" "$_ah_tmp/admission.json" || exit 125
    if "$@"; then _ah_status=0; else _ah_status=$?; fi
    if { [ "$_ah_status" -eq 0 ] && [ "$_ah_profile" = none ]; } ||
        { [ "$_ah_status" -ne 0 ] && [ "$HEAD_ADMISSION_EFFECTS" -eq 0 ]; }; then
        cmd_admission release "$HEAD_ADMISSION_ID" --confirmed >/dev/null || exit 1
    elif [ "$_ah_status" -ne 0 ]; then
        cmd_admission unknown "$HEAD_ADMISSION_ID" >/dev/null || true
    fi
    exit "$_ah_status"
)
