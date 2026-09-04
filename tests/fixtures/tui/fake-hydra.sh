#!/bin/sh

case "${1:-}:${2:-}" in
    tui:--data)
        if [ -n "${HYDRA_TUI_FIXTURE:-}" ]; then
            cat "$HYDRA_TUI_FIXTURE"
        else
            fixture_dir="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
            cat "$fixture_dir/native-v2.tsv"
        fi
        ;;
    dashboard:)
        printf '%s\n' "FAKE DASHBOARD"
        ;;
    spawn:*)
        printf 'FAKE SPAWN %s\n' "$*"
        ;;
    group:create)
        printf 'FAKE GROUP %s\n' "$*"
        ;;
    kill:*)
        printf 'FAKE KILL %s\n' "$*"
        ;;
    *) exit 1 ;;
esac
