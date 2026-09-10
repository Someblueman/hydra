"""Public fleet task controls through a real native TUI PTY."""
import os
from pathlib import Path
import tempfile
import time

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()
DIGEST = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"

with tempfile.TemporaryDirectory(prefix="hydra-fleet-controls-") as folder:
    base = Path(folder)
    log = base / "dispatch.log"
    mode = base / "mode"
    count = base / "count"
    mode.write_text("fresh")
    count.write_text("0")
    (base / "active.lock").write_text("active")
    (base / "dirty.txt").write_text("dirty\n")
    wrapper = base / "hydra-wrapper"
    wrapper.write_text(f"""#!/bin/sh
printf 'ARGS:%s\\n' "$*" >> {log}
if [ "$1" = fleet ] && [ "$2" = tui-visual-data ]; then
  state=$(cat {mode})
  cancel=-; scope=-; requested=-; taskstate=waiting_approval; freshness=fresh
  case "$state" in
    requested) cancel=requested; scope=managed_commands; requested=2026-09-10T00:00:01Z ;;
    delivered) cancel=delivered; scope=managed_commands; requested=2026-09-10T00:00:02Z ;;
    confirmed) cancel=confirmed_stopped; scope=managed_commands; requested=2026-09-10T00:00:03Z ;;
    stale) freshness=stale ;;
    unknown) cancel=unknown; scope=managed_commands; requested=2026-09-10T00:00:04Z; taskstate=outcome_unknown ;;
  esac
  printf 'HYDRA_FLEET_TUI\\t3\\n'
  printf 'F\\tbuild\\t/project\\tbranch\\thead\\tinstance\\tLIVE\\n'
  printf 'T\\tbuild\\tresponded\\t0\\t-\\treachable\\tfresh\\t1\\t1\\n'
  printf 'O\\tbuild\\ttask_1\\t-\\t-\\t-\\t-\\tnone\\trecorded\\trunning\\tnone\\t-\\tinspect\\t1\\t1\\t%s\\t0\\tunavailable\\tunavailable\\t%s\\t-\\t-\\t-\\t-\\n' "$freshness" "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
  printf 'O\\tbuild\\ttask_2\\t-\\t-\\t-\\t-\\tnone\\twaiting\\t%s\\tapproval\\tReview\\tdecide\\t1\\t1\\t%s\\t1\\tunavailable\\tunavailable\\t%s\\t%s\\t%s\\t%s\\treq-2\\n' "$taskstate" "$freshness" "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" "$cancel" "$scope" "$requested"
  exit 0
fi
if [ "$1" = fleet ] && [ "$2" = task ]; then
  if [ "$3" = cancel ]; then
    n=$(cat {count}); n=$((n + 1)); printf '%s\\n' "$n" > {count}
    printf requested > {mode}; exit 9
  fi
fi
printf '%s\\n' "$@" >> {log}
exit 0
""")
    wrapper.chmod(0o755)
    env = {**os.environ, "TERM": "xterm-256color", "HYDRA_BIN_CMD": str(wrapper), "HYDRA_HOME": str(base / "home")}

    def launch():
        session = Session([str(BUILD / "hydra-tui"), "--fleet", "--view", "overview"], 120, 40, env=env, cwd=base)
        session.until("task_2", timeout=8)
        session.send("j")
        session.until("req-2", timeout=3)
        return session

    def confirm(session, key, word):
        session.send(key)
        session.until("INPUT TO HYDRA", timeout=3)
        session.send(word + "\r")
        session.pump(.5)

    s = launch()
    try:
        # Every approval/control argv carries the selected task's request and spec binding.
        confirm(s, "Y", "approve")
        confirm(s, "N", "reject")
        confirm(s, "R", "resume")
        lines = log.read_text()
        assert "task decide build --id task_2 --request req-2 --decision approve --trust-spec " + DIGEST in lines
        assert "task decide build --id task_2 --request req-2 --decision reject --trust-spec " + DIGEST in lines
        assert "task resume build --id task_2 --trust-spec " + DIGEST in lines

        # A receiver change while the modal is open prevents a stale mutation.
        before = count.read_text()
        s.send("Y")
        s.until("INPUT TO HYDRA", timeout=3)
        mode.write_text("stale")
        time.sleep(2.2)
        s.pump(.4)
        s.send("approve\r")
        s.pump(.5)
        assert count.read_text() == before
        assert "Task evidence changed during confirmation" in s.screen.text()
    finally:
        s.close(keys=b"q")

    # First cancel simulates a lost response but durably leaves a requested record.
    mode.write_text("fresh")
    s = launch()
    try:
        confirm(s, "X", "cancel")
        s.pump(1)
        assert "task_2" in log.read_text() and "cancel build --id task_2" in log.read_text()
        assert count.read_text() == "1\n"
        assert (base / "active.lock").read_text() == "active" and (base / "dirty.txt").read_text() == "dirty\n"
    finally:
        s.close(keys=b"q")

    # Restarting the observer sees each receiver-owned cancellation stage without replaying.
    for stage, marker in (("requested", "requested"), ("delivered", "delivered"), ("confirmed", "confirmed_stopped")):
        mode.write_text(stage)
        s = launch()
        try:
            assert marker in s.screen.text()
            assert count.read_text() == "1\n"
        finally:
            s.close(keys=b"q")

    mode.write_text("unknown")
    s = launch()
    try:
        s.send("X")
        s.pump(.5)
        assert "stale or uncertain" in s.screen.text()
        assert count.read_text() == "1\n"
    finally:
        s.close(keys=b"q")

    assert (base / "active.lock").read_text() == "active" and (base / "dirty.txt").read_text() == "dirty\n"
print("PASS fleet controls PTY: exact Y/N/R bindings, stale-modal refusal, lost cancel, receiver stages, restart and preservation")
