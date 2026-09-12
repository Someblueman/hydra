"""Public initialization with no inherited HOME or user configuration."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

from bench_i1_fixture import Fixture


def main() -> None:
    assert "HOME" not in os.environ, "run this control with env -i"
    source, build, output = (Path(value).resolve() for value in sys.argv[1:])
    output.mkdir()
    fixture = Fixture(source, build, output, 1, "headless")
    fixture.repo.mkdir()
    fixture.home.mkdir()
    try:
        assert fixture.env["HOME"] == str(fixture.home)
        assert fixture.env["HYDRA_HOME"] == str(fixture.home)
        assert fixture.env["GIT_CONFIG_GLOBAL"] == "/dev/null"
        fixture.command(["git", "init", "-q"])
        fixture.command(["git", "config", "user.name", "Item10"])
        fixture.command(["git", "config", "user.email", "item10@example.invalid"])
        fixture.command(["git", "commit", "--allow-empty", "-qm", "private-home"])
        initialized = fixture.command(
            ["dash", str(fixture.hydra), "init", "--no-agent", "--trust"]
        )
        assert initialized.returncode == 0
        assert not list((fixture.home / "state/v2/projects").glob("*/heads/head_*"))
        (output / "environment.json").write_text(
            json.dumps(
                {"parent_home": None, "child_environment": fixture.env}, indent=2
            )
            + "\n"
        )
    finally:
        cleanup = fixture.cleanup()
    assert cleanup["cleanup_ok"], cleanup
    print(
        "PASS private fixture HOME: minimal-parent environment, dash public init, no heads, clean cleanup"
    )


if __name__ == "__main__":
    main()
