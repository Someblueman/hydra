#!/bin/sh

cmd_workflow() {
    _cw_action="${1:-}"
    case "$_cw_action" in
        -h|--help|'')
            printf '%s\n' \
                'Usage: hydra workflow list' \
                '       hydra workflow show <id|path>' \
                '       hydra workflow validate <id|path>' \
                '       hydra workflow dry-run <id|path>' \
                '       hydra workflow run <id|path>' \
                '       hydra workflow status <run-id> [--json]' \
                '       hydra workflow cancel <run-id>' \
                '       hydra workflow resume <run-id>'
            [ -n "$_cw_action" ]
            return
            ;;
        list)
            [ "$#" -eq 1 ] || { cli_error workflow invalid_arguments "list accepts no arguments" "run hydra workflow --help"; return 1; }
            workflow_list
            ;;
        show|validate|dry-run)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "$_cw_action requires exactly one workflow ID or path" "run hydra workflow --help"; return 1; }
            _cw_file="$(workflow_resolve "$2")" || return 1
            workflow_require_trust "$_cw_file" || return 1
            case "$_cw_action" in
                show) workflow_parse "$_cw_file" normalized ;;
                validate)
                    workflow_parse "$_cw_file" validate || return 1
                    printf 'Valid workflow: %s\n' "$_cw_file"
                    ;;
                dry-run) workflow_parse "$_cw_file" dry-run ;;
            esac
            ;;
        run)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "run requires exactly one workflow ID or path" "run hydra workflow --help"; return 1; }
            _cw_file="$(workflow_resolve "$2")" || return 1
            workflow_require_trust "$_cw_file" || return 1
            workflow_parse "$_cw_file" validate || return 1
            _cw_runs="$(workflow_runs_dir)" || return 1; mkdir -p "$_cw_runs" || return 1
            _cw_project="$(hydra_get_project_id)" _cw_base="$(git rev-parse HEAD)"
            _cw_run="$(hydra_new_id run "$_cw_project|workflow|$_cw_file")" || return 1
            _cw_tmp="$(mktemp -d "$_cw_runs/.run.XXXXXX")" || return 1
            _cw_dir="$_cw_runs/$_cw_run"
            mkdir -p "$_cw_tmp/steps" || { rm -rf "$_cw_tmp"; return 1; }
            chmod 700 "$_cw_tmp" 2>/dev/null || true
            workflow_parse "$_cw_file" normalized > "$_cw_tmp/resolved.yml" || { rm -rf "$_cw_tmp"; return 1; }
            workflow_parse "$_cw_file" runtime > "$_cw_tmp/graph.tsv" || { rm -rf "$_cw_tmp"; return 1; }
            _cw_hash="$(git hash-object "$_cw_tmp/resolved.yml")" _cw_created="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
            workflow_atomic_scalar "$_cw_tmp/run-id" "$_cw_run"
            workflow_atomic_scalar "$_cw_tmp/schema-version" 1
            workflow_atomic_scalar "$_cw_tmp/runtime-version" 1
            workflow_atomic_scalar "$_cw_tmp/project-id" "$_cw_project"
            workflow_atomic_scalar "$_cw_tmp/definition-path" "$_cw_file"
            workflow_atomic_scalar "$_cw_tmp/definition-hash" "$_cw_hash"
            workflow_atomic_scalar "$_cw_tmp/base-commit" "$_cw_base"
            workflow_atomic_scalar "$_cw_tmp/owner" "${USER:-unknown}"
            workflow_atomic_scalar "$_cw_tmp/created-at" "$_cw_created"
            IFS="$(printf '\t')" read -r _cw_header _cw_workflow _cw_parallelism _cw_disk _cw_heads < "$_cw_tmp/graph.tsv"
            workflow_atomic_scalar "$_cw_tmp/workflow-id" "$_cw_workflow"
            workflow_atomic_scalar "$_cw_tmp/parallelism" "$_cw_parallelism"
            workflow_atomic_scalar "$_cw_tmp/disk-mb" "$_cw_disk"
            workflow_atomic_scalar "$_cw_tmp/max-heads" "$_cw_heads"
            while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_rest; do
                [ "$_cw_tag" = step ] || continue
                mkdir -p "$_cw_tmp/steps/$_cw_id"
                workflow_atomic_scalar "$_cw_tmp/steps/$_cw_id/state" queued
                workflow_atomic_scalar "$_cw_tmp/steps/$_cw_id/attempts" 0
            done < "$_cw_tmp/graph.tsv"
            {
                printf 'run_id\t%s\n' "$_cw_run"
                printf 'project_id\t%s\n' "$_cw_project"
                printf 'workflow_id\t%s\n' "$_cw_workflow"
                printf 'definition_hash\t%s\n' "$_cw_hash"
                printf 'base_commit\t%s\n' "$_cw_base"
                printf 'parallelism\t%s\n' "$_cw_parallelism"
                printf 'disk_mb\t%s\n' "$_cw_disk"
                printf 'max_heads\t%s\n' "$_cw_heads"
            } > "$_cw_tmp/manifest.tsv"
            workflow_atomic_scalar "$_cw_tmp/state" queued
            : > "$_cw_tmp/events.jsonl"
            mv "$_cw_tmp" "$_cw_dir" || { rm -rf "$_cw_tmp"; return 1; }
            workflow_event "$_cw_dir" "" run.created
            printf '%s\n' "$_cw_run"
            workflow_drive "$_cw_dir"
            ;;
        status)
            if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
                cli_error "workflow status" invalid_arguments "status requires a run ID and optional --json" "run hydra workflow --help"
                return 1
            fi
            hydra_valid_id "$2" || { cli_error "workflow status" invalid_run_id "invalid workflow run ID: $2" "use a run ID reported by hydra workflow run"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || { cli_error "workflow status" run_not_found "workflow run not found: $2" "inspect the project workflow runs"; return 1; }
            _cw_state="$(sed -n '1p' "$_cw_dir/state")"; _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"
            if [ "$_cw_state" = running ] && ! workflow_run_owner_fresh "$_cw_dir"; then _cw_state=stale; fi
            if [ "${3:-}" = --json ]; then
                printf '{"schema_version":1,"ok":true,"command":"workflow status","data":{"run_id":"%s","state":"%s","source":"recorded","steps":[' "$2" "$_cw_state"; _cw_first=1
                while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_rest; do [ "$_cw_tag" = step ] || continue; [ "$_cw_first" -eq 1 ] || printf ','; _cw_first=0; printf '{"step_id":"%s","state":"%s","attempts":%s}' "$_cw_id" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/state")" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/attempts")"; done < "$_cw_dir/graph.tsv"; printf ']}}\n'
            else
                printf 'Workflow %s: %s (source: recorded)\n' "$2" "$_cw_state"
                while IFS="$(printf '\t')" read -r _cw_tag _cw_id _cw_kind _cw_rest; do [ "$_cw_tag" = step ] || continue; printf '  %s [%s]: %s (attempts=%s)\n' "$_cw_id" "$_cw_kind" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/state")" "$(sed -n '1p' "$_cw_dir/steps/$_cw_id/attempts")"; done < "$_cw_dir/graph.tsv"
            fi
            ;;
        cancel)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "cancel requires a run ID" "run hydra workflow --help"; return 1; }
            hydra_valid_id "$2" || { cli_error workflow invalid_run_id "invalid workflow run ID: $2" "use a run ID reported by hydra workflow run"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || return 1
            _cw_state="$(sed -n '1p' "$_cw_dir/state")"
            [ "$_cw_state" = running ] || { cli_error workflow not_running "workflow run is already terminal: $_cw_state" "inspect it with hydra workflow status $2"; return 1; }
            workflow_atomic_scalar "$_cw_dir/cancel-requested" "$(date +%s)"; workflow_event "$_cw_dir" "" run.cancel_requested
            _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"
            if workflow_run_owner_active "$_cw_dir"; then
                kill -TERM "$_cw_owner" 2>/dev/null || true
            else
                workflow_drive "$_cw_dir" || true
            fi
            _cw_wait=0
            while [ "$(sed -n '1p' "$_cw_dir/state")" = running ] && [ "$_cw_wait" -lt 7 ]; do sleep 1; _cw_wait=$((_cw_wait + 1)); done
            _cw_state="$(sed -n '1p' "$_cw_dir/state")"
            printf 'Workflow %s: %s\n' "$2" "$_cw_state"
            if [ -s "$_cw_dir/residual-children.tsv" ]; then
                sed 's/^/  remaining child: /' "$_cw_dir/residual-children.tsv"
                return 1
            fi
            [ "$_cw_state" = cancelled ]
            ;;
        resume)
            [ "$#" -eq 2 ] || { cli_error workflow invalid_arguments "resume requires a run ID" "run hydra workflow --help"; return 1; }
            hydra_valid_id "$2" || { cli_error workflow invalid_run_id "invalid workflow run ID: $2" "use a run ID reported by hydra workflow run"; return 1; }
            _cw_runs="$(workflow_runs_dir)" || return 1; _cw_dir="$_cw_runs/$2"; [ -d "$_cw_dir" ] || return 1
            _cw_owner="$(sed -n '1p' "$_cw_dir/owner-pid" 2>/dev/null || true)"; _cw_state="$(sed -n '1p' "$_cw_dir/state")"
            case "$_cw_state" in succeeded|failed|cancelled) return 0 ;; esac
            if [ "$_cw_state" = running ] && workflow_run_owner_active "$_cw_dir"; then cli_error workflow already_running "workflow owner is still active" "wait or cancel the run"; return 1; fi
            workflow_bindings_match "$_cw_dir" || { workflow_atomic_scalar "$_cw_dir/state" recovery-required; cli_error workflow binding_mismatch "recorded workflow bindings do not match this checkout" "restore the recorded project and base commit"; return 1; }
            workflow_event "$_cw_dir" "" run.recovered "previous_owner=$_cw_owner"; workflow_drive "$_cw_dir"
            ;;
        *)
            cli_error workflow invalid_arguments "unknown workflow command '$_cw_action'" "run hydra workflow --help"
            return 1
            ;;
    esac
}
