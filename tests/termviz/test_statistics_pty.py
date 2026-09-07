"""Actual native UI input, filter reconciliation, drill-down and restoration."""
from pathlib import Path
import os
import tempfile

from pty_support import Session

# The execution host may set NO_COLOR for logs. Exercise the explicit dark theme
# in this child-only test process; the existing native suite covers NO_COLOR.
os.environ.pop("NO_COLOR", None)

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("BUILD_DIR", ROOT / "build")).resolve()
EVIDENCE = ROOT / "build/statistics-evidence"
EVIDENCE.mkdir(parents=True, exist_ok=True)
ARGV = [str(BUILD / "hydra-tui"), "--hydra", str(ROOT / "tests/fixtures/tui/fake-hydra.sh"), "--theme", "dark"]


def local_statistics() -> None:
    with tempfile.TemporaryDirectory(prefix="hydra-statistics-") as folder:
        failure = Path(folder) / "fail"
        s = Session(ARGV, 140, 40, env={"HYDRA_TEST_STATS_FAIL_FILE": str(failure)})
        try:
            s.until("HYDRA WORKSPACE")
            s.send("j\tjj\tjjj")
            s.pump(.2)
            assert "scroll 2" in s.screen.text() and "scroll 3" in s.screen.text()
            s.send("a")
            s.until("FAKE SWITCH feature-stale")
            s.until("Press Enter to return")
            s.send("\r")
            s.until("HYDRA WORKSPACE")
            s.send("D")
            s.until("HYDRA / D STATISTICS")
            s.until("Latest attempt mean 95.0s / n=6 / max 120s")
            assert "Attempts 8/11" in s.screen.text()
            s.screen.save(EVIDENCE / "statistics-140x40.html")
            s.send("T")
            s.until("Local project / 24 hours")
            assert "1 date-excluded" in s.screen.text()
            s.until("1/4 / Enter evidence")
            s.send("0/")
            s.until("Find workflow, run or project:")
            s.send("recovery\r")
            s.until("Local project / all recorded / recovery")
            s.until("1/2 / Enter evidence")
            s.send("\r")
            s.until("RUN EVIDENCE")
            s.until("run_cccccccccccccccccccc")
            assert "120" in s.screen.text() and "failed" in s.screen.text()
            s.send("\x1b")
            s.until("RUNS / newest")
            s.send("D")
            s.until("HYDRA WORKSPACE")
            assert "scroll 2" in s.screen.text() and "scroll 3" in s.screen.text()
            s.send("D")
            s.until("Local project / all recorded / recovery")
            s.send("0/")
            s.until("Find workflow, run or project:")
            s.send("visualization\r")
            s.until("1/1 / Enter evidence")
            s.send("g")
            s.until("[Workflows]")
            s.until("prepare")
            s.send("\x1b")
            s.until("HYDRA / D STATISTICS")
            s.until("Local project / all recorded / visualization")
            s.send("0")
            s.until("1/8 / Enter evidence")
            s.send("\x1b[<0;40;23M")
            s.pump(.15)
            s.until("3/8 / Enter evidence")
            s.send("!")
            s.until("all work / attention")
            s.send("0")
            failure.touch()
            s.send("r")
            s.until("STATISTICS / STALE")
            assert "95.0s" in s.screen.text(), "Failed refresh lost last good sample"
            failure.unlink()
            s.send("r")
            s.pump(.25)
            assert "STATISTICS / STALE" not in s.screen.text()
            for cols, rows in [(80, 24), (40, 10), (140, 40)]:
                s.resize(cols, rows)
                s.until("D STATISTICS")
                assert "q quit" in s.screen.text() and s.screen.overflow == 0
                assert s.screen.clears >= 1, "Resizing must invalidate presentation"
                s.screen.save(EVIDENCE / f"statistics-{cols}x{rows}.html")
            s.close(b"q")
        except BaseException:
            s.abort()
            raise
    print("PASS statistics PTY: scope, time range, evidence/graph drill-down, preserved workspace, mouse, stale recovery, three sizes and termios")


def fleet_statistics() -> None:
    s = Session(ARGV + ["--fleet", "--view", "statistics"], 140, 40)
    try:
        s.until("HOST STATISTICS")
        s.until("2 responded   1 failed   1 reported heads")
        assert "offline" in s.screen.text() and "-- heads" in s.screen.text()
        s.send("\r")
        s.until("/work/project")
        s.send("\x1b")
        s.pump(.05)
        s.send("/")
        s.until("Find host:")
        s.send("offline\r")
        s.until("0 responded   1 failed   -- reported heads")
        assert "Failed hosts have unknown head counts" in s.screen.text()
        s.screen.save(EVIDENCE / "statistics-fleet-140x40.html")
        s.send("0")
        s.resize(80, 14)
        s.send("k")
        s.pump(.1)
        assert "> empty" in s.screen.text(), "Fleet selection must scroll into view"
        s.send("j")
        s.until("> offline")
        s.pump(.1)
        s.resize(40, 10)
        s.until("3/3 offline")
        assert "q quit" in s.screen.text() and s.screen.overflow == 0
        s.send("\r")
        s.until("No head evidence")
        s.close(b"q")
    except BaseException:
        s.abort()
        raise
    print("PASS fleet statistics: responded/failed/empty host separation, known counts, host filtering and drill-down")


if __name__ == "__main__":
    local_statistics()
    fleet_statistics()
