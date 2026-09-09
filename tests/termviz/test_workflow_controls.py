"""Real durable requests and lifecycle commands through native workspace forms."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()

with tempfile.TemporaryDirectory(prefix="hydra-controls-") as folder:
    base = Path(folder)
    repo, wrapper = base / "repo", base / "bin"
    repo.mkdir()
    wrapper.mkdir()
    tmux = shutil.which("tmux")
    assert tmux
    socket = str(base / "tmux.sock")
    (wrapper / "tmux").write_text(f'#!/bin/sh\nexec {shlex.quote(tmux)} -S {shlex.quote(socket)} -f /dev/null "$@"\n')
    (wrapper / "tmux").chmod(0o755)
    env = {**os.environ, "PATH": f"{wrapper}:{os.environ['PATH']}", "TERM": "xterm-256color",
           "HYDRA_FLEET_BIN": str(BUILD / "hydra-fleet"), "HYDRA_HOME": str(base / "home"),
           "HYDRA_NONINTERACTIVE": "1", "HYDRA_SKIP_AI": "1", "HYDRA_NO_SWITCH": "1"}
    hydra = str(ROOT / "bin/hydra")

    def run(*args, check=True):
        return subprocess.run(args, cwd=repo, env=env, check=check, capture_output=True, text=True, timeout=40)

    run("git", "init", "-q")
    run("git", "config", "user.name", "Test")
    run("git", "config", "user.email", "test@example.com")
    (repo / "tracked").write_text("base\n")
    run("git", "add", ".")
    run("git", "commit", "-qm", "fixture")
    run(hydra, "init", "--no-agent", "--trust")
    run(hydra, "spawn", "operator-worker", "--no-agent")
    worker = Path(run(hydra, "path", "operator-worker").stdout.strip())
    original_pids = set(run("tmux", "list-panes", "-a", "-F", "#{pane_pid}").stdout.splitlines())
    evidence = ROOT / "build/workspace-control-evidence"
    evidence.mkdir(parents=True, exist_ok=True)
    effects = base / "effects"
    effect = base / "effect.sh"
    effect.write_text('#!/bin/sh\n[ "$2" != after ] || sleep 10\nprintf "%s\\n" "$2" >> "$1"\n')
    flow = base / "flow.yml"
    flow.write_text(f"""version: 1
id: workspace-control
resources:
  disk_mb: 1
steps:
  - id: before
    kind: exec
    idempotent: false
    args:
      head: operator-worker
      argv: [sh, {effect}, {effects}, before]
  - id: input
    kind: approval-wait
    idempotent: false
    needs: [before]
    args:
      head: operator-worker
      name: review
      message: Review operator evidence before continuing
  - id: after
    kind: exec
    idempotent: false
    needs: [input]
    args:
      head: operator-worker
      argv: [sh, {effect}, {effects}, after]
""")
    s = None
    try:
        for action, terminal in [("approve", "succeeded"), ("reject", "failed"), ("cancel", "cancelled")]:
            started = run(hydra, "workflow", "run", str(flow), check=False)
            assert started.returncode == 3, started.stderr
            run_id = started.stdout.splitlines()[0]
            run_dir = next((base / "home").glob(f"state/v2/projects/*/workflows/runs/{run_id}"))
            request = run_dir.joinpath("steps/input/request-id").read_text().strip()
            decision = run_dir / "approvals" / request / "decision/action"
            s = Session([str(BUILD / "hydra-tui"), "--hydra", hydra], 140, 40, env=env, cwd=repo)
            s.until("A CONVERSATION")
            s.send("Ca")
            s.until("INPUT TO AGENT")
            s.pump(.3)
            if action == "approve":
                s.send("printf unsubmitted > unsent-control-proof")
            s.send("\x02\t\tz")
            s.until("OBSERVED EVIDENCE", timeout=15)
            rows = run(hydra, "workflow", "tui-data").stdout.splitlines()
            runs = [line.split("\t")[1] for line in rows if line.startswith("W\t")]
            s.send("]" * runs.index(run_id))
            s.until(run_id, timeout=15)
            s.send("j" * 200)
            s.until("Review operator evidence before continuing", timeout=15)
            s.screen.save(evidence / f"waiting-{action}-140x40.html")
            if action == "approve":
                for width, height in [(40, 10), (80, 24), (140, 40)]:
                    s.resize(width, height)
                    s.pump(.3)
                    assert s.screen.overflow == 0
                    assert "waiting-approval" in s.screen.text(), s.screen.text()
                    s.screen.save(evidence / f"waiting-{width}x{height}.html")
            if action == "approve":
                s.send("Y")
                s.until("Request ID from the evidence pane")
                s.send(request + "\r")
                s.until("Type approve to confirm")
                s.send("wrong\r")
                s.until("Control not submitted")
                assert not decision.exists()
            if action in ("approve", "reject"):
                s.send("Y" if action == "approve" else "N")
                s.until("Request ID from the evidence pane")
                s.send(request + "\r")
                s.until(f"Type {action} to confirm")
                s.send(action + "\r")
                s.until("Control completed", timeout=15)
                assert decision.read_text().strip() == action
                assert run_dir.joinpath("state").read_text().strip() == "waiting-approval"
                assert effects.read_text().splitlines().count("after") == (0 if action == "approve" else 1)
                s.send("R")
                s.until("Type resume to confirm")
                s.send("resume\r")
            else:
                s.send("X")
                s.until("Type cancel to confirm")
                s.send("cancel\r")
            if action == "approve":
                s.until("OBSERVED EVIDENCE / run running", timeout=10)
                for width, height in [(40, 10), (80, 24), (140, 40)]:
                    s.resize(width, height)
                    s.pump(.3)
                    assert s.screen.overflow == 0
                    s.screen.save(evidence / f"running-{width}x{height}.html")
                assert run_dir.joinpath("state").read_text().strip() == "running"
                s.close(keys=b"q")
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and run_dir.joinpath("state").read_text().strip() != terminal:
                if s.closed:
                    time.sleep(.1)
                else:
                    s.pump(.1)
            assert run_dir.joinpath("state").read_text().strip() == terminal
            if not s.closed:
                s.until("Control failed" if action == "reject" else "Control completed", timeout=15)
                s.send("k" * 200)
                s.until(f"Workflow {run_id}: {terminal}", timeout=15)
                for width, height in [(40, 10), (80, 24), (140, 40)]:
                    s.resize(width, height)
                    s.pump(.3)
                    assert s.screen.overflow == 0
                    s.screen.save(evidence / f"{terminal}-{width}x{height}.html")
                s.send("R")
                s.until("Terminal run: no resume or cancel", timeout=10)
                s.send("]")
                s.pump(.3)
                assert "Terminal run: no resume or cancel" not in s.screen.text()
                s.close(keys=b"q")
        assert effects.read_text().splitlines().count("after") == 1
        assert not (worker / "unsent-control-proof").exists()
        assert original_pids <= set(run("tmux", "list-panes", "-a", "-F", "#{pane_pid}").stdout.splitlines())
        panes = run("tmux", "list-panes", "-a", "-F", "#{pane_id}").stdout.splitlines()
        assert any("unsent-control-proof" in run("tmux", "capture-pane", "-p", "-t", pane).stdout for pane in panes)
        assert run(hydra, "workflow", "--workspace-control", "../", "cancel", "-", check=False).returncode != 0
    finally:
        if s:
            s.abort()
        subprocess.run([tmux, "-S", socket, "kill-server"], capture_output=True, check=False)
print("PASS workspace controls: explicit decisions, separate resume, detached continuation, rejection and cancellation")
