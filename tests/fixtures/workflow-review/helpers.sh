#!/bin/sh
# Shared plumbing for public workflow review tests. Every resource belongs to
# the disposable fixture; no repository checkout or default tmux server is used.
: "${root:?}" "${fixture:?}" "${native:?}" "${repo:?}"
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
review_fixture_cleanup() {
    review_exit=$?
    if ! workflow_task_fixture_quiesce || ! test_tmux_fixture_cleanup "$fixture"; then
        printf 'Review fixture cleanup failed; preserved %s\n' "$fixture" >&2
        exit 1
    fi
    if [ "${HYDRA_REVIEW_KEEP_FIXTURE:-0}" = 1 ]; then
        printf 'Preserved review fixture: %s\n' "$fixture" >&2
    else
        rm -rf "$fixture"
    fi
    exit "$review_exit"
}
review_fixture_environment() {
    HYDRA_FLEET_BIN="$native"
    HYDRA_BIN_CMD="$root/bin/hydra"
    HYDRA_HOME="$fixture/home"
    HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2"
    HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
    TMUX_TMPDIR="$fixture/tmux"
    mkdir -p "$TMUX_TMPDIR"
    unset TMUX TMUX_PANE
    export HYDRA_FLEET_BIN HYDRA_BIN_CMD HYDRA_HOME HYDRA_STATE_V2_ROOT
    export HYDRA_NONINTERACTIVE HYDRA_SKIP_AI HYDRA_NO_SWITCH TMUX_TMPDIR
    trap review_fixture_cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP
}
review_revision() {
    : "${project:?}"
    "$native" workflow-data attention "$project" > "$fixture/current-attention.json"
    jq -er --arg run "$1" --arg step "$2" \
        '[.data.items[] | select(.run_id==$run and .step_id==$step and .attempt_id=="attempt-1")] | if length==1 then .[0].revision else error("ambiguous candidate") end' \
        "$fixture/current-attention.json" | tr -d '\n' > "$fixture/current-revision"
    shasum -a 256 "$fixture/current-revision" | awk '{print $1}'
}
review_selection() {
    # Obtain hashes from the actual public native attention projection.
    (cd "$repo" && "$root/bin/hydra" workflow attention-data) > "$fixture/attention.tsv"
    awk -F '\t' -v run="$1" -v step="$2" '$1=="ITEM" && $8==run && $9==step {print}' "$fixture/attention.tsv"
}
review_exact() (
    # $1 is review or review-data; $2 contains one real native ITEM row.
    _re_command="$1" _re_row="$2" _re_reference="${3:-}"
    IFS="$(printf '\t')" read -r _re_tag _re_source _re_kind _re_reason \
        _re_project _re_host _re_task _re_run _re_step _re_attempt _re_head \
        _re_instance _re_request _re_binding _re_revision _re_identity \
        _re_freshness _re_route _re_navigable < "$_re_row"
    [ "$_re_tag" = ITEM ] && [ "$_re_source" = 'workflow records' ]
    set -- "$_re_kind" "$_re_project" "$_re_host" "$_re_task" "$_re_run" \
        "$_re_step" "$_re_attempt" "$_re_head" "$_re_instance" "$_re_request" \
        "$_re_binding" "$_re_revision" "$_re_identity"
    [ -z "$_re_reference" ] || set -- "$@" "$_re_reference"
    "$root/bin/hydra" workflow "$_re_command" "$@"
)
review_frame_valid() {
    LC_ALL=C awk -F '\t' '
    NR==1 {if(NF!=15 || $1!="HYDRA_REVIEW" || $2!="1") exit 1; next}
    ended {exit 1}
    $1=="TEXT" {if(NF!=2) exit 1; text++; next}
    $1=="REF" {if(NF!=4) exit 1; refs++; next}
    $1=="PREVIEW" {if(NF!=3) exit 1; previews++; next}
    $1=="END" {if(NF!=4 || $2!=text+0 || $3!=refs+0 || $4!=previews+0) exit 1; ended=1; next}
    {exit 1}
    END {if(!ended) exit 1}' "$1"
}
