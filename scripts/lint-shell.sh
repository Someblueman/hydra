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
    # A bare mktemp ignores TMPDIR on macOS and escapes the per-case test directory.
    case $file in
        tests/*)
            if grep -nE '\$\(mktemp( -d)?\)' "$file"; then
                printf '%s: use test_mktemp_dir/test_mktemp_file or a template under TMPDIR\n' "$file"
                failed=1
            fi
            ;;
    esac
done
exit "$failed"
