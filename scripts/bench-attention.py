#!/usr/bin/env python3
"""Bounded public Fleet attention measurements through two real native PTYs.

Creates private disposable fixture repositories; never builds or uses network SSH.
Missing evidence, identity mismatches, missed frames and failed cleanup fail closed.
"""

from __future__ import annotations

import argparse
import copy
import fcntl
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
import threading
import time
from collections import Counter
from pathlib import Path
from statistics import median

from bench_attention_pty import Session, observer_cpu, process_tree, selected_line

ROOT = Path(__file__).resolve().parents[1]
WIRE = [
    "source",
    "kind",
    "reason",
    "project",
    "host",
    "task",
    "run",
    "step",
    "attempt",
    "head",
    "instance",
    "request",
    "binding",
    "revision",
    "identity",
    "freshness",
    "route",
    "navigable",
]
IDENTITY = [
    "source",
    "kind",
    "project",
    "host",
    "task",
    "run",
    "step",
    "attempt",
    "head",
    "instance",
    "request",
    "binding",
]
SELECTION = [
    "kind",
    "project",
    "host",
    "task",
    "run",
    "step",
    "attempt",
    "head",
    "instance",
    "request",
    "binding",
    "revision",
    "identity",
]
SEMANTICS = [
    "execution_state",
    "cancellation",
    "cancellation_scope",
    "cancel_requested_at",
    "result_collection",
    "verification",
    "artifact_inventory",
    "process_exit",
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def atomic(path: Path, contents: bytes) -> None:
    temporary = path.with_name(path.name + ".bench-tmp")
    temporary.write_bytes(contents)
    temporary.replace(path)


def encoded(value) -> bytes:
    # Public JSON-C plain representation, including slash escaping.
    return (
        json.dumps(value, separators=(",", ":"), ensure_ascii=False)
        .replace("/", "\\/")
        .encode()
    )


def expected_item(
    task: dict,
    kind: str,
    request: dict | None = None,
    freshness: str = "fresh",
    reason: str | None = None,
) -> dict:
    """Encode exact identity/revision from receiver input, not attention output.

    Callers independently declare the event class from known fixture operations.
    This encodes the public wire contract, not the producer's classification.
    """
    step = request["step_id"] if request else task.get("step_id")
    attempt = task.get("attempt_id")
    if request:
        attempt = next(
            row["attempt_id"] for row in task["steps"] if row["step_id"] == step
        )
    identity = {
        "host": "build",
        "task_id": task.get("task_id"),
        "run_id": task.get("run_id"),
        "step_id": step,
        "attempt_id": attempt,
        "spec_sha256": task.get("spec_sha256"),
        "request_id": request["request_id"] if request else None,
    }
    revision = {"kind": kind, **identity}
    for key in SEMANTICS:
        revision[key] = json.loads(json.dumps(task.get(key), sort_keys=True))
    if request:
        revision.update(
            request_state=request["state"],
            expires_at=request["expires_at"],
            request_message=request["message"],
        )
    item = dict(zip(WIRE, ["-"] * len(WIRE)))
    item.update(
        source="fleet overview",
        kind=kind,
        reason=reason or {"result": "result_ready"}.get(kind, kind),
        host="build",
        task=task.get("task_id") or "-",
        run=task.get("run_id") or "-",
        step=step or "-",
        attempt=attempt or "-",
        request=identity["request_id"] or "-",
        binding=identity["spec_sha256"] or "-",
        freshness=freshness,
        route="task-result" if kind == "result" else "task-observe",
        navigable="1" if task.get("task_id") else "0",
    )
    item["revision"] = hashlib.sha256(encoded(revision)).hexdigest()
    item["identity"] = hashlib.sha256(
        "\t".join(item[key] for key in IDENTITY).encode()
    ).hexdigest()
    return item


def parse_rows(text: str) -> list[dict]:
    lines = text.splitlines()
    if not lines or lines[0] != "HYDRA_ATTENTION\t1":
        raise ValueError("missing attention protocol header")
    rows = []
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 19 or fields[0] != "ITEM":
            raise ValueError("malformed attention item")
        rows.append(dict(zip(WIRE, fields[1:])))
    ending = lines[-1].split("\t")
    if len(ending) != 4 or ending[0] != "END" or int(ending[1]) != len(rows):
        raise ValueError("incomplete attention snapshot")
    return rows


def score(expected: list[dict], observed: list[dict]) -> dict:
    def counted(rows, unknown):
        return Counter(
            json.dumps(row, sort_keys=True)
            for row in rows
            if (row["kind"] == "unknown") == unknown
        )

    want, got = counted(expected, False), counted(observed, False)
    unknown_want, unknown_got = counted(expected, True), counted(observed, True)
    return {
        "tp": sum((want & got).values()),
        "fp": sum((got - want).values()),
        "fn": sum((want - got).values()),
        "expected_supported": sum(want.values()),
        "observed_supported": sum(got.values()),
        "expected_unknown": sum(unknown_want.values()),
        "observed_unknown": sum(unknown_got.values()),
        "unknown_match": unknown_want == unknown_got,
        "missing": [json.loads(row) for row in (want - got).elements()],
        "extra": [json.loads(row) for row in (got - want).elements()],
    }


def authority_snapshot(fixture: Path) -> dict:
    result: dict[str, dict] = {}
    for name in ("host", "source", "receiver", "client"):
        for path in sorted((fixture / name).rglob("*")):
            relative = path.relative_to(fixture).as_posix()
            if relative.startswith("client/fleet/observations/"):
                continue  # Display cache is explicitly non-authoritative.
            if path.is_symlink():
                result[relative] = {"symlink": os.readlink(path)}
            elif path.is_file():
                result[relative] = {
                    "sha256": digest(path),
                    "mode": path.stat().st_mode & 0o777,
                }
    return result


class Run:
    def __init__(self, args):
        self.args, self.root, self.out = args, args.root.resolve(), args.out.resolve()
        self.out.mkdir(parents=True, exist_ok=False)
        self.fixture = self.out / "fixture"
        self.hydra = self.root / "bin/hydra"
        self.env = dict(
            os.environ,
            HYDRA_HOME=str(self.fixture / "client"),
            HYDRA_REVIEW_FIXTURE=str(self.fixture),
            HYDRA_FLEET_BIN=str(args.fleet.resolve()),
            HYDRA_SKIP_AI="1",
            HYDRA_NONINTERACTIVE="1",
            HYDRA_BENCH_TELEMETRY=str(self.out / "transport"),
        )
        self.rows: list[dict] = []
        self.timings: list[dict] = []
        self.windows: list[dict] = []
        self.sessions: list[Session] = []
        self.cleanup: dict = {}
        self.calls = 0
        self.started = time.monotonic_ns()
        self.hashes = {
            str(path.resolve()): digest(path)
            for path in (
                args.tui,
                args.fleet,
                self.hydra,
                Path(__file__),
                ROOT / "scripts/bench_attention_pty.py",
                ROOT / "tests/fixtures/attention-benchmark/setup.sh",
                ROOT / "tests/fixtures/attention-benchmark/ssh.py",
            )
        }
        save(self.out / "starting-hashes.json", self.hashes)

    def call(self, label: str, *args: str) -> str:
        self.calls += 1
        stem = self.out / f"cli-{self.calls:03d}-{label}"
        command = [str(self.hydra), *args]
        start = time.monotonic_ns()
        result = subprocess.run(
            command,
            env=self.env,
            cwd=self.root,
            capture_output=True,
            timeout=65,
            check=False,
        )
        stem.with_suffix(".stdout").write_bytes(result.stdout)
        stem.with_suffix(".stderr").write_bytes(result.stderr)
        save(
            stem.with_suffix(".json"),
            {
                "argv": command,
                "started_ns": start,
                "finished_ns": time.monotonic_ns(),
                "returncode": result.returncode,
            },
        )
        if result.returncode:
            raise RuntimeError(
                f"public command failed: {stem}: {result.stderr.decode()}"
            )
        return result.stdout.decode()

    def setup(self) -> None:
        fixtures = self.root / "tests/fixtures/attention-benchmark"
        with (self.out / "setup.log").open("wb") as log:
            subprocess.run(
                ["sh", str(fixtures / "setup.sh"), str(self.root), str(self.fixture)],
                env=self.env,
                stdout=log,
                stderr=log,
                check=True,
                timeout=150,
            )
        self.env["PATH"] = (self.fixture / "path").read_text().strip()
        transport = self.fixture / "bin/ssh"
        transport.write_text(
            f"#!{sys.executable}\n"
            + (fixtures / "ssh.py").read_text().split("\n", 1)[1]
        )
        transport.chmod(0o755)
        self.status = {
            kind: json.loads((self.fixture / f"{kind}-status.json").read_text())["data"]
            for kind in ("result", "approval")
        }
        self.base = json.loads(
            self.call("source-overview", "fleet", "overview", "--json")
        )["data"]["hosts"][0]
        self.tasks = {task["task_id"]: task for task in self.base["data"]["tasks"]}
        for kind, status in self.status.items():
            task = self.tasks[status["task_id"]]
            assert (
                task["run_id"] == status["runtime"]["run_id"]
                and task["spec_sha256"] == status["spec_sha256"]
            )
            assert task["execution_state"] == (
                "succeeded" if kind == "result" else "waiting_approval"
            )
        self.approval = self.tasks[self.status["approval"]["task_id"]]
        self.result = self.tasks[self.status["result"]["task_id"]]
        self.request = next(
            row
            for row in self.approval["pending_requests"]
            if row["step_id"] == "approval-alpha"
        )
        runtime = self.status["approval"]["runtime"]
        self.request_dir = (
            self.fixture
            / "host/state/v2/projects"
            / runtime["execution_project_id"]
            / "workflows/runs"
            / runtime["run_id"]
            / "approvals"
            / self.request["request_id"]
        )
        assert (
            self.request_dir / "message"
        ).read_text().strip() == "Review alpha request"
        assert not (self.request_dir / "decision").exists()
        assert len(self.approval["pending_requests"]) == 2
        inspected = json.loads((self.fixture / "inspected.json").read_text())
        assert len(inspected["data"]["artifacts"]) == 2
        save(
            self.out / "ground-truth.json",
            {
                "status": self.status,
                "receiver_overview": self.base,
                "inspected_result": inspected,
                "request_dir": str(self.request_dir),
                "authority": authority_snapshot(self.fixture),
            },
        )
        self.initial = self.expected_public()

    def expected_public(self, freshness="fresh") -> list[dict]:
        return [expected_item(self.result, "result", freshness=freshness)] + [
            expected_item(self.approval, "approval", request, freshness)
            for request in self.approval["pending_requests"]
        ]

    def session(self, label: str) -> Session:
        client = Session(
            self.args.tui.resolve(), self.hydra, self.env, self.root, self.out / label
        )
        self.sessions.append(client)
        client.wait(lambda text: "ATTENTION  3 current" in text)
        return client

    def details(self, client: Session, row: dict, rows: list[dict], label: str) -> dict:
        if "ATTENTION DETAIL" in client.text:
            sent = client.key("\r")
            client.wait(lambda text: "ATTENTION DETAIL" not in text, after=sent)
        index = next(
            i for i, item in enumerate(rows) if item["identity"] == row["identity"]
        )
        client.key("k" * (len(rows) + 1) + "j" * index)
        sent = client.key("\r")
        frame = client.wait(
            lambda text: (
                f"Identity SHA256: {row['identity']}" in text
                and f"Revision SHA256: {row['revision']}" in text
            ),
            after=sent,
        )
        for label_key, key in (
            ("Task", "task"),
            ("Run", "run"),
            ("Step", "step"),
            ("Attempt", "attempt"),
        ):
            assert f"{label_key}: {row[key]}" in frame["text"]
        assert (
            f"Source: {row['source']}  Freshness: {row['freshness']}" in frame["text"]
        )
        return client.capture(label)

    def observe(
        self, label: str, expected: list[dict], operation: dict, client: Session
    ) -> list[dict]:
        output = self.call(label, "fleet", "attention-data")
        observed = parse_rows(output)
        counts = score(expected, observed)
        refresh = client.refresh()
        sent = refresh["sent_ns"]
        current = sum(
            row["freshness"] == "fresh"
            and row["kind"] in ("approval", "result", "permission")
            for row in expected
        )
        stale = sum(row["freshness"] == "stale" for row in expected)
        unknown = sum(
            row["freshness"] not in ("fresh", "stale")
            or row["kind"] not in ("approval", "result", "permission")
            for row in expected
        )
        if "ATTENTION DETAIL" in client.text:
            client.key("\r")
        client.wait(
            lambda text: (
                f"ATTENTION  {current} current  {stale} stale  {unknown} unknown"
                in text
            ),
            after=sent,
        )
        frames = [client.capture(label + "-list")]
        for index, row in enumerate(observed):
            frames.append(self.details(client, row, observed, f"{label}-item-{index}"))
        if observed:
            sent = client.key("\r")
            client.wait(lambda text: "ATTENTION DETAIL" not in text, after=sent)
        entry = {
            "case": label,
            "operation": operation,
            "expected": expected,
            "observed": observed,
            "score": counts,
            "frames": frames,
            "refresh": refresh,
            "observed_ns": time.monotonic_ns(),
        }
        self.rows.append(entry)
        with (self.out / "corpus.jsonl").open("a") as raw:
            raw.write(json.dumps(entry, sort_keys=True) + "\n")
        if counts["fp"] or counts["fn"] or not counts["unknown_match"]:
            raise AssertionError(
                f"{label}: exact identity/cardinality mismatch: {counts}"
            )
        return observed

    def corpus(self, client: Session) -> None:
        self.public_rows = self.observe(
            "public-simultaneous",
            self.initial,
            {
                "source": "public task prepare/submit; completed workflow result+inspect-result and two approval-waits",
                "evidence": str(self.out / "ground-truth.json"),
                "class": "real public-generated",
            },
            client,
        )
        controlled = self.fixture / "controlled.json"
        task = copy.deepcopy(self.result)
        task.update(
            execution_state="running",
            pending_requests=[],
            result_collection={"state": "unavailable"},
        )
        snapshot = copy.deepcopy(self.base)
        snapshot["data"]["tasks"] = [task]
        for name, state in (
            ("working-quiet", "running"),
            ("provider-done-without-result", "succeeded"),
        ):
            task["execution_state"] = state
            atomic(controlled, encoded(snapshot))
            self.observe(
                name,
                [],
                {
                    "class": "controlled receiver response",
                    "source_sha256": digest(controlled),
                    "source": copy.deepcopy(snapshot),
                    "operation": f"execution_state={state}, no requests, result unavailable; no supported attention",
                },
                client,
            )
        task["waiting"]["reason"] = "question"
        atomic(controlled, encoded(snapshot))
        (self.fixture / "client/fleet/observations/build.json").unlink(missing_ok=True)
        self.observe(
            "unsupported-question",
            [
                expected_item(
                    {}, "unknown", freshness="unknown", reason="invalid_response"
                )
            ],
            {
                "class": "controlled unsupported receiver response",
                "source": snapshot,
                "source_sha256": digest(controlled),
                "operation": "question waiting reason is unsupported; empty display cache",
            },
            client,
        )
        reordered = copy.deepcopy(self.base)
        reordered["data"]["tasks"].reverse()
        requests = next(
            row
            for row in reordered["data"]["tasks"]
            if row["task_id"] == self.approval["task_id"]
        )["pending_requests"]
        requests[:] = [requests[1], requests[0], requests[0]]
        atomic(controlled, encoded(reordered))
        self.observe(
            "duplicate-reordered-requests",
            self.initial,
            {
                "class": "controlled receiver response",
                "source": reordered,
                "source_sha256": digest(controlled),
                "operation": "reverse task order; send beta,alpha,alpha requests; exactly three distinct identities expected",
            },
            client,
        )
        controlled.unlink()
        expiry = self.request_dir / "expires-at"
        original = expiry.read_bytes()
        try:
            atomic(expiry, b"1\n")
            request = dict(self.request, expires_at="1")
            expected = [
                row
                for row in self.initial
                if row["request"] != self.request["request_id"]
            ]
            expected.append(expected_item(self.approval, "approval_expired", request))
            self.observe(
                "expired-request",
                expected,
                {
                    "class": "controlled authoritative record mutation",
                    "path": str(expiry),
                    "sha256": digest(expiry),
                    "operation": "set actual request expiry to epoch 1",
                },
                client,
            )
        finally:
            atomic(expiry, original)
        directory = self.fixture / "host/fleet/tasks" / self.result["task_id"]
        retention = directory / "retention.json"
        try:
            atomic(
                retention,
                encoded({"schema_version": 1, "state": "expired", "expired_at": 1}),
            )
            self.observe(
                "expired-artifact",
                [row for row in self.initial if row["kind"] != "result"],
                {
                    "class": "controlled authoritative record mutation",
                    "path": str(retention),
                    "sha256": digest(retention),
                    "operation": "expire retained result; no eligible result remains",
                },
                client,
            )
        finally:
            retention.unlink(missing_ok=True)
        runtime = self.status["result"]["runtime"]
        attempt = (
            self.fixture
            / "host/state/v2/projects"
            / runtime["execution_project_id"]
            / "workflows/runs"
            / runtime["run_id"]
            / "steps/verify/authoritative-attempt"
        )
        original = attempt.read_bytes()
        try:
            atomic(attempt, b"2\n")
            changed = dict(self.result, attempt_id="attempt-2")
            expected = [row for row in self.initial if row["kind"] != "result"] + [
                expected_item(changed, "result")
            ]
            self.observe(
                "replaced-authoritative-attempt",
                expected,
                {
                    "class": "controlled authoritative record mutation",
                    "path": str(attempt),
                    "sha256": digest(attempt),
                    "operation": "replace verify authoritative attempt 1 with 2",
                },
                client,
            )
            old = next(row for row in self.initial if row["kind"] == "result")
            review = json.loads(
                self.call(
                    "replaced-old-review",
                    "fleet",
                    "review",
                    *(old[key] for key in SELECTION),
                )
            )
            assert review["data"]["readiness"] == "revoked"
        finally:
            atomic(attempt, original)
        self.public_rows = self.observe(
            "restored-public-source",
            self.initial,
            {
                "class": "real public-generated",
                "operation": "restore controlled expiry/attempt changes; verified source retained",
            },
            client,
        )

    def timed(self, name: str, client: Session, events: list[dict]) -> None:
        origin = time.monotonic_ns() + 250_000_000
        interval = int(self.args.interval * 1e9)
        for i, event in enumerate(events):
            event.update(
                case=name,
                sample=i + 1,
                scheduled_ns=origin + i * interval,
                deadline_ns=origin + (i + 1) * interval,
            )
        save(
            self.out / f"{name}-schedule.json",
            [
                {k: v for k, v in event.items() if k not in ("action", "matches")}
                for event in events
            ],
        )

        def inject():
            for event in events:
                time.sleep(max(0, (event["scheduled_ns"] - time.monotonic_ns()) / 1e9))
                event["sent_ns"] = time.monotonic_ns()
                try:
                    event["action"]()
                    event["input_ns"] = client.key(event.get("key", "I"))
                    event["stimulus_done_ns"] = time.monotonic_ns()
                except (OSError, ValueError) as error:
                    event["error"] = str(error)

        injector = threading.Thread(target=inject, name="absolute-stimulus-schedule")
        injector.start()
        while time.monotonic_ns() < events[-1]["deadline_ns"]:
            client.pump(0.01)
            for event in events:
                if "input_ns" not in event or "observed_ns" in event:
                    continue
                frame = client.frames.latest
                if event["input_ns"] <= frame.get("observed_ns", 0) < event[
                    "deadline_ns"
                ] and event["matches"](client.text):
                    event["observed_ns"] = frame["observed_ns"]
                    event["frame"] = client.capture(f"{name}-{event['sample']}")
        injector.join(timeout=1)
        if injector.is_alive():
            raise RuntimeError("stimulus injector did not finish within schedule")
        for event in events:
            row = {k: v for k, v in event.items() if k not in ("action", "matches")}
            row.update(
                late_ns=max(
                    0, row.get("sent_ns", row["deadline_ns"]) - row["scheduled_ns"]
                ),
                missed_slot=row.get("sent_ns", row["deadline_ns"])
                >= row["deadline_ns"],
                missed_observation="observed_ns" not in row,
            )
            row["sent_to_frame_ms"] = (
                (row["observed_ns"] - row["sent_ns"]) / 1e6
                if "observed_ns" in row
                else None
            )
            row["scheduled_to_frame_ms"] = (
                (row["observed_ns"] - row["scheduled_ns"]) / 1e6
                if "observed_ns" in row
                else None
            )
            self.timings.append(row)
            with (self.out / "timings.jsonl").open("a") as raw:
                raw.write(json.dumps(row, sort_keys=True) + "\n")
        # A missed observation is measurement evidence, not permission to omit
        # later independent windows. Functional identity/authority checks still fail.
        misses = sum("observed_ns" not in event for event in events)
        print(
            f"{name}: {len(events)} scheduled, {misses} missed observations", flush=True
        )

    def clients(self, first: Session, measure: bool = True) -> None:
        second = self.session("client-b")
        approval = next(
            row
            for row in self.public_rows
            if row["request"] == self.request["request_id"]
        )
        before = authority_snapshot(self.fixture)
        for client in (first, second):
            self.details(client, approval, self.public_rows, "independent-selected")
            sent = client.key("\r")
            client.wait(
                lambda text: (
                    "ATTENTION DETAIL" not in text and "NEW" in selected_line(text)
                ),
                after=sent,
            )
        sent = first.key("s")
        first.wait(
            lambda text: (
                "ATTENTION  2 current" in text and "NEW" not in selected_line(text)
            ),
            after=sent,
        )
        ack = first.capture("acknowledged-client-a")
        independent_refresh = second.refresh()
        assert independent_refresh["sent_ns"] > ack["observed_ns"]
        independent = second.capture("unacknowledged-client-b")
        assert (
            "NEW" in selected_line(second.text)
            and "ATTENTION  3 current" in second.text
        )
        self.details(
            second, approval, self.public_rows, "independent-post-ack-selection"
        )
        first.refresh()
        assert "NEW" not in selected_line(first.text)
        events = []
        marker = self.fixture / "offline"
        for repetition in range(self.args.repetitions if measure else 0):
            events.extend(
                [
                    {
                        "transition": "offline",
                        "repetition": repetition + 1,
                        "action": lambda: marker.touch(),
                        "operation": "local transport outage marker",
                        "matches": lambda text: (
                            "ATTENTION  0 current  3 stale" in text
                            and "NEW" not in selected_line(text)
                        ),
                    },
                    {
                        "transition": "same-client-reconnect",
                        "repetition": repetition + 1,
                        "action": lambda: marker.unlink(),
                        "operation": "remove outage marker; same live acknowledged PID",
                        "matches": lambda text: (
                            "ATTENTION  2 current  0 stale" in text
                            and "NEW" not in selected_line(text)
                        ),
                    },
                ]
            )
        try:
            if events:
                self.timed("acknowledged-reconnect", first, events)
        finally:
            marker.unlink(missing_ok=True)
        self.details(
            first,
            approval,
            self.public_rows,
            "reconnected-exact-selection"
            if measure
            else "acknowledged-exact-selection",
        )
        assert authority_snapshot(self.fixture) == before
        save(
            self.out / "client-independence.json",
            {
                "client_a": first.child.pid,
                "client_b": second.child.pid,
                "acknowledged": ack,
                "independent": independent,
                "independent_post_ack_refresh": independent_refresh,
                "same_live_pid_after_reconnect": first.child.pid if measure else None,
                "selection": approval,
                "authority_before": before,
                "authority_after": authority_snapshot(self.fixture),
            },
        )
        self.cleanup["client_b"] = second.close()
        self.sessions.remove(second)
        if not measure:
            return
        message = self.request_dir / "message"
        original = message.read_bytes()
        events = []
        for repetition in range(self.args.repetitions):
            value = f"Independent revision {repetition + 1}"
            expected = expected_item(
                self.approval, "approval", dict(self.request, message=value)
            )
            events.append(
                {
                    "transition": "changed-approval-revision",
                    "repetition": repetition + 1,
                    "operation": "change actual request message; same task/attempt/request identity",
                    "source_path": str(message),
                    "source_bytes": value + "\n",
                    "expected": expected,
                    "action": lambda value=value: atomic(
                        message, (value + "\n").encode()
                    ),
                    "matches": lambda text, expected=expected: (
                        f"Revision SHA256: {expected['revision']}" in text
                        and f"Identity SHA256: {expected['identity']}" in text
                    ),
                }
            )
        try:
            self.timed("approval-revision", first, events)
            sent = first.key("\r")
            first.wait(lambda text: "NEW" in selected_line(text), after=sent)
            first.capture("changed-revision-is-unseen")
        finally:
            atomic(message, original)
        result = next(row for row in self.public_rows if row["kind"] == "result")
        self.details(first, result, self.public_rows, "result-before-revision")
        state = (
            self.fixture / "host/fleet/tasks" / self.result["task_id"] / "state.json"
        )
        original = state.read_bytes()
        events = []
        for repetition in range(self.args.repetitions):
            value = ("failed", "cancelled", "succeeded")[repetition % 3]
            source = dict(json.loads(original), state=value)
            expected = expected_item(
                dict(
                    self.result,
                    execution_state=value,
                    process_exit=dict(self.result["process_exit"], state=value),
                ),
                "result",
            )
            events.append(
                {
                    "transition": "changed-result-revision",
                    "repetition": repetition + 1,
                    "operation": "controlled receiver state change; retained verified bundle unchanged",
                    "source_path": str(state),
                    "source": source,
                    "expected": expected,
                    "action": lambda source=source: atomic(state, encoded(source)),
                    "matches": lambda text, expected=expected: (
                        f"Revision SHA256: {expected['revision']}" in text
                        and f"Identity SHA256: {expected['identity']}" in text
                    ),
                }
            )
        try:
            self.timed("result-revision", first, events)
        finally:
            atomic(state, original)

    def window(self, client: Session, label: str, interactive: bool) -> None:
        start = time.monotonic_ns()
        end = start + int(self.args.window * 1e9)
        before = observer_cpu()
        load_before = os.getloadavg()
        samples: list[dict] = []
        keys: list[dict] = []
        next_sample, next_key = start, start
        while time.monotonic_ns() < end:
            client.pump(0.01)
            now = time.monotonic_ns()
            if now >= next_sample:
                tree = process_tree([client.child.pid])
                client.children.update(tree)
                samples.append(
                    {
                        "scheduled_ns": next_sample,
                        "observed_ns": time.monotonic_ns(),
                        "processes": tree,
                    }
                )
                next_sample += 250_000_000
            if interactive and now >= next_key:
                keys.append({"scheduled_ns": next_key, "sent_ns": client.key("i")})
                next_key += 500_000_000
        last = process_tree([client.child.pid])
        client.children.update(last)
        samples.append({"observed_ns": time.monotonic_ns(), "processes": last})
        after = observer_cpu()
        first_cpu = samples[0]["processes"].get(client.child.pid)
        last_cpu = samples[-1]["processes"].get(client.child.pid)
        row = {
            "case": label,
            "interactive": interactive,
            "started_ns": start,
            "finished_ns": time.monotonic_ns(),
            "observer_before": before,
            "observer_after": after,
            "load_average_before": load_before,
            "load_average_after": os.getloadavg(),
            "concurrent_load_note": self.args.load_note,
            "tui_pid": client.child.pid,
            "tui_direct_before": first_cpu,
            "tui_direct_after": last_cpu,
            "tui_direct_cpu_seconds": last_cpu["cpu_seconds"] - first_cpu["cpu_seconds"]
            if first_cpu and last_cpu
            else None,
            "process_samples": samples,
            "inputs": keys,
            "subtree_cpu_seconds": None,
            "subtree_reason": "ps samples miss short-lived upstream helpers; direct CPU is not subtree CPU",
        }
        self.windows.append(row)
        save(self.out / f"{label}.json", row)

    def reviews(self, client: Session, measure: bool = True) -> None:
        before = authority_snapshot(self.fixture)
        if "ATTENTION DETAIL" in client.text:
            sent = client.key("\r")
            client.wait(lambda text: "ATTENTION DETAIL" not in text, after=sent)
        sent = client.key("r")
        client.wait(
            lambda text: (
                "EXACT REVIEW / evidence" in text and "readiness: ready" in text
            ),
            after=sent,
        )
        client.capture("real-result-review-ready")
        result = next(row for row in self.public_rows if row["kind"] == "result")
        sent = client.key("i")
        client.wait(
            lambda text: f"Identity SHA256: {result['identity']}" in text, after=sent
        )
        client.capture("review-exact-selected-identity")
        sent = client.key("i")
        client.wait(lambda text: "EXACT REVIEW / evidence" in text, after=sent)
        events = []
        for repetition in range(self.args.repetitions if measure else 0):
            events.extend(
                [
                    {
                        "transition": "review-open-identities",
                        "repetition": repetition + 1,
                        "key": "i",
                        "operation": "I3 review identity navigation",
                        "action": lambda: None,
                        "matches": lambda text: (
                            "EXACT REVIEW / selected identity" in text
                        ),
                    },
                    {
                        "transition": "review-return-evidence",
                        "repetition": repetition + 1,
                        "key": "i",
                        "operation": "I3 review evidence navigation",
                        "action": lambda: None,
                        "matches": lambda text: (
                            "EXACT REVIEW / evidence" in text
                            and "readiness: ready" in text
                        ),
                    },
                ]
            )
        if events:
            self.timed("review-navigation", client, events)
        events = []
        for repetition in range(self.args.repetitions if measure else 0):
            events.extend(
                [
                    {
                        "transition": "review-close",
                        "repetition": repetition + 1,
                        "key": "\x1b",
                        "operation": "return from real evidence to attention queue",
                        "action": lambda: None,
                        "matches": lambda text: (
                            "ATTENTION  " in text and "EXACT REVIEW" not in text
                        ),
                    },
                    {
                        "transition": "review-open-public-evidence",
                        "repetition": repetition + 1,
                        "key": "r",
                        "operation": "load exact result through public fleet review-data",
                        "action": lambda: None,
                        "matches": lambda text: (
                            "EXACT REVIEW / evidence" in text
                            and "readiness: ready" in text
                        ),
                    },
                ]
            )
        if events:
            self.timed("review-public-load", client, events)
            # Preserve expired timing slots; this is a separate bounded recovery
            # before CPU sampling, never a retroactive successful observation.
            client.wait(lambda text: "readiness: ready" in text)
            client.capture("review-recovery-before-cpu")
        for repetition in range(self.args.repetitions):
            self.window(client, f"quiet-{repetition + 1}", False)
            self.window(client, f"interactive-review-{repetition + 1}", True)
        log = self.out / "supplied-log.txt"
        log.write_text(
            "Independent benchmark reference\nNo acceptance action was requested.\n"
        )
        client.key("l")
        deadline = time.monotonic() + 5
        while (
            b"log reference: absolute local path" not in client.frames.buffer
            and time.monotonic() < deadline
        ):
            client.pump()
        if b"log reference: absolute local path" not in client.frames.buffer:
            raise TimeoutError("native reference prompt missing")
        sent = client.key(str(log) + "\r")
        client.wait(lambda text: "Reference 1/1: log / available" in text, after=sent)
        sent = client.key("\r")
        client.wait(lambda text: "Independent benchmark reference" in text, after=sent)
        client.capture("opened-local-reference")
        after = authority_snapshot(self.fixture)
        save(
            self.out / "review-read-only.json",
            {"before": before, "after": after, "unchanged": before == after},
        )
        assert before == after

    def finish(self) -> None:
        for client in self.sessions:
            self.cleanup[str(client.child.pid)] = client.close()
        self.sessions.clear()
        self.env["HYDRA_HOME"] = str(self.fixture / "host")
        for record in (self.fixture / "host/fleet/tasks").glob(
            "task_*/acceptance.json"
        ):
            payload = encoded(
                {
                    "protocol": 1,
                    "action": "task",
                    "operation": "cancel",
                    "task_id": record.parent.name,
                }
            )
            subprocess.run(
                [str(self.hydra), "fleet", "serve"],
                input=payload,
                env=self.env,
                capture_output=True,
                timeout=15,
                check=False,
            )
        busy: list[str] = []
        for _ in range(100):
            busy = []
            for path in (self.fixture / "host/fleet/tasks").glob("task_*/owner.lock"):
                with path.open("rb") as lock:
                    try:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        busy.append(str(path))
            if not busy:
                break
            time.sleep(0.1)
        self.cleanup["busy_owner_locks"] = busy
        if not busy and self.fixture.exists():
            with tarfile.open(self.out / "fixture-evidence.tar.gz", "w:gz") as archive:
                for name in ("host", "source", "receiver", "client"):
                    if (self.fixture / name).exists():
                        archive.add(self.fixture / name, arcname=name)
                for path in self.fixture.glob("*.json"):
                    archive.add(path, arcname=path.name)
            shutil.rmtree(self.fixture)
        self.cleanup["fixture_removed"] = not self.fixture.exists()
        save(self.out / "cleanup.json", self.cleanup)


def report(run: Run, error: str | None) -> dict:
    telemetry = [
        json.loads(path.read_text()) for path in (run.out / "transport").glob("*.json")
    ]
    for window in run.windows:
        entries = [
            row
            for row in telemetry
            if window["started_ns"] <= row["started_ns"] < window["finished_ns"]
        ]
        window["transport_invocations"] = len(entries)
        window["transport_pids"] = [row["pid"] for row in entries]
        window["receiver_accounting"] = [
            row for row in entries if row["receiver_cpu"] is not None
        ]
    distributions = {}
    for transition in sorted({row["transition"] for row in run.timings}):
        rows = [row for row in run.timings if row["transition"] == transition]
        values = [
            row["sent_to_frame_ms"]
            for row in rows
            if row["sent_to_frame_ms"] is not None
        ]
        distributions[transition] = {
            "n": len(values),
            "min_ms": min(values) if values else None,
            "median_ms": median(values) if values else None,
            "max_ms": max(values) if values else None,
            "late_stimuli_over_10ms": sum(row["late_ns"] > 10_000_000 for row in rows),
            "missed_slots": sum(row["missed_slot"] for row in rows),
            "missed_observations": sum(row["missed_observation"] for row in rows),
            "p95_ms": None,
            "p99_ms": None,
            "tail_reason": "bounded sample count does not support tail estimates",
        }
    totals = {
        key: sum(row["score"][key] for row in run.rows)
        for key in (
            "tp",
            "fp",
            "fn",
            "expected_supported",
            "observed_supported",
            "expected_unknown",
            "observed_unknown",
        )
    }
    cleanup_ok = run.cleanup.get("fixture_removed", False) and not run.cleanup.get(
        "busy_owner_locks"
    )
    for row in run.cleanup.values():
        if isinstance(row, dict) and "terminal_restored" in row:
            cleanup_ok &= (
                row["terminal_restored"]
                and row["returncode"] == 0
                and not row["forced_termination"]
                and not row["surviving_observed_children"]
            )
    current_hashes = {path: digest(Path(path)) for path in run.hashes}
    hashes_unchanged = current_hashes == run.hashes
    required = {
        "ground_truth": (run.out / "ground-truth.json").exists(),
        "client_independence": (run.out / "client-independence.json").exists(),
        "review_authority_unchanged": (run.out / "review-read-only.json").exists(),
        "quiet_windows": sum(not row["interactive"] for row in run.windows)
        >= run.args.repetitions,
        "interactive_windows": sum(row["interactive"] for row in run.windows)
        >= run.args.repetitions,
    }
    if run.args.phase == "all":
        required["corpus_cases"] = len(run.rows) == 9
        required["scheduled_transitions"] = len(distributions) == 8 and all(
            row["n"] + row["missed_observations"] >= run.args.repetitions
            for row in distributions.values()
        )
    result: dict = {
        "schema_version": 2,
        "complete": error is None
        and cleanup_ok
        and hashes_unchanged
        and all(required.values()),
        "phase": run.args.phase,
        "required_evidence": required,
        "concurrent_load_note": run.args.load_note,
        "error": error,
        "command": shlex.join([sys.executable, *sys.argv]),
        "branch": subprocess.check_output(
            ["git", "branch", "--show-current"], cwd=run.root, text=True
        ).strip(),
        "head": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=run.root, text=True
        ).strip(),
        "host": platform.platform(),
        "python": sys.version,
        "load_average": os.getloadavg(),
        "duration_seconds": (time.monotonic_ns() - run.started) / 1e9,
        "corpus_totals": totals,
        "distributions": distributions,
        "windows": run.windows,
        "cleanup_ok": cleanup_ok,
        "hashes": run.hashes,
        "ending_hashes": current_hashes,
        "hashes_unchanged": hashes_unchanged,
        "limitations": [
            "bounded macOS local receiver fixture; no authenticated provider or network SSH",
            "complete PTY frame availability is not input-to-pixel latency",
            "absolute injection schedule is independent of responses; raw late/missed slots retained",
            "instrumented local transport adds Python startup/accounting overhead",
            "direct TUI ps CPU has 10ms resolution; full producer subtree CPU unavailable",
            "process samples give a lower bound on upstream subprocess counts; transport counts cover completed logged invocations",
            "shared machine load is uncontrolled; no baseline speedup or universal precision/recall claim",
            "item 10 full 1/10/50/platform matrix remains outside this run",
        ],
    }
    save(run.out / "report.json", result)
    lines = [
        "# Bounded attention/PTY measurement",
        "",
        f"Complete: {result['complete']}. Error: {error or 'none'}.",
        "",
        f"Run: `{result['command']}`",
        "",
        "| Case | Expected supported | TP | FP | FN | Unknown expected/observed |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for row in run.rows:
        counts = row["score"]
        lines.append(
            f"| {row['case']} | {counts['expected_supported']} | {counts['tp']} | {counts['fp']} | {counts['fn']} | {counts['expected_unknown']}/{counts['observed_unknown']} |"
        )
    lines += [
        "",
        "Raw exact tuples, ground truth, frames and operations: corpus.jsonl. Absolute schedules: timings.jsonl. CPU windows and hashes: report.json. Cleanup: cleanup.json.",
        "",
    ]
    lines += result["limitations"]
    (run.out / "report.md").write_text("\n".join(lines) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--tui", type=Path, default=ROOT / "build/hydra-tui")
    parser.add_argument("--fleet", type=Path, default=ROOT / "build/hydra-fleet")
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="new directory; existing evidence is never overwritten",
    )
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument(
        "--phase",
        choices=("all", "overhead"),
        default="all",
        help="overhead collects only independent-client refresh and remaining review/CPU evidence",
    )
    parser.add_argument(
        "--load-note", default="shared host; no exclusive machine control"
    )
    parser.add_argument(
        "--interval", type=float, default=4, help="absolute stimulus spacing in seconds"
    )
    parser.add_argument(
        "--window", type=float, default=6, help="quiet/interactive window seconds"
    )
    args = parser.parse_args()
    if args.repetitions < 3 or args.interval < 1 or args.window < 3:
        parser.error("require >=3 repetitions, interval >=1s, window >=3s")
    for path in (args.tui, args.fleet):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"executable not found: {path}")
    run = Run(args)
    error = None
    try:
        run.setup()
        print("public fixture ready", flush=True)
        client = run.session("client-a")
        if args.phase == "all":
            run.corpus(client)
            print("identity-scored corpus complete", flush=True)
        else:
            run.public_rows = run.observe(
                "public-simultaneous",
                run.initial,
                {
                    "class": "real public-generated",
                    "evidence": str(run.out / "ground-truth.json"),
                },
                client,
            )
        run.clients(client, measure=args.phase == "all")
        print(
            "client checks complete",
            flush=True,
        )
        result = next(row for row in run.public_rows if row["kind"] == "result")
        run.details(client, result, run.public_rows, "selected-result-before-overhead")
        run.reviews(client, measure=args.phase == "all")
    except (
        OSError,
        ValueError,
        AssertionError,
        subprocess.SubprocessError,
        RuntimeError,
    ) as failure:
        error = f"{type(failure).__name__}: {failure}"
    finally:
        try:
            run.finish()
        except (
            OSError,
            ValueError,
            subprocess.SubprocessError,
            RuntimeError,
        ) as failure:
            error = f"{error or ''}; cleanup: {type(failure).__name__}: {failure}"
        result = report(run, error)
    print(
        json.dumps(
            {
                key: result[key]
                for key in (
                    "complete",
                    "error",
                    "corpus_totals",
                    "distributions",
                    "cleanup_ok",
                )
            }
        )
    )
    print(f"report={run.out / 'report.md'}")
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
