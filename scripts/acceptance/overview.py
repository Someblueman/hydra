"""U9/U15 (overview and coordination) and U13 (statistics keeps context)."""

from __future__ import annotations

import re
import subprocess
import time

from acceptance.harness import Case, case, goto_tab, pane_text, select_head, tab_position

STATS_TAB = ("[Statistics]", "[Stats]")
COORD_TAB = ("[Coordination]", "[Coord]")


def tab_bar_intact(j: Case, screen: str) -> bool:
    bar = j.tab_bar(screen)
    return all(label in bar for label in ("Work", "Details", "Overview", "Recovery"))


def statistics_round_trip(j: Case, bullet: str, label: str, context: str) -> str:
    """Enter Statistics by its number in the visible tab bar, check the tab bar
    and coverage text, leave with Esc."""
    position = tab_position(j, "Statistics", "Stats")
    j.record(bullet, f"{label}: Statistics keeps its number key (7, as on Work) in the shared navigation",
             position == 7, detail=f"the bar numbers Statistics {position}: {j.tab_bar().strip()!r}")
    screen = goto_tab(j, "Statistics", "Stats")
    j.record(bullet, f"{label}: Statistics keeps the shared tab bar", tab_bar_intact(j, screen))
    j.keys(b"\x1b")
    back = j.see_any("[Work]", "[Details]", "[Overview]", *COORD_TAB, "[Workspace]")
    j.record(bullet, f"{label}: Esc returns to the previous view ({context}) with its context",
             context in back, detail=f"returned to {j.tab_bar(back).strip()[:60]!r}")
    return screen


@case("u9-empty", ("U9.a", "U9.d", "U13.a"))
def empty(j: Case) -> None:
    j.open()
    j.keys(b"3")
    screen = j.see("[Overview]")
    j.record("U9.a", "Overview with no work explains what it will show and how to start (n)",
             "No agent work in this project yet" in screen and "Overview summarises" in screen
             and "Press n to start a task" in screen)
    j.keys(b"W")
    screen = j.see("[Workspace]")
    panes = re.findall(r"\+- ([A-Z][A-Z /]+?) -", screen)
    explained = {"DEPENDENCIES": "No recorded run", "ACTIVITY": "Nothing has happened yet",
                 "SELECTED WORK": "No agent work in this project yet"}
    j.record("U9.a", f"the empty workspace's panes ({', '.join(panes)}) explain their emptiness and offer n, not blank charts",
             "Press n to start a task" in screen
             and all(text in pane_text(screen, name) for name, text in explained.items() if name in panes),
             detail=pane_text(screen, "SELECTED WORK")[:160] or pane_text(screen, "DEPENDENCIES")[:120])
    screen = goto_tab(j, "Statistics", "Stats")
    j.record("U13.a", "Statistics with no history says there are none yet and explains what it covers",
             "No workflow statistics yet" in screen and "A standalone agent conversation is not a recorded run" in screen)
    j.record("U9.d", "in an unregistered repository the statistics scope is reported as unavailable, not as zero runs",
             "Project identity unavailable" in screen and "PARTIAL" in screen)
    j.record("U13.a", "Statistics keeps the shared tab bar in the empty state", tab_bar_intact(j, screen))


@case("u9-single-agent", ("U9.b", "U9.d", "U15.b", "U13.a"), columns=(80, 140))
def single_agent(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "solo", "--no-agent")
    j.open()
    j.keys(b"1")
    j.see_all("[Work]", "solo")
    j.keys(b"3")
    screen = j.see_all("[Overview]", "solo")
    rows = screen.splitlines()
    wide = any("RUNNING" in r and "NEED ATTENTION" in r for r in rows)
    if wide:
        counters = ("heads and runs working now" in screen and "failures, decisions, stopped" in screen
                    and "recorded in this project" in screen and "uncommitted, all heads" in screen)
    else:
        counters = "1 running / 0 need attention / 0 active runs / 0 to repair" in screen
    j.record("U9.b", f"Overview at {j.columns} columns answers running / attention / active runs"
             + (" / changed files with labelled cards" if wide else " / to repair in one labelled line"), counters,
             detail=rows[3].strip()[:100])
    j.record("U9.b", "Overview lists the head with its state and the route to open it",
             any("solo" in r and "running" in r for r in rows) and "Enter opens or expands" in screen)
    if wide:
        j.record("U9.b", "Overview's selected panel says what the head has (changes, reported outcome, approvals)",
                 "Changed files 0" in screen and "Reported: nothing yet" in screen and "No approval requests" in screen)
    else:
        j.keys(b"\r")
        details = j.see("Details: solo")
        j.record("U9.b", "in the compact Overview the hinted Enter opens the head's details (agent, session, changes, approvals)",
                 "Session" in details and "Approval requests" in details and "CHANGES" in details)
        j.keys(b"3")
        screen = j.see("[Overview]")
    j.record("U9.b", "Overview explains that nothing has finished yet and how a run would appear (E in the plan review)",
             "No workflow run yet" in screen and "execute an approved plan" in screen)
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"\r")
    j.see("Details: solo")
    j.keys(b"c")
    screen = j.see_any(*COORD_TAB)
    j.record("U15.b", "Coordination for a single agent explains what coordination means and why there is nothing to coordinate",
             "This head is a single agent session" in screen and "Coordination between heads matters" in screen
             and "No workflow run is linked to this head" in screen)
    j.record("U9.b", "Coordination shows this head's claims, scopes, queue, resources and approvals with the action that inspects each",
             all(label in screen for label in ("Claims", "Scopes", "Queue", "Resources", "Approvals"))
             and ": claims" in screen)
    screen = statistics_round_trip(j, "U13.a", "from Coordination during standalone agent work", "[Coord")
    j.record("U13.a", "Statistics explains that a standalone agent session is not a recorded workflow run",
             "A standalone agent conversation is not a recorded run" in screen
             or "no agent steps" in screen and "all recorded" in screen, detail=screen.splitlines()[2][:80])
    j.record("U9.d", "with a registered project and no runs, Statistics reports zero runs in scope and marks absent measurements (--, unavailable) rather than zero",
             (" 0 " in screen and "in this scope" in screen and ("unavailable" in screen or "average --" in screen))
             or "No workflow statistics yet" in screen, detail=next((l.strip() for l in screen.splitlines() if "average" in l), "")[:100])
    j.record("U13.a", "with no history the empty run list says there are no runs yet rather than that filters exclude data",
             "No workflow statistics yet" in screen or ("No recorded runs" in screen and "reset filters" not in screen),
             detail=next((l.strip() for l in screen.splitlines() if "No " in l), "")[:100])
    j.keys(b"\x1b\x1b1")
    screen = j.see("[Work]")
    before = j.selected_row(screen)
    statistics_round_trip(j, "U13.a", "from Work", "[Work]")
    j.record("U13.a", "the Work selection is unchanged after visiting Statistics",
             j.selected_row() == before and before != "", detail=before[:40])
    j.keys(b"4")
    screen = j.see("[Attention]")
    j.record("U9.d", "Attention counts unknown items separately from items that need the user",
             "unknown" in screen and "need you" in screen)


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


@case("u9-multi-agent-blocked", ("U9.c", "U13.b", "U15.b"))
def multi_agent_blocked(j: Case) -> None:
    run = handoff_workflow(j)
    j.open()
    j.keys(b"1")
    j.see_all("[Work]", "worker-a", "worker-b")
    j.keys(b"3")
    screen = j.see_all("[Overview]", "RUN / run handoff")
    j.record("U9.c", "Overview's run panel lists each step with state and head, including the queued handoff on worker-b",
             "produce" in screen and "succeeded" in screen and "waiting-approval" in screen
             and any("handoff" in r and "queued" in r and "worker-b" in r for r in screen.splitlines()))
    j.record("U9.c", "Overview names the pending decision and the route to take it (C monitor, Y approve or N reject)",
             "Needs your decision" in screen and "Y approve" in screen)
    j.record("U9.c", "the need-attention counter reflects the decision",
             "1 need attention" in screen or any("NEED ATTENTION" in r for r in screen.splitlines()),
             detail=screen.splitlines()[3].strip()[:100])
    j.keys(b"4")
    screen = j.see_all("[Attention]", "Approval needed")
    j.record("U9.c", "Attention lists the approval with a plain explanation and the review route (r)",
             "waiting for your decision" in screen and "Press r" in screen)
    j.keys(b"6")
    screen = j.see_any("[Workflows]", "[Flows]")
    j.record("U9.c", "Workflows draws the dependency chain produce -> approval -> handoff with each state",
             "produce" in screen and "approval" in screen and "handoff" in screen and "DEPENDENCIES" in screen
             and "queued" in screen and "waiting-approval" in screen)
    # Coordination for the blocked head must explain the blocker.
    j.keys(b"1")
    j.see("[Work]")
    select_head(j, "worker-b")
    j.keys(b"\r")
    j.see("Details: worker-b")
    j.keys(b"c")
    screen = j.see_any(*COORD_TAB)
    j.record("U15.b", "Coordination for worker-b names the recorded run and where its steps and dependencies are shown",
             "recorded workflow run" in screen and "Workflows" in screen)
    j.record("U9.c", "Coordination for worker-b explains its assignment (handoff) and the blocker (approval on worker-a)",
             "handoff" in screen and "approval" in screen and "worker-a" in screen,
             detail="desired behaviour: the view says 'single agent session' and does not mention the queued step")
    statistics_round_trip(j, "U13.b", "from Coordination during a recorded workflow", "[Coord")
    goto_tab(j, "Statistics", "Stats")
    screen = j.see_all("handoff", "waiting-approval")
    j.record("U13.b", "Statistics lists the recorded run with its state and counts it",
             any("handoff" in r and "waiting-approval" in r for r in screen.splitlines())
             and ("1/1" in screen or "Blocked" in screen), detail=next((l.strip() for l in screen.splitlines() if "handoff" in l), "")[:100])
    j.keys(b"\x1b")
    # Approve inside the control centre; the handoff to worker-b then runs.
    j.keys(b"\x1b1")
    j.see("[Work]")
    j.keys(b"W")
    j.see("[Workspace]")
    j.keys(b"C")
    screen = j.see("MONITOR")
    j.record("U9.c", "the monitor layout offers Y approve / N reject for the waiting step", "Y approve" in screen)
    j.keys(b"Y")
    time.sleep(0.5)
    confirm = j.screen()
    if "y/N" in confirm or "confirm" in confirm.lower():
        j.keys(b"y\r")
    states = j.base / "state" / "state" / "v2" / "projects"
    run_dirs = list(states.glob(f"*/workflows/runs/{run}")) if run else []
    j.until(lambda: run_dirs and (run_dirs[0] / "state").read_text().strip() in ("succeeded", "failed"), 60,
            "the run reached a final state after the in-app approval")
    final = (run_dirs[0] / "state").read_text().strip() if run_dirs else "absent"
    j.record("U9.c", "approving in-app unblocks the handoff: the dependent step on worker-b ran and the run succeeded",
             final == "succeeded" and "handoff [exec]: succeeded" in j.cli("workflow", "status", run),
             detail=f"run state {final}", evidence=[j.keep("workflow-status-after.txt", j.cli("workflow", "status", run))])
    j.keys(b"\x1b\x1b3")
    screen = j.see_all("[Overview]", "succeeded")
    j.record("U9.c", "Overview reports what finished", "succeeded" in pane_text(screen, "RUN /") or "succeeded" in screen)
