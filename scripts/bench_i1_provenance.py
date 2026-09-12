"""Fingerprint an exported Hydra runtime independently from the benchmark harness."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any

from bench_i1_fixture import sha


def runtime_provenance(source: Path, manifest: Path | None) -> dict[str, Any]:
    if manifest is not None:
        declared = json.loads(manifest.read_text())
        candidate = "base_commit" in declared
        if candidate and "source_commit" in declared:
            raise ValueError(
                "export cannot declare both an exact commit and an overlay"
            )
        commit = declared["base_commit" if candidate else "source_commit"]
        if len(commit) != 40 or any(
            value not in "0123456789abcdef" for value in commit
        ):
            raise ValueError("export manifest lacks an exact source commit")
        expected = dict(
            declared["runtime_source_hashes" if candidate else "source_files"]
        )
        if candidate:
            binaries = declared["native_binaries"]
            if set(binaries) != {"hydra-core", "hydra-tui", "hydra-fleet"}:
                raise ValueError("candidate must declare all three runtime binaries")
            for name, digest in binaries.items():
                path = f"build/{name}"
                if path in expected:
                    raise ValueError("candidate binary is also declared as source")
                expected[path] = digest
        for name in expected:
            relative = Path(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("unsafe export manifest path")
        actual = {
            str(path.relative_to(source)): sha(path)
            for path in sorted(source.rglob("*"))
            if path.is_file()
        }
        if not expected or actual != expected:
            raise ValueError("runtime export differs from its complete file manifest")
        provenance: dict[str, Any] = {
            "kind": "exported source",
            "manifest_sha256": sha(manifest),
            "files": actual,
        }
        if candidate:
            overlay = declared["files"]
            patch = declared["patch_sha256"]
            if (
                not isinstance(overlay, dict)
                or not overlay
                or any(actual.get(name) != digest for name, digest in overlay.items())
                or not isinstance(patch, str)
                or len(patch) != 64
                or any(value not in "0123456789abcdef" for value in patch)
            ):
                raise ValueError("candidate overlay identity does not match its export")
            provenance.update(
                kind="exported candidate source",
                base_commit=commit,
                overlay_files=overlay,
                patch_sha256=patch,
                source_file_count=len(declared["runtime_source_hashes"]),
                native_binary_count=len(binaries),
                native_binaries=binaries,
                binding="All exported bytes checked; base and overlay lineage are declared by the export owner. This is not an exact committed tree.",
            )
        else:
            provenance.update(
                declared_commit=commit,
                binding="All exported bytes checked; commit was verified by the export owner.",
            )
        return provenance
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=source,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        raise ValueError("a runtime without .git requires --source-manifest")
    names = (
        subprocess.check_output(["git", "ls-files", "-z"], cwd=source)
        .decode()
        .split("\0")
    )
    return {
        "kind": "checkout",
        "git_head": result.stdout.strip(),
        "files": {
            name: sha(source / name)
            for name in names
            if name and (source / name).is_file()
        },
        "binding": "Working bytes fingerprinted separately from HEAD; dirty source is not implied committed.",
    }


def identities(
    source: Path, build: Path, observer: Path, manifest: Path | None
) -> dict[str, str]:
    harness = Path(__file__).resolve().parent.parent
    runtime = runtime_provenance(source, manifest)
    files = {"runtime/" + name: digest for name, digest in runtime["files"].items()}
    paths = [
        *sorted((harness / "scripts").glob("bench_i1*.py")),
        harness / "scripts/bench-i1.py",
        harness / "scripts/bench-i1.sh",
        harness / "tests/fixtures/agents/item10-worker.py",
        harness / "tests/c/test_tui_pty.c",
        *sorted((harness / "tests/c").glob("test_tui_*.inc")),
        harness / "tests/test_bench_i1.sh",
    ]
    files.update(
        {"harness/" + str(path.relative_to(harness)): sha(path) for path in paths}
    )
    files.update(
        {
            "binary/" + name: sha(build / name)
            for name in ("hydra-core", "hydra-tui", "hydra-fleet")
        }
    )
    files["binary/observer"] = sha(observer)
    return files
