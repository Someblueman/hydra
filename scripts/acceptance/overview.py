"""U9/U15 (overview and coordination) and U13 (statistics keeps context)."""

from __future__ import annotations

import subprocess
import time

from acceptance.harness import Case, case, pane_text, select_head

STATS_TAB = ("[Statistics]", "[Stats]")
COORD_TAB = ("[Coordination]", "[Coord]")


def tab_bar_intact(j: Case, screen: str) -> bool:
    bar = j.tab_bar(screen)
    return all(label in bar for label in ("Work", "Details", "Overview", "Recovery"))


def statistics_round_trip(j: Case, bullet: str, label: str, context: str) -> str:
    """Enter Statistics with 7, check the tab bar and coverage text, leave with Esc."""
    j.keys(b"7")
    screen = j.see_any(*STATS_TAB)
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
    dependencies = pane_text(screen, "DEPENDENCIES")
    j.record("U9.a", "the empty workspace's dependency and activity panes explain their emptiness rather than showing blank charts",
             "No recorded run" in screen and "Nothing has happened yet" in screen
             and "Press n to start a task" in screen, detail=dependencies[:120])
    j.keys(b"7")
    screen = j.see_any(*STATS_TAB)
    j.record("U13.a", "Statistics with no history says there are none yet and explains what it covers",
             "No workflow statistics yet" in screen and "A standalone agent conversation is not a recorded run" in screen)
    j.record("U9.d", "in an unregistered repository the statistics scope is reported as unavailable, not as zero runs",
             "Project identity unavailable" in screen and "PARTIAL" in screen)
    j.record("U13.a", "Statistics keeps the shared tab bar in the empty state", tab_bar_intact(j, screen))


@case("u9-single-agent", ("U9.b", "U9.d", "U15.b", "U13.a"))
def single_agent(j: Case) -> None:
    j.cli("init", "--no-agent")
    j.cli("spawn", "solo", "--no-agent")
    j.open()
    j.keys(b"1")
    j.see_all("[Work]", "solo")
    j.keys(b"3")
    screen = j.see_all("[Overview]", "solo")
    rows = screen.splitlines()
    cards = [r for r in rows if "RUNNING" in r and "NEED ATTENTION" in r]
    j.record("U9.b", "Overview answers running / attention / active runs / changed files with labelled counters",
             bool(cards) and "heads and runs working now" in screen and "failures, decisions, stopped" in screen
             and "recorded in this project" in screen and "uncommitted, all heads" in screen)
    j.record("U9.b", "Overview lists the head with its state and the route to open it",
             any("solo" in r and "running" in r for r in rows) and "Enter opens or expands" in screen)
    j.record("U9.b", "Overview's selected panel says what the head needs (agent, session, changes, approvals)",
             "Agent: shell" in screen and "Session: running" in screen and "No approval requests" in screen)
    j.record("U9.b", "Overview explains that nothing has finished yet and how a run would appear (E in the plan review)",
             "No workflow run yet" in screen and "execute an approved plan" in screen)
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"\r")
    j.see("Details: solo")
    j.keys(b"c")
    screen = j.see_any(*COORD_TAB)
    j.record("U15.b", "Coordination for a single agent explains what coordination means and why there is nothing to coordinate",
             "This head is a single agent session" in screen and "A single conversation does not need one" in screen)
    j.record("U9.b", "Coordination shows this head's claims, scopes, queue, resources and approvals with the action that inspects each",
             all(label in screen for label in ("Claims", "Scopes", "Queue", "Resources", "Approvals"))
             and ": claims" in screen)
    screen = statistics_round_trip(j, "U13.a", "from Coordination during standalone agent work", "[Coord")
    j.record("U13.a", "Statistics explains that a standalone agent session is not a recorded workflow run",
             "A standalone agent conversation is not a recorded run" in screen
             or "no agent steps" in screen and "all recorded" in screen, detail=screen.splitlines()[2][:80])
    j.record("U9.d", "with a registered project and no runs, Statistics reports zero runs in scope and marks absent measurements as unavailable rather than zero",
             (" 0 " in screen and "in this scope" in screen and "unavailable" in screen)
             or "No workflow statistics yet" in screen)
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
    j.record("U9.c", "the NEED ATTENTION counter reflects the decision",
             any("NEED ATTENTION" in r for r in screen.splitlines()) and "1 need attention" in screen.splitlines()[0])
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
    j.keys(b"7")
    screen = j.see_all("handoff", "waiting-approval")
    j.record("U13.b", "Statistics lists the recorded run with its state and counts it",
             any("handoff" in r and "waiting-approval" in r for r in screen.splitlines()) and "Blocked" in screen)
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
