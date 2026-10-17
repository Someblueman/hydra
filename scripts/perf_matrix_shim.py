"""Exec-counting PATH shims: each counted helper launch records its ancestry.

A shim is a dash script that appends one record (builtins only, no extra
processes) and then execs the real helper. Wrapped helpers also record their
exit so their wall-clock duration is known. Everything not shimmed is unknown.
"""

from __future__ import annotations

import os
import shlex
import shutil
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any

EXEC_ONLY = ("git", "tmux")
WRAPPED = ("hydra", "hydra-core", "hydra-fleet")

_CHAIN = """# Ancestor PIDs, nearest first, from /proc (builtins only; bounded depth).
_perf_chain() {
    _p=$PPID _c=$PPID _i=0
    while [ "$_i" -lt 8 ] && [ "$_p" -gt 1 ]; do
        IFS= read -r _line < "/proc/$_p/stat" 2>/dev/null || break
        _rest=${_line##*) }
        # shellcheck disable=SC2086 # positional parameters are local here
        set -- $_rest
        _p=$2 _c="$_c,$_p" _i=$((_i + 1))
    done
}
"""


def _script(name: str, real: str, log: str, wrapped: bool) -> str:
    head = "#!/bin/sh\n# perf-matrix exec shim: count this launch, then run the real helper.\n"
    body = _CHAIN + "read -r _up _rest < /proc/uptime\n_perf_chain\n"
    body += (
        "printf '%s\\tstart\\t%s\\t%s\\t%s\\t%s\\n' \"$_up\" "
        f"{shlex.quote(name)} \"$$\" \"$_c\" \"$*\" >> {shlex.quote(log)}\n"
    )
    if not wrapped:
        return head + body + f"exec {shlex.quote(real)} \"$@\"\n"
    body += f"{shlex.quote(real)} \"$@\"\n_rc=$?\nread -r _up _rest < /proc/uptime\n"
    body += (
        "printf '%s\\tend\\t%s\\t%s\\t%s\\t%s\\n' \"$_up\" "
        f"{shlex.quote(name)} \"$$\" \"$_c\" \"$_rc\" >> {shlex.quote(log)}\n"
        "exit \"$_rc\"\n"
    )
    return head + body


class Shim:
    """One shim directory plus one append-only log for a whole fixture."""

    def __init__(self, directory: Path, build: Path, hydra: Path) -> None:
        self.dir = directory
        self.dir.mkdir()
        self.log = directory / "launches.tsv"
        self.log.touch()
        self.real: dict[str, str] = {}
        self.wrappers: dict[str, Path] = {}
        for name in EXEC_ONLY:
            real = shutil.which(name)
            if real is None:
                raise RuntimeError(f"{name} is required on PATH")
            self.real[name] = real
            self._write(self.dir / name, _script(name, real, str(self.log), False))
        for name, real in (
            ("hydra", str(hydra)),
            ("hydra-core", str(build / "hydra-core")),
            ("hydra-fleet", str(build / "hydra-fleet")),
        ):
            self.real[name] = real
            wrapper = directory / "wrapped" / name
            wrapper.parent.mkdir(exist_ok=True)
            self._write(wrapper, _script(name, real, str(self.log), True))
            self.wrappers[name] = wrapper
        self.offset = calibrate_uptime()

    @staticmethod
    def _write(path: Path, text: str) -> None:
        path.write_text(text)
        path.chmod(0o755)

    def environment(self, env: dict[str, str]) -> dict[str, str]:
        return {
            **env,
            "PATH": f"{self.dir}:{env['PATH']}",
            "HYDRA_CORE": str(self.wrappers["hydra-core"]),
            "HYDRA_FLEET_BIN": str(self.wrappers["hydra-fleet"]),
            "HYDRA_ITEM10_PATH_PREFIX": str(self.dir),
        }

    def mark(self) -> int:
        """Byte offset of the log; records after it belong to a later window."""
        return self.log.stat().st_size

    def records(self, start: int, end: int | None = None) -> list[dict[str, Any]]:
        with self.log.open("rb") as stream:
            stream.seek(start)
            raw = stream.read() if end is None else stream.read(end - start)
        return parse_records(raw, self.offset["offset_ns"])

    def calibrate_overhead(self, launches: int = 12) -> dict[str, Any]:
        """Per-launch shim cost: shimmed minus direct `git --version`, same host."""
        direct, shimmed = [], []
        env = {"PATH": "/usr/bin:/bin"}
        for _ in range(launches):
            for target, bucket in ((self.real["git"], direct), (self.dir / "git", shimmed)):
                started = time.monotonic_ns()
                subprocess.run([str(target), "--version"], env=env, check=True, capture_output=True)
                bucket.append(time.monotonic_ns() - started)
        return {
            "launches": launches,
            "direct_median_ns": statistics.median(direct),
            "shimmed_median_ns": statistics.median(shimmed),
            "overhead_median_ns": statistics.median(shimmed) - statistics.median(direct),
            "overhead_max_ns": max(shimmed) - min(direct),
            "scope": "wall clock of a trivial helper under the controller; the shim adds one dash process per counted launch",
        }


def calibrate_uptime() -> dict[str, Any]:
    """Bracket /proc/uptime (10 ms resolution) against CLOCK_MONOTONIC."""
    samples = []
    for _ in range(5):
        before = time.monotonic_ns()
        uptime = float(Path("/proc/uptime").read_text().split()[0])
        after = time.monotonic_ns()
        samples.append((before, int(uptime * 1e9), after))
    before, uptime_ns, after = min(samples, key=lambda row: row[2] - row[0])
    return {
        "offset_ns": before - uptime_ns,
        "offset_bound_ns": [before - uptime_ns - 10_000_000, after - uptime_ns],
        "resolution_ns": 10_000_000,
        "scope": "same-host CLOCK_BOOTTIME to CLOCK_MONOTONIC; no suspend expected in a container",
    }


def parse_records(raw: bytes, offset_ns: int) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in raw.split(b"\n"):
        if not line:
            continue
        parts = line.decode(errors="replace").split("\t", 5)
        if len(parts) < 5:
            records.append({"malformed": line.decode(errors="replace")})
            continue
        uptime, phase, name, pid, chain = parts[:5]
        tail = parts[5] if len(parts) > 5 else ""
        try:
            record = {
                "uptime_s": float(uptime),
                "monotonic_ns": int(float(uptime) * 1e9) + offset_ns,
                "phase": phase,
                "name": name,
                "pid": int(pid),
                "chain": [int(value) for value in chain.split(",") if value],
            }
        except ValueError:
            records.append({"malformed": line.decode(errors="replace")})
            continue
        if phase == "end":
            record["exit"] = tail
        else:
            record["argv"] = tail
        records.append(record)
    return records


def _git_worktree(argv: str) -> str | None:
    words = argv.split(" ")
    for index, word in enumerate(words[:-1]):
        if word == "-C":
            return words[index + 1]
    return None


def _tmux_target(argv: str) -> tuple[str, str | None]:
    words = argv.split(" ")
    verb = next((word for word in words if word and not word.startswith("-") and os.sep not in word), "")
    target = None
    for index, word in enumerate(words[:-1]):
        if word == "-t":
            target = words[index + 1]
    return verb, target


def summarize_launches(
    records: list[dict[str, Any]],
    roles: dict[int, str],
    worktrees: dict[str, str],
    sessions: dict[str, str],
    seconds: float,
) -> dict[str, Any]:
    """Counts by helper, by attributed launcher role and per worktree."""
    by_name: dict[str, int] = {}
    by_role: dict[str, dict[str, int]] = {}
    per_worktree: dict[str, dict[str, int]] = {
        branch: {"git": 0, "tmux_capture": 0, "tmux_other": 0} for branch in worktrees.values()
    }
    durations: dict[str, list[int]] = {}
    pending: dict[tuple[str, int], int] = {}
    unattributed = malformed = 0
    for record in records:
        if "malformed" in record:
            malformed += 1
            continue
        key = (record["name"], record["pid"])
        if record["phase"] == "end":
            started = pending.pop(key, None)
            if started is not None:
                durations.setdefault(record["name"], []).append(record["monotonic_ns"] - started)
            continue
        name = record["name"]
        if name in WRAPPED:
            pending[key] = record["monotonic_ns"]
        by_name[name] = by_name.get(name, 0) + 1
        role = next((roles[pid] for pid in record["chain"] if pid in roles), None)
        if role is None:
            unattributed += 1
            role = "unattributed"
        family = role.split("/")[0]
        by_role.setdefault(family, {})
        by_role[family][name] = by_role[family].get(name, 0) + 1
        if name == "git":
            branch = worktrees.get(_git_worktree(record.get("argv", "")) or "")
            if branch is not None:
                per_worktree[branch]["git"] += 1
        elif name == "tmux":
            verb, target = _tmux_target(record.get("argv", ""))
            branch = sessions.get(target or "")
            if branch is not None:
                per_worktree[branch]["tmux_capture" if verb == "capture-pane" else "tmux_other"] += 1
    minutes = seconds / 60 if seconds else None
    return {
        "scope": "exact for shimmed helpers (git, tmux, hydra CLI, hydra-core, hydra-fleet); other utilities (awk, sed, find, ...) are not counted",
        "by_helper": by_name,
        "by_launcher_role": by_role,
        "per_worktree": per_worktree,
        "total": sum(by_name.values()),
        "per_minute": sum(by_name.values()) / minutes if minutes else None,
        "unattributed": unattributed,
        "malformed_records": malformed,
        "wrapped_durations_ns": {
            name: {
                "n": len(values),
                "min": min(values),
                "median": statistics.median(values),
                "max": max(values),
            }
            for name, values in durations.items()
        },
        "unfinished_wrapped": len(pending),
    }
