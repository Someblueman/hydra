#!/usr/bin/env python3
"""Visual review without manual runs: capture, render, lint, contact sheet.

Installs this checkout into a private prefix (as the usability runner under scripts/usability does),
drives the installed native TUI over a PTY through the states in flows.py at
80x24 and 140x40 in the dark, light and no-colour themes, keeps the raw byte
stream of every state, renders each through the product's own terminal
interpreter (termviz-cells) to HTML and SVG with exact colours, runs the
layout lint and writes one contact sheet: OUTPUT/index.html.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import flows  # noqa: E402
import lint  # noqa: E402
import render  # noqa: E402
import report  # noqa: E402
from driver import Driver  # noqa: E402
from usability.journey import run as run_command, tree  # noqa: E402

ROOT = HERE.parent.parent
SIZES = ((80, 24), (140, 40))
THEMES = ("dark", "light", "nocolor")


def variant_env(theme: str) -> dict[str, str]:
    if theme == "nocolor":
        return {"HYDRA_TUI_THEME": "dark", "NO_COLOR": "1"}
    return {"HYDRA_TUI_THEME": theme}


class Capture:
    def __init__(self, output: Path, prefix: Path, cells: Path) -> None:
        self.output = output
        self.prefix = prefix
        self.cells = cells
        self.captures = output / "captures"
        self.captures.mkdir(parents=True, exist_ok=True)
        self.manifest: list[dict] = []
        self.failures: list[dict] = []

    def base_env(self, base: Path, theme: str) -> dict[str, str]:
        env = {k: v for k, v in os.environ.items() if not k.startswith(("HYDRA_", "TMUX", "NO_COLOR"))}
        env.update(
            HOME=str(base / "home"),
            HYDRA_HOME=str(base / "hydra-home"),
            TMPDIR=str(base),
            TERM="xterm-256color",
            LANG="C.UTF-8",
            LC_ALL="C.UTF-8",
            PATH=str(self.prefix / "bin") + os.pathsep + str(ROOT / "tests/fixtures/tui/fake-bin") + os.pathsep + env.get("PATH", ""),
        )
        env.update(variant_env(theme))
        for key in ("HOME", "HYDRA_HOME"):
            Path(env[key]).mkdir(mode=0o700, exist_ok=True)
        return env

    def flow_env(self, flow: str, state_dir: Path, options: dict) -> dict[str, str]:
        if flow.startswith("hosts"):
            (state_dir / "calls.log").write_text("")
            for flag in options.get("flags", []):
                (state_dir / flag).write_text("")
            return {
                "HYDRA_BIN_CMD": str(ROOT / "tests/fixtures/tui/setup/fake-hydra-setup.sh"),
                "HYDRA_SETUP_FAKE": str(state_dir),
                "HYDRA_TUI_FIXTURE": str(ROOT / "tests/fixtures/tui/setup/empty.tsv"),
            }
        (state_dir / "workflow").write_text(str(flows.FIXTURES / "workflow-runs-running.tsv"))
        return {
            "HYDRA_BIN_CMD": str(HERE / "fake-hydra.sh"),
            "VR_STATE": str(state_dir),
            "HYDRA_TUI_FIXTURE": str(flows.FIXTURES / "native-runs.tsv"),
        }

    def session(self, flow: str, theme: str, columns: int, rows: int) -> None:
        function, options = flows.FLOWS[flow]
        variant = f"{theme}-{columns}x{rows}"
        base = Path(tempfile.mkdtemp(prefix="hydra-vr.", dir="/tmp"))
        state_dir = base / "state"
        state_dir.mkdir(mode=0o700)
        env = self.base_env(base, theme)
        env.update(self.flow_env(flow, state_dir, options))
        argv = [str(self.prefix / "libexec/hydra/hydra-tui")]
        if flow == "fleet":
            argv.append("--fleet")
        driver = Driver(argv, self.cells, base, env, columns, rows)

        def record(flow_name: str, state: str, group: str, markers: list[str], note: str) -> None:
            identifier = f"{flow_name}--{state}--{variant}"
            raw = driver.snapshot()
            (self.captures / f"{identifier}.raw").write_bytes(raw)
            screen = driver.cells()
            (self.captures / f"{identifier}.json").write_text(json.dumps(screen, separators=(",", ":")) + "\n")
            (self.captures / f"{identifier}.txt").write_text(render.to_text(screen))
            self.manifest.append({
                "id": identifier, "flow": flow_name, "state": state, "theme": theme,
                "columns": columns, "rows": rows, "group": group, "markers": markers, "note": note,
                "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            })

        try:
            function(flows.Session(driver, flow, state_dir, record))
        except (TimeoutError, RuntimeError, OSError) as error:
            self.failures.append({"flow": flow, "variant": variant, "error": str(error)})
            print(f"FAILED {flow} {variant}: {str(error).splitlines()[0]}", file=sys.stderr)
        finally:
            exit_keys = b"\x1b\x1bq" if not flow.startswith("hosts") else b"\x1b\x1b\x1bq"
            driver.close(exit_keys)
            shutil.rmtree(base, ignore_errors=True)


def install(output: Path) -> Path:
    prefix = output / "installed"
    if prefix.exists():
        shutil.rmtree(prefix)
    env = {**os.environ, "PREFIX": str(prefix), "HYDRA_INSTALL_CORE": "required",
           "HYDRA_INSTALL_TUI": "required", "HYDRA_INSTALL_FLEET": "required", "DESTDIR": ""}
    (output / "install.log").write_text(run_command(["sh", "install.sh"], ROOT, env))
    return prefix


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cells", type=Path, required=True, help="the termviz-cells executable")
    parser.add_argument("--baseline", type=Path, help="an earlier output directory for before/after columns")
    parser.add_argument("--flow", choices=tuple(flows.FLOWS), nargs="+", default=list(flows.FLOWS))
    parser.add_argument("--theme", choices=THEMES, nargs="+", default=list(THEMES))
    parser.add_argument("--size", nargs="+", default=[f"{c}x{r}" for c, r in SIZES])
    parser.add_argument("--keep-prefix", action="store_true", help="reuse OUTPUT/installed when present")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and not args.keep_prefix:
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    prefix = output / "installed"
    if not (args.keep_prefix and (prefix / "libexec/hydra/hydra-tui").exists()):
        prefix = install(output)
    capture = Capture(output, prefix, args.cells.resolve())
    sizes = [tuple(int(v) for v in size.split("x")) for size in args.size]
    previous = output / "manifest.json"
    if args.keep_prefix and previous.exists():
        # A partial re-run keeps the captures of every combination it does not repeat.
        rerun = {(f, t, c, r) for f in args.flow for t in args.theme for c, r in sizes}
        kept = json.loads(previous.read_text())
        capture.manifest = [e for e in kept["captures"] if (e["flow"], e["theme"], e["columns"], e["rows"]) not in rerun]
        capture.failures = [f for f in kept.get("failures", [])
                            if (f["flow"], *f["variant"].replace("x", "-").split("-")) not in {(a, b, str(c), str(d)) for a, b, c, d in rerun}]
    for flow in args.flow:
        for theme in args.theme:
            for columns, rows in sizes:
                try:
                    capture.session(flow, theme, columns, rows)
                except Exception as error:  # noqa: BLE001 - recorded, never hidden
                    capture.failures.append({"flow": flow, "variant": f"{theme}-{columns}x{rows}",
                                             "error": "".join(traceback.format_exception(error))})
                print(f"captured {flow} {theme} {columns}x{rows}", flush=True)
    manifest = {
        "commit": run_command(["git", "rev-parse", "HEAD"], ROOT, dict(os.environ)),
        "installed_hashes": tree(prefix),
        "captures": capture.manifest,
        "failures": capture.failures,
        "seconds": round(time.monotonic() - started, 1),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    findings = lint.lint_directory(output)
    (output / "lint.json").write_text(json.dumps(findings, indent=2) + "\n")
    report.write(output, manifest, findings, args.baseline.resolve() if args.baseline else None)
    print(output / "index.html")
    failed = [f for f in findings["findings"] if not f.get("known")]
    if capture.failures:
        print(f"{len(capture.failures)} capture session(s) failed; see manifest.json", file=sys.stderr)
    print(f"lint: {len(findings['findings'])} finding(s), {len(failed)} new", file=sys.stderr)
    return 1 if capture.failures or failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
