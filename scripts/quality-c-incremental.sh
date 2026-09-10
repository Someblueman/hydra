#!/bin/sh
# Lifecycle complexity check; make quality-c retains the full static analyzer.
set -eu
tool=scripts/clang-tidy.sh
if [ "${1:-}" = --version ]; then
    exec "$tool" --version
fi
report=$(mktemp "${TMPDIR:-/tmp}/hydra-quality-c.XXXXXX")
trap 'rm -f "$report" "${report%.log}.tsv"' EXIT HUP INT TERM
QUALITY_C_LOG=$report
QUALITY_C_CHECKS='-*,readability-function-cognitive-complexity'
export QUALITY_C_LOG QUALITY_C_CHECKS
pkg-config --exists json-c
flags=$(make -s quality-c-flags)
# Match Makefile compiler argument splitting; preserve selected source arguments.
# shellcheck disable=SC2086
set -- "$@" -- $flags
sh scripts/quality-c.sh docs/quality/cognitive-complexity.tsv "$tool" "$@"
