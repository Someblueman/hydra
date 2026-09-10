#!/usr/bin/env python3
"""Public CLI acceptance without tmux, host enrollment, or network access."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("HYDRA_FLEET_BIN", ROOT / "build/hydra-fleet"))


class DiscoveryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.config = self.path / "ssh.config"
        self.included = self.path / "included.config"
        self.included.write_text(
            "Host good other\n HostName fixture.invalid\n User builder\n Port 2222\n"
            " ProxyJump jump\n IdentityFile /fixture/key\n"
            "Host jump\n HostName jump.invalid\n User jumper\n Port 2200\n"
            " StrictHostKeyChecking no\n BatchMode no\n UpdateHostKeys yes\n"
        )
        self.config.write_text(
            f'Include "{self.included}"\nHost *\n'
            " ControlMaster auto\n ControlPath /unsafe/socket\n"
            " LocalCommand touch /must-not-run\n PermitLocalCommand yes\n"
            " ForwardAgent yes\n StrictHostKeyChecking no\n UpdateHostKeys yes\n"
        )
        (self.path / "bin").mkdir()
        fixture = self.path / "bin/ssh"
        shutil.copyfile(Path(__file__).with_name("ssh_fixture.py"), fixture)
        fixture.chmod(0o755)
        self.home = self.path / "home"
        self.home.mkdir()
        (self.home / "sentinel").write_text("preserve remote state and installation")
        (self.home / "fleet/remotes").mkdir(parents=True)
        (self.home / "fleet/remotes/existing.json").write_text(json.dumps({
            "schema_version": 1, "target": "existing", "hydra": "hydra", "home": "", "multiplex": False}))
        self.before = self.state_bytes()
        self.env = {
            **os.environ,
            "PATH": str(self.path / "bin") + os.pathsep + os.environ["PATH"],
            "HYDRA_HOME": str(self.home), "HYDRA_FLEET_BIN": str(BINARY),
            "HD_REAL_SSH": shutil.which("ssh"), "HD_CALLS": str(self.path / "calls"),
            "HD_PID": str(self.path / "pid"),
            "HD_ERRORS": str(self.path / "errors"),
        }

    def state_bytes(self):
        return {str(p.relative_to(self.home)): p.read_bytes()
                for p in self.home.rglob("*") if p.is_file()}

    def command(self, action, *args):
        return [str(ROOT / "bin/hydra"), "fleet", action,
                "--ssh-config", str(self.config), "--json", *args]

    def run_cli(self, action, *args, success=True):
        result = subprocess.run(self.command(action, *args), env=self.env,
                                text=True, capture_output=True, timeout=20)
        if (self.path / "errors").exists():
            self.fail((self.path / "errors").read_text())
        self.assertEqual(result.returncode, 0 if success else 1, result.stderr + result.stdout)
        self.assertNotIn("secret-test-token", result.stdout)
        return json.loads(result.stdout)

    def test_effective_alias_jump_and_conservative_dedupe(self):
        before = self.config.read_bytes(), self.included.read_bytes()
        result = self.run_cli("discover", "--ssh", "good", "--ssh", "other", "--ssh", "good")
        rows = result["data"]["candidates"]
        self.assertEqual([r["target"] for r in rows], ["good", "other"])
        effective = rows[0]["resolution"]["data"]
        self.assertEqual(effective["hostname"], ["fixture.invalid"])
        self.assertEqual(effective["user"], ["builder"])
        self.assertEqual(effective["port"], ["2222"])
        self.assertEqual(effective["proxyjump"], ["jump"])
        self.assertIn("/fixture/key", effective["identityfile"])
        self.assertNotEqual(rows[0]["candidate_id"], rows[1]["candidate_id"])
        self.assertTrue(all(r["verified_identity"] is None for r in rows))
        self.assertFalse((self.path / "calls").exists())
        second = self.run_cli("discover", "--ssh", "other", "--ssh", "good")
        self.assertEqual([r["candidate_id"] for r in rows],
                         [r["candidate_id"] for r in second["data"]["candidates"]])
        qualified = self.run_cli("qualify", "--ssh", "good")
        self.assertEqual(qualified["data"]["candidates"][0]["status"], "compatible")
        self.assertEqual(before, (self.config.read_bytes(), self.included.read_bytes()))
        self.assertEqual(self.state_bytes(), self.before)
        calls = [json.loads(line) for line in (self.path / "calls").read_text().splitlines()]
        self.assertEqual([c["target"] for c in calls], ["good"])
        self.assertTrue(all(not Path(c["config"]).exists() for c in calls))

    def test_mixed_ten_retains_every_row(self):
        hosts = ["good", "unknown", "changed", "auth", "offline", "skew",
                 "nocap", "malformed", "slow", "keygeneric"]
        args = [arg for host in reversed(hosts) for arg in ("--ssh", host)]
        result = self.run_cli("qualify", *args, "--timeout", "1", success=False)
        rows = result["data"]["candidates"]
        self.assertEqual([r["target"] for r in rows], sorted(hosts))
        codes = {r["target"]: r["qualification"].get("error", {}).get("code") for r in rows}
        self.assertEqual(codes, dict(zip(hosts, [None, "host_key_unknown", "host_key_changed",
            "authentication_failed", "offline", "version_mismatch", "capability_unavailable",
            "invalid_response", "timeout", "host_key_failed"])))
        self.assertEqual(len({r["candidate_id"] for r in rows}), 10)
        self.assertTrue(all(r["sources"] and r["verified_identity"] is None for r in rows))
        skew = next(r for r in rows if r["target"] == "skew")
        self.assertEqual(skew["qualification"]["data"]["fleet_protocol"], 99)
        self.assertEqual(self.state_bytes(), self.before)

    def test_static_selection_provenance_and_closed_schema(self):
        inventory = self.path / "inventory.json"
        document = {"schema_version": 1, "observed_at": 1, "hosts": [
            {"name": "chosen", "target": "good", "labels": ["build"]},
            {"name": "excluded", "target": "offline", "labels": []}]}
        inventory.write_text(json.dumps(document))
        args = ["--inventory", str(inventory), "--select", "chosen", "--ssh", "good"]
        result = self.run_cli("discover", *args)
        rows = result["data"]["candidates"]
        self.assertEqual(len(rows), 1)
        self.assertEqual(len(rows[0]["sources"]), 2)
        source = rows[0]["sources"][1]
        self.assertEqual(source["labels"], ["build"])
        self.assertEqual(source["observed_at"], 1)
        self.assertGreater(source["age_seconds"], 1000)
        for change in [dict(document, schema_version=2), dict(document, command="touch unsafe"),
                       dict(document, observed_at="yesterday")]:
            inventory.write_text(json.dumps(change))
            self.assertEqual(self.run_cli("discover", *args, success=False)["error"]["code"], "invalid_inventory")
        document["hosts"][0]["target"] = "good;touch unsafe"
        inventory.write_text(json.dumps(document))
        self.run_cli("discover", *args, success=False)
        self.assertFalse((self.path / "calls").exists())

    def test_limits_and_resolution_failure(self):
        self.run_cli("discover", success=False)
        self.run_cli("discover", "--ssh", "good", "--timeout", "0", success=False)
        args = [arg for i in range(17) for arg in ("--ssh", f"host{i}")]
        self.run_cli("discover", *args, success=False)
        result = self.run_cli("qualify", "--ssh", "badconfig", "--ssh", "good", success=False)
        self.assertEqual(len(result["data"]["candidates"]), 2)
        result = self.run_cli("qualify", "--ssh", "oversized", success=False)
        self.assertEqual(result["data"]["candidates"][0]["qualification"]["error"]["code"], "output_limit")

    def snapshot(self, kind, records, observed=1):
        path = self.path / f"{kind}.json"
        path.write_text(json.dumps({"schema_version": 2, "observed_at": observed,
            "hosts": [], "source_snapshot": {"kind": kind, "locator": f"fixture:{kind}",
            "scope": "fixture", "observed_at": observed, "freshness": "source_reported",
            "records": records}}))
        return path

    def test_supported_snapshot_projections_bind_source(self):
        cases = {
            "mdns": ({"instance": "printer", "host": "good", "port": 22, "labels": ["lan"]}, "printer", "good"),
            "vpn": ({"peer": "peer-a", "address": "good", "labels": ["vpn"]}, "peer-a", "good"),
            "cloud-tags": ({"instance_id": "i-1", "private_ip": "good", "labels": ["prod"]}, "i-1", "good"),
            "config-management": ({"host": "node-a", "address": "good", "labels": ["web"]}, "node-a", "good"),
        }
        for kind, (record, name, target) in cases.items():
            result = self.run_cli("discover", "--inventory", str(self.snapshot(kind, [record])), "--select", name, "--ssh", target)
            row = result["data"]["candidates"][0]
            self.assertEqual(row["target"], target)
            source = next(s for s in row["sources"] if s["kind"] == kind)
            self.assertEqual(source["locator"], f"fixture:{kind}")
            self.assertEqual(source["observed_at"], 1)

    def test_snapshot_rejects_malformed_secret_time_and_schema_smuggling(self):
        base = {"instance": "x", "host": "good", "port": 22, "labels": []}
        for record in [dict(base, token="secret"), dict(base, port="22")]:
            self.assertEqual(self.run_cli("discover", "--inventory", str(self.snapshot("mdns", [record])), "--select", "x", success=False)["error"]["code"], "invalid_inventory")
        for at in [-1, 9999999999]:
            self.assertEqual(self.run_cli("discover", "--inventory", str(self.snapshot("mdns", [base], at)), "--select", "x", success=False)["error"]["code"], "invalid_inventory")
        old = self.path / "old.json"; old.write_text(json.dumps({"schema_version": 1, "observed_at": 1, "hosts": [], "source_snapshot": {}}))
        self.assertEqual(self.run_cli("discover", "--inventory", str(old), "--select", "x", success=False)["error"]["code"], "invalid_inventory")

    def test_snapshot_import_bound_is_one_hundred(self):
        records = [{"host": f"h{i}", "address": "good", "labels": []} for i in range(100)]
        path = self.snapshot("config-management", records)
        names = [arg for i in range(100) for arg in ("--select", f"h{i}")]
        result = self.run_cli("discover", "--inventory", str(path), *names, "--ssh", "good")
        self.assertEqual(len(result["data"]["candidates"]), 1)
        path.write_text(json.dumps({"schema_version": 2, "observed_at": 1, "hosts": [], "source_snapshot": {"kind": "config-management", "locator": "fixture", "scope": "fixture", "observed_at": 1, "freshness": "source_reported", "records": records + [{"host": "h100", "address": "good", "labels": []}]}}))
        self.run_cli("discover", "--inventory", str(path), *names, "--select", "h100", success=False)

    def test_qualification_progress_advances_sixteen_hosts(self):
        inventory = self.path / "batch.json"
        records = [{"name": f"h{i}", "target": f"host{i}", "labels": []} for i in range(20)]
        inventory.write_text(json.dumps({"schema_version": 1, "observed_at": 1, "hosts": records}))
        args = [arg for i in range(20) for arg in ("--select", f"h{i}")]
        progress = self.path / "progress.json"
        first = self.run_cli("qualify", "--inventory", str(inventory), *args, "--progress", str(progress), success=True)
        self.assertEqual(sum("resolution" in row for row in first["data"]["candidates"]), 16)
        second = self.run_cli("qualify", "--inventory", str(inventory), *args, "--progress", str(progress), success=True)
        self.assertEqual(sum("resolution" in row for row in second["data"]["candidates"]), 20)

    def test_cancel_retains_unvisited_rows_and_reaps_ssh(self):
        process = subprocess.Popen(self.command("qualify", "--ssh", "slow", "--ssh", "zzz",
                                                "--timeout", "30"), env=self.env,
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: process.poll() is None and process.kill())
        deadline = time.monotonic() + 10
        while not (self.path / "pid").exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue((self.path / "pid").exists())
        process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=5)
        self.assertEqual(process.returncode, 1, stderr)
        rows = json.loads(stdout)["data"]["candidates"]
        self.assertEqual([r["target"] for r in rows], ["slow", "zzz"])
        self.assertTrue(all(r["qualification"]["error"]["code"] == "cancelled" for r in rows))
        with self.assertRaises(ProcessLookupError):
            os.kill(int((self.path / "pid").read_text()), 0)


if __name__ == "__main__":
    unittest.main()
