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
BACK = ESC + ESC  # back to the Work tab from any view


class Session:
    """A capture session: a driver plus the states it recorded."""

    def __init__(self, driver, flow: str, state_dir: Path, record) -> None:
        self.driver = driver
        self.flow = flow
        self.state_dir = state_dir
        self.record = record

    def keys(self, text: str) -> None:
        self.driver.send(text)

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
    """Planning, running, finished and failed work; details; attention; review."""
    s.state("workspace-planning", "PLAN TOGETHER", "SELECTED WORK",
            note="the default Workspace view with a live planning head selected")
    s.state("work-list", "Heads in this project", "plan run kill-dry-run", keys="1")
    s.state("work-run-expanded", "worker   kill-dry-run-worker", keys="jl")
    s.state("overview-running", "RUN / plan run kill-dry-run", "Running step implement", keys="o")
    s.fixture("workflow", FIXTURES / "workflow-runs.tsv")
    s.state("overview-finished", "holds the result", timeout=20,
            note="the same run after it succeeded (fixture switched; the TUI refreshed)")
    s.fixture("workflow", LOCAL / "workflow-runs-failed.tsv")
    s.state("overview-failed", "Step verify failed on kill-dry-run-check", timeout=20)
    s.fixture("workflow", FIXTURES / "workflow-runs.tsv")
    s.state("worker-details", "Details: kill-dry-run-worker", "headless", keys="1kkkjlj\r", timeout=20, group="detail")
    s.state("technical-details", "Technical details", "IDENTITY", keys="d", group="diagnostic")
    s.keys("d")
    s.state("details-terminal-gone", "Details: notes-cleanup", "terminal gone", keys=ESC + "jjjj\r", group="detail")
    s.state("coordination", "COORDINATION", keys="c")
    s.state("recovery", "RECOVERY", "Terminal stopped", keys=BACK + "5")
    s.state("recovery-finding", "Inspect:", "kept", keys="d", group="diagnostic")
    s.keys("d")
    s.state("workflows", "HYDRA / WORKFLOWS", "kill-dry-run", keys="w")
    s.state("statistics", "HYDRA / STATISTICS", "RUNS", keys=BACK + "7")
    s.state("attention-queue", "ATTENTION  2 need you", "Approval needed", keys="I",
            note="an approval waiting for the user: the waiting-for-input state")
    s.state("attention-detail", "ATTENTION DETAIL", "Approval needed", keys="\r")
    s.keys(ESC)
    s.fixture("attention", LOCAL / "attention-failure.tsv")
    s.refresh("I", "Check failed", "repo-check")
    s.state("attention-failure", "Check failed", "repo-check", note="a failed check leads the queue")
    s.fixture("attention", LOCAL / "attention-review-ready.tsv")
    s.fixture("review", LOCAL / "review-result.tsv")
    s.refresh("I", "review-ready")
    s.state("review-result", "RESULT  add-kill-dry-run", "verdict PASS", keys="r")
    s.state("review-verified", "HOW IT WAS VERIFIED", "Full log: press L", keys="nn")
    s.state("review-changes", "CHANGES ON kill-dry-run-worker", keys="n")
    s.state("review-identity", "selected identity", "Source: workflow records", keys="i", group="detail")
    s.keys("i")
    s.fixture("review", FIXTURES / "review-v1.tsv")
    s.keys(ESC)
    s.refresh("I", "review-ready")
    s.state("review-evidence", "EXACT REVIEW / evidence", "readiness: ready", keys="r",
            note="the bounded evidence document with references")
    s.state("help", "KEYBOARD HELP", keys=BACK + ESC + "?")
    s.keys("?")
    s.state("hosts-hub", "REMOTE HOSTS", "+ Add a host", keys="H")


def fleet(s: Session) -> None:
    """Fleet navigation: remote heads, overview, hosts, recovery, attention."""
    s.state("fleet-workspace", "HYDRA /", "[Workspace]")
    s.state("fleet-heads", "Remote heads", "feature-build", keys="1")
    s.state("fleet-overview", "REMOTE HEADS / desired state", keys="o")
    s.state("fleet-hosts", "HOSTS / latest list response", "builder", keys="H")
    s.state("fleet-recovery", "RECOVERY", keys=BACK + "6", note="Fleet tabs: Hosts sits before Attention, so Recovery is 6")
    s.state("fleet-attention", "ATTENTION", "need you", keys="I")


def hosts(s: Session) -> None:
    """The 2.9.0 guided remote setup through the Hosts tab (recorded envelopes)."""
    s.state("hosts-entry", "press H for Hosts")
    s.state("hosts-list", "+ Add a host", keys="H")
    s.state("hosts-form", "ADD A REMOTE HOST", "deploy@ovh.example.net", keys="A" + "deploy@ovh.example.net\tovh")
    s.state("hosts-trust-key", "TRUST THE HOST KEY OF ovh", "known_hosts", keys="\r\r")
    s.state("hosts-trust-key-wrong", "Type yes (the whole word)", keys="no\r")
    s.state("hosts-requirements-blocked", "MISSING REQUIREMENTS ON ovh", "BLOCKS", keys="\x7f\x7fyes\r")
    s.flag("git_fixed")
    s.state("hosts-heads-paused", "HEADS CANNOT RUN ON ovh YET", "Enter continues setup anyway", keys="\r")
    s.state("hosts-provision-plan", "REVIEW: INSTALL HYDRA ON ovh", "pinned: its digest is recorded", keys="\r")
    s.state("hosts-provision-declined", "Not approved: nothing was changed", keys="n")
    s.state("hosts-provision-plan-again", "REVIEW: INSTALL HYDRA ON ovh", keys="\r")
    s.state("hosts-agents", "AGENTS ON ovh", "Install claude", keys="y", timeout=15)
    s.state("hosts-install-plan", "REVIEW: INSTALL claude ON ovh", "curl -fsSL https://claude.ai/install.sh", keys="j\r")
    s.state("hosts-sign-in-handoff", "SIGN IN TO CLAUDE ON OVH", keys="y", timeout=15)
    s.state("hosts-sign-in-terminal", "FAKE SIGN-IN: open", "Hydra: Sign in to claude", keys="\r")
    s.state("hosts-sign-in-return", "Press Enter to return to Hydra", keys="\r")
    s.state("hosts-sign-in-failed", "Sign-in did not finish", "Enter signs in again", keys="\r")
    s.keys("\r")
    s.driver.wait_for("FAKE SIGN-IN: open", timeout=10)
    s.state("hosts-done", "ovh IS READY", keys="\r", timeout=15)
    s.state("hosts-after", "+ Add a host", "set up", keys="\r")


def entry(s: Session) -> None:
    """The control centre is up (keys sent earlier would be lost)."""
    s.driver.wait_for("press H for Hosts", timeout=15)


def hosts_changed_key(s: Session) -> None:
    entry(s)
    s.state("hosts-key-changed", "THE HOST KEY CHANGED", "ssh-keygen -R", keys="HA" + "deploy@ovh.example.net\tovh\r\r", timeout=15)


def hosts_unfinished(s: Session) -> None:
    entry(s)
    s.state("hosts-unfinished-list", "Check requirements: blocked", keys="H", timeout=15)
    s.state("hosts-unfinished", "Check requirements: blocked", "e edit", keys="j")
    s.state("hosts-remove-plan", "REMOVE THE SETUP RECORD OF ovh?", "Left on remote", keys="x")
    s.state("hosts-kept", "Kept the setup record for ovh", keys="n")
    s.state("hosts-duplicate", "ovh ALREADY HAS AN UNFINISHED SETUP", keys="A" + "deploy@ovh.example.net\tovh\r\r")
    s.state("hosts-edit-form", "EDIT THE SETUP OF ovh", "deploy@ovh.example.net", keys="e")


def hosts_running(s: Session) -> None:
    entry(s)
    s.state("hosts-running", "host key…", "c cancel", keys="HA" + "deploy@ovh.example.net\tovh\r\r", timeout=15,
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
