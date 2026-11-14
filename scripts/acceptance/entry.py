"""U1 (onboarding out of the source tree) and U3 (workspace before work)."""

from __future__ import annotations

import shutil
import subprocess
import time
from pathlib import Path

from acceptance.harness import Case, case, profile_fixture, says
from usability.journey import LEFT_INPUT, TOP_BAR, TYPING, page

WORKFLOW = "examples/workflows/local-review.yml"


def open_configure_reopen(j: Case, bullet: str, label: str) -> None:
    """Open, configure (register the project and an agent profile), reopen and quit;
    the repository's tree, status and exclude rules must be byte-identical."""
    before = j.repo_state()
    j.keep_json("repo-before", before)
    j.open()
    j.record(bullet, f"{label}: entering the control centre leaves the tree and git status unchanged",
             j.repo_state() == before)
    j.record(bullet, f"{label}: entering creates no execution resources",
             '"branch"' not in j.cli("list", "--json"), evidence=[j.keep("list-after-entry.txt", j.cli("list", "--json"))])
    j.quit()
    init = j.cli("init", "--no-agent")
    j.cli("agent", "init", "acceptance-shell", "--executable", "/bin/sh")
    j.record(bullet, f"{label}: registering the project and an agent profile changes nothing in the repository",
             j.repo_state() == before and not (j.repo / ".hydra").exists(),
             evidence=[j.keep("init.txt", init)])
    j.open()
    j.quit()
    after = j.repo_state()
    j.keep_json("repo-after", after)
    j.record(bullet, f"{label}: reopening after configuration leaves tree, status and .git/info/exclude byte-identical",
             after == before, detail="" if after == before else f"status before {before['status']!r} after {after['status']!r}")


@case("u1-clean-repo", ("U1.a", "U1.c", "U1.d", "U3.d"))
def clean_repo(j: Case) -> None:
    open_configure_reopen(j, "U1.a", "clean repo")
    # Shared configuration only after explicit opt-in, with a preview.
    j.record("U1.c", "no .hydra directory exists before the opt-in", not (j.repo / ".hydra").exists())
    out = j.cli("init", "--no-agent", "--write-shared-config")
    status = j.command("git", "status", "--porcelain")
    j.record("U1.c", "the opt-in previews what it writes and says it is meant to be committed",
             "Writing .hydra/config.yml" in out and "commit it" in out, evidence=[j.keep("write-shared-config.txt", out)])
    j.record("U1.c", "after the opt-in only the shared config appears in git status",
             status == "?? .hydra/" and sorted(p.name for p in (j.repo / ".hydra").iterdir()) == ["config.yml"],
             detail=f"git status: {status!r}")
    # Repository commands still need explicit trust; entering the UI does not grant it.
    (j.repo / ".hydra" / "workflows").mkdir()
    shutil.copy2(j.source / WORKFLOW, j.repo / ".hydra" / "workflows" / "local.yml")
    j.command("git", "add", ".hydra")
    j.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "shared config and workflow")
    j.cli("init", "--no-agent")

    def validate() -> subprocess.CompletedProcess:
        return subprocess.run([str(j.hydra), "workflow", "validate", "local"], cwd=j.repo, env=j.env,
                              capture_output=True, text=True, timeout=30, check=False)

    first = validate()
    j.record("U1.d", "a committed repository workflow is refused before trust is recorded",
             first.returncode != 0 and "trust" in (first.stdout + first.stderr).lower(),
             detail=(first.stdout + first.stderr).strip()[-300:],
             evidence=[j.keep("validate-untrusted.txt", first.stdout + first.stderr)])
    j.open()
    j.keys(b"4")
    j.see("Sessions  Runs")  # any screen; the point is that the UI ran against the untrusted repository
    j.quit()
    second = validate()
    j.record("U3.d", "opening the control centre did not trust the repository's commands",
             second.returncode != 0, evidence=[j.keep("validate-after-ui.txt", second.stdout + second.stderr)])
    j.record("U1.d", "reduced setup steps did not imply blanket trust (still refused after init and UI entry)",
             second.returncode != 0)
    trusted = j.cli("init", "--no-agent", "--trust")
    third = validate()
    j.record("U1.d", "the workflow validates once trust is explicit", third.returncode == 0,
             evidence=[j.keep("init-trust.txt", trusted), j.keep("validate-trusted.txt", third.stdout + third.stderr)])


@case("u1-dirty-repo", ("U1.b",))
def dirty_repo(j: Case) -> None:
    j.make_repo_dirty()
    status = j.command("git", "status", "--porcelain")
    lines = [line.strip() for line in status.splitlines()]
    j.record("U1.b", "the fixture repository is dirty with its own ignore and exclude rules",
             "M README.txt" in lines and "?? notes.txt" in lines and "debug.log" not in status
             and "private-notes.txt" not in status, detail=status)
    open_configure_reopen(j, "U1.b", "dirty repo")
    j.record("U1.b", "ignored and excluded files are still ignored after onboarding",
             "debug.log" not in j.command("git", "status", "--porcelain")
             and "private-notes.txt" not in j.command("git", "status", "--porcelain")
             and (j.repo / "debug.log").read_text() == "ignored output\n")


def uninitialized(j: Case) -> None:
    j.record("U3.a", "the repository starts uninitialized: no Hydra registration or state",
             not (j.repo / ".git" / "hydra").exists() and not (j.repo / ".hydra").exists()
             and not list((j.base / "state").rglob("projects/*")))


@case("u3-ui-conversation", ("U3.a", "U3.d"))
def ui_conversation(j: Case) -> None:
    profile_fixture(j)
    uninitialized(j)
    before = j.repo_state()
    j.open()
    screen = j.screen()
    j.record("U3.d", "entering the UI registers nothing, starts no terminal and trusts nothing",
             not list((j.base / "state").rglob("projects/*")) and not j.tmux("list-sessions")
             and not (j.repo / ".git" / "hydra").exists() and "No agent work in this project yet" in screen)
    j.new_task("first-conversation", "codex")
    screen = j.see("ready to discuss")
    j.attached = True
    j.record("U3.a", "a conversation with the agent opens inside the control centre (top bar and agent pane together)",
             all(text in j.tab_bar(screen) for text in TOP_BAR) and TYPING in screen)
    j.keys(b"hello\r")
    j.see("Discuss the plan; type draft")
    j.record("U3.a", "typed input reaches the agent and its reply is shown in place", True)
    used = [c["command"] for c in j.proof if "command" in c and str(j.hydra) == c["command"][0]]
    j.record("U3.a", "no init, spawn, export or plan file was required of the user",
             not any(c[1] in ("init", "spawn") for c in used) and j.repo_state() == before,
             detail="hydra commands run by the case: " + "; ".join(" ".join(c[1:3]) for c in used))
    j.record("U3.a", "the work was created only when the conversation was requested",
             bool(j.tmux("list-sessions")) and '"branch"' in j.cli("list", "--json"))
    j.leave_agent()


@case("u3-cli-launch", ("U3.b",))
def cli_launch(j: Case) -> None:
    profile_fixture(j)
    uninitialized(j)
    env = {k: v for k, v in j.env.items() if k != "HYDRA_NO_SWITCH"}
    env["HYDRA_TUI_BIN"] = str(j.prefix / "libexec/hydra/hydra-tui")
    terminal = j.pty("spawn", [str(j.hydra), "spawn", "cli-task", "--profile", "codex", "--prompt",
                               "Add a greeting to the project"], 80, 24, env)
    try:
        screen = terminal.wait_for("ready to discuss", timeout=30)
        j.record("U3.b", "an interactive `hydra spawn --profile --prompt` opens the control centre with the agent visible",
                 all(text in screen.splitlines()[0] for text in TOP_BAR) and TYPING in screen,
                 evidence=["pty-spawn/final-screen.txt"])
        terminal.send(b"\x1d")
        time.sleep(0.5)
        screen = terminal.wait_for(LEFT_INPUT, timeout=10)
        j.record("U3.b", "leaving agent input keeps the user in the control centre, not in a bare provider",
                 all(text in screen.splitlines()[0] for text in TOP_BAR) and TYPING not in screen)
        terminal.send(b"q")
        receipt = terminal.close(timeout=10)
        j.record("U3.b", "quitting the control centre returns to the shell cleanly and keeps the task",
                 receipt["exit"] == 0 and "cli-task" in j.cli("list"), detail=str(receipt))
    finally:
        if terminal.poll() is None:
            terminal.close()


@case("u3-missing-provider", ("U3.c",))
def missing_provider(j: Case) -> None:
    j.open()
    j.keys(b"n")
    form = j.see_all("New task", "Agent")
    agents = next((row for row in form.splitlines() if "project default" in row), "")
    j.record("U3.c", "the new-task form offers only the agents that are available: codex, absent from PATH, is not a choice",
             "codex" not in agents and "none" in agents, detail=agents.strip())
    j.escape()
    j.see("No agent work in this project yet")
    j.keys(b"3")
    screen = j.see_all("This machine", "codex")
    row = next((r for r in screen.splitlines() if "codex" in r), "")
    j.record("U3.c", "Hosts says, inside the control centre, which agents this machine lacks (codex not found on PATH)",
             "not found on PATH" in row and all(text in j.tab_bar(screen) for text in TOP_BAR), detail=row.strip())
    j.record("U3.c", "nothing was launched for the missing agent; no head was created",
             "No active Hydra heads" in j.cli("list"))


@case("u3-missing-auth", ("U3.c",))
def missing_auth(j: Case) -> None:
    profile_fixture(j, script=j.source / "tests/fixtures/acceptance/noauth-provider.sh")
    j.open()
    j.new_task("needs-login", "codex", "Add a greeting")
    screen = j.see("Not logged in. Run: codex login")
    j.attached = True
    j.record("U3.c", "the provider's own sign-in instruction is visible inside the control centre",
             all(text in j.tab_bar(screen) for text in TOP_BAR))
    deadline = time.monotonic() + 15
    pane = ""
    while not says(pane, "this pane is now a shell") and time.monotonic() < deadline:
        pane = j.screen()
        time.sleep(0.2)
    j.record("U3.c", "Hydra explains that the agent exited and that the pane is a shell where the user can act",
             says(pane, "exited with status 1; this pane is now a shell"), detail=pane[:300])
    j.keys(b"printf ok > login-proof\r")
    worktree = Path(j.cli("path", "needs-login"))
    j.record("U3.c", "the pane is usable for the sign-in step (typed command runs in the head's worktree)",
             j.wait_file(worktree / "login-proof"))
    j.leave_agent()
    j.escape(2)
    j.keys(b"1")
    details = page(j, b"]", "Session", "CHANGES")
    j.record("U3.c", "Details or Work say the agent exited instead of presenting a running codex agent",
             "exited" in details.lower() or "not running" in details.lower() or "sign" in details.lower(),
             detail="desired behaviour: the head is labelled as if the agent were running")
