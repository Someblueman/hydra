#!/usr/bin/env python3
"""Planning-quality evaluation runner (roadmap 9E).

Checks the independent held-out oracle (the archived pilot's cases plus the
held-out additions, with their declared incorrect artifacts) and compares the
serial and fork/join scheduling patterns through the current compiler. Validation
and scheduling effects are reported separately. Every unobserved quantity is
reported as unknown, never estimated. The archived candidate arms are not run;
they remain reproducible at the commit named in archive-manifest.json.

    python3 scripts/plan-eval.py [--execute N] [--output FILE]
"""

import argparse
import json
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EVALUATION = ROOT / "examples/planning/evaluation"
sys.path.insert(0, str(EVALUATION))
sys.path.insert(0, str(ROOT / "scripts"))
import oracle  # noqa: E402
import plan_eval_hydra as hy  # noqa: E402

CLASSES = ("feature_correctness", "finite_research_claim", "manifest_membership")
PATTERN_STEPS = ("spawn-a", "spawn-b", "spawn-compose", "work-a", "work-b", "compose", "spawn-check", "check")
UNOBSERVED = "unknown (not observed)"


def export_archive(manifest, out: Path):
    """Export the archived held-out cases from Git history and verify every digest."""
    target = out / "archive"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    archive = subprocess.run(["git", "archive", manifest["commit"], *manifest["files"]], cwd=ROOT, capture_output=True, check=False)
    if archive.returncode:
        raise RuntimeError(f"archived commit {manifest['commit'][:12]} is not available in this checkout (shallow clone?): {archive.stderr.decode(errors='replace').strip()}")
    with tarfile.open(fileobj=__import__("io").BytesIO(archive.stdout)) as tar:
        tar.extractall(target, filter="data")
    for name, digest in manifest["files"].items():
        actual = oracle.sha256_file(target / name)
        if actual != digest:
            raise RuntimeError(f"archived file {name} digest {actual} differs from the manifest")
    return target / "examples/planning/evaluation"


def validation_phase(cases):
    """The independent checker against correct and declared-incorrect artifacts."""
    rows, controls = [], []
    for case in cases:
        accepted = oracle.equal_artifact(case["expected"], case["expected"])
        rows.append({"id": case["id"], "class": case["class"], "false_rejection": not accepted})
        for control in case["incorrect_artifacts"]:
            rejected = not oracle.equal_artifact(oracle.artifact_value(control), case["expected"])
            controls.append({"case": case["id"], "class": case["class"], "id": control["id"], "rejected": rejected, "reason": control["reason"]})
    return {"correct_total": len(rows), "false_rejections": sum(r["false_rejection"] for r in rows),
            "incorrect_total": len(controls), "incorrect_rejected": sum(c["rejected"] for c in controls),
            "false_acceptances": sum(not c["rejected"] for c in controls),
            "by_class": {name: {"incorrect_total": sum(1 for c in controls if c["class"] == name),
                                "incorrect_rejected": sum(1 for c in controls if c["class"] == name and c["rejected"]),
                                "false_rejections": sum(1 for r in rows if r["class"] == name and r["false_rejection"])} for name in CLASSES},
            "controls": controls,
            "limits": "Exact-artifact equality with strict JSON types and a 64 KiB bound; a rejected declared corruption shows the checker discriminates that corruption only."}


def scheduling_phase(hydra, example_binary: Path, execute):
    """Fixed-graph scheduling on the serial and fork/join patterns."""
    f = hydra.fixture("scheduling")
    for file in ("serial.json", "forkjoin.json", "policy.json", "compose.sh", "worker_a.sh", "worker_b.sh"):
        shutil.copy2(ROOT / "examples/planning/patterns" / file, f.repo / file)
    shutil.copy2(ROOT / "examples/planning/native/payload.sh", f.repo / "payload.sh")
    shutil.copy2(example_binary, f.repo / "plan-example")
    f.commit()
    hydra.init(f)
    compiled, digests = {}, {}
    for name in ("serial", "forkjoin"):
        compiled[name] = f.base / f"{name}.compiled.json"
        digests[name], _ = hydra.compile(f, f"{name}.json", "policy.json", compiled[name], f"compile-{name}")
    structural = hydra.evaluate(f, compiled["serial"], compiled["forkjoin"], None, "evaluate-structural")
    out = {"plans": digests, "structural": {concern: {"left": {k: v for k, v in structural[concern]["left"].items() if k != "estimates"},
                                                      "right": {k: v for k, v in structural[concern]["right"].items() if k != "estimates"},
                                                      "modeled_preference": structural[concern]["modeled_preference"], "reason": structural[concern]["reason"]}
                                            for concern in ("decomposition", "scheduling", "placement")},
           "matched_scope": structural["matched_scope"], "executed": None, "calibration": {"status": UNOBSERVED}}
    if not execute:
        return out
    observed, records = {}, {}
    for name in ("serial", "forkjoin"):
        run_id, seconds, succeeded, _ = hydra.run(f, compiled[name], digests[name], 0, f"run-{name}")
        verified, _ = hydra.result(f, run_id, f"result-{name}")
        records[name] = {**hy.run_record(hydra.run_dir(f, run_id), PATTERN_STEPS), "run_id": run_id, "verified_outcome": verified, "wall_seconds": round(seconds, 3)}
        observed[name] = hydra.observed(f, run_id, f"observed-{name}")
    hydra.kill(f, [f"{p}-{s}" for p in ("serial", "forkjoin") for s in ("a", "b", "compose", "check")])
    estimates = f.base / "observed.json"
    estimates.write_text(json.dumps(hy.merge_estimates(observed["serial"], observed["forkjoin"])) + "\n")
    measured = hydra.evaluate(f, compiled["serial"], compiled["forkjoin"], estimates, "evaluate-observed")["scheduling"]
    out["executed"] = {"runs": records, "parallelism": 4,
                       "time_to_verified_result_milliseconds": {"serial": measured["left"]["time_to_verified_result_milliseconds"], "forkjoin": measured["right"]["time_to_verified_result_milliseconds"]},
                       "critical_path_milliseconds": {"serial": measured["left"]["critical_path_milliseconds"], "forkjoin": measured["right"]["critical_path_milliseconds"]},
                       "modeled_preference": measured["modeled_preference"], "reason": measured["reason"],
                       "n": 1, "stochastic_variability": "not exercised: one run per fixed graph; ranges reflect one-second timestamp resolution, not repeated runs"}
    # Calibration: predict the fork/join graph from the serial run's step
    # durations, then compare with the fork/join run's own recorded elapsed time.
    if observed["serial"] and records["forkjoin"]["elapsed_seconds"] is not None:
        prediction = f.base / "prediction.json"
        prediction.write_text(json.dumps(hy.transplant_estimates(observed["serial"], digests["serial"], digests["forkjoin"], "serial run step durations transplanted onto the fork/join graph (prediction)")) + "\n")
        predicted = hydra.evaluate(f, compiled["forkjoin"], compiled["forkjoin"], prediction, "evaluate-prediction")["scheduling"]["left"]["time_to_verified_result_milliseconds"]
        elapsed_ms = records["forkjoin"]["elapsed_seconds"] * 1000
        within = predicted is not None and predicted[0] <= elapsed_ms <= predicted[1] + 1000
        out["calibration"] = {"status": "observed", "n": 1, "predicted_range_milliseconds": predicted, "observed_milliseconds": elapsed_ms,
                              "within_range": within, "limits": "One prediction from one run at one-second resolution; the fixed-graph model ignores scheduler latency, contention and transfers, so a miss is expected to be an under-prediction."}
    return out


def summary(report):
    lines = [f"plan-eval: {report['status']} ({len(report['case_set']['cases'])} cases, {report['validation']['incorrect_total']} incorrect artifacts)"]
    v = report["validation"]
    lines.append(f"  validation: {v['incorrect_rejected']}/{v['incorrect_total']} corruptions rejected, {v['false_acceptances']} false acceptances, {v['false_rejections']} false rejections")
    s = report["scheduling"]
    lines.append(f"  scheduling: depth serial {s['structural']['scheduling']['left']['depth_nodes']} vs forkjoin {s['structural']['scheduling']['right']['depth_nodes']}; executed: {'yes' if s['executed'] else 'no'}; calibration: {s['calibration'].get('status')}")
    for failure in report["failures"]:
        lines.append(f"  FAILURE: {failure}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "build/plan-eval/report.json")
    parser.add_argument("--execute", type=int, default=0, help="1 runs the serial and fork/join patterns through hydra workflow plan run; 0 compares them structurally only")
    parser.add_argument("--fleet", type=Path, default=ROOT / "build/hydra-fleet")
    parser.add_argument("--plan-example", type=Path, default=ROOT / "build/plan-example")
    args = parser.parse_args()
    out = args.output.parent
    out.mkdir(parents=True, exist_ok=True)
    started = time.time()
    manifest = json.loads((EVALUATION / "archive-manifest.json").read_text())
    failures = []
    report = {"schema_version": 1, "report": "hydra-plan-eval-1", "generated_at": int(started),
              "source_commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=False).stdout.strip() or None,
              "archive": {"commit": manifest["commit"], "files": manifest["files"], "verified": False}}
    try:
        archive = export_archive(manifest, out)
        report["archive"]["verified"] = True
        heldout = json.loads((archive / "planner-evaluation-heldout-v2.json").read_text())
        cases = oracle.load_cases(archive / "planner-evaluation-heldout-v2.json", EVALUATION / "heldout-v3-additions.json")
    except Exception as exc:  # the report must still say why nothing was measured
        report.update({"status": "fail", "failures": [f"setup: {exc}"]})
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"plan-eval: fail ({exc})")
        return 1
    report["matched_contract"] = heldout["matched_evaluation_contract"]
    report["case_set"] = {"archived": sum(c["source"] == "archived-v2" for c in cases), "additions": sum(c["source"] == "additions-v3" for c in cases),
                          "additions_file_sha256": oracle.sha256_file(EVALUATION / "heldout-v3-additions.json"), "oracle_sha256": oracle.sha256_file(EVALUATION / "oracle.py"),
                          "cases": [{"id": c["id"], "class": c["class"], "contract_id": c["contract_id"], "source": c["source"], "incorrect_artifacts": len(c["incorrect_artifacts"])} for c in cases]}
    report["validation"] = validation_phase(cases)
    hydra = hy.Hydra(ROOT, args.fleet.resolve(), out)
    try:
        report["scheduling"] = scheduling_phase(hydra, args.plan_example.resolve(), args.execute)
    except Exception as exc:
        report["scheduling"] = {"structural": None, "executed": None, "calibration": {"status": UNOBSERVED}}
        failures.append(f"scheduling: {exc}")
    report["calibration"] = {"fixed_graph_scheduling": report["scheduling"].get("calibration")}
    report["human_corrections"] = {"count": None, "status": UNOBSERVED, "note": "no human took part in these deterministic runs; the field is reserved for recorded corrections"}
    report["stochastic_variability"] = {"status": "not exercised", "note": "deterministic fixtures and one run per fixed graph"}
    v = report["validation"]
    if v["false_acceptances"] or v["incorrect_rejected"] != v["incorrect_total"]:
        failures.append("a declared incorrect artifact was accepted")
    if v["false_rejections"]:
        failures.append("a correct artifact was rejected")
    if sum(c["source"] == "archived-v2" for c in cases) != 9:
        failures.append("the archived held-out file no longer carries nine cases")
    report["failures"] = failures
    report["status"] = "fail" if failures else "pass"
    report["elapsed_seconds"] = round(time.time() - started, 3)
    report["limits"] = ["The oracle shows the checker rejects its declared corruptions; it measures no planner.",
                        "No planning benefit over its overhead is claimed; no live agent or provider took part.",
                        "Deterministic fixtures: stochastic planner variability is not exercised here."]
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(summary(report))
    print(f"report: {args.output}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
