#!/bin/sh
# Run a command in the test environment of scripts/test-env.sh, with a private
# git configuration that is removed afterwards.
# Usage: with-test-env.sh <command> [argument...]
set -eu
[ "$#" -gt 0 ] || { printf 'usage: with-test-env.sh <command> [argument...]\n' >&2; exit 2; }
scripts=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
# shellcheck source=test-env.sh
# shellcheck disable=SC1091
. "$scripts/test-env.sh"
env_dir=$(mktemp -d "${TMPDIR:-/tmp}/hydra-test-env.XXXXXX")
trap 'rm -rf "$env_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
test_env_apply "$scripts" "$env_dir"
status=0
"$@" || status=$?
exit "$status"
