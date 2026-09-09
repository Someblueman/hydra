#!/usr/bin/env python3
"""Network-free SSH boundary: real config expansion, controlled handshakes."""

import json
import os
from pathlib import Path
import subprocess
import sys
import time
import traceback


def effective(config, target):
    result = subprocess.run(
        [os.environ["HD_REAL_SSH"], "-G", "-F", config, target],
        text=True, capture_output=True, check=True,
    )
    return dict(line.split(" ", 1) for line in result.stdout.splitlines())


def main():
    args = sys.argv[1:]
    config = args[args.index("-F") + 1]
    assert Path(config).stat().st_mode & 0o777 == 0o600
    if "-G" in args:
        target = args[-1]
        if target == "badconfig":
            return 1
        os.execv(os.environ["HD_REAL_SSH"], ["ssh", *args])
    assert args[0] == "-T"
    assert "BatchMode=yes" in args and "StrictHostKeyChecking=yes" in args
    target, command = args[-2:]
    assert command == "env LC_ALL=C  'hydra' fleet serve", command
    settings = effective(config, target)
    assert settings["batchmode"] == "yes"
    assert settings["stricthostkeychecking"] in ("true", "yes")
    assert settings["updatehostkeys"] in ("false", "no")
    assert settings.get("controlpath", "none") == "none"
    assert settings["permitlocalcommand"] == "no"
    assert settings["clearallforwardings"] == "yes"
    if target == "good":
        assert settings["proxyjump"] == "jump"
        jump = effective(config, "jump")
        assert jump["batchmode"] == "yes"
        assert jump["stricthostkeychecking"] in ("true", "yes")
        assert jump["updatehostkeys"] in ("false", "no")
    request = json.load(sys.stdin)
    assert request == {"protocol": 1, "action": "handshake"}, request
    with open(os.environ["HD_CALLS"], "a") as stream:
        stream.write(json.dumps({"target": target, "config": config}) + "\n")
    failures = {
        "unknown": "No ED25519 host key is known for fixture and you have requested strict checking.\nHost key verification failed.",
        "changed": "REMOTE HOST IDENTIFICATION HAS CHANGED!\nHost key verification failed.",
        "keygeneric": "Host key verification failed.",
        "auth": "Permission denied (publickey).",
        "offline": "Connection refused",
    }
    if target in failures:
        print(failures[target] + " secret-test-token", file=sys.stderr)
        return 255
    if target == "slow":
        Path(os.environ["HD_PID"]).write_text(str(os.getpid()))
        time.sleep(30)
        return 0
    if target == "malformed":
        print("{broken")
        return 0
    if target == "oversized":
        try:
            sys.stdout.write("x" * (8 * 1024 * 1024 + 1))
            sys.stdout.flush()
        except BrokenPipeError:
            os._exit(0)  # The bounded reader deliberately closes the pipe.
        return 0
    result = subprocess.run(
        [os.environ["HYDRA_FLEET_BIN"], "fleet", "serve"],
        input=json.dumps(request), text=True, capture_output=True, check=True,
    )
    response = json.loads(result.stdout)
    if target == "skew":
        response["data"]["fleet_protocol"] = 99
    if target == "nocap":
        response["data"]["capabilities"] = []
    print(json.dumps(response))
    return 0


if __name__ == "__main__":
    def report_error(kind, value, trace):
        with open(os.environ["HD_ERRORS"], "a") as stream:
            traceback.print_exception(kind, value, trace, file=stream)
        sys.__excepthook__(kind, value, trace)

    sys.excepthook = report_error
    sys.exit(main())
