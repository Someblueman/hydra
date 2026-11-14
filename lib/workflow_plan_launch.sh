#!/bin/sh
# Workspace launch ownership only; admission and scheduling remain in workflow.

workflow_plan_launch_dir() {
    [ "${#1}" -eq 64 ] || return 2
    case "$1" in *[!0-9a-f]*) return 2 ;; esac
    _wpl_project="$(hydra_get_project_id)" || return 1
    _wpl_project_dir="$(state_v2_project_dir "$_wpl_project")" || return 1
    printf '%s/workflows/launches/%s\n' "$_wpl_project_dir" "$1"
}

# Usage: workflow_plan_launch_planning <launch-dir> <head-id> <instance-id>
# Binds the launch to the exact planning head instance that proposed it. The
# association is refused when that instance is no longer current, or when its
# current draft is a revision the user returned for changes.
workflow_plan_launch_planning() {
    _wplp_project="$(hydra_get_project_id)" || return 1
    _wplp_head_dir="$(state_v2_head_dir "$_wplp_project" "$2")" || return 1
    [ "$(sed -n '1p' "$_wplp_head_dir/current-instance" 2>/dev/null)" = "$3" ] || {
        echo 'Launch refused: the planning head instance is no longer current.'
        return 1
    }
    _load_lib workflow_plan_proposal
    if workflow_plan_returned "$_wplp_head_dir/planning"; then
        echo 'Launch refused: this proposal was returned for changes.'
        return 1
    fi
    workflow_atomic_scalar "$1/planning-head" "$2" &&
        workflow_atomic_scalar "$1/planning-instance" "$3" &&
        workflow_atomic_scalar "$1/planning-branch" "$(sed -n '1p' "$_wplp_head_dir/branch")"
}

# Usage: workflow_plan_launch_failure <launch-dir>
# Records in <launch-dir>/failure why a launch ended without a run receipt, as
# code<TAB>message<TAB>recovery ("-" when absent), from the owner's log: its last
# JSON error envelope, else its last "Error:"/"Next:" pair, else its last line.
workflow_plan_launch_failure() {
    [ ! -f "$1/run-id" ] || return 0
    awk '
        function value(line, key,   i, c, out) {
            i = index(line, "\"" key "\":\"")
            if (!i) return "-"
            out = ""
            for (i += length(key) + 4; i <= length(line); i++) {
                c = substr(line, i, 1)
                if (c == "\"") break
                if (c == "\\") { i++; c = substr(line, i, 1); if (c == "n" || c == "t") c = " " }
                out = out c
            }
            return out == "" ? "-" : out
        }
        function clean(text) { gsub(/[\t\r]/, " ", text); return substr(text, 1, 400) }
        /^\{"schema_version":1,"ok":false,/ { code = value($0, "code"); message = value($0, "message"); recovery = value($0, "recovery"); next }
        /^Error: / { error = substr($0, 8); next_step = "-"; next }
        /^Next: / { next_step = substr($0, 7); next }
        NF { last = $0 }
        END {
            if (message == "" && error != "") { code = "-"; message = error; recovery = next_step }
            if (message == "" && last != "") { code = "-"; message = last; recovery = "-" }
            if (message != "") printf "%s\t%s\t%s\n", clean(code), clean(message), clean(recovery)
        }
    ' "$1/owner.log" > "$1/failure.tmp" 2>/dev/null && mv "$1/failure.tmp" "$1/failure"
}

# Usage: workflow_plan_launch_retire <launch-dir>
# The user may launch the same revision again only after its launch ended
# without a run receipt: refused (failed), or finished with a non-zero exit
# before a run was created. The old record moves aside to <dir>.failed-*/.
# A launch that is starting, crashed before recording its exit, or has a run
# receipt is kept, so nothing is retried automatically or run twice; admission
# also refuses the spawn branches an earlier run of the revision created.
workflow_plan_launch_retire() {
    [ ! -e "$1/run-id" ] || return 1
    case "$(sed -n '1p' "$1/state" 2>/dev/null)" in
        failed) ;;
        finished) [ "$(sed -n '1p' "$1/exit-code" 2>/dev/null)" != 0 ] || return 1 ;;
        *) return 1 ;;
    esac
    _wplr_dir="$(mktemp -d "$1.failed-XXXXXX")" || return 1
    mv "$1" "$_wplr_dir/launch"
}

# stdin is an already-open compiled snapshot. The caller starts this owner in
# an independent process group with SIGHUP ignored and no terminal descriptors.
# A reservation is never retried automatically, even after an uncertain crash.
# Optional <head-id> <instance-id> name the planning conversation to notify.
workflow_plan_launch_owner() (
    [ "$#" -eq 1 ] || [ "$#" -eq 3 ] || exit 2
    if [ "$#" -eq 3 ]; then hydra_valid_id "$2" && hydra_valid_id "$3" || exit 2; fi
    _workflow_plan_launch="$(workflow_plan_launch_dir "$1")" || exit $?
    _wpl_user_umask="$(umask)"
    umask 077
    mkdir -p "$(dirname "$_workflow_plan_launch")" || exit 1
    if ! mkdir "$_workflow_plan_launch" 2>/dev/null; then
        workflow_plan_launch_retire "$_workflow_plan_launch" || exit 1
        mkdir "$_workflow_plan_launch" 2>/dev/null || exit 1
    fi
    exec > "$_workflow_plan_launch/owner.log" 2>&1
    workflow_atomic_scalar "$_workflow_plan_launch/state" starting || exit 1
    if [ "$#" -eq 3 ] && ! workflow_plan_launch_planning "$_workflow_plan_launch" "$2" "$3"; then
        workflow_plan_launch_failure "$_workflow_plan_launch"
        workflow_atomic_scalar "$_workflow_plan_launch/state" failed
        exit 1
    fi
    if ! cat > "$_workflow_plan_launch/compiled.json"; then
        echo 'Launch refused: the compiled plan could not be read.'
        workflow_plan_launch_failure "$_workflow_plan_launch"
        workflow_atomic_scalar "$_workflow_plan_launch/state" failed
        exit 1
    fi
    # The run keeps its own records private; its heads, agents and commands
    # inherit the user's umask like any other workflow run.
    umask "$_wpl_user_umask"
    # Under `set -e` a failed run must still record the owner's exit.
    if cmd_workflow_plan run "$_workflow_plan_launch/compiled.json" --accept "$1"; then
        _wpl_result=0
    else
        _wpl_result=$?
    fi
    [ "$_wpl_result" -eq 0 ] || workflow_plan_launch_failure "$_workflow_plan_launch"
    workflow_atomic_scalar "$_workflow_plan_launch/exit-code" "$_wpl_result"
    workflow_atomic_scalar "$_workflow_plan_launch/state" finished
    exit "$_wpl_result"
)

workflow_plan_launch_status() (
    [ "$#" -eq 1 ] || exit 2
    _wpls_dir="$(workflow_plan_launch_dir "$1")" || exit $?
    _wpls_state=absent _wpls_run=- _wpls_exit=-
    if [ -d "$_wpls_dir" ]; then
        _wpls_state=unknown
        [ ! -f "$_wpls_dir/state" ] || _wpls_state="$(sed -n '1p' "$_wpls_dir/state")"
        [ ! -f "$_wpls_dir/run-id" ] || _wpls_run="$(sed -n '1p' "$_wpls_dir/run-id")"
        [ ! -f "$_wpls_dir/exit-code" ] || _wpls_exit="$(sed -n '1p' "$_wpls_dir/exit-code")"
    fi
    printf 'HYDRA_PLAN_LAUNCH\t1\nL\t%s\t%s\t%s\t%s\n' "$1" "$_wpls_state" "$_wpls_run" "$_wpls_exit"
    # A launch that ended without a run receipt also says why.
    if [ "$_wpls_run" = - ] && [ -f "$_wpls_dir/failure" ]; then
        printf 'F\t%s\n' "$(sed -n '1p' "$_wpls_dir/failure")"
    fi
)
