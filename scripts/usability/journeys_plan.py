"""Planning journeys: conversation to a published proposal on the Plan page,
one approval of the exact revision, and checked delivery through the
provider fixture."""

from __future__ import annotations

import json
import os
import re
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


def footer(screen: str) -> str:
    rows = screen.rstrip("\n").splitlines()
    return rows[-1] if rows else ""


def plan_page(j: Journey, *texts: str) -> str:
    """Show the selected head's Plan page with the keys on it: Ctrl-\\ B
    selects the page; where it is the side pane beside the list, Shift-Tab
    gives it the focus. Waits for every text."""
    for _ in range(3):
        j.keys(b"\x1cB")
        time.sleep(0.6)
        if "E approve" not in footer(j.screen()):
            j.keys(b"\x1b[Z")
            time.sleep(0.6)
        try:
            return j.see_all("E approve", *texts, timeout=4)
        except TimeoutError:
            continue
    return j.see_all("E approve", *texts)


def talk(j: Journey, request: str, reply: str) -> None:
    """Type a request into the planning conversation and leave input once
    the agent answered."""
    j.attach_agent()
    j.keys(request.encode() + b"\r")
    j.see(reply)
    j.leave_agent()


def planning_converse(j: Journey, task: str, agent: str = "journey-provider",
                      objective: str = "Deliver a checked report") -> None:
    """Open the control centre, start a planning conversation from the
    new-task form and converse to a published proposal, which the Plan page
    shows as ready without any key; nothing executes."""
    j.open()
    j.new_task(task, agent, objective)
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    j.see("PROPOSAL READY")
    j.leave_agent()
    plan_page(j, "Plan ready")


def plan_state(j: Journey, revision: int) -> str:
    """Wait for the Plan page's state line to show the revision (not the
    agent's "revision 2" in the conversation beside it)."""
    assert j.observer
    state = re.compile(rf"revision {revision}\W+(not validated yet|awaiting you)")
    screen, _ = j.observer._visible(lambda screen, record: record["parser_complete"] and state.search(screen) is not None,
                                    time.monotonic_ns() + int(15e9), f"the Plan page at revision {revision}")
    return screen


def approval(j: Journey, revision: int) -> str:
    """E on the Plan page: one dialog naming the exact revision it runs. A
    later revision is approved once the page shows it."""
    if revision > 1:
        plan_state(j, revision)
    j.keys(b"E")
    return j.see_prose(f"revision {revision}?")


def planning(j: Journey) -> None:
    planning_prepare(j)
    planning_converse(j, "conversation")
    records = Path(j.env["HYDRA_HOME"]) / "state/v2/projects"
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "Publishing a proposal executed the plan"
    )
    # The approval dialog names revision 1 (E has read it under the guided
    # policy, so it can be compiled independently); cancelling it runs nothing.
    approval(j, 1)
    paths = (
        j.cli("workflow", "plan", "proposal", "conversation")
        .splitlines()[1]
        .split("\t")[1:]
    )
    compiled = j.output / "independent-initial.json"
    original = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    j.escape()
    j.see_all("DRAFT", "revision 1")
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "A cancelled approval executed the plan"
    )
    # Change the actual draft through the same conversation; the approval
    # that follows names and runs the new revision only.
    talk(j, "revise", "PROPOSAL REVISION READY")
    plan_page(j)
    approval(j, 2)
    compiled = j.output / "independent-revised.json"
    revised = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    assert revised != original
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
    launched = {d.name for d in records.glob("*/workflows/launches/*") if d.is_dir()}
    assert launched == {revised}, f"Approval ran {sorted(launched)}, not the revised digest {revised}"
    delivery = json.loads(j.cli("workflow", "plan", "result", states[0].parent.name))
    assert delivery["ok"], "Independent delivery verification failed"
    j.see_all("RUN RECORDED", "succeeded")


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


def converse_to_proposal(j: Journey, task: str) -> None:
    """Start a planning conversation in-app and publish a proposal: no JSON,
    file paths or CLI from the user."""
    j.new_task(task, "codex", "Add a committed greeting and verify it")
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    j.see("PROPOSAL READY")
    j.leave_agent()
    plan_page(j, "Plan ready")


def launched_digests(j: Journey) -> set[str]:
    """Digests the approvals launched: one launch record per accepted digest."""
    records = Path(j.env["HYDRA_HOME"]) / "state/v2/projects"
    return {d.name for d in records.glob("*/workflows/launches/*") if d.is_dir()}


def execute(j: Journey, head: str, name: str, revision: int) -> Path:
    """Approve the Plan page's revision with E and y; the launch must be of
    the digest an independent compile of that revision gives (compiled while
    the dialog is open: E has read the revision under the guided policy)."""
    before, launched = set(runs(j)), launched_digests(j)
    approval(j, revision)
    digest = independent_digest(j, head, name)
    j.keys(b"y")
    run = finished_run(j, before)
    j.check(launched_digests(j) - launched == {digest},
            f"The approval launched exactly the revision's digest {digest[:12]}")
    return run


def worker_diff(j: Journey, branch: str) -> tuple[list[str], str]:
    base = j.command("git", "merge-base", "HEAD", branch)
    files = j.command("git", "diff", "--name-only", base, branch).splitlines()
    return files, j.command("git", "diff", base, branch)


def listed_heads(j: Journey) -> set[str]:
    return {row.split()[0] for row in j.cli("list").splitlines()[1:] if row.split()}


