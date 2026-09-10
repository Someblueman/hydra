#!/bin/sh
# Quiesce this fixture's accepted receiver tasks before removing their files.
: "${root:?}"
# shellcheck source=/dev/null
. "$root/tests/tmux_fixture_cleanup.sh"

workflow_task_fixture_quiesce() {
    : "${root:?}" "${fixture:?}"
    _wtfq_home="${1:-$HYDRA_HOME}"
    for task_record in "$_wtfq_home"/fleet/tasks/task_*/acceptance.json; do
        [ -f "$task_record" ] || continue
        task_id="$(basename "$(dirname "$task_record")")"
        printf '{"protocol":1,"action":"task","operation":"cancel","task_id":"%s"}\n' "$task_id" |
            HYDRA_HOME="$_wtfq_home" "$root/bin/hydra" fleet serve >/dev/null 2>&1 || :
    done
    # The native receiver owns this flock for its full lifetime. A recorded PID
    # can be reused by an unrelated process before a long test reaches teardown.
    # shellcheck source=/dev/null
    . "$root/tests/fixture-tools.sh"
    fixture_lock wait "$_wtfq_home" && return 0
    printf 'Receiver tasks are still active; preserving fixture %s\n' "$fixture" >&2
    return 1
}
