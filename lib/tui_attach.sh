#!/bin/sh
# Attach a presentation client to one recorded instance. No lifecycle mutation.
tui_attach_instance() (
    [ "$#" -eq 2 ] || return 2
    hydra_valid_id "$1" && hydra_valid_id "$2" || return 2
    _tai_project="$(hydra_get_project_id)" || return 1
    _tai_head="$(state_v2_head_dir "$_tai_project" "$1")" || return 1
    if [ "$(cat "$_tai_head/current-instance" 2>/dev/null)" != "$2" ] ||
       [ "$(cat "$_tai_head/instances/$2/instance-id" 2>/dev/null)" != "$2" ]; then
        echo 'Attachment refused: the selected instance is no longer current' >&2
        return 1
    fi
    _tai_session="$(cat "$_tai_head/instances/$2/session" 2>/dev/null)" || return 1
    [ -n "$_tai_session" ] || return 1
    # Resolve exact identity, then attach by tmux's immutable session ID. A branch
    # name or partial target must never select a different session accidentally.
    _tai_target="$(tmux display-message -p -t "=$_tai_session:" '#{session_id}' 2>/dev/null)" || return 1
    for _tai_pair in "HYDRA_PROJECT_ID=$_tai_project" "HYDRA_HEAD_ID=$1" "HYDRA_INSTANCE_ID=$2"; do
        _tai_key="${_tai_pair%%=*}"
        if [ "$(tmux show-environment -t "$_tai_target" "$_tai_key" 2>/dev/null)" != "$_tai_pair" ]; then
            echo "Attachment refused: tmux $_tai_key does not match the selected instance" >&2
            return 1
        fi
    done
    [ "$(cat "$_tai_head/current-instance" 2>/dev/null)" = "$2" ] || return 1
    unset TMUX
    exec tmux attach-session -t "$_tai_target"
)
