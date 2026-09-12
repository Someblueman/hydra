#!/bin/sh
# Private, public-API Fleet source: two verified artifacts and two approval waits.
# Only disposable repositories below the explicitly supplied fixture are changed.
set -eu
root=$1
fixture=$2
mkdir -p "$fixture/source/.hydra/workflows" "$fixture/receiver" "$fixture/bin"
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
PATH="$fixture/bin:$PATH"
export PATH
cp "$root/tests/fixtures/fleet-review/ssh.sh" "$fixture/bin/ssh"
chmod +x "$fixture/bin/ssh"
for repo in source receiver; do
    git -C "$fixture/$repo" init -q
    git -C "$fixture/$repo" config user.name AttentionBenchmark
    git -C "$fixture/$repo" config user.email attention@example.invalid
done
cp "$root/tests/fixtures/fleet-review/workflow.yml" "$fixture/source/.hydra/workflows/review.yml"
cp "$root/tests/fixtures/fleet-review/approval.yml" "$fixture/source/.hydra/workflows/approval.yml"
cp "$root/tests/fixtures/fleet-review/task-work.sh" "$fixture/source/task-work.sh"
git -C "$fixture/source" add .
git -C "$fixture/source" -c commit.gpgSign=false commit -qm benchmark-source
git -C "$fixture/receiver" -c commit.gpgSign=false commit --allow-empty -qm receiver
(cd "$fixture/receiver" && HYDRA_HOME="$fixture/host" "$root/bin/hydra" init --no-agent --json) > "$fixture/initialized.json"
"$root/bin/hydra" remote add build loopback --hydra "$root/bin/hydra" --home "$fixture/host" > "$fixture/remote.json"
commit=$(git -C "$fixture/source" rev-parse HEAD)
jq -n --arg project "$fixture/receiver" --arg commit "$commit" '{schema_version:1,host:"build",project:$project,source:{commit:$commit},work:{kind:"workflow",path:".hydra/workflows/review.yml"},inputs:[],outputs:["alpha.txt","beta.txt"],capabilities:["exec","workflow","execution-headless"],completion:"workflow-success",limits:{transport_seconds:30,queue_seconds:60,startup_seconds:60,execution_seconds:120,cancellation_seconds:5,log_bytes:4096,artifact_bytes:4096}}' > "$fixture/result-spec.json"
jq '.work.path=".hydra/workflows/approval.yml"|.outputs=[]' "$fixture/result-spec.json" > "$fixture/approval-spec.json"
for kind in result approval; do
    "$root/bin/hydra" fleet task prepare --source "$fixture/source" --spec "$fixture/$kind-spec.json" --output "$fixture/$kind-package.json" > "$fixture/$kind-prepared.json"
    binding=$(jq -r '.data.spec_sha256' "$fixture/$kind-prepared.json")
    "$root/bin/hydra" fleet task submit build --input "$fixture/$kind-package.json" --key "benchmark-$kind" --trust-spec "$binding" > "$fixture/$kind-submitted.json"
    task_id=$(jq -r '.data.task_id' "$fixture/$kind-submitted.json")
    tries=0
    while [ "$tries" -lt 300 ]; do
        "$root/bin/hydra" fleet task status build --id "$task_id" > "$fixture/$kind-status.json"
        if [ "$kind" = result ]; then
            if jq -e '.data.runtime.state=="succeeded" and .data.runtime.result_state=="ready"' "$fixture/$kind-status.json" >/dev/null; then break; fi
        elif jq -e '.data.runtime.state=="waiting_approval"' "$fixture/$kind-status.json" >/dev/null; then break; fi
        sleep 0.1
        tries=$((tries + 1))
    done
    [ "$tries" -lt 300 ]
done
task_id=$(jq -r '.data.task_id' "$fixture/result-status.json")
"$root/bin/hydra" fleet task result build --id "$task_id" --output "$fixture/result.json" > "$fixture/result-output.json"
"$root/bin/hydra" fleet task inspect-result --input "$fixture/result.json" > "$fixture/inspected.json"
jq -e '.ok and (.data.artifacts|length)==2' "$fixture/inspected.json" >/dev/null
printf '%s\n' "$PATH" > "$fixture/path"
