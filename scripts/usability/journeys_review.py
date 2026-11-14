"""Result review and failed-check recovery journeys on the greeting plan."""

from __future__ import annotations

import json
import re
import time
from pathlib import Path

from usability.journey import (
    TOP_BAR,
    Journey,
    footer,
    list_column,
    page,
    prose,
    see_column,
    see_footer,
    select,
)
from usability.journeys_plan import (
    GREETING,
    converse_to_proposal,
    execute,
    listed_heads,
    plan_page,
    provider_fixture,
    talk,
    worker_diff,
)


def run_page(j: Journey) -> None:
    """Select the run in the Work list (under the planning head that
    launched it); its Run page shows beside the list."""
    j.keys(b"1")
    j.escape()
    see_footer(j, "select")
    select(j, "plan run ux")


def run_next(j: Journey, *texts: str) -> str:
    """What to do next with the selected run: its Run page's "Next" row. A
    short screen cuts the Run page above that row (the steps and graph take
    the rows), so there the same row is read from the Plan page of the
    planning head that launched the run ("Runs it launched"), paged down."""
    try:
        return see_column(j, "Next", *texts, timeout=4)
    except TimeoutError:
        j.proof.append({"note": "the Run page's Next row is below the visible rows", "at_ns": time.monotonic_ns()})
    select(j, "greeting")
    plan_page(j)
    for _ in range(6):
        try:
            return see_column(j, "Next", *texts, timeout=2)
        except TimeoutError:
            j.keys(b"\x1b[6~")
    return see_column(j, "Next", *texts)


def result_review(j: Journey) -> None:
    provider_fixture(j, GREETING)
    j.open()
    converse_to_proposal(j, "greeting")
    run = execute(j, "greeting", "approved", 1)
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

    # The Run page: the finished run, its worker and what to do next.
    run_page(j)
    j.see_all("plan run ux-greeting", "succeeded")
    run_next(j, "Worker branch ux-worker holds the result", "hydra land ux-worker", "dismiss")
    j.escape()
    see_footer(j, "select")
    select(j, "plan run ux")
    # Work groups the run's heads under the planning conversation that launched it.
    screen = j.screen()
    listing = "\n".join(list_column(screen))
    j.check("ux-worker" not in listing.split("plan run ux")[0],
            "The worker head is not listed as a separate top-level head")
    j.keys(b"l")
    # The retired verifier stays listed in its run; from 100 columns its
    # agent column says it was retired.
    screen = j.see_all("worker ux-w", "verifier", *(("retired",) if j.columns >= 100 else ()))
    # U15: a clean worktree holding a two-file commit must not read as no change.
    worker_rows = [line for line in list_column(screen) if "worker" in line]
    j.check(not any(" 0 files" in row or "0 changed" in row for row in worker_rows),
            "The worker's committed two-file change is not summarised as zero files")
    select(j, "worker ux-w")
    see_footer(j, "x remove")
    details = page(j, b"]", "CHANGES", "Full diff")
    j.check("0 changed files" not in details and "notyetcommitted" in "".join(prose(details).split()),
            "Worker details label uncommitted changes separately from the task's diff")
    # U15: the task's full diff is one action away and shows the committed files.
    j.keys(b":")
    j.see("type to filter")
    j.keys(b"diff")
    j.see("Show diff of the selected head")
    j.keys(b"\r")
    j.see_all("+++ b/greeting.txt", "+++ b/lib/greet.sh", f"+{GREETING}")
    j.expect("Press Enter to return" not in j.screen(), "The diff opens inside Hydra, not in a shell detour")
    j.escape()
    j.see_all(*TOP_BAR, "plan run ux")

    # Inbox: the result, then its review.
    j.keys(b"2")
    j.see_all("Result ready: ux-greeting", "Ready to land: ux-worker")
    select(j, "Result ready: ux-greeting")
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
    """Move through review sections with n until heading is at the top of
    the review (under its identity lines), or as high as the end of the
    review allows."""
    screen = j.screen()
    for _ in range(presses):
        rows = screen.splitlines()
        if any(heading in row for row in rows[3:9]):
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
    converse_to_proposal(j, "greeting")
    run = execute(j, "greeting", "first", 1)
    j.check((run / "state").read_text().strip() == "failed", "The first run failed its check")
    j.check((run / "steps/repo-check/state").read_text().strip() == "failed",
            "The repository check step is the one that failed")
    notices = Path(j.env["UX_NOTICES"])
    j.until(lambda: notices.exists() and "repo-check failed" in notices.read_text(), 30,
            "The planning agent was told which step failed")
    j.until(lambda: "finished: failed" in notices.read_text(), 30,
            "The planning agent was told the run failed")

    # Plain-language failure on the Run page and in the Inbox.
    run_page(j)
    j.see_all("plan run ux-greeting", "failed")
    j.expect(bool(run_next(j, "Step repo-check failed on ux-worker")),
             "The Run page names the failed step and its head")
    j.escape()
    j.keys(b"2")
    # Wait for the loaded list (an item the Inbox offers for this run).
    screen = j.see_all("Failed", "ux-greeting")
    rows = [line.lower() for line in screen.splitlines() if "repo-check" in line or "failed" in line.lower()]
    j.expect(any("fail" in row for row in rows),
             "The Inbox lists the failed repository check in plain language")
    # Review the failure the Inbox offers for this run; the result is the run's.
    select(j, "ux-greeting")
    j.keys(b"r")
    screen = j.see_any("RESULT  ux-greeting", "r loads exact review")
    if "r loads exact review" in screen:
        # The item changed under the review while the run's records landed;
        # the review says to load it again, and r does.
        j.proof.append({"note": "the review asked to be loaded again", "at_ns": time.monotonic_ns()})
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
    j.escape(2)

    # Supported recovery: the agent already has the failure; ask it for a fix
    # and approve the new revision with one key and its confirmation.
    j.keys(b"1")
    j.escape()
    see_footer(j, "select")
    select(j, "greeting")
    j.attach_agent()
    j.see("FIXTURE RECEIVED HYDRA NOTICE")
    j.leave_agent()
    talk(j, "fix", "PROPOSAL FIX READY")
    plan_page(j)
    second = execute(j, "greeting", "fixed", 2)
    j.check((second / "state").read_text().strip() == "succeeded",
            "The corrected revision succeeded")
    j.check(json.loads(j.cli("workflow", "plan", "result", second.name))["ok"],
            "Independent delivery verification passed after recovery")
    files, diff = worker_diff(j, "ux-worker-fix")
    j.check(f"+{GREETING}" in diff and sorted(files) == ["greeting.txt", "lib/greet.sh"],
            "The recovered worker branch holds the corrected change")
    j.check((run / "state").read_text().strip() == "failed",
            "The failed run's record is kept, not rewritten")
    j.check(footer(j.screen()) != "", "The control centre is still up after recovery")
