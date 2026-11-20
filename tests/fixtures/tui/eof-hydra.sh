#!/bin/sh
# A CLI whose answer to the head snapshot's question (the leftover worktrees)
# closes its output but does not exit; every other command is simply
# unavailable.
case "${1:-}:${2:-}" in tui:--orphan-rows) ;; *) exit 1 ;; esac
exec 1>&-
sleep 30 &
child=$!
if [ -n "${HYDRA_TEST_PID_FILE:-}" ]; then
    printf '%s\n%s\n' "$$" "$child" >> "$HYDRA_TEST_PID_FILE"
fi
wait "$child"
