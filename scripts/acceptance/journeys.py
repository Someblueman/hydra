"""U17 (conversation to plan without JSON) and U15.a (a committed change is
shown as the task's diff, never as "0 changed files").

The steps reuse the usability planning journeys by reference: the same
labelled provider fixture installed as `codex`, the same greeting plan, and
the same independent recompilation of each revision's digest. Checks here
are tagged with the acceptance bullet they establish."""

from __future__ import annotations

import json
import time

from acceptance.harness import Case, case, heads, says
from usability.journey import TOP_BAR, list_column, page, see_footer, select
from usability.journeys_plan import (
    GREETING,
    approval,
    finished_run,
    independent_digest,
    launched_digests,
    plan_page,
    plan_state,
    provider_fixture,
    runs,
    talk,
    worker_diff,
)


def hydra_commands(j: Case) -> list[tuple[str, ...]]:
    return [tuple(c["command"][1:]) for c in j.proof if "command" in c and c["command"][0] == str(j.hydra)]


def converse(j: Case, task: str) -> str:
    j.new_task(task, "codex", "Add a committed greeting and verify it")
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    screen = j.see("PROPOSAL READY")
    j.leave_agent()
    return screen


@case("u17-plan-from-conversation", ("U17.a", "U17.b", "U17.c", "U15.a"), seconds=240)
def plan_from_conversation(j: Case) -> None:
    provider_fixture(j, GREETING)
    j.open()
    converse(j, "greeting")
    user_commands = len(hydra_commands(j))
    # The proposal is the agent's; nothing is validated or executed yet.
    screen = plan_page(j, "Plan ready")
    j.record("U17.c", "the Plan page shows a published proposal as ready for review and says nothing runs until approval",
             says(screen, "Nothing runs until you approve it"))
    j.record("U17.c", "publishing a proposal records no workflow run",
             not runs(j), detail=f"runs: {sorted(runs(j))}")
    first = approval(j, 1)
    j.record("U17.a", "approval is one in-app dialog for the revision, with no policy file or JSON to locate",
             says(first, "Approve and run greeting revision 1?") and ".json" not in first.split("Approve plan", 1)[-1][:600],
             evidence=[j.keep("approval-dialog-initial.txt", first)])
    j.record("U17.c", "behind the dialog the revision is labelled DRAFT / not validated yet, not a validated workflow",
             "DRAFT" in first and "not validated yet" in first)
    proposal = j.cli("workflow", "plan", "proposal", "greeting")
    j.record("U17.a", "Hydra associated the agent's draft and the guided policy with the task head itself",
             any(line.startswith("P\t") and "/planning/draft.json" in line and "/planning/policy.json" in line
                 for line in proposal.splitlines()),
             evidence=[j.keep("plan-proposal.txt", proposal)])
    first_digest = independent_digest(j, "greeting", "initial")
    j.record("U17.b", "the dialog names the exact revision and says a changed draft or policy runs nothing",
             says(first, "revision 1?") and says(first, "If the draft or policy changes first") and says(first, "nothing runs"))
    j.escape()
    deadline = time.monotonic() + 3
    while "Approve and run" in j.screen() and time.monotonic() < deadline:
        time.sleep(0.2)
    closed = "Approve and run" not in j.screen()
    j.record("U17.b", "Esc cancels the approval dialog as its 'esc cancel' edge says, and nothing runs",
             closed and not runs(j) and not launched_digests(j))
    # Revise the plan through the same conversation.
    talk(j, "revise", "PROPOSAL REVISION READY")
    plan_page(j)
    screen = plan_state(j, 2)
    j.record("U17.b", "the revised proposal is a new revision shown as DRAFT / not validated yet",
             "DRAFT" in screen and "revision 2" in screen and "not validated yet" in screen)
    second = approval(j, 2)
    revised_digest = independent_digest(j, "greeting", "revised")
    j.keep("approval-dialog-revised.txt", second)
    j.record("U17.b", "the dialog for the changed plan names the new revision, and its digest differs from the first",
             says(second, "revision 2?") and revised_digest != first_digest,
             detail=f"{first_digest[:12]} -> {revised_digest[:12]}")
    j.record("U17.a", "creating and revising the plan needed no JSON, file path or hydra command from the user",
             all(c[:3] in (("workflow", "plan", "proposal"), ("workflow", "plan", "compile"))
                 for c in hydra_commands(j)[user_commands:]),
             detail="commands after the conversation started: "
                    + "; ".join(" ".join(c[:3]) for c in hydra_commands(j)[user_commands:]))
    before = set(runs(j))
    j.keys(b"y")
    run = finished_run(j, before)
    delivery = json.loads(j.cli("workflow", "plan", "result", run.name))
    j.record("U17.b", "the approval launched exactly the revised revision's digest, as an independent compile gives it",
             launched_digests(j) == {revised_digest}, detail=f"launched {sorted(d[:12] for d in launched_digests(j))}")
    j.record("U17.a", "approving the exact revised revision executes it and the delivery verifies",
             (run / "state").read_text().strip() == "succeeded" and delivery["ok"],
             detail=f"run {run.name} state {(run / 'state').read_text().strip()}",
             evidence=[j.keep_json("plan-result", delivery)])
    j.see_all(*TOP_BAR, "RUN RECORDED")
    committed_change(j)


def committed_change(j: Case) -> None:
    """U15.a: the worker committed two files and its worktree is clean."""
    files, diff = worker_diff(j, "ux-worker")
    clean = not j.command("git", "-C", j.cli("path", "ux-worker"), "status", "--porcelain")
    j.record("U15.a", "the worker branch holds a committed two-file change and its worktree is clean",
             sorted(files) == ["greeting.txt", "lib/greet.sh"] and f"+{GREETING}" in diff and clean,
             detail=f"files {files}", evidence=[j.keep("worker-diff.txt", diff)])
    j.until(lambda: "ux-verifier" not in heads(j), 30, "the verifier head was retired")
    j.escape(2)
    j.keys(b"1")
    see_footer(j, "select")
    select(j, "plan run ux")
    j.keys(b"l")
    screen = j.see_all("worker ux-w", "verifier")
    rows = [line for line in list_column(screen) if "ux-w" in line]
    j.record("U15.a", "Work does not summarise the committed change as zero files",
             bool(rows) and not any(" 0 files" in r or "0 changed" in r for r in rows), detail="; ".join(rows)[:200])
    select(j, "worker ux-w")
    details = page(j, b"]", "CHANGES", "Full diff")
    j.record("U15.a", "Details label uncommitted changes separately from the task's diff against its base",
             "0 changed files" not in details and says(details, "not yet committed"))
    j.keys(b":")
    j.see("type to filter")
    j.keys(b"diff")
    j.see("Show diff of the selected head")
    j.keys(b"\r")
    shown = j.see_all("+++ b/greeting.txt", "+++ b/lib/greet.sh", f"+{GREETING}")
    j.record("U15.a", "the task's full diff is one action away, inside the control centre, and lists both committed files",
             "+++ b/greeting.txt" in shown and "+++ b/lib/greet.sh" in shown and "Press Enter to return" not in shown)
    j.escape()
    see_footer(j, "select")
