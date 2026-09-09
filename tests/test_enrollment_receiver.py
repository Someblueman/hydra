#!/usr/bin/env python3
"""Exercise the real receiver and init command across lost-owner boundaries."""
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


class EnrollmentReceiverTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "project"
        subprocess.run(["git", "init", "-q", str(self.repo)], check=True)
        self.home = self.root / "home"
        self.home.mkdir()
        self.counter = self.root / "mutations"
        source = Path(__file__).resolve().parents[1]
        self.fleet = os.environ.get("HYDRA_FLEET_BIN", str(source / "build/hydra-fleet"))
        wrapper = self.root / "hydra-count"
        wrapper.write_text(
            "#!/usr/bin/env python3\n"
            "import os, pathlib, sys, time\n"
            "if 'init' in sys.argv:\n"
            "    pathlib.Path(os.environ['ENROLL_CHILD_PID']).write_text(str(os.getpid()))\n"
            "    pathlib.Path(os.environ['ENROLL_STARTED']).touch()\n"
            "    while os.environ.get('ENROLL_HOLD') and not pathlib.Path(os.environ['ENROLL_HOLD']).exists(): time.sleep(.01)\n"
            "    with open(os.environ['ENROLL_COUNTER'], 'a') as count: count.write('init\\n')\n"
            "os.execv(os.environ['ENROLL_REAL'], [os.environ['ENROLL_REAL'], *sys.argv[1:]])\n"
        )
        wrapper.chmod(0o755)
        self.env = dict(os.environ, HYDRA_HOME=str(self.home), HOME=str(self.root),
                        HYDRA_BIN_CMD=str(wrapper), ENROLL_REAL=str(source / "bin/hydra"),
                        ENROLL_COUNTER=str(self.counter), ENROLL_CHILD_PID=str(self.root / "child-pid"), ENROLL_STARTED=str(self.root / "started"))
        self.request = {"protocol": 1, "action": "init", "project": str(self.repo),
                        "args": ["--no-agent", "--trust"], "enrollment_operation_id": "a" * 64 + ":host", "expected_peer_fingerprint": "SHA256:reviewed"}

    def start(self, request=None):
        process = subprocess.Popen([self.fleet, "fleet", "serve"], env=self.env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, start_new_session=True)
        process.stdin.write(json.dumps(self.request if request is None else request))
        process.stdin.close()
        process.stdin = None
        return process

    def call(self, request=None):
        process = self.start(request)
        output, error = process.communicate(timeout=30)
        self.assertTrue(output, error)
        return json.loads(output)

    def count(self):
        return self.counter.read_text().splitlines() if self.counter.exists() else []

    def await_started(self, process):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and process.poll() is None:
            if (self.root / "started").exists():
                return
            time.sleep(.01)
        self.fail("receiver did not reach the held init boundary")

    def kill_owner(self, process):
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            child = self.root / "child-pid"
            if child.exists():
                try:
                    os.killpg(int(child.read_text()), signal.SIGKILL)
                except ProcessLookupError:
                    pass
        process.communicate(timeout=10)

    def test_duplicate_reconciles_exact_result_after_lost_response(self):
        first = self.call()
        self.assertTrue(first["ok"], first)
        # The caller may discard this response; retry must return its durable result.
        second = self.call()
        self.assertEqual(second, first)
        self.assertEqual(self.count(), ["init"])

    def test_changed_project_or_arguments_cannot_reuse_operation(self):
        self.assertTrue(self.call()["ok"])
        other = self.root / "other"
        subprocess.run(["git", "init", "-q", str(other)], check=True)
        for changed in (dict(self.request, project=str(other)), dict(self.request, args=["--no-agent"]), dict(self.request, expected_peer_fingerprint="SHA256:changed")):
            self.assertEqual(self.call(changed)["error"]["code"], "intent_changed")
        self.assertEqual(self.count(), ["init"])

    def test_concurrent_duplicate_has_one_mutation(self):
        release = self.root / "release"
        self.env["ENROLL_HOLD"] = str(release)
        first = self.start()
        self.addCleanup(self.kill_owner, first)
        self.await_started(first)
        self.assertEqual(self.call()["error"]["code"], "outcome_unknown")
        self.assertEqual(self.count(), [])
        release.touch()
        output, error = first.communicate(timeout=30)
        self.assertTrue(json.loads(output)["ok"], error)
        self.assertEqual(self.call(), json.loads(output))
        self.assertEqual(self.count(), ["init"])

    def test_owner_loss_preserves_unknown_without_replay(self):
        self.env["ENROLL_HOLD"] = str(self.root / "never-release")
        first = self.start()
        self.addCleanup(self.kill_owner, first)
        self.await_started(first)
        self.kill_owner(first)
        self.env.pop("ENROLL_HOLD")
        self.assertEqual(self.call()["error"]["code"], "outcome_unknown")
        self.assertEqual(self.count(), [])

    def test_invalid_ids_and_unavailable_state_have_zero_mutations(self):
        for operation in (None, 7, "../escape", "a" * 64 + ":../escape"):
            self.assertEqual(self.call(dict(self.request, enrollment_operation_id=operation))["error"]["code"], "invalid_input")
        (self.home / "fleet").mkdir()
        (self.home / "fleet/enrollment-ops").write_text("not a directory")
        self.assertEqual(self.call()["error"]["code"], "state_unavailable")
        self.assertEqual(self.count(), [])

    def test_legacy_or_corrupt_receipt_never_implies_success(self):
        directory = self.home / "fleet/enrollment-ops"
        directory.mkdir(parents=True)
        record = directory / ("a" * 64 + "_host")
        for value in ("succeeded", "pending", "{invalid"):
            record.write_text(value)
            self.assertEqual(self.call()["error"]["code"], "outcome_unknown")
        self.assertEqual(self.count(), [])


if __name__ == "__main__":
    unittest.main()
