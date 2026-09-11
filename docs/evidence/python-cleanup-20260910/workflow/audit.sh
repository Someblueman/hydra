#!/bin/sh
# Held-out checks specified from the original contracts before the native port.
set -eu
case_root="$(mktemp -d)"
trap 'rm -rf "$case_root"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
precompiler="$PWD/build/plan-precompile"
# Retained drivers and oracles must remain byte-identical except the three
# authorized entrypoint migrations. Production source must remain byte-identical.
for path in $(git ls-files '*.py'); do
    case "$path" in
        examples/planning/evaluation/*|tests/test_planner_evaluation_public.py|examples/planning/manifest-map/precompile.py|examples/planning/staged/precompile.py)
            if [ -f "$path" ]; then printf 'Retired Python remains: %s\n' "$path" >&2; exit 1; fi ;;
        tests/test_plan_manifest.py|tests/test_plan_staged.py|tests/test_plan_staged_public.py)
            test -f "$path" ;;
        *) git show "HEAD:$path" > "$case_root/baseline"; cmp "$case_root/baseline" "$path" ;;
    esac
done
git ls-files '*.py' | while IFS= read -r path; do
    if [ -f "$path" ]; then printf '%s\n' "$path"; fi
done | LC_ALL=C sort > "$case_root/expected-python"
find examples tests -type f -name '*.py' | LC_ALL=C sort > "$case_root/actual-python"
cmp "$case_root/expected-python" "$case_root/actual-python"
test -z "$(git diff --name-only -- bin lib src)"
test -z "$(git diff --name-only -- '*.sh')"
# The exact historical precompilers act only as independent output oracles.
mkdir "$case_root/original"
git show HEAD:examples/planning/manifest-map/precompile.py > "$case_root/original/manifest.py"
git show HEAD:examples/planning/staged/precompile.py > "$case_root/original/staged.py"
git show HEAD:examples/planning/staged/unique_json.py > "$case_root/original/unique_json.py"
cat > "$case_root/hydra-result" <<'RESULT'
#!/bin/sh
set -eu
test "$1" = workflow && test "$2" = plan && test "$3" = result && test "$4" = run_fixture
cat "$PUBLIC_RESULT"
RESULT
chmod +x "$case_root/hydra-result"
export HYDRA_BIN="$case_root/hydra-result" PUBLIC_RESULT="$case_root/public.json"
cd "$case_root"
# One fresh case for empty, all-disabled, minimum/maximum value, collision-like
# IDs and the maximum cardinality. The native implementation never sees the
# expected graph; jq compares full normalized output including JSON definitions.
for case_id in empty skipped endpoints names maximum; do
    case "$case_id" in
        empty) items='[]' ;;
        skipped) items='[{"id":"disabled","value":-10,"enabled":false}]' ;;
        endpoints) items='[{"id":"low","value":-10,"enabled":true},{"id":"high","value":10,"enabled":true}]' ;;
        names) items='[{"id":"source","value":0,"enabled":true},{"id":"finding","value":1,"enabled":true},{"id":"check","value":-1,"enabled":false}]' ;;
        maximum) items="$(jq -cn '[range(0;8)|{id:("x-"+tostring),value:.,enabled:(.%2==0)}]')" ;;
    esac
    jq -n --argjson items "$items" '{schema_version:1,items:$items}' > manifest.json
    python3 original/manifest.py manifest.json old.json
    "$precompiler" manifest manifest.json new.json
    jq -S '.checks[].definition |= fromjson' old.json > old-normalized.json
    jq -S '.checks[].definition |= fromjson' new.json > new-normalized.json
    cmp old-normalized.json new-normalized.json
    source_sha="$(shasum -a 256 manifest.json | cut -d ' ' -f 1)"
    results="$(jq -cS '[.items[]|select(.enabled)|{id:.id,value:.value,square:(.value*.value)}]' manifest.json)"
    result_sha="$(printf '%s' "$results" | shasum -a 256 | cut -d ' ' -f 1)"
    jq -n --slurpfile manifest manifest.json --arg source "$source_sha" --arg result "$result_sha" --argjson results "$results" \
        '{schema_version:3,status:"pass",source_path:"manifest.json",source_sha256:$source,selected_ids:[$manifest[0].items[]|select(.enabled)|.id],results:$results,result_sha256:$result,limitations:[]}' > finding.json
    finding_sha="$(shasum -a 256 finding.json | cut -d ' ' -f 1)"
    jq -n --arg path "$case_root/finding.json" --arg hash "$finding_sha" \
        '{ok:true,data:{schema_version:1,plan_sha256:("a"*64),verdict:"pass",deliverables:{report:{type:"file",path:$path,sha256:$hash}},checks:{check:{verdict:"pass"}}}}' > "$PUBLIC_RESULT"
    python3 original/staged.py manifest.json run_fixture old.json
    "$precompiler" staged manifest.json run_fixture new.json
    jq -S '.checks[].definition |= fromjson' old.json > old-normalized.json
    jq -S '.checks[].definition |= fromjson' new.json > new-normalized.json
    cmp old-normalized.json new-normalized.json
    printf 'Full original/native graph equality: %s, manifest and staged\n' "$case_id"
done
# Independently proposed stage-1 boundary corruptions. Rehash each forged
# finding in the public-result double so rejection must be semantic.
cp finding.json accepted-finding.json
cp "$PUBLIC_RESULT" accepted-public.json
for mutation in order duplicate forged; do
    case "$mutation" in
        order) jq '.selected_ids |= reverse' accepted-finding.json > finding.json; expected='finding selection mismatch' ;;
        duplicate) jq '.selected_ids[1] = .selected_ids[0]' accepted-finding.json > finding.json; expected='finding selection mismatch' ;;
        forged)
            jq '.results[0].square += 1' accepted-finding.json > finding.json
            forged="$(jq -cS .results finding.json)"
            forged_sha="$(printf '%s' "$forged" | shasum -a 256 | cut -d ' ' -f 1)"
            jq --arg hash "$forged_sha" '.result_sha256=$hash' finding.json > replacement.json
            mv replacement.json finding.json; expected='finding results mismatch' ;;
    esac
    finding_sha="$(shasum -a 256 finding.json | cut -d ' ' -f 1)"
    jq --arg hash "$finding_sha" '.data.deliverables.report.sha256=$hash' accepted-public.json > "$PUBLIC_RESULT"
    printf '%s' preserved > old-output
    if "$precompiler" staged manifest.json run_fixture old-output 2> refusal; then exit 1; fi
    grep -F "$expected" refusal
    test "$(cat old-output)" = preserved
done
cp accepted-finding.json finding.json
jq '.data.checks.check.verdict="fail"' accepted-public.json > "$PUBLIC_RESULT"
if "$precompiler" staged manifest.json run_fixture old-output 2> refusal; then exit 1; fi
grep -F 'lacks passing stage-1 check' refusal
cp accepted-public.json "$PUBLIC_RESULT"
# External accepted bytes remain valid while the local copy changes.
jq --arg path "$case_root/accepted-finding.json" '.data.deliverables.report.path=$path' accepted-public.json > "$PUBLIC_RESULT"
printf ' ' >> finding.json
if "$precompiler" staged manifest.json run_fixture old-output 2> refusal; then exit 1; fi
grep -F 'local finding does not match' refusal
cp accepted-finding.json finding.json
cp accepted-public.json "$PUBLIC_RESULT"
# Parent-relative staging and caller-relative output are preserved.
mkdir caller
(cd caller && "$precompiler" staged ../manifest.json run_fixture caller-output.json)
test -s caller/caller-output.json
# FIFO findings must refuse promptly rather than block while opening a pipe.
mkfifo fifo-finding
jq --arg path "$case_root/fifo-finding" '.data.deliverables.report.path=$path' accepted-public.json > "$PUBLIC_RESULT"
if "$precompiler" staged manifest.json run_fixture old-output 2> refusal; then exit 1; fi
grep -F 'invalid, duplicate or oversized finding' refusal
cp accepted-public.json "$PUBLIC_RESULT"
printf 'Stage selection/order/semantic forgery/check/local-byte/parent-path/FIFO controls passed.\n'
# Extra token-boundary cases held out from the direct driver tests.
for bad in '{"schema_version":1,"items":[{"id":"x\u0000bad","value":0,"enabled":true}]}' \
    '{"schema_version":1,"items":[{"id":"x","value":-0.0,"enabled":true}]}' \
    '{"schema_version":1,"items":[{"id":"x","value":0,"enabled":true,"\u0069d":"x"}]}' \
    '{"schema_version":1,"items":[]} trailing'; do
    printf '%s' "$bad" > manifest.json
    printf '%s' preserved > old-output
    if "$precompiler" manifest manifest.json old-output; then exit 1; fi
    test "$(cat old-output)" = preserved
done
printf 'Retained Python and production source unchanged; retired files absent; held-out refusals preserved output.\n'
