#!/bin/sh
# Private native-TUI adapter: a bounded, read-only text view of the workflow
# step that last ran on a head, in place of a terminal preview for headless
# heads. The live provider stream exists only while an agent step runs; a
# finished step shows its receipt summary and declared result instead, since
# keeping provider output remains the explicit --retain choice.

# Usage: tui_head_output_clean -> stdin without NUL and C0 controls other than
# TAB, LF, CR and ESC. The native view renders SGR color from ESC sequences and
# neutralizes every other escape sequence; deleting ESC alone would leave
# fragments such as "[31m" in the text.
tui_head_output_clean() {
    LC_ALL=C tr -d '\000-\010\013\014\016-\032\034-\037\177'
}

# Usage: tui_head_output_tail <bytes> <file> -> the file's last bytes from a
# line start, so a cut never begins inside a character or escape sequence.
tui_head_output_tail() {
    if [ "$(wc -c < "$2")" -gt "$1" ]; then
        tail -c "$1" "$2" | LC_ALL=C sed '1d'
    else
        cat "$2"
    fi
}

# Usage: tui_head_output_step <runs-dir> <branch>
# Prints "started<TAB>run<TAB>step<TAB>kind<TAB>profile<TAB>attempts<TAB>result-file"
# for the most recently started non-spawn step on the branch.
tui_head_output_step() {
    for _thos_graph in "$1"/run_*/graph.tsv; do
        [ -f "$_thos_graph" ] || continue
        _thos_dir="$(dirname "$_thos_graph")"
        awk -F '\t' -v OFS='\t' -v dir="$_thos_dir" -v branch="$2" '
            function first(path,   line) { line = ""; if ((getline line < path) <= 0) line = ""; close(path); return line }
            $1 == "profile_args" { result[$2] = $5 }
            $1 == "step" && $3 != "spawn" && $7 == branch { steps[++n] = $2; kind[$2] = $3; profile[$2] = $10 }
            END {
                run = dir; sub(/^.*\//, "", run)
                for (i = 1; i <= n; i++) {
                    s = steps[i]; attempts = first(dir "/steps/" s "/attempts")
                    if (attempts !~ /^[1-9][0-9]*$/) continue
                    started = first(dir "/steps/" s "/started-at")
                    if (started !~ /^[0-9]+$/) started = 0
                    print started, run, s, kind[s], profile[s], attempts, (s in result ? result[s] : "-")
                }
            }
        ' "$_thos_graph"
    done | LC_ALL=C sort -t "$(printf '\t')" -k1,1nr | sed -n '1p'
}

# Usage: tui_head_output_result <attempt-dir> <result-file|->
# Shows the declared result, or the sealed artifacts of the attempt (at most
# three) when the step names no single result file. Unsealed outputs are
# shown only when nothing was sealed.
tui_head_output_result() {
    _thor_shown=0
    for _thor_kind in artifacts outputs; do
        [ "$_thor_shown" -eq 0 ] || return 0
        for _thor_path in "$1/$_thor_kind"/*; do
            [ -f "$_thor_path" ] && [ "$_thor_shown" -lt 3 ] || continue
            _thor_name="$(basename "$_thor_path")"
            [ "$2" = - ] || [ "$_thor_name" = "$2" ] || continue
            printf 'Result (%s):\n' "$_thor_name"
            tui_head_output_tail 1024 "$_thor_path" | tui_head_output_clean | tail -n 12 | awk '{ print }'
            _thor_shown=$((_thor_shown + 1))
        done
    done
}

# Usage: tui_head_output_exec <project-dir> <head-id> <attempt-dir> <step-started>
# The exec run a step attempt started: named by its output once the command
# finishes, otherwise the newest exec of the head that began with the step.
tui_head_output_exec() {
    _thoe_run="$(dd if="$3/stdout" bs=256 count=1 2>/dev/null |
        sed -n -e 's/^.*"run_id":"\(run_[a-z0-9_]*\)".*$/\1/p' -e 's/^Exec run \(run_[a-z0-9_]*\)$/\1/p' | sed -n '1p')"
    if [ -n "$_thoe_run" ]; then printf '%s\n' "$_thoe_run"; return 0; fi
    for _thoe_dir in "$1"/exec/run_*/"$2"; do
        [ -f "$_thoe_dir/started-at" ] || continue
        printf '%s\t%s\n' "$(sed -n '1p' "$_thoe_dir/started-at")" "$(basename "$(dirname "$_thoe_dir")")"
    done | awk -F '\t' -v since="$4" '$1 ~ /^[0-9]+$/ && $1 >= since' | LC_ALL=C sort -t "$(printf '\t')" -k1,1nr | sed -n '1s/^.*\t//p'
}

# Usage: tui_head_output_agent <exec-head-dir> <profile> <attempt-dir> <result-file> <state>
tui_head_output_agent() {
    if [ -f "$1/.provider-stdout" ] && [ "$5" = running ]; then
        printf 'Live output (read-only; refreshes while the step runs):\n'
        (cmd_fleet_dispatch agent-view stream-view "$2" "$1/.provider-stdout" 60) 2>/dev/null |
            tui_head_output_clean || printf 'The live output could not be read.\n'
        return 0
    fi
    if [ ! -f "$1/agent.json" ]; then
        printf 'Waiting for the agent to start.\n'
        return 0
    fi
    [ "$5" = running ] && { printf 'The agent has started; its output appears here as it arrives.\n'; return 0; }
    printf 'The step finished (%s). Provider output is kept only while the step runs\n' "$5"
    printf 'unless retention was requested; its receipt and declared result remain.\n'
    tui_head_output_result "$3" "$4"
}

# Usage: tui_head_output_command <exec-head-dir>
tui_head_output_command() {
    for _thoc_stream in stdout stderr; do
        [ -s "$1/$_thoc_stream" ] || continue
        printf 'Command %s (last lines):\n' "$_thoc_stream"
        tui_head_output_tail 3072 "$1/$_thoc_stream" | tui_head_output_clean | tail -n 30
    done
    [ -s "$1/stdout" ] || [ -s "$1/stderr" ] || printf 'The command printed no output.\n'
}

# Usage: tui_head_output <branch>
tui_head_output() (
    [ "$#" -eq 1 ] && [ -n "$1" ] || exit 2
    _tho_project="$(hydra_get_project_id 2>/dev/null)" || { printf 'Hydra project identity is unavailable.\n'; exit 0; }
    _tho_project_dir="$(state_v2_project_dir "$_tho_project")" || exit 0
    _tho_head="$(state_v2_find_head_by_branch "$_tho_project" "$1" 2>/dev/null)" || _tho_head=""
    _tho_step="$(tui_head_output_step "$_tho_project_dir/workflows/runs" "$1")"
    if [ -z "$_tho_step" ]; then
        printf 'No workflow step has run on %s yet.\n' "$1"
        exit 0
    fi
    IFS="$(printf '\t')" read -r _tho_started _tho_run _tho_id _tho_kind _tho_profile _tho_attempt _tho_result <<EOF
$_tho_step
EOF
    _tho_run_dir="$_tho_project_dir/workflows/runs/$_tho_run"
    _tho_attempt_dir="$_tho_run_dir/steps/$_tho_id/attempt-$_tho_attempt"
    _tho_state="$(sed -n '1p' "$_tho_run_dir/steps/$_tho_id/state" 2>/dev/null)" || true
    printf 'Step %s (%s%s) %s, attempt %s, run %s\n' "$_tho_id" "$_tho_kind" \
        "$([ "$_tho_profile" = - ] || printf ', %s' "$_tho_profile")" "${_tho_state:-unknown}" "$_tho_attempt" "$_tho_run" | tui_head_output_clean
    _tho_exec=""
    [ -z "$_tho_head" ] || _tho_exec="$(tui_head_output_exec "$_tho_project_dir" "$_tho_head" "$_tho_attempt_dir" "$_tho_started")"
    _tho_exec_dir="$_tho_project_dir/exec/${_tho_exec:-none}/${_tho_head:-none}"
    if [ -z "$_tho_exec" ] || [ -z "$_tho_head" ] || [ ! -d "$_tho_exec_dir" ]; then
        if [ "$_tho_state" = running ]; then printf 'Waiting for the step to start its command.\n'
        else printf 'No command output is recorded for this step.\n'; tui_head_output_result "$_tho_attempt_dir" "$_tho_result"; fi
        exit 0
    fi
    if [ "$_tho_profile" != - ]; then
        tui_head_output_agent "$_tho_exec_dir" "$_tho_profile" "$_tho_attempt_dir" "$_tho_result" "${_tho_state:-unknown}"
    else
        tui_head_output_command "$_tho_exec_dir"
        [ "$_tho_state" = running ] || tui_head_output_result "$_tho_attempt_dir" -
    fi
)
