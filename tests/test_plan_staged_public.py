#!/usr/bin/env python3
"""Retained two-stage public CLI acceptance in a disposable local repository."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HYDRA = ROOT / "bin/hydra"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fleet", type=Path, default=ROOT / "build/hydra-fleet")
    parser.add_argument("--precompiler", type=Path, default=Path(os.environ.get("HYDRA_PLAN_PRECOMPILE_BIN", ROOT / "build/plan-precompile")))
    parser.add_argument(
        "--case",
        choices=("normal", "empty", "all-skipped", "collision", "maximum"),
        default="normal",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    parent = ROOT / "build/qualification"
    parent.mkdir(exist_ok=True)
    base = Path(
        tempfile.mkdtemp(prefix="staged-" + args.case + "-", dir=parent)
    ).resolve()
    source = base / "source"
    shutil.copytree(
        ROOT / "examples/planning/staged",
        source,
        ignore=shutil.ignore_patterns("__pycache__"),
    )
    manifest = source / "manifest.json"
    value = json.loads(manifest.read_text())
    if args.case == "empty":
        value["items"] = []
    elif args.case == "all-skipped":
        for item in value["items"]:
            item["enabled"] = False
    elif args.case == "collision":
        value["items"] = [
            {"id": name, "value": 2, "enabled": True}
            for name in ("finding", "manifest", "compose", "check")
        ]
    elif args.case == "maximum":
        value["items"] = [
            {"id": f"item-{i}", "value": i, "enabled": True}
            for i in range(8)
        ]
    manifest.write_text(json.dumps(value) + "\n")
    env = {
        **os.environ,
        "HYDRA_HOME": str(base / "home"),
        "HYDRA_BIN": str(HYDRA),
        "HYDRA_FLEET_BIN": str(args.fleet.resolve()),
        "HYDRA_NONINTERACTIVE": "1",
        "HYDRA_SKIP_AI": "1",
        "HYDRA_NO_SWITCH": "1",
    }
    commands = []

    def call(argv, expected=0, label=None):
        result = subprocess.run(
            list(map(str, argv)),
            cwd=source,
            env=env,
            text=True,
            capture_output=True,
            timeout=360,
        )
        stem = label or f"command-{len(commands)}"
        (base / (stem + ".stdout")).write_text(result.stdout)
        (base / (stem + ".stderr")).write_text(result.stderr)
        commands.append(
            {"argv": list(map(str, argv)), "exit": result.returncode, "log": stem}
        )
        (base / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        assert result.returncode == expected, result.stdout + result.stderr
        return result.stdout

    def commit(message):
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
                message,
            ]
        )

    def run_plan(number, plan, policy):
        compiled = base / f"compiled{number}.json"
        compiled_response = json.loads(
            call(
                [HYDRA, "workflow", "plan", "compile", plan, policy, compiled],
                label=f"compile{number}",
            )
        )
        digest = compiled_response["data"]["sha256"]
        launched = call(
            [HYDRA, "workflow", "plan", "run", compiled, "--accept", digest],
            label=f"launch{number}",
        )
        run = re.search(r"^run_[a-z0-9]+$", launched, re.MULTILINE).group(0)
        raw = call([HYDRA, "workflow", "plan", "result", run], label=f"result{number}")
        result = json.loads(raw)
        assert result["ok"] and result["data"]["verdict"] == "pass", result
        return {
            "run_id": run,
            "plan_sha256": digest,
            "result": str(base / f"result{number}.stdout"),
        }, result["data"]

    call(["git", "init", "-q"])
    commit("staged fixture source")
    call([HYDRA, "init", "--no-agent", "--trust"])
    stage1, result = run_plan(1, "stage1-plan.json", "stage1-policy.json")
    finding = Path(result["deliverables"]["report"]["path"])
    accepted_bytes = finding.read_bytes()
    assert (
        hashlib.sha256(accepted_bytes).hexdigest()
        == result["deliverables"]["report"]["sha256"]
    )
    (source / "finding.json").write_bytes(accepted_bytes)
    precompile = [
        str(args.precompiler.resolve()),
        "staged",
        "manifest.json",
        stage1["run_id"],
        base / "plan2.json",
    ]
    call(precompile, label="precompile2")
    # Same sealed stage-1 result cannot authorize different local finding bytes.
    (source / "finding.json").write_bytes(accepted_bytes + b" ")
    call(precompile, expected=1, label="reject-local-finding-change")
    (source / "finding.json").write_bytes(accepted_bytes)
    original_manifest = manifest.read_bytes()
    manifest.write_bytes(original_manifest + b" ")
    call(precompile, expected=1, label="reject-stale-source")
    manifest.write_bytes(original_manifest)
    call(precompile, label="precompile2-final")
    commit("freeze accepted stage-one finding")
    stage2, result2 = run_plan(2, base / "plan2.json", "policy.json")
    report = json.loads(Path(result2["deliverables"]["report"]["path"]).read_text())
    selected = [item for item in value["items"] if item["enabled"]]
    assert report == {
        "schema_version": 1,
        "selected_ids": [item["id"] for item in selected],
        "members": [
            {"id": item["id"], "value": item["value"], "square": item["value"] ** 2}
            for item in selected
        ],
    }
    summary = {
        "schema_version": 1,
        "case": args.case,
        "fixture": str(base),
        "stage1": stage1,
        "stage2": stage2,
        "negative_controls": ["changed-local-finding", "stale-source"],
        "verdict": "pass",
        "source_files": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in source.glob("*.py")
        },
    }
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
