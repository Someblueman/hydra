#!/bin/sh
# Measure the bounded native refresh adapter and renderer at roadmap head counts.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_root="$(mktemp -d)"
bench_home="$test_root/home"
bench_repo="$test_root/repo"
tmux_socket="hydra-tui-bench-$$"
mkdir -p "$bench_home" "$bench_repo"
git -C "$bench_repo" init -q

real_tmux="$(command -v tmux)"
cleanup() {
    "$real_tmux" -L "$tmux_socket" kill-server >/dev/null 2>&1 || true
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

export HYDRA_HOME="$bench_home"
export HYDRA_TUI_BIN="$repo_root/build/hydra-tui"
export HYDRA_REAL_TMUX="$real_tmux"
export HYDRA_TEST_TMUX_SOCKET="$tmux_socket"
PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH"
export PATH

created=0

for heads in 5 20 100; do
    while [ "$created" -lt "$heads" ]; do
        created=$((created + 1))
        "$real_tmux" -L "$tmux_socket" new-session -d -s "bench-$created"
    done
    : > "$bench_home/map"
    index=1
    while [ "$index" -le "$heads" ]; do
        printf 'bench-%s bench-%s - - 1 - -\n' "$index" "$index" >> "$bench_home/map"
        index=$((index + 1))
    done
    fixture="$test_root/$heads.tsv"
    adapter_start="$(now_ms)"
    (cd "$bench_repo" && "$repo_root/bin/hydra" tui --data) > "$fixture"
    adapter_end="$(now_ms)"
    render_start="$(now_ms)"
    "$repo_root/build/hydra-tui" --headless-fixture "$fixture" --size 120x40 --frames 10 > /dev/null
    render_end="$(now_ms)"
    cpu_ms=null
    if [ -x /usr/bin/time ]; then
        /usr/bin/time -p "$repo_root/build/hydra-tui" --headless-fixture "$fixture" \
            --size 120x40 --frames 10 > /dev/null 2> "$test_root/time-$heads"
        cpu_ms="$(awk '/^(user|sys) / { total += $2 } END { printf "%.0f", total * 1000 }' "$test_root/time-$heads")"
    fi
    interactive="$("$repo_root/build/test-tui-pty" --measure "$repo_root/build/hydra-tui" \
        "$repo_root/bin/hydra" "$repo_root/tests/fixtures/tui/fake-bin")"
    startup_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"startup_ms":\([0-9][0-9]*\).*/\1/p')"
    window_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"window_ms":\([0-9][0-9]*\).*/\1/p')"
    interactive_cpu_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"cpu_ms":\([0-9][0-9]*\).*/\1/p')"
    cpu_percent=$((interactive_cpu_ms * 100 / window_ms))
    adapter_ms=$((adapter_end - adapter_start))
    render_ms=$((render_end - render_start))
    printf '{"schema_version":1,"benchmark":"native-tui-live-refresh","heads":%s,"frames":10,"adapter_ms":%s,"render_ms":%s,"render_cpu_ms":%s,"interactive_startup_ms":%s,"interactive_window_ms":%s,"interactive_cpu_ms":%s,"interactive_cpu_percent":%s,"budgets":{"adapter_ms":1000,"render_ms":100,"interactive_startup_ms":3000,"interactive_cpu_percent":40},"scope":"live isolated tmux sessions; selected-pane capture disabled"}\n' \
        "$heads" "$adapter_ms" "$render_ms" "$cpu_ms" "$startup_ms" "$window_ms" "$interactive_cpu_ms" "$cpu_percent"
    [ "$adapter_ms" -le 1000 ] || { echo "adapter budget exceeded at $heads heads" >&2; exit 1; }
    [ "$render_ms" -le 100 ] || { echo "render budget exceeded at $heads heads" >&2; exit 1; }
    [ "$startup_ms" -le 3000 ] || { echo "interactive startup budget exceeded at $heads heads" >&2; exit 1; }
    [ "$cpu_percent" -le 40 ] || { echo "interactive CPU budget exceeded at $heads heads" >&2; exit 1; }
done
