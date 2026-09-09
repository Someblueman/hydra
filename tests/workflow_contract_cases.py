#!/usr/bin/env python3
"""Native data boundary and public plan CLI checks; optional supervised runs."""
import json
import copy
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HYDRA = ROOT / "bin/hydra"
RUNTIME = "--runtime" in sys.argv
if RUNTIME:
    sys.argv.remove("--runtime")


def write(path, value):
    path.write_text(json.dumps(value))


def contract(fields=None, *, unit="ms", kind="object", schema="measurement"):
    return {"schema": schema, "version": 1, "type": kind,
            "cardinality": {"min": 1, "max": 1},
            "fields": fields if fields is not None else {
                "candidate": {"type": "string", "equals": "current"},
                "duration": {"type": "integer", "unit": unit, "minimum": 0}}}


def declaration(schema=None, path="value.json"):
    schema = schema or contract()
    return {"path": path, "type": schema["type"], "max_bytes": 4096, "contract": schema}


def value(duration=1000, candidate="current", unit="ms"):
    return {"candidate": candidate, "duration": {"value": duration, "unit": unit}}


def data():
    return json.loads((ROOT / "tests/fixtures/plan-9b/data.json").read_text())


class Handoff(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-contract-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.env = dict(os.environ, HYDRA_HOME=str(self.root / "home"), HYDRA_SKIP_AI="1",
                        HYDRA_NONINTERACTIVE="1", HYDRA_NO_SWITCH="1")
        self.env.setdefault("HYDRA_FLEET_BIN", str(ROOT / "build/hydra-fleet"))
        self.run_command(["git", "init", "-q"])
        self.run_command(["git", "config", "user.name", "Contract Test"])
        self.run_command(["git", "config", "user.email", "contract@example.invalid"])
        (self.repo / "base").write_text("base\n")
        self.run_command(["git", "add", "."])
        self.run_command(["git", "-c", "commit.gpgSign=false", "commit", "-qm", "base"])
        self.manifest = self.root / "data.json"
        self.graph = self.root / "graph.tsv"
        self.graph.write_text("step\tproduce\texec\t-\nstep\tconsume\texec\tproduce\n")
        self.run = self.root / "run"
        self.run.mkdir()
        shutil.copy(self.graph, self.run / "graph.tsv")

    def run_command(self, args, *, ok=True):
        proc = subprocess.run([str(a) for a in args], cwd=self.repo, env=self.env,
                              text=True, capture_output=True, timeout=120)
        self.assertEqual(proc.returncode == 0, ok, (args, proc.stdout, proc.stderr))
        return proc

    def cli(self, *args, ok=True):
        if args[:2] == ("fleet", "workflow-data"):
            result = self.run_command([self.env["HYDRA_FLEET_BIN"], *args[1:]], ok=ok)
            response = json.loads(result.stdout)
            if not ok:
                self.assertEqual(response["error"]["code"], "invalid_data")
            return result
        return self.run_command([HYDRA, *args], ok=ok)

    def validate(self, manifest, *, ok=True):
        write(self.manifest, manifest)
        return self.cli("fleet", "workflow-data", "validate", self.root, "data.json", self.graph, ok=ok)

    def initialize(self, manifest):
        self.validate(manifest)
        self.cli("fleet", "workflow-data", "init", self.run, self.repo, self.root, "data.json")

    def prepare(self, step, *, ok=True):
        attempt = self.run / "steps" / step / "attempt-1"
        attempt.mkdir(parents=True)
        self.cli("fleet", "workflow-data", "prepare", self.run, step, attempt, ok=ok)
        return attempt

    def seal(self, step, attempt, *, ok=True):
        self.cli("fleet", "workflow-data", "seal", self.run, step, attempt, ok=ok)
        if ok:
            (attempt.parent / "state").write_text("succeeded\n")
            (attempt.parent / "authoritative-attempt").write_text("1\n")

    def test_compatible_sealed_handoff(self):
        self.initialize(data())
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value())
        self.seal("produce", producer)
        consumer = self.prepare("consume")
        self.assertEqual(json.loads((consumer / "inputs/value").read_text()), value())
        self.cli("fleet", "workflow-data", "verify-output", self.run, "produce", producer)

    def test_semantic_fields_units_identity_and_cardinality(self):
        for name, schema, payload in [
            ("empty", contract(), {}),
            ("semantic", contract(), {"duration": {"value": 1, "unit": "ms"}}),
            ("units", contract(), value(unit="s")),
            ("stale", contract(), value(candidate="old")),
            ("unknown", contract(), dict(value(), extra=1)),
            ("minimum", contract(), value(duration=-1)),
            ("integer-overflow", contract(), value(duration=1 << 64)),
            ("empty-array", contract(kind="array"), []),
            ("many", contract(kind="array"), [value(), value()]),
        ]:
            with self.subTest(name=name):
                case = self.root / name
                case.mkdir()
                write(case / "value.json", payload)
                manifest = {"schema_version": 2, "inputs": {"value": declaration(schema)}, "steps": {}}
                write(self.manifest, manifest)
                shutil.copy(self.graph, case / "graph.tsv")
                self.cli("fleet", "workflow-data", "init", case, case, self.root, "data.json", ok=False)

    def test_incompatible_and_unsupported_contracts(self):
        for mutation in ("unit", "version", "type", "unsupported", "missing"):
            manifest = data()
            c = manifest["steps"]["consume"]["inputs"]["value"]["contract"]
            if mutation == "unit":
                c["fields"]["duration"]["unit"] = "s"
            elif mutation == "version":
                c["version"] = 2
            elif mutation == "type":
                c["type"] = "array"
            elif mutation == "unsupported":
                c["fields"]["candidate"]["regex"] = ".*"
                manifest["steps"]["produce"]["outputs"]["value"]["contract"] = c
            else:
                del manifest["steps"]["consume"]["inputs"]["value"]["contract"]
            with self.subTest(mutation=mutation):
                self.validate(manifest, ok=False)

    def test_conversion_loss_and_preserved_fields(self):
        manifest = data()
        target = contract(unit="s", schema="seconds")
        manifest["steps"]["consume"]["outputs"] = {"converted": declaration(target)}
        manifest["steps"]["consume"]["conversion"] = {
            "input": "value", "output": "converted", "field": "duration", "numerator": 1, "denominator": 1000}
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value(duration=1500))
        self.seal("produce", producer)
        consumer = self.prepare("consume")
        write(consumer / "outputs/value.json", value(duration=1, unit="s"))
        self.seal("consume", consumer, ok=False)

    def test_lossless_conversion(self):
        manifest = data()
        manifest["steps"]["consume"]["outputs"] = {"converted": declaration(contract(unit="s"))}
        manifest["steps"]["consume"]["conversion"] = {
            "input": "value", "output": "converted", "field": "duration", "numerator": 1, "denominator": 1000}
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value(duration=2000))
        self.seal("produce", producer)
        consumer = self.prepare("consume")
        write(consumer / "outputs/value.json", value(duration=2, unit="s"))
        self.seal("consume", consumer)

    def test_composition_shared_invariant(self):
        manifest = data()
        manifest["steps"]["produce"]["outputs"]["other"] = declaration(path="other.json")
        step = manifest["steps"]["consume"]
        step["inputs"]["other"] = {"step": "produce", "output": "other", "contract": contract()}
        step["invariants"] = [{"phase": "before", "op": "equal",
                               "left": {"input": "value", "field": "duration"},
                               "right": {"input": "other", "field": "duration"}}]
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value(duration=1))
        write(producer / "outputs/other.json", value(duration=2))
        self.seal("produce", producer)
        self.prepare("consume", ok=False)

    def test_composition_postcondition(self):
        manifest = data()
        step = manifest["steps"]["consume"]
        step["outputs"] = {"total": declaration()}
        step["invariants"] = [{"phase": "after", "op": "sum",
                               "left": [{"input": "value", "field": "duration"}] * 2,
                               "right": {"output": "total", "field": "duration"}}]
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value(duration=1))
        self.seal("produce", producer)
        consumer = self.prepare("consume")
        write(consumer / "outputs/value.json", value(duration=3))
        self.seal("consume", consumer, ok=False)

    def test_missing_input_after_prepare(self):
        self.initialize(data())
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value())
        self.seal("produce", producer)
        consumer = self.prepare("consume")
        (consumer / "inputs/value").write_text("{}")
        self.seal("consume", consumer, ok=False)

    def test_legacy_contract_rejected_without_opt_in(self):
        manifest = data()
        manifest["schema_version"] = 1
        self.validate(manifest, ok=False)

    def test_null_and_duplicate_constraints_are_rejected(self):
        for key in ["invariants", "candidates", "conversion"]:
            manifest = data()
            manifest["steps"]["consume"][key] = None
            self.validate(manifest, ok=False)
        manifest = data()
        manifest["steps"]["produce"]["outputs"]["value"]["contract"]["fields"]["candidate"]["equals"] = None
        self.validate(manifest, ok=False)
        # Ordinary JSON parsers accept this; the contract boundary must not.
        text = json.dumps(data()).replace('"schema_version": 2', '"schema_version": 2, "schema_version": 2')
        self.manifest.write_text(text)
        self.cli("fleet", "workflow-data", "validate", self.root, "data.json", self.graph, ok=False)
        manifest = data(); manifest["steps"]["consume"]["inputs"]["value"]["provenance"] = False
        self.validate(manifest, ok=False)
        manifest = data(); manifest["steps"]["consume"]["inputs"]["value"]["step"] = "produce\0stale"
        self.validate(manifest, ok=False)
        self.manifest.write_text(json.dumps(data()).replace('"schema":', '"schema\\u0000alias":'))
        self.cli("fleet", "workflow-data", "validate", self.root, "data.json", self.graph, ok=False)

    def test_duplicate_value_members_rejected(self):
        self.initialize(data())
        producer = self.prepare("produce")
        (producer / "outputs/value.json").write_text('{"candidate":"previous","candidate":"current","duration":{"value":1,"unit":"ms"}}')
        self.seal("produce", producer, ok=False)

    def test_array_and_evidence_contract(self):
        manifest = data()
        fields = {"evidence": {"type": "strings", "equals": ["test", "source"]}}
        schema = contract(fields, kind="array")
        manifest["steps"]["produce"]["outputs"]["value"] = declaration(schema)
        manifest["steps"]["consume"]["inputs"]["value"]["contract"] = schema
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", [{"evidence": ["test", "source"]}])
        self.seal("produce", producer)
        self.prepare("consume")

    def test_missing_required_evidence(self):
        manifest = data()
        fields = {"evidence": {"type": "strings", "equals": ["test", "source"]}}
        schema = contract(fields)
        manifest["steps"]["produce"]["outputs"]["value"] = declaration(schema)
        manifest["steps"]["consume"]["inputs"]["value"]["contract"] = schema
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", {"evidence": ["test"]})
        self.seal("produce", producer, ok=False)

    def plan_fixture(self):
        fixture = ROOT / "tests/fixtures/plan"
        shutil.copytree(fixture / "repo", self.repo, dirs_exist_ok=True)
        self.run_command(["git", "add", "."])
        self.run_command(["git", "-c", "commit.gpgSign=false", "commit", "-qm", "fixture"])
        plan = json.loads((fixture / "plan.json").read_text())
        policy = json.loads((fixture / "policy.json").read_text())
        plan["data"]["schema_version"] = 2
        declarations = list(plan["data"]["inputs"].values())
        for step in plan["data"]["steps"].values():
            declarations.extend(step.get("outputs", {}).values())
        for decl in declarations:
            fields = {} if decl["type"] == "file" else {
                "schema_version": {"type": "integer", "equals": 1},
                "verdict": {"type": "string"}, "subject_sha256": {"type": "string"},
                "requirements": {"type": "strings", "equals": ["content"]},
                "evidence": {"type": "string"}}
            decl["contract"] = contract(fields, kind=decl["type"], schema="bytes" if not fields else "report")
        self.bind_references(plan)
        return plan, policy

    @staticmethod
    def bind_references(plan):
        d = plan["data"]
        for step in d["steps"].values():
            for ref in step.get("inputs", {}).values():
                if "provenance" in ref:
                    continue
                decl = d["inputs"][ref["input"]] if "input" in ref else d["steps"][ref["step"]]["outputs"][ref["output"]]
                ref["contract"] = copy.deepcopy(decl["contract"])

    def compile(self, plan, policy, *, ok=True):
        write(self.root / "plan.json", plan)
        write(self.root / "policy.json", policy)
        result = self.cli("workflow", "plan", "compile", self.root / "plan.json",
                          self.root / "policy.json", self.root / "compiled.json", ok=ok)
        if not ok:
            self.assertEqual(json.loads(result.stdout)["error"]["code"], "invalid_plan")
        return result

    def test_public_plan_compatible_and_schema(self):
        plan, policy = self.plan_fixture()
        self.compile(plan, policy)
        schema = json.loads(self.cli("workflow", "plan", "schema").stdout)
        self.assertIn("data2", schema["$defs"])
        self.assertIn("relations", schema["allOf"][0]["properties"])
        self.assertIn("strings", schema["$defs"]["contract"]["properties"]["fields"]["additionalProperties"]["properties"]["type"]["enum"])
        compiled = json.loads((self.root / "compiled.json").read_text())
        self.assertEqual(compiled["plan"]["data"], plan["data"])

    def test_public_plan_incompatible_contract(self):
        plan, policy = self.plan_fixture()
        plan["data"]["steps"]["verify"]["inputs"]["subject"]["contract"]["version"] = 2
        self.compile(plan, policy, ok=False)

    def test_public_plan_relation_types(self):
        plan, policy = self.plan_fixture()
        plan["relations"] = [{"type": kind, "from": "compose", "to": "verify", "enforcement": "dependency"}
                             for kind in ["data", "evidence", "order", "effect"]]
        plan["relations"].extend({"type": kind, "from": "verify", "to": "spawn", "enforcement": "descriptive"}
                                 for kind in ["resource", "provenance"])
        self.compile(plan, policy)
        (self.root / "compiled.json").unlink()
        plan["relations"][-2]["enforcement"] = "mutex"
        self.compile(plan, policy, ok=False)

    def candidate_plan(self):
        plan, policy = self.plan_fixture()
        d = plan["data"]
        fields = {name: {"type": "string"} for name in ["source_commit", "source_sha256", "config_sha256"]}
        d["steps"]["compose"]["outputs"]["report"] = declaration(contract(fields, schema="candidate"), "report.txt")
        d["steps"]["verify"]["inputs"]["source"] = {"provenance": "plan"}
        d["steps"]["verify"]["candidates"] = [{"phase": "before", "manifest": "subject", "source": "source",
                "bindings": {"config_sha256": {"kind": "configuration", "input": "expected"}}}]
        # The fixture envelope must reserve both generated manifest and report.
        plan["envelope"]["artifact_bytes"] = policy["envelope"]["artifact_bytes"] = 8192
        self.bind_references(plan)
        return plan, policy

    def candidate_run(self, stale=False):
        plan, policy = self.candidate_plan()
        response = self.compile(plan, policy)
        compiled = json.loads((self.root / "compiled.json").read_text())
        shutil.copy(self.root / "compiled.json", self.run / "compiled.json")
        (self.run / "plan-accepted").write_text(json.loads(response.stdout)["data"]["sha256"] + "\n")
        rows = "".join("step\t%s\t%s\t%s\n" % (step["id"], step["kind"], ",".join(step["needs"]) or "-") for step in plan["steps"])
        self.graph.write_text(rows)
        (self.run / "graph.tsv").write_text(rows)
        self.initialize(compiled["data"])
        producer = self.prepare("compose")
        source = compiled["source"]
        payload = {"source_commit": "0" * 40 if stale else source["commit"], "source_sha256": source["sha256"],
                   "config_sha256": hashlib.sha256((self.repo / "expected.txt").read_bytes()).hexdigest()}
        write(producer / "outputs/report.txt", payload)
        self.seal("compose", producer)
        self.prepare("verify", ok=not stale)

    def test_candidate_bound_to_source_and_configuration(self):
        self.candidate_run()

    def test_candidate_stale_source_stops_materialization(self):
        self.candidate_run(stale=True)

    def test_candidate_missing_coverage_rejected(self):
        plan, policy = self.candidate_plan()
        del plan["data"]["steps"]["verify"]["candidates"][0]["bindings"]["config_sha256"]
        self.compile(plan, policy, ok=False)

    def test_composed_candidate_binds_all_outputs(self):
        plan, policy = self.candidate_plan()
        d = plan["data"]
        produce = d["steps"]["compose"]
        consume = d["steps"]["verify"]
        produce["inputs"] = {"source": {"provenance": "plan"}, "expected": {"input": "expected"}}
        produce["outputs"]["program"] = declaration(contract({}, kind="file", schema="bytes"), "program.bin")
        produce["outputs"]["report"]["contract"]["fields"]["program_sha256"] = {"type": "string"}
        produce["candidates"] = [{"phase": "after", "manifest": "report", "source": "source", "bindings": {
            "config_sha256": {"kind": "configuration", "input": "expected"},
            "program_sha256": {"kind": "artifact", "output": "program"}}}]
        consume["inputs"]["program"] = {"step": "compose", "output": "program"}
        consume["candidates"][0]["bindings"]["program_sha256"] = {"kind": "artifact", "input": "program"}
        plan["envelope"]["artifact_bytes"] = policy["envelope"]["artifact_bytes"] = 16384
        self.bind_references(plan)
        response = self.compile(plan, policy)
        compiled = json.loads((self.root / "compiled.json").read_text())
        shutil.copy(self.root / "compiled.json", self.run / "compiled.json")
        (self.run / "plan-accepted").write_text(json.loads(response.stdout)["data"]["sha256"] + "\n")
        rows = "step\tcompose\texec\t-\nstep\tverify\texec\tcompose\n"
        self.graph.write_text(rows)
        (self.run / "graph.tsv").write_text(rows)
        self.initialize(compiled["data"])
        attempt = self.prepare("compose")
        source = json.loads((attempt / "inputs/source").read_text())
        (attempt / "outputs/program.bin").write_bytes(b"candidate program")
        source["config_sha256"] = hashlib.sha256((attempt / "inputs/expected").read_bytes()).hexdigest()
        source["program_sha256"] = hashlib.sha256(b"candidate program").hexdigest()
        write(attempt / "outputs/report.txt", source)
        self.seal("compose", attempt)
        self.prepare("verify")

    def test_provenance_requires_verified_collection(self):
        manifest = data()
        manifest["steps"]["consume"]["inputs"]["source"] = {"provenance": "step", "step": "produce"}
        rows = "step\tproduce\ttask\t-\nstep\tconsume\texec\tproduce\n"
        self.graph.write_text(rows)
        (self.run / "graph.tsv").write_text(rows)
        self.initialize(manifest)
        producer = self.prepare("produce")
        write(producer / "outputs/value.json", value())
        self.seal("produce", producer)
        # A succeeded flag and sealed file cannot substitute for a collected task receipt.
        self.prepare("consume", ok=False)

    @unittest.skipUnless(RUNTIME, "supervised acceptance requires an allocated tmux test slot")
    def test_supervised_consumer_not_submitted(self):
        self.cli("init", "--no-agent", "--trust")
        head = "contract-worker"
        self.addCleanup(lambda: subprocess.run([str(HYDRA), "kill", head, "--force"], cwd=self.repo,
                        env=self.env, capture_output=True, timeout=30, check=False))
        self.cli("spawn", head, "--no-agent")
        produce = self.root / "produce.sh"
        consume = self.root / "consume.sh"
        convert = self.root / "convert.sh"
        produce.write_text('#!/bin/sh\nset -eu\ncp "$1/payload.json" "$HYDRA_WORKFLOW_OUTPUTS_DIR/value.json"\n'
                           'if [ -f "$1/other.json" ]; then cp "$1/other.json" "$HYDRA_WORKFLOW_OUTPUTS_DIR/other.json"; fi\n')
        consume.write_text('#!/bin/sh\nset -eu\nprintf received > "$1/consumed"\n')
        convert.write_text('#!/bin/sh\nset -eu\ncp "$1/converted.json" "$HYDRA_WORKFLOW_OUTPUTS_DIR/value.json"\n')
        for case in ["compatible", "semantic", "units", "stale", "lossy", "composition"]:
            with self.subTest(case=case):
                manifest = data()
                payload = value()
                if case == "semantic":
                    payload = {"candidate": "current"}
                elif case == "units":
                    payload = value(unit="s")
                elif case == "stale":
                    payload = value(candidate="previous")
                elif case == "lossy":
                    payload = value(duration=1500)
                    target = contract(unit="s")
                    manifest["steps"]["convert"] = {
                        "inputs": {"value": {"step": "produce", "output": "value", "contract": contract()}},
                        "outputs": {"value": declaration(target)},
                        "conversion": {"input": "value", "output": "value", "field": "duration", "numerator": 1, "denominator": 1000}}
                    manifest["steps"]["consume"]["inputs"]["value"] = {"step": "convert", "output": "value", "contract": target}
                    write(self.root / "converted.json", value(duration=1, unit="s"))
                elif case == "composition":
                    manifest["steps"]["produce"]["outputs"]["other"] = declaration(path="other.json")
                    manifest["steps"]["consume"]["inputs"]["other"] = {"step": "produce", "output": "other", "contract": contract()}
                    manifest["steps"]["consume"]["invariants"] = [{"phase": "before", "op": "equal",
                        "left": {"input": "value", "field": "duration"}, "right": {"input": "other", "field": "duration"}}]
                    write(self.root / "other.json", value(duration=999))
                write(self.root / "payload.json", payload)
                write(self.manifest, manifest)
                flow = "version: 1\nid: contracts-%s\ndata: data.json\nresources:\n  disk_mb: 1\nsteps:\n" % case
                nodes = [("produce", "", produce)]
                if case == "lossy":
                    nodes.append(("convert", "produce", convert))
                nodes.append(("consume", "convert" if case == "lossy" else "produce", consume))
                for name, needs, script in nodes:
                    flow += ("  - id: %s\n    kind: exec\n    needs: [%s]\n    idempotent: false\n"
                             "    args:\n      head: %s\n      argv: [sh, %s, %s]\n" % (name, needs, head, script, self.root))
                (self.root / "flow.yml").write_text(flow)
                marker = self.root / "consumed"
                marker.unlink(missing_ok=True)
                self.cli("workflow", "validate", self.root / "flow.yml")
                self.cli("workflow", "run", self.root / "flow.yml", ok=case == "compatible")
                self.assertEqual(marker.exists(), case == "compatible", "consumer command execution marker")
                if case != "compatible":
                    # No consumer exec result means rejection happened before submission.
                    runs = list((self.root / "home/state/v2/projects").glob("*/workflows/runs/*"))
                    matching = [run for run in runs if (run / "resolved.yml").exists() and "contracts-" + case in (run / "resolved.yml").read_text()]
                    self.assertEqual(len(matching), 1)
                    consumer = matching[0] / "steps/consume"
                    self.assertFalse((consumer / "command-pid").exists())
                    self.assertFalse(list(consumer.glob("attempt-*/stdout")))
                    if case != "composition":
                        failed = matching[0] / "steps" / ("convert" if case == "lossy" else "produce")
                        self.assertEqual((failed / "state").read_text(), "failed\n")
                        sealed = json.loads((failed / "attempt-1/data-seal.json").read_text())
                        self.assertEqual(sealed["error"]["code"], "invalid_data")
                    if case == "composition":
                        self.assertEqual((matching[0] / "steps/produce/state").read_text(), "succeeded\n")
                        self.assertEqual((consumer / "state").read_text(), "failed\n")
                        preparation = json.loads((consumer / "attempt-1/data-preparation.json").read_text())
                        self.assertEqual(preparation["error"]["code"], "invalid_data")


if __name__ == "__main__":
    unittest.main()
