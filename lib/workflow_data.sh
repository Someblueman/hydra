#!/bin/sh
# The shell owns workflow sequencing; the optional helper handles bounded data.

workflow_data_tool() (
    cmd_fleet_dispatch workflow-data "$@"
)

workflow_data_validate() (
    _wdv_file="$1"
    _wdv_ref="$(workflow_parse "$_wdv_file" data)" || exit 1
    [ -n "$_wdv_ref" ] || exit 0
    _wdv_tmp="$(mktemp -d)" || exit 1
    trap 'rm -rf "$_wdv_tmp"' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
    workflow_parse "$_wdv_file" runtime > "$_wdv_tmp/graph.tsv" || exit 1
    workflow_data_tool validate "$(dirname "$_wdv_file")" "$_wdv_ref" "$_wdv_tmp/graph.tsv" >/dev/null
)

workflow_data_initialize() {
    _wdi_file="$1" _wdi_dir="$2"
    _wdi_ref="$(workflow_parse "$_wdi_file" data)" || return 1
    [ -n "$_wdi_ref" ] || return 0
    workflow_data_tool init "$_wdi_dir" "$(workflow_repo_root)" "$(dirname "$_wdi_file")" "$_wdi_ref" > "$_wdi_dir/data-initialization.json" || {
        cli_error workflow invalid_data 'workflow inputs could not be snapshotted and validated' 'inspect the data manifest, input types, paths, sizes, and digests'
        return 1
    }
    workflow_atomic_scalar "$_wdi_dir/data-hash" "$(git hash-object "$_wdi_dir/data.json")"
}

workflow_data_definition_matches() {
    _wdb_dir="$1"
    _wdb_ref="$(workflow_parse "$_wdb_dir/resolved.yml" data)" || return 1
    [ -n "$_wdb_ref" ] || return 0
    [ -f "$_wdb_dir/data.json" ] && [ ! -L "$_wdb_dir/data.json" ] &&
        [ "$(git hash-object "$_wdb_dir/data.json")" = "$(sed -n '1p' "$_wdb_dir/data-hash")" ]
}

workflow_data_bindings_match() {
    workflow_data_definition_matches "$1" || return 1
    [ ! -f "$1/data.json" ] || workflow_data_tool verify "$1" >/dev/null
}
