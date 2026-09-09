#!/usr/bin/env python3
"""Loopback OpenSSH identity and reviewed-master regression tests."""
import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


class ReviewedMasterTest(unittest.TestCase):
    def setUp(self):
        if not shutil.which("sshd") or not shutil.which("ssh"):
            self.skipTest("OpenSSH client/server unavailable")
        self.tmp = Path(tempfile.mkdtemp())
        self.key = self.tmp / "key"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(self.key)], check=True)
        self.hostkey = self.tmp / "host"
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(self.hostkey)], check=True)
        self.authorized = self.tmp / "authorized_keys"
        self.authorized.write_text(self.key.with_suffix(".pub").read_text())
        self.port = 39000 + (os.getpid() % 1000)
        self.socket = self.tmp / "ctl"
        self.log = self.tmp / "peer.log"
        self.config = self.tmp / "sshd_config"
        self.config.write_text(f"Port {self.port}\nListenAddress 127.0.0.1\nHostKey {self.hostkey}\nPidFile {self.tmp/'pid'}\nAuthorizedKeysFile {self.authorized}\nStrictModes no\nUsePAM no\nPasswordAuthentication no\nPermitRootLogin yes\nLogLevel QUIET\n")
        self.daemon = subprocess.Popen(["sshd", "-D", "-e", "-f", str(self.config)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(40):
            if subprocess.run(["ssh", "-p", str(self.port), "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "-i", str(self.key), "root@127.0.0.1", "true"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
                return
            time.sleep(0.05)
        self.tearDown()
        self.skipTest("loopback sshd did not start in this environment")

    def tearDown(self):
        if getattr(self, "socket", None):
            subprocess.run(["ssh", "-S", str(self.socket), "-O", "exit", "root@127.0.0.1"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if getattr(self, "daemon", None):
            self.daemon.terminate(); self.daemon.wait(timeout=2)
        shutil.rmtree(getattr(self, "tmp", "/nonexistent"), ignore_errors=True)

    def base(self):
        return ["ssh", "-p", str(self.port), "-i", str(self.key), "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "root@127.0.0.1"]

    def test_first_diagnostic_reuse_and_lost_master_fail_closed(self):
        first = subprocess.run(self.base() + ["-vv", "-E", str(self.log), "-M", "-S", str(self.socket), "-o", "ControlPersist=15", "-fN"], capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("Server host key:", self.log.read_text())
        reused = subprocess.run(self.base() + ["-S", str(self.socket), "-o", "ControlMaster=no", "-o", "ProxyCommand=false", "true"], capture_output=True)
        self.assertEqual(reused.returncode, 0, reused.stderr)
        subprocess.run(["ssh", "-S", str(self.socket), "-O", "exit", "root@127.0.0.1"], check=False)
        lost = subprocess.run(self.base() + ["-S", str(self.socket), "-o", "ControlMaster=no", "-o", "ProxyCommand=false", "true"], capture_output=True)
        self.assertNotEqual(lost.returncode, 0)


if __name__ == "__main__":
    unittest.main()
