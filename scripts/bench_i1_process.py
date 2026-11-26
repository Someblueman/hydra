"""Direct owned-PID samples, with explicit units and incomplete subtree scope."""

from __future__ import annotations

import ctypes
import os
import platform
import time
from pathlib import Path
from typing import Any

_V4_FIELDS = [
    "user_time",
    "system_time",
    "pkg_idle_wkups",
    "interrupt_wkups",
    "pageins",
    "wired_size",
    "resident_size",
    "phys_footprint",
    "proc_start_abstime",
    "proc_exit_abstime",
    "child_user_time",
    "child_system_time",
    "child_pkg_idle_wkups",
    "child_interrupt_wkups",
    "child_pageins",
    "child_elapsed_abstime",
    "diskio_bytesread",
    "diskio_byteswritten",
    "cpu_time_qos_default",
    "cpu_time_qos_maintenance",
    "cpu_time_qos_background",
    "cpu_time_qos_utility",
    "cpu_time_qos_legacy",
    "cpu_time_qos_user_initiated",
    "cpu_time_qos_user_interactive",
    "billed_system_time",
    "serviced_system_time",
    "logical_writes",
    "lifetime_max_phys_footprint",
    "instructions",
    "cycles",
    "billed_energy",
    "serviced_energy",
    "interval_max_phys_footprint",
    "runnable_time",
]


class _Usage(ctypes.Structure):
    _fields_ = [("uuid", ctypes.c_uint8 * 16)] + [
        (name, ctypes.c_uint64) for name in _V4_FIELDS
    ]


class _Timebase(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


class ProcessCounters:
    def __init__(self) -> None:
        self.system = platform.system()
        self.lib: Any = None
        self.timebase = _Timebase()
        if self.system == "Darwin":
            self.lib = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
            self.lib.proc_pid_rusage.argtypes = [
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_void_p,
            ]
            self.lib.proc_pid_rusage.restype = ctypes.c_int
            self.lib.proc_pidinfo.argtypes = [
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_uint64,
                ctypes.c_void_p,
                ctypes.c_int,
            ]
            self.lib.proc_pidinfo.restype = ctypes.c_int
            self.lib.proc_listchildpids.argtypes = [
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.c_int,
            ]
            self.lib.proc_listchildpids.restype = ctypes.c_int
            if (
                self.lib.mach_timebase_info(ctypes.byref(self.timebase))
                or not self.timebase.denom
            ):
                raise RuntimeError("Mac clock timebase unavailable")

    def sample(self, pid: int) -> dict[str, Any]:
        record: dict[str, Any] = {
            "pid": pid,
            "sampled_ns": time.monotonic_ns(),
            "scope": "direct process",
        }
        try:
            record.update(
                self._mac(pid) if self.system == "Darwin" else self._linux(pid)
            )
        except (OSError, ValueError, IndexError) as error:
            record.update(available=False, error=str(error))
        record["sample_end_ns"] = time.monotonic_ns()
        return record

    def _mac(self, pid: int) -> dict[str, Any]:
        usage = _Usage()
        if self.lib.proc_pid_rusage(pid, 4, ctypes.byref(usage)):
            raise OSError(ctypes.get_errno(), "proc_pid_rusage")
        fds = (ctypes.c_uint64 * 4096)()
        size = self.lib.proc_pidinfo(pid, 1, 0, fds, ctypes.sizeof(fds))
        return {
            "available": True,
            "identity_start": usage.proc_start_abstime,
            "cpu_ticks": usage.user_time + usage.system_time,
            "cpu_tick_ns_numer": self.timebase.numer,
            "cpu_tick_ns_denom": self.timebase.denom,
            "resident_bytes": usage.resident_size,
            "footprint_bytes": usage.phys_footprint,
            "peak_footprint_bytes": usage.lifetime_max_phys_footprint,
            "interrupt_wakeups": usage.interrupt_wkups,
            "package_idle_wakeups": usage.pkg_idle_wkups,
            "disk_read_bytes": usage.diskio_bytesread,
            "disk_write_bytes": usage.diskio_byteswritten,
            "instructions": usage.instructions,
            "cycles": usage.cycles,
            "fds": size // 8
            if 0 < size < ctypes.sizeof(fds) and size % 8 == 0
            else None,
        }

    def _linux(self, pid: int) -> dict[str, Any]:
        root = Path(f"/proc/{pid}")
        stat = (root / "stat").read_text()
        values = stat[stat.rfind(")") + 2 :].split()
        status = dict(
            line.split(":", 1)
            for line in (root / "status").read_text().splitlines()
            if ":" in line
        )
        io: dict[str, int] = {}
        try:
            io = {
                key: int(value)
                for key, value in (
                    line.split(":", 1)
                    for line in (root / "io").read_text().splitlines()
                )
            }
        except OSError:
            pass
        try:
            fds: int | None = len(list((root / "fd").iterdir()))
        except OSError:
            fds = None
        return {
            "available": True,
            "identity_start": int(values[19]),
            "cpu_ticks": int(values[11]) + int(values[12]),
            "cpu_tick_ns_numer": 1_000_000_000,
            "cpu_tick_ns_denom": os.sysconf("SC_CLK_TCK"),
            "resident_bytes": int(values[21]) * os.sysconf("SC_PAGE_SIZE"),
            "peak_rss_bytes": int(status["VmHWM"].split()[0]) * 1024
            if "VmHWM" in status
            else None,
            "interrupt_wakeups": None,
            "package_idle_wakeups": None,
            "disk_read_bytes": io.get("read_bytes"),
            "disk_write_bytes": io.get("write_bytes"),
            "instructions": None,
            "cycles": None,
            "fds": fds,
        }

    def children(self, pid: int) -> list[int]:
        if self.system == "Darwin":
            children = (ctypes.c_int * 4096)()
            count = self.lib.proc_listchildpids(pid, children, ctypes.sizeof(children))
            if count <= 0 or count >= len(children):
                return []
            return list(children[:count])
        try:
            return [
                int(child)
                for child in Path(f"/proc/{pid}/task/{pid}/children")
                .read_text()
                .split()
            ]
        except OSError:
            return []

    def tree(self, roots: dict[int, str]) -> dict[str, dict[str, Any]]:
        result: dict[str, dict[str, Any]] = {}
        pending = list(roots.items())
        while pending and len(result) < 4096:
            pid, role = pending.pop()
            if str(pid) in result:
                continue
            role = roots.get(pid, role)
            result[str(pid)] = {**self.sample(pid), "role": role}
            pending.extend((child, role + "/child") for child in self.children(pid))
        return result


def cpu_seconds(before: dict[str, Any], after: dict[str, Any]) -> float | None:
    if (
        not before.get("available")
        or not after.get("available")
        or before["identity_start"] != after["identity_start"]
    ):
        return None
    ticks = after["cpu_ticks"] - before["cpu_ticks"]
    if ticks < 0:
        return None
    return (
        ticks * after["cpu_tick_ns_numer"] / after["cpu_tick_ns_denom"] / 1_000_000_000
    )


def _counter_delta(
    first: dict[str, Any], last: dict[str, Any], name: str
) -> int | None:
    before, after = first.get(name), last.get(name)
    return (
        after - before
        if before is not None and after is not None and after >= before
        else None
    )


def summarize_samples(samples: list[dict[str, Any]]) -> dict[str, Any]:
    """Keep process birth identities, actual sample spans and partial coverage explicit."""
    episodes: dict[str, list[dict[str, Any]]] = {}
    for sample in samples:
        for row in sample["processes"].values():
            if row.get("available"):
                episodes.setdefault(f"{row['pid']}:{row['identity_start']}", []).append(
                    row
                )
    processes = []
    for rows in episodes.values():
        first, last = rows[0], rows[-1]
        elapsed = (last["sampled_ns"] - first["sampled_ns"]) / 1e9
        seconds = cpu_seconds(first, last) if len(rows) > 1 else None

        complete = (
            samples[0]["processes"].get(str(first["pid"]), {}).get("identity_start")
            == first["identity_start"]
            and samples[-1]["processes"]
            .get(str(first["pid"]), {})
            .get("identity_start")
            == first["identity_start"]
        )
        processes.append(
            {
                "pid": first["pid"],
                "identity_start": first["identity_start"],
                "role": first["role"],
                "samples": len(rows),
                "span_seconds": elapsed,
                "present_at_both_window_ends": complete,
                "cpu_seconds": seconds,
                "cpu_percent_one_core": 100 * seconds / elapsed
                if seconds is not None and elapsed
                else None,
                "cpu_percent_machine": 100 * seconds / elapsed / (os.cpu_count() or 1)
                if seconds is not None and elapsed
                else None,
                "sampled_max_resident_bytes": max(
                    row["resident_bytes"] for row in rows
                ),
                "lifetime_peak_footprint_bytes": last.get("peak_footprint_bytes"),
                "lifetime_peak_rss_bytes": last.get("peak_rss_bytes"),
                "sampled_max_fds": max(
                    (row["fds"] for row in rows if row.get("fds") is not None),
                    default=None,
                ),
                "interrupt_wakeups": _counter_delta(first, last, "interrupt_wakeups"),
                "package_idle_wakeups": _counter_delta(
                    first, last, "package_idle_wakeups"
                ),
                "disk_read_bytes": _counter_delta(first, last, "disk_read_bytes"),
                "disk_write_bytes": _counter_delta(first, last, "disk_write_bytes"),
                "instructions": _counter_delta(first, last, "instructions"),
                "cycles": _counter_delta(first, last, "cycles"),
            }
        )
    return {
        "processes": processes,
        "normalization": "100 percent one-core CPU equals one fully busy logical CPU",
        "logical_cpus": os.cpu_count(),
        "sampled_descendants": "Lower bound: short-lived helpers can be absent from every sample.",
        "full_process_launch_count": None,
        "full_subtree_cpu_seconds": None,
        "filesystem_operations": None,
        "worktree_scans": None,
        "native_redraws": None,
        "source_invalidations": None,
        "clock_interrupt_trace": None,
        "total_system_power": None,
        "full_subtree_peak_memory": None,
    }
