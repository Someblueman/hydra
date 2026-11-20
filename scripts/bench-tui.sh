#!/bin/sh
# Measure the control centre at roadmap head counts, on the path `hydra tui`
# takes. The heads are real: a state-v2 store for a scratch repository, written
# by the functions the CLI uses, with one live tmux session per head.
#
#   adapter_ms   one head snapshot as the control centre reads it at every
#                refresh: the state store read in the helper's own process,
#                plus its one tmux command (`hydra-tui --data`). The shell
#                reader `hydra tui --data` is not timed: the control centre
#                `hydra tui` starts (`hydra-tui --store`) never runs it, and
#                tests/test_tui_store_parity.sh keeps the two byte-identical.
#   render_ms    ten headless frames of that snapshot.
#   interactive  `hydra` in a pseudo-terminal, as a user starts it (the
#                launcher, then `hydra-tui --store`): time to the first frame,
#                and the processor time of the whole process tree over the two
#                seconds after it, as a share of one core. The heads have never
#                been observed, so the window includes the activity observer's
#                first records and its first `hydra adapter watch`. It starts
#                once every session is older than the observer's quiet
#                threshold (ACTIVITY_QUIET_AFTER_SECONDS in src/tui/activity.c):
#                a younger session has no activity state yet and nothing would
#                be recorded for it, which no head of a real project is.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build}"
case "$build_dir" in /*) ;; *) build_dir="$PWD/$build_dir" ;; esac
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

# The control centre draws its frame with UTF-8 when the locale has it, and the
# interactive measurement waits for that frame.
if [ "$(locale charmap 2>/dev/null || true)" != UTF-8 ]; then
    utf8_locale="$(locale -a 2>/dev/null | awk '
        { name = tolower($0); sub(/utf-8$/, "utf8", name) }
        name == "c.utf8" { c = $0 }
        name == "en_us.utf8" { e = $0 }
        END { print (c != "" ? c : e) }')"
    [ -n "$utf8_locale" ] || { echo "bench-tui: no UTF-8 locale (C.UTF-8 or en_US.UTF-8) is installed" >&2; exit 1; }
    LC_ALL="$utf8_locale"
    export LC_ALL
fi

now_ms() {
    if command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf "%.0f\n", time * 1000'
    else
        printf '%s000\n' "$(date +%s)"
    fi
}

export HYDRA_HOME="$bench_home"
export HYDRA_TUI_BIN="$build_dir/hydra-tui"
export HYDRA_REAL_TMUX="$real_tmux"
export HYDRA_TEST_TMUX_SOCKET="$tmux_socket"
PATH="$repo_root/tests/fixtures/tui/fake-bin:$PATH"
export PATH

HYDRA_LIB_DIR="$repo_root/lib"
export HYDRA_LIB_DIR
# shellcheck source=../lib/output.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/output.sh"
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/locks.sh"
# shellcheck source=../lib/identity.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/identity.sh"
# shellcheck source=../lib/state_v2.sh
# shellcheck disable=SC1091
. "$HYDRA_LIB_DIR/state_v2.sh"

project_id="$(cd "$bench_repo" && hydra_ensure_project_id)"
state_v2_init_project "$project_id" "$bench_repo"
created=0
exceeded=0
quiet_after=11

for heads in 5 20 100; do
    while [ "$created" -lt "$heads" ]; do
        created=$((created + 1))
        "$real_tmux" -L "$tmux_socket" new-session -d -s "bench-$created"
        state_v2_create_head "$project_id" "bench-$created" "bench-$created" - - 1 - - "$bench_repo" > /dev/null
    done
    fixture="$test_root/$heads.tsv"
    adapter_start="$(now_ms)"
    (cd "$bench_repo" && "$build_dir/hydra-tui" --hydra "$repo_root/bin/hydra" --data) > "$fixture"
    adapter_end="$(now_ms)"
    rows="$(grep -c '^H	' "$fixture" || true)"
    [ "$rows" -eq "$heads" ] || { echo "bench-tui: the adapter listed $rows heads, not $heads" >&2; exit 1; }
    render_start="$(now_ms)"
    "$build_dir/hydra-tui" --headless-fixture "$fixture" --size 120x40 --frames 10 > /dev/null
    render_end="$(now_ms)"
    cpu_ms=null
    if [ -x /usr/bin/time ]; then
        /usr/bin/time -p "$build_dir/hydra-tui" --headless-fixture "$fixture" \
            --size 120x40 --frames 10 > /dev/null 2> "$test_root/time-$heads"
        cpu_ms="$(awk '/^(user|sys) / { total += $2 } END { printf "%.0f", total * 1000 }' "$test_root/time-$heads")"
    fi
    # The sessions created above become quiet, as the observer sees them.
    sleep "$quiet_after"
    # HYDRA_TEST_PUBLIC_ENTRY: the session is `hydra` itself, which finds the
    # helper through HYDRA_TUI_BIN and starts it with --store.
    interactive="$(cd "$bench_repo" && HYDRA_TEST_PUBLIC_ENTRY=1 "$build_dir/test-tui-pty" --measure \
        "$build_dir/hydra-tui" "$repo_root/bin/hydra" "$repo_root/tests/fixtures/tui/fake-bin")"
    startup_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"startup_ms":\([0-9][0-9]*\).*/\1/p')"
    window_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"window_ms":\([0-9][0-9]*\).*/\1/p')"
    interactive_cpu_ms="$(printf '%s\n' "$interactive" | sed -n 's/.*"cpu_ms":\([0-9][0-9]*\).*/\1/p')"
    cpu_percent=$((interactive_cpu_ms * 100 / window_ms))
    adapter_ms=$((adapter_end - adapter_start))
    render_ms=$((render_end - render_start))
    printf '{"schema_version":1,"benchmark":"native-tui-live-refresh","heads":%s,"frames":10,"adapter_ms":%s,"render_ms":%s,"render_cpu_ms":%s,"interactive_startup_ms":%s,"interactive_window_ms":%s,"interactive_cpu_ms":%s,"interactive_cpu_percent":%s,"budgets":{"adapter_ms":1000,"render_ms":100,"interactive_startup_ms":3000,"interactive_cpu_percent":40},"scope":"live isolated tmux sessions; adapter is the native store read (hydra-tui --data); interactive is hydra as a user starts it (hydra-tui --store)"}\n' \
        "$heads" "$adapter_ms" "$render_ms" "$cpu_ms" "$startup_ms" "$window_ms" "$interactive_cpu_ms" "$cpu_percent"
    [ "$adapter_ms" -le 1000 ] || { echo "adapter budget exceeded at $heads heads" >&2; exceeded=1; }
    [ "$render_ms" -le 100 ] || { echo "render budget exceeded at $heads heads" >&2; exceeded=1; }
    [ "$startup_ms" -le 3000 ] || { echo "interactive startup budget exceeded at $heads heads" >&2; exceeded=1; }
    [ "$cpu_percent" -le 40 ] || { echo "interactive CPU budget exceeded at $heads heads" >&2; exceeded=1; }
done
# Every head count is measured before a budget fails the run.
[ "$exceeded" -eq 0 ]
