#!/usr/bin/env python3
"""Planning-quality evaluation runner (roadmap 9E).

Revives the archived finite pilot's cases at the current source without
altering the archived record, adds held-out cases, and compares the frozen
"current" and "contract-aware" candidate arms under matched scope, tools and
budgets. Planning, validation and scheduling effects are reported separately.
Every unobserved quantity is reported as unknown, never estimated.

    python3 scripts/plan-eval.py [--execute N] [--repeat K] [--output FILE]
"""

import argparse
import hashlib
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
    """Export the archived pilot from Git history and verify every digest."""
    target = out / "archive"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    archive = subprocess.run(["git", "archive", manifest["commit"], "examples/planning/evaluation"], cwd=ROOT, capture_output=True, check=False)
    if archive.returncode:
        raise RuntimeError(f"archived commit {manifest['commit'][:12]} is not available in this checkout (shallow clone?): {archive.stderr.decode(errors='replace').strip()}")
    with tarfile.open(fileobj=__import__("io").BytesIO(archive.stdout)) as tar:
        tar.extractall(target, filter="data")
    for name, digest in manifest["files"].items():
        actual = oracle.sha256_file(target / name)
        if actual != digest:
            raise RuntimeError(f"archived file {name} digest {actual} differs from the manifest")
    return target / "examples/planning/evaluation"


def by_class(rows, key):
    out = {}
    for row in rows:
        bucket = out.setdefault(row["class"], {"total": 0, key: 0})
        bucket["total"] += 1
        bucket[key] += int(bool(row[key]))
    return out


def planning_phase(archive: Path, manifest, cases, contracts, repeats):
    """Candidate acceptance per arm and interface; n = repeats per case."""
    arms = {}
    for arm, names in manifest["arms"].items():
        arms[arm] = []
        for name in names:
            candidate = archive / "candidates" / name / "candidate.py"
            entry = {"candidate": name, "sha256": oracle.sha256_file(candidate), "interfaces": {}}
            for interface in ("declared", "adapted"):
                signatures, rows, wall, cpu = set(), [], 0.0, 0.0
                for _ in range(repeats):
                    rows = []
                    for case in cases:
                        contract = contracts[case["contract_id"]]
                        inputs = oracle.declared_inputs(case["contract_id"], case["inputs"]) if interface == "declared" else oracle.adapted_inputs(case["contract_id"], case["inputs"], contract)
                        value, seconds, elapsed, error = oracle.run_candidate(candidate, case["contract_id"], inputs)
                        if interface == "adapted" and error is None:
                            value = oracle.adapt_output(value, contract)
                        accepted = error is None and oracle.equal_artifact(value, case["expected"])
                        rows.append({"id": case["id"], "class": case["class"], "source": case["source"], "accepted": accepted,
                                     "reason": "accepted" if accepted else (error or "output_mismatch")})
                        wall += elapsed
                        cpu += seconds if isinstance(seconds, (int, float)) else 0.0
                    signatures.add(tuple(row["accepted"] for row in rows))
                entry["interfaces"][interface] = {
                    "accepted": sum(row["accepted"] for row in rows), "total": len(rows),
                    "by_class": by_class(rows, "accepted"),
                    "by_source": {source: sum(1 for r in rows if r["source"] == source and r["accepted"]) for source in ("archived-v2", "additions-v3")},
                    "cases": rows, "repeats": repeats, "distinct_outcomes": len(signatures),
                    "wall_seconds_total": round(wall, 3), "cpu_seconds_total": round(cpu, 3)}
            arms[arm].append(entry)
    return arms


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


def current_source_phase(hydra, archive: Path, manifest, execute):
    """Admit the archived evaluation plan with the current compiler; optionally
    execute one run per arm through Hydra and read the verified outcome."""
    out = {"compiled": {}, "runs": {}}
    for arm, names in manifest["arms"].items():
        for index, name in enumerate(names):
            if index >= max(execute, 1):
                break
            f = hydra.fixture(f"current-source-{name}")
            for file in ("plan.json", "policy.json", "requests.json", "evaluate.py", "produce.py", "check.py", "planner-evaluation-heldout-v2.json"):
                shutil.copy2(archive / file, f.repo / file)
            shutil.copy2(archive / "candidates" / name / "candidate.py", f.repo / "candidate.py")
            f.commit()
            hydra.init(f)
            compiled = f.base / "compiled.json"
            digest, compile_seconds = hydra.compile(f, "plan.json", "policy.json", compiled, "compile")
            out["compiled"][name] = {"plan_sha256": digest, "compile_seconds": round(compile_seconds, 3), "admissible": True}
            if index >= execute:
                continue
            expected_verified = arm == "contract_aware"
            run_id, run_seconds, succeeded, as_expected = hydra.run(f, compiled, digest, 0 if expected_verified else 1, "run")
            verified, result_seconds = hydra.result(f, run_id, "result")
            run = hydra.run_dir(f, run_id)
            report_path = run / "steps/check/attempt-1/outputs/check.json"
            report = json.loads(report_path.read_text()) if report_path.exists() else {}
            record = hy.run_record(run, ["spawn-work", "work", "spawn-check", "check"])
            observed = hydra.observed(f, run_id, "observed")
            to_verified = None
            if observed:
                estimates = f.base / "observed.json"
                estimates.write_text(json.dumps(observed) + "\n")
                to_verified = hydra.evaluate(f, compiled, compiled, estimates, "evaluate")["scheduling"]["left"]["time_to_verified_result_milliseconds"]
            hydra.kill(f, ["evaluation-work", "evaluation-check"])
            out["runs"][name] = {
                "arm": arm, "run_id": run_id, "plan_sha256": digest, "run_state": record["state"],
                "verified_outcome": verified, "expected_verified": expected_verified, "as_expected": as_expected and verified == expected_verified,
                "checker_verdict": report.get("verdict"), "checker_counts": (report.get("evidence_records") or [{}])[0].get("counts"),
                "wall_seconds": {"run_command": round(run_seconds, 3), "result_command": round(result_seconds, 3), "recorded_run": record["elapsed_seconds"]},
                "time_to_verified_result_milliseconds": to_verified,
                "cost": "unknown (local Python candidate; no provider cost recorded)",
                "human_corrections": {"count": None, "status": UNOBSERVED},
                "repeated_work": {"steps": record["repeated_work_steps"], "attempts": record["attempts"]},
                "fixture": str(f.base)}
    return out


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
    for arm, entries in report["planning"].items():
        for entry in entries:
            d, a = entry["interfaces"]["declared"], entry["interfaces"]["adapted"]
            lines.append(f"  {arm}/{entry['candidate']}: declared {d['accepted']}/{d['total']}, adapted {a['accepted']}/{a['total']}, distinct outcomes over {d['repeats']} repeats: {d['distinct_outcomes']}")
    v = report["validation"]
    lines.append(f"  validation: {v['incorrect_rejected']}/{v['incorrect_total']} corruptions rejected, {v['false_acceptances']} false acceptances, {v['false_rejections']} false rejections")
    for name, run in report["execution"]["runs"].items():
        lines.append(f"  executed {name}: verified={run['verified_outcome']} as_expected={run['as_expected']} run {run['wall_seconds']['run_command']}s")
    s = report["scheduling"]
    lines.append(f"  scheduling: depth serial {s['structural']['scheduling']['left']['depth_nodes']} vs forkjoin {s['structural']['scheduling']['right']['depth_nodes']}; executed: {'yes' if s['executed'] else 'no'}; calibration: {s['calibration'].get('status')}")
    for failure in report["failures"]:
        lines.append(f"  FAILURE: {failure}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "build/plan-eval/report.json")
    parser.add_argument("--execute", type=int, default=0, help="candidates per arm to execute through hydra workflow plan run (also runs the scheduling patterns)")
    parser.add_argument("--repeat", type=int, default=2, help="repeated candidate invocations per case for the determinism check")
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
        contracts = {c["contract_id"]: c for c in json.loads((archive / "planner-visible-contracts.json").read_text())["task_contracts"]}
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
    report["planning"] = planning_phase(archive, manifest, cases, contracts, max(args.repeat, 1))
    report["validation"] = validation_phase(cases)
    hydra = hy.Hydra(ROOT, args.fleet.resolve(), out)
    try:
        report["execution"] = current_source_phase(hydra, archive, manifest, args.execute)
    except Exception as exc:
        report["execution"] = {"compiled": {}, "runs": {}}
        failures.append(f"execution: {exc}")
    try:
        report["scheduling"] = scheduling_phase(hydra, args.plan_example.resolve(), args.execute)
    except Exception as exc:
        report["scheduling"] = {"structural": None, "executed": None, "calibration": {"status": UNOBSERVED}}
        failures.append(f"scheduling: {exc}")
    report["calibration"] = {"by_class": {name: {"status": "unknown", "reason": "no estimate was supplied for this class; the frozen candidates estimate nothing"} for name in CLASSES},
                             "fixed_graph_scheduling": report["scheduling"].get("calibration")}
    report["human_corrections"] = {"count": None, "status": UNOBSERVED, "note": "no human took part in these deterministic runs; the field is reserved for recorded corrections"}
    report["stochastic_variability"] = {"status": "not exercised", "n_per_case": max(args.repeat, 1),
                                        "note": "frozen deterministic candidates and fixtures; repeated invocations are a determinism check, not a variability estimate"}
    v = report["validation"]
    if v["false_acceptances"] or v["incorrect_rejected"] != v["incorrect_total"]:
        failures.append("a declared incorrect artifact was accepted")
    if v["false_rejections"]:
        failures.append("a correct artifact was rejected")
    for arm, entries in report["planning"].items():
        for entry in entries:
            declared = entry["interfaces"]["declared"]
            if declared["distinct_outcomes"] != 1:
                failures.append(f"{entry['candidate']} produced varying outcomes across repeats")
            archived = declared["by_source"]["archived-v2"]
            if arm == "contract_aware" and archived != 9 or arm == "current" and archived != 0:
                failures.append(f"{entry['candidate']} archived-case acceptance {archived}/9 differs from the archived record")
    for name, run in report["execution"].get("runs", {}).items():
        if not run["as_expected"]:
            failures.append(f"executed {name}: verified outcome did not match the archived expectation")
    report["failures"] = failures
    report["status"] = "fail" if failures else "pass"
    report["elapsed_seconds"] = round(time.time() - started, 3)
    report["limits"] = ["Interface conformance, not semantic planning advantage, is the only difference these frozen arms show; the adapted interface makes it visible.",
                        "No planning benefit over its overhead is claimed; no live agent or provider took part.",
                        "Deterministic fixtures: stochastic planner variability is not exercised here."]
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(summary(report))
    print(f"report: {args.output}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
