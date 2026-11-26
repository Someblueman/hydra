"""The first-use and planning walkthrough: a new task from the new-task form of
a repository nobody has initialized, a conversation to a published proposal on
the Plan page, one approval of the exact revision, and checked delivery
through the provider fixture."""

from __future__ import annotations

import json
import os
import re
import shutil
import time
from pathlib import Path

from usability.journey import Journey, tree


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
    shows as ready without any key; nothing executes. Entering the control
    centre only observes: the repository's files and git status are as they
    were and no head exists until the task is requested."""
    before, status = tree(j.repo), j.command("git", "status", "--porcelain")
    j.open()
    j.see("n new task")
    j.check(tree(j.repo) == before and j.command("git", "status", "--porcelain") == status,
            "Entering Hydra leaves the repository's files and git status unchanged")
    j.check('"branch"' not in j.cli("list", "--json"), "Entering Hydra creates no execution resources")
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

