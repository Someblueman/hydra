#!/bin/sh
# The parallel runner must preserve failure and clean only its own resources.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
HYDRA_SHELL_EXEC=${HYDRA_SHELL_EXEC:-$root/build/test-shell-exec}
export HYDRA_SHELL_EXEC
for option in --jobserver-fds --jobserver-auth; do
    MAKEFLAGS="$option=8,9" "$HYDRA_SHELL_EXEC" sh -c '
        if ( : <&8 ) 2>/dev/null; then exit 1; fi
        if ( : >&9 ) 2>/dev/null; then exit 1; fi
        if ( : <&7 ) 2>/dev/null; then exit 1; fi
    ' 8</dev/null 9>/dev/null 7<&8
done
echo 'PASS shell runner: detached cases cannot inherit Make jobserver pipes'
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
cat > "$fixture/case.sh" <<'CASE'
#!/bin/sh
set -eu
printf '%s\n' "$TMPDIR" "$TMUX_TMPDIR" > "$RUNNER_MARKER"
tmux new-session -d -s runner-contract 'sleep 30'
printf ready >> "$RUNNER_MARKER"
if [ "$RUNNER_MODE" = signal ]; then sleep 30; fi
if [ "$RUNNER_MODE" = interrupt ]; then trap 'exit 130' INT; kill -INT "$$"; exit 99; fi
exit 7
CASE
export RUNNER_MARKER="$fixture/paths" RUNNER_MODE=exit
status=0
MAKEFLAGS='SHELL_TEST_FILES=foundation' sh "$root/scripts/run-shell-test.sh" "$fixture/logs" failed "$fixture/case.sh" > "$fixture/output" 2>&1 || status=$?
[ "$status" -eq 7 ]
grep -q 'exit 7' "$fixture/output"
test -f "$fixture/logs/failed.log"
test ! -e "$(sed -n '1p' "$RUNNER_MARKER")"
test ! -e "$(sed -n '2p' "$RUNNER_MARKER")"
echo 'PASS shell runner: failure status, retained log and private cleanup'
MAKEFLAGS=n sh "$root/scripts/run-test.sh" "$fixture/logs" dry-run true
test ! -e "$fixture/logs/dry-run.log"
echo 'PASS shell runner: actual Make dry runs do not create success logs'
RUNNER_MODE=interrupt
export RUNNER_MODE
status=0
sh "$root/scripts/run-shell-test.sh" "$fixture/logs" signal-default "$fixture/case.sh" > "$fixture/output" 2>&1 || status=$?
[ "$status" -eq 130 ]
echo 'PASS shell runner: asynchronous launch preserves the test SIGINT contract'
rm "$RUNNER_MARKER"
RUNNER_MODE=signal
export RUNNER_MODE
sh "$root/scripts/run-shell-test.sh" "$fixture/logs" interrupted "$fixture/case.sh" > "$fixture/output" 2>&1 &
runner=$!
tries=0
while ! grep -q ready "$RUNNER_MARKER" 2>/dev/null; do
    tries=$((tries + 1))
    if [ "$tries" -ge 100 ]; then kill -TERM "$runner"; wait "$runner" || true; exit 1; fi
    sleep 0.02
done
kill -TERM "$runner"
status=0
wait "$runner" || status=$?
[ "$status" -eq 143 ]
test ! -e "$(sed -n '1p' "$RUNNER_MARKER")"
test ! -e "$(sed -n '2p' "$RUNNER_MARKER")"
echo 'PASS shell runner: interruption fails and removes only owned resources'
