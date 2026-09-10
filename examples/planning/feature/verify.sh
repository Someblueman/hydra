#!/bin/sh
set -eu
verify_root="$(mktemp -d)"
trap 'rm -rf "$verify_root"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
printf 'Makefile\nmain.c\nslug.c\nslug.h\n' > "$verify_root/expected-members"
tar -tf "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | LC_ALL=C sort > "$verify_root/members"
cmp "$verify_root/expected-members" "$verify_root/members"
cp test_slug.c "$verify_root/test_slug.c"
tar -xf "$HYDRA_WORKFLOW_INPUTS_DIR/subject" -C "$verify_root"
(
    cd "$verify_root"
    make
    cc -std=c99 -Wall -Wextra -Werror -pedantic test_slug.c slug.c -o test-slug
    ./test-slug
    test "$(./catalog-slug ' Hello, WORLD! ' 'a__b')" = "$(printf 'hello-world\na-b')"
    test "$(./catalog-slug --limit 3 ABC)" = abc
    if ./catalog-slug --limit 2 ABC; then exit 1; else test "$?" -eq 2; fi
    if ./catalog-slug --limit 0 ABC; then exit 1; else test "$?" -eq 2; fi
    if ./catalog-slug; then exit 1; else test "$?" -eq 2; fi
)
digest="$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)"
validator="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["data"]["slug-check"])' "$HYDRA_WORKFLOW_VALIDATION_FILE")"
recipe="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["data"]["slug-check-recipe"])' "$HYDRA_WORKFLOW_VALIDATION_FILE")"
python3 - "$HYDRA_WORKFLOW_OUTPUTS_DIR/verification.json" "$digest" "$validator" "$recipe" <<'PY'
import hashlib, json, sys
out, subject, validator, recipe = sys.argv[1:]
raw = {'verdict':'pass','measurement':'strict C99 build, independent API/CLI checks','requirements':['normalization','bounds','cli']}
rh = hashlib.sha256(json.dumps(raw, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
obs = [{'id':'slug-check','raw':raw,'raw_sha256':rh}]
record = {'obligation_id':'slug-check','subject_manifest_sha256':subject,'validator_identity':'feature-slug-v3','validator_recipe_sha256':recipe,'invocation':{'argv':['sh','verify.sh'],'exit_code':0},'environment':{'host':'local','toolchain':'cc/make/sh'},'case_inventory':['normalization','bounds','cli'],'observations':obs,'measurements':[raw],'raw_evidence_sha256':hashlib.sha256(json.dumps(obs, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),'counts':{'executed':1,'failed':0,'skipped':0},'limitations':['fixture-only']}
record2=dict(record); record2['obligation_id']='bounds-check'; record2['observations']=[dict(obs[0], id='bounds-check')]
record3=dict(record); record3['obligation_id']='cli-check'; record3['observations']=[dict(obs[0], id='cli-check')]
for rr in (record2, record3): rr['raw_evidence_sha256']=hashlib.sha256(json.dumps(rr['observations'], sort_keys=True, separators=(',', ':')).encode()).hexdigest()
report = {'schema_version':3,'execution_status':'completed','evidence_status':'valid','domain_verdict':'pass','verdict':'pass','subject_sha256':subject,'validator_sha256':validator,'requirements':['normalization','bounds','cli'],'evidence':'sealed archive independently built and tested','measurements':[raw],'limitations':['fixture-only'],'evidence_records':[record,record2,record3]}
json.dump(report, open(out, 'w'), separators=(',', ':')); open(out, 'a').write('\n')
PY
