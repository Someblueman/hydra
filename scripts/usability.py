#!/usr/bin/env python3
"""Bounded installed-product journeys using the existing native PTY observer."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from bench_i1_pty import Observer


def run(argv: list[str], cwd: Path, env: dict[str, str]) -> str:
    result = subprocess.run(
        argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=90, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"{argv!r}: exit {result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout.strip()


def tree(repo: Path) -> dict[str, str]:
    return {
        str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in repo.rglob("*")
        if p.is_file() and ".git" not in p.relative_to(repo).parts
    }


class Journey:
    def __init__(
        self, source: Path, prefix: Path, output: Path, name: str, columns: int
    ):
        self.output = output / f"{name}-{columns}"
        self.output.mkdir()
        self.base = Path(tempfile.mkdtemp(prefix="hydra-ux.", dir="/tmp"))
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.hydra = prefix / "bin/hydra"
        self.env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(("HYDRA_", "GIT_", "TMUX"))
        }
        self.env.update(
            HOME=str(self.base / "home"),
            HYDRA_HOME=str(self.base / "state"),
            TMUX_TMPDIR=str(self.base / "tmux"),
            TMPDIR=str(self.base),
            HYDRA_NONINTERACTIVE="1",
            HYDRA_TEST_PUBLIC_ENTRY="1",
            LC_ALL="C",
            LANG="C",
            TERM="xterm-256color",
        )
        for key in ("HOME", "HYDRA_HOME", "TMUX_TMPDIR"):
            Path(self.env[key]).mkdir()
        self.proof: list[dict] = []
        self.failures: list[str] = []
        self.observer: Observer | None = None
        self.attached = False
        self.source, self.prefix, self.columns = source, prefix, columns

    def initialize(self) -> None:
        for args in (
            ["init", "-q"],
            ["config", "user.name", "Usability fixture"],
            ["config", "user.email", "fixture@example.invalid"],
        ):
            self.command("git", *args)
        (self.repo / "README.txt").write_text("Disposable usability fixture.\n")
        self.command("git", "add", "README.txt")
        self.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "fixture")

    def command(self, *argv: str) -> str:
        value = run(list(argv), self.repo, self.env)
        self.proof.append(
            {"command": argv, "output": value, "at_ns": time.monotonic_ns()}
        )
        return value

    def cli(self, *argv: str) -> str:
        return self.command(str(self.hydra), *argv)

    def open(self) -> None:
        self.observer = Observer(
            self.source / "build/test-tui-pty",
            self.prefix / "libexec/hydra/hydra-tui",
            self.hydra,
            self.output / "terminal",
            self.repo,
            self.env,
            self.columns,
            40 if self.columns > 80 else 24,
        )
        self.observer.wait("raw-ready", timeout=15)
        self.see("HYDRA /")

    def see(self, text: str) -> str:
        assert self.observer
        screen, receipt = self.observer.visible(text)
        self.proof.append(
            {"expect": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_all(self, *texts: str, timeout: float = 15) -> str:
        """Wait for one applied screen that shows every text at once."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"]
            and all(text in screen for text in texts),
            time.monotonic_ns() + int(timeout * 1e9),
            " and ".join(map(repr, texts)),
        )
        self.proof.append(
            {"expect_all": texts, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_prose(self, text: str, timeout: float = 15) -> str:
        """Wait for text that may be word-wrapped across rows of the screen."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"] and text in prose(screen),
            time.monotonic_ns() + int(timeout * 1e9),
            f"{text!r} (rows rejoined)",
        )
        self.proof.append(
            {"expect_prose": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def see_any(self, *texts: str, timeout: float = 15) -> str:
        """Wait for a screen showing at least one of several outcomes."""
        assert self.observer
        screen, receipt = self.observer._visible(
            lambda screen, record: record["parser_complete"] and any(t in screen for t in texts),
            time.monotonic_ns() + int(timeout * 1e9),
            " or ".join(map(repr, texts)),
        )
        self.proof.append({"expect_any": texts, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
        return screen

    def screen(self) -> str:
        assert self.observer
        screen, receipt = self.observer.snapshot()
        self.proof.append({"snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
        return screen

    def check(self, condition: bool, message: str) -> None:
        """An independent outcome check, recorded whether it passes or not."""
        self.proof.append(
            {"check": message, "passed": bool(condition), "at_ns": time.monotonic_ns()}
        )
        assert condition, message

    def expect(self, condition: bool, message: str) -> bool:
        """A desired-behaviour check that does not block the rest of the
        journey: it is recorded, and any failure still fails the journey."""
        self.proof.append(
            {"expect_check": message, "passed": bool(condition), "at_ns": time.monotonic_ns()}
        )
        if not condition:
            self.failures.append(message)
        return bool(condition)

    def expect_see(self, text: str, message: str, timeout: float = 10) -> bool:
        assert self.observer
        try:
            _, receipt = self.observer.visible(text, timeout)
            self.proof.append({"expect": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()})
            return self.expect(True, message)
        except TimeoutError:
            return self.expect(False, f"{message} (not shown: {text!r})")

    def until(self, predicate, timeout: float, message: str):
        """Poll independent evidence (files, Git, CLI) within a deadline."""
        deadline = time.monotonic() + timeout
        while True:
            value = predicate()
            if value or time.monotonic() >= deadline:
                break
            time.sleep(0.1)
        self.check(bool(value), message)
        return value

    def keys(self, value: bytes) -> None:
        assert self.observer
        self.observer.input(f"action-{len(self.proof)}", value)
        self.proof.append({"keys_hex": value.hex(), "at_ns": time.monotonic_ns()})

    def seed(self) -> None:
        self.cli("init", "--no-agent")
        self.cli("spawn", "sample", "--no-agent")

    def close(self) -> None:
        cleanup = {}
        try:
            if self.observer:
                cleanup = self.observer.close(attached=self.attached)
                if not cleanup.get("reaped") or cleanup.get("observer_exit"):
                    raise RuntimeError(f"UI cleanup incomplete: {cleanup}")
        finally:
            # This root owns the entire socket namespace; no global tmux cleanup.
            for socket in Path(self.env["TMUX_TMPDIR"]).glob("tmux-*/*"):
                subprocess.run(
                    ["tmux", "-S", str(socket), "kill-server"],
                    capture_output=True,
                    timeout=10,
                    check=False,
                )
            (self.output / "proof.json").write_text(
                json.dumps(self.proof, indent=2) + "\n"
            )
            (self.output / "cleanup.json").write_text(
                json.dumps(cleanup, indent=2) + "\n"
            )
            deadline = time.monotonic() + 10
            while True:
                try:
                    shutil.rmtree(self.base)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.2)


def clean_entry(j: Journey) -> None:
    before = tree(j.repo)
    status = j.command("git", "status", "--porcelain")
    j.open()
    j.see("n new task")
    assert tree(j.repo) == before, "Entering Hydra changed repository files"
    assert j.command("git", "status", "--porcelain") == status
    assert not j.cli("list", "--json").count('"branch"'), (
        "Entry created execution resources"
    )
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"first-task\r")
    j.see("Agent profile")
    j.keys(b"none\r")
    j.see("Objective")
    j.keys(b"\r")
    j.see("Typing goes to the agent")
    j.attached = True
    assert Path(j.cli("path", "first-task")).is_dir()
    j.keys(b"printf delivered > input-proof\r")
    marker = Path(j.cli("path", "first-task")) / "input-proof"
    deadline = time.monotonic() + 5
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert marker.read_text() == "delivered"
    marker.unlink()


def navigation(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for key, label in (
        (b"2", "[Details]"),
        (b"3", "[Overview]"),
        (b"4", "[Attention]"),
        (b"5", "[Recovery]"),
        (b"6", "[Workflows]"),
        (b"7", "[Statistics]"),
    ):
        j.keys(key)
        j.see(label)
        j.keys(b"\x1b")
        j.see("[Work]")
    j.keys(b"\t")
    j.see("[Details]")
    j.keys(b"\x1b[Z")
    j.see("[Work]")
    assert Path(j.cli("path", "sample")).is_dir()


def attachment(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for cycle in range(2):
        j.keys(b"a")
        j.see("Typing goes to the agent")
        j.attached = True
        # Verify input independently on disk, not by an echoed command marker.
        marker = f"input-{cycle}"
        j.keys(f"printf delivered > {marker}\r".encode())
        worktree = Path(j.cli("path", "sample"))
        deadline = time.monotonic() + 5
        while not (worktree / marker).exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert (worktree / marker).read_text() == "delivered"
        (worktree / marker).unlink()
        assert j.observer
        for width, height in (
            (80, 24),
            (140, 40),
            (j.columns, 40 if j.columns > 80 else 24),
        ):
            j.observer.resize(f"resize-{cycle}-{width}", width, height)
            j.see("Ctrl-B")
        j.keys(b"\x02\t")
        j.attached = False
        j.see("HYDRA /")
        j.keys(b"\x02x")
        j.keys(b"\x1b\x1b")
        j.see("[Work]")
        assert j.command("tmux", "list-panes", "-a", "-F", "#{pane_pid}")


def removal(j: Journey) -> None:
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    dirty = worktree / "keep.txt"
    dirty.write_text("must survive\n")
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("exit 1")
    assert dirty.read_text() == "must survive\n"
    j.keys(b"\x1b")
    dirty.unlink()
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("Removed sample")
    assert not worktree.exists(), "Removal did not remove the worktree"
    j.keys(b"\t")
    j.see("[Details]")
    assert j.command("git", "rev-parse", "--verify", "sample"), (
        "Removal deleted the branch"
    )


def planning(j: Journey) -> None:
    # Authoring is supplied through a public interactive profile, never by
    # fabricating Hydra state. The checker independently recompiles revisions.
    for file in (j.source / "tests/fixtures/plan/repo").iterdir():
        shutil.copy2(file, j.repo / file.name)
    j.command("git", "add", ".")
    j.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "planning inputs")
    # The guided local policy requires a plan free-space floor of at least 1 GiB.
    template = json.loads((j.source / "tests/fixtures/plan/plan.json").read_text())
    template["envelope"]["disk_mb"] = 1024
    (j.base / "plan-template.json").write_text(json.dumps(template))
    j.env["UX_PLAN_TEMPLATE"] = str(j.base / "plan-template.json")
    j.env["PATH"] = str(j.prefix / "bin") + os.pathsep + j.env["PATH"]
    j.cli(
        "agent",
        "init",
        "journey-provider",
        "--executable",
        str(j.source / "tests/fixtures/usability/provider.py"),
        "--prompt-mode",
        "task-file",
    )
    j.open()
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"conversation\r")
    j.see("Agent profile")
    j.keys(b"journey-provider\r")
    j.see("Objective")
    j.keys(b"Deliver a checked report\r")
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    j.see("PROPOSAL READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("PLAN OVERVIEW")
    j.keys(b"P")
    j.see_prose("No execution yet. y/N:")
    j.keys(b"y\r")
    assert "malformed" not in j.see("unvalidated")
    j.keys(b"V")
    j.see("awaiting approval")
    records = Path(j.env["HYDRA_HOME"]) / "state/v2/projects"
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "Validation executed the plan"
    )
    paths = (
        j.cli("workflow", "plan", "proposal", "conversation")
        .splitlines()[1]
        .split("\t")[1:]
    )
    compiled = j.output / "independent-initial.json"
    original = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    # Change the actual draft through the same conversation; old approval must
    # become unusable before any execution is permitted.
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True
    j.keys(b"revise\r")
    j.see("PROPOSAL REVISION READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("unvalidated")
    j.keys(b"E")
    j.see("Validate a local plan")
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "Changed draft reused old approval"
    )
    j.keys(b"V")
    j.see("awaiting approval")
    compiled = j.output / "independent-revised.json"
    revised = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    assert revised != original
    j.keys(b"E")
    j.see("INPUT TO HYDRA / execution approval")
    j.see(f"digest {revised[:12]}?")
    j.keys(b"y")
    deadline = time.monotonic() + 60
    states = []
    while time.monotonic() < deadline:
        states = list(records.glob("*/workflows/runs/*/state"))
        if states and states[0].read_text().strip() in (
            "succeeded",
            "failed",
            "cancelled",
        ):
            break
        time.sleep(0.1)
    assert len(states) == 1 and states[0].read_text().strip() == "succeeded", (
        "Approved plan did not deliver"
    )
    delivery = json.loads(j.cli("workflow", "plan", "result", states[0].parent.name))
    assert delivery["ok"], "Independent delivery verification failed"
    j.see("HYDRA /")


GREETING = "Hello from Hydra"


def greeting_plan(greeting: str, suffix: str = "") -> dict:
    """An implementation plan in the guided shape: a headless worker commits a
    change, the repository's own check runs on that worker head, and a
    separate verifier head checks the agent's summary with a plan asset."""
    worker, verifier = f"ux-worker{suffix}", f"ux-verifier{suffix}"
    prompt = "\n".join(
        [
            "Add a greeting to the project and commit it.",
            f"FIXTURE-FILE greeting.txt={greeting}\\n",
            f"FIXTURE-FILE lib/greet.sh=echo '{greeting}'\\n",
            "FIXTURE-COMMIT feat: add a greeting",
            "FIXTURE-SUMMARY Added greeting.txt and lib/greet.sh with the greeting.",
            "",
        ]
    )
    report = {"type": "object", "path": "check.json", "max_bytes": 2048}
    subject = {"step": "implement", "output": "summary"}

    def step(identifier, role, kind, needs, args, writes=()):
        return {
            "id": identifier,
            "role": role,
            "kind": kind,
            "needs": needs,
            "writes": list(writes),
            "args": args,
        }

    def check(identifier, definition, step_id):
        return {
            "id": identifier,
            "method": "executable",
            "definition": definition,
            "step": step_id,
            "input": "subject",
            "report": "check",
            "deliverable": "greeting",
        }

    return {
        "schema_version": 1,
        "id": "ux-greeting",
        "objective": "Add a committed greeting and verify it",
        "context": [],
        "assumptions": [],
        "questions": [],
        "envelope": {
            "hosts": ["local"],
            "tools": ["sh", "profile:codex"],
            "effects": ["worktree", "execute"],
            "writes": [f"{worker}:*"],
            "parallelism": 1,
            "timeout_seconds": 600,
            "artifact_bytes": 65536,
            "max_heads": 2,
            "disk_mb": 1024,
            "retry_budget": 0,
            "repair_budget": 0,
        },
        "steps": [
            step("spawn-worker", "work", "spawn", [],
                 {"branch": worker, "terminal_mode": "headless"}),
            step("implement", "compose", "exec", ["spawn-worker"],
                 {"head": worker, "profile": "codex", "prompt": prompt,
                  "result_file": "summary", "timeout": 120}, [f"{worker}:*"]),
            step("repo-check", "verify", "exec", ["implement"],
                 {"head": worker, "argv": ["sh", "checks/greeting.sh"], "timeout": 60}),
            step("spawn-verifier", "work", "spawn", ["implement"],
                 {"branch": verifier, "terminal_mode": "headless"}),
            step("verify-summary", "verify", "exec", ["implement", "spawn-verifier"],
                 {"head": verifier, "argv": ["sh", "@input/verify"], "timeout": 60}),
        ],
        "deliverables": [
            {"id": "greeting", "description": "Greeting change summary",
             "step": "implement", "output": "summary", "destination": "run-artifact"}
        ],
        "checks": [
            check("repository", "Run the repository greeting check on the worker branch",
                  "repo-check"),
            check("summary", "Confirm the summary names both changed files",
                  "verify-summary"),
        ],
        "requirements": [
            {"id": "greeting-committed", "criterion": "The greeting is committed and printed",
             "deliverable": "greeting", "check": "repository"},
            {"id": "summary-names-files", "criterion": "The summary names both changed files",
             "deliverable": "greeting", "check": "summary"},
        ],
        "data": {
            "schema_version": 1,
            "inputs": {"verify": {"asset": "verify", "type": "file", "max_bytes": 4096}},
            "steps": {
                "implement": {"outputs": {"summary": {
                    "type": "file", "path": "summary", "max_bytes": 4096}}},
                "repo-check": {"inputs": {"subject": subject}, "outputs": {"check": report}},
                "verify-summary": {
                    "inputs": {"subject": subject, "verify": {"input": "verify"}},
                    "outputs": {"check": report},
                },
            },
        },
    }


def provider_fixture(j: Journey, greeting: str, fixed: str | None = None) -> None:
    """Install the labelled fixture as `codex` on this journey's private PATH.
    Hydra reaches it only through the built-in profile's public launch and
    headless contracts; the repository carries its own greeting check."""
    shutil.copytree(j.source / "tests/fixtures/usability/greeting-repo", j.repo,
                    dirs_exist_ok=True)
    j.command("git", "add", ".")
    j.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "greeting check")
    tools = j.base / "fixture-bin"
    tools.mkdir()
    (tools / "codex").symlink_to(j.source / "tests/fixtures/usability/provider.py")
    (j.base / "plan-template.json").write_text(json.dumps(greeting_plan(greeting)))
    j.env["UX_PLAN_TEMPLATE"] = str(j.base / "plan-template.json")
    if fixed:
        (j.base / "plan-fixed.json").write_text(json.dumps(greeting_plan(fixed, "-fix")))
        j.env["UX_PLAN_FIXED"] = str(j.base / "plan-fixed.json")
    j.env["UX_PLAN_ASSETS"] = "verify=" + str(
        j.source / "tests/fixtures/usability/verify-summary.sh")
    j.env["UX_NOTICES"] = str(j.base / "notices.txt")
    j.env["PATH"] = os.pathsep.join([str(tools), str(j.prefix / "bin"), j.env["PATH"]])


def runs(j: Journey) -> dict[str, Path]:
    records = Path(j.env["HYDRA_HOME"]) / "state/v2/projects"
    return {p.name: p for p in records.glob("*/workflows/runs/*") if (p / "state").exists()}


def finished_run(j: Journey, before: set[str], timeout: float = 90) -> Path:
    def done():
        new = [p for name, p in runs(j).items() if name not in before]
        if len(new) == 1 and (new[0] / "state").read_text().strip() in (
                "succeeded", "failed", "cancelled", "recovery-required"):
            return new[0]
        return None
    return j.until(done, timeout, "Approved plan reached a terminal run state")


def independent_digest(j: Journey, head: str, name: str) -> str:
    draft, policy = j.cli("workflow", "plan", "proposal", head).splitlines()[1].split("\t")[1:3]
    assets = Path(draft).parent / "assets"
    extra = ["--assets-dir", str(assets)] if assets.is_dir() else []
    compiled = j.output / f"independent-{name}.json"
    return json.loads(j.cli("workflow", "plan", "compile", draft, policy, str(compiled),
                            *extra))["data"]["sha256"]


def converse_and_validate(j: Journey, task: str) -> None:
    """Start a planning conversation in-app, publish, review with the guided
    policy and validate: no JSON, file paths or CLI from the user."""
    j.keys(b"n")
    j.see("Task name:")
    j.keys(task.encode() + b"\r")
    j.see("Agent profile")
    j.keys(b"codex\r")
    j.see("Objective")
    j.keys(b"Add a committed greeting and verify it\r")
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    j.see("PROPOSAL READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("PLAN OVERVIEW")
    j.keys(b"P")
    j.see("Review with local policy")
    j.keys(b"y\r")
    j.see("unvalidated")
    j.keys(b"V")
    j.see("awaiting approval")


def execute(j: Journey, head: str, name: str) -> Path:
    digest = independent_digest(j, head, name)
    before = set(runs(j))
    j.keys(b"E")
    j.see_all("INPUT TO HYDRA / execution approval", f"digest {digest[:12]}?")
    j.keys(b"y")
    return finished_run(j, before)


def worker_diff(j: Journey, branch: str) -> tuple[list[str], str]:
    base = j.command("git", "merge-base", "HEAD", branch)
    files = j.command("git", "diff", "--name-only", base, branch).splitlines()
    return files, j.command("git", "diff", base, branch)


def listed_heads(j: Journey) -> set[str]:
    return {row.split()[0] for row in j.cli("list").splitlines()[1:] if row.split()}


def result_review(j: Journey) -> None:
    provider_fixture(j, GREETING)
    j.open()
    converse_and_validate(j, "greeting")
    run = execute(j, "greeting", "approved")
    j.check((run / "state").read_text().strip() == "succeeded", "The approved run succeeded")
    delivery = json.loads(j.cli("workflow", "plan", "result", run.name))
    j.check(delivery["ok"], "Independent delivery verification passed")
    files, diff = worker_diff(j, "ux-worker")
    j.check(sorted(files) == ["greeting.txt", "lib/greet.sh"],
            "The worker committed exactly the two planned files")
    j.check(f"+{GREETING}" in diff, "The worker diff adds the greeting")
    j.check(not j.command("git", "-C", j.cli("path", "ux-worker"), "status", "--porcelain"),
            "The worker worktree is clean: the change is committed, not pending")
    j.until(lambda: "ux-verifier" not in listed_heads(j), 30, "The verifier head was retired")
    j.check("ux-worker" in listed_heads(j), "The worker head stays for review")
    j.check(bool(j.command("git", "rev-parse", "--verify", "ux-verifier")),
            "Retiring the verifier kept its branch")

    # Overview's run panel: the finished run, its worker and what to do next.
    j.keys(b"\x1b\x1b3")
    j.see_all("RUN / plan run ux-greeting", "succeeded")
    j.see_all("Worker branch ux-worker holds the result", "hydra land ux-worker")
    j.see("dismiss")
    j.see("(retired)")
    # Work groups the run's heads under the planning conversation that launched it.
    j.keys(b"1")
    j.see_all("Heads in this project", "plan run ux-greeting")
    screen = j.screen()
    j.check("ux-worker" not in screen.split("plan run ux-greeting")[0],
            "The worker head is not listed as a separate top-level head")
    j.keys(b"j\r")
    j.see_all("worker", "ux-worker")
    j.see("retired")
    # U15: a clean worktree holding a two-file commit must not read as no change.
    screen = j.screen()
    worker_rows = [line for line in screen.splitlines() if "ux-worker" in line]
    j.check(not any(" 0 files" in row or "0 changed" in row for row in worker_rows),
            "The worker's committed two-file change is not summarised as zero files")
    j.keys(b"j\r")
    details = j.see("Details: ux-worker")
    j.expect("x  dismiss" in details or "x remove" in details,
             "Worker details offer removing the worker head (dismissing the result)")
    j.check("0 changed files" not in details and "not yet committed" in details,
            "Worker details label uncommitted changes separately from the task's diff")
    # U15: the task's full diff is one action away and shows the committed files.
    j.keys(b":")
    j.see("INPUT TO HYDRA")
    j.keys(b"diff\r")
    j.see_all("+++ b/greeting.txt", "+++ b/lib/greet.sh", f"+{GREETING}")
    j.see("Press Enter to return to Hydra")
    j.keys(b"\r")
    j.see("HYDRA /")

    # Attention: the result, then its review.
    j.keys(b"I")
    j.see_all("Result ready for review", "ux-greeting")
    j.keys(b"r")
    j.see("verdict PASS")
    screen = review_section(j, "WHAT WAS CHECKED")
    for requirement in ("greeting-committed", "summary-names-files"):
        rows = [line for line in screen.splitlines() if requirement in line and "PASS" in line]
        j.expect(bool(rows), f"Requirement {requirement} shows PASS")
    screen = review_section(j, "HOW IT WAS VERIFIED")
    j.expect("Ran: sh checks/greeting.sh" in screen, "The review shows the verify command")
    screen = review_section(j, "CHANGES ON ux-worker")
    j.expect("(1 commit, 2 files," in screen,
             "The review counts the worker branch's one commit and two files")
    j.expect("0 changed files" not in screen, "The review does not claim zero changed files")
    for name in ("greeting.txt", "lib/greet.sh"):
        j.expect(re.search(rf"file\s+\+1\s+-0\s+{re.escape(name)}", screen) is not None,
                 f"The review lists {name} with its added and removed lines")
    screen = review_section(j, "DIFF  (complete)")
    j.expect("+++ b/greeting.txt" in screen and f"+{GREETING}" in screen,
             "The review shows the diff lines of the committed change")


def review_section(j: Journey, heading: str, presses: int = 16) -> str:
    """Move through review sections with n until heading is at the top, or
    as high as the end of the review allows."""
    screen = j.screen()
    for _ in range(presses):
        rows = screen.splitlines()
        if any(heading in row for row in rows[3:7]):
            return screen
        j.keys(b"n")
        time.sleep(0.3)
        following = j.screen()
        if heading in screen and heading not in following:
            # The review ended before the heading reached the top: step back.
            for _ in range(presses):
                j.keys(b"N")
                time.sleep(0.3)
                if heading in j.screen():
                    break
            return j.see(heading)
        screen = following
    return j.see(heading)


def recovery(j: Journey) -> None:
    provider_fixture(j, "Hello from Hydar", fixed=GREETING)
    j.open()
    converse_and_validate(j, "greeting")
    run = execute(j, "greeting", "first")
    j.check((run / "state").read_text().strip() == "failed", "The first run failed its check")
    j.check((run / "steps/repo-check/state").read_text().strip() == "failed",
            "The repository check step is the one that failed")
    notices = Path(j.env["UX_NOTICES"])
    j.until(lambda: notices.exists() and "repo-check failed" in notices.read_text(), 30,
            "The planning agent was told which step failed")
    j.until(lambda: "finished: failed" in notices.read_text(), 30,
            "The planning agent was told the run failed")

    # Plain-language failure in the run panel and in Attention.
    j.keys(b"\x1b\x1b3")
    j.see_all("RUN / plan run ux-greeting", "failed")
    j.expect_see("Step repo-check failed on ux-worker",
                 "The run panel names the failed step and its head")
    j.keys(b"I")
    # Wait for the loaded list (an item Attention offers for this run).
    screen = j.see_all("[Attention]", "ux-greeting")
    rows = [line.lower() for line in screen.splitlines() if "repo-check" in line]
    j.expect(any("fail" in row for row in rows),
             "Attention lists the failed repository check in plain language")
    # Review whichever item Attention offers for this run; the result is the run's.
    j.keys(b"r")
    screen = j.see("RESULT  ux-greeting")
    j.expect("verdict FAILED" in screen, "The review states the run's verdict as failed")
    screen = review_section(j, "WHAT WAS CHECKED")
    rows = [line for line in screen.splitlines() if "greeting-committed" in line]
    j.expect(any("FAIL" in row for row in rows),
             "The review marks the requirement whose check failed as FAIL")
    screen = review_section(j, "HOW IT WAS VERIFIED")
    j.expect("Ran: sh checks/greeting.sh" in screen, "The review shows the failed check's command")
    j.expect('committed greeting.txt says "Hello from Hydar"' in prose(screen),
             "The review summarises the failing log line")
    j.keys(b"\x1b\x1b")

    # Supported recovery: the agent already has the failure; ask it for a fix,
    # validate the new revision and execute it with one key.
    j.keys(b"1")
    j.see("greeting")
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True
    j.see("FIXTURE RECEIVED HYDRA NOTICE")
    j.keys(b"fix\r")
    j.see("PROPOSAL FIX READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("unvalidated")
    j.keys(b"V")
    j.see("awaiting approval")
    second = execute(j, "greeting", "fixed")
    j.check((second / "state").read_text().strip() == "succeeded",
            "The corrected revision succeeded")
    j.check(json.loads(j.cli("workflow", "plan", "result", second.name))["ok"],
            "Independent delivery verification passed after recovery")
    files, diff = worker_diff(j, "ux-worker-fix")
    j.check(f"+{GREETING}" in diff and sorted(files) == ["greeting.txt", "lib/greet.sh"],
            "The recovered worker branch holds the corrected change")
    j.check((run / "state").read_text().strip() == "failed",
            "The failed run's record is kept, not rewritten")


LOSS_WORDS = ("lost", "failed", "crashed", "deleted", "corrupt")
INTERNAL_WORDS = ("dead-session", "try-hydra", "hydra doctor")


def recovery_terminal(j: Journey) -> None:
    # U7: an interactive head whose terminal is gone keeps its work; the view
    # must say what stopped without inferring failure or loss, and offer a way
    # back to the work.
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    (worktree / "committed.txt").write_text("committed work\n")
    j.command("git", "-C", str(worktree), "add", "committed.txt")
    j.command("git", "-C", str(worktree), "-c", "commit.gpgSign=false", "commit", "-qm", "work")
    (worktree / "draft.txt").write_text("uncommitted work\n")
    sessions = j.command("tmux", "list-sessions", "-F", "#{session_name}").splitlines()
    j.check(len(sessions) == 1, "The head has exactly one terminal session")
    j.command("tmux", "kill-session", "-t", sessions[0])
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"\t")
    details = j.see_all("Details: sample", "NEXT")
    j.expect(not any(word in details.lower() for word in LOSS_WORDS),
             "Details do not infer failure or lost work from a missing terminal")
    j.expect("kept" in details, "Details say the head's files are kept")
    j.expect(not any(word in details for word in INTERNAL_WORDS),
             "Details explain the stopped terminal without internal status names")
    j.keys(b"5")
    j.see_all("[Recovery]", "Terminal stopped: sample")
    # The finding's plain-language account; raw kinds stay in its diagnostics.
    j.keys(b"d")
    account = j.see("The terminal session for sample is no longer running")
    explanation = prose(account).split("Check:")[0]
    j.expect("files are kept" in explanation, "Recovery says the worktree and files are kept")
    j.expect(not any(word in explanation.lower() for word in LOSS_WORDS),
             "Recovery does not infer failure or lost work from a missing terminal")
    j.expect("Open it from Work" in explanation, "Recovery offers a way back to the work")
    j.keys(b"d")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Uncommitted work is untouched")
    # The offered action: open the head from Work, which restores a terminal.
    j.keys(b"\x1b1")
    j.see("[Work]")
    j.keys(b"a")
    # Either an attached pane or its disconnection notice: both are outcomes
    # of the offered action, judged below by whether a terminal came back.
    screen = j.see_any("Typing goes to the agent", "CLIENT DISCONNECTED")
    j.attached = True
    j.expect("CLIENT DISCONNECTED" not in screen, "Opening the head attaches a live terminal")
    deadline = time.monotonic() + 15
    while not sessions_of(j) and time.monotonic() < deadline:
        time.sleep(0.2)
    if j.expect(bool(sessions_of(j)), "Opening the head from Work restored its terminal"):
        marker = worktree / "restored-proof"
        j.keys(b"printf restored > restored-proof\r")
        deadline = time.monotonic() + 10
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        j.expect(marker.exists(), "Input reaches the restored terminal in the head's worktree")
    j.check((worktree / "draft.txt").read_text() == "uncommitted work\n",
            "Opening the head kept uncommitted work")
    j.check("committed.txt" in j.command("git", "-C", str(worktree), "ls-files"),
            "Opening the head kept committed work")


def prose(screen: str) -> str:
    """Screen text with frame borders removed and word-wrapped lines rejoined."""
    lines = [line.strip().strip("|│").strip() for line in screen.splitlines()]
    return " ".join(" ".join(lines).split())


def sessions_of(j: Journey) -> list[str]:
    """Terminal sessions on this journey's private tmux server (none is not an error)."""
    listing = subprocess.run(["tmux", "list-sessions", "-F", "#{session_name}"],
                             env=j.env, capture_output=True, text=True, timeout=10, check=False)
    return listing.stdout.split() if listing.returncode == 0 else []


JOURNEYS = {
    "clean-entry": clean_entry,
    "navigation": navigation,
    "attachment": attachment,
    "removal": removal,
    "planning": planning,
    "result-review": result_review,
    "recovery": recovery,
    "recovery-terminal": recovery_terminal,
}


PURPOSE = {
    "clean-entry": "Open a clean repository and start a task from visible controls",
    "navigation": "Reach every view with number keys, Tab/Shift-Tab and Esc",
    "attachment": "Attach, type to the agent, resize and return without stopping it",
    "removal": "Remove a head in-app with dirty-work protection",
    "planning": "Converse, revise, validate and approve a plan; checked delivery",
    "result-review": "Find a finished run in Overview, review its verdict, diff, "
    "requirements and verify command; verifier retired, worker grouped with land/dismiss",
    "recovery": "A failed repository check is explained in the run panel, Attention and "
    "review; the planning agent's corrected revision is validated, executed with y and succeeds",
    "recovery-terminal": "An interactive head's missing terminal is explained without "
    "inferring failure or loss, and the offered action restores access to the work",
}


def report(output: Path, result: dict) -> None:
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    rows = []
    for case in result["journeys"]:
        captures = []
        for path in sorted((output / case["id"]).glob("terminal/snapshot-*.txt")):
            html_path = path.with_suffix(".html")
            html_path.write_text(
                '<meta charset="utf-8"><pre>' + html.escape(path.read_text()) + "</pre>"
            )
            captures.append(
                f'<a href="{html.escape(str(html_path.relative_to(output)))}">{path.stem}</a>'
            )
        purpose = PURPOSE.get(case["id"].rsplit("-", 1)[0], "")
        reasons = "".join(
            f"<li>{html.escape(reason)}</li>" for reason in case.get("reason", "").split("; ") if reason
        )
        rows.append(
            f"<h2>{html.escape(case['id'])}: {case['status']}</h2><p>{html.escape(purpose)}</p>"
            + (f"<ul>{reasons}</ul>" if reasons else "")
            + f'<a href="{case["id"]}/proof.json">Actions and independent checks</a> · '
            + f'<a href="{case["id"]}/terminal/terminal.raw">Terminal bytes</a><p>'
            + " · ".join(captures)
            + "</p>"
        )
    (output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Hydra usability</title>'
        "<style>body{font:16px system-ui;max-width:1100px;margin:32px auto}pre{font:14px monospace}</style>"
        "<h1>Installed Hydra usability journeys</h1><p>Deterministic local fixtures: a shell "
        "fixture and a labelled provider fixture reached through the built-in codex profile's public "
        "launch and headless contracts; it never claims live-model coverage. "
        "Failed journeys list every failed desired-behaviour check; a journey stops at the first "
        "check that blocks the rest of its flow. "
        "Captures are escaped text, not a rendering of terminal colors. Raw bytes are retained. "
        "This checks product interaction, not real-provider behavior or novice discoverability.</p>"
        + "".join(rows)
        + "<h2>Separate acceptance still required</h2><p>"
        + html.escape("; ".join(result["unqualified"]))
        + "</p>"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--journey",
        choices=tuple(JOURNEYS),
    )
    parser.add_argument(
        "--columns", type=int, choices=(80, 140), nargs="+", default=[80, 140]
    )
    args = parser.parse_args()
    if not __debug__:
        parser.error(
            "Run without Python optimization so outcome assertions remain enabled"
        )
    source = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = output / "installed"
    result = {
        "mode": "installed deterministic shell and provider fixtures",
        "journeys": [],
        "unqualified": [
            "Live authenticated provider",
            "Real terminal visual review",
            "Screen-only discoverability trial",
            "Authorized remote host",
        ],
        "commit": run(["git", "rev-parse", "HEAD"], source, dict(os.environ)),
    }
    try:
        install_env = {
            **os.environ,
            "PREFIX": str(prefix),
            "HYDRA_INSTALL_CORE": "required",
            "HYDRA_INSTALL_TUI": "required",
            "HYDRA_INSTALL_FLEET": "required",
            "DESTDIR": "",
        }
        (output / "install.log").write_text(
            run(["sh", "install.sh"], source, install_env)
        )
        result["installed_hashes"] = tree(prefix)
        (output / "source.diff").write_text(
            run(["git", "diff", "HEAD"], source, dict(os.environ))
        )
        result["source_files"] = {
            str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in ("src", "lib", "scripts", "tests/fixtures/usability")
            for p in (source / root).rglob("*")
            if p.is_file()
        }
        for name, function in JOURNEYS.items():
            if args.journey and args.journey != name:
                continue
            for columns in args.columns:
                case = {"id": f"{name}-{columns}", "status": "passed"}
                started = time.monotonic()
                j = None
                try:
                    j = Journey(source, prefix, output, name, columns)
                    j.initialize()
                    function(j)
                    if j.failures:
                        raise AssertionError("; ".join(j.failures))
                except (
                    AssertionError,
                    OSError,
                    RuntimeError,
                    TimeoutError,
                    subprocess.SubprocessError,
                ) as error:
                    reason = str(error)
                    if j and j.failures and reason != "; ".join(j.failures):
                        reason = "; ".join([*j.failures, reason])
                    case.update(status="failed" if j else "blocked", reason=reason)
                finally:
                    if j:
                        try:
                            j.close()
                        except (
                            OSError,
                            RuntimeError,
                            subprocess.SubprocessError,
                        ) as error:
                            case.update(
                                status="failed",
                                reason=f"{case.get('reason', '')} Cleanup: {error}",
                            )
                case["seconds"] = time.monotonic() - started
                result["journeys"].append(case)
                report(output, result)
                print(
                    f"{case['status'].upper()} {case['id']}: {case.get('reason', '')}",
                    flush=True,
                )
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["infrastructure_error"] = str(error)
    report(output, result)
    print(output / "index.html")
    if result.get("infrastructure_error"):
        return 2
    return int(any(case["status"] != "passed" for case in result["journeys"]))


if __name__ == "__main__":
    raise SystemExit(main())
