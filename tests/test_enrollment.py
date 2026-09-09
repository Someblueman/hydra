#!/usr/bin/env python3
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


class EnrollmentTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.home = self.tmp / "home"
        self.home.mkdir()
        subprocess.run(["git", "init", "-q", str(self.tmp / "project")], check=True)
        self.counter = self.tmp / "mutations"
        self.ssh = self.tmp / "ssh"
        self.ssh.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import json, os, subprocess, sys
            request = json.load(sys.stdin)
            target = os.environ.get("ENROLL_TARGET", "good")
            print("debug1: Server host key: ssh-ed25519 " + os.environ["ENROLL_FINGERPRINT"], file=sys.stderr)
            if request.get("action") == "init":
                with open(os.environ["ENROLL_COUNTER"], "a") as f: f.write("init\\n")
            result = subprocess.run([os.environ["HYDRA_FLEET_BIN"], "fleet", "serve"], input=json.dumps(request), text=True, capture_output=True)
            if os.environ.get("ENROLL_STDOUT_MARKER") and request.get("action") == "handshake":
                print("Server host key: ssh-ed25519 SHA256:fixture")
            sys.stdout.write(result.stdout)
            sys.exit(0)
        """))
        self.ssh.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(HOME=str(self.tmp), HYDRA_HOME=str(self.home), PATH=f"{self.tmp}:{os.environ['PATH']}",
                         HYDRA_FLEET_BIN=str(Path(__file__).parents[1] / "build/hydra-fleet"), ENROLL_COUNTER=str(self.counter), ENROLL_FINGERPRINT="SHA256:fixture")
        self.cli = str(Path(__file__).parents[1] / "bin/hydra")

    def run_cli(self, *args, check=True):
        return subprocess.run([self.cli, "fleet", "enroll", *args], env=self.env, text=True, capture_output=True, check=check)

    def qualification(self, fingerprint="SHA256:fixture"):
        value = {"schema_version": 1, "ok": True, "data": {"required_capability": "list", "candidates": [{
            "candidate_id": "cand_676f6f64", "target": "good", "sources": [], "status": "compatible",
            "resolution": {"ok": True, "data": {"user": "tester"}},
            "qualification": {"ok": True, "data": {"peer_fingerprint": fingerprint}},
        }]}}
        path = self.tmp / "qualification.json"
        path.write_text(json.dumps(value))
        return path

    def test_apply_is_explicit_and_duplicate_is_reconciled(self):
        qualification = self.qualification()
        intent = self.tmp / "intent.json"
        project = self.tmp / "project"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(project), "--output", str(intent))
        reviewed = json.loads(intent.read_text())
        digest = reviewed["intent_sha256"]
        first = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(first.stdout)["data"]["hosts"][0]["status"], "enrolled")
        second = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(second.stdout)["data"]["hosts"][0]["status"], "enrolled")
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_peer_mismatch_has_zero_mutations(self):
        qualification = self.qualification("SHA256:reviewed")
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        result = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(result.stdout)["data"]["hosts"][0]["status"], "host_key_changed")
        self.assertFalse(self.counter.exists())

    def test_stdout_marker_is_not_peer_identity(self):
        self.env["ENROLL_STDOUT_MARKER"] = "1"
        self.env["ENROLL_FINGERPRINT"] = ""
        qualification = self.qualification()
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        result = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(result.stdout)["data"]["hosts"][0]["status"], "review_required")
        self.assertFalse(self.counter.exists())

    def test_reviewed_ssh_config_change_requires_renewal(self):
        config = self.tmp / "ssh-config"
        config.write_text("Host good\n  HostName good\n  User tester\n")
        qualification = self.qualification()
        value = json.loads(qualification.read_text())
        value["data"]["candidates"][0]["sources"] = [{"kind": "ssh-config", "locator": str(config)}]
        qualification.write_text(json.dumps(value))
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp / "project"), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        config.write_text("Host good\n  HostName changed\n  User other\n")
        result = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(result.stdout)["data"]["hosts"][0]["status"], "invalid_intent")
        self.assertFalse(self.counter.exists())


if __name__ == "__main__":
    unittest.main()
