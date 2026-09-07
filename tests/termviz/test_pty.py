"""Real process/terminal acceptance for the dependency-free C workspace."""
from __future__ import annotations

import os
from pathlib import Path
import re
import signal
import sys

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", str(ROOT / "build"))).resolve()
DEMO = str(BUILD / "termviz-workspace")
CHILD = str(BUILD / "test-workspace-child")
EVIDENCE = ROOT / "build" / "workspace-evidence"
EVIDENCE.mkdir(parents=True, exist_ok=True)


def workspace() -> None:
    s = Session([DEMO], 80, 24)
    try:
        s.until("ACTIVITY")
        s.pump(.1)
        assert s.pump(.2) == b"", "Unchanged demo frame emitted terminal data"
        before = s.screen.clears
        s.send("\tjjj")
        s.until("pane 3 / offset 3")
        s.send("\tjj")
        s.until("pane 4 / offset 2")
        assert "pane 3 / offset 3" in s.screen.text(), "Scroll state leaked between panes"
        s.send("\t")
        s.pump(.1)
        s.send("h")
        s.pump(.1)
        assert "renderer" not in s.screen.text(), "Tree branch did not collapse"
        s.send("l")
        s.until("renderer")
        s.send("\x1b[<0;19;3M\x1b[<32;30;3M\x1b[<0;30;3m")
        s.pump(.15)
        assert s.screen.cells[2][28][0] == "│", "Divider did not follow mouse drag"
        assert s.screen.clears == before, "Interaction cleared the whole screen"
        s.screen.save(EVIDENCE / "workspace-80x24.html")
        s.resize(40, 10)
        s.send("\t")
        s.pump(.15)
        assert "FOCUS" in s.screen.text() and "q quit" in s.screen.text()
        assert s.screen.overflow == 0
        s.screen.save(EVIDENCE / "workspace-40x10.html")
        s.resize(140, 40)
        s.until("INSPECT")
        assert s.screen.overflow == 0
        s.screen.save(EVIDENCE / "workspace-140x40.html")
        s.close(b"q")
    except BaseException:
        s.abort()
        raise
    print("PASS workspace: tree, independent scrolling, focus, mouse divider, incremental output, three sizes, terminal restoration")


def shell() -> None:
    s = Session([DEMO, "--shell"], 140, 40)
    try:
        s.until("TV$ ")
        s.send("printf '\\033[31mSHELL_%s\\033[0m\\n' OK; false; printf 'STATUS:%s\\n' \"$?\"\r")
        s.until("SHELL_OK")
        s.until("STATUS:1")
        pid_match = re.search(r"Child PID: (\d+)", s.screen.text())
        assert pid_match
        child_pid = int(pid_match.group(1))
        s.send("i=0; while [ $i -lt 700 ]; do printf 'HISTORY_%03d\\n' $i; i=$((i+1)); done\r")
        s.until("HISTORY_699")
        s.until("History: 512 / 512 rows")
        s.send("\x02[k")
        s.pump(.2)
        assert "offset 1" in s.screen.text()
        s.send("\x02]")
        s.send("i=0; while [ $i -lt 12 ]; do printf 'STREAM_%02d\\n' $i; i=$((i+1)); sleep 0.1; done\r")
        s.send("\x02\t")
        s.until("FOCUS pane 4")
        s.until("STREAM_11")
        s.send("\x02\t\x02\t")
        s.send(f"{CHILD}\r")
        s.until("ALT_READY")
        s.until("Screen: alternate")
        s.send("x")
        s.until("SAFE_OUTPUT")
        assert b"]52;" not in s.raw and b"SHOULD_NOT_ESCAPE" not in s.raw
        s.resize(80, 24)
        s.send("r")
        s.until("SIZE:")
        model_size = re.search(r"Terminal: (\d+)x(\d+)", s.screen.text())
        reported = re.search(r"SIZE:(\d+):(\d+)", s.screen.text())
        assert model_size and reported and model_size.groups() == tuple(reversed(reported.groups()))
        s.screen.save(EVIDENCE / "shell-80x24.html")
        s.resize(40, 10)
        s.send("r")
        s.until("SIZE:")
        assert s.screen.overflow == 0
        s.screen.save(EVIDENCE / "shell-40x10.html")
        s.resize(140, 40)
        s.send("r")
        s.until("ALT_READY")
        s.screen.save(EVIDENCE / "shell-140x40.html")
        s.send("q")
        s.until("AFTER_ALT")
        s.send("printf 'ALT_STATUS:%s\\n' \"$?\"\r")
        s.until("ALT_STATUS:7")
        s.send("exit 9\r")
        s.until("exited / status 9")
        try:
            os.kill(child_pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("Exited shell was not reaped")
        s.close(b"\x02q")
    except BaseException:
        s.abort()
        raise
    print("PASS shell: commands/status, bounded history, focus during streaming, real fullscreen client, escape isolation, child resize/exit and restoration")


def interruption() -> None:
    s = Session([DEMO, "--shell"], 140, 40)
    try:
        s.until("TV$ ")
        s.send("sleep 30 & printf 'JOB_PID:'; jobs -p; wait\r")
        match = s.until_pattern(r"JOB_PID:(\d+)")
        job_pid = int(match.group(1))
        s.close(signum=signal.SIGTERM, expected=143)
        # Background jobs receive the terminal/session hangup. A zombie is no
        # longer executing; inspect its state where the platform exposes ps.
        import subprocess
        state = subprocess.run(["ps", "-o", "stat=", "-p", str(job_pid)], capture_output=True, text=True, check=False).stdout.strip()
        assert not state or state.startswith("Z"), f"Owned job remained live: {state}"
    except BaseException:
        s.abort()
        raise
    s = Session([DEMO, "--command", "/nonexistent/termviz-child"], 80, 24)
    try:
        s.until("exited / status 127")
        s.close(b"\x02q")
    except BaseException:
        s.abort()
        raise
    print("PASS lifecycle: interruption stops the owned shell/job; exec failure is visible; terminal restored")


def hydra() -> None:
    argv = [str(BUILD / "hydra-tui"), "--hydra", str(ROOT / "tests/fixtures/tui/fake-hydra.sh"), "--theme", "dark"]
    s = Session(argv, 140, 40)
    try:
        s.until("HYDRA WORKSPACE")
        s.until("Observed: LIVE")
        before = s.screen.clears
        s.send("j")
        s.until("Observed: STALE")
        s.send("\tjjj\tjj")
        s.pump(.2)
        assert "scroll 3" in s.screen.text() and "scroll 2" in s.screen.text()
        s.send("\x1b[<0;42;4M\x1b[<32;50;4M\x1b[<0;50;4m")
        s.pump(.3)
        assert s.screen.clears == before
        s.screen.save(EVIDENCE / "hydra-140x40.html")
        s.resize(80, 24)
        s.until("NAVIGATION")
        assert s.screen.overflow == 0
        s.screen.save(EVIDENCE / "hydra-80x24.html")
        s.resize(40, 10)
        s.until("FOCUS")
        assert "q quit" in s.screen.text() and s.screen.overflow == 0
        s.screen.save(EVIDENCE / "hydra-40x10.html")
        s.close(b"q")
    except BaseException:
        s.abort()
        raise
    print("PASS Hydra: real adapter rows, selection/details, independent pane scroll, mouse divider, resize and restoration")


if __name__ == "__main__":
    try:
        workspace()
        shell()
        interruption()
        hydra()
    except AssertionError as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise
