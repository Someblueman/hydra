#!/bin/sh
# The native helper owns bounded structured-data inspection; shell only routes it.
workflow_attention() (
    [ "$#" -eq 2 ] && [ "$2" = --json ] || {
        json_error "workflow attention" invalid_arguments "attention requires --json" "run hydra workflow --help"
        exit 1
    }
    _wa_project="$(hydra_get_project_id 2>/dev/null || true)"
    [ -n "$_wa_project" ] || {
        json_error "workflow attention" not_initialized "Hydra project identity is unavailable" "run hydra init"
        exit 1
    }
    workflow_data_tool attention "$_wa_project"
)
workflow_attention_data() (
    [ "$#" -eq 1 ] || exit 2
    _wa_project="$(hydra_get_project_id 2>/dev/null || true)"
    [ -n "$_wa_project" ] || exit 1
    workflow_data_tool attention-data "$_wa_project"
)
