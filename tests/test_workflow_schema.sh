#!/bin/sh
set -u

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
HYDRA_BIN="$ROOT_DIR/bin/hydra"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/helpers.sh"

test_count=0
pass_count=0
fail_count=0
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT INT TERM HUP
export HYDRA_HOME="$TEST_ROOT/home"

valid="$TEST_ROOT/valid.yml"
# shellcheck disable=SC2016
sed -n '/^```yaml$/,/^```$/p' "$ROOT_DIR/docs/workflows.md" | sed '1d;$d' > "$valid"

"$HYDRA_BIN" workflow validate "$valid" > "$TEST_ROOT/out" 2>&1
assert_success "$?" "accepts documented schema"
"$HYDRA_BIN" workflow show "$valid" > "$TEST_ROOT/show1" 2>&1
"$HYDRA_BIN" workflow show "$valid" > "$TEST_ROOT/show2" 2>&1
cmp -s "$TEST_ROOT/show1" "$TEST_ROOT/show2"
assert_success "$?" "normalized view is deterministic"
"$HYDRA_BIN" workflow dry-run "$valid" > "$TEST_ROOT/dry" 2>&1
assert_success "$?" "dry-run accepts a valid DAG"
grep -q 'NON-IDEMPOTENT BOUNDARY' "$TEST_ROOT/dry"
assert_success "$?" "dry-run explains idempotency boundaries"

reject() {
    _r_name="$1"
    _r_text="$2"
    printf '%s\n' "$_r_text" > "$TEST_ROOT/$_r_name.yml"
    "$HYDRA_BIN" workflow validate "$TEST_ROOT/$_r_name.yml" > "$TEST_ROOT/reject" 2>&1
    assert_failure "$?" "rejects $_r_name"
    grep -q "$TEST_ROOT/$_r_name.yml" "$TEST_ROOT/reject"
    assert_success "$?" "$_r_name error identifies definition"
}

base='version: 1
id: sample
steps:
  - id: first
    kind: wait
    needs: []
    args:
      head: one'
reject unknown_top "$base
extra: no"
reject unknown_step 'version: 1
id: sample
steps:
  - id: first
    kind: wait
    mystery: no
    args:
      head: one'
reject unsupported_yaml 'version: &v 1
id: sample
steps: []'
reject duplicate_id 'version: 1
id: sample
steps:
  - id: same
    kind: wait
    args:
      head: one
  - id: same
    kind: wait
    args:
      head: two'
reject missing_dependency 'version: 1
id: sample
steps:
  - id: first
    kind: wait
    needs: [absent]
    args:
      head: one'
reject self_dependency 'version: 1
id: sample
steps:
  - id: first
    kind: wait
    needs: [first]
    args:
      head: one'
reject cycle 'version: 1
id: sample
steps:
  - id: first
    kind: wait
    needs: [second]
    args:
      head: one
  - id: second
    kind: wait
    needs: [first]
    args:
      head: two'
reject invalid_name 'version: 1
id: Bad Name
steps:
  - id: first
    kind: wait
    args:
      head: one'
reject invalid_bound 'version: 1
id: sample
parallelism: 17
steps:
  - id: first
    kind: wait
    args:
      head: one'
reject ambiguous_command 'version: 1
id: sample
steps:
  - id: first
    kind: exec
    args:
      command: make test
      argv: [make, test]'
reject unsupported_kind 'version: 1
id: sample
steps:
  - id: first
    kind: daemon
    args:
      head: one'

before="$(find "$HYDRA_HOME" -type f -print | LC_ALL=C sort | xargs cksum 2>/dev/null || true)"
"$HYDRA_BIN" workflow dry-run "$valid" >/dev/null
after="$(find "$HYDRA_HOME" -type f -print | LC_ALL=C sort | xargs cksum 2>/dev/null || true)"
assert_equal "$before" "$after" "dry-run does not mutate initialized Hydra state"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
