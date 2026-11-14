#!/bin/sh
# Deterministic provider fixture whose only behaviour is a missing login.
# Installed as `codex` on a private PATH so Hydra reaches it through the
# built-in profile's public launch; it never claims live-provider coverage.
case "${1:-}" in
    --version) echo "codex-cli 0.0.0-acceptance-noauth-fixture"; exit 0 ;;
    --help) echo "Usage: codex [--version] [--help]"; echo "Acceptance fixture: reports a missing login."; exit 0 ;;
esac
echo "Not logged in. Run: codex login" >&2
exit 1
