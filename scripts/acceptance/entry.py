"""U1 (onboarding out of the source tree) and U3 (workspace before work)."""

from __future__ import annotations

import shutil
import subprocess
import time
from pathlib import Path

from acceptance.harness import Case, case, pane_text, profile_fixture

WORKFLOW = "examples/workflows/local-review.yml"


def open_configure_reopen(j: Case, bullet: str, label: str) -> None:
    """Open, configure (register the project and an agent profile), reopen and quit;
    the repository's tree, status and exclude rules must be byte-identical."""
    before = j.repo_state()
    j.keep_json("repo-before", before)
    j.open()
    j.see("HYDRA /")
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
    j.see("HYDRA /")
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
    j.see("HYDRA /")
    j.keys(b"6")
    j.see("[Work")  # any view; the point is that the UI ran against the untrusted repository
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
    j.see("HYDRA /")
    screen = j.screen()
    j.record("U3.d", "entering the UI registers nothing, starts no terminal and trusts nothing",
             not list((j.base / "state").rglob("projects/*")) and not j.tmux("list-sessions")
             and not (j.repo / ".git" / "hydra").exists() and "no agent work yet" in screen)
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"first-conversation\r")
    j.see("Agent profile")
    j.keys(b"codex\r")
    j.see("Objective")
    j.keys(b"\r")
    screen = j.see("ready to discuss")
    j.attached = True
    j.record("U3.a", "a conversation with the agent opens inside the control centre (tab bar and agent pane together)",
             "[Workspace]" in j.tab_bar(screen) and "INPUT TO AGENT" in screen)
    j.keys(b"hello\r")
    j.see("Discuss the plan; type draft")
    j.record("U3.a", "typed input reaches the agent and its reply is shown in place", True)
    used = [c["command"] for c in j.proof if "command" in c and str(j.hydra) == c["command"][0]]
    j.record("U3.a", "no init, spawn, export or plan file was required of the user",
             not any(c[1] in ("init", "spawn") for c in used) and j.repo_state() == before,
             detail="hydra commands run by the case: " + "; ".join(" ".join(c[1:3]) for c in used))
    j.record("U3.a", "the work was created only when the conversation was requested",
             bool(j.tmux("list-sessions")) and '"branch"' in j.cli("list", "--json"))
    j.keys(b"\x02\t")
    j.attached = False
    j.see("HYDRA /")


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
                 "HYDRA /" in screen and "[Workspace]" in screen.splitlines()[1] and "INPUT TO AGENT" in screen,
                 evidence=["pty-spawn/final-screen.txt"])
        terminal.send(b"\x02\t")
        time.sleep(0.5)
        screen = terminal.wait_for("Keys go to Hydra", timeout=10)
        j.record("U3.b", "leaving agent input keeps the user in the control centre, not in a bare provider",
                 "HYDRA /" in screen and "Typing goes to the agent" not in screen.splitlines()[-1])
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
    j.see("Task name:")
    j.keys(b"needs-codex\r")
    screen = j.see("Agent profile")
    j.record("U3.c", "the profile chooser shows which agents are available before one is chosen",
             "AVAILABLE" in screen and any("codex" in row and " no " in row for row in screen.splitlines()))
    j.keys(b"codex\r")
    j.see("Objective")
    j.keys(b"Add a greeting\r")
    screen = j.see_prose("is not available on PATH")
    j.record("U3.c", "a missing provider is reported inside the control centre with a next step",
             "Next:" in screen and "HYDRA /" in screen and "Enter or Esc closes" in j.hint(screen))
    j.keys(b"\x1b")
    screen = j.see_any("n new task", "Tab pane")
    j.record("U3.c", "closing the message stays in the control centre; no head was created for the failed launch",
             "Enter or Esc closes" not in j.hint(screen) and "HYDRA /" in screen
             and "No active Hydra heads" in j.cli("list"))


@case("u3-missing-auth", ("U3.c",))
def missing_auth(j: Case) -> None:
    profile_fixture(j, script=j.source / "tests/fixtures/acceptance/noauth-provider.sh")
    j.open()
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"needs-login\r")
    j.see("Agent profile")
    j.keys(b"codex\r")
    j.see("Objective")
    j.keys(b"Add a greeting\r")
    screen = j.see("Not logged in. Run: codex login")
    j.attached = True
    j.record("U3.c", "the provider's own sign-in instruction is visible inside the control centre",
             "HYDRA /" in screen)
    deadline = time.monotonic() + 15
    pane = ""
    while "this pane is now a shell" not in pane and time.monotonic() < deadline:
        pane = pane_text(j.screen(), "Not logged in")
        time.sleep(0.2)
    j.record("U3.c", "Hydra explains that the agent exited and that the pane is a shell where the user can act",
             "exited with status 1; this pane is now a shell" in pane, detail=pane[:300])
    j.keys(b"printf ok > login-proof\r")
    worktree = Path(j.cli("path", "needs-login"))
    j.record("U3.c", "the pane is usable for the sign-in step (typed command runs in the head's worktree)",
             j.wait_file(worktree / "login-proof"))
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"\x1b\x1b1")
    j.see("[Work]")
    j.keys(b"\r")
    details = j.see("Details: needs-login")
    j.record("U3.c", "Details or Work say the agent exited instead of presenting a running codex agent",
             "exited" in details.lower() or "not running" in details.lower() or "sign" in details.lower(),
             detail="desired behaviour: the head is labelled as if the agent were running")
