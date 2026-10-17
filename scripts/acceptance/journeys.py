"""U17 (conversation to plan without JSON) and U15.a (a committed change is
shown as the task's diff, never as "0 changed files").

The steps reuse the usability planning journeys by reference: the same
labelled provider fixture installed as `codex`, the same greeting plan, and
the same independent recompilation of each revision's digest. Checks here
are tagged with the acceptance bullet they establish."""

from __future__ import annotations

import json
import re
import time
from pathlib import Path

from acceptance.harness import Case, case, heads, pane_text
from usability.journeys_plan import (
    GREETING,
    finished_run,
    independent_digest,
    provider_fixture,
    runs,
    worker_diff,
)

REVISION = re.compile(r"Revision (\d+) / ([A-Z ]+)/ ([a-zA-Z ]+)")


def plan_header(screen: str) -> tuple[int, str] | None:
    match = REVISION.search(screen)
    return (int(match.group(1)), match.group(0)) if match else None


def hydra_commands(j: Case) -> list[tuple[str, ...]]:
    return [tuple(c["command"][1:]) for c in j.proof if "command" in c and c["command"][0] == str(j.hydra)]


def converse(j: Case, task: str) -> str:
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
    screen = j.see("PROPOSAL READY")
    j.keys(b"\x02\t")
    j.attached = False
    return screen


@case("u17-plan-from-conversation", ("U17.a", "U17.b", "U17.c", "U15.a"), seconds=240)
def plan_from_conversation(j: Case) -> None:
    provider_fixture(j, GREETING)
    j.open()
    j.keys(b"3")
    j.see("[Overview]")
    converse(j, "greeting")
    user_commands = len(hydra_commands(j))
    # The proposal is the agent's; nothing is validated or executed yet.
    j.keys(b"B")
    screen = j.see("PLAN OVERVIEW")
    j.record("U17.c", "before review the plan pane explains that a proposal runs nothing until validated and approved",
             "runs nothing until V validates it" in pane_text(screen, "PLAN REVIEW"))
    j.record("U17.c", "publishing a proposal records no workflow run",
             not runs(j), detail=f"runs: {sorted(runs(j))}")
    j.keys(b"P")
    screen = j.see("Review with local policy")
    j.record("U17.a", "the guided policy review is offered in-app before validation (P, y/N), no policy file to locate",
             "y/N" in screen and ".json" not in screen.split("Review with local policy")[1][:400])
    j.keys(b"y\r")
    screen = j.see("unvalidated")
    header = plan_header(screen)
    j.record("U17.c", "the loaded proposal is labelled DRAFT / unvalidated, not a validated workflow",
             header is not None and "DRAFT / unvalidated" in header[1], detail=str(header))
    proposal = j.cli("workflow", "plan", "proposal", "greeting")
    j.record("U17.a", "Hydra associated the agent's draft and the chosen policy with the task head itself",
             any(line.startswith("P\t") and "/planning/draft.json" in line and "/planning/policy.json" in line
                 for line in proposal.splitlines()),
             evidence=[j.keep("plan-proposal.txt", proposal)])
    j.keys(b"V")
    screen = j.see("awaiting approval")
    first = plan_header(screen)
    first_digest = independent_digest(j, "greeting", "initial")
    j.record("U17.b", "after validation the plan pane names the exact revision and its READY / awaiting approval state",
             first is not None and first[0] >= 1 and "READY / awaiting approval" in first[1], detail=str(first))
    j.keys(b"E")
    approval = j.see_all("INPUT TO HYDRA / execution approval", f"digest {first_digest[:12]}?")
    j.record("U17.b", "the approval prompt shows the revision number and the digest an independent compile reproduces",
             f"revision {first[0]}," in approval and f"digest {first_digest[:12]}?" in approval
             and "y executes exactly this validated revision" in approval,
             evidence=[j.keep("approval-prompt-initial.txt", approval)])
    j.keys(b"\x1b")
    deadline = time.monotonic() + 3
    while "execution approval" in j.screen() and time.monotonic() < deadline:
        time.sleep(0.2)
    closed = "execution approval" not in j.screen()
    j.record("U17.b", "Esc cancels the approval prompt as its 'n / Esc cancel' line says, and nothing runs",
             closed and not runs(j), detail="" if closed else "the prompt stayed open after Esc; n closes it")
    if not closed:
        j.keys(b"n")
        j.until(lambda: "execution approval" not in j.screen(), 5, "n closes the approval prompt")
    j.see("PLAN OVERVIEW")
    # Revise the plan through the same conversation.
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True
    j.keys(b"revise\r")
    j.see("PROPOSAL REVISION READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    screen = j.see("unvalidated")
    second = plan_header(screen)
    j.record("U17.b", "the revised proposal is a new revision shown as DRAFT / unvalidated again",
             second is not None and first is not None and second[0] > first[0] and "unvalidated" in second[1],
             detail=f"{first} -> {second}")
    j.keys(b"E")
    notice = j.see("Validate a local plan")
    j.record("U17.b", "E on the changed plan asks for validation instead of reusing the old approval; no run started",
             "Validate a local plan with V" in notice and not runs(j))
    j.keys(b"V")
    screen = j.see("awaiting approval")
    revised_digest = independent_digest(j, "greeting", "revised")
    j.record("U17.b", "the revised revision validates to a different digest",
             revised_digest != first_digest, detail=f"{first_digest[:12]} -> {revised_digest[:12]}")
    j.record("U17.a", "creating and revising the plan needed no JSON, file path or hydra command from the user",
             all(c[:3] in (("workflow", "plan", "proposal"), ("workflow", "plan", "compile"))
                 for c in hydra_commands(j)[user_commands:]),
             detail="commands after the conversation started: "
                    + "; ".join(" ".join(c[:3]) for c in hydra_commands(j)[user_commands:]))
    before = set(runs(j))
    j.keys(b"E")
    approval = j.see_all("INPUT TO HYDRA / execution approval", f"digest {revised_digest[:12]}?")
    j.keep("approval-prompt-revised.txt", approval)
    j.keys(b"y")
    run = finished_run(j, before)
    delivery = json.loads(j.cli("workflow", "plan", "result", run.name))
    j.record("U17.a", "approving the exact revised revision executes it and the delivery verifies",
             (run / "state").read_text().strip() == "succeeded" and delivery["ok"],
             detail=f"run {run.name} state {(run / 'state').read_text().strip()}",
             evidence=[j.keep_json("plan-result", delivery)])
    j.see("HYDRA /")
    committed_change(j)


def committed_change(j: Case) -> None:
    """U15.a: the worker committed two files and its worktree is clean."""
    files, diff = worker_diff(j, "ux-worker")
    clean = not j.command("git", "-C", j.cli("path", "ux-worker"), "status", "--porcelain")
    j.record("U15.a", "the worker branch holds a committed two-file change and its worktree is clean",
             sorted(files) == ["greeting.txt", "lib/greet.sh"] and f"+{GREETING}" in diff and clean,
             detail=f"files {files}", evidence=[j.keep("worker-diff.txt", diff)])
    j.until(lambda: "ux-verifier" not in heads(j), 30, "the verifier head was retired")
    j.keys(b"\x1b\x1b1")
    j.see_all("Heads in this project", "plan run ux-greeting")
    j.keys(b"j\r")
    screen = j.see_all("worker", "ux-worker")
    rows = [line for line in screen.splitlines() if "ux-worker" in line]
    j.record("U15.a", "Work does not summarise the committed change as zero files",
             bool(rows) and not any(" 0 files" in r or "0 changed" in r for r in rows), detail="; ".join(rows)[:200])
    j.keys(b"j\r")
    details = j.see("Details: ux-worker")
    j.record("U15.a", "Details label uncommitted changes separately from the task's diff against its base",
             "0 changed files" not in details and "not yet committed" in details)
    j.keys(b":")
    j.see("INPUT TO HYDRA")
    j.keys(b"diff\r")
    shown = j.see_all("+++ b/greeting.txt", "+++ b/lib/greet.sh", f"+{GREETING}")
    j.record("U15.a", "the task's full diff is one action away and lists both committed files",
             "+++ b/greeting.txt" in shown and "+++ b/lib/greet.sh" in shown)
    j.see("Press Enter to return to Hydra")
    j.keys(b"\r")
    j.see("HYDRA /")
