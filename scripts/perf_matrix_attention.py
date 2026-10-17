#!/usr/bin/env python3
"""I2 attention qualification over annotated local workflow states.

One real public workflow run (exec step succeeded, approval-wait pending) is
mutated per annotated case; `hydra workflow attention --json` and the
`attention-data` wire are scored for false positives and missed signals
separately, with unknown items matched by count and unsupported-by-design
silence reported on its own. Measurements never approve or accept anything.
"""

from __future__ import annotations

import argparse
import json
import os
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
SUPPORTED = {"approval", "approval_expired", "result", "failure"}


class Harness:
    def __init__(self, root: Path, build: Path, out: Path) -> None:
        self.root, self.build, self.out = root, build, out
        self.out.mkdir(parents=True)
        self.repo, self.home = out / "repo", out / "home"
        self.tmux = Path(tempfile.mkdtemp(prefix="perf-att-", dir="/tmp"))
        self.env = {
            "PATH": os.environ["PATH"], "HOME": str(self.home), "LANG": "C", "LC_ALL": "C",
            "HYDRA_HOME": str(self.home), "HYDRA_STATE_V2_ROOT": str(self.home / "state/v2"),
            "HYDRA_FLEET_BIN": str(build / "hydra-fleet"), "HYDRA_NONINTERACTIVE": "1", "HYDRA_SKIP_AI": "1",
            "HYDRA_NO_SWITCH": "1", "HYDRA_SETUP_CONTINUE": "1", "TMUX_TMPDIR": str(self.tmux),
            "GIT_CONFIG_GLOBAL": "/dev/null", "GIT_CONFIG_NOSYSTEM": "1",
        }
        self.hydra = root / "bin/hydra"
        self.calls: list[dict[str, Any]] = []
        self.run_dir: Path | None = None
        self.request = ""
        self.aside: list[tuple[Path, Path]] = []

    def call(self, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
        started = time.monotonic_ns()
        result = subprocess.run(args, cwd=self.repo, env=self.env, capture_output=True, text=True,
                                timeout=120, check=False)
        self.calls.append({"argv": list(args), "exit": result.returncode,
                           "wall_ns": time.monotonic_ns() - started})
        if check and result.returncode:
            raise RuntimeError(f"{args}: {result.returncode}: {result.stderr[-400:]}")
        return result

    def hydra_call(self, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
        return self.call(str(self.hydra), *args, check=check)

    def setup(self) -> None:
        self.repo.mkdir()
        self.home.mkdir()
        self.call("git", "init", "-q")
        self.call("git", "config", "user.email", "perf@example.invalid")
        self.call("git", "config", "user.name", "perf attention fixture")
        (self.repo / "tracked").write_text("base\n")
        self.call("git", "add", "tracked")
        self.call("git", "commit", "-qm", "base")
        self.hydra_call("init", "--no-agent", "--trust")
        self.hydra_call("spawn", "attention-worker", "--no-agent")
        (self.out / "data.json").write_text(json.dumps({
            "schema_version": 1, "inputs": {},
            "steps": {"produce": {"outputs": {"answer": {"path": "answer.txt", "type": "file", "max_bytes": 128}}}}}))
        produce = self.out / "produce.sh"
        produce.write_text('#!/bin/sh\nset -eu\nprintf \'sealed attention result\\n\' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.txt"\n')
        produce.chmod(0o755)
        (self.out / "flow.yml").write_text(
            "version: 1\nid: attention-perf\ndata: data.json\nresources:\n  disk_mb: 1\nsteps:\n"
            "  - id: produce\n    kind: exec\n    idempotent: false\n    args:\n      head: attention-worker\n"
            f"      argv: [sh, {produce}]\n"
            "  - id: approval\n    kind: approval-wait\n    needs: [produce]\n    idempotent: false\n"
            "    args:\n      head: attention-worker\n      name: review\n      message: Review sealed output\n")
        result = self.hydra_call("workflow", "run", str(self.out / "flow.yml"), check=False)
        if result.returncode != 3:
            raise RuntimeError(f"workflow run did not stop at approval: {result.returncode} {result.stderr[-300:]}")
        run = result.stdout.splitlines()[0]
        matches = list((self.home / "state/v2/projects").glob(f"*/workflows/runs/{run}"))
        if len(matches) != 1:
            raise RuntimeError("run directory is not unique")
        self.run_dir = matches[0]
        self.request = (self.run_dir / "steps/approval/request-id").read_text().splitlines()[0]

    def attention(self) -> tuple[dict[str, Any], int]:
        result = self.hydra_call("workflow", "attention", "--json", check=False)
        return json.loads(result.stdout or result.stderr), self.calls[-1]["wall_ns"]

    def apply(self, mutation: dict[str, Any]) -> None:
        assert self.run_dir is not None
        op = mutation["op"]
        if op == "write":
            path = self.run_dir / mutation["path"].replace("{request}", self.request)
            backup = path.with_name(path.name + ".perf-backup")
            if path.exists():
                shutil.copy2(path, backup)
                self.aside.append((backup, path))
            path.write_text(mutation["text"])
        elif op == "move-aside":
            path = self.run_dir / mutation["path"].replace("{request}", self.request)
            (self.out / "aside").mkdir(exist_ok=True)
            aside = self.out / "aside" / f"{len(self.aside)}-{path.name}.perf-aside"
            path.rename(aside)
            self.aside.append((aside, path))
        elif op == "empty-runs":
            runs = self.run_dir.parent
            aside = runs.with_name("runs.perf-aside")
            runs.rename(aside)
            runs.mkdir(mode=0o700)
            self.aside.append((aside, runs))
        elif op == "remove-runs":
            runs = self.run_dir.parent
            aside = runs.with_name("runs.perf-aside")
            runs.rename(aside)
            self.aside.append((aside, runs))
        else:
            raise ValueError(f"unknown mutation {op}")

    def restore(self) -> None:
        for source, target in reversed(self.aside):
            if target.is_dir() and not target.is_symlink() and source.name.endswith("perf-aside"):
                shutil.rmtree(target)
            if source.name.endswith("perf-backup"):
                shutil.copy2(source, target)
                source.unlink()
            else:
                source.rename(target)
        self.aside.clear()

    def cleanup(self) -> dict[str, Any]:
        receipt: dict[str, Any] = {"errors": []}
        try:
            self.hydra_call("kill", "attention-worker", "--force", check=False)
            receipt["kill_exit"] = self.calls[-1]["exit"]
        except (OSError, subprocess.SubprocessError) as error:
            receipt["errors"].append(repr(error))
        try:
            subprocess.run(["tmux", "-S", str(self.tmux / f"tmux-{os.getuid()}/default"), "kill-server"],
                           capture_output=True, check=False, timeout=20)
        except (OSError, subprocess.SubprocessError):
            pass
        shutil.rmtree(self.tmux, ignore_errors=True)
        receipt["ok"] = not receipt["errors"]
        return receipt


def score_case(case: dict[str, Any], items: list[dict[str, Any]]) -> dict[str, Any]:
    expected = [(row["kind"], row.get("step")) for row in case["expected"]]
    observed = [(row["kind"], row.get("step_id")) for row in items]
    want_supported = [row for row in expected if row[0] in SUPPORTED]
    got_supported = [row for row in observed if row[0] in SUPPORTED]
    remaining = list(got_supported)
    tp = 0
    for row in want_supported:
        if row in remaining:
            remaining.remove(row)
            tp += 1
    fn = len(want_supported) - tp
    fp = len(remaining)
    unknown_expected = sum(1 for row in expected if row[0] == "unknown")
    unknown_observed = sum(1 for row in observed if row[0] == "unknown")
    other = [row for row in observed if row[0] not in SUPPORTED and row[0] != "unknown"]
    return {"tp": tp, "fp": fp + len(other), "fn": fn, "unknown_expected": unknown_expected,
            "unknown_observed": unknown_observed, "unknown_match": unknown_expected == unknown_observed,
            "unexpected_kinds": other, "observed": observed, "expected": expected,
            "reasons": sorted({row.get("reason", "") for row in items})}


def seen_check(harness: Harness, items: list[dict[str, Any]], mode: str, wire: Any) -> dict[str, Any]:
    rows = wire(harness.hydra_call("workflow", "attention-data").stdout)
    listed = harness.hydra_call("workflow", "attention-seen", "list").stdout
    if mode == "unseen":
        return {"mode": mode, "seen_rows_before": listed.count("\nSEEN\t"), "wire_items": len(rows)}
    if not rows:
        return {"mode": mode, "error": "no wire row to mark"}
    row = rows[0]
    harness.hydra_call("workflow", "attention-seen", "mark", row["identity"], row["revision"])
    after = harness.hydra_call("workflow", "attention-seen", "list").stdout
    still, _ = harness.attention()
    return {"mode": mode, "marked_identity": row["identity"], "seen_listed": row["identity"] in after,
            "signal_still_listed": len(still.get("data", {}).get("items", [])) == len(items),
            "meaning": "seen is a per-user presentation marker; the public signal remains until the revision changes"}


def run(root: Path, build: Path, out: Path, cases_file: Path) -> dict[str, Any]:
    bench = runpy.run_path(str(root / "scripts/bench-attention.py"))
    parse_rows = bench["parse_rows"]
    spec = json.loads(cases_file.read_text())
    harness = Harness(root, build, out)
    report: dict[str, Any] = {"schema": "hydra-attention-qualification/1", "cases_file": str(cases_file),
                              "cases": [], "error": None}
    try:
        harness.setup()
        for case in spec["cases"]:
            entry: dict[str, Any] = {"id": case["id"], "class": case["class"], "note": case.get("note")}
            try:
                for mutation in case["mutations"]:
                    harness.apply(mutation)
                envelope, wall_ns = harness.attention()
                entry["wall_ns"] = wall_ns
                if case.get("expect_error"):
                    entry["error_code"] = envelope.get("error", {}).get("code")
                    entry["score"] = score_case(case, [])
                    entry["explicit_error"] = entry["error_code"] == case["expect_error"]
                else:
                    items = envelope["data"]["items"]
                    entry["score"] = score_case(case, items)
                    entry["partial"] = envelope["data"].get("partial")
                    wire_rows = parse_rows(harness.hydra_call("workflow", "attention-data").stdout)
                    entry["wire_parity"] = len(wire_rows) == len(items)
                    if case.get("seen_check"):
                        entry["seen"] = seen_check(harness, items, case["seen_check"], parse_rows)
                entry["signal_absent_by_design"] = case.get("signal_absent_by_design", [])
                entry["question_classified"] = case.get("question_classified")
            finally:
                harness.restore()
            report["cases"].append(entry)
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report["error"] = repr(error)
    finally:
        report["cleanup"] = harness.cleanup()
        report["calls"] = harness.calls
    scored = [c for c in report["cases"] if "score" in c]
    report.update({
        "cases_total": len(spec["cases"]), "cases_scored": len(scored),
        "false_positives_total": sum(c["score"]["fp"] for c in scored),
        "missed_signals_total": sum(c["score"]["fn"] for c in scored),
        "true_positives_total": sum(c["score"]["tp"] for c in scored),
        "unknown_expected_total": sum(c["score"]["unknown_expected"] for c in scored),
        "unknown_matched_total": sum(c["score"]["unknown_observed"] for c in scored if c["score"]["unknown_match"]),
        "unsupported_silent_total": sum(len(c["signal_absent_by_design"]) for c in scored),
        "wire_parity_all": all(c.get("wire_parity", True) for c in scored),
        "attention_wall_ms": {"n": len(scored), "max": max((c["wall_ns"] for c in scored), default=0) / 1e6},
        "complete": report["error"] is None and len(scored) == len(spec["cases"]) and report["cleanup"]["ok"]
        and all(c["score"]["unknown_match"] for c in scored) and all(c.get("explicit_error", True) for c in scored),
        "scope": "local workflow records route only; fleet receiver, provider and TUI presentation timing are outside this corpus",
    })
    (out / "report.json").write_text(json.dumps(report, indent=1) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cases", type=Path, default=ROOT / "tests/fixtures/perf/attention-cases.json")
    args = parser.parse_args()
    report = run(args.root.resolve(), args.build.resolve(), args.out.resolve(), args.cases.resolve())
    print(json.dumps({key: report[key] for key in ("complete", "error", "false_positives_total",
                                                   "missed_signals_total", "unsupported_silent_total")}))
    print(args.out / "report.json")
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    sys.exit(main())
