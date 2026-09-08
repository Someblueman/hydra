#!/bin/sh
# Real clang-tidy checks for the baseline policy, including failure boundaries.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
tool="$root/${1:-build/quality-tools/bin/clang-tidy}"
case "${1:-}" in /*) tool="$1" ;; esac
fixture="$(mktemp -d)"
cleanup() {
    code=$?
    [ "$code" -eq 0 ] || cat check.log >&2
    rm -rf "$fixture"
    exit "$code"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
cd "$fixture"
mkdir src
printf '# clang-tidy 22.1.8, threshold 15\n' > baseline.tsv
emit_probe() {
    printf 'int probe(int x) {\n'
    i=0
    while [ "$i" -lt "$1" ]; do
        printf 'if (x == %s) x++;\n' "$i"
        i=$((i + 1))
    done
    printf 'return x;\n}\n'
}
check() { sh "$root/scripts/quality-c.sh" baseline.tsv "$tool" src/probe.c -- -std=c99 > check.log 2>&1; }
emit_probe 15 > src/probe.c
check
emit_probe 16 > src/probe.c
if check; then echo 'new complex function was accepted' >&2; exit 1; fi
grep -q 'increased from 15 to 16' check.log
cp build/quality-c.tsv baseline.tsv
check
# Source-line movement is not a regression.
printf '\n\n' > src/moved.c
cat src/probe.c >> src/moved.c
mv src/moved.c src/probe.c
check
emit_probe 17 > src/probe.c
if check; then echo 'increased complexity was accepted' >&2; exit 1; fi
grep -q 'increased from 16 to 17' check.log
# An empty baseline must not accidentally treat current records as allowances.
: > baseline.tsv
if check; then echo 'empty baseline hid new complexity' >&2; exit 1; fi
printf 'malformed C\n' > src/probe.c
if check; then echo 'compiler failure was accepted' >&2; exit 1; fi
if sh "$root/scripts/quality-c.sh" baseline.tsv "$tool" -- -std=c99 > check.log 2>&1; then
    echo 'analysis without sources was accepted' >&2; exit 1
fi
if sh "$root/scripts/quality-c.sh" baseline.tsv "$tool" src/missing.c -- -std=c99 > check.log 2>&1; then
    echo 'missing source was accepted' >&2; exit 1
fi
emit_probe 16 > src/probe.c
printf 'src/probe.c\tprobe\t16\nsrc/probe.c\tprobe\t16\n' > baseline.tsv
if check; then echo 'duplicate baseline was accepted' >&2; exit 1; fi
grep -q 'Invalid complexity baseline record' check.log
printf 'C quality gate: threshold, new/increased functions, line movement, empty/duplicate baselines, missing sources and compiler errors passed\n'
