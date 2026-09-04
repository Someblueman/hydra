#!/bin/sh
# Reproducible shell/native snapshot comparison. Evidence only, not a CI gate.

set -eu

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_root="$(mktemp -d)"
HYDRA_HOME="$test_root/home"
HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
export HYDRA_HOME HYDRA_STATE_V2_ROOT

cleanup() {
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

state="$HYDRA_STATE_V2_ROOT"
project=project_aaaaaaaaaaaaaaaa
mkdir -p "$state/projects/$project/heads"
printf '2\n' > "$state/schema-version"
printf '%s\n' "$project" > "$state/projects/$project/project-id"
printf '/tmp/benchmark\n' > "$state/projects/$project/repo-root"
index=0
while [ "$index" -lt 20 ]; do
    suffix="$(printf '%016x' "$index")"
    head="head_$suffix"
    instance="instance_$suffix"
    dir="$state/projects/$project/heads/$head"
    instance_dir="$dir/instances/$instance"
    mkdir -p "$instance_dir" "$dir/events/archive"
    printf '%s\n' "$head" > "$dir/head-id"
    printf 'benchmark-%s\n' "$index" > "$dir/branch"
    printf 'session-%s\n' "$index" > "$dir/session"
    printf '%s\n' - > "$dir/profile"
    printf '%s\n' - > "$dir/group"
    printf '%s\n' 100 > "$dir/created-at"
    printf '%s\n' - > "$dir/dependencies"
    printf '%s\n' - > "$dir/pr"
    printf '%s\n' "$instance" > "$dir/current-instance"
    printf 'running\n' > "$dir/desired-state"
    printf 'declared-done\n' > "$dir/completion-policy"
    printf '/tmp/%s\n' "$head" > "$dir/worktree"
    : > "$dir/task"
    : > "$dir/scopes"
    printf 'main\n' > "$dir/base-ref"
    printf '%s\n' "$instance" > "$instance_dir/instance-id"
    printf 'session-%s\n' "$index" > "$instance_dir/session"
    printf '100\n' > "$instance_dir/started-at"
    printf 'starting\n' > "$instance_dir/observed-status"
    printf 'hydra\n' > "$instance_dir/observed-source"
    printf 'exact\n' > "$instance_dir/observed-confidence"
    printf '100\n' > "$instance_dir/observed-at"
    : > "$instance_dir/observed-exit-code"
    : > "$instance_dir/provider-session-id"
    : > "$dir/events/events.jsonl"
    index=$((index + 1))
done

run_batch() {
    _rb_mode="$1"
    _rb_file="$2"
    _rb_index=0
    while [ "$_rb_index" -lt 3 ]; do
        _rb_start="$(now_ms)"
        if [ "$_rb_mode" = native ]; then
            HYDRA_CORE="$repo_root/build/hydra-core" "$repo_root/bin/hydra" snapshot --native > /dev/null
        else
            "$repo_root/bin/hydra" snapshot > /dev/null
        fi
        _rb_end="$(now_ms)"
        printf '%s\n' "$((_rb_end - _rb_start))" >> "$_rb_file"
        _rb_index=$((_rb_index + 1))
    done
}

shell_runs="$test_root/shell-runs"
native_runs="$test_root/native-runs"
run_batch shell "$shell_runs"
run_batch native "$native_runs"
shell_cold="$(sed -n '1p' "$shell_runs")"
native_cold="$(sed -n '1p' "$native_runs")"
shell_p50="$(sort -n "$shell_runs" | sed -n '2p')"
native_p50="$(sort -n "$native_runs" | sed -n '2p')"
shell_p95="$(sort -n "$shell_runs" | sed -n '3p')"
native_p95="$(sort -n "$native_runs" | sed -n '3p')"
if [ "$native_p95" -lt "$shell_p95" ]; then useful=true; else useful=false; fi
printf '{"schema_version":1,"benchmark":"snapshot","os":"%s","arch":"%s","heads":20,"runs":3,"shell":{"cold_ms":%s,"p50_ms":%s,"p95_ms":%s},"native":{"cold_ms":%s,"p50_ms":%s,"p95_ms":%s},"subprocess_count":"not-instrumented","rss":"not-measured-one-shot","native_faster":%s}\n' \
    "$(uname -s)" "$(uname -m)" "$shell_cold" "$shell_p50" "$shell_p95" \
    "$native_cold" "$native_p50" "$native_p95" "$useful"
