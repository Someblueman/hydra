#!/bin/sh
# Real public task/result bundles through a local-only fake SSH transport.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
BUILD_DIR="$(dirname "$HYDRA_FLEET_BIN")"
HYDRA_HOME="$fixture/client"
HYDRA_REVIEW_FIXTURE="$fixture"
export HYDRA_FLEET_BIN BUILD_DIR HYDRA_HOME HYDRA_REVIEW_FIXTURE
(cd "$root" && make -s quality-c-flags | xargs "${CC:-cc}" "$root/tests/fixtures/fleet-review/projection-cases.c" "$BUILD_DIR/libhydra-fleet.a" "$(pkg-config --variable=libdir json-c)/libjson-c.a" -lm -o "$BUILD_DIR/test-fleet-review-projection")
# shellcheck source=/dev/null
. "$root/tests/workflow_task_cleanup.sh"
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
cleanup() {
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ] && [ "$test_code" = 0 ]; then
        fixture_lock wait "$fixture/host" || return 1
    else workflow_task_fixture_quiesce "$fixture/host" || return 1; fi
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then
        printf 'Preserved Fleet review fixture: %s\n' "$fixture" >&2
    else rm -rf "$fixture"; fi
}
test_code=0
trap 'test_code=$?; cleanup || test_code=1; exit "$test_code"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
mkdir -p "$fixture/source/.hydra/workflows" "$fixture/receiver" "$fixture/bin"
cp "$root/tests/fixtures/fleet-review/ssh.sh" "$fixture/bin/ssh"
chmod +x "$fixture/bin/ssh"
PATH="$fixture/bin:$PATH"
export PATH HYDRA_SKIP_AI=1 HYDRA_NONINTERACTIVE=1
for repo in source receiver; do
    git -C "$fixture/$repo" init -q
    git -C "$fixture/$repo" config user.name Review
    git -C "$fixture/$repo" config user.email review@example.invalid
done
cp "$root/tests/fixtures/fleet-review/workflow.yml" "$fixture/source/.hydra/workflows/review.yml"
cp "$root/tests/fixtures/fleet-review/task-work.sh" "$fixture/source/task-work.sh"
git -C "$fixture/source" add .
git -C "$fixture/source" -c commit.gpgSign=false commit -qm source
git -C "$fixture/receiver" -c commit.gpgSign=false commit --allow-empty -qm receiver
(cd "$fixture/receiver" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" init --no-agent --json) > "$fixture/initialized.json"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$fixture/host" >/dev/null
source_commit="$(git -C "$fixture/source" rev-parse HEAD)"
jq -n --arg project "$fixture/receiver" --arg commit "$source_commit" '{schema_version:1,host:"build",project:$project,source:{commit:$commit},work:{kind:"workflow",path:".hydra/workflows/review.yml"},inputs:[],outputs:["alpha.txt","beta.txt"],capabilities:["exec","workflow","execution-headless"],completion:"workflow-success",limits:{transport_seconds:30,queue_seconds:60,startup_seconds:60,execution_seconds:120,cancellation_seconds:5,log_bytes:4096,artifact_bytes:4096}}' > "$fixture/spec.json"
task() { "$root/bin/hydra" fleet task "$@"; }
task prepare --source "$fixture/source" --spec "$fixture/spec.json" --output "$fixture/package.json" > "$fixture/prepared.json"
binding="$(jq -r '.data.spec_sha256' "$fixture/prepared.json")"
task submit build --input "$fixture/package.json" --key review --trust-spec "$binding" > "$fixture/submitted.json"
task_id="$(jq -r '.data.task_id' "$fixture/submitted.json")"
tries=0
while [ "$tries" -lt 300 ]; do
    task status build --id "$task_id" > "$fixture/status.json"
    if jq -e '.data.runtime.result_state=="ready"' "$fixture/status.json" >/dev/null; then break; fi
    if jq -e '.data.runtime.result_state=="unavailable"' "$fixture/status.json" >/dev/null; then cat "$fixture/status.json" >&2; exit 1; fi
    sleep 0.1
    tries=$((tries + 1))
done
jq -e '.data.runtime.state=="succeeded" and .data.runtime.result_state=="ready"' "$fixture/status.json" >/dev/null
task result build --id "$task_id" --output "$fixture/result.json" > "$fixture/result-output.json"
task inspect-result --input "$fixture/result.json" > "$fixture/inspected.json"
"$root/bin/hydra" fleet attention-data > "$fixture/attention.tsv"
awk -F '\t' '$1=="ITEM" && $3=="result" { print; exit }' "$fixture/attention.tsv" > "$fixture/selected.tsv"
tab="$(printf '\t')"
load_selection() {
    IFS="$tab" read -r tag source kind reason project host task_id run step attempt head instance request binding revision identity freshness route navigable < "$1"
}
load_selection "$fixture/selected.tsv"
[ "$tag" = ITEM ] && [ "$source" = 'fleet overview' ] && [ "$route" = task-result ] && [ "$navigable" = 1 ]
[ "$reason" = result_ready ] && [ "$freshness" = fresh ]
[ "$project" = - ] && [ "$head" = - ] && [ "$instance" = - ]
review() {
    action="$1"; shift
    "$root/bin/hydra" fleet "$action" "$kind" "$project" "$host" "$task_id" "$run" "$step" "$attempt" "$head" "$instance" "$request" "$binding" "$revision" "$identity" "$@"
}
snapshot() {
    find "$fixture/host" "$fixture/source" "$fixture/receiver" "$fixture/client" -type f -exec shasum -a 256 {} + | LC_ALL=C sort
}
snapshot > "$fixture/before.sha256"
review review > "$fixture/review.json"
jq -e '.ok and .data.readiness=="ready" and .data.accepted==false and .data.current_head_authority==false and .data.subject_state=="matches_observation" and (.data.artifacts|length)==2 and (.data.diffs|length)==2 and all(.data.diffs[];.state=="available" and .truncated==false)' "$fixture/review.json" >/dev/null
jq -e --arg source "$source_commit" 'all(.data.diffs[];.source_commit==$source and .head_commit!=$source)' "$fixture/review.json" >/dev/null
jq -er '.data.diffs[].text' "$fixture/review.json" | grep -q '^+alpha change$'
jq -er '.data.diffs[].text' "$fixture/review.json" | grep -q '^+beta change$'
jq -S '.data.artifacts|map({path,sha256,bytes,head_id})' "$fixture/review.json" > "$fixture/review-subjects"
jq -S '.data.artifacts' "$fixture/inspected.json" > "$fixture/inspect-subjects"
cmp "$fixture/review-subjects" "$fixture/inspect-subjects"
review review-data > "$fixture/review.tsv"
awk -F '\t' 'NR==1 {if(NF!=15 || $1!="HYDRA_REVIEW" || $2!=1) exit 1} $1=="TEXT" {n++} $1=="REF" {r++} $1=="PREVIEW" {p++} $1=="END" {if($2!=n || $3!=r || $4!=p) exit 1;end=NR} length($0)>=8192 {exit 1} END {if(end!=NR || end==0) exit 1}' "$fixture/review.tsv"
grep -q "^TEXT${tab}readiness: ready$" "$fixture/review.tsv"
grep -q '+alpha change' "$fixture/review.tsv"
grep -q '+beta change' "$fixture/review.tsv"
[ "$(wc -c < "$fixture/review.tsv" | tr -d ' ')" -le 1048576 ]
snapshot > "$fixture/after.sha256"
cmp "$fixture/before.sha256" "$fixture/after.sha256"
# Explicit references: local files, bounded previews, no symlink/FIFO reads,
# unopened supplied URLs, and control characters neutralized for the terminal.
printf 'first\nsecond\033[31m\n' > "$fixture/log"
ln -s "$fixture/log" "$fixture/symlink"
mkfifo "$fixture/fifo"
refs="$(jq -cn --arg path "$fixture/log" --arg missing "$fixture/missing" --arg sym "$fixture/symlink" --arg fifo "$fixture/fifo" '[{kind:"log",locator:$path},{kind:"transcript",locator:$missing},{kind:"pr",locator:"https://example.invalid/pull/1"},{kind:"log",locator:$sym},{kind:"log",locator:$fifo}]')"
review review "$refs" > "$fixture/references.json"
jq -e '.data.references|map(.state)==["available","inaccessible","supplied-unopened","inaccessible","inaccessible"]' "$fixture/references.json" >/dev/null
review review-data "$refs" > "$fixture/references.tsv"
if LC_ALL=C grep -q "$(printf '\033')" "$fixture/references.tsv"; then exit 1; fi
if review review '[{"kind":"log","locator":"ftp://unsupported"}]' > "$fixture/error.json"; then exit 1; fi
if review review "$(jq -cn '[range(17)|{kind:"log",locator:"/missing"}]')" > "$fixture/error.json"; then exit 1; fi
printf '%5000s' x > "$fixture/long.log"
review review "$(jq -cn --arg path "$fixture/long.log" '[{kind:"log",locator:$path}]')" > "$fixture/long.json"
jq -e '(.data.references[0].preview|length)==4096 and .data.references[0].truncated' "$fixture/long.json" >/dev/null
# Semantic drift retains immutable evidence but revokes the old selection.
directory="$fixture/host/fleet/tasks/$task_id"
cp "$directory/state.json" "$fixture/state.saved"
jq '.state="failed"' "$fixture/state.saved" > "$directory/state.json"
review review > "$fixture/changed-revision.json"
jq -e '.data.readiness=="revoked" and .data.revision_state=="changed" and .data.candidate_state=="verified_retained"' "$fixture/changed-revision.json" >/dev/null
cp "$fixture/state.saved" "$directory/state.json"
execution_project="$(jq -r '.data.runtime.execution_project_id' "$fixture/status.json")"
attempt_file="$fixture/host/state/v2/projects/$execution_project/workflows/runs/$run/steps/$step/authoritative-attempt"
cp "$attempt_file" "$fixture/attempt.saved"
printf '2\n' > "$attempt_file"
review review > "$fixture/changed-attempt.json"
jq -e '.data.readiness=="revoked" and .data.revision_state=="selection_replaced"' "$fixture/changed-attempt.json" >/dev/null
cp "$fixture/attempt.saved" "$attempt_file"
# Wrong original revision/identity and malformed selection bounds cannot become ready.
saved_revision="$revision"
revision="$(printf '%064d' 0)"
review review > "$fixture/wrong-revision.json"
jq -e '.data.readiness=="revoked" and .data.revision_state=="changed"' "$fixture/wrong-revision.json" >/dev/null
revision="$saved_revision"
saved_identity="$identity"
identity="$(printf '%064d' 0)"
if review review > "$fixture/wrong-identity.json"; then exit 1; fi
identity="$saved_identity"
saved_head="$head"
head="$(printf '%128s' x | tr ' ' x)"
if review review > "$fixture/long-selection.json"; then exit 1; fi
head="$saved_head"
# Missing/tampered/expired evidence is explicit; ordinary result_state is insufficient.
mv "$directory/result.json" "$fixture/result.saved"
review review > "$fixture/missing-result.json"
jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="result_unavailable"' "$fixture/missing-result.json" >/dev/null
cp "$fixture/result.saved" "$directory/result.json"
jq '.result.artifacts[0].sha256="0000000000000000000000000000000000000000000000000000000000000000"' "$fixture/result.saved" > "$directory/result.json"
review review > "$fixture/tampered.json"
jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="result_unavailable"' "$fixture/tampered.json" >/dev/null
cp "$fixture/result.saved" "$directory/result.json"
printf '{"schema_version":1,"state":"expired","expired_at":1}\n' > "$directory/retention.json"
review review > "$fixture/expired.json"
jq -e '.data.readiness=="unavailable" and .data.candidate_state=="expired"' "$fixture/expired.json" >/dev/null
rm "$directory/retention.json"
touch "$fixture/offline"
review review > "$fixture/offline.json"
jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="offline"' "$fixture/offline.json" >/dev/null
rm "$fixture/offline"
touch "$fixture/malformed"
review review > "$fixture/malformed.json"
jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="invalid_response"' "$fixture/malformed.json" >/dev/null
rm "$fixture/malformed"
(GIT_DIR="$fixture/source/.git" review review) > "$fixture/git-environment.json"
jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="git_environment_redirect"' "$fixture/git-environment.json" >/dev/null
# shellcheck source=/dev/null
. "$root/tests/fixtures/fleet-review/retained-cases.sh"
# shellcheck source=/dev/null
. "$root/tests/fixtures/fleet-review/request-cases.sh"
# Keep a reproducible real fixture for the native parity consumer when requested.
review review > "$fixture/review.json"
review review-data > "$fixture/review.tsv"
jq -e '.data.readiness=="ready"' "$fixture/review.json" >/dev/null
"$BUILD_DIR/test-fleet-review-projection" "$fixture/review.json"
jq -n --arg cwd "$root" --arg fixture "$fixture" --arg path "$PATH" --arg home "$HYDRA_HOME" --arg binary "$HYDRA_FLEET_BIN" --arg source_commit "$source_commit" --argjson selection "$(jq '.data.selection' "$fixture/review.json")" '{cwd:$cwd,fixture:$fixture,environment:{PATH:$path,HYDRA_HOME:$home,HYDRA_FLEET_BIN:$binary,HYDRA_REVIEW_FIXTURE:$fixture,HYDRA_SKIP_AI:"1",HYDRA_NONINTERACTIVE:"1"},source_commit:$source_commit,selection:$selection,cleanup_owner:"Fleet review worker; hand off through leader"}' > "$fixture/manifest.json"
shasum -a 256 "$HYDRA_FLEET_BIN" "$root/src/fleet/review_task.c" "$root/src/fleet/review_projection.c" "$root/src/fleet/attention_tui.c" "$root/src/fleet/cli.c" "$fixture/package.json" "$fixture/result.json" "$fixture/review.json" "$fixture/review.tsv" > "$fixture/identities.sha256"
printf '%s\n' 'Fleet review: real result subjects/diffs, exact selection, revocation, references and read-only checks passed'
