#!/usr/bin/env python3
"""Installed-build acceptance for the open roadmap items (see docs/ACCEPTANCE.md).

Installs a fresh prefix, runs every registered case on real Git, tmux and
pseudo-terminals, and writes an acceptance matrix (HTML and JSON) that maps
each acceptance bullet to its checks and retained evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib
import os
import subprocess
import sys
import time
import traceback
from pathlib import Path

PACKAGE_ROOT = Path(__file__).resolve().parent.parent
if str(PACKAGE_ROOT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_ROOT))

from usability import journey as usability  # noqa: E402
from acceptance import harness, matrix, report  # noqa: E402

MODULES = ("entry", "terminal", "flicker", "details", "navigation", "overview", "removal", "journeys")


def selected_cases(args: argparse.Namespace) -> list[dict]:
    chosen = []
    for entry in harness.CASES:
        if args.case and not any(c in entry["name"] for c in args.case):
            continue
        if args.item and not any(matrix.item_of(b) in args.item for b in entry["bullets"]):
            continue
        chosen.append(entry)
    return chosen


def run_case(source: Path, prefix: Path, output: Path, entry: dict, columns: int) -> dict:
    record = {"id": f"{entry['name']}-{columns}", "status": "passed", "results": [], "seconds": 0.0}
    started = time.monotonic()
    j = None
    try:
        j = harness.Case(source, prefix, output, entry["name"], columns)
        j.initialize()
        entry["function"](j)
    except (AssertionError, OSError, RuntimeError, TimeoutError, subprocess.SubprocessError, KeyError,
            ValueError) as error:
        reason = f"{type(error).__name__}: {error}"
        record.update(status="failed" if j else "blocked", reason=reason)
        (output / f"{record['id']}.traceback.txt").write_text(traceback.format_exc())
    finally:
        if j:
            try:
                j.close()
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                record.update(status="failed", reason=f"{record.get('reason', '')} Cleanup: {error}".strip())
            record["results"] = j.results
    if j and record["status"] == "passed" and any(not r["passed"] for r in j.results):
        record["status"] = "failed"
    covered = {r["bullet"] for r in record["results"]}
    for bullet in entry["bullets"]:
        if bullet not in covered:
            record["results"].append({"bullet": bullet, "check": f"{entry['name']}: case did not reach this check",
                                      "passed": False, "detail": record.get("reason", "no check recorded"),
                                      "evidence": [], "blocked": record["status"] == "blocked"})
    record["seconds"] = time.monotonic() - started
    return record


def install(source: Path, output: Path) -> Path:
    prefix = output / "installed"
    env = {**os.environ, "PREFIX": str(prefix), "HYDRA_INSTALL_CORE": "required",
           "HYDRA_INSTALL_TUI": "required", "HYDRA_INSTALL_FLEET": "required", "DESTDIR": ""}
    (output / "install.log").write_text(usability.run(["sh", "install.sh"], source, env))
    return prefix


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", help="run cases whose name contains this text")
    parser.add_argument("--item", action="append", help="run cases that report on this item, e.g. U6")
    parser.add_argument("--columns", type=int, nargs="+", help="restrict to these widths where a case offers them")
    parser.add_argument("--list", action="store_true", help="list cases and exit")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without Python optimization so outcome assertions remain enabled")
    for name in MODULES:
        importlib.import_module(f"acceptance.{name}")
    cases = selected_cases(args)
    if args.list:
        for entry in cases:
            print(f"{entry['name']:28} {','.join(map(str, entry['columns'])):8} {' '.join(entry['bullets'])}")
        return 0
    source = PACKAGE_ROOT.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {
        "mode": "installed deterministic shell and provider fixtures; no live provider, no remote host",
        "commit": usability.run(["git", "rev-parse", "HEAD"], source, dict(os.environ)),
        "cases": [],
        "unqualified": ["Live authenticated provider", "Real terminal visual review (perceptual flicker)",
                        "Screen-only discoverability trial", "Authorized remote host", "macOS terminal apps"],
    }
    try:
        prefix = install(source, output)
        result["installed_hashes"] = usability.tree(prefix)
        result["source_files"] = {
            str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in ("src", "lib", "scripts/acceptance", "tests/fixtures/acceptance", "tests/fixtures/usability")
            for p in (source / root).rglob("*") if p.is_file()}
        for entry in cases:
            widths = [c for c in entry["columns"] if not args.columns or c in args.columns] or [entry["columns"][0]]
            for columns in widths:
                record = run_case(source, prefix, output, entry, columns)
                result["cases"].append(record)
                report.write(output, result)
                failed = [r["check"] for r in record["results"] if not r["passed"]]
                print(f"{record['status'].upper()} {record['id']} ({record['seconds']:.0f}s)"
                      + (": " + "; ".join(failed) if failed else ""), flush=True)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["infrastructure_error"] = str(error)
    report.write(output, result)
    print(report.summary(result))
    print(output / "index.html")
    if result.get("infrastructure_error"):
        return 2
    return int(any(c["status"] != "passed" for c in result["cases"]))


if __name__ == "__main__":
    raise SystemExit(main())
