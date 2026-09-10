#!/usr/bin/env python3
"""Qualify frozen candidate/oracle outputs through real admitted Hydra plans."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples/planning/evaluation"
HYDRA = ROOT / "bin/hydra"


def run_candidate(candidate, fleet):
    parent = ROOT / "build/qualification"
    parent.mkdir(exist_ok=True)
    base = Path(
        tempfile.mkdtemp(prefix="planner-" + candidate + "-", dir=parent)
    ).resolve()
    repo = base / "source"
    repo.mkdir()
    for name in (
        "plan.json",
        "policy.json",
        "requests.json",
        "evaluate.py",
        "produce.py",
        "check.py",
        "planner-evaluation-heldout-v2.json",
    ):
        shutil.copy2(SOURCE / name, repo / name)
    shutil.copy2(
        SOURCE / "candidates" / candidate / "candidate.py", repo / "candidate.py"
    )
    env = {
        **os.environ,
        "HYDRA_HOME": str(base / "home"),
        "HYDRA_FLEET_BIN": str(fleet.resolve()),
        "HYDRA_NONINTERACTIVE": "1",
        "HYDRA_SKIP_AI": "1",
        "HYDRA_NO_SWITCH": "1",
    }
    commands = []

    def call(argv, expected=0, label=None):
        started = time.monotonic()
        result = subprocess.run(
            list(map(str, argv)),
            cwd=repo,
            env=env,
            capture_output=True,
            text=True,
            timeout=180,
        )
        elapsed = time.monotonic() - started
        stem = label or f"command-{len(commands)}"
        (base / (stem + ".stdout")).write_text(result.stdout)
        (base / (stem + ".stderr")).write_text(result.stderr)
        commands.append(
            {
                "argv": list(map(str, argv)),
                "exit": result.returncode,
                "seconds": elapsed,
                "log": stem,
            }
        )
        (base / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        assert result.returncode == expected, result.stdout + result.stderr
        return result.stdout, elapsed

    call(["git", "init", "-q"])
    call(["git", "add", "."])
    call(
        [
            "git",
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-qm",
            "frozen candidate and independent checker",
        ]
    )
    source_commit = call(["git", "rev-parse", "HEAD"])[0].strip()
    call([HYDRA, "init", "--no-agent", "--trust"])
    compiled = base / "compiled.json"
    raw, compile_seconds = call(
        [HYDRA, "workflow", "plan", "compile", "plan.json", "policy.json", compiled],
        label="compile",
    )
    digest = json.loads(raw)["data"]["sha256"]
    call([HYDRA, "workflow", "plan", "explain", compiled], label="explain")
    expected_pass = candidate.startswith("contract-aware-")
    raw, execution_seconds = call(
        [HYDRA, "workflow", "plan", "run", compiled, "--accept", digest],
        expected=0 if expected_pass else 1,
        label="launch",
    )
    run_id = re.search(r"^run_[a-z0-9]+$", raw, re.MULTILINE).group(0)
    raw, result_seconds = call(
        [HYDRA, "workflow", "plan", "result", run_id],
        expected=0 if expected_pass else 1,
        label="result",
    )
    result = json.loads(raw)
    run = next((base / "home/state/v2/projects").glob(f"*/workflows/runs/{run_id}"))
    raw_report = run / "steps/check/attempt-1/outputs/check.json"
    report = json.loads(raw_report.read_text())
    assert report["evidence_status"] == "valid" and report["verdict"] == (
        "pass" if expected_pass else "fail"
    )
    counts = report["evidence_records"][0]["counts"]
    assert counts == {
        "executed": 9,
        "failed": 0 if expected_pass else 9,
        "skipped": 0,
    }, counts
    assert bool(result["ok"]) == expected_pass
    if expected_pass:
        assert result["data"]["verdict"] == "pass"
    return {
        "candidate": candidate,
        "fixture": str(base),
        "source_commit": source_commit,
        "run_id": run_id,
        "plan_sha256": digest,
        "candidate_sha256": hashlib.sha256(
            (repo / "candidate.py").read_bytes()
        ).hexdigest(),
        "checker_sha256": hashlib.sha256((repo / "check.py").read_bytes()).hexdigest(),
        "compile_seconds": compile_seconds,
        "execution_seconds": execution_seconds,
        "result_seconds": result_seconds,
        "verdict": report["verdict"],
        "counts": counts,
        "public_result": str(base / "result.stdout"),
        "explanation": str(base / "explain.stdout"),
        "report": str(raw_report),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fleet", type=Path, default=ROOT / "build/hydra-fleet")
    parser.add_argument(
        "--candidate",
        choices=sorted(p.name for p in (SOURCE / "candidates").iterdir() if p.is_dir()),
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    selected = (
        [args.candidate]
        if args.candidate
        else sorted(p.name for p in (SOURCE / "candidates").iterdir() if p.is_dir())
    )
    results = []
    for candidate in selected:
        results.append(run_candidate(candidate, args.fleet))
        args.output.write_text(
            json.dumps({"schema_version": 1, "results": results}, indent=2) + "\n"
        )
        print(candidate, results[-1]["run_id"], results[-1]["verdict"], flush=True)


if __name__ == "__main__":
    main()
