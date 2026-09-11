#!/bin/sh
# Sourced acceptance helper: retain host tools but make tmux undiscoverable.
# The caller owns and removes the supplied disposable directory.
headless_path() {
    _hp_target=$1
    mkdir -p "$_hp_target"
    _hp_remaining="${PATH}:"
    while [ -n "$_hp_remaining" ]; do
        _hp_directory=${_hp_remaining%%:*}
        _hp_remaining=${_hp_remaining#*:}
        _hp_directory=$(CDPATH='' cd -- "${_hp_directory:-.}" 2>/dev/null && pwd -P) || continue
        set --
        for _hp_entry in "$_hp_directory"/* "$_hp_directory"/.[!.]* "$_hp_directory"/..?*; do
            _hp_name=${_hp_entry##*/}
            if [ "$_hp_name" = tmux ] || [ ! -f "$_hp_entry" ] || [ ! -x "$_hp_entry" ]; then continue; fi
            if [ -e "$_hp_target/$_hp_name" ] || [ -L "$_hp_target/$_hp_name" ]; then continue; fi
            set -- "$@" "$_hp_entry"
            if [ "$#" -ge 100 ]; then
                ln -s "$@" "$_hp_target" || return 1
                set --
            fi
        done
        if [ "$#" -gt 0 ]; then ln -s "$@" "$_hp_target" || return 1; fi
    done
    PATH="$_hp_target"
    export PATH
    ! command -v tmux >/dev/null 2>&1
}
