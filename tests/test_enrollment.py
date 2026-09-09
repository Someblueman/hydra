#!/usr/bin/env python3
import json
import fcntl
import os
import shutil
import signal
import time
import hashlib
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
        self.hydra_wrapper = self.tmp / "hydra-count"
        self.hydra_wrapper.write_text("#!/bin/sh\nfor a in \"$@\"; do if [ \"$a\" = init ]; then printf 'init\\n' >> \"$ENROLL_COUNTER\"; break; fi; done\nexec \"$ENROLL_REAL\" \"$@\"\n")
        self.hydra_wrapper.chmod(0o755)
        self.ssh = self.tmp / "ssh"
        self.ssh.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import json, os, subprocess, sys, time
            if "-G" in sys.argv:
                result = subprocess.run(["/usr/bin/ssh", *sys.argv[1:]], text=True, capture_output=True)
                sys.stdout.write(result.stdout)
                sys.exit(result.returncode)
            if "-O" in sys.argv:
                sys.exit(0)
            target = sys.argv[-2]
            policy = json.loads(os.environ.get("ENROLL_HOST_POLICY", "{}" )).get(target, {})
            if policy.get("offline"):
                sys.exit(255)
            diagnostic = "debug1: Server host key: ssh-ed25519 " + policy.get("fingerprint", os.environ["ENROLL_FINGERPRINT"]) + "\\n"
            if "-E" in sys.argv:
                with open(sys.argv[sys.argv.index("-E") + 1], "a") as log:
                    log.write(diagnostic)
            else:
                sys.stderr.write(diagnostic)
            receiver_home = os.path.join(os.environ["ENROLL_RECEIVERS"], target)
            receiver_env = dict(os.environ, HYDRA_HOME=receiver_home, HYDRA_BIN_CMD=os.environ["ENROLL_COUNT_WRAPPER"])
            if " install '" in sys.argv[-1] or " install-check '" in sys.argv[-1]:
                installing = " install '" in sys.argv[-1]
                result = subprocess.run(["/bin/sh", "-c", sys.argv[-1]], input=sys.stdin.buffer.read(), capture_output=True, env=receiver_env)
                if installing:
                    with open(os.environ["ENROLL_INSTALL_COUNTER"], "a") as log:
                        log.write("install\\n")
                drop = os.environ.get("ENROLL_DROP_INSTALL_RESPONSE")
                if installing and drop and not os.path.exists(drop):
                    open(drop, "w").close()
                    sys.exit(255)
                sys.stdout.buffer.write(result.stdout)
                sys.stderr.buffer.write(result.stderr)
                sys.exit(result.returncode)
            if sys.argv[-1].startswith("git -C "):
                result = subprocess.run(["/bin/sh", "-c", sys.argv[-1]], text=True, capture_output=True, env=receiver_env)
                sys.stdout.write(result.stdout)
                sys.exit(result.returncode)
            request = json.load(sys.stdin)
            hold = os.environ.get("ENROLL_HOLD_PREFLIGHT")
            if hold and request.get("action") == "enrollment-preflight":
                open(hold + ".started", "w").close()
                while not os.path.exists(hold):
                    time.sleep(.01)
            if policy.get("bad_project") and request.get("action") == "enrollment-preflight":
                request["project"] = "/nonexistent/hydra-enrollment-fixture"

            result = subprocess.run([os.environ["HYDRA_FLEET_BIN"], "fleet", "serve"], input=json.dumps(request), text=True, capture_output=True, env=receiver_env)
            if request.get("action") == "init" and os.environ.get("ENROLL_DROP_INIT_RESPONSE") and not os.path.exists(os.environ["ENROLL_DROP_INIT_RESPONSE"]):
                open(os.environ["ENROLL_DROP_INIT_RESPONSE"], "w").close()
                sys.exit(255)
            if os.environ.get("ENROLL_STDOUT_MARKER") and request.get("action") == "handshake":
                print("Server host key: ssh-ed25519 SHA256:fixture")
            output = json.loads(result.stdout)
            if policy.get("missing_capability") and request.get("action") == "handshake":
                output["data"]["capabilities"] = [c for c in output["data"]["capabilities"] if c != "list"]
            old_prefix = os.environ.get("ENROLL_OLD_RECEIVER_PREFIX")
            if old_prefix and request.get("action") == "handshake" and not os.path.exists(old_prefix):
                output["data"]["capabilities"] = [c for c in output["data"]["capabilities"] if not c.startswith("enrollment-")]
            print(json.dumps(output))
            sys.exit(result.returncode)
        """))
        self.ssh.chmod(0o755)
        self.env = os.environ.copy()
        self.env.update(HOME=str(self.tmp), HYDRA_HOME=str(self.home), PATH=f"{self.tmp}:{os.environ['PATH']}",
                         HYDRA_FLEET_BIN=os.environ.get("HYDRA_FLEET_BIN", str(Path(__file__).parents[1] / "build/hydra-fleet")), HYDRA_BIN_CMD=str(self.hydra_wrapper), ENROLL_COUNT_WRAPPER=str(self.hydra_wrapper), ENROLL_REAL=str(Path(__file__).parents[1] / "bin/hydra"), ENROLL_COUNTER=str(self.counter), ENROLL_FINGERPRINT="SHA256:fixture", ENROLL_RECEIVERS=str(self.tmp / "receivers"), ENROLL_INSTALL_COUNTER=str(self.tmp / "installs"))
        self.cli = str(Path(__file__).parents[1] / "bin/hydra")

    def run_cli(self, *args, check=True):
        return subprocess.run([self.cli, "fleet", "enroll", *args], env=self.env, text=True, capture_output=True, check=check)

    def run_fleet(self, *args):
        return subprocess.run([self.cli, "fleet", *args], env=self.env, text=True, capture_output=True, check=True)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def qualification(self, fingerprint="SHA256:fixture", config=None, targets=("good",)):
        args = ["qualify", "--require", "list"]
        for target in targets:
            args += ["--ssh", target]
        if config:
            args += ["--ssh-config", str(config)]
        value = json.loads(self.run_fleet(*args).stdout)
        for row in value["data"]["candidates"]:
            self.assertEqual(row["status"], "compatible", row)
            row["qualification"]["data"]["peer_fingerprint"] = fingerprint
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
        qualification = self.qualification()
        self.env["ENROLL_STDOUT_MARKER"] = "1"
        self.env["ENROLL_FINGERPRINT"] = ""
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        result = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(result.stdout)["data"]["hosts"][0]["status"], "outcome_unknown")
        self.assertFalse(self.counter.exists())

    def test_lost_response_reconciles_without_second_init(self):
        drop = self.tmp / "dropped"
        self.env["ENROLL_DROP_INIT_RESPONSE"] = str(drop)
        qualification = self.qualification()
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp / "project"), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        first = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(first.stdout)["data"]["hosts"][0]["status"], "outcome_unknown")
        second = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(second.stdout)["data"]["hosts"][0]["status"], "enrolled")
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_concurrent_apply_is_rejected(self):
        qualification = self.qualification()
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp / "project"), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        lock_dir = self.home / "fleet" / "enrollment"
        lock_dir.mkdir(parents=True)
        lock = lock_dir / f"{digest}.json.lock"
        with lock.open("w") as held:
            fcntl.flock(held, fcntl.LOCK_EX)
            result = self.run_cli("apply", "--input", str(intent), "--confirm", digest, check=False)
        self.assertEqual(json.loads(result.stdout)["error"]["code"], "apply_in_progress")
        self.assertFalse(self.counter.exists())

    def test_reviewed_ssh_config_change_requires_renewal(self):
        config = self.tmp / "ssh-config"
        config.write_text("Host good\n  HostName good\n  User tester\n")
        qualification = self.qualification(config=config)
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp / "project"), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        config.write_text("Host good\n  HostName changed\n  User other\n")
        result = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(result.stdout)["data"]["hosts"][0]["status"], "review_required")
        self.assertFalse(self.counter.exists())

    def test_real_qualify_output_can_be_reviewed_and_applied(self):
        config = self.tmp / "ssh-config"
        config.write_text("Host good\n  HostName good\n  User tester\n")
        qualified = self.run_fleet("qualify", "--ssh", "good", "--ssh-config", str(config), "--require", "list")
        qualification = self.tmp / "qualified.json"
        qualification.write_text(qualified.stdout)
        intent = self.tmp / "intent.json"
        self.run_cli("review", "--input", str(qualification), "--candidate", "cand_676f6f64", "--project", str(self.tmp / "project"), "--output", str(intent))
        digest = json.loads(intent.read_text())["intent_sha256"]
        applied = self.run_cli("apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(json.loads(applied.stdout)["data"]["hosts"][0]["status"], "enrolled")
        alias = json.loads((self.home / "fleet" / "remotes" / "cand_676f6f64.json").read_text())
        self.assertEqual(alias["target"], "good")
        self.assertEqual(alias.get("ssh_config"), str(config))

    def review_many(self, qualification, candidates, package=None):
        intent = self.tmp / "intent.json"
        args = ["review", "--input", str(qualification), "--project", str(self.tmp / "project"), "--output", str(intent)]
        for candidate in candidates:
            args += ["--candidate", candidate]
        if package:
            args += ["--package", str(package[0]), "--sha256", package[1], "--prefix", str(package[2])]
        self.run_cli(*args)
        return intent, json.loads(intent.read_text())["intent_sha256"]

    def apply(self, intent, digest):
        return json.loads(self.run_cli("apply", "--input", str(intent), "--confirm", digest).stdout)["data"]

    def package(self):
        package = self.tmp / "package"
        result = json.loads(self.run_fleet("package", "--source", str(Path(__file__).resolve().parents[1]),
                                           "--binary", self.env["HYDRA_FLEET_BIN"], "--output", str(package)).stdout)
        return package, result["data"]["sha256"], self.tmp / "exact-prefix"

    def test_ten_host_mixed_apply_preserves_six_successes(self):
        targets = tuple(f"host{i}" for i in range(10))
        qualified = self.qualification(targets=targets)
        candidates = [row["candidate_id"] for row in json.loads(qualified.read_text())["data"]["candidates"]]
        intent, digest = self.review_many(qualified, candidates)
        self.env["ENROLL_HOST_POLICY"] = json.dumps({"host1": {"fingerprint": "SHA256:changed"},
            "host3": {"missing_capability": True}, "host5": {"offline": True}, "host7": {"bad_project": True}})
        first = self.apply(intent, digest)
        by_alias = {row["alias"]: row for row in first["hosts"]}
        expected = {"host1": "host_key_changed", "host3": "capability_changed", "host5": "outcome_unknown", "host7": "project_unavailable"}
        for target in targets:
            alias = "cand_" + target.encode().hex()
            self.assertEqual(by_alias[alias]["status"], expected.get(target, "enrolled"))
        self.assertTrue(first["partial_failure"])
        self.assertEqual(self.counter.read_text().splitlines(), ["init"] * 6)
        second = self.apply(intent, digest)
        self.assertEqual(second["hosts"], first["hosts"])
        self.assertEqual(self.counter.read_text().splitlines(), ["init"] * 6)

    def test_changed_intent_fields_require_fresh_review(self):
        qualified = self.qualification()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"])
        original = json.loads(intent.read_text())
        for field, replacement in (("target", "other"), ("project", str(self.tmp)), ("fingerprint", "SHA256:other"),
                                   ("principal", "other"), ("required_capability", "spawn")):
            value = json.loads(json.dumps(original))
            value["hosts"][0][field] = replacement
            intent.write_text(json.dumps(value))
            result = self.run_cli("apply", "--input", str(intent), "--confirm", digest, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stdout)["error"]["code"], "invalid_intent")
        self.assertFalse(self.counter.exists())

    def test_changed_package_has_zero_mutations(self):
        qualified = self.qualification()
        package = self.package()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"], package)
        package[0].write_bytes(package[0].read_bytes() + b" ")
        self.assertEqual(self.apply(intent, digest)["hosts"][0]["status"], "review_required")
        self.assertFalse(self.counter.exists())
        self.assertFalse(package[2].exists())

    def test_lost_install_response_checks_bytes_without_second_install(self):
        qualified = self.qualification()
        package = self.package()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"], package)
        self.env["ENROLL_DROP_INSTALL_RESPONSE"] = str(self.tmp / "dropped-install")
        first = self.apply(intent, digest)
        self.assertEqual(first["hosts"][0]["status"], "outcome_unknown", first)
        self.assertEqual(first["hosts"][0]["phase"], "install")
        self.assertFalse(self.counter.exists())
        binary = package[2] / "libexec/hydra/hydra-fleet"
        before = binary.stat()
        second = self.apply(intent, digest)
        self.assertEqual(second["hosts"][0]["status"], "enrolled", second)
        self.assertEqual(binary.read_bytes(), Path(self.env["HYDRA_FLEET_BIN"]).read_bytes())
        self.assertEqual((binary.stat().st_ino, binary.stat().st_mtime_ns), (before.st_ino, before.st_mtime_ns))
        self.assertEqual((self.tmp / "installs").read_text().splitlines(), ["install"])
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_lost_install_with_changed_bytes_remains_unknown(self):
        qualified = self.qualification()
        package = self.package()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"], package)
        self.env["ENROLL_DROP_INSTALL_RESPONSE"] = str(self.tmp / "dropped-install")
        self.assertEqual(self.apply(intent, digest)["hosts"][0]["status"], "outcome_unknown")
        (package[2] / "bin/hydra").write_text("changed")
        self.assertEqual(self.apply(intent, digest)["hosts"][0]["status"], "outcome_unknown")
        self.assertEqual((self.tmp / "installs").read_text().splitlines(), ["install"])
        self.assertFalse(self.counter.exists())

    def test_pinned_upgrade_accepts_older_compatible_receiver(self):
        package = self.package()
        self.env["ENROLL_OLD_RECEIVER_PREFIX"] = str(package[2])
        qualified = self.qualification()
        caps = json.loads(qualified.read_text())["data"]["candidates"][0]["qualification"]["data"]["capabilities"]
        self.assertNotIn("enrollment-init", caps)
        intent, digest = self.review_many(qualified, ["cand_676f6f64"], package)
        result = self.apply(intent, digest)
        self.assertEqual(result["hosts"][0]["status"], "enrolled", result)
        self.assertEqual((self.tmp / "installs").read_text().splitlines(), ["install"])
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_existing_alias_conflict_has_zero_mutations(self):
        qualified = self.qualification()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"])
        alias = self.home / "fleet/remotes/cand_676f6f64.json"
        alias.parent.mkdir(parents=True)
        existing = {"schema_version": 1, "target": "other", "hydra": "hydra", "home": "", "multiplex": False}
        alias.write_text(json.dumps(existing))
        self.assertEqual(self.apply(intent, digest)["hosts"][0]["status"], "alias_conflict")
        self.assertEqual(json.loads(alias.read_text()), existing)
        self.assertFalse(self.counter.exists())

    def test_interrupt_before_init_can_resume_once(self):
        qualified = self.qualification()
        intent, digest = self.review_many(qualified, ["cand_676f6f64"])
        hold = self.tmp / "preflight-release"
        self.env["ENROLL_HOLD_PREFLIGHT"] = str(hold)
        process = subprocess.Popen([self.cli, "fleet", "enroll", "apply", "--input", str(intent), "--confirm", digest],
                                   env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 10
        try:
            while time.monotonic() < deadline and process.poll() is None and not Path(str(hold) + ".started").exists():
                time.sleep(.01)
            self.assertTrue(Path(str(hold) + ".started").exists(), "apply never reached preflight")
            process.send_signal(signal.SIGTERM)
            output, error = process.communicate(timeout=10)
            self.assertTrue(output, error)
            self.assertFalse(self.counter.exists())
        finally:
            hold.touch()
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)
        self.env.pop("ENROLL_HOLD_PREFLIGHT")
        self.assertEqual(self.apply(intent, digest)["hosts"][0]["status"], "enrolled")
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_install_check_is_read_only_and_exact_prefix(self):
        package, digest, prefix = self.package()
        raw = package.read_bytes()
        def install(command, target):
            result = subprocess.run([self.env["HYDRA_FLEET_BIN"], command, digest, "--prefix", str(target)],
                                    env=self.env, input=raw, capture_output=True)
            return json.loads(result.stdout)
        self.assertTrue(install("install", prefix)["ok"])
        def snapshot():
            return {str(path.relative_to(prefix)): (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
                    for path in prefix.rglob("*") if path.is_file()}
        before = snapshot()
        self.assertTrue(install("install-check", prefix)["ok"])
        self.assertEqual(snapshot(), before)
        wrong = self.tmp / "exact-prefix-evil"
        self.assertFalse(install("install-check", wrong)["ok"])
        self.assertFalse(wrong.exists())
        (prefix / "bin/hydra").write_text("changed")
        changed = snapshot()
        self.assertFalse(install("install", prefix)["ok"])
        self.assertEqual(snapshot(), changed)


if __name__ == "__main__":
    unittest.main()
