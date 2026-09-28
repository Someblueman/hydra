#!/bin/sh
# The native helper owns bounded structured-data inspection; shell only routes it.
workflow_attention() (
    if [ "$#" -ne 2 ] || [ "$2" != --json ]; then
        json_error "workflow attention" invalid_arguments "attention requires --json" "run hydra workflow --help"
        exit 1
    fi
    _wa_project="$(hydra_get_project_id 2>/dev/null || true)"
    [ -n "$_wa_project" ] || {
        json_error "workflow attention" not_initialized "Hydra project identity is unavailable" "run hydra init"
        exit 1
    }
    workflow_data_tool attention "$_wa_project"
)
workflow_attention_data() (
    [ "$#" -eq 1 ] || exit 2
    _wa_project="$(hydra_get_project_id 2>/dev/null || true)"
    [ -n "$_wa_project" ] || exit 1
    workflow_data_tool attention-data "$_wa_project"
)

# Per-user "seen" markers for attention items. This is a client preference, not
# workflow state: $HYDRA_HOME/attention/seen.tsv holds at most 512 rows of
# "identity<TAB>revision<TAB>seen-at", one per identity. A new revision of the
# same item no longer matches, so it is unseen again. Seen never approves,
# rejects or dismisses anything.
WORKFLOW_ATTENTION_SEEN_LIMIT=512

workflow_attention_seen_list() {
    printf 'HYDRA_ATTENTION_SEEN\t1\n'
    if [ -f "$1" ] && [ ! -L "$1" ]; then
        awk -F '\t' -v limit="$WORKFLOW_ATTENTION_SEEN_LIMIT" '
            NF == 3 && $1 ~ /^[0-9a-f]+$/ && length($1) == 64 && $2 ~ /^[0-9a-f]+$/ && length($2) == 64 {
                rows[++n] = $1 "\t" $2
            }
            END {
                first = n > limit ? n - limit + 1 : 1
                for (i = first; i <= n; i++) print "SEEN\t" rows[i]
                printf "END\t%d\n", (n >= first ? n - first + 1 : 0)
            }' "$1"
    else
        printf 'END\t0\n'
    fi
}

# Replace the identity's row (mark) or drop it (clear) under the home lock.
workflow_attention_seen_update() {
    _wasu_file="$1" _wasu_action="$2" _wasu_identity="$3" _wasu_revision="${4:-}"
    hydra_private_mkdir "$(dirname "$_wasu_file")" || return 1
    acquire_lock attention-seen || return 1
    _wasu_tmp="$(mktemp_adjacent "$_wasu_file")" || { release_lock attention-seen; return 1; }
    {
        if [ -f "$_wasu_file" ] && [ ! -L "$_wasu_file" ]; then
            awk -F '\t' -v id="$_wasu_identity" 'NF == 3 && $1 != id' "$_wasu_file"
        fi
        [ "$_wasu_action" = clear ] || printf '%s\t%s\t%s\n' "$_wasu_identity" "$_wasu_revision" "$(date +%s)"
    } | tail -n "$WORKFLOW_ATTENTION_SEEN_LIMIT" > "$_wasu_tmp" && atomic_replace "$_wasu_file" "$_wasu_tmp"
    _wasu_status=$?
    [ "$_wasu_status" -eq 0 ] || rm -f "$_wasu_tmp"
    release_lock attention-seen
    return "$_wasu_status"
}

workflow_attention_seen_digest() {
    case "$1" in *[!0-9a-f]*|'') return 1 ;; esac
    [ "${#1}" -eq 64 ]
}

workflow_attention_seen() (
    _was_file="${HYDRA_HOME:?}/attention/seen.tsv"
    case "$#:${2:-}" in
        2:list) ;;
        4:mark)
            workflow_attention_seen_digest "$3" && workflow_attention_seen_digest "$4" || exit 2
            workflow_attention_seen_update "$_was_file" mark "$3" "$4" || exit 1
            ;;
        3:clear)
            workflow_attention_seen_digest "$3" || exit 2
            workflow_attention_seen_update "$_was_file" clear "$3" || exit 1
            ;;
        *)
            printf '%s\n' 'Usage: hydra workflow attention-seen list | mark IDENTITY REVISION | clear IDENTITY' >&2
            exit 2
            ;;
    esac
    workflow_attention_seen_list "$_was_file"
)
