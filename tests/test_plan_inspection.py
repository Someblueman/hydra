#!/usr/bin/env python3
"""Public, read-only plan explanations and matched comparisons; no execution."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PlanInspection(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.repo = self.path / "repo"
        shutil.copytree(ROOT / "tests/fixtures/plan/repo", self.repo)
        self.env = {**os.environ, "HYDRA_HOME": str(self.path / "home"),
                    "HYDRA_FLEET_BIN": os.environ.get("HYDRA_FLEET_BIN", str(ROOT / "build/hydra-fleet"))}
        for args in (("init", "-q"), ("add", "."),
                     ("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture")):
            subprocess.run(["git", *args], cwd=self.repo, check=True, capture_output=True)
        self.plan = json.loads((ROOT / "tests/fixtures/plan/plan.json").read_text())
        self.policy = ROOT / "tests/fixtures/plan/policy.json"

    def cli(self, *args, success=True):
        result = subprocess.run([str(ROOT / "bin/hydra"), "workflow", "plan", *map(str, args)],
                                cwd=self.repo, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def compile(self, name, plan=None):
        source = self.path / f"{name}.plan.json"
        source.write_text(json.dumps(plan or self.plan))
        output = self.path / f"{name}.compiled.json"
        self.cli("compile", source, self.policy, output)
        digest = self.cli("show", output, "--json")["data"]["sha256"]
        return output, digest

    def estimates(self, *entries):
        data = {"schema_version": 1, "plans": {}}
        for digest, low, high in entries:
            data["plans"][digest] = {"source": "Explicit test intervals", "cost_unit": "USD",
                                      "steps": {name: {"milliseconds": [low, high], "cost_microunits": [low, high]}
                                                for name in ("spawn", "compose", "verify")}}
        file = self.path / "estimates.json"
        file.write_text(json.dumps(data))
        return file

    def test_explain_all_nodes_edges_and_unknowns(self):
        path, digest = self.compile("base")
        before = subprocess.check_output(["git", "status", "--porcelain"], cwd=self.repo)
        data = self.cli("explain", path)["data"]
        self.assertEqual(data["plan_sha256"], digest)
        self.assertEqual([n["id"] for n in data["nodes"]], ["spawn", "compose", "verify"])
        self.assertTrue(all(n["outcome_link"] == "reachable" for n in data["nodes"]))
        self.assertEqual(data["nodes"][1]["dependencies"][0]["reasons"], ["creates_execution_head"])
        reason = data["nodes"][2]["dependencies"][0]["reasons"][0]
        self.assertEqual(reason, {"kind": "artifact_input", "input": "subject", "output": "report"})
        self.assertIsNone(data["metrics"]["critical_path_milliseconds"])
        self.assertIsNone(data["metrics"]["cost_microunits"])
        self.assertEqual(data["metrics"]["hard_budgets"]["timeout_seconds"], 180)
        self.assertEqual(before, subprocess.check_output(["git", "status", "--porcelain"], cwd=self.repo))

    def test_ranges_are_bound_deterministic_and_never_authorize(self):
        left, lid = self.compile("left")
        changed = copy.deepcopy(self.plan)
        changed["steps"][1]["args"]["argv"] = ["sh", "other-compose.sh"]
        right, rid = self.compile("right", changed)
        file = self.estimates((lid, 10, 20), (rid, 100, 200))
        data = self.cli("compare", left, right, "--estimates", file)["data"]
        self.assertTrue(data["matched_scope"])
        self.assertEqual(data["modeled_preference"], "left")
        self.assertEqual(data["left"]["critical_path_milliseconds"], [30, 60])
        self.assertEqual(data, self.cli("compare", left, right, "--estimates", file)["data"])
        self.assertTrue(data["invalidation"]["checks"][0]["binding_changed"])
        self.assertIn("no reuse", data["invalidation"]["reuse_policy"])
        self.assertEqual(self.cli("compare", left, right)["data"]["modeled_preference"], "unresolved")
        file = self.estimates((lid, 40000, 50000), (rid, 100000, 200000))
        data = self.cli("compare", left, right, "--estimates", file)["data"]
        self.assertTrue(data["left"]["estimate_budget_conflict"])
        self.assertEqual(data["modeled_preference"], "unresolved")

    def test_scope_change_prevents_ranking(self):
        left, _ = self.compile("left")
        changed = copy.deepcopy(self.plan)
        changed["requirements"][0]["criterion"] = "A materially different target"
        right, _ = self.compile("right", changed)
        data = self.cli("compare", left, right)["data"]
        self.assertFalse(data["matched_scope"])
        self.assertIn("requirements", data["scope_differences"])
        self.assertEqual(data["modeled_preference"], "unresolved")

    def test_budget_or_output_contract_change_is_not_matched(self):
        left, _ = self.compile("left")
        for key in ("envelope", "data"):
            changed = copy.deepcopy(self.plan)
            if key == "envelope":
                changed[key]["timeout_seconds"] = 170
            else:
                changed[key]["steps"]["compose"]["outputs"]["report"]["max_bytes"] = 1023
            right, _ = self.compile(key, changed)
            data = self.cli("compare", left, right)["data"]
            self.assertFalse(data["matched_scope"])
            self.assertIn(key, data["scope_differences"])

    def test_malformed_stale_and_unknown_estimates_rejected(self):
        path, digest = self.compile("base")
        file = self.estimates((digest, 1, 2))
        original = json.loads(file.read_text())
        cases = []
        for values in ([-1, 2], [2, 1], [0, 10**13], [1.5, 2], [True, 2], [1]):
            data = copy.deepcopy(original)
            data["plans"][digest]["steps"]["spawn"]["milliseconds"] = values
            cases.append(data)
        data = copy.deepcopy(original)
        data["plans"]["0" * 64] = data["plans"].pop(digest)
        cases.append(data)
        data = copy.deepcopy(original)
        data["plans"][digest]["steps"]["unknown"] = {}
        cases.append(data)
        for data in cases:
            file.write_text(json.dumps(data))
            self.cli("explain", path, "--estimates", file, success=False)
        data = json.loads(path.read_text())
        data["plan"]["steps"][0]["needs"] = ["verify"]
        path.write_text(json.dumps(data))
        self.cli("explain", path, success=False)

    def test_partial_ranges_remain_unknown_and_extra_order_is_explicit(self):
        plan = copy.deepcopy(self.plan)
        plan["steps"][2]["needs"].append("spawn")
        path, digest = self.compile("base", plan)
        file = self.estimates((digest, 5, 7))
        estimates = json.loads(file.read_text())
        del estimates["plans"][digest]["steps"]["compose"]["milliseconds"]
        file.write_text(json.dumps(estimates))
        data = self.cli("explain", path, "--estimates", file)["data"]
        self.assertIsNone(data["metrics"]["critical_path_milliseconds"])
        self.assertEqual(data["metrics"]["cost_microunits"], [15, 21])
        self.assertEqual(data["metrics"]["edges"], 3)


if __name__ == "__main__":
    unittest.main()
