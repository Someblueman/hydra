#!/usr/bin/env python3
"""Deterministic provider fixture; never claims live-model coverage.

Interactive mode is a planning conversation: `draft`, `revise` and `fix`
publish a proposal through the public `hydra workflow plan propose` command.
Lines Hydra types into the conversation (``Hydra: ...``) are acknowledged and
recorded in $UX_NOTICES so a checker can confirm what the agent was told.

Installed as `codex` on a journey's private PATH, `codex exec --json ... -`
is a headless worker speaking Codex JSONL: it follows only `FIXTURE-*` lines
in its prompt (write a file, commit, summarize), so the plan carries the work.
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def worker() -> int:
    if "--help" in sys.argv:
        print("Usage: codex exec --json [--color never] -  (resume --json SESSION -)")
        print("Deterministic usability fixture worker; not a live provider.")
        return 0
    files, summary, message = [], "No FIXTURE-SUMMARY line in the prompt.", ""
    for line in sys.stdin.read().splitlines():
        if line.startswith("FIXTURE-FILE "):
            path, _, text = line[len("FIXTURE-FILE ") :].partition("=")
            files.append((path, text.replace("\\n", "\n")))
        elif line.startswith("FIXTURE-COMMIT "):
            message = line[len("FIXTURE-COMMIT ") :]
        elif line.startswith("FIXTURE-SUMMARY "):
            summary = line[len("FIXTURE-SUMMARY ") :]
    events = [{"type": "thread.started", "thread_id": "usability-fixture"}]
    for path, text in files:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_text(text)
    if files and message:
        identity = ["-c", "user.name=Fixture worker", "-c", "user.email=worker@example.invalid"]
        subprocess.run(["git", "add", "--", *[p for p, _ in files]], check=True)
        commit = ["git", *identity, "-c", "commit.gpgSign=false", "commit", "-qm", message]
        subprocess.run(commit, check=True)
        events.append(
            {
                "type": "item.completed",
                "item": {
                    "id": "commit",
                    "type": "command_execution",
                    "command": f"git commit -m '{message}'",
                    "exit_code": 0,
                    "status": "completed",
                },
            }
        )
    events.append(
        {"type": "item.completed", "item": {"id": "final", "type": "agent_message", "text": summary}}
    )
    usage = {"input_tokens": 4200, "cached_input_tokens": 1000, "output_tokens": 120}
    events.append({"type": "turn.completed", "usage": usage})
    for event in events:
        print(json.dumps(event), flush=True)
    return 0


def publish(template: str, suffix: str) -> None:
    plan = json.loads(Path(os.environ[template]).read_text())
    plan["objective"] += suffix
    assets = []
    for pair in filter(None, os.environ.get("UX_PLAN_ASSETS", "").split(",")):
        assets += ["--asset", pair]
    with tempfile.TemporaryDirectory(prefix="provider-proposal-") as directory:
        draft = Path(directory) / "draft.json"
        draft.write_text(json.dumps(plan))
        result = subprocess.run(
            ["hydra", "workflow", "plan", "propose", str(draft), *assets],
            capture_output=True,
            text=True,
            check=False,
            timeout=15,
        )
        print(result.stdout or result.stderr, flush=True)


def converse() -> int:
    print("DETERMINISTIC PROVIDER FIXTURE - ready to discuss", flush=True)
    for line in sys.stdin:
        request = line.strip()
        if request.startswith("Hydra:"):
            if os.environ.get("UX_NOTICES"):
                with open(os.environ["UX_NOTICES"], "a") as notices:
                    notices.write(request + "\n")
            print("FIXTURE RECEIVED HYDRA NOTICE", flush=True)
            continue
        if request == "draft":
            publish("UX_PLAN_TEMPLATE", "")
            print("PROPOSAL READY", flush=True)
        elif request == "revise":
            publish("UX_PLAN_TEMPLATE", " (revised after discussion)")
            print("PROPOSAL REVISION READY", flush=True)
        elif request == "fix" and os.environ.get("UX_PLAN_FIXED"):
            publish("UX_PLAN_FIXED", "")
            print("PROPOSAL FIX READY", flush=True)
        else:
            print(
                "Discuss the plan; type draft, revise or fix. Hydra owns execution approval.",
                flush=True,
            )
    return 0


if __name__ == "__main__":
    if "--version" in sys.argv[1:2]:
        print("codex-cli 0.0.0-usability-fixture")
        raise SystemExit(0)
    raise SystemExit(worker() if sys.argv[1:2] == ["exec"] else converse())
