#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / "examples/planning/research"


class ResearchOutcomeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.inputs = self.tmp / "inputs"
        self.outputs = self.tmp / "outputs"
        self.inputs.mkdir(); self.outputs.mkdir()
        shutil.copy(RESEARCH / "jobs.csv", self.inputs / "jobs")
        report_env = dict(os.environ, HYDRA_WORKFLOW_INPUTS_DIR=str(self.inputs), HYDRA_WORKFLOW_OUTPUTS_DIR=str(self.tmp / "analysis"))
        Path(report_env["HYDRA_WORKFLOW_OUTPUTS_DIR"]).mkdir()
        subprocess.run(["python3", str(RESEARCH / "research_report.py")], env=report_env, check=True)
        shutil.copy(Path(report_env["HYDRA_WORKFLOW_OUTPUTS_DIR"]) / "report.json", self.inputs / "subject")
        self.validation = self.inputs / "validation.json"
        self.validation.write_text(json.dumps({"data": {"assessment": "a" * 64, "assessment-recipe": "b" * 64}}))
        self.env = dict(os.environ, HYDRA_WORKFLOW_INPUTS_DIR=str(self.inputs), HYDRA_WORKFLOW_OUTPUTS_DIR=str(self.outputs), HYDRA_WORKFLOW_VALIDATION_FILE=str(self.validation))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def run_check(self):
        return subprocess.run(["python3", str(RESEARCH / "research_check.py")], env=self.env, text=True, capture_output=True)

    def test_valid_negative_recommendation_emits_schema3_evidence(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        assessment = json.loads((self.outputs / "assessment").read_text())
        self.assertEqual(assessment["verdict"], "pass")
        self.assertEqual(assessment["validator_sha256"], "a" * 64)
        self.assertEqual(assessment["evidence_records"][0]["validator_recipe_sha256"], "b" * 64)
        self.assertEqual(assessment["evidence_records"][0]["case_inventory"], ["fcfs", "sjf", "recommendation"])

    def test_unsupported_recommendation_is_rejected(self):
        report = json.loads((self.inputs / "subject").read_text())
        report["claims"]["recommendation"] = "SJF is generally best"
        (self.inputs / "subject").write_text(json.dumps(report))
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads((self.outputs / "assessment").read_text())["verdict"], "fail")

    def test_malformed_subject_is_rejected_without_traceback(self):
        (self.inputs / "subject").write_text("{")
        result = self.run_check()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("Traceback", result.stdout + result.stderr)
        self.assertEqual(json.loads((self.outputs / "assessment").read_text())["verdict"], "fail")


if __name__ == "__main__":
    unittest.main()
