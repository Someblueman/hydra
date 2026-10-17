"""Result review and failed-check recovery journeys on the greeting plan."""

from __future__ import annotations

import json
import re
import time
from pathlib import Path

from usability.journey import Journey, prose
from usability.journeys_plan import (
    GREETING,
    converse_and_validate,
    execute,
    listed_heads,
    provider_fixture,
    worker_diff,
)


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


