"""Disposable Hydra fixtures for scripts/plan-eval.py.

Every fixture is its own Git repository with a private HYDRA_HOME and a private
tmux socket directory, driven only through the public `hydra` CLI. Command logs
are kept beside the fixture so a reported number can be traced to its command.
"""

import json
import os
import shutil
import subprocess
import time
from pathlib import Path

GIT_IDENTITY = ["-c", "user.name=Plan evaluation", "-c", "user.email=plan-eval@example.invalid", "-c", "commit.gpgSign=false"]


class CommandFailed(RuntimeError):
    pass


class Fixture:
    def __init__(self, base: Path, fleet: Path):
        self.base, self.repo = base, base / "source"
        self.repo.mkdir(parents=True)
        (base / "tmux").mkdir()
        (base / "tmp").mkdir()
        self.env = {**os.environ, "HYDRA_HOME": str(base / "home"), "HYDRA_FLEET_BIN": str(fleet),
                    "HYDRA_NONINTERACTIVE": "1", "HYDRA_SKIP_AI": "1", "HYDRA_NO_SWITCH": "1",
                    "TMUX_TMPDIR": str(base / "tmux"), "TMPDIR": str(base / "tmp")}
        self.env.pop("TMUX", None)
        self.env.pop("TMUX_PANE", None)
        self.commands = []

    def call(self, argv, expected=0, label=None, timeout=300):
        started = time.monotonic()
        result = subprocess.run([str(a) for a in argv], cwd=self.repo, env=self.env, capture_output=True, text=True, timeout=timeout, check=False)
        elapsed = time.monotonic() - started
        stem = label or f"command-{len(self.commands)}"
        (self.base / (stem + ".stdout")).write_text(result.stdout)
        (self.base / (stem + ".stderr")).write_text(result.stderr)
        self.commands.append({"argv": [str(a) for a in argv], "exit": result.returncode, "seconds": round(elapsed, 3), "log": stem})
        (self.base / "commands.json").write_text(json.dumps(self.commands, indent=2) + "\n")
        if expected is not None and result.returncode != expected:
            raise CommandFailed(f"{' '.join(str(a) for a in argv[:4])} exited {result.returncode}, expected {expected}: {result.stderr.strip()[:400]}")
        return result.stdout, elapsed, result.returncode

    def commit(self):
        self.call(["git", "init", "-q"])
        self.call(["git", "add", "."])
        self.call(["git", *GIT_IDENTITY, "commit", "-qm", "plan evaluation fixture"])
        return self.call(["git", "rev-parse", "HEAD"])[0].strip()


class Hydra:
    def __init__(self, root: Path, fleet: Path, out: Path):
        self.root, self.fleet, self.out = root, fleet, out
        self.bin = root / "bin/hydra"

    def fixture(self, name: str) -> Fixture:
        base = self.out / "fixtures" / name
        if base.exists():
            shutil.rmtree(base)
        return Fixture(base, self.fleet)

    def init(self, f: Fixture):
        f.call([self.bin, "init", "--no-agent", "--trust"], label="init")

    def plan(self, f: Fixture, *argv, expected=0, label=None):
        return f.call([self.bin, "workflow", "plan", *argv], expected=expected, label=label)

    def compile(self, f: Fixture, plan: str, policy: str, output: Path, label: str):
        raw, seconds, _ = self.plan(f, "compile", plan, policy, output, label=label)
        return json.loads(raw)["data"]["sha256"], seconds

    def run(self, f: Fixture, compiled: Path, digest: str, expected: int, label: str):
        raw, seconds, code = self.plan(f, "run", compiled, "--accept", digest, expected=None, label=label)
        run_id = next((line for line in raw.splitlines() if line.startswith("run_")), None)
        if run_id is None or code not in (0, 1):
            raise CommandFailed(f"plan run did not report a run ID (exit {code})")
        return run_id, seconds, code == 0, code == expected

    def result(self, f: Fixture, run_id: str, label: str):
        raw, seconds, code = self.plan(f, "result", run_id, expected=None, label=label)
        try:
            payload = json.loads(raw)
        except ValueError:
            payload = {"ok": False}
        return bool(payload.get("ok")) and code == 0, seconds

    def observed(self, f: Fixture, run_id: str, label: str):
        raw, _, code = self.plan(f, "observed", run_id, expected=None, label=label)
        return json.loads(raw) if code == 0 else None

    def evaluate(self, f: Fixture, left: Path, right: Path, estimates=None, label="evaluate"):
        argv = ["evaluate", left, right] + (["--estimates", estimates] if estimates else [])
        raw, _, _ = self.plan(f, *argv, label=label)
        return json.loads(raw)["data"]

    def run_dir(self, f: Fixture, run_id: str) -> Path:
        return next((f.base / "home/state/v2/projects").glob(f"*/workflows/runs/{run_id}"))

    def kill(self, f: Fixture, heads):
        for head in heads:
            f.call([self.bin, "kill", head, "--force"], expected=None, label=f"kill-{head}")
        for socket in (f.base / "tmux").glob("tmux-*/*"):
            subprocess.run(["tmux", "-S", str(socket), "kill-server"], capture_output=True, check=False)


def scalar(path: Path):
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return None


def run_record(run: Path, step_ids):
    """Recorded facts of one run: elapsed seconds (started-at to completed-at,
    both epoch seconds) and repeated work."""
    created, completed = scalar(run / "started-at"), scalar(run / "completed-at")
    attempts = {step: scalar(run / "steps" / step / "attempts") for step in step_ids}
    return {"state": (run / "state").read_text().strip() if (run / "state").exists() else None,
            "elapsed_seconds": completed - created if created is not None and completed is not None else None,
            "attempts": attempts,
            "repeated_work_steps": sorted(step for step, count in attempts.items() if count is not None and count > 1)}


def merge_estimates(*documents):
    plans = {}
    for document in documents:
        if document:
            plans.update(document["plans"])
    return {"schema_version": 1, "plans": plans}


def transplant_estimates(document, source_digest, target_digest, source_text):
    """Reuse one plan's recorded step rows as the estimate for another plan
    with the same step IDs: a prediction to compare with that plan's own run."""
    entry = dict(document["plans"][source_digest])
    entry["source"] = source_text
    return {"schema_version": 1, "plans": {target_digest: entry}}
