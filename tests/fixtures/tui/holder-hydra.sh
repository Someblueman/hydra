#!/bin/sh
# A CLI one of whose descendants leaves its session and keeps its output open
# while it answers the head snapshot's question (the leftover worktrees), as a
# daemon started by a command would (HYDRA_TEST_HOLDER is the PTY test
# program, which records that descendant's PID in HYDRA_TEST_HOLDER_PIDS and
# ends it after a bounded time). The command itself then waits like a hung
# one; every other command is simply unavailable.
case "${1:-}:${2:-}" in tui:--orphan-rows) ;; *) exit 1 ;; esac
"$HYDRA_TEST_HOLDER" --pipe-holder "$HYDRA_TEST_HOLDER_PIDS" || exit 1
sleep 30 &
child=$!
wait "$child"
