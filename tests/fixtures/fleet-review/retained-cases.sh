#!/bin/sh
# Sourced adversarial checks over the public-created immutable result fixture.
set -eu
: "${directory:?}" "${fixture:?}" "${run:?}" "${step:?}" "${attempt:?}"
rehash_result() {
    "$BUILD_DIR/test-fleet-review-projection" rehash "$directory/result.json" > "$fixture/rehash.json"
    cp "$fixture/rehash.json" "$directory/result.json"
}
cp "$directory/package.json" "$fixture/package.saved"
jq '.spec.source.commit="0000000000000000000000000000000000000000"' "$fixture/package.saved" > "$directory/package.json"
review review > "$fixture/changed-source.json"
jq -e '.data.readiness=="revoked" and .data.configuration_state=="changed"' "$fixture/changed-source.json" >/dev/null
cp "$fixture/package.saved" "$directory/package.json"
jq '.spec_sha256="0000000000000000000000000000000000000000000000000000000000000000"' "$fixture/package.saved" > "$directory/package.json"
review review > "$fixture/changed-spec.json"
jq -e '.data.readiness=="revoked" and .data.revision_state=="unavailable"' "$fixture/changed-spec.json" >/dev/null
cp "$fixture/package.saved" "$directory/package.json"
# An outer checksum cannot conceal invalid head, artifact or evidence bindings.
for probe in head artifact evidence; do
    case "$probe" in
        head) expression='.result.heads[0].commit="0000000000000000000000000000000000000000"' ;;
        artifact) expression='.result.artifacts[0].sha256="0000000000000000000000000000000000000000000000000000000000000000"' ;;
        evidence) expression='.result.evidence=[]' ;;
    esac
    jq "$expression" "$fixture/result.saved" > "$directory/result.json"
    rehash_result
    review review > "$fixture/invalid-$probe.json"
    jq -e '.data.readiness=="unavailable" and .data.unavailable_reason=="result_unavailable"' "$fixture/invalid-$probe.json" >/dev/null
done
cp "$fixture/result.saved" "$directory/result.json"
# Valid integrity with failed recorded completion must still revoke readiness.
jq '.result.receipt.runtime.state="failed"' "$fixture/result.saved" > "$directory/result.json"
rehash_result
task inspect-result --input "$directory/result.json" > "$fixture/failed-inspection.json"
review review > "$fixture/failed-verification.json"
jq -e '.data.readiness=="revoked" and .data.checks.state=="failed_or_unavailable"' "$fixture/failed-verification.json" >/dev/null
cp "$fixture/result.saved" "$directory/result.json"
# Tampering only selected-attempt completion passes base bundle validation but
# must fail the stronger exact-attempt review boundary.
jq --arg path "workflows/runs/$run/steps/$step/$attempt/exit-code" '.result.evidence|=map(select(.path!=$path))' "$fixture/result.saved" > "$directory/result.json"
rehash_result
task inspect-result --input "$directory/result.json" > "$fixture/missing-check-inspection.json"
review review > "$fixture/missing-check.json"
jq -e '.data.readiness=="revoked" and .data.checks.state=="failed_or_unavailable"' "$fixture/missing-check.json" >/dev/null
cp "$fixture/result.saved" "$directory/result.json"
