#!/bin/sh
# Shared POSIX/style and syntax policy for make, CI and development hooks.
set -eu
if [ "${1:-}" = --version ]; then
    exec shellcheck --version
fi
if [ "$#" -eq 0 ]; then
    # Include new source files while excluding ignored build fixtures and worktrees.
    git ls-files -z --cached --others --exclude-standard -- '*.sh' bin/hydra |
        xargs -0 sh "$0"
    exit $?
fi
failed=0
for file do
    printf 'Checking %s...\n' "$file"
    shellcheck --shell=sh --severity=style "$file" || failed=1
    dash -n "$file" || failed=1
done
exit "$failed"
