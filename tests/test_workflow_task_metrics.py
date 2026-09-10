"""Bounded native metric reads preserve missing and malformed evidence as unknown."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FLEET = Path(os.environ.get("HYDRA_FLEET_BIN", ROOT / "build/hydra-fleet")).resolve()

class Metrics(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-metric-read-")
        self.run = Path(self.temp.name)
        (self.run / "run-id").write_text("run_fixture\n")
        self.write(self.run / "tasks.json", {"schema_version": 1, "steps": {"work": {}}})
        self.step = self.run / "steps/work"
        self.step.mkdir(parents=True)
        (self.step / "attempts").write_text("1\n")
        self.remote = self.attempt(1)
        self.events("run.created", "run.recovered")

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value))

    def events(self, *types):
        values = [{"schema_version": 1, "sequence": i + 1, "run_id": "run_fixture",
                   "occurred_at": "2026-09-10T10:00:00Z", "step_id": None,
                   "type": name, "detail": ""} for i, name in enumerate(types)]
        (self.run / "events.jsonl").write_text("".join(json.dumps(v) + "\n" for v in values))

    def attempt(self, number, state="succeeded"):
        remote = self.step / f"attempt-{number}/remote"
        remote.mkdir(parents=True)
        receipt = {"task_id": "task_" + "a" * 64, "spec_sha256": "b" * 64,
                   "submission_key": "fixture", "runtime": {"schema_version": 1,
                   "state": state, "launch_intent": "started", "run_id": "run_receiver", "exit_status": 0}}
        self.write(remote / "receipt.json", receipt)
        self.write(remote / "observation.json", receipt)
        self.write(remote / "transport-metrics.json", {"schema_version": 1, "calls": 2,
                   "request_bytes": 10, "response_bytes": 20, "complete": True})
        return remote

    def read(self):
        result = subprocess.run([str(FLEET), "workflow-task", "metrics", str(self.run)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)["data"]
        tsv = subprocess.run([str(FLEET), "workflow-task", "metrics-tsv", str(self.run)],
                             capture_output=True, text=True, timeout=10)
        self.assertEqual(tsv.returncode, 0, tsv.stderr)
        for row in tsv.stdout.splitlines():
            name, state, eligible, known, total = row.split("\t")
            self.assertEqual(data[name], {"state": state, "eligible": int(eligible),
                             "known": int(known), "sum": None if total == "-" else int(total)})
        return data

    def test_known_zero_and_recorded_actions(self):
        data = self.read()
        self.assertEqual(data["unknown_receiver_outcomes"], {"state": "known", "eligible": 1, "known": 1, "sum": 0})
        self.assertEqual(data["recorded_operator_actions"]["sum"], 0)
        self.assertEqual(data["transport_stdio_bytes"]["sum"], 30)
        self.events("run.created", "run.recovered", "run.resume_requested", "approval.decided", "run.cancel_requested")
        self.assertEqual(self.read()["recorded_operator_actions"]["sum"], 3)

    def test_unknown_receiver_and_partial_attempt_coverage(self):
        self.attempt(2, "outcome_unknown")
        (self.step / "attempts").write_text("2\n")
        self.assertEqual(self.read()["unknown_receiver_outcomes"], {"state": "known", "eligible": 2, "known": 2, "sum": 1})
        (self.step / "attempts").write_text("3\n")
        data = self.read()
        self.assertEqual(data["unknown_receiver_outcomes"], {"state": "partial", "eligible": 3, "known": 2, "sum": None})
        self.assertEqual(data["transport_stdio_bytes"]["state"], "partial")

    def test_receipt_binding_and_runtime_are_required(self):
        observation = json.loads((self.remote / "observation.json").read_text())
        observation["task_id"] = "task_" + "c" * 64
        self.write(self.remote / "observation.json", observation)
        self.assertEqual(self.read()["unknown_receiver_outcomes"]["state"], "unknown")
        self.write(self.remote / "observation.json", {})
        self.assertIsNone(self.read()["unknown_receiver_outcomes"]["sum"])

    def test_events_require_complete_contiguous_bound_history(self):
        path = self.run / "events.jsonl"
        original = path.read_text()
        for malformed in ("", original.rstrip("\n"), original.replace('"sequence": 2', '"sequence": 3'),
                          original.replace("run_fixture", "run_foreign"), '{"type":"run.created"}\n',
                          "\n".join(original.splitlines()[1:]) + "\n"):
            path.write_text(malformed)
            self.assertEqual(self.read()["recorded_operator_actions"]["state"], "unknown", malformed)

    def test_transfer_malformed_overflow_and_interruption(self):
        path = self.remote / "transport-metrics.json"
        original = json.loads(path.read_text())
        for field, value in (("request_bytes", -1), ("request_bytes", 2**64), ("calls", 0),
                             ("response_bytes", "20"), ("complete", False), ("complete", 1)):
            self.write(path, {**original, field: value})
            self.assertEqual(self.read()["transport_stdio_bytes"]["state"], "unknown", (field, value))
        path.write_text(json.dumps(original).replace('"calls": 2', '"calls": 0, "calls": 2'))
        self.assertEqual(self.read()["transport_stdio_bytes"]["state"], "unknown")
        self.write(path, original)
        (self.remote / "transport-incomplete").write_text("incomplete\n")
        self.assertIsNone(self.read()["transport_stdio_bytes"]["sum"])

    def test_bounds_links_and_expiry(self):
        path = self.remote / "observation.json"
        path.unlink(); path.symlink_to(self.remote / "receipt.json")
        self.assertEqual(self.read()["unknown_receiver_outcomes"]["state"], "unknown")
        (self.step / "attempts").write_text("4097\n")
        self.assertEqual(self.read()["transport_stdio_bytes"]["state"], "unknown")
        (self.run / "retention.json").write_text("{}")
        self.assertTrue(all(value["state"] == "unknown" and value["sum"] is None for value in self.read().values()))

    def test_no_attempts_is_unavailable_not_measured_zero(self):
        (self.step / "attempts").write_text("0\n")
        data = self.read()
        self.assertEqual(data["transport_stdio_bytes"], {"state": "unavailable", "eligible": 0, "known": 0, "sum": None})

if __name__ == "__main__":
    unittest.main()
