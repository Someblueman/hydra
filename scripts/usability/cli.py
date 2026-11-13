"""One local command: install a fresh prefix, run the scripted journeys, the
goal-driven trials and the comprehension trials at 80x24 and 140x40, and
write the report. ``--journey`` and ``--trial`` select cases; ``--prefix``
reuses an installed prefix while iterating (never for a release report)."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import time
from pathlib import Path

from usability.comprehension import SESSIONS, run_session
from usability.journey import Journey, run, tree
from usability.registry import JOURNEYS
from usability.report import report
from usability.trials import GOALS, run_goal

UNQUALIFIED = [
    "Live authenticated provider: the planning and delivery journeys use a labelled fixture",
    "Real terminal visual review: captures are escaped text without colors",
    "Human first-use review: the goal-driven evaluator is a fixed rule, not a novice",
    "Authorized remote host and remote onboarding: Hosts is checked only as far as its empty state",
]
TRIALS = (*GOALS, "comprehension")

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
    parser.add_argument("--trial", choices=TRIALS, help="run one goal-driven or comprehension trial only")
    parser.add_argument("--columns", type=int, choices=(80, 140), nargs="+", default=[80, 140])
    parser.add_argument("--prefix", type=Path, help="reuse an installed prefix (development only)")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without Python optimization so outcome assertions remain enabled")
    source = Path(__file__).resolve().parent.parent.parent
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = args.prefix.resolve() if args.prefix else output / "installed"
    selected_journeys = [args.journey] if args.journey else [] if args.trial else list(JOURNEYS)
    selected_trials = [args.trial] if args.trial else [] if args.journey else list(TRIALS)
    result = {
        "mode": "installed deterministic shell and provider fixtures"
                + (" (reused prefix; not a release report)" if args.prefix else ""),
        "journeys": [], "trials": [], "comprehension": [],
        "unqualified": UNQUALIFIED,
        "commit": run(["git", "rev-parse", "HEAD"], source, dict(os.environ)),
    }

    def emit(section: str, case: dict) -> None:
        result[section].append(case)
        report(output, result)
        print(f"{case['status'].upper()} {case['id']}: {case.get('reason', '')}", flush=True)

    try:
        if args.prefix:
            if not (prefix / "bin/hydra").exists():
                raise RuntimeError(f"no installed hydra under {prefix}")
        else:
            install(source, output, prefix)
        result["installed_hashes"] = tree(prefix)
        (output / "source.diff").write_text(run(["git", "diff", "HEAD"], source, dict(os.environ)))
        result["source_files"] = {
            str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in ("src", "lib", "scripts", "tests/fixtures/usability")
            for p in (source / root).rglob("*") if p.is_file()
        }
        for name in selected_journeys:
            for columns in args.columns:
                emit("journeys", run_case(source, prefix, output, name, columns, JOURNEYS[name]))
        for name in selected_trials:
            for columns in args.columns:
                if name == "comprehension":
                    for session in SESSIONS:
                        emit("comprehension", run_case(
                            source, prefix, output, f"comprehension-{session}", columns,
                            lambda j, s=session: {"concepts": run_session(j, s)}))
                else:
                    emit("trials", run_case(source, prefix, output, name, columns,
                                            lambda j, g=GOALS[name]: run_goal(j, g)))
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["infrastructure_error"] = str(error)
    report(output, result)
    print(output / "index.html")
    if result.get("infrastructure_error"):
        return 2
    cases = result["journeys"] + result["trials"] + result["comprehension"]
    return int(any(case["status"] != "passed" for case in cases))
