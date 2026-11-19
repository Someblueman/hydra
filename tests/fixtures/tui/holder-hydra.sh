#!/bin/sh
# A local data adapter one of whose descendants leaves the adapter's session
# and keeps its output open, as a daemon started by a command would
# (HYDRA_TEST_HOLDER is the PTY test program, which records that descendant's
# PID in HYDRA_TEST_HOLDER_PIDS and ends it after a bounded time). The adapter
# itself then waits like a hung one; every other adapter is simply unavailable.
case "${1:-}:${2:-}" in tui:--data) ;; *) exit 1 ;; esac
"$HYDRA_TEST_HOLDER" --pipe-holder "$HYDRA_TEST_HOLDER_PIDS" || exit 1
sleep 30 &
child=$!
wait "$child"
