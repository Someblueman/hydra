#!/bin/sh
# Lint what ShellCheck does not cover: tracked Python (ruff, policy in ruff.toml)
# and the GitHub workflows (actionlint). `make lint` runs this after lint-shell.sh.
#
# Both tools are optional locally: a missing one prints a one-line warning and is
# skipped. CI sets HYDRA_LINT_STRICT=1, which turns a missing tool into a failure.
set -eu
cd "$(dirname "$0")/.."

failed=0

# Say, without failing, when the installed release is not the one quality.json
# pins: findings differ between releases.
version_warning() {
    tool=$1
    installed=$2
    [ -f quality.json ] || return 0
    pinned="$(sed -n "/\"$tool\": {/,/}/s/.*\"version\": \"\([^\"]*\)\".*/\1/p" quality.json | head -n 1)"
    if [ -n "$pinned" ] && [ -n "$installed" ] && [ "$pinned" != "$installed" ]; then
        printf 'lint-extra: warning: %s %s is installed but quality.json pins %s; findings may differ from CI\n' \
            "$tool" "$installed" "$pinned" >&2
    fi
}

# Succeed when the tool is on PATH. Otherwise warn and skip it, or, in strict
# mode, report the failure.
require_tool() {
    tool=$1
    install=$2
    if command -v "$tool" >/dev/null 2>&1; then
        return 0
    fi
    if [ "${HYDRA_LINT_STRICT:-}" = 1 ]; then
        printf 'lint-extra: error: %s is not installed (%s)\n' "$tool" "$install" >&2
        failed=1
    else
        printf 'lint-extra: warning: %s is not installed, skipping (%s)\n' "$tool" "$install" >&2
    fi
    return 1
}

if require_tool ruff 'pipx install ruff, or pip install ruff'; then
    version_warning ruff "$(ruff --version | sed -n 's/^ruff //p')"
    printf 'Checking Python with ruff...\n'
    git ls-files -z --cached --others --exclude-standard -- '*.py' |
        xargs -0 ruff check --quiet -- || failed=1
fi

if require_tool actionlint 'brew install actionlint, or https://github.com/rhysd/actionlint/releases'; then
    version_warning actionlint "$(actionlint --version | sed -n '1p')"
    printf 'Checking GitHub workflows with actionlint...\n'
    git ls-files -z --cached --others --exclude-standard -- '.github/workflows/*.yml' '.github/workflows/*.yaml' |
        xargs -0 actionlint || failed=1
fi

exit "$failed"
