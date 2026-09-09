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
    python3 - "$_wtfq_home" <<'PYLOCK' && return 0
import fcntl
import os
from pathlib import Path
import sys
import time

home = Path(sys.argv[1])
for _ in range(100):
    busy = False
    for path in home.glob("fleet/tasks/task_*/owner.lock"):
        try:
            fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW)
        except FileNotFoundError:
            continue
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            busy = True
        finally:
            os.close(fd)
    if not busy:
        sys.exit(0)
    time.sleep(0.2)
sys.exit(1)
PYLOCK
    printf 'Receiver tasks are still active; preserving fixture %s\n' "$fixture" >&2
    return 1
}
