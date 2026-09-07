#!/bin/sh
# Exercise the shared checker through its real file discovery and installed hook.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
cd "$fixture"
git init -q
mkdir scripts
cp "$root/scripts/lint-shell.sh" scripts/
printf 'lint:\n\t@sh scripts/lint-shell.sh\n' > Makefile
sh "$root/scripts/install-hooks.sh" >/dev/null
printf '#!/bin/sh\nprintf "ok\\n"\n' > 'space name.sh'
printf '#!/bin/sh\nprintf "ok\\n"\n' > 'line
break.sh'
.git/hooks/pre-commit > valid.log 2>&1
# Both ordinary and unusual filenames must reach the same rejecting policy.
for file in 'space name.sh' 'line
break.sh'; do
    printf '#!/bin/sh\n[[ bad ]]\n' > "$file"
    if .git/hooks/pre-commit > invalid.log 2>&1; then
        echo 'lint accepted a bashism in a quoted filename' >&2
        exit 1
    fi
    grep -q SC3010 invalid.log
    printf '#!/bin/sh\nprintf "ok\\n"\n' > "$file"
done
.git/hooks/pre-commit > valid.log 2>&1
printf 'Shared shell lint and installed hook handle spaces/newlines and reject invalid POSIX scripts\n'
