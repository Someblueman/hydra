#!/bin/sh
# A CLI that does not answer the part of the head snapshot it is asked for (the
# leftover worktrees, asked while the store could hold one); every other
# command is simply unavailable.
case "${1:-}:${2:-}" in tui:--orphan-rows) ;; *) exit 1 ;; esac

sleep 30 &
child=$!
if [ -n "${HYDRA_TEST_PID_FILE:-}" ]; then
    printf '%s\n%s\n' "$$" "$child" >> "$HYDRA_TEST_PID_FILE"
fi
wait "$child"
