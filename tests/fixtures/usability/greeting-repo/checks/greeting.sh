#!/bin/sh
# The repository's own check, run on the worker branch: the greeting must be
# committed there and printed by lib/greet.sh. It writes Hydra's check report.
set -u
expected="$(cat expected-greeting.txt)"
passed=0
printf 'Running repository greeting checks\n'
committed="$(git show HEAD:greeting.txt 2>/dev/null || true)"
if [ "$committed" = "$expected" ]; then
    printf '[PASS] greeting-committed\n'
    passed=$((passed + 1))
else
    printf '[FAIL] greeting-committed: committed greeting.txt says "%s", expected "%s"\n' "$committed" "$expected"
fi
printed="$(sh lib/greet.sh 2>/dev/null || true)"
if [ "$printed" = "$expected" ]; then
    printf '[PASS] greeting-prints\n'
    passed=$((passed + 1))
else
    printf '[FAIL] greeting-prints: lib/greet.sh prints "%s", expected "%s"\n' "$printed" "$expected"
fi
printf 'Passed: %s  Failed: %s  Total: 2\n' "$passed" "$((2 - passed))"
verdict=fail
[ "$passed" -ne 2 ] || verdict=pass
hash=$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)
printf '{"schema_version":1,"verdict":"%s","subject_sha256":"%s","requirements":["greeting-committed"],"evidence":"Repository greeting checks: %s of 2 passed"}\n' \
    "$verdict" "$hash" "$passed" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/check.json"
[ "$verdict" = pass ]
