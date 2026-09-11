#!/bin/sh
# Select a native LLVM 22 analyzer; quality-c.sh checks the exact pinned version.
set -eu
if [ -n "${HYDRA_CLANG_TIDY:-}" ]; then
    exec "$HYDRA_CLANG_TIDY" "$@"
fi
for tool in clang-tidy-22 /opt/homebrew/opt/llvm@22/bin/clang-tidy /usr/local/opt/llvm@22/bin/clang-tidy clang-tidy; do
    if command -v "$tool" >/dev/null 2>&1; then
        exec "$tool" "$@"
    fi
done
printf '%s\n' 'Install native clang-tidy 22.1.8 or set HYDRA_CLANG_TIDY to its executable.' >&2
exit 127
