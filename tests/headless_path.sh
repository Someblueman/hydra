#!/bin/sh
# Sourced acceptance helpers: retain host tools but make tmux undiscoverable.
# The caller owns and removes the supplied disposable directories.

# Fill <dir> with links to every executable on the current PATH except tmux.
# The caller's PATH is left unchanged.
# Usage: headless_path_build <dir>
headless_path_build() {
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
}

# Make the current process tmux-less: PATH becomes <dir> alone.
# Usage: headless_path <dir>
headless_path() {
    headless_path_build "$1" || return 1
    PATH="$1"
    export PATH
    ! command -v tmux >/dev/null 2>&1
}

# Execution-host (receiver) side of a split-PATH qualification: <dir> holds the
# host tools without tmux plus a tmux stand-in that refuses every terminal
# operation loudly (exit 99) and records it in <log>. The version probe that
# dependency checks run (tmux -V) fails quietly, as an absent tmux would, and is
# recorded in <log>.probes instead. The caller's own PATH (the coordinator side)
# is left unchanged; run receiver commands with PATH=<dir>.
# Usage: headless_receiver_path <dir> <log>
headless_receiver_path() {
    headless_path_build "$1" || return 1
    rm -f "$1/tmux"
    cat > "$1/tmux" <<EOF
#!/bin/sh
# tmux is absent on this execution host; nothing here may depend on it.
if [ "\${1:-}" = -V ] && [ "\$#" -eq 1 ]; then
    printf 'tmux -V\n' >> '$2.probes'
    exit 1
fi
printf 'tmux %s\n' "\$*" >> '$2'
printf 'Error: tmux is absent on this execution host; refused: tmux %s\n' "\$*" >&2
exit 99
EOF
    chmod +x "$1/tmux" || return 1
    : > "$2"
    : > "$2.probes"
}
