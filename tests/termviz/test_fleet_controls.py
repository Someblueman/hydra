"""Public fleet task controls through a real native TUI PTY."""
import os
from pathlib import Path
import tempfile

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()

with tempfile.TemporaryDirectory(prefix="hydra-fleet-controls-") as folder:
    base = Path(folder)
    log = base / "dispatch.log"
    log.unlink(missing_ok=True)
    mode = base / "mode"
    mode.write_text("fresh")
    wrapper = base / "hydra-wrapper"
    wrapper.write_text(f"""#!/bin/sh
printf "ARGS:%s\n" "$*" >> {log}
if [ "$1" = fleet ] && [ "$2" = tui-visual-data ]; then
  state=$(cat {mode})
  printf 'HYDRA_FLEET_TUI\\t3\\n'
  printf 'F\\tbuild\\t/project\\tbranch\\thead\\tinstance\\tLIVE\\n'
  printf 'T\\tbuild\\tresponded\\t0\\t-\\treachable\\tfresh\\t1\\t1\\n'
  printf 'O\\tbuild\\ttask_1\\t-\\t-\\t-\\t-\\tnone\\trecorded\\trunning\\tnone\\t-\\tinspect\\t1\\t1\\t%s\\t0\\tunavailable\\tunavailable\\t%s\\t-\\t-\\t-\\t-\\n' "$state" "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
  printf 'O\\tbuild\\ttask_2\\t-\\t-\\t-\\t-\\tnone\\twaiting\\twaiting_approval\\tapproval\\tReview\\tdecide\\t1\\t1\\t%s\\t1\\tunavailable\\tunavailable\\t%s\\t-\\t-\\t-\\treq-2\\n' "$state" "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
  exit 0
fi
printf '%s\\n' "$@" >> {log}
exit 0
""")
    wrapper.chmod(0o755)
    env = {**os.environ, "TERM": "xterm-256color", "HYDRA_BIN_CMD": str(wrapper), "HYDRA_HOME": str(base / "home")}
    s = Session([str(BUILD / "hydra-tui"), "--fleet", "--view", "overview"], 120, 40, env=env, cwd=base)
    try:
        s.until("task_2", timeout=8)
        assert "task_2" in s.screen.text()
        s.pump(1.0)
        s.send("j")
        s.pump(.5)
        assert "req-2" in s.screen.text()
        s.send("X")
        s.until("Type cancel to dispatch for the selected task", timeout=5)
        s.send("cancel\r")
        s.pump(1)
        dispatched = log.read_text()
        assert "task_2" in dispatched and "cancel" in dispatched
        assert "task_1" not in dispatched
        assert "dispatch finished; outcome unconfirmed" in s.screen.text() or "refresh task evidence" in s.screen.text()
    finally:
        s.close(keys=b"q")
print("PASS fleet controls PTY: second-task selection, bound digest visibility, and task-scoped cancel dispatch")
