#!/bin/sh
# Sourced acceptance helper: retain host tools but make tmux undiscoverable.
# The caller owns and removes the supplied disposable directory.
headless_path() {
    mkdir -p "$1"
    _hp_remaining="${PATH}:"
    while [ -n "$_hp_remaining" ]; do
        _hp_directory=${_hp_remaining%%:*}
        _hp_remaining=${_hp_remaining#*:}
        _hp_directory=$(CDPATH='' cd -- "${_hp_directory:-.}" 2>/dev/null && pwd -P) || continue
        for _hp_entry in "$_hp_directory"/* "$_hp_directory"/.[!.]* "$_hp_directory"/..?*; do
            _hp_name=${_hp_entry##*/}
            [ "$_hp_name" != tmux ] && [ -f "$_hp_entry" ] && [ -x "$_hp_entry" ] || continue
            [ ! -e "$1/$_hp_name" ] && [ ! -L "$1/$_hp_name" ] || continue
            ln -s "$_hp_entry" "$1/$_hp_name"
        done
    done
    PATH="$1"
    export PATH
    ! command -v tmux >/dev/null 2>&1
}
