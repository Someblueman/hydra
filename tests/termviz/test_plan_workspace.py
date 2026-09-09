"""Real compiler and native PTY: revisions, diagnostics and reversible layouts."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from pty_support import Session

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()

with tempfile.TemporaryDirectory(prefix="hydra-plan-workspace-") as folder:
    base = Path(folder)
    repo = base / "repo"
    shutil.copytree(ROOT / "tests/fixtures/plan/repo", repo)
    draft, policy = base / "draft.json", base / "policy.json"
    shutil.copy(ROOT / "tests/fixtures/plan/plan.json", draft)
    shutil.copy(ROOT / "tests/fixtures/plan/policy.json", policy)
    env = {**os.environ, "TERM": "xterm-256color", "HYDRA_FLEET_BIN": str(BUILD / "hydra-fleet"), "HYDRA_HOME": str(base / "home"), "HYDRA_NONINTERACTIVE": "1", "HYDRA_SKIP_AI": "1"}
    for args in (["git", "init", "-q"], ["git", "add", "."],
                 ["git", "-c", "user.name=Test", "-c", "user.email=test@example.com", "commit", "-qm", "fixture"],
                 [str(ROOT / "bin/hydra"), "init", "--no-agent", "--trust"]):
        subprocess.run(args, cwd=repo, env=env, check=True, stdout=subprocess.DEVNULL)
    s = Session([str(BUILD / "hydra-tui"), "--hydra", str(ROOT / "bin/hydra")], 140, 40, env=env, cwd=repo)
    try:
        s.until("A CONVERSATION")
        s.send("P")
        s.until("Draft JSON path:")
        s.send(str(draft) + "\r")
        s.until("Policy JSON path:")
        s.send(str(policy) + "\r")
        s.until("Revision 1 / DRAFT")
        s.send("VB\t\t")
        s.until("READY / awaiting approval", timeout=30)
        s.send("z")
        s.until("READY / awaiting approval")
        first = s.screen.text()
        assert "Revision 1" in first
        s.send("jjjjB")
        s.until("A CONVERSATION")
        s.send("B")
        s.until("B PLAN OVERVIEW")
        assert "Revision 1" in s.screen.text()
        changed = json.loads(draft.read_text())
        changed["objective"] = "Revised workspace acceptance objective"
        draft.write_text(json.dumps(changed))
        s.until("Revision 2 / DRAFT", timeout=8)
        assert "READY / awaiting approval" not in s.screen.text()
        s.send("V")
        s.until("READY / awaiting approval", timeout=30)
        s.send("kkkk")
        s.until("Revised workspace acceptance objective")
        for width, height in [(40, 10), (80, 24), (140, 40)]:
            if width == 40:
                s.send("j")
                time.sleep(.1)  # Leave a pre-resize frame queued in the PTY.
            before_resize = len(s.raw)
            s.resize(width, height)
            s.pump(.3)
            if s.screen.overflow:
                (ROOT / "build/plan-resize-failure.ansi").write_bytes(s.raw[before_resize:])
            assert s.screen.overflow == 0, f"{width}x{height}, overflow={s.screen.overflow}, clears={s.screen.clears}\n{s.screen.text()}"
        draft.write_text('{"broken":')
        s.until("Revision 3 / DRAFT", timeout=8)
        s.send("V")
        s.until("INVALID", timeout=30)
        s.send("CA")
        s.until("A CONVERSATION")
        s.close(keys=b"q")
    finally:
        s.abort()
print("PASS plan workspace: real compilation, revised scope, invalid input, layouts and terminal restoration")
