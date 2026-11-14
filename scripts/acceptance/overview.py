"""U9/U15 (what is running, needs you, changed and finished; coordination)
and U13 (statistics keeps context).

Overview and the Coordination view are gone (the redesign's D11 and its
"Dropped" list): the top bar counts the work, the Inbox holds what needs a
decision, the Run page holds a run's steps and next step, and a head's
Details page holds its coordination row. These cases check each bullet
where it now lives."""

from __future__ import annotations

import subprocess
import time

from acceptance.harness import Case, case, column_rows, goto_tab, says, select_head, tab_position
from usability.journey import TOP_BAR, page, see_footer, select


def top_bar_intact(j: Case, screen: str) -> bool:
    bar = j.tab_bar(screen)
    return all(f"{n} {label}" in bar for n, label in ((1, "Work"), (2, "Inbox"), (3, "Hosts"), (4, "Stats")))


def statistics_round_trip(j: Case, bullet: str, label: str, back: str) -> str:
    """Enter Stats by its number in the top bar, check the bar and the
    coverage text, return with the number of the screen it came from."""
    position = tab_position(j, "Stats")
    j.record(bullet, f"{label}: Stats keeps its number key (4) in the shared top bar",
             position == 4, detail=f"the bar numbers Stats {position}: {j.tab_bar().strip()!r}")
    screen = goto_tab(j, "Stats")
    j.record(bullet, f"{label}: Stats keeps the shared top bar", top_bar_intact(j, screen))
    j.keys(b"1")
    returned = j.see_all(*TOP_BAR, back)
    j.record(bullet, f"{label}: 1 returns to Work with its context ({back})", back in returned)
    return screen


def details_text(screen: str) -> str:
    return " ".join(" ".join(column_rows(screen, "Agent")).split())


@case("u9-empty", ("U9.a", "U9.d", "U13.a"))
def empty(j: Case) -> None:
    j.open()
    j.keys(b"1")
    screen = j.see("No agent work in this project yet")
    j.record("U9.a", "Work with no work explains what a head is and offers the next actions (n, 3, :)",
             "A head is one task" in screen and "n start a task" in screen and "3 add a remote host" in screen)
    j.keys(b"2")
    screen = j.see("Nothing needs you")
    j.record("U9.a", "the Inbox with no work explains what would appear there instead of an empty list",
             says(screen, "Approvals, failed checks, finished results, stopped terminals and repairs appear here"))
    j.record("U9.d", "without workflow runs the Inbox says so, muted, rather than reporting a failure or zero",
             says(screen, "No workflow runs are recorded in this project, so only heads are watched"))
    screen = goto_tab(j, "Stats")
    screen = page(j, b"[", "No sessions recorded yet")
    j.record("U13.a", "Stats with no history says there are none yet and explains what it covers",
             says(screen, "A session is recorded each time a head is started or resumed"))
    j.record("U9.d", "in an unregistered repository the statistics scope is reported as unavailable, not as zero",
             "Project identity unavailable" in screen and "Partial sample" in screen)
    j.record("U13.a", "Stats keeps the shared top bar in the empty state", top_bar_intact(j, screen))


@case("u9-single-agent", ("U9.b", "U9.d", "U15.b", "U13.a"), columns=(80, 140))
def single_agent(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "solo", "--no-agent")
    j.open()
    j.keys(b"1")
    screen = j.see_all("solo", "1 head")
    j.record("U9.b", f"the top bar at {j.columns} columns counts what is running", "1 running" in j.tab_bar(screen),
             detail=j.tab_bar(screen).strip())
    j.record("U9.b", "Work lists the head with its state and the route to talk to it",
             any("solo" in r for r in screen.splitlines()) and "running" in screen and "talk" in j.hint(screen))
    screen = page(j, b"]", "Session", "CHANGES")
    text = details_text(screen)
    j.record("U9.b", "the Details page says what the head has (session, changes, reported outcome, approvals)",
             "Session running" in text and "Changed 0 files" in text and "Reported nothing yet" in text
             and "Approvals none" in text, detail=text[:300])
    j.record("U15.b", "Details explain coordination where it applies: claims and scopes, and that Hydra flags overlap",
             "Coordinate 0 claims" in text and "Claims are files a head means to change" in text
             and "Hydra flags overlap between heads before they conflict" in text, detail=text[-300:])
    j.record("U9.b", "with no run there is no Run page: nothing finished is presented",
             "Run " not in screen.splitlines()[1 if j.columns < 100 else 2])
    see_footer(j, "x remove")
    statistics_round_trip(j, "U13.a", "from Work during standalone agent work", "solo")
    goto_tab(j, "Stats")
    screen = page(j, b"]", "No workflow runs recorded yet")
    j.record("U13.a", "Stats explains that a run is recorded when a plan is executed (a standalone head is no run)",
             says(screen, "A run is recorded each time a plan is executed"))
    j.record("U9.d", "with a registered project and no runs, Stats says no runs are recorded rather than that filters exclude them",
             "No workflow runs recorded yet" in screen and "0 resets" not in screen)
    j.keys(b"1")
    screen = j.see("solo")
    before = j.selected_row(screen)
    statistics_round_trip(j, "U13.a", "from Work", "solo")
    j.record("U13.a", "the Work selection is unchanged after visiting Stats",
             j.selected_row() == before and before != "", detail=before[:40])
    j.keys(b"2")
    screen = j.see("Nothing needs you")
    j.record("U9.d", "the Inbox says nothing needs the user and why only heads are watched (no runs), not a zero count",
             says(screen, "No workflow runs are recorded in this project"))


def handoff_workflow(j: Case) -> str:
    """Two heads; a run whose handoff to worker-b waits on an approval on worker-a."""
    j.cli("init", "--no-agent", "--trust")
    j.cli("spawn", "worker-a", "--no-agent")
    j.cli("spawn", "worker-b", "--no-agent")
    fixtures = j.source / "tests/fixtures/acceptance"
    (j.base / "data.json").write_text((fixtures / "handoff-data.json").read_text())
    flow = (fixtures / "handoff-flow.yml").read_text().replace("@PRODUCE@", str(fixtures / "handoff-produce.sh"))
    (j.base / "flow.yml").write_text(flow)
    result = subprocess.run([str(j.hydra), "workflow", "run", str(j.base / "flow.yml")], cwd=j.repo, env=j.env,
                            capture_output=True, text=True, timeout=120, check=False)
    j.proof.append({"command": ("hydra", "workflow", "run", "flow.yml"), "exit": result.returncode,
                    "output": result.stdout + result.stderr})
    run = result.stdout.split()[0] if result.stdout.split() else ""
    j.record("U9.c", "the workflow run stops at the approval wait (exit 3) with the handoff step queued",
             result.returncode == 3 and "handoff [exec]: queued" in j.cli("workflow", "status", run),
             evidence=[j.keep("workflow-status.txt", j.cli("workflow", "status", run) if run else result.stderr)])
    return run


def confirm(j: Case, key: bytes, *hints: str) -> str:
    """A confirmed action: its key, the dialog (its bottom edge names y), then y."""
    j.keys(key)
    dialog = j.see_any(*hints)
    j.keys(b"y")
    return dialog


@case("u9-multi-agent-blocked", ("U9.c", "U13.b", "U15.b"))
def multi_agent_blocked(j: Case) -> None:
    run = handoff_workflow(j)
    j.open()
    j.keys(b"1")
    j.see_all("worker-a", "worker-b", "run handoff")
    select(j, "run handoff")
    screen = j.see_all("STEP", "produce", "handoff")
    j.record("U9.c", "the Run page lists each step with its state, including the approval wait and the queued handoff",
             "produce" in screen and "succeeded" in screen and "waiting-approval" in screen
             and any("handoff" in r and "queued" in r for r in screen.splitlines()))
    j.record("U9.c", "with the run selected the footer offers the decision (Y approve, N reject)",
             "Y approve" in j.hint(screen) and "N reject" in j.hint(screen), detail=j.hint(screen))
    j.record("U9.c", "the Work list counts what needs the user",
             any("need you" in r or "needs you" in r for r in screen.splitlines()), detail=screen.splitlines()[-3].strip())
    j.record("U9.c", "the Run page draws the dependency chain produce -> approval -> handoff with each state",
             "produce" in screen and "approval" in screen and "handoff" in screen and "queued" in screen)
    j.keys(b"2")
    listing = j.see("Decisions")
    select(j, "Approve")
    j.keys(b"\r")
    screen = j.see_prose("waiting for your decision")
    j.record("U9.c", "the Inbox lists the approval first, with a plain explanation and the review route (r)",
             listing.find("Decisions") < listing.find("Approve") and ("r review" in j.hint(screen) or "r Review" in screen))
    j.escape()
    # Coordination for the blocked head: its Details page.
    j.keys(b"1")
    see_footer(j, "select")
    select_head(j, "worker-b")
    screen = page(j, b"]", "Session", "CHANGES")
    text = details_text(screen)
    j.record("U15.b", "worker-b's Details name the recorded run it belongs to",
             "handoff" in text and ("run" in text.lower()), detail=text[:300])
    j.record("U9.c", "worker-b's Details explain its assignment (handoff) and the blocker (approval on worker-a)",
             "handoff" in text and "approval" in text and "worker-a" in text,
             detail="desired behaviour: " + text[:240])
    statistics_round_trip(j, "U13.b", "from Work during a recorded workflow", "worker-b")
    goto_tab(j, "Stats")
    screen = page(j, b"]", "handoff", "waiting-approval")
    j.record("U13.b", "Stats lists the recorded run with its state and counts it",
             any("handoff" in r and "waiting-approval" in r for r in screen.splitlines()) and "1 run" in screen,
             detail=next((l.strip() for l in screen.splitlines() if "handoff" in l), "")[:100])
    # Approve inside the control centre from the Inbox; then resume the run.
    states = j.base / "state" / "state" / "v2" / "projects"
    run_dirs = list(states.glob(f"*/workflows/runs/{run}")) if run else []
    j.keys(b"2")
    j.see("Decisions")
    select(j, "Approve")
    dialog = confirm(j, b"Y", "y approve")
    j.record("U9.c", "the approval dialog names the request; nothing is typed", "y approve" in dialog)
    j.until(lambda: bool(run_dirs and list(run_dirs[0].glob("approvals/*/decision/action"))), 30,
            "the in-app approval was recorded")
    # A decision never resumes work by itself: R resumes the run after its confirmation.
    j.keys(b"1")
    see_footer(j, "select")
    select(j, "run handoff")
    confirm(j, b"R", "y resume", "y confirm")
    j.until(lambda: run_dirs and (run_dirs[0] / "state").read_text().strip() in ("succeeded", "failed"), 120,
            "the run reached a final state after the in-app approval")
    final = (run_dirs[0] / "state").read_text().strip() if run_dirs else "absent"
    j.record("U9.c", "approving in-app unblocks the handoff: the dependent step on worker-b ran and the run succeeded",
             final == "succeeded" and "handoff [exec]: succeeded" in j.cli("workflow", "status", run),
             detail=f"run state {final}", evidence=[j.keep("workflow-status-after.txt", j.cli("workflow", "status", run))])
    deadline = time.monotonic() + 15
    screen = j.screen()
    while "succeeded" not in screen.splitlines()[1 if j.columns < 100 else 3] and time.monotonic() < deadline:
        time.sleep(0.5)
        screen = j.screen()
    j.record("U9.c", "the Run page reports what finished", any("succeeded" in r and "handoff" in r for r in screen.splitlines()[:4]))
