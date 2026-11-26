"""Real public Hydra heads, active stand-ins and positively owned cleanup."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

from bench_i1_process import ProcessCounters


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def json_lines(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    raw = path.read_text()
    complete = raw.rsplit("\n", 1)[0] if "\n" in raw else ""
    return [json.loads(line) for line in complete.splitlines() if line]


class Fixture:
    def __init__(
        self, source: Path, build: Path, output: Path, count: int, mode: str
    ) -> None:
        self.harness = Path(__file__).resolve().parent.parent
        self.source, self.build, self.output, self.count, self.mode = (
            source,
            build,
            output,
            count,
            mode,
        )
        self.repo, self.home = output / "repo", output / "home"
        self.tmux_root = Path(tempfile.mkdtemp(prefix="i10-", dir="/tmp"))
        self.socket = self.tmux_root / f"tmux-{os.getuid()}" / "default"
        self.workers: list[dict[str, Any]] = []
        self.owners: list[subprocess.Popen[str]] = []
        self.owner_files: list[Any] = []
        self.counters = ProcessCounters()
        self.deadline = time.monotonic() + {1: 60, 10: 180, 50: 600}[count]
        self.env = {
            "PATH": os.environ["PATH"],
            "HOME": str(self.home),
            "LANG": "C",
            "LC_ALL": "C",
            "HYDRA_HOME": str(self.home),
            "HYDRA_FLEET_BIN": str(build / "hydra-fleet"),
            "HYDRA_CORE": str(build / "hydra-core"),
            "HYDRA_TUI_BIN": str(build / "hydra-tui"),
            "TMUX_TMPDIR": str(self.tmux_root),
            "HYDRA_NONINTERACTIVE": "1",
            "HYDRA_SETUP_CONTINUE": "1",
            "HYDRA_NO_SWITCH": "1",
            "GIT_CONFIG_GLOBAL": "/dev/null",
            "GIT_CONFIG_NOSYSTEM": "1",
            "PYTHONDONTWRITEBYTECODE": "1",
        }
        if mode == "headless":
            self.env["HYDRA_SKIP_AI"] = "1"
        self.hydra = source / "bin/hydra"

    def command(
        self, args: list[str], *, check: bool = True, timeout: float = 40
    ) -> subprocess.CompletedProcess[str]:
        started = time.monotonic_ns()
        stdout, stderr = "", ""
        process: subprocess.Popen[str] | None = None
        error: OSError | subprocess.TimeoutExpired | None = None
        try:
            process = subprocess.Popen(
                args,
                cwd=self.repo,
                env=self.env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                start_new_session=True,
            )
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired as caught:
            error = caught
            assert process is not None
            os.killpg(process.pid, signal.SIGTERM)
            try:
                stdout, stderr = process.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate(timeout=2)
        except OSError as caught:
            error = caught
        with (self.output / "commands.jsonl").open("a") as stream:
            stream.write(
                json.dumps(
                    {
                        "argv": args,
                        "started_ns": started,
                        "finished_ns": time.monotonic_ns(),
                        "pid": process.pid if process else None,
                        "exit": process.returncode if process else None,
                        "stdout": stdout,
                        "stderr": stderr,
                        "timeout_seconds": timeout,
                        "error": repr(error) if error else None,
                    }
                )
                + "\n"
            )
        if error is not None:
            raise error
        assert process is not None
        result = subprocess.CompletedProcess(args, process.returncode, stdout, stderr)
        if check and result.returncode:
            raise RuntimeError(
                f"command failed ({result.returncode}): {args}: {result.stderr[-500:]}"
            )
        return result

    def public(
        self, *args: str, check: bool = True, timeout: float = 40
    ) -> subprocess.CompletedProcess[str]:
        return self.command([str(self.hydra), *args], check=check, timeout=timeout)

    def setup(self) -> list[dict[str, Any]]:
        self.repo.mkdir()
        self.home.mkdir()
        self.command(["git", "init", "-q"])
        self.command(["git", "config", "user.email", "benchmark@example.invalid"])
        self.command(["git", "config", "user.name", "Hydra item10 fixture"])
        self.command(["git", "config", "core.hooksPath", "/dev/null"])
        for index in range(100):
            (self.repo / f"data-{index:03d}.txt").write_bytes(b"x" * 1023 + b"\n")
        self.command(["git", "add", "."])
        self.command(["git", "commit", "-qm", "seed104729"])
        self.public("init", "--no-agent", "--trust")
        if self.mode == "interactive":
            self.socket.parent.mkdir(mode=0o700)
            self.command(
                [
                    "tmux",
                    "-f",
                    "/dev/null",
                    "-S",
                    str(self.socket),
                    "new-session",
                    "-d",
                    "-s",
                    "item10-bootstrap",
                    "/bin/sh",
                ]
            )
            self.command(
                [
                    "tmux",
                    "-S",
                    str(self.socket),
                    "set-option",
                    "-g",
                    "default-shell",
                    "/bin/sh",
                ]
            )
            self.command(
                [
                    "tmux",
                    "-S",
                    str(self.socket),
                    "set-option",
                    "-g",
                    "default-command",
                    "",
                ]
            )
        worker_source = self.harness / "tests/fixtures/agents/item10-worker.py"
        worker = self.output / "worker-launcher"
        worker.write_text(
            "#!/bin/sh\nexport PYTHONPATH="
            + shlex.quote(str(self.harness / "scripts"))
            + "\nexec "
            + shlex.quote(sys.executable)
            + " -B -s "
            + shlex.quote(str(worker_source))
            + ' "$@"\n'
        )
        worker.chmod(0o755)
        if self.mode == "interactive":
            self.public(
                "agent",
                "init",
                "item10-worker",
                "--executable",
                str(worker),
                "--prompt-mode",
                "task-file",
            )
        else:
            profile = self.output / "profile.json"
            profile.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "executable": str(worker),
                        "argv": [{"input": "prompt"}],
                        "prompt": "argument",
                        "session": "none",
                        "adapter": "canonical-jsonl",
                        "probe_argv": ["--help"],
                        "probe_tokens": ["hydra-item10-worker-v2"],
                    }
                )
            )
            self.public("agent", "import", "item10-worker", str(profile))
        for index in range(self.count):
            self._spawn(index)
            resident = sum(
                self.counters.sample(row["pid"]).get("resident_bytes", 0)
                for row in self.workers
            )
            if resident > 2 * 1024**3:
                raise RuntimeError(
                    "owned active-worker RSS admission cap 2 GiB exceeded"
                )
        self.verify()
        return self.workers

    def _spawn(self, index: int) -> None:
        if time.monotonic() >= self.deadline:
            raise TimeoutError("predeclared cohort provisioning deadline")
        branch = f"i10-h{index:02d}"
        control = self.output / f"worker-{index}.fifo"
        receipt = self.output / f"worker-{index}.jsonl"
        prompt = self.output / f"worker-{index}-prompt.json"
        os.mkfifo(control, 0o600)
        token = f"I104729H{index:02d}"
        prompt.write_text(
            json.dumps(
                {
                    "token": token,
                    "control": str(control),
                    "receipt": str(receipt),
                    "mode": self.mode,
                }
            )
        )
        args = ["spawn", branch]
        if self.mode == "headless":
            args += ["--headless", "--no-agent"]
        else:
            args += ["--profile", "item10-worker", "--prompt-file", str(prompt)]
        launch_ns = time.monotonic_ns()
        self.public(*args, timeout=max(1, min(40, self.deadline - time.monotonic())))
        if index == 0 and self.mode == "interactive":
            self.command(
                [
                    "tmux",
                    "-S",
                    str(self.socket),
                    "kill-session",
                    "-t",
                    "item10-bootstrap",
                ]
            )
        worktree = Path(self.public("path", branch).stdout.strip())
        owner: subprocess.Popen[str] | None = None
        if self.mode == "headless":
            heads = list((self.home / "state/v2/projects").glob("*/heads/head_*"))
            matches = [
                path
                for path in heads
                if (path / "worktree").read_text().strip() == str(worktree)
            ]
            if len(matches) != 1:
                raise RuntimeError(
                    "headless launch lacks an exact public workspace binding"
                )
            state = matches[0]
            prompt_value = json.loads(prompt.read_text())
            prompt_value["binding"] = {
                "HYDRA_PROJECT_ID": state.parent.parent.name,
                "HYDRA_HEAD_ID": state.name,
                "HYDRA_INSTANCE_ID": (state / "current-instance").read_text().strip(),
            }
            prompt.write_text(json.dumps(prompt_value))
            stdout, stderr = (
                (self.output / f"exec-{index}.{suffix}").open("w")
                for suffix in ("json", "stderr")
            )
            self.owner_files.extend((stdout, stderr))
            owner = subprocess.Popen(
                [
                    str(self.hydra),
                    "exec",
                    "--branch",
                    branch,
                    "--profile",
                    "item10-worker",
                    "--prompt-file",
                    str(prompt),
                    "--result-file",
                    "retained-result.txt",
                    "--require",
                    "prompt,observations",
                    "--retain-raw",
                    "--timeout",
                    "900",
                    "--exit-code",
                    "--json",
                ],
                cwd=self.repo,
                env=self.env,
                text=True,
                stdout=stdout,
                stderr=stderr,
            )
            self.owners.append(owner)
        ready_deadline = min(self.deadline, time.monotonic() + 15)
        while time.monotonic() < ready_deadline:
            records = json_lines(receipt)
            if records:
                ready = records[0]
                if ready["event"] != "ready" or Path(ready["cwd"]) != worktree:
                    raise RuntimeError("worker readiness identity mismatch")
                if not launch_ns <= ready["monotonic_ns"] <= time.monotonic_ns():
                    raise RuntimeError(
                        "worker and controller monotonic clocks do not share an epoch"
                    )
                self.workers.append(
                    {
                        "branch": branch,
                        "worktree": str(worktree),
                        "control": str(control),
                        "receipt": str(receipt),
                        "prompt": str(prompt),
                        "token": token,
                        "pid": ready["pid"],
                        "owner_pid": owner.pid if owner else None,
                        "process_start": ready["process_start"],
                        "ready": ready,
                    }
                )
                return
            if owner is not None and owner.poll() is not None:
                raise RuntimeError("headless owner exited before worker readiness")
            time.sleep(0.04)
        raise TimeoutError(f"active worker not ready for {branch}")

    def verify(self) -> None:
        heads = list((self.home / "state/v2/projects").glob("*/heads/head_*"))
        current = [path for path in heads if (path / "head-id").is_file()]
        if len(current) != self.count or len(self.workers) != self.count:
            raise RuntimeError("real head/active worker count mismatch")
        inventory = []
        for row in self.workers:
            matches = [
                path
                for path in current
                if (path / "worktree").read_text().strip() == row["worktree"]
            ]
            if len(matches) != 1:
                raise RuntimeError("exact worktree identity missing")
            head = matches[0]
            row.update(
                project_id=head.parent.parent.name,
                head_id=head.name,
                instance_id=(head / "current-instance").read_text().strip(),
                session=(head / "session").read_text().strip(),
            )
            sample = self.counters.sample(row["pid"])
            if (
                not sample.get("available")
                or sample["identity_start"] != row["process_start"]
            ):
                raise RuntimeError("worker exited or PID identity changed")
            if self.mode == "interactive":
                pane = self.command(
                    [
                        "tmux",
                        "-S",
                        str(self.socket),
                        "display-message",
                        "-p",
                        "-t",
                        row["session"],
                        "#{pane_pid}",
                    ]
                ).stdout.strip()
                descendants = self.counters.tree({int(pane): "pane-shell"})
                if str(row["pid"]) not in descendants:
                    raise RuntimeError(
                        "worker is not a descendant of its recorded tmux pane"
                    )
                row["pane_pid"] = int(pane)
            elif row["session"] != "-":
                raise RuntimeError("headless worker has a terminal session")
            inventory.append({**row, "process_sample": sample})
        (self.output / "inventory.json").write_text(
            json.dumps(inventory, indent=2) + "\n"
        )

    def tell(self, worker: dict[str, Any], command: dict[str, Any]) -> None:
        descriptor = os.open(worker["control"], os.O_WRONLY | os.O_NONBLOCK)
        try:
            payload = (json.dumps(command) + "\n").encode()
            if os.write(descriptor, payload) != len(payload):
                raise RuntimeError("incomplete worker control message")
        finally:
            os.close(descriptor)

    def finish_workers(self) -> list[dict[str, Any]]:
        results = []
        for row in self.workers:
            self.tell(row, {"action": "finish"})
        deadline = time.monotonic() + 15
        for row in self.workers:
            while time.monotonic() < deadline:
                records = json_lines(Path(row["receipt"]))
                if records and records[-1]["event"] == "completed":
                    break
                time.sleep(0.03)
            else:
                raise TimeoutError("worker did not retain its final result")
            artifact = Path(row["worktree"]) / "item10-result.txt"
            if artifact.read_text() != row["token"]:
                raise RuntimeError("worker result bytes mismatch")
            results.append(
                {
                    "head_id": row["head_id"],
                    "instance_id": row["instance_id"],
                    "token": row["token"],
                    "sha256": sha(artifact),
                    "receipt": records[-1],
                }
            )
        for owner in self.owners:
            if owner.wait(timeout=10) != 0:
                raise RuntimeError("supervised worker result failed")
        if self.mode == "headless":
            for index, row in enumerate(self.workers):
                if (Path(row["worktree"]) / "retained-result.txt").read_text() != row[
                    "token"
                ]:
                    raise RuntimeError("public retained result bytes mismatch")
                result = json.loads((self.output / f"exec-{index}.json").read_text())[
                    "data"
                ]["results"][0]
                agent = json.loads(result["stdout"])["data"]
                for key in ("head_id", "project_id", "instance_id", "worktree"):
                    if agent[key] != row[key]:
                        raise RuntimeError(
                            "public retained agent record identity differs from its worker"
                        )
                (self.output / f"agent-result-{index}.json").write_text(
                    json.dumps(agent, indent=2) + "\n"
                )
        (self.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        return results

    def terminal_clients(self, *, timeout: float = 40) -> list[dict[str, Any]]:
        if self.mode == "headless" or not self.socket.exists():
            return []
        result = self.command(
            [
                "tmux",
                "-S",
                str(self.socket),
                "list-clients",
                "-F",
                "#{client_pid}|#{session_name}",
            ],
            timeout=timeout,
        )
        return [
            {"pid": int(pid), "session": session}
            for pid, session in (
                line.split("|", 1) for line in result.stdout.splitlines()
            )
        ]

    def cleanup(self) -> dict[str, Any]:
        from bench_i1_cleanup import cleanup_fixture

        return cleanup_fixture(self)
