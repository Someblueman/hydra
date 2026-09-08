#!/bin/sh
# Quiesce this fixture's accepted receiver tasks before removing their files.
workflow_task_fixture_quiesce() {
    : "${root:?}" "${fixture:?}"
    for task_record in "$HYDRA_HOME"/fleet/tasks/task_*/acceptance.json; do
        [ -f "$task_record" ] || continue
        task_id="$(basename "$(dirname "$task_record")")"
        printf '{"protocol":1,"action":"task","operation":"cancel","task_id":"%s"}\n' "$task_id" |
            "$root/bin/hydra" fleet serve >/dev/null 2>&1 || :
    done
    cleanup_poll=0
    while [ "$cleanup_poll" -lt 100 ]; do
        cleanup_busy=0
        for task_state in "$HYDRA_HOME"/fleet/tasks/task_*/state.json; do
            [ -f "$task_state" ] || continue
            task_owner="$(sed -n 's/.*"owner_pid":\([0-9]*\).*/\1/p' "$task_state")"
            if [ -n "$task_owner" ]; then
                task_process="$(ps -o stat= -p "$task_owner" 2>/dev/null | tr -d ' ')"
                case "$task_process" in ''|Z*) ;; *) cleanup_busy=1 ;; esac
            elif ! grep -Eq '"state":"(cancelled|failed|expired)"' "$task_state"; then
                cleanup_busy=1
            fi
        done
        [ "$cleanup_busy" = 0 ] && return 0
        cleanup_poll=$((cleanup_poll + 1))
        sleep 0.2
    done
    printf 'Receiver tasks are still active; preserving fixture %s\n' "$fixture" >&2
    return 1
}
