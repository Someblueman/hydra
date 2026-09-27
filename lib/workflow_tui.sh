#!/bin/sh
# Read-only projection of recorded workflow runs. No definition execution.
#
# Protocol 2 (internal, versioned with the native TUI; fields never contain
# tabs or newlines, "-" means unknown or not applicable):
#   W run name state kind planning-branch created-at completed-at digest12
#   N run step kind state attempts needs role head profile started completed
#   E run step attempt <agent receipt summary: see agent-view receipt-view>
#   R run branch spawn-step class retirement detail
#   X warning
# Only recorded state is read. Derived role and receipt summaries of finished
# attempts are cached under $HYDRA_HOME/cache, which is never an authority.

# Usage: workflow_tui_roles <run-dir> <run-id> -> path of a step-role table
workflow_tui_roles() {
    if [ -f "$1/plan-roles.tsv" ]; then printf '%s\n' "$1/plan-roles.tsv"; return 0; fi
    [ -f "$1/compiled.json" ] || { printf '/dev/null\n'; return 0; }
    # Runs recorded before role tables existed: derive once into the cache.
    _wtr_cache="$HYDRA_HOME/cache/tui-workflow/$2/roles.tsv"
    if [ ! -f "$_wtr_cache" ]; then
        mkdir -p "$(dirname "$_wtr_cache")" 2>/dev/null &&
            (workflow_plan_tool roles "$1/compiled.json") > "$_wtr_cache.$$" 2>/dev/null &&
            mv "$_wtr_cache.$$" "$_wtr_cache" 2>/dev/null
        rm -f "$_wtr_cache.$$" 2>/dev/null
    fi
    if [ -f "$_wtr_cache" ]; then printf '%s\n' "$_wtr_cache"; else printf '/dev/null\n'; fi
}

# Usage: workflow_tui_receipt <exec-root> <run-dir> <run-id> <step> <attempt>
# Prints one E row for an agent step attempt. A finished attempt's summary is
# immutable and cached; a running one is read again on each observation.
workflow_tui_receipt() {
    _wtx_attempt_dir="$2/steps/$4/attempt-$5"
    _wtx_cache="$HYDRA_HOME/cache/tui-workflow/$3/$4.$5.tsv"
    if [ -f "$_wtx_cache" ]; then cat "$_wtx_cache"; return 0; fi
    # The exec envelope publishes its run ID before the agent starts.
    _wtx_exec="$(dd if="$_wtx_attempt_dir/stdout" bs=256 count=1 2>/dev/null | sed -n 's/^.*"run_id":"\(run_[a-z0-9_]*\)".*$/\1/p' | sed -n '1p')"
    _wtx_receipt=""
    if [ -n "$_wtx_exec" ] && [ -d "$1/$_wtx_exec" ]; then
        for _wtx_candidate in "$1/$_wtx_exec"/head_*/agent.json; do
            [ -f "$_wtx_candidate" ] && _wtx_receipt="$_wtx_candidate" && break
        done
    fi
    if [ -z "$_wtx_receipt" ]; then
        printf 'E\t%s\t%s\t%s\t-\tpending\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\n' "$3" "$4" "$5"
        return 0
    fi
    _wtx_row="$(cmd_fleet_dispatch agent-view receipt-view "$_wtx_receipt" 2>/dev/null)" || _wtx_row=""
    _wtx_newline='
'
    case "$_wtx_row" in
        *"$_wtx_newline"*|'') printf 'E\t%s\t%s\t%s\t-\tunavailable\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\n' "$3" "$4" "$5"; return 0 ;;
    esac
    _wtx_line="$(printf 'E\t%s\t%s\t%s\t%s' "$3" "$4" "$5" "$_wtx_row")"
    printf '%s\n' "$_wtx_line"
    if [ -f "$_wtx_attempt_dir/completed-at" ] && mkdir -p "$(dirname "$_wtx_cache")" 2>/dev/null; then
        printf '%s\n' "$_wtx_line" > "$_wtx_cache.$$" 2>/dev/null && mv "$_wtx_cache.$$" "$_wtx_cache" 2>/dev/null
        rm -f "$_wtx_cache.$$" 2>/dev/null
    fi
    return 0
}

# Usage: workflow_tui_run_rows <run-dir> <run-id> <roles-file> <node-budget>
# One pass over the recorded graph: N rows, Q requests for agent receipts,
# and R rows for the heads the run's spawn steps created.
workflow_tui_run_rows() {
    awk -F '\t' -v OFS='\t' -v dir="$1" -v run="$2" -v roles="$3" -v budget="$4" -v plan="$([ -f "$1/compiled.json" ] && echo 1 || echo 0)" '
        function first(path,   line) {
            line = "-"
            if ((getline line < path) <= 0 || line == "") line = "-"
            close(path)
            gsub(/[\t\r]/, " ", line)
            return line
        }
        BEGIN {
            while ((getline line < roles) > 0) {
                split(line, f, "\t")
                role[f[1]] = f[2]
                if (f[3] != "spawn" && f[4] != "-" && f[4] != "") {
                    if (f[2] == "verify") verifies[f[4]] = 1
                    else works[f[4]] = 1
                }
            }
            close(roles)
        }
        $1 == "step" {
            if ($2 !~ /^[a-z][a-z0-9_-]*$/) { print "X", "Invalid recorded step ID"; next }
            if (++steps > 128 || steps > budget) { print "X", "Workflow exceeds visualization node limit"; exit 3 }
            sd = dir "/steps/" $2
            state = first(sd "/state"); attempts = first(sd "/attempts")
            if (attempts !~ /^[0-9]+$/) attempts = 0
            started = first(sd "/started-at")
            completed = attempts > 0 ? first(sd "/attempt-" attempts "/completed-at") : "-"
            head = $3 == "spawn" ? $8 : $7
            if (head == "") head = "-"
            print "N", run, $2, $3, state, attempts, $4, ($2 in role ? role[$2] : "-"), head, ($10 == "" ? "-" : $10), started, completed
            if ($3 == "exec" && $10 != "-" && $10 != "" && attempts > 0) print "Q", $2, attempts
            if ($3 == "spawn" && head != "-") { spawned[++count] = head; spawn_step[count] = $2 }
        }
        END {
            for (i = 1; i <= count; i++) {
                b = spawned[i]
                class = !plan ? "run" : (b in works) ? "worker" : (b in verifies) ? "verifier" : "worker"
                rd = dir "/retirement/" spawn_step[i]
                print "R", run, b, spawn_step[i], class, first(rd "/state"), first(rd "/detail")
            }
        }
    ' "$1/graph.tsv"
}

workflow_tui_data() (
    printf 'HYDRA_WORKFLOW_TUI\t2\n'
    _wtd_root="$(workflow_runs_dir 2>/dev/null)" || return 0
    [ -d "$_wtd_root" ] || return 0
    _wtd_exec="$(dirname "$(dirname "$_wtd_root")")/exec"
    _wtd_runs=0
    _wtd_nodes=0
    for _wtd_dir in "$_wtd_root"/run_*; do
        [ -d "$_wtd_dir" ] || continue
        _wtd_id="$(basename "$_wtd_dir")"
        hydra_valid_id "$_wtd_id" || continue
        _wtd_runs=$((_wtd_runs + 1))
        if [ "$_wtd_runs" -gt 32 ]; then printf 'X\tMore than 32 runs; showing first 32 by ID\n'; break; fi
        _wtd_name="$(sed -n '1p' "$_wtd_dir/workflow-id" 2>/dev/null || printf unavailable)"
        _wtd_state="$(sed -n '1p' "$_wtd_dir/state" 2>/dev/null || printf unavailable)"
        if [ "$_wtd_state" = running ] && ! workflow_run_owner_fresh "$_wtd_dir"; then _wtd_state=stale; fi
        _wtd_kind=workflow
        [ ! -f "$_wtd_dir/compiled.json" ] || _wtd_kind=plan
        _wtd_planning="$(sed -n '1p' "$_wtd_dir/planning-branch" 2>/dev/null)" || true
        _wtd_created="$(sed -n '1p' "$_wtd_dir/created-at" 2>/dev/null)" || true
        _wtd_completed="$(sed -n '1p' "$_wtd_dir/completed-at" 2>/dev/null)" || true
        _wtd_digest="$(sed -n '1p' "$_wtd_dir/plan-accepted" 2>/dev/null | cut -c1-12)"
        printf 'W\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$_wtd_id" "${_wtd_name:-unavailable}" "${_wtd_state:-unavailable}" \
            "$_wtd_kind" "${_wtd_planning:--}" "${_wtd_created:--}" "${_wtd_completed:--}" "${_wtd_digest:--}" | tr '\r' ' '
        if [ ! -f "$_wtd_dir/graph.tsv" ]; then printf 'X\tRecorded workflow graph unavailable\n'; continue; fi
        _wtd_roles="$(workflow_tui_roles "$_wtd_dir" "$_wtd_id")"
        _wtd_budget=$((512 - _wtd_nodes))
        _wtd_over=0
        _wtd_rows="$(workflow_tui_run_rows "$_wtd_dir" "$_wtd_id" "$_wtd_roles" "$_wtd_budget")" || _wtd_over=$?
        while IFS="$(printf '\t')" read -r _wtd_tag _wtd_a _wtd_b _wtd_rest; do
            case "$_wtd_tag" in
                Q) workflow_tui_receipt "$_wtd_exec" "$_wtd_dir" "$_wtd_id" "$_wtd_a" "$_wtd_b" ;;
                N) _wtd_nodes=$((_wtd_nodes + 1)); printf '%s\t%s\t%s\t%s\n' "$_wtd_tag" "$_wtd_a" "$_wtd_b" "$_wtd_rest" ;;
                '') ;;
                *) if [ -n "$_wtd_rest" ]; then printf '%s\t%s\t%s\t%s\n' "$_wtd_tag" "$_wtd_a" "$_wtd_b" "$_wtd_rest"
                   elif [ -n "$_wtd_b" ]; then printf '%s\t%s\t%s\n' "$_wtd_tag" "$_wtd_a" "$_wtd_b"
                   else printf '%s\t%s\n' "$_wtd_tag" "$_wtd_a"; fi ;;
            esac
        done <<EOF
$_wtd_rows
EOF
        [ "$_wtd_over" -ne 3 ] || return 0
    done
)
