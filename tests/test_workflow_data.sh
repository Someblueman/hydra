#!/bin/sh
# Public CLI artifact handoff and rejection boundaries; requires build-fleet.
set -u
HYDRA_BIN="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)/bin/hydra"
ROOT="$(mktemp -d)"
export HYDRA_HOME="$ROOT/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"
test_count=0 pass_count=0 fail_count=0
cleanup() {
    if [ -d "$ROOT/repo" ]; then
        (cd "$ROOT/repo" && "$HYDRA_BIN" kill data-producer --force >/dev/null 2>&1) || true
        (cd "$ROOT/repo" && "$HYDRA_BIN" kill data-consumer --force >/dev/null 2>&1) || true
    fi
    rm -rf "$ROOT"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$ROOT/repo"
cd "$ROOT/repo" || exit 1
git init -q
git config user.name Test
git config user.email test@example.com
printf 'base\n' > tracked
git add tracked && git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null
"$HYDRA_BIN" spawn data-producer --no-agent >/dev/null
"$HYDRA_BIN" spawn data-consumer --no-agent >/dev/null
printf '{"question":42}\n' > request.json
cat > "$ROOT/data.json" <<'JSON'
{"schema_version":1,"inputs":{"request":{"path":"request.json","type":"object","max_bytes":128}},"steps":{"produce":{"inputs":{"request":{"input":"request"}},"outputs":{"answer":{"path":"answer.bin","type":"file","max_bytes":128},"report":{"path":"report.json","type":"object","max_bytes":128}}},"consume":{"inputs":{"answer":{"step":"produce","output":"answer"},"report":{"step":"produce","output":"report"}}}}}
JSON
cat > "$ROOT/produce.sh" <<'SCRIPT'
#!/bin/sh
set -eu
grep -q '"question":42' "$HYDRA_WORKFLOW_INPUTS_DIR/request"
printf '\000\377exact artifact\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.bin"
printf '{"answer":42}\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.json"
SCRIPT
cat > "$ROOT/consume.sh" <<'SCRIPT'
#!/bin/sh
set -eu
printf '\000\377exact artifact\n' > "$1/expected"
cmp "$1/expected" "$HYDRA_WORKFLOW_INPUTS_DIR/answer"
grep -q '"answer":42' "$HYDRA_WORKFLOW_INPUTS_DIR/report"
printf 'received\n' >> "$1/consumed"
SCRIPT
cat > "$ROOT/flow.yml" <<EOF2
version: 1
id: data-handoff
data: data.json
resources:
  disk_mb: 1
steps:
  - id: produce
    kind: exec
    idempotent: false
    args:
      head: data-producer
      argv: [sh, $ROOT/produce.sh]
  - id: consume
    kind: exec
    needs: [produce]
    idempotent: false
    args:
      head: data-consumer
      argv: [sh, $ROOT/consume.sh, $ROOT]
EOF2
"$HYDRA_BIN" workflow run "$ROOT/flow.yml" > "$ROOT/run.out" 2> "$ROOT/run.err"
assert_success $? "two supervised workers exchange exact binary and structured artifacts"
assert_equal received "$(cat "$ROOT/consumed" 2>/dev/null)" "consumer executes with named input files"
run="$(sed -n '1p' "$ROOT/run.out")"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
if [ -f "$run_dir/steps/produce/attempt-1/outputs.json" ]; then receipt_status=0; else receipt_status=1; fi
assert_success "$receipt_status" "producer publishes a durable output receipt"

# Source inputs are checked before a run is published.
printf '[42]\n' > request.json
"$HYDRA_BIN" workflow run "$ROOT/flow.yml" >/dev/null 2>&1
assert_failure $? "wrong structured input type fails preflight"
printf '{"question":42}\n' > request.json
mv request.json original.json
ln -s original.json request.json
"$HYDRA_BIN" workflow run "$ROOT/flow.yml" >/dev/null 2>&1
assert_failure $? "symlinked input files are refused"
rm request.json
mv original.json request.json

# Required output absence blocks dependents.
printf '#!/bin/sh\nexit 0\n' > "$ROOT/produce.sh"
"$HYDRA_BIN" workflow run "$ROOT/flow.yml" > "$ROOT/missing.out" 2>&1
assert_failure $? "exit zero without required outputs fails the producer"
assert_equal 1 "$(wc -l < "$ROOT/consumed" | tr -d ' ')" "missing outputs never release the consumer"

# Data references must agree with the finite dependency graph.
sed 's/needs: \[produce\]/needs: []/' "$ROOT/flow.yml" > "$ROOT/no-edge.yml"
"$HYDRA_BIN" workflow validate "$ROOT/no-edge.yml" >/dev/null 2>&1
assert_failure $? "artifact references require an explicit producer dependency"
cp "$ROOT/data.json" "$ROOT/data-good.json"
sed 's/answer.bin/..\/escape/' "$ROOT/data-good.json" > "$ROOT/data.json"
"$HYDRA_BIN" workflow validate "$ROOT/flow.yml" >/dev/null 2>&1
assert_failure $? "output path traversal fails validation"
cp "$ROOT/data-good.json" "$ROOT/data.json"

# Changed sealed artifacts block work before dispatch, even after a completed step.
cat > "$ROOT/produce.sh" <<'SCRIPT'
#!/bin/sh
set -eu
printf '\000\377exact artifact\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.bin"
printf '{"answer":42}\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/report.json"
SCRIPT
cat > "$ROOT/pause.sh" <<'SCRIPT'
#!/bin/sh
set -eu
: > "$1/paused"
n=0
while [ ! -f "$1/release" ] && [ "$n" -lt 200 ]; do sleep 0.1; n=$((n + 1)); done
[ -f "$1/release" ]
SCRIPT
sed 's/needs: \[produce\]/needs: [produce, pause]/' "$ROOT/flow.yml" > "$ROOT/tamper.yml"
cat >> "$ROOT/tamper.yml" <<EOF2
  - id: pause
    kind: exec
    needs: [produce]
    idempotent: true
    args:
      head: data-producer
      argv: [sh, $ROOT/pause.sh, $ROOT]
EOF2
"$HYDRA_BIN" workflow run "$ROOT/tamper.yml" > "$ROOT/tamper.out" 2>&1 &
runner=$!
n=0
while [ ! -f "$ROOT/paused" ] && [ "$n" -lt 200 ]; do sleep 0.1; n=$((n + 1)); done
if [ ! -f "$ROOT/paused" ]; then cat "$ROOT/tamper.out" >&2; exit 1; fi
run="$(sed -n '1p' "$ROOT/tamper.out")"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print)"
printf '{"answer":99}\n' > "$run_dir/steps/produce/attempt-1/artifacts/report"
: > "$ROOT/release"
wait "$runner"
assert_failure $? "tampered sealed output fails dependent input verification"
assert_equal 1 "$(wc -l < "$ROOT/consumed" | tr -d ' ')" "changed outputs never reach the consumer command"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
