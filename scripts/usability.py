#!/usr/bin/env python3
"""Bounded installed-product journeys using the existing native PTY observer."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from bench_i1_pty import Observer


def run(argv: list[str], cwd: Path, env: dict[str, str]) -> str:
    result = subprocess.run(
        argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=90, check=False
    )
    if result.returncode:
        raise RuntimeError(
            f"{argv!r}: exit {result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout.strip()


def tree(repo: Path) -> dict[str, str]:
    return {
        str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in repo.rglob("*")
        if p.is_file() and ".git" not in p.relative_to(repo).parts
    }


class Journey:
    def __init__(
        self, source: Path, prefix: Path, output: Path, name: str, columns: int
    ):
        self.output = output / f"{name}-{columns}"
        self.output.mkdir()
        self.base = Path(tempfile.mkdtemp(prefix="hydra-ux.", dir="/tmp"))
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.hydra = prefix / "bin/hydra"
        self.env = {
            k: v
            for k, v in os.environ.items()
            if not k.startswith(("HYDRA_", "GIT_", "TMUX"))
        }
        self.env.update(
            HOME=str(self.base / "home"),
            HYDRA_HOME=str(self.base / "state"),
            TMUX_TMPDIR=str(self.base / "tmux"),
            TMPDIR=str(self.base),
            HYDRA_NONINTERACTIVE="1",
            HYDRA_TEST_PUBLIC_ENTRY="1",
            LC_ALL="C",
            LANG="C",
            TERM="xterm-256color",
        )
        for key in ("HOME", "HYDRA_HOME", "TMUX_TMPDIR"):
            Path(self.env[key]).mkdir()
        self.proof: list[dict] = []
        self.observer: Observer | None = None
        self.attached = False
        self.source, self.prefix, self.columns = source, prefix, columns

    def initialize(self) -> None:
        for args in (
            ["init", "-q"],
            ["config", "user.name", "Usability fixture"],
            ["config", "user.email", "fixture@example.invalid"],
        ):
            self.command("git", *args)
        (self.repo / "README.txt").write_text("Disposable usability fixture.\n")
        self.command("git", "add", "README.txt")
        self.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "fixture")

    def command(self, *argv: str) -> str:
        value = run(list(argv), self.repo, self.env)
        self.proof.append(
            {"command": argv, "output": value, "at_ns": time.monotonic_ns()}
        )
        return value

    def cli(self, *argv: str) -> str:
        return self.command(str(self.hydra), *argv)

    def open(self) -> None:
        self.observer = Observer(
            self.source / "build/test-tui-pty",
            self.prefix / "libexec/hydra/hydra-tui",
            self.hydra,
            self.output / "terminal",
            self.repo,
            self.env,
            self.columns,
            40 if self.columns > 80 else 24,
        )
        self.observer.wait("raw-ready", timeout=15)
        self.see("HYDRA /")

    def see(self, text: str) -> str:
        assert self.observer
        screen, receipt = self.observer.visible(text)
        self.proof.append(
            {"expect": text, "snapshot": receipt["id"], "at_ns": time.monotonic_ns()}
        )
        return screen

    def keys(self, value: bytes) -> None:
        assert self.observer
        self.observer.input(f"action-{len(self.proof)}", value)
        self.proof.append({"keys_hex": value.hex(), "at_ns": time.monotonic_ns()})

    def seed(self) -> None:
        self.cli("init", "--no-agent")
        self.cli("spawn", "sample", "--no-agent")

    def close(self) -> None:
        cleanup = {}
        try:
            if self.observer:
                cleanup = self.observer.close(attached=self.attached)
                if not cleanup.get("reaped") or cleanup.get("observer_exit"):
                    raise RuntimeError(f"UI cleanup incomplete: {cleanup}")
        finally:
            # This root owns the entire socket namespace; no global tmux cleanup.
            for socket in Path(self.env["TMUX_TMPDIR"]).glob("tmux-*/*"):
                subprocess.run(
                    ["tmux", "-S", str(socket), "kill-server"],
                    capture_output=True,
                    timeout=10,
                    check=False,
                )
            (self.output / "proof.json").write_text(
                json.dumps(self.proof, indent=2) + "\n"
            )
            (self.output / "cleanup.json").write_text(
                json.dumps(cleanup, indent=2) + "\n"
            )
            shutil.rmtree(self.base)


def clean_entry(j: Journey) -> None:
    before = tree(j.repo)
    status = j.command("git", "status", "--porcelain")
    j.open()
    j.see("n new task")
    assert tree(j.repo) == before, "Entering Hydra changed repository files"
    assert j.command("git", "status", "--porcelain") == status
    assert not j.cli("list", "--json").count('"branch"'), (
        "Entry created execution resources"
    )
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"first-task\r")
    j.see("Agent profile")
    j.keys(b"none\r")
    j.see("Objective")
    j.keys(b"\r")
    j.see("Typing goes to the agent")
    j.attached = True
    assert Path(j.cli("path", "first-task")).is_dir()
    j.keys(b"printf delivered > input-proof\r")
    marker = Path(j.cli("path", "first-task")) / "input-proof"
    deadline = time.monotonic() + 5
    while not marker.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert marker.read_text() == "delivered"
    marker.unlink()


def navigation(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for key, label in (
        (b"2", "[Details]"),
        (b"3", "[Overview]"),
        (b"4", "[Attention]"),
        (b"5", "[Recovery]"),
        (b"6", "[Workflows]"),
        (b"7", "[Statistics]"),
    ):
        j.keys(key)
        j.see(label)
        j.keys(b"\x1b")
        j.see("[Work]")
    j.keys(b"\t")
    j.see("[Details]")
    j.keys(b"\x1b[Z")
    j.see("[Work]")
    assert Path(j.cli("path", "sample")).is_dir()


def attachment(j: Journey) -> None:
    j.seed()
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    for cycle in range(2):
        j.keys(b"a")
        j.see("Typing goes to the agent")
        j.attached = True
        # Verify input independently on disk, not by an echoed command marker.
        marker = f"input-{cycle}"
        j.keys(f"printf delivered > {marker}\r".encode())
        worktree = Path(j.cli("path", "sample"))
        deadline = time.monotonic() + 5
        while not (worktree / marker).exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert (worktree / marker).read_text() == "delivered"
        (worktree / marker).unlink()
        assert j.observer
        for width, height in (
            (80, 24),
            (140, 40),
            (j.columns, 40 if j.columns > 80 else 24),
        ):
            j.observer.resize(f"resize-{cycle}-{width}", width, height)
            j.see("Ctrl-B")
        j.keys(b"\x02\t")
        j.attached = False
        j.see("HYDRA /")
        j.keys(b"\x02x")
        j.keys(b"\x1b\x1b")
        j.see("[Work]")
        assert j.command("tmux", "list-panes", "-a", "-F", "#{pane_pid}")


def removal(j: Journey) -> None:
    j.seed()
    worktree = Path(j.cli("path", "sample"))
    dirty = worktree / "keep.txt"
    dirty.write_text("must survive\n")
    j.open()
    j.see("sample")
    j.keys(b"1")
    j.see("[Work]")
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("exit 1")
    assert dirty.read_text() == "must survive\n"
    j.keys(b"\x1b")
    dirty.unlink()
    j.keys(b"x")
    j.see("y/N:")
    j.keys(b"y\r")
    j.see("Removed sample")
    assert not worktree.exists(), "Removal did not remove the worktree"
    j.keys(b"\t")
    j.see("[Details]")
    assert j.command("git", "rev-parse", "--verify", "sample"), (
        "Removal deleted the branch"
    )


def planning(j: Journey) -> None:
    # Authoring is supplied through a public interactive profile, never by
    # fabricating Hydra state. The checker independently recompiles revisions.
    for file in (j.source / "tests/fixtures/plan/repo").iterdir():
        shutil.copy2(file, j.repo / file.name)
    j.command("git", "add", ".")
    j.command("git", "-c", "commit.gpgSign=false", "commit", "-qm", "planning inputs")
    j.env["UX_PLAN_TEMPLATE"] = str(j.source / "tests/fixtures/plan/plan.json")
    j.env["PATH"] = str(j.prefix / "bin") + os.pathsep + j.env["PATH"]
    j.cli(
        "agent",
        "init",
        "journey-provider",
        "--executable",
        str(j.source / "tests/fixtures/usability/provider.py"),
        "--prompt-mode",
        "task-file",
    )
    j.open()
    j.keys(b"n")
    j.see("Task name:")
    j.keys(b"conversation\r")
    j.see("Agent profile")
    j.keys(b"journey-provider\r")
    j.see("Objective")
    j.keys(b"Deliver a checked report\r")
    j.see("ready to discuss")
    j.attached = True
    j.keys(b"draft\r")
    j.see("PROPOSAL READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("PLAN OVERVIEW")
    j.keys(b"P")
    j.see("No execution yet")
    j.keys(b"y\r")
    assert "malformed" not in j.see("unvalidated")
    j.keys(b"V")
    j.see("awaiting approval")
    records = Path(j.env["HYDRA_HOME"]) / "state/v2/projects"
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "Validation executed the plan"
    )
    paths = (
        j.cli("workflow", "plan", "proposal", "conversation")
        .splitlines()[1]
        .split("\t")[1:]
    )
    compiled = j.output / "independent-initial.json"
    original = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    # Change the actual draft through the same conversation; old approval must
    # become unusable before any execution is permitted.
    j.keys(b"a")
    j.see("Typing goes to the agent")
    j.attached = True
    j.keys(b"revise\r")
    j.see("PROPOSAL REVISION READY")
    j.keys(b"\x02\t")
    j.attached = False
    j.keys(b"B")
    j.see("unvalidated")
    j.keys(b"E")
    j.see("Validate a local plan")
    assert not list(records.glob("*/workflows/runs/*/state")), (
        "Changed draft reused old approval"
    )
    j.keys(b"V")
    j.see("awaiting approval")
    compiled = j.output / "independent-revised.json"
    revised = json.loads(j.cli("workflow", "plan", "compile", *paths, str(compiled)))[
        "data"
    ]["sha256"]
    assert revised != original
    j.keys(b"E")
    j.see("Type exact digest")
    j.keys((revised + "\r").encode())
    deadline = time.monotonic() + 60
    states = []
    while time.monotonic() < deadline:
        states = list(records.glob("*/workflows/runs/*/state"))
        if states and states[0].read_text().strip() in (
            "succeeded",
            "failed",
            "cancelled",
        ):
            break
        time.sleep(0.1)
    assert len(states) == 1 and states[0].read_text().strip() == "succeeded", (
        "Approved plan did not deliver"
    )
    delivery = json.loads(j.cli("workflow", "plan", "result", states[0].parent.name))
    assert delivery["ok"], "Independent delivery verification failed"
    j.see("HYDRA /")


def report(output: Path, result: dict) -> None:
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    rows = []
    for case in result["journeys"]:
        captures = []
        for path in sorted((output / case["id"]).glob("terminal/snapshot-*.txt")):
            html_path = path.with_suffix(".html")
            html_path.write_text(
                '<meta charset="utf-8"><pre>' + html.escape(path.read_text()) + "</pre>"
            )
            captures.append(
                f'<a href="{html.escape(str(html_path.relative_to(output)))}">{path.stem}</a>'
            )
        rows.append(
            f"<h2>{html.escape(case['id'])}: {case['status']}</h2><p>{html.escape(case.get('reason', ''))}</p>"
            + f'<a href="{case["id"]}/proof.json">Actions and independent checks</a> · '
            + f'<a href="{case["id"]}/terminal/terminal.raw">Terminal bytes</a><p>'
            + " · ".join(captures)
            + "</p>"
        )
    (output / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Hydra usability</title>'
        "<style>body{font:16px system-ui;max-width:1100px;margin:32px auto}pre{font:14px monospace}</style>"
        "<h1>Installed Hydra usability journeys</h1><p>Deterministic local shell fixture. "
        "Captures are escaped text, not a rendering of terminal colors. Raw bytes are retained. "
        "This checks product interaction, not real-provider behavior or novice discoverability.</p>"
        + "".join(rows)
        + "<h2>Separate acceptance still required</h2><p>"
        + html.escape("; ".join(result["unqualified"]))
        + "</p>"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--journey",
        choices=("clean-entry", "navigation", "attachment", "removal", "planning"),
    )
    parser.add_argument(
        "--columns", type=int, choices=(80, 140), nargs="+", default=[80, 140]
    )
    args = parser.parse_args()
    if not __debug__:
        parser.error(
            "Run without Python optimization so outcome assertions remain enabled"
        )
    source = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = output / "installed"
    result = {
        "mode": "installed deterministic shell fixture",
        "journeys": [],
        "unqualified": [
            "Live authenticated provider",
            "Real terminal visual review",
            "Screen-only discoverability trial",
            "Authorized remote host",
        ],
        "commit": run(["git", "rev-parse", "HEAD"], source, dict(os.environ)),
    }
    try:
        install_env = {
            **os.environ,
            "PREFIX": str(prefix),
            "HYDRA_INSTALL_CORE": "required",
            "HYDRA_INSTALL_TUI": "required",
            "HYDRA_INSTALL_FLEET": "required",
            "DESTDIR": "",
        }
        (output / "install.log").write_text(
            run(["sh", "install.sh"], source, install_env)
        )
        result["installed_hashes"] = tree(prefix)
        (output / "source.diff").write_text(
            run(["git", "diff", "HEAD"], source, dict(os.environ))
        )
        result["source_files"] = {
            str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in ("src", "lib", "scripts", "tests/fixtures/usability")
            for p in (source / root).rglob("*")
            if p.is_file()
        }
        for name, function in [
            ("clean-entry", clean_entry),
            ("navigation", navigation),
            ("attachment", attachment),
            ("removal", removal),
            ("planning", planning),
        ]:
            if args.journey and args.journey != name:
                continue
            for columns in args.columns:
                case = {"id": f"{name}-{columns}", "status": "passed"}
                started = time.monotonic()
                j = None
                try:
                    j = Journey(source, prefix, output, name, columns)
                    j.initialize()
                    function(j)
                except (
                    AssertionError,
                    OSError,
                    RuntimeError,
                    TimeoutError,
                    subprocess.SubprocessError,
                ) as error:
                    case.update(status="failed" if j else "blocked", reason=str(error))
                finally:
                    if j:
                        try:
                            j.close()
                        except (
                            OSError,
                            RuntimeError,
                            subprocess.SubprocessError,
                        ) as error:
                            case.update(
                                status="failed",
                                reason=f"{case.get('reason', '')} Cleanup: {error}",
                            )
                case["seconds"] = time.monotonic() - started
                result["journeys"].append(case)
                report(output, result)
                print(
                    f"{case['status'].upper()} {case['id']}: {case.get('reason', '')}",
                    flush=True,
                )
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["infrastructure_error"] = str(error)
    report(output, result)
    print(output / "index.html")
    if result.get("infrastructure_error"):
        return 2
    return int(any(case["status"] != "passed" for case in result["journeys"]))


if __name__ == "__main__":
    raise SystemExit(main())
