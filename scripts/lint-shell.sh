#!/bin/sh
# Shared POSIX/style and syntax policy for make, CI and development hooks.
set -eu
if [ "${1:-}" = --version ]; then
    exec shellcheck --version
fi
# Findings differ between ShellCheck releases. Say so in one line, without
# failing, when the installed release is not the one quality.json pins.
version_warning() {
    quality="$(dirname "$0")/../quality.json"
    [ -f "$quality" ] || return 0
    pinned="$(sed -n '/"shellcheck": {/,/}/s/.*"version": "\([^"]*\)".*/\1/p' "$quality" | head -n 1)"
    installed="$(shellcheck --version 2>/dev/null | sed -n 's/^version: //p')"
    if [ -n "$pinned" ] && [ -n "$installed" ] && [ "$pinned" != "$installed" ]; then
        printf 'lint-shell: warning: ShellCheck %s is installed but quality.json pins %s; findings may differ from CI\n' \
            "$installed" "$pinned" >&2
    fi
}
if [ "$#" -eq 0 ]; then
    # Include new source files while excluding ignored build fixtures and worktrees.
    status=0
    git ls-files -z --cached --others --exclude-standard -- '*.sh' bin/hydra |
        HYDRA_LINT_NESTED=1 xargs -0 sh "$0" || status=$?
    version_warning
    exit "$status"
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
[ -n "${HYDRA_LINT_NESTED:-}" ] || version_warning
exit "$failed"
