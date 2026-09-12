#!/bin/sh
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
cleanup() {
    if [ -d "$fixture/repo/.git" ]; then
        (cd "$fixture/repo" && "$root/bin/hydra" kill attention-worker --force >/dev/null 2>&1) || true
    fi
    if [ "${KEEP_FIXTURE:-0}" = 1 ]; then printf '%s\n' "$fixture" >&2; else rm -rf "$fixture"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

attention_path_bounds() (
    fleet="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
    suffix=/projects/project_a/workflows/runs/run_a/steps/check
    state_root="$fixture/path-control"
    mkdir -p "$state_root$suffix"
    printf 'waiting-approval\n' > "$state_root$suffix/state"
    output="$(HYDRA_STATE_V2_ROOT="$state_root" "$fleet" workflow-data attention project_a)"
    printf '%s\n' "$output" | grep -q '"reason":"missing_request"'
    # Darwin rejects absolute paths before Hydra's 4096-byte buffer boundary.
    if [ "$(uname -s)" != Linux ]; then
        printf '%s\n' 'SKIP workflow attention 4096-byte path boundary: requires Linux pathname limits'
        return
    fi
    state_root="$fixture/path-boundary"
    mkdir "$state_root"
    cd "$state_root"
    remaining=$((4090 - ${#state_root} - ${#suffix}))
    while [ "$remaining" -gt 1 ]; do
        size=$((remaining - 1))
        [ "$size" -le 240 ] || size=240
        component="$(printf '%*s' "$size" '' | tr ' ' x)"
        mkdir "$component"
        cd "$component"
        state_root="$state_root/$component"
        remaining=$((remaining - size - 1))
    done
    mkdir -p "${suffix#/}"
    step_dir="$state_root$suffix"
    [ "${#step_dir}" -ge 4089 ]
    [ "${#step_dir}" -le 4090 ]
    cd "$step_dir"
    printf 'waiting-approval\n' > state
    # An unchecked snprintf would read this existing prefix of request-id.
    prefix_length=$((4095 - ${#step_dir} - 1))
    prefix="$(printf '%s' request-id | cut -c "1-$prefix_length")"
    printf 'step_b\n' > "$prefix"
    output="$(HYDRA_STATE_V2_ROOT="$state_root" "$fleet" workflow-data attention project_a)"
    printf '%s\n' "$output" | grep -q '"reason":"path_unavailable"'
    printf '%s\n' "$output" | grep -q '"kind":"unknown"'
    printf '%s\n' "$output" | grep -q '"partial":true'
    printf '%s\n' "$output" | grep -q '"navigable":false'
    [ "$(cat "$prefix")" = step_b ]
    printf '%s\n' 'workflow attention rejects truncated child paths without reading prefix evidence'
)
if [ "${HYDRA_TEST_ATTENTION_PATH_ONLY:-0}" = 1 ]; then
    attention_path_bounds
    exit
fi

repo="$fixture/repo"
mkdir -p "$repo"
git -C "$repo" init -q
git -C "$repo" config user.email test@example.invalid
git -C "$repo" config user.name test
printf 'base\n' > "$repo/tracked"
git -C "$repo" add tracked
git -C "$repo" commit -qm base
export HYDRA_HOME="$fixture/home"
export HYDRA_STATE_V2_ROOT="$HYDRA_HOME/state/v2" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
(cd "$repo" && "$root/bin/hydra" init --no-agent --trust >/dev/null && "$root/bin/hydra" spawn attention-worker --no-agent >/dev/null)
cat > "$fixture/data.json" <<'JSON'
{"schema_version":1,"inputs":{},"steps":{"produce":{"outputs":{"answer":{"path":"answer.txt","type":"file","max_bytes":128}}}}}
JSON
cat > "$fixture/produce.sh" <<'SCRIPT'
#!/bin/sh
set -eu
printf 'sealed attention result\n' > "$HYDRA_WORKFLOW_OUTPUTS_DIR/answer.txt"
SCRIPT
chmod +x "$fixture/produce.sh"
cat > "$fixture/flow.yml" <<EOF2
version: 1
id: attention-public
data: data.json
resources:
  disk_mb: 1
steps:
  - id: produce
    kind: exec
    idempotent: false
    args:
      head: attention-worker
      argv: [sh, $fixture/produce.sh]
  - id: approval
    kind: approval-wait
    needs: [produce]
    idempotent: false
    args:
      head: attention-worker
      name: review
      message: Review sealed output
EOF2
set +e
(cd "$repo" && "$root/bin/hydra" workflow run "$fixture/flow.yml" > "$fixture/run.out" 2> "$fixture/run.err")
run_code=$?
set -e
[ "$run_code" -eq 3 ] || { cat "$fixture/run.err" >&2; exit 1; }
[ "$run_code" -eq 3 ]
run="$(sed -n '1p' "$fixture/run.out")"
run_dir="$(find "$HYDRA_STATE_V2_ROOT/projects" -type d -path "*/workflows/runs/$run" -print)"
project_dir="${run_dir%/workflows/runs/*}"
request="$(sed -n '1p' "$run_dir/steps/approval/request-id")"
[ -n "$request" ]

set +e
missing_output="$(cd "$repo" && HYDRA_STATE_V2_ROOT="$fixture/missing-state" "$root/bin/hydra" workflow attention --json 2>&1)"
missing_code=$?
set -e
[ "$missing_code" -ne 0 ]
printf '%s\n' "$missing_output" | grep -q 'workflow run storage is unavailable'
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"kind":"approval"'
printf '%s\n' "$output" | grep -q '"kind":"result"'
printf '%s\n' "$output" | grep -q '"step_id":"produce"'
printf '%s\n' "$output" | grep -q '"step_id":"approval"'
printf '%s\n' "$output" | grep -q '"accepted":false'
printf '%s\n' "$output" | grep -q '"fresh_action":false'
printf '%s\n' "$output" | grep -q '"read_only":true'
data_output="$(cd "$repo" && "$root/bin/hydra" workflow attention-data)"
printf '%s\n' "$data_output" | grep -q '^HYDRA_ATTENTION[[:space:]]1$'
printf '%s\n' "$data_output" | grep -q "^ITEM$(printf '\t')workflow records$(printf '\t')"
printf '%s\n' "$data_output" | grep -q '^END[[:space:]]2[[:space:]]0[[:space:]]0$'

# Retained authoritative attempts use the same bounded 1..11 contract as requests.
cp -R "$run_dir/steps/produce/attempt-1" "$run_dir/steps/produce/attempt-12"
printf '12\n' > "$run_dir/steps/produce/authoritative-attempt"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"missing_authoritative_attempt"'
if printf '%s\n' "$output" | grep -q '"attempt_id":"attempt-12".*"kind":"result"'; then
    exit 1
fi
rm -rf "$run_dir/steps/produce/attempt-12"
printf '1\n' > "$run_dir/steps/produce/authoritative-attempt"

# A receipt is only a result when its retained artifact still verifies.
printf 'tampered\n' > "$run_dir/steps/produce/attempt-1/artifacts/answer"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"result_binding_unknown"'
printf 'sealed attention result\n' > "$run_dir/steps/produce/attempt-1/artifacts/answer"

# The declared output set and retained artifact path are both mandatory.
cp "$run_dir/steps/produce/attempt-1/outputs.json" "$fixture/receipt.json"
printf '%s\n' '{"schema_version":1,"files":{}}' > "$run_dir/steps/produce/attempt-1/outputs.json"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"result_binding_unknown"'
cp "$fixture/receipt.json" "$run_dir/steps/produce/attempt-1/outputs.json"
mv "$run_dir/steps/produce/attempt-1/artifacts/answer" "$fixture/answer.saved"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"result_binding_unknown"'
mv "$fixture/answer.saved" "$run_dir/steps/produce/attempt-1/artifacts/answer"
touch "$run_dir/retention.json"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"retention_expired"'
rm "$run_dir/retention.json"

# Mutating the stored request binding, instance, or expiry produces explicit unknowns.
printf 'bad\n' > "$run_dir/approvals/$request/binding-hash"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"approval_binding_unknown"'
git_hash="$(git hash-object "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$git_hash" > "$run_dir/approvals/$request/binding-hash"
head_id="$(awk -F '\t' '$1 == "head" { print $2; exit }' "$run_dir/approvals/$request/binding.tsv")"
# Immutable sealed evidence remains inspectable after its execution head is gone;
# its recorded identity is explicit and never treated as a live attachment.
cp -R "$project_dir/heads/$head_id" "$fixture/head.saved"
rm -rf "$project_dir/heads/$head_id"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"kind":"result".*"reason":"result_ready"'
printf '%s\n' "$output" | grep -q '"identity_provenance":"not_recorded"'
printf '%s\n' "$output" | grep -q '"freshness":"fresh"'
mv "$fixture/head.saved" "$project_dir/heads/$head_id"
printf 'instance_badbadbadbad\n' > "$project_dir/heads/$head_id/current-instance"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"approval_binding_unknown"'
instance="$(awk -F '\t' '$1 == "instance" { print $2; exit }' "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$instance" > "$project_dir/heads/$head_id/current-instance"

cp "$run_dir/approvals/$request/binding.tsv" "$fixture/binding.tsv"
awk -F '\t' '$1 != "head"' "$fixture/binding.tsv" > "$run_dir/approvals/$request/binding.tsv"
git_hash="$(git hash-object "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$git_hash" > "$run_dir/approvals/$request/binding-hash"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"approval_binding_unknown"'
if printf '%s\n' "$output" | grep -q '"kind":"approval".*"step_id":"approval"'; then
    exit 1
fi
cp "$fixture/binding.tsv" "$run_dir/approvals/$request/binding.tsv"
git_hash="$(git hash-object "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$git_hash" > "$run_dir/approvals/$request/binding-hash"

# The binding must name the exact stored head resolved by the request, even when
# another stored head reports the same live instance.
cp -R "$project_dir/heads/$head_id" "$project_dir/heads/head_face1234"
printf 'same-instance-worker\n' > "$project_dir/heads/head_face1234/branch"
awk -F '\t' -v OFS='\t' '$1 == "head" {$2 = "head_face1234"} {print}' \
    "$run_dir/approvals/$request/binding.tsv" > "$fixture/binding.same.tsv"
cp "$fixture/binding.same.tsv" "$run_dir/approvals/$request/binding.tsv"
git_hash="$(git hash-object "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$git_hash" > "$run_dir/approvals/$request/binding-hash"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"approval_binding_unknown"'
rm -rf "$project_dir/heads/head_face1234"
cp "$fixture/binding.tsv" "$run_dir/approvals/$request/binding.tsv"
git_hash="$(git hash-object "$run_dir/approvals/$request/binding.tsv")"
printf '%s\n' "$git_hash" > "$run_dir/approvals/$request/binding-hash"

cp -R "$project_dir/heads/$head_id" "$project_dir/heads/head_abcdef12"
printf 'other-worker\n' > "$project_dir/heads/head_abcdef12/branch"
printf 'instance_other123456789\n' > "$project_dir/heads/head_abcdef12/current-instance"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"kind":"approval".*"step_id":"approval"'
rm -rf "$project_dir/heads/head_abcdef12"

cp -R "$project_dir/heads/$head_id" "$project_dir/heads/head_deadbeef"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"approval_binding_unknown"'
rm -rf "$project_dir/heads/head_deadbeef"

printf 'not-a-number\n' > "$run_dir/approvals/$request/expires-at"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"malformed_expiry"'
printf '0\n' > "$run_dir/approvals/$request/expires-at"

# A symlinked retained artifact and an over-cap scan stay unknown/partial.
mv "$run_dir/steps/produce/attempt-1/artifacts/answer" "$fixture/answer.real"
ln -s "$fixture/answer.real" "$run_dir/steps/produce/attempt-1/artifacts/answer"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"reason":"result_binding_unknown"'
if printf '%s\n' "$output" | grep -q '"kind":"result".*"step_id":"produce"'; then
    exit 1
fi
rm "$run_dir/steps/produce/attempt-1/artifacts/answer"
mv "$fixture/answer.real" "$run_dir/steps/produce/attempt-1/artifacts/answer"

mv "$run_dir/steps" "$fixture/steps.real"
ln -s "$fixture/steps.real" "$run_dir/steps"
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"partial":true'
printf '%s\n' "$output" | grep -q '"reason":"missing_steps"'
if printf '%s\n' "$output" | grep -q '"kind":"result"'; then
    exit 1
fi
rm "$run_dir/steps"
mv "$fixture/steps.real" "$run_dir/steps"

step_n=0
while [ "$step_n" -lt 513 ]; do
    step_n=$((step_n + 1))
    mkdir -p "$run_dir/steps/cap_$step_n"
done
output="$(cd "$repo" && "$root/bin/hydra" workflow attention --json)"
printf '%s\n' "$output" | grep -q '"truncated":true'
printf '%s\n' "$output" | grep -q '"partial":true'

attention_path_bounds
printf '%s\n' 'workflow attention public CLI checks passed'
