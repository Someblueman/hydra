#!/usr/bin/env python3
"""Validate a closed finite manifest and lower it to a static schema-1 plan."""
import json, re, sys
from pathlib import Path

ID = re.compile(r"^[a-z][a-z0-9-]{0,31}$")
MAX_ITEMS = 8
MIN_VALUE, MAX_VALUE = -10, 10

def fail(message):
    raise ValueError(message)

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result

def load_manifest(path):
    try:
        content = Path(path).read_bytes()
        if len(content) > 4096:
            fail("manifest exceeds 4096 bytes")
        obj = json.loads(content, object_pairs_hook=unique_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest: {exc}")
    if not isinstance(obj, dict) or set(obj) != {"schema_version", "items"} or type(obj.get("schema_version")) is not int or obj["schema_version"] != 1:
        fail("manifest must be schema 1 with only schema_version and items")
    items = obj["items"]
    if not isinstance(items, list) or len(items) > MAX_ITEMS:
        fail(f"items must contain at most {MAX_ITEMS} records")
    seen = set()
    for i, item in enumerate(items):
        if not isinstance(item, dict) or set(item) != {"id", "value", "enabled"}:
            fail(f"items[{i}] has unknown fields or malformed type")
        ident, value, enabled = item["id"], item["value"], item["enabled"]
        if not isinstance(ident, str) or not ID.fullmatch(ident) or ident in seen:
            fail(f"items[{i}].id must be unique and match {ID.pattern}")
        if isinstance(value, bool) or not isinstance(value, int) or not MIN_VALUE <= value <= MAX_VALUE:
            fail(f"items[{i}].value must be an integer in [{MIN_VALUE}, {MAX_VALUE}]")
        if not isinstance(enabled, bool):
            fail(f"items[{i}].enabled must be boolean")
        seen.add(ident)
    return obj

def lower(manifest):
    items = manifest["items"]
    enabled = [x for x in items if x["enabled"]]
    steps = []
    def add(step_id, role, kind, needs, args):
        steps.append({"id": step_id, "role": role, "kind": kind, "needs": needs, "writes": [], "args": args})
    for item in enabled:
        ident = item["id"]
        add(f"spawn-item-{ident}", "work", "spawn", [], {"branch": f"manifest-item-{ident}", "terminal_mode": "headless"})
    for item in enabled:
        ident = item["id"]
        add(f"work-item-{ident}", "work", "exec", [f"spawn-item-{ident}"],
            {"head": f"manifest-item-{ident}", "argv": ["python3", "worker.py", ident, str(item["value"])], "timeout": 30})
    compose_needs = [f"work-item-{x['id']}" for x in enabled]
    compose_needs += ["spawn-compose"]
    # A compose head is always present, including empty/all-skipped manifests.
    add("spawn-compose", "work", "spawn", [], {"branch": "manifest-compose", "terminal_mode": "headless"})
    add("compose", "compose", "exec", compose_needs, {"head": "manifest-compose", "argv": ["python3", "compose.py"], "timeout": 30})
    add("spawn-check", "work", "spawn", [], {"branch": "manifest-check", "terminal_mode": "headless"})
    add("check", "verify", "exec", ["spawn-check", "compose"], {"head": "manifest-check", "argv": ["python3", "check.py"], "timeout": 30})
    data_steps = {}
    for item in enabled:
        ident = item["id"]
        data_steps[f"work-item-{ident}"] = {"outputs": {"result": {"type": "file", "path": f"result-{ident}.json", "max_bytes": 128}}}
    compose_inputs = {"manifest": {"input": "manifest"}}
    for item in enabled:
        ident = item["id"]
        compose_inputs[f"member-{ident}"] = {"step": f"work-item-{ident}", "output": "result"}
    data_steps["compose"] = {"inputs": compose_inputs,
                              "outputs": {"report": {"type": "file", "path": "report.json", "max_bytes": 4096}}}
    data_steps["check"] = {"inputs": {"subject": {"step": "compose", "output": "report"}, "manifest": {"input": "manifest"}},
                            "outputs": {"check": {"type": "object", "path": "check.json", "max_bytes": 8192}}}
    return {
      "schema_version": 1, "id": "manifest-map",
      "objective": "Compute bounded squares for enabled manifest members and explicitly report skipped members.",
      "context": [], "assumptions": ["The manifest is validated and bound before compilation; membership is finite and fixed."], "questions": [],
      "envelope": {"hosts": ["local"], "tools": ["python3"], "effects": ["worktree", "execute"], "writes": [],
                    "parallelism": 4, "timeout_seconds": 600, "artifact_bytes": 65536, "max_heads": 10, "disk_mb": 1, "retry_budget": 0, "repair_budget": 0},
      "steps": steps,
      "deliverables": [{"id": "report", "description": "Square results and explicit skips", "step": "compose", "output": "report", "destination": "run-artifact"}],
      "checks": [{"id": "check", "method": "executable", "definition": json.dumps({"predicate": "equals", "cases": [{"id": "membership-case", "expected": "pass"}]}), "step": "check", "input": "subject", "report": "check", "deliverable": "report"}],
      "requirements": [{"id": "membership", "criterion": "Every manifest member is executed or explicitly skipped and enabled values are squared.", "deliverable": "report", "check": "check"}],
      "data": {"schema_version": 1, "inputs": {"manifest": {"path": "manifest.json", "type": "file", "max_bytes": 4096}}, "steps": data_steps},
      "obligations": [{"id": "membership-check", "requirement": "membership", "intent_ref": "objective",
          "subject": {"deliverable": "report", "step": "compose", "output": "report"},
          "criterion": "Every manifest member is executed or explicitly skipped and enabled values are squared.",
          "evaluation": {"method": "executable", "check": "check"},
          "required_evidence": ["subject_sha256", "verdict", "evidence", "case_inventory", "observations", "raw_evidence_sha256"],
          "environment": {"hosts": ["local"], "tools": ["python3"], "effects": ["execute"]},
          "completion_rule": "verdict=pass", "limitations": ["Finite arithmetic contract; no runtime graph expansion."]}],
    }

def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} MANIFEST PLAN", file=sys.stderr); return 2
    try:
        if Path(argv[1]).resolve() != Path("manifest.json").resolve():
            fail("run in the copied example repository with its manifest.json input")
        plan = lower(load_manifest(argv[1]))
    except ValueError as exc:
        print(f"manifest error: {exc}", file=sys.stderr); return 1
    Path(argv[2]).write_text(json.dumps(plan, indent=2) + "\n")
    return 0
if __name__ == "__main__": sys.exit(main(sys.argv))
