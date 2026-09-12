#!/bin/sh
set -eu
exec python3 "$(dirname "$0")/bench-i1.py" "$@"
