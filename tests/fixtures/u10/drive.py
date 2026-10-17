#!/usr/bin/env python3
"""Drive the control centre's Hosts tab over a PTY for tests/test_u10_loopback.sh.

The TUI under test runs the real `hydra remote ...` CLI against a private
loopback sshd; this driver only presses keys, waits for screens and records
them as evidence. Journeys (one per invocation):

  onboard    add the host, trust its key (typed yes), review and approve the
             install, install the fixture agent and record its location, then
             a sign-in that is killed mid-way, resumed without re-running,
             fails verification once and succeeds on retry; ends on IS READY
  missing    add a host whose sshd hides git: the explained requirement and
             its suggestion; after the shell restores git, continue to the
             install plan and decline it
  again      set the same account up under a second name: the plan is
             reviewed again and the identical pin is reused, no agents
  reconnect  open the Fleet view and find the launched task and the host

Environment: HYDRA_TUI_BIN, HYDRA_CELLS_BIN, U10_EVIDENCE (directory),
U10_SSH_CONFIG, U10_FINGERPRINT, U10_HOLD (the fixture's hold file),
U10_LOGIN_LOG (the remote account's login log), U10_SYNC (hand-shake dir),
U10_JOURNEY_LABEL (the evidence file prefix, the journey's name by default)
and everything the TUI needs (HYDRA_HOME, HYDRA_BIN_CMD, HYDRA_FLEET_BIN,
HYDRA_REMOTE_SETUP_BINARY, PATH, HOME, TERM).
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts/visual_review"))
from driver import Driver  # noqa: E402

ESC = "\x1b"
BACKSPACE = "\x7f"


class Journey:
    def __init__(self, name: str, extra: list[str], columns: int = 140, rows: int = 40) -> None:
        self.name = os.environ.get("U10_JOURNEY_LABEL") or name
        self.evidence = Path(os.environ["U10_EVIDENCE"])
        self.evidence.mkdir(parents=True, exist_ok=True)
        self.count = 0
        self.failures = 0
        env = dict(os.environ)
        for key in ("CI", "HYDRA_NONINTERACTIVE", "TMUX", "TMUX_PANE"):
            env.pop(key, None)
        env.setdefault("TERM", "xterm-256color")
        self.driver = Driver([os.environ["HYDRA_TUI_BIN"], *extra], Path(os.environ["HYDRA_CELLS_BIN"]),
                             Path.cwd(), env, columns, rows)

    def keys(self, text: str) -> None:
        self.driver.send(text)

    def see(self, label: str, *markers: str, timeout: float = 30.0) -> str:
        try:
            screen = self.driver.wait_for(*markers, timeout=timeout)
        except (TimeoutError, RuntimeError) as error:
            self.count += 1
            (self.evidence / f"{self.name}-{self.count:02d}-{label}-FAILED.txt").write_text(str(error))
            raise
        self.driver.settle(quiet=0.3)
        self.count += 1
        screen = self.driver.text()
        (self.evidence / f"{self.name}-{self.count:02d}-{label}.txt").write_text(screen)
        print(f"[PASS] {self.name}: {label}: {' / '.join(markers)}", flush=True)
        return screen

    def check(self, condition: bool, message: str) -> None:
        print(f"[{'PASS' if condition else 'FAIL'}] {self.name}: {message}", flush=True)
        if not condition:
            self.failures += 1

    def quit(self) -> int:
        # Esc keys first, then q on its own: an Esc followed at once by a
        # letter would be read as one Alt sequence.
        self.keys(ESC + ESC + ESC)
        self.driver.settle(quiet=0.3)
        code = self.driver.close("q", timeout=10)
        self.check(code == 0, f"the control centre exited cleanly (exit {code})")
        return self.failures


def login_log() -> list[str]:
    path = Path(os.environ["U10_LOGIN_LOG"])
    return path.read_text().split() if path.exists() else []


def setup_state(name: str) -> dict:
    return json.loads((Path(os.environ["HYDRA_HOME"]) / "fleet/setup" / f"{name}.json").read_text())


def kill_step(pattern: str) -> list[int]:
    """SIGKILL the running setup step (the hydra-fleet child the control centre
    started) and its ssh child, like a client that dies mid-step."""
    found = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True, check=False).stdout.split()
    pids = [int(p) for p in found if int(p) != os.getpid()]
    children = []
    for pid in pids:
        kids = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True, check=False).stdout.split()
        children += [int(k) for k in kids]
    for pid in pids + children:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    return pids


def add_host(j: Journey, destination: str, name: str) -> None:
    j.see("entry", "press H for Hosts")
    j.keys("H")
    j.see("hosts", "+ Add a host")
    j.keys("A")
    j.see("form", "ADD A REMOTE HOST")
    j.keys(f"{destination}\t{name}\t{os.environ['U10_SSH_CONFIG']}")
    j.see("form-filled", destination, os.environ["U10_SSH_CONFIG"])
    j.keys("\r")


def trust_key(j: Journey, name: str) -> None:
    screen = j.see("trust-key", f"TRUST THE HOST KEY OF {name}", "known_hosts", timeout=60)
    j.check(os.environ["U10_FINGERPRINT"] in screen, "the fingerprint shown is the loopback sshd's own")
    j.keys("no\r")
    j.see("trust-key-wrong-word", "Type yes (the whole word)")
    j.keys(BACKSPACE + BACKSPACE + "yes\r")


def onboard() -> int:
    j = Journey("onboard", [])
    add_host(j, "u10", "u10")
    trust_key(j, "u10")
    screen = j.see("provision-plan", "REVIEW: INSTALL HYDRA ON u10", timeout=120)
    j.check("Not a release build" in screen and "--binary" in screen,
            "the plan says the helper passed with --binary is not a release build")
    j.keys("n")
    j.see("provision-declined", "Not approved: nothing was changed")
    j.keys("\r")
    j.see("provision-plan-again", "REVIEW: INSTALL HYDRA ON u10", timeout=120)
    j.keys("y")
    j.see("agents", "AGENTS ON u10", "Install codex", timeout=300)
    for _ in range(12):
        j.keys("j")
        j.driver.settle(quiet=0.2)
        if "> Install codex" in j.driver.text():
            break
    j.see("agents-codex-selected", "> Install codex")
    j.keys("\r")
    screen = j.see("install-plan", "REVIEW: INSTALL codex ON u10", timeout=60)
    j.check("Recipe" in screen and "built into" not in screen, "the installer plan comes from the private local recipe")
    j.keys("y")
    j.see("record-location", "Record this location so Hydra uses it?", timeout=120)
    j.keys("y\r")
    j.see("sign-in-handoff", "SIGN IN TO CODEX ON U10", timeout=120)
    j.keys("\r")
    j.see("sign-in-terminal", "U10 FIXTURE SIGN-IN: device code", timeout=60)
    time.sleep(0.5)
    killed = kill_step("remote sign-in u10 --agent codex")
    j.check(bool(killed), f"the running sign-in step was killed mid-way (pids {killed})")
    j.see("sign-in-return", "Press Enter to return to Hydra", timeout=60)
    j.keys("\r")
    j.see("interrupted", "The step was interrupted", "Enter continues", timeout=60)
    before = login_log()
    j.check(before == ["start"], f"the remote login ran once and did not finish: {before}")
    state = setup_state("u10")
    j.check(state["steps"]["sign_in:codex"]["status"] == "in_progress", "the interrupted sign-in is recorded as in progress")
    os.unlink(os.environ["U10_HOLD"])
    j.keys("\r")
    screen = j.see("resumed-not-verified", "Sign-in did not finish", "Enter signs in again", timeout=120)
    j.check(login_log() == ["start"], "resuming verified the interrupted sign-in without running it again")
    state = setup_state("u10")
    error = state["steps"]["sign_in:codex"].get("detail", {}).get("error", {})
    j.check(state["steps"]["sign_in:codex"]["status"] == "failed" and error.get("code") == "sign_in_failed",
            f"the CLI recorded the failed verification: {error}")
    j.keys("\r")
    j.see("sign-in-terminal-2", "U10 FIXTURE SIGN-IN: finished", timeout=60)
    j.see("sign-in-return-2", "Press Enter to return to Hydra", timeout=60)
    j.keys("\r")
    screen = j.see("sign-in-failed", "Sign-in did not finish", "Enter signs in again", timeout=60)
    j.check("not signed in" in screen, "the error screen explains the recorded verification failure")
    j.check(login_log() == ["start", "start", "done"], f"the second attempt completed and failed verification: {login_log()}")
    j.keys("\r")
    j.see("sign-in-terminal-3", "U10 FIXTURE SIGN-IN: finished", timeout=60)
    j.see("done", "u10 IS READY", timeout=180)
    j.check(login_log() == ["start", "start", "done", "start", "done"], f"the retry signed in: {login_log()}")
    j.keys("\r")
    j.see("hosts-after", "+ Add a host", timeout=30)
    return j.quit()


def missing() -> int:
    sync = Path(os.environ["U10_SYNC"])
    j = Journey("missing", [])
    add_host(j, "u10x", "u10x")
    trust_key(j, "u10x")
    screen = j.see("requirements-blocked", "MISSING REQUIREMENTS ON u10x", "install git", timeout=90)
    j.check("sudo apt-get install git" in screen, "the missing requirement carries its suggested command")
    j.check("BLOCKS" in screen, "the requirement is marked as blocking")
    (sync / "blocked").write_text("")
    deadline = time.monotonic() + 60
    while not (sync / "fixed").exists() and time.monotonic() < deadline:
        time.sleep(0.2)
    j.check((sync / "fixed").exists(), "the shell restored git on the remote")
    j.keys("\r")
    j.see("plan-after-fix", "REVIEW: INSTALL HYDRA ON u10x", timeout=120)
    j.keys("n")
    j.see("declined", "Not approved: nothing was changed")
    return j.quit()


def again() -> int:
    j = Journey("again", [])
    add_host(j, "u10", "u10-again")
    j.see("provision-plan", "REVIEW: INSTALL HYDRA ON u10-again", timeout=120)
    j.keys("y")
    j.see("agents", "AGENTS ON u10-again", timeout=300)
    j.keys("\r")
    j.see("done", "u10-again IS READY", timeout=180)
    j.keys("\r")
    j.see("hosts-after", "+ Add a host", timeout=30)
    return j.quit()


def reconnect() -> int:
    j = Journey("reconnect", ["--fleet"])
    j.see("fleet-entry", "HYDRA /", timeout=60)
    j.keys("1")
    j.see("remote-heads", "Remote heads", "u10-task", timeout=90)
    j.keys("H")
    j.see("hosts", "HOSTS / latest list response", "u10", timeout=60)
    return j.quit()


JOURNEYS = {"onboard": onboard, "missing": missing, "again": again, "reconnect": reconnect}


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in JOURNEYS:
        print(f"usage: drive.py {'|'.join(JOURNEYS)}", file=sys.stderr)
        return 2
    try:
        failures = JOURNEYS[sys.argv[1]]()
    except (TimeoutError, RuntimeError) as error:
        print(f"[FAIL] {sys.argv[1]}: {error}", flush=True)
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
