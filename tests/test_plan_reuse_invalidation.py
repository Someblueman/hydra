#!/usr/bin/env python3
"""Copied-fixture invalidation checks for selective plan reuse."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
UNSUPPORTED_ENV = ("LD_PRELOAD", "LD_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES",
                   "DYLD_LIBRARY_PATH", "ENV", "BASH_ENV")


def digest(value):
    return hashlib.sha256(value).hexdigest()


def json_digest(value):
    return digest(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).replace("/", r"\/").encode())


def find_fixture(fixture):
    homes = fixture / "home"
    runs = list(homes.glob("state/v2/projects/*/workflows/runs/*"))
    if len(runs) != 1:
        raise AssertionError(f"expected one workflow run under {homes}, found {runs}")
    run = runs[0]
    source = fixture / "source"
    if not source.is_dir():
        raise AssertionError(f"missing fixture source: {source}")
    return homes, run.name, source


def recorded_environment(run, home, fleet_bin):
    manifest = json.loads((run / "artifacts" / "environment").read_text())
    variables = manifest["variables"]
    env = dict(os.environ)
    env.update({"HYDRA_HOME": str(home), "HYDRA_FLEET_BIN": str(fleet_bin)})
    env.update(variables)
    for name in UNSUPPORTED_ENV:
        env.pop(name, None)
    return env


def run_result(source, home, run_id, fleet_bin, expect_pass):
    env = recorded_environment(run_path(home, run_id), home, fleet_bin)
    command = [str(ROOT / "bin" / "hydra"), "workflow", "plan", "result", run_id]
    result = subprocess.run(command, cwd=source, env=env, capture_output=True,
                            text=True, timeout=45)
    if expect_pass:
        if result.returncode or not json.loads(result.stdout)["ok"]:
            raise AssertionError(result.stdout + result.stderr)
    elif result.returncode == 0:
        raise AssertionError("corrupted copied fixture was accepted: " + result.stdout)
    return result


def run_path(home, run_id):
    runs = list(home.glob("state/v2/projects/*/workflows/runs"))
    if len(runs) != 1:
        raise AssertionError(f"expected one copied project, found {runs}")
    return runs[0] / run_id


def rehash_reuse_record(run, step, file_name):
    record_path = run / "repair-2.json"
    record = json.loads(record_path.read_text())
    proof = record["evidence"]["reuse"]["steps"][step]
    proof[file_name] = digest((run / "steps" / step / "attempt-1" / file_name).read_bytes())
    record["sha256"] = json_digest(record["evidence"])
    record_path.write_text(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")


def tamper_input(run):
    path = run / "steps/produce/attempt-1/inputs/environment"
    path.write_bytes(path.read_bytes() + b" ")


def tamper_environment(run):
    path = run / "steps/produce/attempt-1/inputs/environment"
    value = json.loads(path.read_text())
    value["scope"] = "tampered"
    path.write_text(json.dumps(value, sort_keys=True) + "\n")


def tamper_recipe(run):
    path = run / "steps/produce/attempt-1/inputs/recipe"
    path.write_bytes(path.read_bytes() + b" ")


def tamper_acceptance(run):
    (run / "plan-accepted").write_text("0" * 64 + "\n")


def tamper_receipt(run):
    path = run / "steps/produce/attempt-1/remote/receipt.json"
    value = json.loads(path.read_text())
    value["submission_key"] = "tampered"
    path.write_text(json.dumps(value, sort_keys=True) + "\n")
    rehash_reuse_record(run, "produce", "remote/receipt.json")


def tamper_result(run):
    path = run / "steps/produce/attempt-1/remote/result.json"
    value = json.loads(path.read_text())
    value["result"]["receipt"]["task_id"] = "task_" + "0" * 64
    path.write_text(json.dumps(value, sort_keys=True) + "\n")
    rehash_reuse_record(run, "produce", "remote/result.json")


def tamper_failures(run):
    path = run / "repair-2.json"
    value = json.loads(path.read_text())
    value["evidence"]["failures"] = []
    value["sha256"] = json_digest(value["evidence"])
    path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")


def tamper_compiled(run):
    path = run / "compiled.json"
    value = json.loads(path.read_text())
    value["plan"]["objective"] += " tampered"
    path.write_text(json.dumps(value, sort_keys=True) + "\n")


def tamper_source_binding(run):
    path = run / "compiled.json"
    value = json.loads(path.read_text())
    value["source"]["sha256"] = "0" * 64
    path.write_text(json.dumps(value, sort_keys=True) + "\n")


def tamper_unknown_step(run):
    path = run / "repair-2.json"
    value = json.loads(path.read_text())
    value["evidence"]["reuse"]["steps"]["unknown"] = {}
    value["sha256"] = json_digest(value["evidence"])
    path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")


def tamper_dependency_removal(run):
    path = run / "repair-2.json"
    value = json.loads(path.read_text())
    del value["evidence"]["reuse"]["steps"]["inspect"]
    value["sha256"] = json_digest(value["evidence"])
    path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")


def policy_controls(fixture, source, home, run_id, fleet_bin):
    original = json.loads((fixture / "plan.json").read_text())
    env = recorded_environment(run_path(home, run_id), home, fleet_bin)
    changes = [
        ("policy-version", ("reuse_policy", "schema_version"), 2),
        ("policy-mode", ("reuse_policy", "mode"), "external_cache"),
        ("policy-extra-field", ("reuse_policy", "extra"), True),
        ("unknown-step", ("reuse_policy", "steps", "unknown"), {}),
        ("incomplete-dependencies", ("reuse_policy", "steps", "produce", "dependencies"), "unknown"),
        ("external-effects", ("reuse_policy", "steps", "produce", "effects"), "external"),
        ("unbound-environment", ("reuse_policy", "steps", "produce", "environment_input"), "missing"),
        ("repair-input", ("data", "steps", "produce", "inputs", "repair"), {"repair": True}),
        ("provenance-input", ("data", "steps", "produce", "inputs", "history"), {"provenance": "produce"}),
    ]
    with tempfile.TemporaryDirectory(prefix="hydra-reuse-policy-") as folder:
        for name, keys, value in [("baseline", (), None)] + changes:
            plan = json.loads(json.dumps(original))
            target = plan
            for key in keys[:-1]:
                target = target[key]
            if keys:
                target[keys[-1]] = value
            path = Path(folder) / (name + ".json")
            path.write_text(json.dumps(plan))
            result = subprocess.run([str(ROOT / "bin/hydra"), "workflow", "plan", "validate",
                str(path), str(fixture / "policy.json")], cwd=source, env=env,
                capture_output=True, text=True, timeout=30)
            document = json.loads(result.stdout)
            if name == "baseline":
                assert result.returncode == 0 and document["ok"], document
            else:
                assert result.returncode != 0 and not document["ok"], (name, document)
                assert any(row["code"] == "unsupported_reuse" for row in document["data"]["diagnostics"]), (name, document)
        print("policy: baseline accepted, 9 unsupported declarations rejected")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--fleet-bin", type=Path,
                        default=Path(os.environ.get("HYDRA_FLEET_BIN", ROOT / "build/hydra-fleet")))
    args = parser.parse_args()
    fixture = args.fixture.resolve()
    original_home, run_id, original_source = find_fixture(fixture)
    original_record = json.loads((run_path(original_home, run_id) / "repair-2.json").read_text())
    assert json_digest(original_record["evidence"]) == original_record["sha256"], "canonical hash parity"
    run_result(original_source, original_home, run_id, args.fleet_bin, True)
    policy_controls(fixture, original_source, original_home, run_id, args.fleet_bin)
    cases = [("input", tamper_input), ("environment", tamper_environment),
             ("recipe", tamper_recipe),
             ("acceptance", tamper_acceptance), ("receipt-rehashed", tamper_receipt),
             ("result-rehashed", tamper_result), ("malformed-failures", tamper_failures),
             ("compiled-contract", tamper_compiled), ("source-binding", tamper_source_binding),
             ("unknown-proof-step", tamper_unknown_step), ("dependency-removal", tamper_dependency_removal)]
    with tempfile.TemporaryDirectory(prefix="hydra-plan-reuse-") as scratch:
        for name, mutate in cases:
            case = Path(scratch) / name
            shutil.copytree(original_home, case / "home")
            mutate(run_path(case / "home", run_id))
            run_result(original_source, case / "home", run_id, args.fleet_bin, False)
            print(f"{name}: rejected")
    print("baseline: accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
