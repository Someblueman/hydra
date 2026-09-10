#!/bin/sh
# Independent source reconstruction and real checks for the two frozen schedules.
set -eu
mode="$1"
case "$mode" in functional|sanitize) ;; *) exit 2 ;; esac
candidate="$(mktemp -d)"
cleanup() {
    cleanup_status="$1"
    if [ "$cleanup_status" -ne 0 ]; then
        printf 'Verification failed: %s (exit %s)\n' "$mode" "$cleanup_status" >&2
        for log in "$candidate"/*.log; do
            if [ -f "$log" ]; then cat "$log" >&2; fi
        done
    fi
    rm -rf "$candidate"
    exit "$cleanup_status"
}
trap 'cleanup "$?"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
git archive HEAD | tar -x -C "$candidate"
subject="$HYDRA_WORKFLOW_INPUTS_DIR/subject"
validation="$HYDRA_WORKFLOW_VALIDATION_FILE"
outputs="$HYDRA_WORKFLOW_OUTPUTS_DIR"
cd "$candidate"
git init -q
git -c core.hooksPath=/dev/null add .
git -c core.hooksPath=/dev/null -c user.name=Qualification -c user.email=qualification@example.invalid -c commit.gpgSign=false commit -qm baseline
git apply --check "$subject"
git apply "$subject"
# A worker never supplies the checker recipe: these scripts come from the source
# snapshot. The patch is rejected if it attempts to modify that trusted recipe.
if git diff --name-only | grep -E '^(compose|verify|audit)\.sh$'; then exit 1; fi
export PYTHONDONTWRITEBYTECODE=1
unset HYDRA_CORE HYDRA_FLEET_BIN HYDRA_PLAN_PRECOMPILE_BIN
if [ "$mode" = functional ]; then
    make build-plan-precompile test-plan-outcomes test-plan-staged > native.log 2>&1
    sh audit.sh > coverage.log 2>&1
    python3 tests/test_plan_staged_public.py --fleet "$candidate/build/hydra-fleet" \
        --precompiler "$candidate/build/plan-precompile" --output "$candidate/public.json" >> native.log 2>&1
    cat "$candidate/public.json" >> native.log
    cases='["native-check","coverage-check"]'
    requirements='["native","coverage"]'
else
    make CC="$(cat sanitizer-cc)" BUILD_DIR=build/sanitized CORE_CFLAGS='-O1 -g -std=c99 -Wall -Wextra -Werror -pedantic -Isrc -fsanitize=address,undefined -fno-omit-frame-pointer' \
        build-plan-precompile test-plan-outcomes test-plan-staged > sanitizer.log 2>&1
    cases='["sanitizer-check"]'
    requirements='["sanitizer"]'
fi
hash_file() { shasum -a 256 "$1" | cut -d ' ' -f 1; }
# Hydra's existing JSON-C canonical digest escapes forward slashes.
canonical_hash() { sed 's#/#\\/#g' | shasum -a 256 | cut -d ' ' -f 1; }
subject_sha="$(hash_file "$subject")"
validator="$(jq -er --arg id "$mode" '.data[$id]' "$validation")"
recipe="$(jq -er --arg id "$mode-recipe" '.data[$id]' "$validation")"
# Each observation refers to the actual command log for one required check.
printf '[]' > observations.json
for id in $(printf '%s' "$cases" | jq -r '.[]'); do
    case "$id" in native-check) log=native.log ;; coverage-check) log=coverage.log ;; sanitizer-check) log=sanitizer.log ;; *) exit 2 ;; esac
    log_sha="$(hash_file "$log")"
    tail -c 2500 "$log" > observation-output
    raw="$(jq -cnS --arg log_sha256 "$log_sha" --rawfile output observation-output '{actual:"pass",verdict:"pass",measurement:1,log_sha256:$log_sha256,output:$output}')"
    raw_sha="$(printf '%s' "$raw" | canonical_hash)"
    jq -cS --arg id "$id" --argjson raw "$raw" --arg hash "$raw_sha" \
        '. + [{id:$id,raw:$raw,raw_sha256:$hash}]' observations.json > next.json
    mv next.json observations.json
    cat "$log"
done
observations="$(jq -cS . observations.json)"
evidence_sha="$(printf '%s' "$observations" | canonical_hash)"
jq -n --arg subject "$subject_sha" --arg validator "$validator" --arg recipe "$recipe" \
    --arg mode "$mode" --argjson cases "$cases" --argjson requirements "$requirements" \
    --argjson observations "$observations" --arg evidence "$evidence_sha" '
    {schema_version:3,execution_status:"completed",evidence_status:"valid",domain_verdict:"pass",verdict:"pass",
     subject_sha256:$subject,validator_sha256:$validator,requirements:$requirements,
     evidence:"Exact cleanup patch applied to bound baseline and independently rebuilt/tested; actual logs are in observations.",
     limitations:["One real repository cleanup; costs unknown; no semantic planning-benefit claim."],
     evidence_records:[$cases[] as $case | {
       obligation_id:$case,subject_manifest_sha256:$subject,validator_identity:"hydra-python-cleanup-1",
       validator_recipe_sha256:$recipe,invocation:{argv:["sh","verify.sh",$mode],exit_code:0},
       environment:{host:"local",toolchain:"cc/make/sh/python3/jq"},case_inventory:$cases,
       observations:$observations,raw_evidence_sha256:$evidence,
       counts:{executed:($cases|length),failed:0,skipped:0},limitations:["Single bounded repository change."]}]}' \
    > "$outputs/verification.json"
