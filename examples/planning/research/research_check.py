#!/usr/bin/env python3
import csv, hashlib, json, math, os, sys

LIMITS = ["12 synthetic jobs", "one server", "exact known durations", "non-preemptive", "no production evidence"]

def digest(path):
    with open(path, "rb") as stream: return hashlib.sha256(stream.read()).hexdigest()

def jobs_from(path):
    with open(path, newline="") as stream: rows = list(csv.DictReader(stream))
    if len(rows) != 12 or (rows and set(rows[0]) != {"id", "arrival", "duration"}): raise ValueError("invalid jobs schema")
    seen, jobs = set(), []
    for order, row in enumerate(rows):
        if not row["id"] or row["id"] in seen: raise ValueError("duplicate job id")
        arrival, duration = int(row["arrival"]), int(row["duration"])
        if arrival < 0 or duration <= 0: raise ValueError("invalid job bounds")
        seen.add(row["id"]); jobs.append({"id": row["id"], "arrival": arrival, "duration": duration, "order": order})
    return jobs

def simulate(jobs, policy):
    left, time, schedule = [dict(x) for x in jobs], 0, []
    while left:
        ready = [x for x in left if x["arrival"] <= time]
        if not ready: time = min(x["arrival"] for x in left); ready = [x for x in left if x["arrival"] <= time]
        key = (lambda x: (x["arrival"], x["order"])) if policy == "FCFS" else (lambda x: (x["duration"], x["arrival"], x["order"]))
        job = min(ready, key=key); left.remove(job); start, finish = time, time + job["duration"]; time = finish
        schedule.append({"id": job["id"], "start": start, "finish": finish, "wait": start - job["arrival"], "turnaround": finish - job["arrival"]})
    turns, waits = [x["turnaround"] for x in schedule], [x["wait"] for x in schedule]
    return {"schedule": schedule, "mean_wait": sum(waits) / len(waits), "mean_turnaround": sum(turns) / len(turns), "p95_turnaround": sorted(turns)[math.ceil(.95 * len(turns)) - 1], "max_wait": max(waits), "worst_wait_job": max(schedule, key=lambda x: x["wait"])["id"]}

def canonical(value): return json.dumps(value, sort_keys=True, separators=(",", ":"))

def check(report, jobs, subject_hash):
    failures, actual = [], {p: simulate(jobs, p) for p in ("FCFS", "SJF")}
    if not isinstance(report, dict) or report.get("schema_version") != 3: return ["schema_version"], actual
    expected_keys = {"schema_version", "question", "provenance", "policies", "claims", "claim_locations", "limitations"}
    if set(report) != expected_keys: failures.append("report_schema")
    provenance = report.get("provenance", {})
    if not isinstance(provenance, dict) or provenance.get("data_path") != "jobs.csv" or provenance.get("data_sha256") != subject_hash or provenance.get("method") != "deterministic non-preemptive simulation; tie arrival then input order": failures.append("provenance")
    if report.get("policies") != actual: failures.append("policies")
    claims = report.get("claims", {})
    if not isinstance(claims, dict) or set(claims) != {"question", "recommendation", "constraint_checks", "limits", "competing_explanations"}: failures.append("claims_schema")
    else:
        qualified = {p: actual[p]["p95_turnaround"] <= 22 and actual[p]["max_wait"] <= 16 for p in actual}
        recommendation = next((p for p, ok in qualified.items() if ok), "neither qualifies")
        if claims.get("recommendation") != recommendation: failures.append("recommendation")
        if claims.get("constraint_checks") != qualified: failures.append("constraint_checks")
        if claims.get("limits") != LIMITS: failures.append("claim_limits")
        if not isinstance(claims.get("question"), str) or not claims["question"]: failures.append("question")
        if not isinstance(claims.get("competing_explanations"), list) or len(claims["competing_explanations"]) < 2: failures.append("explanations")
    locations = report.get("claim_locations")
    if not isinstance(locations, list) or not locations or not all(isinstance(x, str) and x.startswith("/") for x in locations): failures.append("claim_locations")
    if report.get("limitations") != LIMITS: failures.append("limitations")
    return failures, actual

def main():
    inp, out = os.environ["HYDRA_WORKFLOW_INPUTS_DIR"], os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"]
    validation = json.load(open(os.environ["HYDRA_WORKFLOW_VALIDATION_FILE"])); data = validation.get("data", {})
    subject = os.path.join(inp, "subject"); subject_hash = digest(subject); report, jobs, failures = {}, [], []
    try:
        jobs_path = os.path.join(inp, "jobs")
        report, jobs = json.load(open(subject)), jobs_from(jobs_path)
        failures, actual = check(report, jobs, digest(jobs_path))
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        failures, actual = ["malformed_input:" + type(error).__name__], {}
    qualified = {p: x["p95_turnaround"] <= 22 and x["max_wait"] <= 16 for p, x in actual.items()}
    recommendation = next((p for p, ok in qualified.items() if ok), "neither qualifies") if qualified else None
    raw_actual = {"policies": actual, "recommendation": recommendation, "constraint_checks": qualified}
    claimed = report.get("claims", {}) if isinstance(report, dict) else {}
    raw = {"actual": raw_actual, "claimed": {"recommendation": claimed.get("recommendation"), "constraint_checks": claimed.get("constraint_checks")}, "checks": failures or "all quantities recomputed"}
    metric_actual = {p: {key: actual[p][key] for key in ("mean_wait", "mean_turnaround", "p95_turnaround", "max_wait", "worst_wait_job")} for p in ("FCFS", "SJF") if p in actual}
    observations = [{"id": p.lower(), "raw": {"actual": metric_actual[p]}, "raw_sha256": hashlib.sha256(canonical({"actual": metric_actual[p]}).encode()).hexdigest()} for p in ("FCFS", "SJF")]
    rec_raw = {"actual": {"recommendation": recommendation, "constraint_checks": qualified}}
    observations.append({"id": "recommendation", "raw": rec_raw, "raw_sha256": hashlib.sha256(canonical(rec_raw).encode()).hexdigest()})
    records = []
    for obligation in ("means", "tails", "fairness", "recommendation", "reproduce", "limits"):
        records.append({"obligation_id": obligation, "subject_manifest_sha256": subject_hash, "validator_identity": "research-checker-v4", "validator_recipe_sha256": data.get("assessment-recipe"), "invocation": {"argv": ["python3", "research_check.py"], "exit_code": 0 if not failures else 1}, "environment": {"host": "local", "toolchain": "python3"}, "case_inventory": ["fcfs", "sjf", "recommendation"], "observations": observations, "raw_evidence_sha256": hashlib.sha256(canonical(observations).encode()).hexdigest(), "counts": {"executed": len(observations), "failed": 1 if failures else 0, "skipped": 0}, "limitations": ["bounded finite trace"]})
    result = {"schema_version": 3, "execution_status": "completed", "evidence_status": "valid" if not failures else "invalid", "domain_verdict": "pass" if not failures else "fail", "verdict": "pass" if not failures else "fail", "subject_sha256": subject_hash, "validator_sha256": data.get("assessment"), "requirements": ["means", "tails", "fairness", "recommendation", "reproduce", "limits"], "evidence": "independent recomputation against jobs.csv", "limitations": ["bounded finite trace"], "evidence_records": records}
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "assessment"), "w") as stream: json.dump(result, stream, separators=(",", ":"))
    print(json.dumps(result, separators=(",", ":"))); return 0 if not failures else 1

if __name__ == "__main__":
    try: sys.exit(main())
    except Exception as error:
        fallback = {"schema_version": 3, "execution_status": "completed", "evidence_status": "invalid", "domain_verdict": "fail", "verdict": "fail", "requirements": [], "evidence": "checker error: " + type(error).__name__}
        try:
            os.makedirs(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"], exist_ok=True)
            with open(os.path.join(os.environ["HYDRA_WORKFLOW_OUTPUTS_DIR"], "assessment"), "w") as stream: json.dump(fallback, stream, separators=(",", ":"))
        except Exception:
            pass
        print(json.dumps(fallback)); sys.exit(1)
