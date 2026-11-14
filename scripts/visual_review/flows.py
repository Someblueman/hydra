"""The screen states the visual review walks through, per flow.

Every state is reached with real key presses through the installed native
TUI over a PTY, fed only by the deterministic fixtures under
tests/fixtures/tui (and the few synthetic ones beside this module). A state
records the cumulative byte stream up to a settled frame; its markers are the
text that must be on screen before the frame counts as that state.

Groups: ``detail`` and ``diagnostic`` screens may show raw identifiers; the
lint exempts them from the identifier rule (docs/VISUAL-REVIEW.md).
"""

from __future__ import annotations

import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FIXTURES = ROOT / "tests/fixtures/tui"
LOCAL = Path(__file__).resolve().parent / "fixtures"

ESC = "\x1b"
CTRL_R = "\x12"  # read everything again


class Session:
    """A capture session: a driver plus the states it recorded."""

    def __init__(self, driver, flow: str, state_dir: Path, record) -> None:
        self.driver = driver
        self.flow = flow
        self.state_dir = state_dir
        self.record = record

    def keys(self, text: str) -> None:
        self.driver.send(text)

    def escape(self) -> None:
        """One Esc on its own: an Esc followed at once by another key reads
        as that key with Alt held."""
        self.driver.pump(0.2)
        self.driver.send(ESC)
        self.driver.pump(0.3)

    def fixture(self, kind: str, path: Path) -> None:
        """Serve ``path`` for the next ``hydra ... <kind>-data`` call."""
        (self.state_dir / kind).write_text(str(path))

    def flag(self, name: str) -> None:
        (self.state_dir / name).write_text("")

    def state(self, name: str, *markers: str, keys: str | None = None, timeout: float = 12.0,
              group: str = "", absent: str | None = None, note: str = "") -> str:
        if keys:
            self.keys(keys)
        screen = self.driver.wait_for(*markers, timeout=timeout, absent=absent)
        self.driver.settle()
        self.record(self.flow, name, group, list(markers), note)
        return screen

    def page(self, key: str, *markers: str, presses: int = 5) -> str:
        """Press ``key`` (a page key) until every marker is on screen."""
        for _ in range(presses):
            try:
                return self.driver.wait_for(*markers, timeout=1.0)
            except TimeoutError:
                self.keys(key)
        return self.driver.wait_for(*markers, timeout=2.0)

    def select(self, text: str, presses: int = 8) -> None:
        """Move a list's selection (the row with the bar) to the row showing
        ``text``: up to the top first, then down."""
        def selected() -> bool:
            self.driver.settle()
            return any(line.lstrip().startswith("\u258c") and text in line
                       for line in self.driver.text().splitlines())
        for key in ("k", "j"):
            for _ in range(presses):
                if selected():
                    return
                self.keys(key)
        raise TimeoutError(f"no list row showing {text!r} could be selected")

    def refresh(self, key: str, *markers: str, timeout: float = 15.0) -> str:
        """Press ``key`` until every marker is on screen (fixture reloads)."""
        deadline = time.monotonic() + timeout
        while True:
            self.keys(key)
            try:
                return self.driver.wait_for(*markers, timeout=2.5)
            except TimeoutError:
                if time.monotonic() >= deadline:
                    raise


def work(s: Session) -> None:
    """Work, its Run and Details pages, Inbox with its pages and review,
    Stats, help, the palette and the confirmation dialogs."""
    s.state("work-list", "Heads", "add-kill", "plan run kill",
            note="the default Work screen: a live planning head selected, its latest output beside the list")
    s.state("work-run-expanded", "worker kill-d", "Running step implement", keys="jl",
            note="a run expanded under its planning head; the side pane shows its Run page (graph, steps, evidence)")
    s.fixture("workflow", FIXTURES / "workflow-runs.tsv")
    s.state("run-finished", "succeeded · plan run kill-dry-run", "Worker branch", "holds", timeout=20,
            note="the same run after it succeeded (fixture switched; the TUI refreshed)")
    s.fixture("workflow", LOCAL / "workflow-runs-failed.tsv")
    s.state("run-failed", "failed · plan run kill-dry-run", "Step verify failed on", timeout=20)
    s.fixture("workflow", FIXTURES / "workflow-runs.tsv")
    s.keys("j")
    s.page("]", "succeeded · plan run kill-dry-run", "kill-dry-run-worker")
    s.state("run-head", "succeeded · plan run kill-dry-run", "kill-dry-run-worker",
            note="a head the run created, on its Run page")
    s.page("]", "AGENT STEP", "headless")
    s.state("worker-details", "AGENT STEP", "headless", group="detail")
    s.state("technical-details", "IDENTITY", "LIFECYCLE", keys="d", group="diagnostic")
    s.keys("d")
    s.keys("jj")
    s.page("]", "The terminal is gone", "files are kept")
    s.state("details-terminal-gone", "The terminal is gone", "files are kept", group="detail")
    s.state("remove-confirm", "Remove notes-cleanup?", "branch", keys="x", note="the destructive confirm dialog")
    s.escape()
    s.state("palette", "Commands", "type to filter", keys=":")
    s.escape()
    s.state("inbox", "Needs you", "Approve S-1: T-1", "Terminal stopped: notes-cleanup", keys="2",
            note="decisions first, then results, stopped terminals and repairs")
    s.state("inbox-detail", "What is waiting", "Y Approve", keys="\r")
    s.state("approve-confirm", "Approve S-1?", "y approve", keys="Y",
            note="approval is a dialog naming the request; nothing is typed")
    s.escape()
    s.escape()
    s.select("Terminal stopped: notes-cleanup")
    s.state("inbox-terminal-stopped", "is no longer running", "R Restart terminal", keys="\r",
            note="a stopped terminal: what happened, what is kept, and the actions")
    s.page("[", "DIAGNOSTICS", "dead-session")
    s.state("inbox-technical", "DIAGNOSTICS", "dead-session", group="diagnostic")
    s.escape()
    s.fixture("attention", LOCAL / "attention-failure.tsv")
    s.refresh(CTRL_R, "Check failed", "repo-check")
    s.state("inbox-failure", "Check failed", "repo-check", note="a failed check leads the list")
    s.fixture("attention", LOCAL / "attention-review-ready.tsv")
    s.fixture("review", LOCAL / "review-result.tsv")
    s.refresh(CTRL_R, "Result ready: R-1")
    s.select("Result ready: R-1")
    s.state("review-result", "RESULT  add-kill-dry-run", "verdict PASS", keys="r")
    s.state("review-verified", "HOW IT WAS VERIFIED", "Full log: press L", keys="nn")
    s.state("review-changes", "CHANGES ON kill-dry-run-worker", keys="n")
    s.page("]", "IDENTITY", "Request")
    s.state("review-identity", "IDENTITY", "Request", group="detail")
    s.fixture("review", FIXTURES / "review-v1.tsv")
    s.escape()
    s.refresh(CTRL_R, "Result ready: R-1")
    s.select("Result ready: R-1")
    s.state("review-evidence", "EXACT REVIEW / evidence", "readiness: ready", keys="r",
            note="the bounded evidence document with references")
    s.escape()
    s.state("statistics", "Workflow runs", "kill-dry-run", keys="4")
    s.state("help", "KEYBOARD HELP", keys="?")
    s.keys("?")
    s.state("hosts-hub", "This machine", "+ Add a host", keys="3")


def fleet(s: Session) -> None:
    """Fleet mode: remote heads, their details, hosts, Inbox and Stats."""
    s.state("fleet-work", "Heads · remote work", "builder/feature-bui")
    s.page("]", "Where", "Liveness")
    s.state("fleet-details", "Where", "Liveness", note="a remote head's Details page")
    s.state("fleet-hosts", "Observed 3", "builder", keys="3")
    s.state("fleet-inbox", "Repairs", "fleet: offline", "Approve S-1", keys="2",
            note="one Inbox for remote decisions and host problems")
    s.state("fleet-stats", "latest fleet response", "offline", keys="4")


def hosts(s: Session) -> None:
    """The 2.9.0 guided remote setup through the Hosts tab (recorded envelopes)."""
    s.state("hosts-entry", "1 Work")
    s.state("hosts-list", "+ Add a host", "This machine", keys="3")
    s.state("hosts-form", "Hydra connects with your own SSH", "deploy@ovh.example.net", keys="A" + "deploy@ovh.example.net\tovh")
    s.state("hosts-trust-key", "Trust the host key of ovh?", "known_hosts", keys="\r\r")
    s.state("hosts-trust-key-wrong", "Type yes (the whole word)", keys="no\r")
    s.state("hosts-requirements-blocked", "Missing requirements on ovh", "BLOCKS", keys="\x7f\x7fyes\r")
    s.flag("git_fixed")
    s.state("hosts-heads-paused", "Heads cannot run on ovh yet", "Enter continues setup anyway", keys="\r")
    s.state("hosts-provision-plan", "Install Hydra on ovh", "pinned: its digest is recorded", keys="\r")
    s.state("hosts-provision-declined", "Not approved: nothing was changed", keys="n")
    s.state("hosts-provision-plan-again", "Install Hydra on ovh", keys="\r")
    s.state("hosts-agents", "Agents on ovh", "Install claude", keys="y", timeout=15)
    s.state("hosts-install-plan", "Install claude on ovh", "curl -fsSL https://claude.ai/install.sh", keys="j\r")
    s.state("hosts-sign-in-handoff", "Sign in to claude on ovh", keys="y", timeout=15)
    s.state("hosts-sign-in-terminal", "FAKE SIGN-IN: open", "Hydra: Sign in to claude", keys="\r")
    s.state("hosts-sign-in-return", "Press Enter to return to Hydra", keys="\r")
    s.state("hosts-sign-in-failed", "Sign-in did not finish", "Enter signs in again", keys="\r")
    s.keys("\r")
    s.driver.wait_for("FAKE SIGN-IN: open", timeout=10)
    s.state("hosts-done", "ovh is ready", keys="\r", timeout=15)
    s.state("hosts-after", "+ Add a host", "set up", keys="\r")


def entry(s: Session) -> None:
    """The control centre is up (keys sent earlier would be lost)."""
    s.driver.wait_for("1 Work", timeout=15)


def hosts_changed_key(s: Session) -> None:
    entry(s)
    s.state("hosts-key-changed", "The host key changed", "ssh-keygen -R", keys="3A" + "deploy@ovh.example.net\tovh\r\r", timeout=15)


def hosts_unfinished(s: Session) -> None:
    entry(s)
    s.state("hosts-unfinished-list", "Check requirements: blocked", keys="3", timeout=15)
    s.state("hosts-unfinished", "Check requirements: blocked", "e edit", keys="j")
    s.state("hosts-remove-plan", "Remove the setup record of ovh?", "Left on remote", keys="x")
    s.state("hosts-kept", "Kept the setup record for ovh", keys="n")
    s.state("hosts-duplicate", "ovh already has an unfinished setup", keys="A" + "deploy@ovh.example.net\tovh\r\r")
    s.state("hosts-edit-form", "Edit the setup of ovh", "deploy@ovh.example.net", keys="e")


def hosts_running(s: Session) -> None:
    entry(s)
    s.state("hosts-running", "host key…", "c cancel", keys="3A" + "deploy@ovh.example.net\tovh\r\r", timeout=15,
            note="a step in progress with its elapsed time (time-varying)")
    s.state("hosts-cancelled", "Cancelled: Checking the host key", "Continue setup", keys="c")


FLOWS = {
    "work": (work, {}),
    "fleet": (fleet, {}),
    "hosts": (hosts, {"flags": ["needs_git", "no_tmux", "fail_sign_in"]}),
    "hosts-changed-key": (hosts_changed_key, {"flags": ["changed"]}),
    "hosts-unfinished": (hosts_unfinished, {"flags": ["early"]}),
    "hosts-running": (hosts_running, {"flags": ["slow_host_key"]}),
}
