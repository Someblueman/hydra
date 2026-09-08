#!/bin/sh
# Private workspace projection: declared branch references, not instance ownership.
workflow_links() (
    _wlk_project="$(hydra_get_project_id)" || return 1
    _wlk_repo="$(workflow_repo_root)" || return 1
    # The root is display-only; match the native adapter's single-field encoding.
    _wlk_repo="$(printf '%s' "$_wlk_repo" | tr '\t\r\n' '   ')"
    printf 'HYDRA_WORKSPACE_LINKS\t1\nP\t%s\t%s\n' "$_wlk_project" "$_wlk_repo"
    _wlk_root="$(workflow_runs_dir)" || return 1
    _wlk_runs=0
    for _wlk_dir in "$_wlk_root"/run_*; do
        [ -d "$_wlk_dir" ] || continue
        _wlk_id="$(basename "$_wlk_dir")"
        hydra_valid_id "$_wlk_id" || continue
        _wlk_runs=$((_wlk_runs + 1))
        [ "$_wlk_runs" -le 32 ] || break
        [ -f "$_wlk_dir/graph.tsv" ] || continue
        awk -F '\t' -v run="$_wlk_id" '
            $1 == "step" {
                branch = $3 == "spawn" ? $8 : $7
                if (branch != "" && branch != "-" && !seen[branch]++) {
                    if (++count > 128) exit
                    printf "L\t%s\t%s\n", run, branch
                }
            }
        ' "$_wlk_dir/graph.tsv"
    done | awk 'NR <= 512 { print }'
    printf 'Z\n'
)
