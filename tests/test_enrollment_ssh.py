#!/usr/bin/env python3
"""Exercise public enrollment through an ephemeral strict loopback SSH server."""
import getpass
import json
import os
from pathlib import Path
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest


class EnrollmentSSHTest(unittest.TestCase):
    def setUp(self):
        self.sshd = shutil.which("sshd") or "/usr/sbin/sshd"
        if not os.access(self.sshd, os.X_OK) or not shutil.which("ssh-keygen"):
            self.skipTest("OpenSSH client/server is unavailable")
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-enrollment-ssh-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = Path(__file__).resolve().parents[1]
        self.fleet = os.environ.get("HYDRA_FLEET_BIN", str(self.source / "build/hydra-fleet"))
        self.cli = str(self.source / "bin/hydra")
        self.counter = self.root / "mutations"
        self.project = self.root / "project"
        subprocess.run(["git", "init", "-q", str(self.project)], check=True)
        self.client_key, self.host_key = self.root / "client", self.root / "host"
        for key in (self.client_key, self.host_key):
            subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(key)], check=True)
        authorized = self.root / "authorized_keys"
        authorized.write_text(self.client_key.with_suffix(".pub").read_text())
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            self.port = listener.getsockname()[1]
        self.known = self.root / "known_hosts"
        self.known.write_text(f"[127.0.0.1]:{self.port} " + self.host_key.with_suffix(".pub").read_text())
        self.client_config = self.root / "ssh_config"
        self.client_config.write_text(
            f"Host fixture\n HostName 127.0.0.1\n Port {self.port}\n User {getpass.getuser()}\n"
            f" IdentityFile {self.client_key}\n IdentitiesOnly yes\n UserKnownHostsFile {self.known}\n"
            " GlobalKnownHostsFile /dev/null\n StrictHostKeyChecking yes\n")
        count = self.root / "hydra-count"
        count.write_text(f"#!{sys.executable}\nimport os, sys\n"
                         "if 'init' in sys.argv:\n"
                         f"    with open({str(self.counter)!r}, 'a') as output: output.write('init\\n')\n"
                         "os.execv(os.environ['ENROLL_REAL'], [os.environ['ENROLL_REAL'], *sys.argv[1:]])\n")
        count.chmod(0o755)
        force = self.root / "receiver"
        force.write_text(f"#!{sys.executable}\nimport os, shlex, subprocess, sys\n"
                         "args = shlex.split(os.environ['SSH_ORIGINAL_COMMAND'])\n"
                         f"os.environ['HYDRA_HOME'] = {str(self.root / 'remote-home')!r}\n"
                         f"os.environ['HYDRA_BIN_CMD'] = {str(count)!r}\n"
                         f"os.environ['PATH'] = {os.environ['PATH']!r}\n"
                         "if args[-2:] == ['fleet', 'serve']:\n"
                         "    hydra = args[-3]\n"
                         f"    binary = {self.fleet!r} if hydra == 'hydra' else os.path.join(os.path.dirname(hydra), '../libexec/hydra/hydra-fleet')\n"
                         f"    os.environ['ENROLL_REAL'] = {self.cli!r} if hydra == 'hydra' else hydra\n"
                         "    os.execv(binary, [binary, 'fleet', 'serve'])\n"
                         "result = subprocess.run(['/bin/sh', '-c', os.environ['SSH_ORIGINAL_COMMAND']], input=sys.stdin.buffer.read(), capture_output=True)\n"
                         f"open({str(self.root / 'install.err')!r}, 'wb').write(result.stderr + result.stdout)\n"
                         "sys.stdout.buffer.write(result.stdout); sys.stderr.buffer.write(result.stderr); sys.exit(result.returncode)\n")
        force.chmod(0o755)
        server_config = self.root / "sshd_config"
        server_config.write_text(
            f"Port {self.port}\nListenAddress 127.0.0.1\nHostKey {self.host_key}\nPidFile {self.root / 'pid'}\n"
            f"AuthorizedKeysFile {authorized}\nStrictModes no\nUsePAM no\nPasswordAuthentication no\n"
            f"KbdInteractiveAuthentication no\nAllowUsers {getpass.getuser()}\nForceCommand {force}\nLogLevel ERROR\n")
        self.server_log = (self.root / "sshd.log").open("w")
        self.addCleanup(self.server_log.close)
        self.daemon = subprocess.Popen([self.sshd, "-D", "-e", "-f", str(server_config)], stdout=subprocess.DEVNULL, stderr=self.server_log)
        self.addCleanup(self.stop_server)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.daemon.poll() is not None:
                self.fail((self.root / "sshd.log").read_text())
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=.1):
                    break
            except OSError:
                time.sleep(.02)
        else:
            self.fail("ephemeral SSH server did not become ready")
        transport = self.root / "transport"
        transport.mkdir()
        injector = transport / "ssh"
        injector.write_text(f"#!{sys.executable}\nimport json, os, subprocess, sys\n"
                            "args = sys.argv[1:]\n"
                            "if '-G' in args or '-O' in args: os.execv('/usr/bin/ssh', ['/usr/bin/ssh', *args])\n"
                            "data = sys.stdin.buffer.read()\n"
                            "request = json.loads(data) if data.startswith(b'{') else {}\n"
                            "if os.environ.get('ENROLL_BREAK_MASTER') and request.get('action') == 'init':\n"
                            "    control = next(value.split('=', 1)[1] for value in args if value.startswith('ControlPath='))\n"
                            "    config = args[args.index('-F') + 1]\n"
                            "    subprocess.run(['/usr/bin/ssh', '-F', config, '-S', control, '-O', 'exit', args[-2]], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)\n"
                            "result = subprocess.run(['/usr/bin/ssh', *args], input=data)\n"
                            "sys.exit(result.returncode)\n")
        injector.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.root), HYDRA_HOME=str(self.root / "client-home"), HYDRA_FLEET_BIN=self.fleet, PATH=f"{transport}:{os.environ['PATH']}")

    def stop_server(self):
        if self.daemon.poll() is None:
            self.daemon.terminate()
            self.daemon.wait(timeout=5)

    def fleet_call(self, *args):
        result = subprocess.run([self.cli, "fleet", *args], env=self.env, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return json.loads(result.stdout)

    def test_public_review_pinned_install_init_and_duplicate(self):
        qualification = self.fleet_call("qualify", "--ssh", "fixture", "--ssh-config", str(self.client_config), "--require", "list")
        row = qualification["data"]["candidates"][0]
        self.assertEqual(row["status"], "compatible", row)
        expected_key = subprocess.check_output(["ssh-keygen", "-lf", str(self.host_key.with_suffix('.pub'))], text=True).split()[1]
        self.assertEqual(row["qualification"]["data"]["peer_fingerprint"], expected_key)
        qualified = self.root / "qualification.json"
        qualified.write_text(json.dumps(qualification))
        package = self.root / "package"
        packaged = self.fleet_call("package", "--source", str(self.source), "--binary", self.fleet, "--output", str(package))
        prefix = self.root / "exact-prefix"
        intent = self.root / "intent.json"
        reviewed = self.fleet_call("enroll", "review", "--input", str(qualified), "--candidate", row["candidate_id"],
                                   "--project", str(self.project), "--package", str(package), "--sha256", packaged["data"]["sha256"],
                                   "--prefix", str(prefix), "--output", str(intent))
        digest = reviewed["data"]["intent_sha256"]
        applied = self.fleet_call("enroll", "apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(applied["data"]["hosts"][0]["status"], "enrolled", (applied, (self.root / "install.err").read_text() if (self.root / "install.err").exists() else "no install stderr"))
        self.assertEqual((prefix / "libexec/hydra/hydra-fleet").read_bytes(), Path(self.fleet).read_bytes())
        alias = json.loads((self.root / "client-home/fleet/remotes" / (row["candidate_id"] + ".json")).read_text())
        self.assertEqual(alias["accepted_host_key"], expected_key)
        self.assertEqual(alias["project"], str(self.project))
        self.assertEqual(alias["hydra"], str(prefix / "bin/hydra"))
        self.assertEqual(alias["principal"], getpass.getuser())
        duplicate = self.fleet_call("enroll", "apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(duplicate["data"]["hosts"][0]["status"], "enrolled")
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])

    def test_lost_authenticated_master_cannot_open_fallback_connection(self):
        qualification = self.fleet_call("qualify", "--ssh", "fixture", "--ssh-config", str(self.client_config), "--require", "list")
        row = qualification["data"]["candidates"][0]
        self.assertEqual(row["status"], "compatible", row)
        qualified = self.root / "qualification.json"
        qualified.write_text(json.dumps(qualification))
        intent = self.root / "intent.json"
        review = self.fleet_call("enroll", "review", "--input", str(qualified), "--candidate", row["candidate_id"],
                                "--project", str(self.project), "--output", str(intent))
        digest = review["data"]["intent_sha256"]
        self.env["ENROLL_BREAK_MASTER"] = "1"
        first = self.fleet_call("enroll", "apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(first["data"]["hosts"][0]["status"], "outcome_unknown", first)
        self.assertFalse(self.counter.exists())
        self.assertFalse((self.root / "client-home/fleet/remotes" / (row["candidate_id"] + ".json")).exists())
        self.env.pop("ENROLL_BREAK_MASTER")
        second = self.fleet_call("enroll", "apply", "--input", str(intent), "--confirm", digest)
        self.assertEqual(second["data"]["hosts"][0]["status"], "enrolled", second)
        self.assertEqual(self.counter.read_text().splitlines(), ["init"])


if __name__ == "__main__":
    unittest.main()
