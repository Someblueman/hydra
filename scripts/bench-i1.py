#!/usr/bin/env python3
"""I1 fixture reuse for item10 readiness and declared, independently scheduled trials."""

from __future__ import annotations

import argparse
import json
import os
import resource
import subprocess
import threading
import time
from pathlib import Path
from typing import Any

from bench_i1_actions import late_dispatch_control, view_actions, wait_until
from bench_i1_fixture import Fixture, json_lines
from bench_i1_process import ProcessCounters, summarize_samples
from bench_i1_provenance import identities, runtime_provenance
from bench_i1_pty import Observer, region
from bench_i1_workload import phase_proof, retain_sink_evidence


def await_attachment_count(
    fixture: Fixture, expected: int, deadline_ns: int
) -> list[dict[str, Any]]:
    while time.monotonic_ns() < deadline_ns:
        clients = fixture.terminal_clients(
            timeout=max(0, (deadline_ns - time.monotonic_ns()) / 1e9)
        )
        if time.monotonic_ns() >= deadline_ns:
            break
        if len(clients) == expected:
            return clients
        time.sleep(min(0.04, max(0, (deadline_ns - time.monotonic_ns()) / 1e9)))
    raise TimeoutError(
        "private terminal clients did not become ready within the fixed client guard"
    )


def live_input_gate(
    client: Observer, worker: dict[str, Any], fixture: Fixture
) -> dict[str, Any]:
    deadline_ns = client.started_ns + 15_000_000_000
    client.input("workspace", b"W")
    client.visible("HYDRA WORKSPACE", deadline_ns=deadline_ns)
    client.input("attach", b"a")
    client.visible("INPUT TO AGENT", deadline_ns=deadline_ns)
    attached_clients = await_attachment_count(fixture, 1, deadline_ns)
    client.input("first", b"ECHO FIRST\n")
    prefix = "ACK " + worker["token"] + " "
    screen, _ = client.visible(prefix + "FIRST", timeout=4)
    x, y, _, _ = region(screen, prefix + "FIRST")
    identity = (x, y, len(prefix), prefix)
    schedule: list[dict[str, Any]] = [
        {
            "id": "second",
            "tag": "SECOND",
            "scheduled_ns": time.monotonic_ns() + 150_000_000,
        }
    ]
    schedule.append(
        {
            "id": "third",
            "tag": "THIRD",
            "scheduled_ns": schedule[0]["scheduled_ns"] + 800_000_000,
        }
    )
    errors: list[str] = []

    def inject() -> None:
        try:
            for row in schedule:
                wait_until(row["scheduled_ns"])
                text = prefix + row["tag"]
                client.expect(row["id"], identity, (x, y, len(text), text))
                row["controller_sent_ns"] = client.input(
                    row["id"], ("ECHO " + row["tag"] + "\n").encode()
                )
        except (OSError, RuntimeError, ValueError) as error:
            errors.append(str(error))

    writer = threading.Thread(target=inject)
    writer.start()
    writer.join(timeout=5)
    if writer.is_alive() or errors:
        raise RuntimeError(f"absolute input schedule failed: {errors}")
    for row in schedule:
        row["observation"] = client.wait("observed", row["id"], timeout=4)
        row["injection"] = client.wait("input", row["id"])
        if row["injection"]["requested"] != row["injection"]["sent"]:
            raise RuntimeError("incomplete PTY input injection")
    producer = json_lines(Path(worker["receipt"]))
    received = {row["text"]: row for row in producer if row["event"] == "input"}
    if not all(row["tag"] in received for row in schedule):
        raise RuntimeError("applied response lacks actual worker input receipt")
    screen, _ = client.visible(prefix + "THIRD", timeout=4)
    current = region(screen, prefix + "THIRD")
    client.expect("preexisting", identity, current)
    preexisting = client.wait("invalid-expectation", "preexisting")
    client.input("preexisting", b"ECHO FOURTH\n")
    client.visible(prefix + "FOURTH", timeout=4)
    late_text = prefix + "DELAYED"
    client.expect(
        "late-control", identity, (x, y, len(late_text), late_text), timeout_ms=50
    )
    client.input("late-control", b"LATE DELAYED\n")
    missed = client.wait("miss", "late-control")
    late = client.wait("late-observed", "late-control")
    if late["observed_ns"] < late["deadline_ns"] or any(
        row.get("event") == "observed"
        and row.get("id") in ("preexisting", "late-control")
        for row in client.records
    ):
        raise RuntimeError("invalid or late response became an on-time observation")
    return {
        "boundary": "applied semantic region after complete ANSI sequence",
        "terminal_clients": attached_clients,
        "schedule": schedule,
        "worker_receipts": received,
        "preexisting_rejected": preexisting,
        "late_miss": missed,
        "late_observation": late,
        "whole_frame_completion": None,
        "pixels": None,
    }


def readiness(
    args: argparse.Namespace, fixture: Fixture, report: dict[str, Any]
) -> None:
    workers = fixture.setup()
    report["topology_by_phase"] = {
        "workers_ready": {
            "tui_clients": 0,
            "terminal_clients": fixture.terminal_clients(),
        }
    }
    report["active_workers"] = len(workers)
    report["queued_workers"] = 0
    if args.mode == "interactive":
        client = Observer(
            args.observer,
            args.build / "hydra-tui",
            args.source / "bin/hydra",
            args.output / "client",
            fixture.repo,
            fixture.env,
        )
        attached = False
        try:
            client.ready({worker["branch"] for worker in workers})
            report.setdefault("clock_sync", []).append(client.calibrate_clock())
            if args.heads == 1 and not args.failure_observer:
                attached = True
                report["live_input_gate"] = live_input_gate(client, workers[0], fixture)
                if args.preview != "open":
                    client.input("close-readiness-terminal", b"\x02x\x1b")
                    client.visible("HYDRA MISSION CONTROL")
                    attached = False
                report["view_action_gate"] = view_actions(
                    client,
                    time.monotonic_ns() + 100_000_000,
                    args.output / "view-actions.json",
                    from_attachment=args.preview == "open",
                )
                if any(
                    row["status"] != "observed" for row in report["view_action_gate"]
                ):
                    raise RuntimeError("one or more view action predicates failed")
                report["late_dispatch_control"] = late_dispatch_control(client)
            report["topology_by_phase"]["before_client_close"] = {
                "tui_clients": 1,
                "terminal_clients": fixture.terminal_clients(),
            }
            report["native_ready"] = True
        finally:
            receipt = client.close(attached=attached)
            report["client_cleanup"] = receipt
            report["topology_by_phase"]["after_client_close"] = {
                "tui_clients": 0,
                "terminal_clients": fixture.terminal_clients(),
            }
            if (
                not receipt.get("terminal_restored")
                or receipt.get("forced")
                or receipt.get("observer_exit")
            ):
                raise RuntimeError("observer cleanup was incomplete or forced")
    report["worker_results"] = fixture.finish_workers()


def measure(args: argparse.Namespace, fixture: Fixture, report: dict[str, Any]) -> None:
    workers = fixture.setup()
    clients: list[Observer] = []
    roots = {row["pid"]: "agent" for row in workers}
    roots[os.getpid()] = "controller"
    roots.update(
        {row["owner_pid"]: "execution-owner" for row in workers if row["owner_pid"]}
    )
    roots.update(
        {row["pane_pid"]: "pane-shell" for row in workers if "pane_pid" in row}
    )
    try:
        for index in range(args.clients):
            client = Observer(
                args.observer,
                args.build / "hydra-tui",
                args.source / "bin/hydra",
                args.output / f"client-{index}",
                fixture.repo,
                fixture.env,
            )
            clients.append(client)
            client.ready({worker["branch"] for worker in workers})
            report.setdefault("clock_sync", []).append(client.calibrate_clock())
            spawn = client.wait("spawn")
            roots[spawn["tui_pid"]] = "native-tui"
            roots[spawn["observer_pid"]] = "pty-observer"
            if args.preview == "open":
                deadline_ns = client.started_ns + 15_000_000_000
                client.input("workspace", b"W")
                client.visible("HYDRA WORKSPACE", deadline_ns=deadline_ns)
                client.input("attach", b"a")
                client.visible("INPUT TO AGENT", deadline_ns=deadline_ns)
                await_attachment_count(fixture, index + 1, deadline_ns)
        if fixture.mode == "interactive":
            server = fixture.command(
                ["tmux", "-S", str(fixture.socket), "display-message", "-p", "#{pid}"]
            ).stdout.strip()
            roots[int(server)] = "tmux-server"
        terminal_clients = fixture.terminal_clients()
        if len(terminal_clients) != (args.clients if args.preview == "open" else 0):
            raise RuntimeError(
                "measured terminal attachment count differs from requested topology"
            )
        report["topology_by_phase"] = {
            "steady": {
                "tui_clients": len(clients),
                "terminal_clients": terminal_clients,
            }
        }
        time.sleep(10)
        start = time.monotonic_ns() + 500_000_000
        for index, worker in enumerate(workers):
            fixture.tell(
                worker,
                {
                    "action": "phase",
                    "id": "trial",
                    "case": args.case,
                    "start_ns": start,
                    "duration_ns": 30_000_000_000,
                    "change": args.case == "all-changing" or index == 0,
                },
            )
        while time.monotonic_ns() < start:
            time.sleep(0.005)
        counters = ProcessCounters()
        usage_before = resource.getrusage(resource.RUSAGE_SELF)
        before = counters.tree(roots)
        samples: list[dict[str, Any]] = [
            {
                "scheduled_ns": start,
                "sampled_ns": time.monotonic_ns(),
                "processes": before,
            }
        ]
        for tick in range(1, 61):
            target = start + tick * 500_000_000
            wait_until(target)
            samples.append(
                {
                    "scheduled_ns": target,
                    "sampled_ns": time.monotonic_ns(),
                    "processes": counters.tree(roots),
                }
            )
        usage_after = resource.getrusage(resource.RUSAGE_SELF)
        steady_end = time.monotonic_ns()
        (args.output / "process-samples.json").write_text(
            json.dumps(samples, indent=2) + "\n"
        )
        report["actions"] = view_actions(
            clients[0],
            start + 30_000_000_000,
            args.output / "view-actions.json",
            from_attachment=args.preview == "open",
        )
        wait_until(start + 50_000_000_000)
        report["topology_by_phase"]["after_actions"] = {
            "tui_clients": len(clients),
            "terminal_clients": fixture.terminal_clients(),
        }
        report["steady_start_ns"], report["steady_end_ns"] = start, steady_end
        report["duration_seconds"] = (steady_end - start) / 1e9
        report["process_summary"] = summarize_samples(samples)
        report["workload_proof"] = phase_proof(
            fixture, args.case, start, 30_000_000_000
        )
        report["controller_cpu_seconds"] = (
            usage_after.ru_utime
            + usage_after.ru_stime
            - usage_before.ru_utime
            - usage_before.ru_stime
        )
        report["full_subtree_cpu_seconds"] = None
        report["full_subtree_reason"] = (
            "Sampled descendants miss short-lived/exited helpers; no complete launch trace."
        )
        report["worker_results"] = fixture.finish_workers()
        report["sink_proof"] = retain_sink_evidence(fixture, args.case, 960)
    finally:
        report["client_cleanup"] = [
            client.close(attached=args.preview == "open") for client in clients
        ]
        if any(
            not row.get("terminal_restored")
            or row.get("forced")
            or row.get("observer_exit")
            for row in report["client_cleanup"]
        ):
            raise RuntimeError("one or more measured clients failed cleanup")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=Path(__file__).resolve().parent.parent
    )
    parser.add_argument("--source-manifest", type=Path)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--observer", type=Path)
    parser.add_argument("--heads", type=int, choices=(1, 10, 50), default=1)
    parser.add_argument(
        "--mode", choices=("interactive", "headless"), default="interactive"
    )
    parser.add_argument(
        "--stage", choices=("readiness", "measure"), default="readiness"
    )
    parser.add_argument(
        "--case",
        choices=(
            "quiescent",
            "one-changing",
            "all-changing",
            "sustained-output",
            "result-output",
        ),
        default="quiescent",
    )
    parser.add_argument("--clients", type=int, choices=(1, 3), default=1)
    parser.add_argument("--preview", choices=("closed", "open"), default="closed")
    parser.add_argument(
        "--failure-observer",
        action="store_true",
        help="expect readiness to fail; retain cleanup proof",
    )
    args = parser.parse_args()
    args.source, args.build, args.output = (
        args.source.resolve(),
        args.build.resolve(),
        args.output.resolve(),
    )
    args.observer = (args.observer or args.build / "test-tui-pty").resolve()
    if args.mode == "headless" and args.preview == "open":
        parser.error("headless stdin attachment and terminal preview are unsupported")
    args.output.mkdir(parents=True)
    hashes = identities(args.source, args.build, args.observer, args.source_manifest)
    report: dict[str, Any] = {
        "schema_version": 3,
        "stage": args.stage,
        "status": "incomplete",
        "heads": args.heads,
        "mode": args.mode,
        "case": args.case,
        "pressure_event_type": "observation/running"
        if args.case == "sustained-output"
        else "result"
        if args.case == "result-output"
        else None,
        "performance_gate": None,
        "requested_measurement_topology": {
            "tui_clients": args.clients,
            "terminal_attach_clients": args.clients if args.preview == "open" else 0,
            "preview": args.preview,
        },
        "seed": 104729,
        "hashes_before": hashes,
        "source_provenance": runtime_provenance(args.source, args.source_manifest),
        "clock": vars(time.get_clock_info("monotonic")),
        "load_before": os.getloadavg(),
    }
    fixture = Fixture(args.source, args.build, args.output, args.heads, args.mode)
    try:
        (readiness if args.stage == "readiness" else measure)(args, fixture, report)
        report["status"] = "passed" if args.stage == "readiness" else "measured"
    except (
        OSError,
        RuntimeError,
        ValueError,
        TimeoutError,
        subprocess.SubprocessError,
    ) as error:
        report["error"] = repr(error)
    finally:
        report["cleanup"] = fixture.cleanup()
        report["hashes_after"] = identities(
            args.source, args.build, args.observer, args.source_manifest
        )
        report["hashes_unchanged"] = hashes == report["hashes_after"]
        report["load_after"] = os.getloadavg()
        report["measurement_phase_complete"] = (
            "process_summary" in report and "workload_proof" in report
        )
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    good = report["cleanup"]["cleanup_ok"] and report["hashes_unchanged"]
    if args.failure_observer:
        good = good and report["status"] == "incomplete"
    else:
        good = good and report["status"] in ("passed", "measured")
    print(args.output / "report.json")
    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
