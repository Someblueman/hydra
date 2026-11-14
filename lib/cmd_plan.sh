#!/bin/sh
# The short planning flow: an agent proposes a short plan, the user reviews and
# approves it, follows the run and reviews its result. Nothing is copied by
# hand: Hydra finds the proposal, compiles it afresh against the project's plan
# policy and binds the approval to the exact compiled revision it showed.
# Expert planning stays under hydra workflow plan.

cmd_plan_schema() {
    cat <<'EOF'
Hydra short plan form (simple-1). Publish it with: hydra plan propose <plan.json>

{
  "plan": "simple-1",
  "objective": "Add a greeting file and a test for it",
  "steps": [
    {"id": "implement", "agent": "Create greeting.txt with a friendly greeting and tests/greeting.sh that fails unless it is non-empty."},
    {"id": "check", "run": ["make", "check"]}
  ],
  "done_when": [
    {"criterion": "make check passes with the new test", "check": "check"}
  ]
}

plan        Always "simple-1".
objective   What the plan achieves, in a sentence or two.
steps       1 to 63 steps in order. Each has a unique id (lowercase letters and
            digits, separated by single - or _) and exactly one of:
              agent  the worker agent's complete instructions. When it finishes,
                     Hydra commits what it changed on the worker branch.
              run    a command as a list of strings, run in the worker's checkout.
            Optional per step:
              needs    ids of earlier steps it waits for (default: the step before)
              timeout  seconds (default 1800 for agent, 600 for run)
              profile  the agent profile, when the policy allows more than one
done_when   1 to 64 criteria: {"criterion": text, "check": id of a run step}.
            A check runs after an agent step and passes when its command exits 0
            on the commit that agent step left.
id, worker  Optional plan name and worker branch (default: from the objective;
            the worker branch is <id>-work and must not exist yet).
context     Optional repository files the plan relies on (paths).
questions   Open questions for the user; a plan with any cannot be approved.

Every step runs on the one new worker branch; your checkout and other branches
are not changed. The project's plan policy (hydra plan policy) limits the tools
and time a plan may use. The full expert schema: hydra workflow plan schema.
EOF
}

cmd_plan_usage() {
    printf '%s\n' \
        'Usage: hydra plan show [<head>] [--technical | --json]' \
        '       hydra plan approve [<head>] [--accept <sha256>] [--allow-agent-environment]' \
        '       hydra plan status [<run-id>]' \
        '       hydra plan review [<run-id>] [--json]' \
        '       hydra plan policy [--json | --allow-tool <tool> | --remove-tool <tool> | --reset]' \
        '       hydra plan schema' \
        '       hydra plan propose <plan.json> [--branch <head>] [--asset NAME=FILE]...' \
        'With no <head>, the plan is the current head'"'"'s, or the only head'"'"'s with a proposal.' \
        'With no <run-id>, the run is the most recent one a plan started.' \
        'Expert planning (full schema, compile, explain and compare): hydra workflow plan --help'
}

# Usage: cmd_plan_head [<head>]
# Prints the branch of the planning head whose proposal is meant.
cmd_plan_head() {
    if [ -n "${1:-}" ]; then printf '%s\n' "$1"; return 0; fi
    _cph_branch="$(git symbolic-ref --quiet --short HEAD 2>/dev/null || true)"
    if [ -n "$_cph_branch" ] && parallel_head_load "$_cph_branch" >/dev/null 2>&1 &&
        [ -f "$PARALLEL_HEAD_DIR/planning/draft.json" ]; then
        printf '%s\n' "$_cph_branch"; return 0
    fi
    _cph_project="$(hydra_get_project_id)" || { cli_error plan not_initialized 'Hydra project identity is unavailable' 'run hydra init'; return 1; }
    _cph_dir="$(state_v2_project_dir "$_cph_project")" || return 1
    _cph_found=""
    for _cph_head in "$_cph_dir"/heads/head_*; do
        [ -f "$_cph_head/planning/draft.json" ] && [ -f "$_cph_head/current-instance" ] || continue
        _cph_found="$_cph_found$(sed -n '1p' "$_cph_head/branch")
"
    done
    case "$(printf '%s' "$_cph_found" | grep -c .)" in
        0) cli_error plan no_proposal 'no head has a proposed plan yet' 'ask the planning agent to publish one with hydra plan propose'; return 1 ;;
        1) printf '%s' "$_cph_found" ;;
        *)
            cli_error plan ambiguous_proposal "several heads have proposed plans: $(printf '%s' "$_cph_found" | tr '\n' ' ')" 'name one: hydra plan show <head>'
            return 1 ;;
    esac
}

# Usage: cmd_plan_compile <branch> <directory>
# Compiles the head's current proposal afresh, against the project's plan
# policy and the project's checkout, into <directory>/compiled.json. Sets
# _cpc_revision, _cpc_digest, _cpc_draft (draft digest) and _cpc_head_dir.
cmd_plan_compile() {
    parallel_head_load "$1" || { cli_error plan head_not_found "no head named $1" 'name a head from hydra list'; return 1; }
    _cpc_head_dir="$PARALLEL_HEAD_DIR"
    _cpc_dir="$PARALLEL_HEAD_DIR/planning"
    [ -f "$_cpc_dir/draft.json" ] || { cli_error plan no_proposal "head $1 has not proposed a plan" 'ask its agent to publish one with hydra plan propose'; return 1; }
    if workflow_plan_returned "$_cpc_dir"; then
        cli_error plan proposal_returned "you asked for changes to this revision of $1's plan" 'wait for the agent to publish a new revision'
        return 1
    fi
    _cpc_revision="$(sed -n '1p' "$_cpc_dir/revision" 2>/dev/null || true)"
    case "$_cpc_revision" in ''|*[!0-9]*) _cpc_revision=1 ;; esac
    _cpc_draft="$(workflow_plan_draft_digest "$_cpc_dir/draft.json")" || return 1
    workflow_plan_head_policy "$_cpc_head_dir" "$_cpc_dir" || return 1
    _cpc_project="$(hydra_get_project_id)" || return 1
    _cpc_root="$(sed -n '1p' "$(state_v2_project_dir "$_cpc_project")/repo-root" 2>/dev/null || true)"
    [ -d "$_cpc_root" ] || _cpc_root="$(workflow_repo_root)" || return 1
    if ! (cd "$_cpc_root" && workflow_plan_tool compile "$_cpc_dir/draft.json" "$_cpc_dir/policy.json" "$_cpc_root" "$2/compiled.json" \
        --assets-dir "$_cpc_dir/assets") > "$2/compile.json" 2>&1; then
        printf 'Revision %s of the plan from %s cannot be approved:\n' "$_cpc_revision" "$1" >&2
        workflow_plan_diagnostics < "$2/compile.json" >&2
        return 1
    fi
    _cpc_digest="$(sed -n 's/.*"sha256":"\([0-9a-f]\{64\}\)".*/\1/p' "$2/compile.json")"
    [ "${#_cpc_digest}" -eq 64 ]
}

# A private scratch directory beside the head's planning records.
cmd_plan_scratch() {
    hydra_private_mkdir "$1/planning" || return 1
    mktemp -d "$1/planning/.review.XXXXXX"
}

cmd_plan_show() {
    _cps_head="" _cps_mode=summary
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --technical) _cps_mode=technical ;;
            --json) _cps_mode=json ;;
            -*) cli_error plan invalid_arguments "unknown option '$1'" 'run hydra plan --help'; return 1 ;;
            *) [ -z "$_cps_head" ] || { cli_error plan invalid_arguments 'name at most one head' 'run hydra plan --help'; return 1; }; _cps_head="$1" ;;
        esac
        shift
    done
    _cps_branch="$(cmd_plan_head "$_cps_head")" || return 1
    parallel_head_load "$_cps_branch" || return 1
    _cps_tmp="$(cmd_plan_scratch "$PARALLEL_HEAD_DIR")" || return 1
    if ! cmd_plan_compile "$_cps_branch" "$_cps_tmp"; then rm -rf "$_cps_tmp"; return 1; fi
    case "$_cps_mode" in
        technical) workflow_plan_tool preview "$_cps_tmp/compiled.json" ;;
        json) workflow_plan_tool summary "$_cps_tmp/compiled.json" --json ;;
        *)
            printf 'Proposed by %s, revision %s\n\n' "$_cps_branch" "$_cpc_revision"
            workflow_plan_tool summary "$_cps_tmp/compiled.json"
            printf '\nApprove exactly this revision with: hydra plan approve %s\n' "$_cps_branch"
            printf 'Request changes by telling the agent; the technical view is hydra plan show --technical.\n'
            ;;
    esac
    _cps_status=$?
    rm -rf "$_cps_tmp"
    return "$_cps_status"
}

# Usage: cmd_plan_launch <digest> <head-id> <instance-id> <compiled> <root>
# Starts the detached launch owner for an approved revision and waits for its
# run receipt. Sets _cpl_run.
cmd_plan_launch() {
    _load_lib workflow_plan_launch
    _cpl_status="$(workflow_plan_launch_status "$1" | sed -n '2p')"
    _cpl_run="$(printf '%s\n' "$_cpl_status" | cut -f4)"
    if [ -n "$_cpl_run" ] && [ "$_cpl_run" != - ]; then
        cli_error plan already_run "this revision already ran as $_cpl_run" 'review it with hydra plan review; to run the plan again, ask the agent to publish a new revision'
        return 1
    fi
    case "$(printf '%s\n' "$_cpl_status" | cut -f3)" in
        starting|unknown)
            cli_error plan launch_in_progress 'this revision is already being launched' 'follow it with hydra plan status'
            return 1 ;;
    esac
    # The owner outlives this command in a session of its own, ignoring
    # hangups and holding no terminal descriptors, so closing this terminal
    # cannot stop it. Approval from here already passed the agent-environment
    # guard, so the owner is not refused for it.
    (
        cd "$5" || exit 1
        HYDRA_PLAN_ALLOW_AGENT=1 workflow_plan_tool detach "$4" "$HYDRA_BIN_PATH" workflow plan --workspace-owner "$1" "$2" "$3"
    ) > /dev/null || {
        cli_error plan launch_failed 'the plan could not be started' 'approve it again'
        return 1
    }
    _cpl_tries=0
    while [ "$_cpl_tries" -lt 120 ]; do
        _cpl_status="$(workflow_plan_launch_status "$1")"
        _cpl_run="$(printf '%s\n' "$_cpl_status" | sed -n '2p' | cut -f4)"
        [ "$_cpl_run" = - ] || return 0
        case "$(printf '%s\n' "$_cpl_status" | sed -n '2p' | cut -f3)" in
            failed|finished)
                _cpl_failure="$(printf '%s\n' "$_cpl_status" | awk -F '\t' '$1 == "F" { sub(/^F\t/, ""); print }')"
                cli_error plan launch_failed "the plan did not start: $(printf '%s' "$_cpl_failure" | cut -f2)" \
                    "$(printf '%s' "$_cpl_failure" | cut -f3 | sed 's/^-$/approve it again once the cause is fixed/')"
                return 1 ;;
        esac
        _cpl_tries=$((_cpl_tries + 1))
        sleep 1
    done
    cli_error plan launch_unconfirmed 'the plan was approved but its run has not started yet' 'follow it with hydra plan status'
    return 1
}

cmd_plan_approve() {
    _cpa_head="" _cpa_accept="" _cpa_allow=0
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --accept) [ "$#" -ge 2 ] || { cli_error plan invalid_arguments '--accept needs a digest' 'run hydra plan --help'; return 1; }; _cpa_accept="$2"; shift ;;
            --allow-agent-environment) _cpa_allow=1 ;;
            -*) cli_error plan invalid_arguments "unknown option '$1'" 'run hydra plan --help'; return 1 ;;
            *) [ -z "$_cpa_head" ] || { cli_error plan invalid_arguments 'name at most one head' 'run hydra plan --help'; return 1; }; _cpa_head="$1" ;;
        esac
        shift
    done
    # The explicit expert override of the agent-environment guard.
    [ "$_cpa_allow" -eq 0 ] || export HYDRA_PLAN_ALLOW_AGENT=1
    workflow_plan_agent_guard plan 'approve a plan' || return 1
    if [ -z "$_cpa_accept" ] && [ ! -t 0 ]; then
        cli_error plan approval_required 'approving a plan needs a terminal to answer on, or --accept <sha256>' 'run hydra plan approve in your terminal; automation passes the digest hydra plan show --json printed'
        return 1
    fi
    _cpa_branch="$(cmd_plan_head "$_cpa_head")" || return 1
    parallel_head_load "$_cpa_branch" || return 1
    _cpa_head_id="$PARALLEL_HEAD_ID" _cpa_instance="$(sed -n '1p' "$PARALLEL_HEAD_DIR/current-instance" 2>/dev/null || true)"
    hydra_valid_id "$_cpa_instance" || { cli_error plan head_stopped "head $_cpa_branch has no running instance" "resume it with hydra resume $_cpa_branch"; return 1; }
    _cpa_tmp="$(cmd_plan_scratch "$PARALLEL_HEAD_DIR")" || return 1
    if ! cmd_plan_compile "$_cpa_branch" "$_cpa_tmp"; then rm -rf "$_cpa_tmp"; return 1; fi
    if [ -n "$_cpa_accept" ]; then
        if [ "$_cpa_accept" != "$_cpc_digest" ]; then
            rm -rf "$_cpa_tmp"
            cli_error plan digest_mismatch "the plan is not the revision that digest names: it changed since it was reviewed" 'review it again with hydra plan show --json and accept the digest it prints'
            return 1
        fi
    else
        printf 'Proposed by %s, revision %s\n\n' "$_cpa_branch" "$_cpc_revision"
        workflow_plan_tool summary "$_cpa_tmp/compiled.json"
        printf '\nRun revision %s of this plan? [y/N] ' "$_cpc_revision"
        IFS= read -r _cpa_answer || _cpa_answer=""
        case "$_cpa_answer" in
            y|Y|yes|YES|Yes) ;;
            *) rm -rf "$_cpa_tmp"; echo 'Not approved; nothing ran.'; return 1 ;;
        esac
    fi
    # The approval stands only for the revision just shown.
    if [ "$(workflow_plan_draft_digest "$_cpc_head_dir/planning/draft.json" 2>/dev/null)" != "$_cpc_draft" ] ||
        workflow_plan_returned "$_cpc_head_dir/planning"; then
        rm -rf "$_cpa_tmp"
        cli_error plan proposal_changed 'the agent published a new revision while you were reviewing; nothing ran' 'review the new revision with hydra plan show'
        return 1
    fi
    if ! cmd_plan_launch "$_cpc_digest" "$_cpa_head_id" "$_cpa_instance" "$_cpa_tmp/compiled.json" "$_cpc_root"; then
        rm -rf "$_cpa_tmp"; return 1
    fi
    rm -rf "$_cpa_tmp"
    printf 'Approved revision %s of the plan from %s. Run %s started.\n' "$_cpc_revision" "$_cpa_branch" "$_cpl_run"
    printf 'Follow it with: hydra plan status %s\nWhen it finishes, review the result with: hydra plan review %s\n' "$_cpl_run" "$_cpl_run"
}

# Usage: cmd_plan_run_dir [<run-id>]
# Prints the run directory of the named run, or of the most recent plan run.
cmd_plan_run_dir() {
    _cprd_runs="$(workflow_runs_dir)" || return 1
    if [ -n "${1:-}" ]; then
        hydra_valid_id "$1" && [ -f "$_cprd_runs/$1/compiled.json" ] || {
            cli_error plan run_not_found "no plan run $1" 'run hydra plan status for the most recent plan run'; return 1;
        }
        printf '%s\n' "$_cprd_runs/$1"; return 0
    fi
    # Creation times are UTC timestamps, so they sort as text.
    _cprd_latest="$(for _cprd_dir in "$_cprd_runs"/run_*; do
        [ -f "$_cprd_dir/compiled.json" ] || continue
        printf '%s\t%s\n' "$(sed -n '1p' "$_cprd_dir/created-at" 2>/dev/null)" "$_cprd_dir"
    done | LC_ALL=C sort | tail -n 1 | cut -f2-)"
    [ -n "$_cprd_latest" ] || { cli_error plan no_run 'no plan has run in this project yet' 'approve a proposed plan with hydra plan approve'; return 1; }
    printf '%s\n' "$_cprd_latest"
}

cmd_plan_status() {
    [ "$#" -le 1 ] || { cli_error plan invalid_arguments 'status takes at most one run ID' 'run hydra plan --help'; return 1; }
    _cpst_dir="$(cmd_plan_run_dir "${1:-}")" || return 1
    _cpst_run="$(basename "$_cpst_dir")"
    cmd_workflow status "$_cpst_run" || return 1
    case "$(sed -n '1p' "$_cpst_dir/state")" in
        succeeded) printf '\nResult ready: hydra plan review %s\n' "$_cpst_run" ;;
        failed|cancelled|recovery-required) printf '\nThe run did not succeed. hydra plan review %s shows each step and its log.\n' "$_cpst_run" ;;
        *) printf '\nStill running. Run hydra plan status %s again to follow it.\n' "$_cpst_run" ;;
    esac
}

cmd_plan_review() {
    _cpr_run="" _cpr_json=""
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --json) _cpr_json=--json ;;
            -*) cli_error plan invalid_arguments "unknown option '$1'" 'run hydra plan --help'; return 1 ;;
            *) [ -z "$_cpr_run" ] || { cli_error plan invalid_arguments 'name at most one run' 'run hydra plan --help'; return 1; }; _cpr_run="$1" ;;
        esac
        shift
    done
    _cpr_dir="$(cmd_plan_run_dir "$_cpr_run")" || return 1
    _cpr_project="$(state_v2_project_dir "$(sed -n '1p' "$_cpr_dir/project-id")")" || return 1
    workflow_plan_tool review-text "$_cpr_dir" "$_cpr_project" $_cpr_json
}

cmd_plan_policy() {
    _load_lib workflow_plan_proposal
    _cpp_file="$(workflow_plan_project_policy)" || { cli_error plan not_initialized 'Hydra project identity is unavailable' 'run hydra init'; return 1; }
    case "${1:-}" in
        '')
            [ "$#" -le 1 ] || return 1
            printf 'Plan policy for this project (%s):\n' "$_cpp_file"
            workflow_plan_tool policy-text "$_cpp_file" || return 1
            printf 'Every plan is checked against it when it is proposed and when you approve it.\n'
            printf 'Change it with hydra plan policy --allow-tool <tool> | --remove-tool <tool> | --reset.\n'
            ;;
        --json) [ "$#" -eq 1 ] || return 1; cat "$_cpp_file" ;;
        --allow-tool|--remove-tool)
            [ "$#" -eq 2 ] || { cli_error plan invalid_arguments "$1 needs one tool name" 'run hydra plan --help'; return 1; }
            workflow_plan_agent_guard plan 'change the plan policy' || return 1
            if [ "$1" = --allow-tool ]; then _cpp_action=allow; else _cpp_action=remove; fi
            _cpp_new="$(workflow_plan_tool policy-tool "$_cpp_file" "$_cpp_action" "$2")" || return 1
            workflow_atomic_scalar "$_cpp_file" "$_cpp_new" || return 1
            workflow_plan_tool policy-text "$_cpp_file"
            ;;
        --reset)
            [ "$#" -eq 1 ] || return 1
            workflow_plan_agent_guard plan 'change the plan policy' || return 1
            workflow_atomic_scalar "$_cpp_file" "$(workflow_plan_local_policy "")" || return 1
            workflow_plan_tool policy-text "$_cpp_file"
            ;;
        *) cli_error plan invalid_arguments "unknown option '$1'" 'run hydra plan --help'; return 1 ;;
    esac
}

cmd_plan() {
    _cp_action="${1:-}"
    case "$_cp_action" in
        show|approve|status|review|policy) _load_lib workflow_plan_proposal; shift ;;
    esac
    case "$_cp_action" in
        schema)
            [ "$#" -eq 1 ] || { cli_error plan invalid_arguments 'schema takes no arguments' 'run hydra plan schema'; return 1; }
            cmd_plan_schema
            ;;
        propose) cmd_workflow_plan "$@" ;;
        show) cmd_plan_show "$@" ;;
        approve) cmd_plan_approve "$@" ;;
        status) cmd_plan_status "$@" ;;
        review) cmd_plan_review "$@" ;;
        policy) cmd_plan_policy "$@" ;;
        ''|-h|--help)
            cmd_plan_usage
            [ -n "$_cp_action" ]
            ;;
        *)
            cli_error plan invalid_arguments "unknown plan command '$_cp_action'" 'run hydra plan --help'
            return 1
            ;;
    esac
}
