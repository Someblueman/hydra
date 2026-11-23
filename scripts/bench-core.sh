#!/bin/sh
# Shell and native snapshot timings on valid state-v2 stores, one JSON line per
# head count. Evidence only, not a CI gate.
# Usage: bench-core.sh [heads...]   (default: 5 20 60)
# HYDRA_BENCH_RUNS sets the runs per implementation and head count (default 3).

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
core="${HYDRA_CORE:-${BUILD_DIR:-$repo_root/build}/hydra-core}"
case "$core" in /*) ;; *) core="$PWD/$core" ;; esac
[ -x "$core" ] || { echo "bench-core: no helper at $core (run make build-core)" >&2; exit 1; }
runs="${HYDRA_BENCH_RUNS:-3}"
[ $# -gt 0 ] || set -- 5 20 60

test_root="$(mktemp -d)"
cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

HYDRA_CORE="$core"
HYDRA_LIB_DIR="$repo_root/lib"
HYDRA_HOME="$test_root/home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
export HYDRA_CORE HYDRA_LIB_DIR HYDRA_HOME HYDRA_STATE_V2_ROOT

# The store is written by the functions the CLI itself uses, so the fixture is
# valid for as long as the verifier and state_v2_create_head agree.
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

now_ms() {
    if command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf "%.0f\n", time * 1000'
    else
        printf '%s000\n' "$(date +%s)"
    fi
}

# Usage: make_store <heads>; a new HYDRA_HOME with one project of that many heads.
make_store() {
    HYDRA_HOME="$test_root/home-$1"
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    export HYDRA_HOME HYDRA_STATE_V2_ROOT
    _ms_project=project_aaaaaaaaaaaaaaaa
    state_v2_init_project "$_ms_project" "$test_root/repo"
    _ms_index=0
    while [ "$_ms_index" -lt "$1" ]; do
        state_v2_create_head "$_ms_project" "benchmark-$_ms_index" "session-$_ms_index" - - 100 - - \
            "$test_root/repo" > /dev/null
        _ms_index=$((_ms_index + 1))
    done
}

# Usage: run_batch <file> <snapshot option...>; one duration in ms per line.
run_batch() {
    _rb_file="$1"
    shift
    _rb_index=0
    while [ "$_rb_index" -lt "$runs" ]; do
        _rb_start="$(now_ms)"
        "$repo_root/bin/hydra" snapshot "$@" > /dev/null
        _rb_end="$(now_ms)"
        printf '%s\n' "$((_rb_end - _rb_start))" >> "$_rb_file"
        _rb_index=$((_rb_index + 1))
    done
}

# Usage: nearest_rank <file> <percent>; that percentile of the durations.
nearest_rank() {
    sort -n "$1" | awk -v percent="$2" '
        { value[NR] = $1 }
        END { rank = int(NR * percent / 100 + 0.999999); print value[rank < 1 ? 1 : rank] }'
}

for heads in "$@"; do
    make_store "$heads"
    # The comparison means something only when the default path really is the
    # helper and both implementations print the same document.
    "$repo_root/bin/hydra" snapshot --shell > "$test_root/shell.out"
    "$repo_root/bin/hydra" snapshot --native > "$test_root/native.out" 2> "$test_root/native.err"
    if [ -s "$test_root/native.err" ]; then
        echo "bench-core: the helper was not used at $heads heads:" >&2
        cat "$test_root/native.err" >&2
        exit 1
    fi
    cmp -s "$test_root/shell.out" "$test_root/native.out" || {
        echo "bench-core: shell and native snapshots differ at $heads heads" >&2
        exit 1
    }
    listed="$(grep -o '"head_id"' "$test_root/shell.out" | wc -l | tr -d ' ')"
    [ "$listed" -eq "$heads" ] || { echo "bench-core: fixture lists $listed heads, not $heads" >&2; exit 1; }
    shell_runs="$test_root/shell-runs-$heads"
    native_runs="$test_root/native-runs-$heads"
    run_batch "$shell_runs" --shell
    run_batch "$native_runs"
    shell_p95="$(nearest_rank "$shell_runs" 95)"
    native_p95="$(nearest_rank "$native_runs" 95)"
    if [ "$native_p95" -lt "$shell_p95" ]; then useful=true; else useful=false; fi
    printf '{"schema_version":1,"benchmark":"snapshot","os":"%s","arch":"%s","heads":%s,"runs":%s,"shell":{"cold_ms":%s,"p50_ms":%s,"p95_ms":%s},"native":{"cold_ms":%s,"p50_ms":%s,"p95_ms":%s},"subprocess_count":"not-instrumented","rss":"not-measured-one-shot","native_faster":%s}\n' \
        "$(uname -s)" "$(uname -m)" "$heads" "$runs" \
        "$(sed -n '1p' "$shell_runs")" "$(nearest_rank "$shell_runs" 50)" "$shell_p95" \
        "$(sed -n '1p' "$native_runs")" "$(nearest_rank "$native_runs" 50)" "$native_p95" "$useful"
done
