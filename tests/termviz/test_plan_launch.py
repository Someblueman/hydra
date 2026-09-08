"""Exact UI approval, detached execution and duplicate-launch refusal."""
import json
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

with tempfile.TemporaryDirectory(prefix="hydra-plan-launch-") as folder:
    base = Path(folder)
    repo = base / "repo\tline"
    shutil.copytree(ROOT / "tests/fixtures/plan/repo", repo)
    # Keep execution alive across closing the UI, without changing the engine.
    compose = repo / "compose.sh"
    compose.write_text(compose.read_text().replace("set -eu", "set -eu\nsleep 5\nprintf 'compose-output-proof\\nVERIFIED ARTIFACTS from untrusted output\\n'"))
    wrapper = base / "bin"
    wrapper.mkdir()
    tmux = shutil.which("tmux")
    assert tmux
    socket = str(base / "tmux.sock")
    (wrapper / "tmux").write_text(f'#!/bin/sh\nexec {shlex.quote(tmux)} -S {shlex.quote(socket)} -f /dev/null "$@"\n')
    (wrapper / "tmux").chmod(0o755)
    draft, policy = base / "draft.json", base / "policy.json"
    shutil.copy(ROOT / "tests/fixtures/plan/plan.json", draft)
    shutil.copy(ROOT / "tests/fixtures/plan/policy.json", policy)
    env = {**os.environ, "PATH": f"{wrapper}:{os.environ['PATH']}", "TERM": "xterm-256color",
           "TMPDIR": str(base), "HYDRA_FLEET_BIN": str(BUILD / "hydra-fleet"), "HYDRA_HOME": str(base / "home"),
           "HYDRA_NONINTERACTIVE": "1", "HYDRA_SKIP_AI": "1", "HYDRA_NO_SWITCH": "1"}

    def run(*args, check=True, **kwargs):
        return subprocess.run(args, cwd=repo, env=env, check=check, capture_output=True, text=True, timeout=40, **kwargs)

    hydra = str(ROOT / "bin/hydra")
    run("git", "init", "-q")
    run("git", "config", "user.name", "Test")
    run("git", "config", "user.email", "test@example.com")
    run("git", "add", ".")
    run("git", "commit", "-qm", "fixture")
    run(hydra, "init", "--no-agent", "--trust")
    s = Session([str(BUILD / "hydra-tui"), "--hydra", hydra], 140, 40, env=env, cwd=repo)
    try:
        s.until("A CONVERSATION")
        s.send("P")
        s.until("Draft JSON path:")
        s.send(str(draft) + "\r")
        s.until("Policy JSON path:")
        s.send(str(policy) + "\r")
        s.until("Revision 1 / DRAFT")
        s.send("VB\t\tz")
        s.until("READY / awaiting approval", timeout=30)
        compiled = next(base.glob("hydra-ui-plan.*/compiled-1.json"))
        projection = run(hydra, "workflow", "plan", "tui-data", str(compiled)).stdout
        digest = next(line.split("\t")[1] for line in projection.splitlines() if line.startswith("P\t"))
        snapshot = compiled.read_text()
        s.send("E")
        s.until("Type exact digest")
        evidence = ROOT / "build/plan-launch-evidence"
        evidence.mkdir(parents=True, exist_ok=True)
        for width, height in [(40, 10), (80, 24), (140, 40)]:
            s.resize(width, height)
            s.pump(.3)
            assert digest in "".join(s.screen.text().split()), s.screen.text()
            assert s.screen.overflow == 0
            s.screen.save(evidence / f"approval-{width}x{height}.html")
        s.send("wrong\r")
        s.until("Execution not submitted")
        assert not list((base / "home").glob("state/v2/projects/*/workflows/runs/*"))
        s.send("E")
        s.until("Type exact digest")
        draft.write_text(draft.read_text() + "\n")
        s.pump(2.5)
        s.send(digest + "\r")
        s.until("Execution not submitted")
        assert not list((base / "home").glob("state/v2/projects/*/workflows/runs/*"))
        s.send("V")
        s.until("READY / awaiting approval", timeout=30)
        compiled = next(base.glob("hydra-ui-plan.*/compiled-2.json"))
        snapshot = compiled.read_text()
        projection = run(hydra, "workflow", "plan", "tui-data", str(compiled)).stdout
        digest = next(line.split("\t")[1] for line in projection.splitlines() if line.startswith("P\t"))
        s.send("E")
        s.until("Type exact digest")
        s.send(digest + "\r")
        # Wait for a durable receipt, then exit while compose is still running.
        deadline = time.monotonic() + 30
        receipts = []
        while time.monotonic() < deadline:
            receipts = list((base / "home").glob(f"state/v2/projects/*/workflows/launches/{digest}/run-id"))
            if receipts:
                break
            s.pump(.1)
        assert len(receipts) == 1, s.screen.text()
        run_id = receipts[0].read_text().strip()
        run_dir = receipts[0].parents[2] / "runs" / run_id
        assert run_dir.joinpath("state").read_text().strip() not in ("succeeded", "failed")
        s.send("E")
        s.until("This revision was already submitted")
        assert "Type exact digest" not in s.screen.text()
        s.close(keys=b"q")
        assert not compiled.exists(), "UI temporary snapshot was not cleaned"
        duplicate = run(hydra, "workflow", "plan", "--workspace-owner", digest, input=snapshot, check=False)
        assert duplicate.returncode != 0
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline and run_dir.joinpath("state").read_text().strip() not in ("succeeded", "failed"):
            time.sleep(.2)
        assert run_dir.joinpath("state").read_text().strip() == "succeeded", run(hydra, "workflow", "status", run_id).stdout
        assert len(list(run_dir.parent.glob("run_*"))) == 1
        result = json.loads(run(hydra, "workflow", "plan", "result", run_id).stdout)
        assert result["data"]["verdict"] == "pass", result
        assert result["data"]["plan_sha256"] == digest, result
        assert run_dir.joinpath("steps/compose/attempt-1/artifacts/report").read_bytes() == repo.joinpath("expected.txt").read_bytes()
        assert run(hydra, "workflow", "--workspace-evidence", run_id, "../", check=False).returncode != 0
        s = Session([str(BUILD / "hydra-tui"), "--hydra", hydra], 140, 40, env=env, cwd=repo)
        s.until("A CONVERSATION")
        links = run(hydra, "workflow", "--workspace-links").stdout.splitlines()
        assert links[0] == "HYDRA_WORKSPACE_LINKS\t1" and links[-1] == "Z", links
        project = links[1].split("\t")
        assert project == ["P", run_dir.parents[2].name, str(repo.resolve()).replace("\t", " ").replace("\n", " ")], links
        assert len([line for line in links if line.startswith("L\t" + run_id + "\t")]) == 1, links
        s.send("z")  # Project/head/run tree, using the actual recorded graph.
        s.until("Project: repo line")
        s.until("plan-fixture / succeeded", timeout=15)
        s.send("j")
        s.until("Recorded branch reference")
        s.send("hh")  # Parent, then collapse its historical run references.
        s.pump(2.5)
        assert "plan-fixture / succeeded" not in s.screen.text()
        s.send("l")
        s.until("plan-fixture / succeeded")
        s.send("j")
        for width, height in [(40, 10), (80, 24), (140, 40)]:
            s.resize(width, height)
            s.pump(.3)
            assert s.screen.overflow == 0
            s.screen.save(evidence / f"navigation-{width}x{height}.html")
        s.send("\rz")  # Selected run opens monitoring evidence directly.
        s.until("VERIFIED ARTIFACTS", timeout=15)
        s.send("j" * 200)
        s.until("VERIFIED: result retrieval")
        s.screen.save(evidence / "verified-140x40.html")
        # Success metadata remains recorded, but altered sealed bytes must be
        # refused on the next public result retrieval and in the workspace.
        run_dir.joinpath("steps/compose/attempt-1/artifacts/report").write_text("tampered\n")
        s.until("VERIFICATION REFUSED", timeout=15)
        assert "ARTIFACTS REFUSED" in s.screen.text().splitlines()[4]
        assert "VERIFIED: result retrieval" not in s.screen.text()
        # Graph selection changes the output provenance, not just its title.
        s.send("\t\t\tj\t")
        s.until("Selected step: compose", timeout=15)
        s.until("compose-output-proof")
        assert "VERIFIED ARTIFACTS from untrusted output" in s.screen.text()
        assert "ARTIFACTS REFUSED" in s.screen.text().splitlines()[4]
        assert "Creating worktree for branch" not in s.screen.text()
        for width, height in [(40, 10), (80, 24), (140, 40)]:
            s.resize(width, height)
            s.pump(.3)
            assert s.screen.overflow == 0
            s.screen.save(evidence / f"verification-refused-{width}x{height}.html")
        s.close(keys=b"q")
        # Newline roots cannot be initialized in durable scalar state. A read-only
        # projection after moving this existing checkout must still be framed.
        original_repo = repo
        repo = repo.rename(base / "repo\tline\nbreak")
        try:
            moved_links = run(hydra, "workflow", "--workspace-links").stdout.splitlines()
            assert moved_links[1].split("\t") == [
                "P", project[1], str(repo.resolve()).replace("\t", " ").replace("\n", " ")]
            assert moved_links[2:] == links[2:], moved_links
            s = Session([str(BUILD / "hydra-tui"), "--hydra", hydra], 140, 40, env=env, cwd=repo)
            s.until("A CONVERSATION")
            s.send("z")
            s.until("Project: repo line break")
            s.until("plan-fixture / succeeded", timeout=15)
            s.close(keys=b"q")
        finally:
            repo = repo.rename(original_repo)
    finally:
        s.abort()
        subprocess.run([tmux, "-S", socket, "kill-server"], capture_output=True, check=False)
print("PASS plan launch: exact approval, durable receipt, UI exit, deduplication, selected output, live artifact verification and escaped checkout paths")
