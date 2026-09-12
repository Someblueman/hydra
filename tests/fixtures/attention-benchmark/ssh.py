#!/usr/bin/env python3
"""Local-only receiver transport with optional controlled overview and accounting.

Installed as `ssh` in the private fixture PATH. Never invokes a network client.
Each invocation has its own telemetry file, so simultaneous clients cannot mix
records. Receiver CPU is wait4 child accounting, not upstream producer CPU.
"""

from __future__ import annotations

import hashlib
import json
import os
import resource
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    fixture = Path(os.environ["HYDRA_REVIEW_FIXTURE"])
    started = time.monotonic_ns()
    request = sys.stdin.buffer.read()
    parsed = json.loads(request)
    mode = "public-receiver"
    before = resource.getrusage(resource.RUSAGE_SELF)
    child_cpu = None
    if (fixture / "offline").exists():
        code, output, mode = 255, b"", "offline"
    elif (fixture / "controlled.json").exists() and parsed.get("action") == "overview":
        code, output, mode = (
            0,
            (fixture / "controlled.json").read_bytes(),
            "controlled-overview",
        )
    else:
        # The command originates in this benchmark's public remote registration.
        # Match the repository's established local-only fleet-review transport.
        before_child = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run(
            ["/bin/sh", "-c", sys.argv[-1]],
            input=request,
            capture_output=True,
            check=False,
        )
        after_child = resource.getrusage(resource.RUSAGE_CHILDREN)
        child_cpu = {
            "user_seconds": after_child.ru_utime - before_child.ru_utime,
            "system_seconds": after_child.ru_stime - before_child.ru_stime,
            "scope": "receiver child and descendants it reaped",
        }
        code, output = result.returncode, result.stdout
        sys.stderr.buffer.write(result.stderr)
    sys.stdout.buffer.write(output)
    sys.stdout.buffer.flush()
    after = resource.getrusage(resource.RUSAGE_SELF)
    telemetry = Path(os.environ["HYDRA_BENCH_TELEMETRY"])
    telemetry.mkdir(exist_ok=True)
    row = {
        "pid": os.getpid(),
        "ppid": os.getppid(),
        "started_ns": started,
        "finished_ns": time.monotonic_ns(),
        "action": parsed.get("action"),
        "operation": parsed.get("operation"),
        "mode": mode,
        "returncode": code,
        "request_sha256": hashlib.sha256(request).hexdigest(),
        "response_sha256": hashlib.sha256(output).hexdigest(),
        "receiver_cpu": child_cpu,
        "transport_observer_cpu": {
            "user_seconds": after.ru_utime - before.ru_utime,
            "system_seconds": after.ru_stime - before.ru_stime,
        },
    }
    (telemetry / f"{started}-{os.getpid()}.json").write_text(json.dumps(row) + "\n")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
