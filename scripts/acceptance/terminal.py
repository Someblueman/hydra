"""U2: clean terminal entry and human-readable context, on real tmux clients."""

from __future__ import annotations

import os
import re
import time
from pathlib import Path

from acceptance.harness import RAW_ID, Case, case, profile_fixture

BOOTSTRAP = re.compile(r"export HYDRA_|HYDRA_PROJECT_ID=|HYDRA_HEAD_ID=|set-environment|HYDRA_INSTANCE_ID=")


def hydra_lines(screen: str) -> list[str]:
    """Lines Hydra itself prints into the terminal (banner and notes)."""
    return [line.strip() for line in screen.splitlines()
            if line.strip().startswith(("Hydra head ", "Details: hydra", "Hydra: "))]


def transcript_checks(j: Case, bullet: str, label: str, screen: str, session: str, width: int,
                      banner_expected: bool = True) -> str:
    scrollback = j.tmux("capture-pane", "-p", "-J", "-S", "-", "-t", session)
    kept = j.keep(f"{label}-scrollback.txt", scrollback)
    j.record(bullet, f"{label}: no bootstrap export or set-environment command in the visible transcript",
             not BOOTSTRAP.search(screen), evidence=[kept])
    j.record(bullet, f"{label}: no bootstrap command in the pane's scrollback",
             not BOOTSTRAP.search(scrollback), evidence=[kept])
    banner = hydra_lines(screen if banner_expected else scrollback)
    heads = [line for line in banner if line.startswith("Hydra head ")]
    j.record(bullet, f"{label}: Hydra's context is a concise one-line banner (head, agent, repo) within {width} columns",
             any("repo" in line and len(line) <= width for line in heads), detail=" / ".join(banner)[:300])
    j.record(bullet, f"{label}: no raw head, instance or project identity in Hydra's lines",
             not any(RAW_ID.search(line) for line in banner))
    return scrollback


def agent_environment(j: Case, session: str, branch: str) -> dict:
    pane = j.pane_pids(session)[0]
    candidates = [pane, *j.children(pane)]
    agent = next((p for p in candidates if "provider.py" in j.command_line(p) or "codex" in j.command_line(p)), None)
    env = j.environ(agent or pane)
    return {"pane_pid": pane, "agent_pid": agent, "agent_cmdline": j.command_line(agent or pane),
            "env": {k: v for k, v in env.items() if k.startswith("HYDRA_")}}


@case("u2-spawn-reconnect", ("U2.a", "U2.b", "U2.c"), columns=(80, 140))
def spawn_reconnect(j: Case) -> None:
    profile_fixture(j)
    shell = "/bin/bash" if os.path.exists("/bin/bash") else "/bin/sh"
    j.env.update(SHELL=shell, HISTFILE=str(Path(j.env["HOME"]) / ".acceptance_history"), HISTSIZE="500")
    j.cli("init", "--no-agent")
    spawn = j.cli("spawn", "fresh", "--profile", "codex")
    j.keep("spawn.txt", spawn)
    session = j.tmux("list-sessions", "-F", "#{session_name}").split()[0]
    rows = 40 if j.columns > 80 else 24
    client = j.pty("attach", ["tmux", "attach-session", "-t", session], j.columns, rows)
    screen = client.wait_for("ready to discuss", timeout=20)
    attached_at = time.monotonic()
    client.send(b"hello\r")
    client.wait_for("Discuss the plan", timeout=10)
    j.record("U2.a", f"fresh spawn at {j.columns} columns: the agent prompt accepts input immediately",
             True, detail=f"reply within {time.monotonic() - attached_at:.2f}s of the first usable screen",
             evidence=["pty-attach/final-screen.txt"])
    transcript_checks(j, "U2.a", f"fresh-{j.columns}", client.text(), session, j.columns)
    # The agent process really carries the head environment.
    identity = agent_environment(j, session, "fresh")
    environ_evidence = j.keep_json("agent-environ", identity)
    env = identity["env"]
    project_dirs = list((j.base / "state" / "state" / "v2" / "projects").iterdir())
    head_dir = (j.base / "state" / "state" / "v2" / "projects" / env.get("HYDRA_PROJECT_ID", "-") / "heads"
                / env.get("HYDRA_HEAD_ID", "-"))
    j.record("U2.b", "the agent process (read from /proc/<pid>/environ) carries HYDRA_PROJECT_ID, HYDRA_HEAD_ID, "
             "HYDRA_BRANCH and HYDRA_WORKTREE matching the durable state",
             identity["agent_pid"] is not None and len(project_dirs) == 1
             and env.get("HYDRA_PROJECT_ID") == project_dirs[0].name and head_dir.is_dir()
             and env.get("HYDRA_BRANCH") == "fresh" and env.get("HYDRA_WORKTREE") == j.cli("path", "fresh"),
             detail=f"pid {identity['agent_pid']} {identity['agent_cmdline'][:80]}", evidence=[environ_evidence])
    # Leave the agent; the launcher hands over to the interactive shell.
    client.send(b"\x04")
    screen = client.wait_for("this pane is now a shell", timeout=15)
    client.send(b"printf ok > u2-proof\r")
    worktree = Path(j.cli("path", "fresh"))
    j.record("U2.a", "after the agent exits the shell prompt in the worktree is usable",
             j.wait_file(worktree / "u2-proof"))
    transcript_checks(j, "U2.a", f"shell-{j.columns}", client.text(), session, j.columns)
    client.send(b"\x02d")
    receipt = client.close(timeout=10)
    j.record("U2.c", "detaching the real client leaves the session running", receipt["exit"] == 0
             and session in j.tmux("list-sessions", "-F", "#{session_name}"), detail=str(receipt))
    # Reconnect at the other width.
    other = 140 if j.columns == 80 else 80
    client = j.pty("reattach", ["tmux", "attach-session", "-t", session], other, 40 if other > 80 else 24)
    client.quiet(0.5, 10)
    client.send(b"printf ok > u2-proof-2\r")
    j.record("U2.c", f"reconnected at {other} columns: the prompt is usable immediately",
             j.wait_file(worktree / "u2-proof-2"), evidence=["pty-reattach/final-screen.txt"])
    transcript_checks(j, "U2.c", f"reconnect-{other}", client.text(), session, other, banner_expected=False)
    client.send(b"exit\r")
    receipt = client.close(timeout=10)
    histories = j.history_files()
    j.keep_json("history-files", {k: v[-2000:] for k, v in histories.items()})
    j.record("U2.a", "no bootstrap command reached the shell history file",
             not any(BOOTSTRAP.search(text) or "HYDRA_" in text for text in histories.values()),
             detail="history files: " + (", ".join(histories) or "none written by the shell"))
