"""Real Hydra heads -> identity-checked tmux clients -> embedded terminal input."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()
EVIDENCE = ROOT / "build/attached-evidence"
EVIDENCE.mkdir(parents=True, exist_ok=True)


def attached() -> None:
    with tempfile.TemporaryDirectory(prefix="hydra-attached-") as folder:
        temp = Path(folder)
        repo = temp / "repo"
        repo.mkdir()
        wrapper = temp / "bin"
        wrapper.mkdir()
        tmux = shutil.which("tmux")
        assert tmux
        socket = str(temp / "tmux.sock")
        (wrapper / "tmux").write_text(f"#!/bin/sh\nexec {shlex.quote(tmux)} -S {shlex.quote(socket)} -f /dev/null \"$@\"\n")
        (wrapper / "tmux").chmod(0o755)
        env = {**os.environ, "PATH": f"{wrapper}:{os.environ['PATH']}", "HYDRA_HOME": str(temp / "home"),
               "HYDRA_SKIP_AI": "1", "HYDRA_NONINTERACTIVE": "1", "HYDRA_NO_SWITCH": "1", "NO_COLOR": "1", "TERM": "xterm-256color"}

        def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
            return subprocess.run(args, cwd=repo, env=env, text=True, capture_output=True, check=check, timeout=30)

        def hydra(*args: str) -> str:
            return run(str(ROOT / "bin/hydra"), *args).stdout.strip()

        run("git", "init", "-q")
        run("git", "config", "user.name", "Test")
        run("git", "config", "user.email", "test@example.com")
        (repo / "input").write_text("attachment\n")
        run("git", "add", "input")
        run("git", "commit", "-qm", "base")
        hydra("init", "--no-agent", "--trust")
        s = None
        try:
            hydra("spawn", "one", "--no-agent")
            hydra("spawn", "two", "--no-agent")
            one = Path(hydra("path", "one"))
            two = Path(hydra("path", "two"))
            original_pids = run("tmux", "list-panes", "-a", "-F", "#{pane_pid}").stdout
            rows = [line.split("\t") for line in hydra("tui", "--data").splitlines() if line.startswith("H\t")]
            one = Path(hydra("path", rows[0][1]))
            two = Path(hydra("path", rows[1][1]))
            # A stale instance must not attach to a replacement head.
            refused = run(str(ROOT / "bin/hydra"), "tui", "--attach", rows[0][24], "instance_00000000000000000000", check=False)
            assert refused.returncode != 0 and "no longer current" in refused.stderr
            slow = temp / "slow"
            started = temp / "started"
            adapter = temp / "hydra-adapter"
            adapter.write_text("#!/bin/sh\n" +
                f'if [ "$1:$2" = tui:--data ] && [ -f {shlex.quote(str(slow))} ]; then\n' +
                f'  touch {shlex.quote(str(started))}\n  sleep 3\nfi\n' +
                f'exec {shlex.quote(str(ROOT / "bin/hydra"))} "$@"\n')
            adapter.chmod(0o755)
            s = Session([str(BUILD / "hydra-tui"), "--hydra", str(adapter)], 140, 40, env=env, cwd=repo)
            s.until("HYDRA WORKSPACE")
            s.send("a")
            s.until("INPUT TO AGENT")
            s.pump(.5)
            s.send("printf 'one' > attachment-proof\r")
            s.pump(.5)
            assert (one / "attachment-proof").exists(), s.screen.text()
            assert (one / "attachment-proof").read_text() == "one"
            slow.touch()
            for _ in range(30):
                if started.exists():
                    break
                s.pump(.1)
            assert started.exists(), "Delayed observation did not start"
            s.send("printf 'responsive' > latency-proof\r")
            s.pump(.4)
            assert (one / "latency-proof").read_text() == "responsive", "Snapshot collection blocked terminal input"
            s.until("STALE: last good", timeout=4)
            slow.unlink()
            s.until("Current snapshot", timeout=6)
            s.send("sleep 15\r")
            s.pump(.2)
            s.send("\x03")
            s.pump(.2)
            s.send("printf 'interrupted' > interruption-proof\r")
            s.pump(.4)
            assert (one / "interruption-proof").read_text() == "interrupted"
            assert s.process.poll() is None, "Ctrl-C must target the attached terminal"
            s.send("\x1b[200~printf 'paste' > paste-proof\x1b[201~\r")
            s.pump(.4)
            assert (one / "paste-proof").read_text() == "paste"
            s.send("printf 'draft' > draft-proof")
            s.pump(.2)
            s.send("\x02D")
            s.until("D STATISTICS")
            assert not (one / "draft-proof").exists()
            s.send("D")
            s.until("INPUT TO AGENT")
            s.send("\r")
            s.pump(.5)
            assert (one / "draft-proof").read_text() == "draft"
            # Return input to Hydra, select the other head, attach a second client.
            s.send("\x02\t\tj")
            s.pump(.2)
            s.send("a")
            s.until("INPUT TO AGENT")
            s.pump(.5)
            s.send("printf 'two' > attachment-proof\r")
            s.pump(.5)
            assert (two / "attachment-proof").read_text() == "two"
            s.send("\x02n")
            s.pump(.2)
            s.send("printf 'returned' > returned-proof\r")
            s.pump(.5)
            assert (one / "returned-proof").read_text() == "returned"
            s.send("printf 'reconnected' > reconnect-proof")
            s.pump(.1)
            s.send("\x02x")
            s.pump(.2)
            assert not (one / "reconnect-proof").exists()
            s.send("a")
            s.until("INPUT TO AGENT")
            s.pump(.4)
            s.send("\r")
            s.pump(.4)
            assert (one / "reconnect-proof").read_text() == "reconnected"
            s.send("printf 'zoom-draft' > zoom-proof")
            s.pump(.1)
            s.send("\x02z")
            s.until("z restore panes")
            assert "NAVIGATION" not in s.screen.text(), "Zoom retained hidden pane content"
            s.send("\x02D")
            s.until("D STATISTICS")
            s.send("D")
            s.until("z restore panes")
            s.send("\x02z")
            s.until("NAVIGATION")
            assert not (one / "zoom-proof").exists()
            s.send("\r")
            s.pump(.4)
            assert (one / "zoom-proof").read_text() == "zoom-draft"
            s.screen.save(EVIDENCE / "attached-140x40.html")
            for cols, rows_count in [(80, 24), (40, 10), (140, 40)]:
                s.resize(cols, rows_count)
                s.pump(.3)
                assert s.screen.overflow == 0
                s.send("stty size > size-proof\r")
                s.pump(.3)
                terminal_rows, terminal_cols = map(int, (one / "size-proof").read_text().split())
                if cols == 40:
                    assert terminal_rows >= 6 and terminal_cols >= 35, "Compact conversation is too small"
                    assert "NAVIGATION" not in s.screen.text()
                    assert "INPUT TO AGENT" in s.screen.text()
                s.screen.save(EVIDENCE / f"attached-{cols}x{rows_count}.html")
            run("tmux", "detach-client", "-s", "=" + rows[0][2])
            s.until("CLIENT DISCONNECTED")
            assert "NO INPUT" in s.screen.text()
            s.send("\x02r")
            s.until("INPUT TO AGENT")
            s.pump(.4)
            s.send("printf 'resumed' > resumed-proof\r")
            s.pump(.4)
            assert (one / "resumed-proof").read_text() == "resumed"
            s.close(b"\x02q")
            s = None
            assert run("tmux", "list-panes", "-a", "-F", "#{pane_pid}").stdout == original_pids
            assert not run("tmux", "list-clients", "-F", "#{client_pid}").stdout.strip()
            print("PASS attached PTY: real input, draft preservation across D, two existing sessions, resize and client-only exit")
        finally:
            if s is not None:
                s.abort()
            run("tmux", "kill-server", check=False)


if __name__ == "__main__":
    attached()
