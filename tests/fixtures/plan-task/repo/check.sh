#!/bin/sh
set -eu
check="$1" requirement="$2" mode="$3"
[ "$mode" != crash ] || exit 7
case "$check" in
    part-check) printf 'candidate\n' ;;
    final-check) printf 'candidate\ncomposed\n' ;;
    *) exit 1 ;;
esac | cmp - "$HYDRA_TASK_INPUT_DIR/subject"
if command -v sha256sum >/dev/null 2>&1; then
    hash="$(sha256sum "$HYDRA_TASK_INPUT_DIR/subject" | cut -d ' ' -f 1)"
else
    hash="$(shasum -a 256 "$HYDRA_TASK_INPUT_DIR/subject" | cut -d ' ' -f 1)"
fi
validator="$(sed -n 's/.*"'"$check"'":"\([a-f0-9]*\)".*/\1/p' "$HYDRA_TASK_INPUT_DIR/validation")"
[ "${#validator}" -eq 64 ]
verdict=pass
case "$mode" in
    fail|inconclusive) verdict="$mode" ;;
    stale-subject) hash=0000000000000000000000000000000000000000000000000000000000000000 ;;
    stale-validator) validator=0000000000000000000000000000000000000000000000000000000000000000 ;;
    missing-coverage) requirement=absent ;;
esac
printf '{"schema_version":2,"verdict":"%s","subject_sha256":"%s","validator_sha256":"%s","requirements":["%s"],"evidence":"Inspected the exact candidate bytes"}\n' "$verdict" "$hash" "$validator" "$requirement" > report.json
