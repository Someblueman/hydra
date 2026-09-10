#!/usr/bin/env python3
"""Synthetic measurement controls, not performance measurements."""
import copy
import csv
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "examples/planning/performance"



class PerformanceOutcome(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hydra-performance-controls-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.inputs = self.base / "inputs"
        self.outputs = self.base / "outputs"
        self.inputs.mkdir()
        self.outputs.mkdir()
        shutil.copy(SOURCE / "records.txt", self.inputs / "records")
        self.context = self.base / "validation.json"
        self.context.write_text(json.dumps({"data": {"performance-check": "1" * 64,
                                                   "performance-check-recipe": "2" * 64}}))
        self.env = {**os.environ, "HYDRA_WORKFLOW_INPUTS_DIR": str(self.inputs),
                    "HYDRA_WORKFLOW_OUTPUTS_DIR": str(self.outputs), "HYDRA_WORKFLOW_REPO_ROOT": str(SOURCE),
                    "HYDRA_WORKFLOW_VALIDATION_FILE": str(self.context)}
        self.manifest, self.rows = self.fixture(.6)

    def fixture(self, multiplier):
        import hashlib
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        tools = {"shell": shutil.which("sh"), "awk": shutil.which("awk"), "python": sys.executable}
        manifest = {"schema_version": 1, "workload": {"path": "records.txt",
                    "sha256": digest(self.inputs / "records"), "bytes": (self.inputs / "records").stat().st_size},
                    "commands": {name: [tools["shell"], str(SOURCE / f"{name}.sh"), str(self.inputs / "records")]
                                 for name in ("baseline", "candidate")},
                    "sources": {name: {"path": f"{name}.sh", "sha256": digest(SOURCE / f"{name}.sh"),
                                      "bytes": (SOURCE / f"{name}.sh").stat().st_size} for name in ("baseline", "candidate")},
                    "environment": {"python": sys.version, "platform": platform.platform(), "machine": platform.machine(),
                    "cwd": str(SOURCE), "timer": "time.perf_counter_ns", "units": "nanoseconds",
                    "source_git": {name: subprocess.check_output(["git", "-C", str(SOURCE), "rev-parse", ref], text=True).strip()
                                   for name, ref in (("commit", "HEAD"), ("tree", "HEAD^{tree}"))},
                    "tools": {name: {"path": path, "sha256": digest(Path(path))} for name, path in tools.items()},
                    "load_before": [0., 0., 0.], "load_after": [0., 0., 0.]},
                    "protocol": {"warmups": 2, "trials": 10, "order": "alternating_AB_BA",
                    "stopping_rule": "exactly_10_pairs_no_exclusions_or_retries", "sample_timeout_seconds": 10,
                    "expected_count": "4003", "exclusions": [], "exclusive_hydra_measurement": True,
                    "exclusivity_scope": "coordinated Hydra jobs; other system activity is observed, not excluded"},
                    "warmups": [{"implementation": name, "warmup": n, "elapsed_ns": 1000, "status": "ok",
                                 "count": "4003", "returncode": 0} for n in (1, 2) for name in ("baseline", "candidate")],
                    "failures": []}
        rows = []
        for trial in range(1, 11):
            names = ("baseline", "candidate") if trial % 2 else ("candidate", "baseline")
            for position, name in enumerate(names):
                rows.append({"sample_id": f"{trial:02d}-{name}", "implementation": name, "trial": str(trial),
                             "order": str(position), "elapsed_ns": str(int((1000 + trial * 13) * (multiplier if name == "candidate" else 1))),
                             "status": "ok", "count": "4003", "returncode": "0"})
        return manifest, rows

    def compose(self, manifest=None, rows=None):
        manifest = self.manifest if manifest is None else manifest
        rows = self.rows if rows is None else rows
        (self.inputs / "manifest").write_text(json.dumps(manifest))
        with (self.inputs / "raw").open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(self.rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        subprocess.run([sys.executable, str(SOURCE / "analyze.py")], env=self.env,
                       check=True, capture_output=True, text=True, timeout=20)
        shutil.copy2(self.outputs / "report.json", self.inputs / "subject")
        return json.loads((self.inputs / "subject").read_text())

    def check(self, success):
        result = subprocess.run([sys.executable, str(SOURCE / "checker.py")], cwd=SOURCE,
                                env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        report = json.loads((self.outputs / "assessment").read_text())
        self.assertEqual(report["schema_version"], 3)
        self.assertEqual(report["domain_verdict"], "pass" if success else "fail")
        self.assertEqual(len(report["evidence_records"]), 5)
        for record in report["evidence_records"]:
            self.assertEqual(record["counts"], {"executed": 1, "failed": 0 if success else 1, "skipped": 0})
            self.assertIsInstance(record["observations"][0]["raw"]["measurement"], int)
        return report

    def test_positive_and_valid_no_improvement(self):
        report = self.compose()
        self.assertEqual(report["outcome"], "target established")
        self.check(True)
        self.manifest, self.rows = self.fixture(1.)
        report = self.compose()
        self.assertEqual(report["outcome"], "target not established")
        self.assertEqual(report["paired_uncertainty"]["estimate"], 0)
        self.check(True)

    def test_exact_measurement_domains(self):
        for fault in ("truncated", "duplicate", "wrong_count", "nonzero", "zero_time", "bad_order"):
            with self.subTest(fault=fault):
                rows = copy.deepcopy(self.rows)
                if fault == "truncated": rows.pop()
                elif fault == "duplicate": rows[1] = rows[0]
                elif fault == "wrong_count": rows[0]["count"] = "4002"
                elif fault == "nonzero": rows[0]["returncode"] = "7"
                elif fault == "zero_time": rows[0]["elapsed_ns"] = "0"
                else: rows[0]["order"] = "1"
                report = self.compose(rows=rows)
                self.assertEqual(report["outcome"], "invalid/insufficient measurement")
                self.check(False)

    def test_source_protocol_and_warmup_binding(self):
        for fault in ("source", "source_git", "workload", "units", "protocol", "missing_warmup", "failed_warmup", "failed_record"):
            with self.subTest(fault=fault):
                manifest = copy.deepcopy(self.manifest)
                if fault == "source": manifest["sources"]["baseline"]["sha256"] = "0" * 64
                elif fault == "source_git": manifest["environment"]["source_git"]["tree"] = "0" * 40
                elif fault == "workload": manifest["workload"]["bytes"] += 1
                elif fault == "units": manifest["environment"]["units"] = "milliseconds"
                elif fault == "protocol": del manifest["protocol"]["stopping_rule"]
                elif fault == "missing_warmup": manifest["warmups"] = []
                elif fault == "failed_warmup": manifest["warmups"][0]["returncode"] = 7
                else: manifest["failures"] = [{"phase": "warmup", "returncode": 7}]
                self.compose(manifest=manifest)
                self.check(False)

    def test_tampered_subject_and_raw_are_rejected(self):
        for fault in ("manifest", "raw", "claim", "interval", "limits", "unknown_field"):
            with self.subTest(fault=fault):
                report = self.compose()
                if fault == "manifest": report["manifest"]["commands"]["baseline"][0] = "/different/tool"
                elif fault == "raw": report["raw_samples"][1] += "0"
                elif fault == "claim": report["outcome"] = "target not established"
                elif fault == "interval": report["paired_uncertainty"]["upper"] = -100
                elif fault == "limits": report["limits"] = ["General speedup established"]
                else: report["hidden_exclusion"] = 1
                (self.inputs / "subject").write_text(json.dumps(report))
                self.check(False)

    def test_measurement_requires_coordinated_window(self):
        env = dict(self.env)
        env.pop("HYDRA_PERFORMANCE_EXCLUSIVE", None)
        result = subprocess.run([sys.executable, str(SOURCE / "measure.py")], env=env,
                                capture_output=True, text=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("coordinated measurement window", result.stderr)
        self.assertFalse((self.outputs / "raw.csv").exists())


if __name__ == "__main__":
    unittest.main()
