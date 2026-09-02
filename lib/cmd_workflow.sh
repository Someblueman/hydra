#!/bin/sh

cmd_workflow() {
    _cw_action="${1:-}"
    case "$_cw_action" in
        -h|--help|'')
            printf '%s\n' \
                'Usage: hydra workflow list' \
                '       hydra workflow show <id|path>' \
                '       hydra workflow validate <id|path>' \
                '       hydra workflow dry-run <id|path>'
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
        *)
            cli_error workflow invalid_arguments "unknown workflow command '$_cw_action'" "run hydra workflow --help"
            return 1
            ;;
    esac
}
