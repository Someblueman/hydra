#!/bin/sh
# Record shell baseline timings. Not a CI gate; prints measurements only.
# Usage: sh scripts/bench.sh [heads...]
# Default head counts: 5 20

set -eu

HYDRA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$HYDRA_ROOT/bin/hydra"
COUNTS="${*:-5 20}"

if ! command -v tmux >/dev/null 2>&1; then
    echo "bench: tmux is required" >&2
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    echo "bench: git is required" >&2
    exit 1
fi

_time_cmd() {
    _label="$1"
    shift
    _start="$(date +%s%N 2>/dev/null || date +%s)"
    "$@" >/dev/null 2>&1 || true
    _end="$(date +%s%N 2>/dev/null || date +%s)"
    if [ ${#_start} -gt 10 ]; then
        _ms=$(( (_end - _start) / 1000000 ))
        printf '  %s: %sms\n' "$_label" "$_ms"
    else
        printf '  %s: %ss\n' "$_label" "$((_end - _start))"
    fi
}

echo "Hydra shell baseline (measurements only; no speedup claims)"
echo "host=$(uname -s) arch=$(uname -m) tmux=$(tmux -V 2>/dev/null || echo unknown)"

for n in $COUNTS; do
    work="$(mktemp -d)"
    home="$work/hydra-home"
    repo="$work/repo"
    mkdir -p "$home" "$repo"
    git -C "$repo" init -q
    git -C "$repo" config user.email "bench@example.com"
    git -C "$repo" config user.name "bench"
    echo "ok" > "$repo/README"
    git -C "$repo" add README
    git -C "$repo" commit -q -m init

    # Spawn and time from the throwaway repo so branches/worktrees stay there.
    export HYDRA_HOME="$home"
    export HYDRA_ROOT
    export HYDRA_SKIP_AI=1
    export HYDRA_NONINTERACTIVE=1

    i=1
    while [ "$i" -le "$n" ]; do
        (cd "$repo" && "$HYDRA_BIN" spawn "bench-$i") >/dev/null 2>&1 || true
        i=$((i + 1))
    done

    echo ""
    echo "heads=$n"
    _time_cmd "list" sh -c "cd \"$repo\" && \"$HYDRA_BIN\" list"
    _time_cmd "status" sh -c "cd \"$repo\" && \"$HYDRA_BIN\" status"
    _time_cmd "doctor" sh -c "cd \"$repo\" && \"$HYDRA_BIN\" doctor"

    # Control-centre refresh: the shell snapshot the native TUI reads
    _time_cmd "tui --data" sh -c "cd \"$repo\" && \"$HYDRA_BIN\" tui --data"

    (cd "$repo" && "$HYDRA_BIN" kill --all --force) >/dev/null 2>&1 || true
    # Kill only sessions recorded for this isolated HYDRA_HOME, never a
    # global bench-* prefix sweep of the shared tmux server.
    if [ -f "$home/map" ]; then
        awk '{ print $2 }' "$home/map" | while IFS= read -r _sess; do
            if [ -n "$_sess" ]; then
                tmux kill-session -t "$_sess" 2>/dev/null || true
            fi
        done
    fi
    unset HYDRA_HOME HYDRA_SKIP_AI HYDRA_NONINTERACTIVE
    rm -rf "$work"
done
