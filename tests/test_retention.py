"""Public local retention boundaries. Every mutation targets a disposable home."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
FLEET = Path(os.environ.get("HYDRA_FLEET_BIN", ROOT / "build/hydra-fleet")).resolve()


class Retention(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-retention-")
        self.base = Path(self.temp.name)
        self.home = self.base / "home"
        self.home.mkdir(mode=0o700)
        self.env = {**os.environ, "HYDRA_HOME": str(self.home)}
        self.policy = self.base / "policy.json"
        self.write(self.policy, {"schema_version": 1, "audit_days": 1,
                                 "max_bytes": 1048576, "max_evidence_records": 100})

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        path.write_text(json.dumps(value))

    @staticmethod
    def old(directory):
        stamp = time.time() - 3 * 86400
        for path in directory.rglob("*"):
            if not path.is_symlink():
                os.utime(path, (stamp, stamp))

    def task(self, key="a", state="succeeded"):
        ident = "task_" + hashlib.sha256(json.dumps({"submission_key": key}, separators=(",", ":")).encode()).hexdigest()
        path = self.home / "fleet/tasks" / ident
        self.write(path / "acceptance.json", {"schema_version": 1, "task_id": ident,
                   "project": "/fixture", "project_id": "project_fixture", "submission_key": key,
                   "spec_sha256": "a" * 64, "accepted_at": 1})
        self.write(path / "state.json", {"schema_version": 1, "state": state,
                   "launch_intent": "started", "run_id": "run_inner", "exit_status": 0,
                   "finished_at": 1, "result_state": "ready"})
        self.write(path / "package.json", {"fixture": True})
        self.write(path / "result.json", {"fixture": "sealed evidence"})
        (path / "stdout").write_text("recorded output\n")
        (path / "workspace").mkdir()
        (path / "workspace/dirty-user-file").write_text("keep workspace")
        self.old(path)
        return path

    def run_record(self, ident="run_outer", state="succeeded", task=None, key=None):
        path = self.home / "state/v2/projects/project_fixture/workflows/runs" / ident
        path.mkdir(parents=True, mode=0o700)
        for name, text in {"state": state, "run-id": ident, "project-id": "project_fixture",
                           "graph.tsv": "step\twork\texec", "completed-at": "1"}.items():
            (path / name).write_text(text + "\n")
        self.write(path / "compiled.json", {"fixture": "contract"})
        attempt = path / "steps/work/attempt-1/remote"
        self.write(attempt / "receipt.json", {"task_id": task} if task else {"submission_key": key} if key else {})
        (path / "steps/work/state").write_text("succeeded\n")
        (path / "steps/work/attempts").write_text("1\n")
        self.old(path)
        return path

    def command(self, *args, ok=True):
        proc = subprocess.run([str(ROOT / "bin/hydra"), "fleet", "retention", *args],
                              env={**self.env, "HYDRA_FLEET_BIN": str(FLEET)},
                              capture_output=True, text=True)
        value = json.loads(proc.stdout)
        self.assertEqual(proc.returncode == 0, ok, proc.stdout + proc.stderr)
        return value

    def action(self, action="apply", ok=True):
        return self.command(action, "--policy", str(self.policy), ok=ok)

    def task_operation(self, task, operation):
        request = {"protocol": 1, "action": "task", "operation": operation, "task_id": task.name}
        proc = subprocess.run([str(FLEET), "fleet", "serve"], input=json.dumps(request),
                              env=self.env, capture_output=True, text=True)
        return json.loads(proc.stdout)

    def test_preview_then_expiry_keeps_identity_and_workspaces(self):
        task = self.task()
        before = (task / "acceptance.json").read_bytes()
        preview = self.action("preview")["data"]
        self.assertTrue(preview["capacity_satisfied"])
        self.assertFalse(preview["applied"])
        self.assertTrue((task / "result.json").exists())
        self.assertTrue(self.action()["data"]["applied"])
        self.assertEqual((task / "acceptance.json").read_bytes(), before)
        self.assertFalse((task / "result.json").exists())
        self.assertEqual((task / "workspace/dirty-user-file").read_text(), "keep workspace")
        status = self.task_operation(task, "status")
        self.assertTrue(status["ok"], status)
        self.assertEqual(status["data"]["runtime"]["result_state"], "expired")
        for operation in ("result", "logs", "observe"):
            self.assertEqual(self.task_operation(task, operation)["error"]["code"], "evidence_expired")
        self.assertTrue(self.action()["data"]["applied"])

    def test_active_and_unknown_work_never_expires(self):
        task = self.task(state="outcome_unknown")
        self.run_record(state="waiting-remote", task=task.name)
        result = self.action()["data"]
        self.assertTrue(all(item["action"] == "preserve" for item in result["records"]))
        self.assertTrue((task / "result.json").exists())

    def test_expired_overview_discloses_history_and_keeps_its_bound(self):
        root = self.home / "fleet/tasks"
        for number in range(513):
            self.write(root / ("task_" + f"{number:064x}") / "retention.json",
                       {"schema_version": 1, "state": "expired"})
            if number not in (511, 512):
                continue
            proc = subprocess.run([str(FLEET), "fleet", "serve"],
                input=json.dumps({"protocol": 1, "action": "overview"}),
                env=self.env, capture_output=True, text=True)
            response = json.loads(proc.stdout)
            if number == 511:
                self.assertTrue(response["ok"], response)
                self.assertEqual(response["data"]["expired_task_count"], 512)
                self.assertEqual(response["data"]["tasks"], [])
            else:
                self.assertFalse(response["ok"], response)
                self.assertEqual(response["error"]["code"], "limit")

    def test_references_include_lost_acceptance_ack_key(self):
        task = self.task(key="lost-ack")
        run = self.run_record(state="waiting-remote", key="lost-ack")
        result = self.action()["data"]
        item = next(item for item in result["records"] if item["id"] == task.name)
        self.assertEqual(item["reason"], "referenced_evidence")
        self.assertTrue((task / "result.json").exists())
        self.assertTrue((run / "compiled.json").exists())

    def test_pin_keeps_transitive_evidence_then_expiry_is_disclosed(self):
        task = self.task()
        run = self.run_record(task=task.name)
        self.command("pin", "run", run.name)
        self.action()
        self.assertTrue((task / "result.json").exists())
        self.command("unpin", "run", run.name)
        self.action()
        self.assertFalse((task / "result.json").exists())
        self.assertFalse((run / "compiled.json").exists())
        result = subprocess.run([str(FLEET), "workflow-plan", "result", str(run)], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(json.loads(result.stdout)["error"]["code"], "evidence_expired")
        self.assertEqual(self.command("pin", "run", run.name, ok=False)["error"]["code"], "evidence_expired")

    def test_quota_refusal_does_not_remove_other_candidates(self):
        old = self.task("old")
        self.task("unknown", state="outcome_unknown")
        policy = json.loads(self.policy.read_text()); policy["max_evidence_records"] = 0; self.write(self.policy, policy)
        result = self.action(ok=False)
        self.assertEqual(result["error"]["code"], "protected_capacity_exceeded")
        self.assertTrue((old / "result.json").exists())

    def test_owner_locks_and_audit_window_preserve_records(self):
        task = self.task()
        run = self.run_record()
        with (task / "owner.lock").open("w") as owner, (run / "coordinator.lock").open("w") as coordinator:
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.lockf(coordinator, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.action()
            self.assertTrue((task / "result.json").exists())
            self.assertTrue((run / "compiled.json").exists())
        os.utime(task / "result.json", None)
        self.action()
        self.assertTrue((task / "result.json").exists())

    def test_link_and_corrupt_receipt_fail_closed(self):
        task = self.task()
        target = self.base / "unrelated"; target.write_text("preserved")
        (task / "stdout").unlink(); (task / "stdout").symlink_to(target)
        self.assertEqual(self.action(ok=False)["error"]["code"], "incomplete_inventory")
        self.assertEqual(target.read_text(), "preserved")
        (task / "stdout").unlink()
        run = self.run_record(task=task.name)
        (run / "steps/work/attempt-1/remote/receipt.json").write_text("bad")
        self.assertEqual(self.action(ok=False)["error"]["code"], "incomplete_inventory")
        self.assertTrue((task / "result.json").exists())

    def test_interrupted_expiry_resumes_original_manifest(self):
        task = self.task()
        self.action()
        marker = json.loads((task / "retention.json").read_text())
        marker["state"] = "expiring"; self.write(task / "retention.json", marker)
        self.assertEqual(self.task_operation(task, "result")["error"]["code"], "evidence_expired")
        (task / "stdout").write_text("recorded output\n")
        self.action()
        self.assertFalse((task / "stdout").exists())
        (task / "stdout").write_text("new unexpected bytes")
        self.assertEqual(self.action(ok=False)["error"]["code"], "expiry_incomplete")
        self.assertEqual((task / "stdout").read_text(), "new unexpected bytes")

    def test_invalid_policy_and_manifest_refuse(self):
        task = self.task()
        original = self.policy.read_text()
        self.policy.write_text(original[:-1] + ',"audit_days":0}')
        self.assertEqual(self.action(ok=False)["error"]["code"], "invalid_input")
        self.policy.write_text(original)
        self.action()
        (task / "retention-manifest.json").write_text('{"files":[]}')
        self.assertEqual(self.action(ok=False)["error"]["code"], "expiry_incomplete")

    def test_declared_audit_window_cannot_be_shortened(self):
        task = self.task()
        policy = json.loads(self.policy.read_text())
        policy["audit_days"] = 30
        self.write(self.policy, policy)
        self.action()
        audit = (task / "retention-audit.json").read_bytes()
        self.assertTrue((task / "result.json").exists())
        policy["audit_days"] = 1
        self.write(self.policy, policy)
        result = self.action()["data"]
        self.assertEqual(result["records"][0]["reason"], "declared_audit_window")
        self.assertEqual((task / "retention-audit.json").read_bytes(), audit)
        self.assertTrue((task / "result.json").exists())
        (task / "retention-audit.json").write_text("broken")
        self.action()
        self.assertTrue((task / "result.json").exists())

    def test_incomplete_acceptance_keeps_original_evidence(self):
        for index, field in enumerate(("project", "project_id", "accepted_at")):
            task = self.task("missing-" + str(index))
            acceptance = json.loads((task / "acceptance.json").read_text())
            del acceptance[field]
            self.write(task / "acceptance.json", acceptance)
            self.old(task)
        result = self.action()["data"]
        self.assertTrue(all(item["action"] == "preserve" for item in result["records"]))

    def test_capacity_reserves_audit_metadata(self):
        task = self.task(state="outcome_unknown")
        original = self.action("preview")["data"]["bytes_before"]
        (task / "stdout").write_bytes(b"x" * (4096 - original + len("recorded output\n")))
        policy = json.loads(self.policy.read_text())
        policy["max_bytes"] = 4096
        self.write(self.policy, policy)
        result = self.action(ok=False)
        self.assertEqual(result["error"]["code"], "protected_capacity_exceeded")
        self.assertFalse((task / "retention-audit.json").exists())

    def test_terminal_header_does_not_override_unresolved_steps(self):
        for index, state in enumerate(("queued", "ready", "retrying", "unrecognized")):
            run = self.run_record(ident="run_state_" + str(index))
            (run / "steps/work/state").write_text(state + "\n")
            self.old(run)
        result = self.action()["data"]
        self.assertTrue(all(item["action"] == "preserve" and item["reason"] == "unresolved_step"
                            for item in result["records"]))


if __name__ == "__main__":
    unittest.main()
