"""Staged precompiler gates with a fixed public-result double; no E2E claim."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples/planning/staged"
PRECOMPILER = os.environ.get("HYDRA_PLAN_PRECOMPILE_BIN", str(ROOT / "build/plan-precompile"))


class Staged(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-staged-gates-")
        self.base = Path(self.temp.name)
        self.repo = self.base / "repo"
        shutil.copytree(SOURCE, self.repo, ignore=shutil.ignore_patterns("__pycache__"))
        self.finding = self.repo / "finding.json"
        self.manifest = self.repo / "manifest.json"
        self.public = self.base / "result.json"
        self.wrapper = self.base / "hydra"
        self.wrapper.write_text(
            "#!/usr/bin/env python3\nfrom pathlib import Path\nprint((Path(__file__).parent/'result.json').read_text())\n"
        )
        self.wrapper.chmod(0o755)
        self.env = {**os.environ, "HYDRA_BIN": str(self.wrapper)}
        self.prepare()

    def tearDown(self):
        self.temp.cleanup()

    def prepare(self, items=None):
        if items is not None:
            self.manifest.write_text(json.dumps({"schema_version": 1, "items": items}))
        inputs = self.base / "inputs"
        inputs.mkdir(exist_ok=True)
        shutil.copy2(self.manifest, inputs / "manifest")
        env = {
            **self.env,
            "HYDRA_WORKFLOW_INPUTS_DIR": str(inputs),
            "HYDRA_WORKFLOW_OUTPUTS_DIR": str(self.repo),
        }
        subprocess.run(
            ["python3", "stage1_run.py"],
            cwd=self.repo,
            env=env,
            check=True,
            capture_output=True,
        )
        self.bind()

    def bind(self):
        self.public.write_text(
            json.dumps(
                {
                    "ok": True,
                    "data": {
                        "schema_version": 1,
                        "plan_sha256": "a" * 64,
                        "verdict": "pass",
                        "deliverables": {
                            "report": {
                                "type": "file",
                                "path": str(self.finding),
                                "sha256": hashlib.sha256(
                                    self.finding.read_bytes()
                                ).hexdigest(),
                            }
                        },
                        "checks": {"check": {"verdict": "pass"}},
                    },
                }
            )
        )

    def precompile(self, error=None):
        output = self.base / "plan.json"
        output.write_text("prior plan")
        result = subprocess.run(
            [PRECOMPILER, "staged", "manifest.json", "run_fixture", output],
            cwd=self.repo,
            env=self.env,
            text=True,
            capture_output=True,
            timeout=10,
        )
        if error:
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn(error, result.stderr)
            self.assertEqual(output.read_text(), "prior plan")
            return None
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(output.read_text())

    def test_empty_all_skipped_collision_and_maximum_maps(self):
        for items in (
            [],
            [{"id": "only", "value": 2, "enabled": False}],
            [
                {"id": name, "value": 2, "enabled": True}
                for name in ("finding", "manifest", "compose", "check")
            ],
            [{"id": f"i{i}", "value": i, "enabled": True} for i in range(8)],
        ):
            with self.subTest(items=items):
                self.prepare(items)
                plan = self.precompile()
                selected = [item["id"] for item in items if item["enabled"]]
                self.assertEqual(
                    [
                        s["id"]
                        for s in plan["steps"]
                        if s["id"].startswith("work-item-")
                    ],
                    ["work-item-" + name for name in selected],
                )
                join = plan["data"]["steps"]["compose"]["inputs"]
                self.assertEqual(
                    set(join),
                    {"finding", "manifest", *("member-" + name for name in selected)},
                )

    def test_boundaries_refuse_before_public_result_lookup(self):
        original = self.manifest.read_text()
        for raw, error in (
            (json.dumps({"schema_version": 1, "items": {}}), "manifest cardinality"),
            (
                json.dumps(
                    {
                        "schema_version": 1,
                        "items": [
                            {"id": f"i{i}", "value": 1, "enabled": False}
                            for i in range(9)
                        ],
                    }
                ),
                "manifest cardinality",
            ),
            ('{"schema_version":1,"schema_version":1,"items":[]}', "duplicate"),
            (
                json.dumps(
                    {
                        "schema_version": 1,
                        "items": [{"id": "a", "value": True, "enabled": True}],
                    }
                ),
                "manifest member",
            ),
        ):
            self.manifest.write_text(raw)
            self.precompile(error)
        self.manifest.write_text(original)

    def test_public_result_and_raw_hash_gate(self):
        value = json.loads(self.public.read_text())
        value["data"]["verdict"] = "fail"
        self.public.write_text(json.dumps(value))
        self.precompile("not an accepted passing delivery")
        self.bind()
        self.finding.write_text(self.finding.read_text() + " ")
        self.precompile("finding hash mismatch")

    def test_stale_and_rehashed_semantic_forgery_refuse(self):
        original = json.loads(self.finding.read_text())
        for field, value, error in (
            ("source_sha256", "c" * 64, "stale or changed finding source"),
            ("selected_ids", [], "finding selection mismatch"),
            ("results", [], "finding results mismatch"),
            ("result_sha256", "d" * 64, "finding result hash mismatch"),
            ("status", "inconclusive", "finding status/schema"),
        ):
            with self.subTest(field=field):
                self.finding.write_text(json.dumps({**original, field: value}))
                self.bind()
                self.precompile(error)

        self.prepare([{"id": "one", "value": 1, "enabled": True}])
        forged = json.loads(self.finding.read_text())
        forged["results"][0]["square"] = True
        self.finding.write_text(json.dumps(forged))
        self.bind()
        self.precompile("finding results mismatch")

    def test_stage2_checker_rejects_missing_changed_and_false_typed_members(self):
        inputs, output = self.base / "checker-inputs", self.base / "checker-output"
        inputs.mkdir()
        output.mkdir()
        shutil.copy2(self.manifest, inputs / "manifest")
        shutil.copy2(self.finding, inputs / "finding")
        finding = json.loads(self.finding.read_text())
        valid = {
            "schema_version": 1,
            "selected_ids": finding["selected_ids"],
            "members": finding["results"],
        }
        validation = self.base / "validation.json"
        validation.write_text(
            json.dumps({"data": {"check": "a" * 64, "check-recipe": "b" * 64}})
        )
        env = {
            **self.env,
            "HYDRA_WORKFLOW_INPUTS_DIR": str(inputs),
            "HYDRA_WORKFLOW_OUTPUTS_DIR": str(output),
            "HYDRA_WORKFLOW_VALIDATION_FILE": str(validation),
        }
        altered = json.loads(json.dumps(valid))
        altered["members"][0]["square"] += 1
        for report, expected in (
            (valid, 0),
            ({**valid, "members": []}, 1),
            (altered, 1),
            ({**valid, "schema_version": True}, 1),
        ):
            (inputs / "subject").write_text(json.dumps(report))
            result = subprocess.run(
                ["python3", "check.py"],
                cwd=self.repo,
                env=env,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, expected, result.stderr)
            self.assertEqual(
                json.loads((output / "check.json").read_text())["verdict"],
                "pass" if expected == 0 else "fail",
            )


if __name__ == "__main__":
    unittest.main()
