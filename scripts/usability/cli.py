"""One local command: install a fresh prefix, run the scripted first-use and
planning walkthrough at 80x24 and 140x40 and write ``report.json``.
``--journey`` selects a journey; ``--prefix`` reuses an installed prefix while
iterating (never for a release report)."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import time
from pathlib import Path

from usability.journey import Journey, run, tree
from usability.registry import JOURNEYS

UNQUALIFIED = [
    "Live authenticated provider: the planning and delivery journeys use a labelled fixture",
    "Human first-use review: the walkthrough is a script, not a novice",
    "Authorized remote host and remote onboarding",
]

CASE_ERRORS = (AssertionError, OSError, RuntimeError, TimeoutError, subprocess.SubprocessError)


def run_case(source: Path, prefix: Path, output: Path, name: str, columns: int, body) -> dict:
    """Run one case in its own session; classify failed versus blocked setup
    and always retain proof, cleanup receipts and captures."""
    case = {"id": f"{name}-{columns}", "status": "passed"}
    started = time.monotonic()
    j = None
    try:
        j = Journey(source, prefix, output, name, columns)
        j.initialize()
        extra = body(j)
        if isinstance(extra, dict):
            case.update(extra)
        if j.failures:
            raise AssertionError("; ".join(j.failures))
    except CASE_ERRORS as error:
        reason = str(error)
        if j and j.failures and reason != "; ".join(j.failures):
            reason = "; ".join([*j.failures, reason])
        case.update(status="failed" if j else "blocked", reason=reason)
    finally:
        if j:
            try:
                j.close()
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                case.update(status="failed", reason=f"{case.get('reason', '')} Cleanup: {error}")
    case["seconds"] = time.monotonic() - started
    return case


def install(source: Path, output: Path, prefix: Path) -> None:
    env = {**os.environ, "PREFIX": str(prefix), "HYDRA_INSTALL_CORE": "required",
           "HYDRA_INSTALL_TUI": "required", "HYDRA_INSTALL_FLEET": "required", "DESTDIR": ""}
    (output / "install.log").write_text(run(["sh", "install.sh"], source, env))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--journey", choices=tuple(JOURNEYS), help="run one scripted journey only")
    parser.add_argument("--columns", type=int, choices=(80, 140), nargs="+", default=[80, 140])
    parser.add_argument("--prefix", type=Path, help="reuse an installed prefix (development only)")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without Python optimization so outcome assertions remain enabled")
    source = Path(__file__).resolve().parent.parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = args.prefix.resolve() if args.prefix else output / "installed"
    result = {
        "mode": "installed deterministic shell and provider fixtures"
                + (" (reused prefix; not a release report)" if args.prefix else ""),
        "journeys": [],
        "unqualified": UNQUALIFIED,
        "commit": run(["git", "rev-parse", "HEAD"], source, dict(os.environ)),
    }
    try:
        if args.prefix:
            if not (prefix / "bin/hydra").exists():
                raise RuntimeError(f"no installed hydra under {prefix}")
        else:
            install(source, output, prefix)
        result["installed_hashes"] = tree(prefix)
        for name in [args.journey] if args.journey else list(JOURNEYS):
            for columns in args.columns:
                case = run_case(source, prefix, output, name, columns, JOURNEYS[name])
                result["journeys"].append(case)
                print(f"{case['status'].upper()} {case['id']}: {case.get('reason', '')}", flush=True)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["infrastructure_error"] = str(error)
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(output / "report.json")
    if result.get("infrastructure_error"):
        print(f"infrastructure error: {result['infrastructure_error']}")
        return 2
    return int(any(case["status"] != "passed" for case in result["journeys"]))
