#!/bin/sh
# Kill session functions for Hydra
# POSIX-compliant shell script
#
# Provides session kill capabilities for single and bulk operations.
# Dependencies: paths.sh, git.sh, tmux.sh, state.sh

# Load message cleanup if available
if ! command -v cleanup_messages_for_branch >/dev/null 2>&1; then
    _kill_lib_dir="${HYDRA_LIB_DIR:-}"
    if [ -z "$_kill_lib_dir" ]; then
        _kill_lib_dir="$(cd "$(dirname "$0")/../lib" 2>/dev/null && pwd)" || true
    fi
    if [ -n "$_kill_lib_dir" ] && [ -f "$_kill_lib_dir/messages.sh" ]; then
        # shellcheck disable=SC1090,SC1091
        . "$_kill_lib_dir/messages.sh"
    fi
fi
