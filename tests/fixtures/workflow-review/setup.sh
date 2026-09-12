#!/bin/sh
# Real local artifact and pending request fixture, created through public CLI.
: "${root:?}"
fixture="$(mktemp -d)"
repo="$fixture/repo"
native="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
[ -x "$native" ] || exit 1
# shellcheck source=/dev/null
. "$root/tests/fixtures/workflow-review/helpers.sh"
review_fixture_environment
mkdir -p "$repo"
git -C "$repo" init -q --object-format="${HYDRA_REVIEW_OBJECT_FORMAT:-sha1}"
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name test
printf '%s\n' base > "$repo/tracked"
git -C "$repo" add tracked && git -C "$repo" commit -qm base
export HYDRA_HOME="$fixture/home"
export HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
(cd "$repo" && "$root/bin/hydra" init --no-agent --trust >/dev/null && "$root/bin/hydra" spawn review-worker --no-agent >/dev/null)
printf '%s\n' '{"schema_version":1,"inputs":{},"steps":{"produce":{"outputs":{"answer":{"path":"answer.txt","type":"file","max_bytes":128}}}}}' > "$fixture/data.json"
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' 'set -eu' 'printf sealed > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.txt"' > "$fixture/produce.sh"
chmod +x "$fixture/produce.sh"
cat > "$fixture/flow.yml" <<EOF
version: 1
id: review-public
data: data.json
resources:
  disk_mb: 1
steps:
  - id: produce
    kind: exec
    idempotent: false
    args:
      head: review-worker
      argv: [sh, $fixture/produce.sh]
  - id: approval
    kind: approval-wait
    needs: [produce]
    idempotent: false
    args:
      head: review-worker
      name: review
      message: Review output
EOF
set +e
(cd "$repo" && "$root/bin/hydra" workflow run "$fixture/flow.yml" > "$fixture/run.out" 2> "$fixture/run.err")
run_status=$?
set -e
[ "$run_status" -eq 3 ] || { cat "$fixture/run.err" >&2; exit 1; }
run="$(sed -n '1p' "$fixture/run.out")"
project="$(find "$HYDRA_STATE_V2_ROOT/projects" -mindepth 1 -maxdepth 1 -type d -name 'project_*' -exec basename {} \; | sed -n '1p')"
attention="$fixture/attention.json"
"$native" workflow-data attention "$project" > "$attention"
row="$(jq -c --arg run "$run" '.data.items[]|select(.kind=="result" and .run_id==$run and .step_id=="produce" and .attempt_id=="attempt-1")' "$attention")"
[ -n "$row" ]
revision="$(printf '%s' "$row" | jq -r '.revision')"
printf '%s' "$revision" > "$fixture/revision"
digest="$(shasum -a 256 "$fixture/revision" | awk '{print $1}')"
export digest
