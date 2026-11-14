#!/bin/sh
# Optional planning projections feed the existing workflow runtime.
# Initialization is called inside cmd_workflow_plan's subshell, where these
# private variables are intentionally scoped for the nested cmd_workflow call.
# shellcheck disable=SC2030,SC2031

workflow_plan_tool() (
    cmd_fleet_dispatch workflow-plan "$@"
)

# Usage: workflow_plan_agent_guard <command> <action>
# Refuses to approve or run a plan from inside an agent's head, where Hydra
# sets HYDRA_HEAD_ID and HYDRA_INSTANCE_ID, unless HYDRA_PLAN_ALLOW_AGENT=1.
# This guards against an obedient agent approving its own plan; it is not a
# security boundary, since the agent runs as the same user with the same files.
workflow_plan_agent_guard() {
    [ -n "${HYDRA_HEAD_ID:-}${HYDRA_INSTANCE_ID:-}" ] || return 0
    [ "${HYDRA_PLAN_ALLOW_AGENT:-}" != 1 ] || return 0
    cli_error "$1" agent_environment "refusing to $2 from inside an agent's head (HYDRA_HEAD_ID or HYDRA_INSTANCE_ID is set): the user approves plans" \
        "approve it from your own terminal with hydra plan approve, or in the control centre; experts can set HYDRA_PLAN_ALLOW_AGENT=1"
    return 1
}

cmd_workflow_plan() (
    _cwp_action="${1:-}"
    case "$_cwp_action" in
        propose|proposal)
            _load_lib workflow_plan_proposal
            workflow_plan_proposal "$@"
            ;;
        --workspace-owner|--workspace-status)
            _load_lib workflow_plan_launch
            shift
            if [ "$_cwp_action" = --workspace-owner ]; then workflow_plan_launch_owner "$@"
            else workflow_plan_launch_status "$@"; fi
            ;;
        schema)
            [ "$#" -eq 1 ] || exit 1
            workflow_plan_tool schema
            ;;
        validate|compile)
            if [ "$_cwp_action" = validate ]; then _cwp_count=3; else _cwp_count=4; fi
            _cwp_usage='use validate <plan.json> <policy.json> or compile <plan.json> <policy.json> <new-output.json>, each with an optional --assets-dir <dir>'
            [ "$#" -ge "$_cwp_count" ] || { cli_error 'workflow plan' invalid_arguments "$_cwp_usage" 'run hydra workflow plan --help'; exit 1; }
            _cwp_plan="$2" _cwp_policy="$3" _cwp_output="${4:-}"
            shift "$_cwp_count"
            # Assets a plan names by data.inputs.<name>.asset come from this
            # directory, never from the source checkout.
            if [ "$#" -eq 2 ] && [ "$1" = --assets-dir ]; then set -- --assets-dir "$2"
            elif [ "$#" -ne 0 ]; then cli_error 'workflow plan' invalid_arguments "$_cwp_usage" 'run hydra workflow plan --help'; exit 1; fi
            _cwp_root="$(workflow_repo_root)" || {
                cli_error 'workflow plan' invalid_source "source $(pwd) is not a Git repository" "run validation from the project's main checkout"
                exit 1
            }
            if [ "$_cwp_action" = validate ]; then workflow_plan_tool validate "$_cwp_plan" "$_cwp_policy" "$_cwp_root" "$@"
            else workflow_plan_tool compile "$_cwp_plan" "$_cwp_policy" "$_cwp_root" "$_cwp_output" "$@"; fi
            ;;
        show)
            if [ "$#" -eq 2 ]; then workflow_plan_tool preview "$2"
            elif [ "$#" -eq 3 ] && [ "$3" = --json ]; then workflow_plan_tool show "$2"
            else exit 1; fi
            ;;
        obligations)
            if [ "$#" -eq 2 ]; then workflow_plan_tool obligations "$2"
            elif [ "$#" -eq 3 ] && [ "$3" = --json ]; then workflow_plan_tool obligations "$2" --json
            else exit 1; fi
            ;;
        explain|compare|evaluate)
            workflow_plan_tool "$@"
            ;;
        observed)
            if [ "$#" -ne 2 ] || ! hydra_valid_id "$2"; then cli_error 'workflow plan' invalid_arguments 'observed takes one run ID' 'run hydra workflow plan --help'; exit 1; fi
            workflow_plan_tool observed "$(workflow_runs_dir)/$2"
            ;;
        decision)
            _load_lib workflow_plan_decision
            shift
            workflow_plan_decision "$@"
            ;;
        expand)
            _load_lib workflow_plan_expansion
            shift
            workflow_plan_expansion "$@"
            ;;
        tui-data)
            [ "$#" -eq 2 ] || exit 1
            workflow_plan_tool tui-data "$2"
            ;;
        check-definition)
            [ "$#" -eq 3 ] || exit 1
            workflow_plan_tool check-definition "$2" "$3"
            ;;
        check-recipe)
            [ "$#" -eq 3 ] || exit 1
            workflow_plan_tool check-recipe "$2" "$3"
            ;;
        result)
            [ "$#" -eq 2 ] && hydra_valid_id "$2" || exit 1
            workflow_plan_tool result "$(workflow_runs_dir)/$2"
            ;;
        run)
            if [ "$#" -ne 4 ] || [ "$3" != --accept ]; then
                cli_error 'workflow plan' acceptance_required 'run requires <compiled.json> --accept <sha256>' 'review workflow plan show and accept its exact digest'
                exit 1
            fi
            # A launch owner was already checked when it started.
            [ -n "${_workflow_plan_launch:-}" ] || workflow_plan_agent_guard 'workflow plan' 'run a plan' || exit 1
            _cwp_root="$(workflow_repo_root)" || exit 1
            _workflow_plan_stage="$(mktemp -d)" || exit 1
            trap 'rm -rf "$_workflow_plan_stage"' EXIT
            trap 'exit 130' INT
            trap 'exit 143' TERM
            trap 'exit 129' HUP
            # An admitted expansion round executes once, with its admitted digest.
            _load_lib workflow_plan_expansion
            workflow_plan_expansion_bind "$2" "$4" "$_workflow_plan_stage" || exit 1
            workflow_plan_tool admit "$2" "$_cwp_root" "$4" "$_workflow_plan_stage" > "$_workflow_plan_stage/admission.json" || {
                cat "$_workflow_plan_stage/admission.json" >&2
                exit 1
            }
            _cwp_project="$(hydra_get_project_id)" || exit 1
            workflow_plan_tool heads "$_workflow_plan_stage/compiled.json" > "$_workflow_plan_stage/heads" || exit 1
            while IFS= read -r _cwp_head; do
                if state_v2_find_head_by_branch "$_cwp_project" "$_cwp_head" >/dev/null 2>&1; then
                    cli_error 'workflow plan' head_exists "durable head state exists for $_cwp_head" 'use fresh head names in a newly compiled and accepted plan'
                    exit 1
                fi
            done < "$_workflow_plan_stage/heads"
            _workflow_plan_accepted="$4"
            cmd_workflow run "$_workflow_plan_stage/workflow.yml"
            ;;
        ''|-h|--help)
            printf '%s\n' \
                'Usage: hydra workflow plan schema' \
                '       hydra workflow plan propose <draft.json> [--branch <head>] [--asset NAME=FILE]...' \
                '       hydra workflow plan proposal <head> [--local-policy | --return <feedback>]' \
                '       hydra workflow plan validate <plan.json> <policy.json> [--assets-dir <dir>]' \
                '       hydra workflow plan compile <plan.json> <policy.json> <new-output.json> [--assets-dir <dir>]' \
                '       hydra workflow plan show <compiled.json> [--json]' \
                '       hydra workflow plan obligations <compiled.json> [--json]' \
                '       hydra workflow plan explain <compiled.json> [--estimates <estimates.json>]' \
                '       hydra workflow plan compare <left.json> <right.json> [--estimates <estimates.json>]' \
                '       hydra workflow plan evaluate <left.json> <right.json> [--estimates <estimates.json>]' \
                '       hydra workflow plan observed <run-id>' \
                '       hydra workflow plan decision record <head> --select <compiled.json> <reason> [--reject <compiled.json> <reason>]... [--estimates <estimates.json>] [--json]' \
                '       hydra workflow plan decision list <head> [--json]' \
                '       hydra workflow plan decision show <head> <decision-id> [--json]' \
                '       hydra workflow plan expand <head> --run <run-id> [--proposal <expansion.json>] [--json]' \
                '       hydra workflow plan run <compiled.json> --accept <sha256>' \
                '       hydra workflow plan result <run-id>' \
                '       hydra workflow plan check-definition <compiled.json> <check-id>' \
                '       hydra workflow plan check-recipe <compiled.json> <check-id>' \
                'Compile from the source repository. Keep compiled output outside it.' \
                'Plans carry inline agent prompts and --asset files in the compiled artifact;' \
                'nothing needs to be committed to the source to plan.' \
                'Execution uses the existing workflow status, cancel and resume commands.' \
                'evaluate separates decomposition, fixed-graph scheduling and placement; observed turns' \
                'a run'"'"'s recorded step durations into an estimates file; decisions replay their comparison.' \
                'expand admits a bounded hydra-plan-expansion-1 proposal over a finished run as a new version;' \
                'each round runs through plan run with its own exact digest and never enlarges the policy.'
            ;;
        *) cli_error 'workflow plan' invalid_arguments 'unknown planning command' 'run hydra workflow plan --help'; exit 1 ;;
    esac
)

workflow_plan_initialize() {
    [ -n "${_workflow_plan_stage:-}" ] || return 0
    cp "$_workflow_plan_stage/compiled.json" "$1/compiled.json" || return 1
    workflow_atomic_scalar "$1/plan-accepted" "$_workflow_plan_accepted" || return 1
    # The launch owner's planning association travels with the run it creates.
    for _wpi_name in planning-head planning-instance planning-branch; do
        [ ! -f "${_workflow_plan_launch:-}/$_wpi_name" ] || cp "$_workflow_plan_launch/$_wpi_name" "$1/$_wpi_name" || return 1
    done
    # Step roles (work, compose, verify) and heads, for display and for
    # retiring verifier heads when the run finishes.
    hydra_private_touch "$1/plan-roles.tsv" || return 1
    workflow_plan_tool roles "$1/compiled.json" > "$1/plan-roles.tsv" || return 1
    # Exec steps on the plan's own heads: agent steps are followed by a commit,
    # commands record the commit they ran on (see workflow_plan_step_hook).
    hydra_private_touch "$1/plan-hooks.tsv" || return 1
    workflow_plan_tool hooks "$1/compiled.json" > "$1/plan-hooks.tsv" || return 1
    _wpi_timeout="$(workflow_plan_tool timeout "$1/compiled.json")" || return 1
    workflow_atomic_scalar "$1/plan-deadline" "$(($(date +%s) + _wpi_timeout))" || return 1
    if [ -n "${_workflow_plan_expansion:-}" ]; then
        _load_lib workflow_plan_expansion
        workflow_plan_expansion_link "$1" || return 1
    fi
    # Admission compiled the plan afresh against the source moments ago; the
    # run's definition must be exactly that plan's projection. Its digests are
    # then recorded for the guard before every step.
    workflow_plan_projection_match "$1" && workflow_plan_tool guard "$1" --record >/dev/null
}

# Usage: workflow_plan_bindings_match <run-dir>
# The check before each step, at the finish and on resume: the plan as it was
# approved and the inputs the run copied, never the live checkout (see
# plan_run_guard). An untracked or changed file in the source does not stop a
# running plan; a moved source commit is only recorded in plan-warnings.
workflow_plan_bindings_match() {
    [ -f "$1/compiled.json" ] || return 0
    workflow_plan_tool guard "$1" >/dev/null
}

workflow_plan_projection_match() (
    _wpb_dir="$1"
    workflow_plan_tool data-match "$_wpb_dir/compiled.json" "$_wpb_dir" >/dev/null || exit 1
    _wpb_tmp="$(mktemp -d)" || exit 1
    trap 'rm -rf "$_wpb_tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    workflow_plan_tool projection "$_wpb_dir/compiled.json" > "$_wpb_tmp/workflow.yml" || exit 1
    workflow_parse "$_wpb_tmp/workflow.yml" normalized > "$_wpb_tmp/expected.yml" || exit 1
    workflow_parse "$_wpb_tmp/workflow.yml" runtime > "$_wpb_tmp/expected.tsv" || exit 1
    cmp -s "$_wpb_tmp/expected.yml" "$_wpb_dir/resolved.yml" &&
        cmp -s "$_wpb_tmp/expected.tsv" "$_wpb_dir/graph.tsv" || exit 1
    IFS="$(printf '\t')" read -r _wpb_header _wpb_id _wpb_parallel _wpb_disk _wpb_heads < "$_wpb_tmp/expected.tsv"
    [ "$(sed -n '1p' "$_wpb_dir/parallelism")" = "$_wpb_parallel" ] &&
        [ "$(sed -n '1p' "$_wpb_dir/disk-mb")" = "$_wpb_disk" ] &&
        [ "$(sed -n '1p' "$_wpb_dir/max-heads")" = "$_wpb_heads" ]
)

# Usage: workflow_plan_step_hook <run-dir> <step>
# Prints "<kind><TAB><head>[<TAB><check>]" for an exec step on one of the
# plan's own heads (kind agent, command or run), or nothing.
workflow_plan_step_hook() {
    [ -f "$1/plan-hooks.tsv" ] || return 0
    awk -F '\t' -v id="$2" '$1 == id { sub(/^[^\t]*\t/, ""); print; exit }' "$1/plan-hooks.tsv"
}

# Usage: workflow_plan_worktree <run-dir> <branch>
# The worktree of the head the run's project records for branch.
workflow_plan_worktree() {
    _wpw_project="$(sed -n '1p' "$1/project-id")"
    _wpw_head="$(state_v2_find_head_by_branch "$_wpw_project" "$2")" || return 1
    _wpw_dir="$(state_v2_head_dir "$_wpw_project" "$_wpw_head")" || return 1
    _wpw_tree="$(sed -n '1p' "$_wpw_dir/worktree" 2>/dev/null || true)"
    [ -n "$_wpw_tree" ] && [ -d "$_wpw_tree" ] || return 1
    printf '%s\n' "$_wpw_tree"
}

# Usage: workflow_plan_record_commit <worktree> <file>
workflow_plan_record_commit() {
    _wprc_commit="$(git -C "$1" rev-parse --verify HEAD 2>/dev/null)" || return 1
    workflow_atomic_scalar "$2" "$_wprc_commit"
}

# Usage: workflow_plan_secret_path <path>
# Names of new files that look like secrets (environment files, private keys,
# key stores, credential files). Narrow on purpose: a match is left out of
# Hydra's commit, so a broad pattern would drop real work.
workflow_plan_secret_path() {
    _wpsp_base="$(printf '%s' "${1##*/}" | tr '[:upper:]' '[:lower:]')"
    case "$_wpsp_base" in
        *.example|*.sample|*.template|*.dist) return 1 ;;
        .env|.env.*|*.pem|*.key|*.p12|*.pfx|*.jks|*.keystore|id_rsa*|id_dsa*|id_ecdsa*|id_ed25519*|\
        credentials|credentials.*|.netrc|.npmrc|.pypirc) return 0 ;;
        *) return 1 ;;
    esac
}

# Usage: workflow_plan_withhold <worktree> <attempt-dir>
# Unstages new files that look like secrets before Hydra commits an agent
# step, and lists them in <attempt-dir>/withheld. They stay in the worktree
# as untracked files, so `hydra land` keeps the worker head instead of
# removing them, and the review lists them as uncommitted work.
workflow_plan_withhold() {
    _wph_list="$(git -C "$1" -c core.quotepath=false diff --cached --name-only --diff-filter=A)" || return 1
    _wph_held=''
    while IFS= read -r _wph_path; do
        [ -n "$_wph_path" ] || continue
        workflow_plan_secret_path "$_wph_path" || continue
        git -C "$1" rm --cached -q -- "$_wph_path" >/dev/null || return 1
        _wph_held="$_wph_held$_wph_path
"
    done <<EOF
$_wph_list
EOF
    [ -n "$_wph_held" ] || return 0
    hydra_private_touch "$2/withheld" && printf '%s' "$_wph_held" > "$2/withheld"
}

# Usage: workflow_plan_commit <run-dir> <step> <worktree> <attempt-dir>
# After an agent step, commits whatever it left in its worker head (tracked,
# new and deleted files, as .gitignore allows, except new files that look like
# secrets: see workflow_plan_withhold), so the checks, the review and
# landing all refer to one commit. Commit hooks and signing are not run: the
# plan's checks verify the work. A step that changed nothing records the head
# commit as it is. The commit is also offered to the step as its "commit"
# output, unless the step's command wrote that file itself.
workflow_plan_commit() (
    _wpc_dir="$1" _wpc_step="$2" _wpc_tree="$3" _wpc_attempt="$4"
    unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR
    git -C "$_wpc_tree" add -A >/dev/null || exit 1
    workflow_plan_withhold "$_wpc_tree" "$_wpc_attempt" || exit 1
    if ! git -C "$_wpc_tree" diff --cached --quiet; then
        _wpc_plan="$(sed -n '1p' "$_wpc_dir/graph.tsv" | cut -f2)"
        _wpc_run="$(sed -n '1p' "$_wpc_dir/run-id")"
        set -- -c commit.gpgsign=false
        [ -n "$(git -C "$_wpc_tree" config user.name 2>/dev/null)" ] || set -- "$@" -c user.name=Hydra
        [ -n "$(git -C "$_wpc_tree" config user.email 2>/dev/null)" ] || set -- "$@" -c user.email=hydra@localhost
        git -C "$_wpc_tree" "$@" commit --no-verify -q \
            -m "Hydra plan $_wpc_plan: $_wpc_step" \
            -m "Hydra committed what the agent step $_wpc_step of run $_wpc_run left in this worktree, so the plan's checks, its review and landing refer to this commit." \
            >/dev/null || exit 1
    fi
    workflow_plan_record_commit "$_wpc_tree" "$_wpc_attempt/commit" || exit 1
    if [ -n "${HYDRA_WORKFLOW_OUTPUTS_DIR:-}" ] && [ -d "$HYDRA_WORKFLOW_OUTPUTS_DIR" ] &&
        [ ! -e "$HYDRA_WORKFLOW_OUTPUTS_DIR/commit" ] && [ ! -L "$HYDRA_WORKFLOW_OUTPUTS_DIR/commit" ]; then
        cp "$_wpc_attempt/commit" "$HYDRA_WORKFLOW_OUTPUTS_DIR/commit" || exit 1
    fi
)

workflow_plan_expired() {
    [ -f "$1/compiled.json" ] || return 1
    _wpe_deadline="$(sed -n '1p' "$1/plan-deadline")"
    case "$_wpe_deadline" in ''|*[!0-9]*) return 0 ;; esac
    [ "$(date +%s)" -ge "$_wpe_deadline" ]
}

workflow_plan_finish() {
    [ -f "$1/compiled.json" ] || return 0
    workflow_plan_bindings_match "$1" && hydra_private_touch "$1/plan-verification.json" &&
        workflow_plan_tool finish "$1" > "$1/plan-verification.json" || return 1
    # Historical timing of the independent gate, bound to the accepted revision.
    # Optional telemetry must not change delivery. Invalidate old timing first,
    # and publish the binding last so a partial write remains unknown.
    rm -f "$1/verification-plan-sha256" "$1/verified-at" || return 0
    if ! { workflow_atomic_scalar "$1/verified-at" "$(date +%s)" &&
        workflow_atomic_scalar "$1/verification-plan-sha256" "$(sed -n '1p' "$1/plan-accepted")"; }; then
        rm -f "$1/verification-plan-sha256" "$1/verified-at" || true
    fi
    return 0
}

workflow_plan_repair() {
    if [ ! -f "$1/compiled.json" ]; then printf '0\n'; return 0; fi
    workflow_plan_tool "${2:-repair}" "$1" || {
        workflow_atomic_scalar "$1/state" recovery-required
        rm -rf "$1/.drive.lock"
        return 1
    }
}
