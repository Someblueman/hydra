"""One provisioned fixture: attached clients, scheduled cases and per-cell evidence."""

from __future__ import annotations

import json
import os
import resource
import threading
import time
from pathlib import Path
from typing import Any

from bench_i1_actions import wait_until
from bench_i1_fixture import Fixture, json_lines
from bench_i1_pty import Observer
from bench_i1_workload import phase_proof
from perf_matrix_actions import ChangePoller, ClientState, restore_work_view, run_actions
from perf_matrix_sampling import WindowSampler, family_totals, role_map, summarize_window
from perf_matrix_shim import Shim, summarize_launches

CASES = ("quiescent", "one-changing", "all-changing", "sustained-output")


class Durations:
    def __init__(self, settle: float, idle: float, gap: float, poll_ms: int) -> None:
        self.settle_ns = int(settle * 1e9)
        self.idle_ns = int(idle * 1e9)
        self.gap_ns = int(gap * 1e9)
        self.interaction_ns = 4 * self.gap_ns + 4_000_000_000
        self.poll_ns = poll_ms * 1_000_000
        if self.idle_ns + self.interaction_ns > 30_000_000_000:
            raise ValueError("idle plus interaction exceeds the worker's 30 s phase bound")


def _attach(client: Observer, fixture: Fixture, expected: int) -> list[dict[str, Any]]:
    deadline_ns = time.monotonic_ns() + 15_000_000_000
    client.input("workspace", b"W")
    client.visible("HYDRA / PLAN TOGETHER", deadline_ns=deadline_ns)
    client.input("attach", b"a")
    client.visible("INPUT TO AGENT", deadline_ns=deadline_ns)
    while time.monotonic_ns() < deadline_ns:
        clients = fixture.terminal_clients()
        if len(clients) == expected:
            return clients
        time.sleep(0.05)
    raise TimeoutError("terminal client attachment did not reach the requested count")


class FixtureRun:
    """Provision once per (heads, mode); every client group and case reuses it."""

    def __init__(self, source: Path, build: Path, observer: Path, output: Path,
                 heads: int, mode: str, durations: Durations, label: str) -> None:
        self.source, self.build, self.observer = source, build, observer
        self.output, self.heads, self.mode, self.durations = output, heads, mode, durations
        self.output.mkdir(parents=True)
        self.shim = Shim(output / "shim", build, source / "bin/hydra")
        self.fixture = Fixture(source, build, output, heads, mode)
        self.fixture.env = self.shim.environment(self.fixture.env)
        self.roots: dict[int, str] = {os.getpid(): "controller"}
        self.worktrees: dict[str, str] = {}
        self.sessions: dict[str, str] = {}
        self.phase_serial = 0
        self.record: dict[str, Any] = {
            "id": label, "heads": heads, "mode": mode, "cells": [], "errors": [],
            "shim_overhead": self.shim.calibrate_overhead(),
            "uptime_clock": self.shim.offset,
        }

    def provision(self) -> None:
        started = time.monotonic_ns()
        mark = self.shim.mark()
        workers = self.fixture.setup()
        finished = time.monotonic_ns()
        for row in workers:
            self.roots[row["pid"]] = "agent"
            if row.get("owner_pid"):
                self.roots[row["owner_pid"]] = "execution-owner"
            if "pane_pid" in row:
                self.roots[row["pane_pid"]] = "pane-shell"
            self.worktrees[row["worktree"]] = row["branch"]
            if row["session"] != "-":
                self.sessions[row["session"]] = row["branch"]
        if self.mode == "interactive":
            server = self.fixture.command(
                ["tmux", "-S", str(self.fixture.socket), "display-message", "-p", "#{pid}"]).stdout.strip()
            self.roots[int(server)] = "tmux-server"
        spawns = []
        for line in (self.output / "commands.jsonl").read_text().splitlines():
            command = json.loads(line)
            if command["argv"][1:2] == ["spawn"]:
                branch = command["argv"][2]
                worker = next(row for row in workers if row["branch"] == branch)
                spawns.append({"branch": branch, "spawn_ms": (command["finished_ns"] - command["started_ns"]) / 1e6,
                               "launch_to_worker_ready_ms": (worker["ready"]["monotonic_ns"] - command["started_ns"]) / 1e6})
        launches = summarize_launches(self.shim.records(mark), self.roots, self.worktrees, self.sessions,
                                      (finished - started) / 1e9)
        self.record["provisioning"] = {
            "total_ms": (finished - started) / 1e6, "spawns": spawns,
            "spawn_ms_median": sorted(row["spawn_ms"] for row in spawns)[len(spawns) // 2],
            "helper_launches": launches["by_helper"], "workers": len(workers),
            "queued_workers": 0, "note": "sequential public spawns including repository seeding; startup, not steady state",
        }

    def start_clients(self, count: int, preview: str) -> tuple[list[Observer], list[ClientState]]:
        clients, states = [], []
        receipts = []
        for index in range(count):
            serial = len(list(self.output.glob("client-*")))
            client = Observer(self.observer, self.build / "hydra-tui", self.shim.wrappers["hydra"],
                              self.output / f"client-{serial}", self.fixture.repo, self.fixture.env)
            clients.append(client)
            _, ready = client.ready({row["branch"] for row in self.fixture.workers})
            spawn = client.wait("spawn")
            raw = client.wait("raw-ready")
            self.roots[spawn["tui_pid"]] = f"native-tui#{index}"
            self.roots[spawn["observer_pid"]] = f"pty-observer#{index}"
            receipts.append({"client": index, "raw_mode_ms": (raw["observed_ns"] - spawn["started_ns"]) / 1e6,
                             "time_to_usable_ms": (ready["observed_ns"] - spawn["started_ns"]) / 1e6,
                             "usable_meaning": "all owned heads visible with exact cardinality (PTY frame completion)",
                             "clock": client.calibrate_clock()})
            attached = preview == "open"
            if attached:
                receipts[-1]["terminal_clients"] = _attach(client, self.fixture, index + 1)
            states.append(ClientState(attached))
        self.record.setdefault("client_startups", []).extend(receipts)
        return clients, states

    def close_clients(self, clients: list[Observer], states: list[ClientState]) -> list[dict[str, Any]]:
        receipts = []
        for client, state in zip(clients, states):
            receipt = client.close(attached=state.attached)
            receipts.append(receipt)
            if not receipt.get("terminal_restored") or receipt.get("forced") or receipt.get("observer_exit"):
                self.record["errors"].append(f"client cleanup incomplete: {receipt}")
        self.record.setdefault("client_closures", []).extend(receipts)
        return receipts

    def _changes_of(self, worker: dict[str, Any], arm_ns: int) -> list[dict[str, Any]]:
        return [row for row in json_lines(Path(worker["receipt"]))
                if row["event"] == "repository-change" and row["monotonic_ns"] >= arm_ns]

    def _await_stop(self, phase: str, deadline_ns: int) -> list[dict[str, Any]]:
        stops = []
        for worker in self.fixture.workers:
            while time.monotonic_ns() < deadline_ns:
                rows = [row for row in json_lines(Path(worker["receipt"]))
                        if row["event"] == "generation-stopped" and row["phase"] == phase]
                if rows:
                    stops.append(rows[0])
                    break
                time.sleep(0.05)
            else:
                raise TimeoutError("worker did not stop generating within the recovery guard")
        return stops

    def _await_drain(self, stops: list[dict[str, Any]], deadline_ns: int) -> dict[str, Any]:
        """A slow sink may still hold producer backlog after the declared end; wait, bounded."""
        waits, timed_out = [], 0
        for worker, stop in zip(self.fixture.workers, stops):
            started = time.monotonic_ns()
            while True:
                drains = [row for row in json_lines(Path(worker["receipt"]))
                          if row["event"] == "output-drained" and row["monotonic_ns"] >= stop["monotonic_ns"] - 1]
                if (not stop["generated_bytes"] or not stop["backlog_bytes"]
                        or (drains and drains[-1]["total_written_bytes"] == stop["generated_bytes"])):
                    break
                if time.monotonic_ns() >= deadline_ns:
                    timed_out += 1
                    break
                time.sleep(0.1)
            waits.append(time.monotonic_ns() - started)
        return {"controller_wait_ns": waits, "timed_out_workers": timed_out,
                "scope": "time this controller waited after generation stopped until each producer reported its backlog drained"}

    def run_case(self, clients: list[Observer], states: list[ClientState], case: str,
                 meta: dict[str, Any], interval_ns: int | None) -> dict[str, Any]:
        d = self.durations
        self.phase_serial += 1
        phase = f"p{self.phase_serial}"
        cell_dir = self.output / f"cell-{phase}"
        cell_dir.mkdir()
        cell: dict[str, Any] = {**meta, "phase": phase, "case": case, "status": "incomplete",
                                "directory": str(cell_dir), "unknowns": {}}
        changing = set(range(self.heads)) if case == "all-changing" else {0} if case == "one-changing" else set()
        sampler = WindowSampler(self.roots)
        now = time.monotonic_ns()
        cell["windows"] = {}
        # Settle: sampled, unstimulated, reported separately from steady state.
        settle = sampler.run(now, now + d.settle_ns)
        start = time.monotonic_ns() + 300_000_000
        duration = d.idle_ns + d.interaction_ns
        for index, worker in enumerate(self.fixture.workers):
            command = {"action": "phase", "id": phase, "case": case, "start_ns": start,
                       "duration_ns": duration, "change": index in changing}
            if interval_ns and case == "sustained-output":
                command["interval_ns"] = interval_ns
            self.fixture.tell(worker, command)
        usage_before = resource.getrusage(resource.RUSAGE_SELF)
        mark = self.shim.mark()
        wait_until(start)
        poller = ChangePoller(clients[0], d.poll_ns) if changing else None
        window_marks = [clients[0].snapshot()[1]["sequence"]]
        if poller:
            poller.start(start + d.idle_ns)
        idle = sampler.run(start, start + d.idle_ns)
        window_marks.append(clients[0].snapshot()[1]["sequence"])
        interaction: list[dict[str, Any]] = []
        thread = threading.Thread(target=lambda: interaction.extend(
            sampler.run(start + d.idle_ns, start + duration)), daemon=True)
        thread.start()
        actions = run_actions(clients[0], states[0], start + d.idle_ns + 200_000_000, d.gap_ns, phase,
                              cell_dir / "actions.json")
        thread.join(timeout=d.interaction_ns / 1e9 + 10)
        end = start + duration
        # A blocked producer (full sink pipe) may finish its schedule late; wait,
        # bounded, and report the lateness instead of failing the cell.
        stops = self._await_stop(phase, end + 180_000_000_000)
        cell["stop_lateness_ns"] = [max(0, stop["monotonic_ns"] - end) for stop in stops]
        cell["drain"] = self._await_drain(stops, end + 180_000_000_000)
        usage_after = resource.getrusage(resource.RUSAGE_SELF)
        records = self.shim.records(mark)
        roles = {**role_map(settle + idle + interaction), **self.roots}
        for name, samples, window_start, window_end in (
                ("settle", settle, now, now + d.settle_ns), ("idle", idle, start, start + d.idle_ns),
                ("interaction", interaction, start + d.idle_ns, end)):
            summary = summarize_window(samples)
            subset = [row for row in records if "malformed" in row or window_start <= row["monotonic_ns"] < window_end]
            cell["windows"][name] = {
                "start_ns": window_start, "end_ns": window_end, "processes": summary,
                "families": family_totals(summary),
                "launches": summarize_launches(subset, roles, self.worktrees, self.sessions,
                                               (window_end - window_start) / 1e9),
            }
        (cell_dir / "samples.json").write_text(json.dumps(
            {"settle": settle, "idle": idle, "interaction": interaction}, separators=(",", ":")) + "\n")
        (cell_dir / "launches.json").write_text(json.dumps(records, separators=(",", ":")) + "\n")
        cell["actions"] = [{key: value for key, value in action.items() if key != "receipts"} for action in actions]
        cell["controller_cpu_seconds"] = (usage_after.ru_utime + usage_after.ru_stime
                                          - usage_before.ru_utime - usage_before.ru_stime)
        cell["locality"] = self._locality(cell["windows"]["idle"]["launches"], changing)
        if poller:
            arm_ns = start - 300_000_000
            cell["update_latency"] = poller.finish(self._changes_of(self.fixture.workers[0], arm_ns))
            cell["update_latency"]["unselected_worktrees"] = (
                "not displayed: the Work view shows the changed-file count of the selected head only; "
                "no matching display state exists for unselected heads, so their update latency is not applicable here")
        else:
            cell["unknowns"]["update_latency"] = "no repository changes in this case"
        cell["pressure"] = self._pressure(case, stops, end, idle, interaction, clients, window_marks,
                                          interval_ns or 31_250_000, duration)
        try:
            cell["workload_proof"] = phase_proof(self.fixture, case, start, duration, phase=phase,
                                                 interval_ns=interval_ns if case == "sustained-output" else None,
                                                 changing=changing)
            cell["status"] = "measured"
        except RuntimeError as error:
            # Keep the cell's evidence; a failed proof is reported, not hidden.
            cell["workload_proof"] = []
            cell["workload_proof_error"] = repr(error)
            cell["status"] = "proof-failed"
        cell["phase_start_ns"], cell["phase_end_ns"] = start, end
        if changing:
            # Each changing cell starts from the committed seed so the visible
            # changed-file count can rise again; receipts keep every edit.
            for index in changing:
                self.fixture.command(["git", "-C", self.fixture.workers[index]["worktree"], "checkout", "--", "."])
            cell["seed_restored_after_cell"] = sorted(self.fixture.workers[index]["branch"] for index in changing)
        restore_work_view(clients[0], states[0])
        if meta.get("preview") == "open" and not states[0].attached:
            _attach(clients[0], self.fixture, 1)
            states[0].attached = True
        (cell_dir / "cell.json").write_text(json.dumps(cell, indent=1) + "\n")
        self.record["cells"].append(cell)
        return cell

    def _locality(self, launches: dict[str, Any], changing: set[int]) -> dict[str, Any]:
        per = launches["per_worktree"]
        changed = {self.fixture.workers[index]["branch"] for index in changing}
        unchanged = {branch: counts for branch, counts in per.items() if branch not in changed}
        return {
            "changed_worktrees": sorted(changed),
            "git_launches_changed": {branch: per[branch]["git"] for branch in sorted(changed)},
            "git_launches_per_unchanged_worktree": [counts["git"] for counts in unchanged.values()],
            "captures_per_unchanged_worktree": [counts["tmux_capture"] for counts in unchanged.values()],
            "unchanged_worktrees": len(unchanged),
            "scope": "idle window; git launches attributed by -C <worktree>, tmux captures by -t <session>",
        }

    def _pressure(self, case: str, stops: list[dict[str, Any]], end: int, idle: list, interaction: list,
                  clients: list[Observer], marks: list[int], interval_ns: int, duration_ns: int) -> dict[str, Any]:
        recoveries = []
        for worker in self.fixture.workers:
            rows = json_lines(Path(worker["receipt"]))
            generated = [row for row in rows if row["event"] == "output-scheduled"]
            if not generated:
                recoveries.append(None)
                continue
            last = generated[-1]["monotonic_ns"]
            drains = [row for row in rows if row["event"] == "output-drained" and row["monotonic_ns"] >= last]
            # Backlog drained before the declared end counts as zero recovery time.
            recoveries.append(max(0, drains[-1]["monotonic_ns"] - end) if drains else None)
        growth: dict[str, Any] = {}
        first, last = (idle[0] if idle else None), (interaction[-1] if interaction else None)
        if first and last:
            for pid, row in last["processes"].items():
                before = first["processes"].get(pid)
                if before and before.get("available") and row.get("available"):
                    family = row["role"].split("/")[0].split("#")[0]
                    growth[family] = growth.get(family, 0) + row["resident_bytes"] - before["resident_bytes"]
        reads = []
        for client in clients:
            try:
                reads.append(client.snapshot()[1]["sequence"])
            except (OSError, RuntimeError, TimeoutError):
                reads.append(None)
        return {
            "declared": {"producers": self.heads if case == "sustained-output" else 0,
                         "line_bytes": 128, "interval_ns": interval_ns,
                         "bytes_per_second_per_producer": 128e9 / interval_ns if case == "sustained-output" else 0,
                         "duration_ns": duration_ns, "sink": "tmux pane" if self.mode == "interactive" else "retained provider stdout"},
            "producer_receipts": [{"generated_bytes": s["generated_bytes"], "written_bytes": s["written_bytes"],
                                   "backlog_bytes_at_stop": s["backlog_bytes"], "write_eagain_count": s["write_eagain_count"],
                                   "queue_nonempty_ns": s["queue_nonempty_ns"], "peak_backlog_bytes": s["peak_backlog_bytes"]}
                                  for s in stops],
            "recovery_after_stop_ns": recoveries,
            "recovery_scope": "producer backlog fully drained after the declared end; display catch-up is unmeasured",
            "resident_growth_bytes_by_role": growth,
            "client_pty_reads_idle_window": marks[1] - marks[0],
            "client_pty_reads_total_by_client": [None if r is None else r for r in reads],
            "coalesced_updates": None,
            "coalesced_updates_reason": "the TUI repaints on its 2 s period, not per output event; dropped/coalesced frames are not exposed by the renderer",
        }

    def finish(self) -> None:
        try:
            # Supervised (headless) owners retain every observation event after
            # the worker exits; allow for that and measure it instead of failing.
            events = max((sum(1 for row in json_lines(Path(w["receipt"])) if row["event"] == "output-scheduled")
                          for w in self.fixture.workers), default=0)
            self.record["worker_results"] = self.fixture.finish_workers(
                owner_timeout=60 + 0.5 * events, completion_timeout=150)
            self.record["result_integrity"] = "every worker's final result bytes matched its token after the output phases"
            self.record["result_retention"] = {
                "retained_output_events_max_per_worker": events,
                "final_drain_wait_ns": [r["receipt"].get("final_drain_wait_ns") for r in self.record["worker_results"]],
                "owner_exit_after_finish_ns": getattr(self.fixture, "owner_exit_waits_ns", []),
                "scope": "headless only: time from the finish command to the public hydra exec result; interactive workers have no owner",
            }
        except (OSError, RuntimeError, TimeoutError, ValueError) as error:
            # Public retention failing under output load is a finding, not a
            # harness failure: record it and check the worker artifacts directly.
            artifacts = {}
            for worker in self.fixture.workers:
                try:
                    artifacts[worker["branch"]] = (Path(worker["worktree"]) / "item10-result.txt").read_text() == worker["token"]
                except OSError:
                    artifacts[worker["branch"]] = False
            self.record["result_retention"] = {
                "status": "failed", "error": repr(error),
                "worker_artifact_bytes_verified": artifacts,
                "owner_exits": [owner.poll() for owner in self.fixture.owners],
                "scope": "public hydra exec retention of the supervised run; see the evidence summary",
            }
            self.record["result_integrity"] = "public retention failed; worker artifacts checked directly"
        finally:
            self.record["cleanup"] = self.fixture.cleanup()
            self.record["cleanup_ok"] = bool(self.record["cleanup"].get("cleanup_ok"))
