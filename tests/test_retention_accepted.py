#!/usr/bin/env python3
"""Expire a COPY of a real accepted fixture; original evidence is read-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--fleet", type=Path, default=ROOT / "build/hydra-fleet")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.fleet = args.fleet.resolve()
    source = args.fixture.resolve() / "source"
    parent = ROOT / "build/qualification"
    parent.mkdir(exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix="retention-accepted-", dir=parent)).resolve()
    home = base / "home"
    shutil.copytree(args.fixture / "home", home, symlinks=True)
    runs = list(home.glob("state/v2/projects/*/workflows/runs/run_*"))
    assert len(runs) == 1, runs
    run = runs[0]
    variables = json.loads((run / "artifacts/environment").read_text())["variables"]
    env = {**os.environ, **variables, "HYDRA_HOME": str(home), "HYDRA_FLEET_BIN": str(args.fleet.resolve())}
    for key in ("LD_PRELOAD", "LD_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "ENV", "BASH_ENV"):
        env.pop(key, None)
    commands = []

    def call(argv, ok=True, request=None):
        result = subprocess.run(list(map(str, argv)), cwd=source, env=env,
                                input=json.dumps(request) if request else None,
                                capture_output=True, text=True, timeout=90)
        n = len(commands)
        (base / f"{n}.stdout").write_text(result.stdout)
        (base / f"{n}.stderr").write_text(result.stderr)
        commands.append({"argv": list(map(str, argv)), "exit": result.returncode,
                         "stdout": f"{n}.stdout", "stderr": f"{n}.stderr"})
        (base / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        return json.loads(result.stdout)

    hydra = ROOT / "bin/hydra"
    accepted = call([hydra, "workflow", "plan", "result", run.name])
    tasks = sorted((home / "fleet/tasks").glob("task_*"))
    bindings = {task.name: (task / "acceptance.json").read_bytes() for task in tasks}
    packages = {task.name: json.loads((task / "package.json").read_text()) for task in tasks}
    for task in tasks:
        (task / "workspace/retention-user-change.txt").write_text("preserve this uncommitted workspace file\n")
    original_workspaces = {str(path.relative_to(home)): path.read_bytes()
                           for task in tasks for path in (task / "workspace").rglob("*")
                           if path.is_file() and not path.is_symlink() and ".git" not in path.relative_to(task / "workspace").parts}
    assert original_workspaces, "workspace preservation must not be a vacuous check"
    policy = base / "policy.json"
    policy.write_text(json.dumps({"schema_version": 1, "audit_days": 1,
                                 "max_bytes": 67108864, "max_evidence_records": 1024}))
    stamp = time.time() - 3 * 86400
    for path in home.rglob("*"):
        if not path.is_symlink():
            os.utime(path, (stamp, stamp))
    call([hydra, "fleet", "retention", "pin", "run", run.name])
    protected = call([hydra, "fleet", "retention", "apply", "--policy", policy])
    assert all(item["action"] == "preserve" for item in protected["data"]["records"])
    assert call([hydra, "workflow", "plan", "result", run.name])["data"]["verdict"] == "pass"
    call([hydra, "fleet", "retention", "unpin", "run", run.name])
    expired = call([hydra, "fleet", "retention", "apply", "--policy", policy])
    assert all(item["action"] == "expire" for item in expired["data"]["records"])
    refused = call([hydra, "workflow", "plan", "result", run.name], ok=False)
    assert refused["error"]["code"] == "evidence_expired"
    status = call([hydra, "workflow", "status", run.name, "--json"])
    assert status["data"]["state"] == "succeeded" and status["data"]["evidence_expired"]
    for task in tasks:
        binding = json.loads(bindings[task.name])
        response = call([args.fleet, "fleet", "serve"], request={"protocol": 1, "action": "task",
                        "operation": "submit", "submission_key": binding["submission_key"], "package": packages[task.name]})
        assert response["data"]["task_id"] == task.name
        assert response["data"]["runtime"]["result_state"] == "expired"
        assert (task / "acceptance.json").read_bytes() == bindings[task.name]
        assert not (task / "result.json").exists()
    conflict = json.loads(bindings[tasks[0].name])
    response = call([args.fleet, "fleet", "serve"], ok=False, request={"protocol": 1, "action": "task",
                    "operation": "submit", "submission_key": conflict["submission_key"], "package": packages[tasks[1].name]})
    assert response["error"]["code"] == "submission_conflict"
    assert sorted(path.name for path in (home / "fleet/tasks").glob("task_*")) == sorted(bindings)
    for relative, content in original_workspaces.items():
        assert (home / relative).read_bytes() == content
    call([hydra, "fleet", "retention", "apply", "--policy", policy])
    summary = {"fixture_copy": str(base), "original_fixture": str(args.fixture.resolve()),
               "run": run.name, "accepted_before": accepted["data"]["verdict"],
               "pinned_result": "pass", "expired_result": refused["error"]["code"],
               "receiver_bindings_preserved": len(bindings), "duplicate_submissions_reused": len(tasks),
               "new_receiver_acceptances": 0, "workspaces_preserved": True,
               "commands_sha256": hashlib.sha256((base / "commands.json").read_bytes()).hexdigest()}
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
