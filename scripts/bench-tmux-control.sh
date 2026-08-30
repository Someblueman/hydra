#!/bin/sh
# Bounded tmux control-mode prototype versus one-process-per-poll capture-pane.

set -eu

iterations="${HYDRA_TMUX_BENCH_ITERATIONS:-30}"
case "$iterations" in ''|*[!0-9]*|0) echo "iterations must be a positive integer" >&2; exit 2 ;; esac
command -v tmux >/dev/null 2>&1 || { echo "tmux is required" >&2; exit 1; }

test_root="$(mktemp -d)"
socket="hydra-control-$$"
session=hydra-control-bench

cleanup() {
    tmux -L "$socket" kill-server >/dev/null 2>&1 || true
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

now_ms() {
    if command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf "%.0f\n", time * 1000'
    else
        printf '%s000\n' "$(date +%s)"
    fi
}

tmux -L "$socket" new-session -d -s "$session" "printf 'hydra-control-marker\\n'; sleep 120"

direct_start="$(now_ms)"
index=0
while [ "$index" -lt "$iterations" ]; do
    tmux -L "$socket" capture-pane -pt "$session" >> "$test_root/direct.out"
    index=$((index + 1))
done
direct_end="$(now_ms)"

index=0
while [ "$index" -lt "$iterations" ]; do
    printf 'capture-pane -pt %s\n' "$session"
    index=$((index + 1))
done > "$test_root/control.in"
control_start="$(now_ms)"
tmux -L "$socket" -C attach -t "$session" < "$test_root/control.in" > "$test_root/control.out"
control_end="$(now_ms)"

direct_markers="$(grep -c 'hydra-control-marker' "$test_root/direct.out" || true)"
control_markers="$(grep -c 'hydra-control-marker' "$test_root/control.out" || true)"
if [ "$direct_markers" -eq "$iterations" ] && [ "$control_markers" -eq "$iterations" ]; then reliable=true; else reliable=false; fi
printf '{"schema_version":1,"benchmark":"tmux-control-prototype","os":"%s","arch":"%s","tmux":"%s","iterations":%s,"polling_ms":%s,"control_ms":%s,"polling_markers":%s,"control_markers":%s,"reliable":%s}\n' \
    "$(uname -s)" "$(uname -m)" "$(tmux -V | sed 's/^tmux //')" "$iterations" \
    "$((direct_end - direct_start))" "$((control_end - control_start))" \
    "$direct_markers" "$control_markers" "$reliable"
[ "$reliable" = true ]
