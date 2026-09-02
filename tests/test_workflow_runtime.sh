#!/bin/sh
set -eu

ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
# shellcheck disable=SC1091
. "$ROOT/lib/workflow.sh"

cat > "$TMP/workflow.yml" <<'EOF'
version: 1
id: durable-test
parallelism: 2
steps:
  - id: effect
    kind: exec
    retry: 0
    idempotent: false
    args:
      argv: [true]
  - id: join
    kind: exec
    needs: [effect]
    args:
      argv: [true]
EOF

actual="$(workflow_parse "$TMP/workflow.yml" runtime)"
printf '%s\n' "$actual" | grep '^workflow[[:space:]]durable-test[[:space:]]2[[:space:]]' >/dev/null
printf '%s\n' "$actual" | grep '^step[[:space:]]effect[[:space:]]exec[[:space:]]-[[:space:]]0[[:space:]]false' >/dev/null
printf '%s\n' "$actual" | grep '^step[[:space:]]join[[:space:]]exec[[:space:]]effect[[:space:]]0[[:space:]]true' >/dev/null
printf 'Passed: 3\nFailed: 0\n'
