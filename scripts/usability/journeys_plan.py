"""Planning journeys: conversation to proposal, guided policy, validation,
exact-digest approval and checked delivery through the provider fixture."""

from __future__ import annotations

import json
import os
import shutil
import time
from pathlib import Path

from usability.journey import Journey


def planning_prepare(j: Journey) -> None:
    """Planning inputs and the interactive provider fixture profile. Authoring
    is supplied through a public interactive profile, never by fabricating
    Hydra state; the checker independently recompiles revisions."""
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


def planning_converse(j: Journey, task: str) -> None:
    """Open the control centre, converse to a proposal, review it with the
    guided policy and validate it to "awaiting approval"; nothing executes."""
    j.open()
    j.keys(b"n")
    j.see("Task name:")
    j.keys(task.encode() + b"\r")
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


def planning(j: Journey) -> None:
    planning_prepare(j)
    planning_converse(j, "conversation")
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


