#!/bin/sh
# Inputs come from the native precompiler's closed, bounded manifest. Checkers
# independently validate every resulting value, member and skipped record.
set -eu
mode="$1"
shift
case "$mode" in
    worker)
        printf '{"id":"%s","value":%s,"square":%s}\n' "$1" "$2" "$(($2 * $2))" \
            > "$HYDRA_WORKFLOW_OUTPUTS_DIR/result-$1.json"
        ;;
    manifest|staged)
        {
            printf '{"schema_version":1,'
            if [ "$mode" = staged ]; then
                printf '"selected_ids":['
                # Retain the argument list for the member join below.
                (
                    separator=''
                    while [ "$#" -gt 0 ]; do
                        printf '%s"%s"' "$separator" "$2"
                        separator=,
                        shift 3
                    done
                )
                printf '],'
            fi
            printf '"members":['
            separator=''
            while [ "$#" -gt 0 ]; do
                printf '%s' "$separator"
                if [ "$1" = enabled ]; then
                    cat "$HYDRA_WORKFLOW_INPUTS_DIR/member-$2"
                else
                    printf '{"id":"%s","value":%s,"status":"skipped"}' "$2" "$3"
                fi
                separator=,
                shift 3
            done
            printf ']}\n'
        } > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.json"
        ;;
    *) exit 2 ;;
esac
