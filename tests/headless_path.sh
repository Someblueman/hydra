#!/bin/sh
# Sourced acceptance helper: retain host tools but make tmux undiscoverable.
# The caller owns and removes the supplied disposable directory.
headless_path() {
    python3 - "$1" <<'PY'
import os
import sys
from pathlib import Path
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for directory in os.environ['PATH'].split(os.pathsep):
    try:
        entries = list(Path(directory).iterdir())
    except OSError:
        continue
    for entry in entries:
        try:
            usable = entry.name != 'tmux' and entry.is_file() and os.access(entry, os.X_OK)
        except OSError:
            usable = False
        if not usable:
            continue
        destination = out / entry.name
        if not destination.exists():
            destination.symlink_to(entry.absolute())
PY
    PATH="$1"
    export PATH
    ! command -v tmux >/dev/null 2>&1
}
