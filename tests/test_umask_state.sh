#!/bin/sh
# Hydra state stays private under a group-writable umask (Ubuntu's default
# 002), while worktree files keep following the user's umask. Shared state is
# still refused, and `hydra doctor --fix-permissions` repairs only your own
# regular files and directories without following links.

umask 002
test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
repo="$base/repo"
# Hydra creates this home itself; a test must not pre-create it.
export HYDRA_HOME="$base/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
export HYDRA_SETUP_CONTINUE=1
export HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-umask or make test-one T=umask_state}"
trap 'rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# Print group- or other-writable entries, never following links.
writable() {
    find "$@" ! -type l \( -perm -0020 -o -perm -0002 \) -print 2>/dev/null
}

# Print the permission string of one entry (for example -rw-rw-r--).
mode_of() {
    # stat(1) differs between GNU and BSD; ls -ld is portable for one entry.
    # shellcheck disable=SC2012
    ls -ld "$1" | awk '{ print substr($1, 1, 10) }'
}

echo "Testing Hydra state under umask 002..."

"$HYDRA_BIN" admission configure 2 2 0 8 - >/dev/null
assert_success $? "admission configures a fresh home under umask 002"
"$HYDRA_BIN" admission request umask-probe project_umask 60 - | grep -q '"state":"reserved"'
assert_success $? "admission grants a request under umask 002"
"$HYDRA_BIN" admission release umask-probe --confirmed >/dev/null
assert_success $? "admission releases the request"

mkdir -p "$repo/.hydra/workflows"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
writer="$base/write.sh"
# shellcheck disable=SC2016 # The generated script expands its own arguments.
printf '%s\n' '#!/bin/sh' 'printf "%s\n" "$1" > "$2"' > "$writer"
chmod +x "$writer"
printf '%s\n' \
    'version: 1' \
    'id: umask-run' \
    'parallelism: 1' \
    'resources:' \
    '  disk_mb: 1' \
    '  max_heads: 1' \
    'steps:' \
    '  - id: spawn-head' \
    '    kind: spawn' \
    '    needs: []' \
    '    retry: 0' \
    '    idempotent: false' \
    '    args:' \
    '      branch: umask-workflow' \
    '      terminal_mode: headless' \
    '  - id: write' \
    '    kind: exec' \
    '    needs: [spawn-head]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: umask-workflow' \
    "      argv: [sh, $writer, workflow, workflow.txt]" \
    '  - id: check' \
    '    kind: gate' \
    '    needs: [write]' \
    '    retry: 0' \
    '    idempotent: true' \
    '    args:' \
    '      head: umask-workflow' \
    '      name: acceptance' \
    '      argv: [test, -f, workflow.txt]' > .hydra/workflows/umask-run.yml
git add README .hydra
git commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "init records host-local trust under umask 002"

"$HYDRA_BIN" spawn --headless --no-agent umask-head >/dev/null 2>&1
assert_success $? "headless spawn passes admission under umask 002"
"$HYDRA_BIN" exec --branch umask-head -- sh -c 'printf created > created.txt' >/dev/null 2>&1
assert_success $? "exec runs in the head worktree"
worktree="$("$HYDRA_BIN" path umask-head 2>/dev/null)"
assert_equal "-rw-rw-r--" "$(mode_of "$worktree/created.txt")" "worktree files keep the user's umask"
assert_equal "drwxrwxr-x" "$(mode_of "$worktree")" "worktree directories keep the user's umask"

run="$("$HYDRA_BIN" workflow run umask-run 2>/dev/null)"
assert_success $? "workflow run completes under umask 002"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run" -print | sed -n '1p')"
assert_equal succeeded "$(sed -n '1p' "$run_dir/state" 2>/dev/null)" "workflow run records success"
assert_equal "drwx------" "$(mode_of "$run_dir/steps/write")" "workflow run creates a private steps directory"
workflow_tree="$("$HYDRA_BIN" path umask-workflow 2>/dev/null)"
assert_equal "-rw-rw-r--" "$(mode_of "$workflow_tree/workflow.txt")" "workflow step commands keep the user's umask"
"$HYDRA_BIN" events tail --branch umask-head >/dev/null 2>&1 || true
"$HYDRA_BIN" state verify >/dev/null 2>&1
assert_success $? "state verifies under umask 002"

assert_equal "" "$(writable "$HYDRA_HOME" "$repo/.git/hydra")" "Hydra creates no group- or other-writable state"
statistics="$("$HYDRA_BIN" workflow statistics-data 2>/dev/null)"
printf '%s\n' "$statistics" | awk -F '\t' -v id="$run" '$1 == "R" && $2 == id && $14 != "-" { found = 1 } END { exit !found }'
assert_success $? "workflow statistics read the private run record"

"$HYDRA_BIN" doctor > "$base/doctor.out" 2>&1
grep -q 'Hydra state is private' "$base/doctor.out"
assert_success $? "doctor reports private state"

echo ""
echo "Testing shared state refusal and explicit repair..."

chmod g+w "$HYDRA_HOME" "$run_dir/steps/write" "$run_dir/events.jsonl"
"$HYDRA_BIN" admission status --json > "$base/admission.out" 2>&1
assert_failure $? "a group-writable home is still refused"
grep -q state_unavailable "$base/admission.out"
assert_success $? "refusal names unavailable state"
"$HYDRA_BIN" workflow statistics-data 2>/dev/null |
    awk -F '\t' -v id="$run" '$1 == "R" && $2 == id && $14 == "-" { found = 1 } END { exit !found }'
assert_success $? "a group-writable run record is not counted"

outside="$base/outside"
mkdir -p "$outside/dir"
printf 'victim\n' > "$outside/file"
printf 'victim\n' > "$outside/dir/file"
chmod 664 "$outside/file" "$outside/dir/file"
chmod 775 "$outside/dir"
ln -s "$outside/file" "$HYDRA_HOME/link-file"
ln -s "$outside/dir" "$HYDRA_HOME/link-dir"
mkfifo -m 662 "$HYDRA_HOME/shared-pipe"

"$HYDRA_BIN" doctor > "$base/doctor.out" 2>&1
assert_failure $? "doctor fails on group-writable state"
grep -q 'hydra doctor --fix-permissions' "$base/doctor.out"
assert_success $? "doctor names the explicit repair"
grep -q "$run_dir/events.jsonl" "$base/doctor.out"
assert_success $? "doctor lists the writable entries"
grep -q 'left unchanged' "$base/doctor.out" && grep -q "$HYDRA_HOME/shared-pipe" "$base/doctor.out"
assert_success $? "doctor reports entries it will not repair"
grep -q "$outside" "$base/doctor.out"
assert_failure $? "doctor does not follow links out of the state tree"

"$HYDRA_BIN" doctor --fix-permissions > "$base/repair.out" 2>&1
grep -q 'Removed group and other write from 3 Hydra state entries' "$base/repair.out"
assert_success $? "repair removes group write from owned entries"
assert_equal "drwx------" "$(mode_of "$HYDRA_HOME")" "repair keeps the home private"
assert_equal "-rw-------" "$(mode_of "$run_dir/events.jsonl")" "repair clears group write on files"
assert_equal "-rw-rw-r--" "$(mode_of "$outside/file")" "repair does not follow a file link"
assert_equal "-rw-rw-r--" "$(mode_of "$outside/dir/file")" "repair does not follow a directory link"
assert_equal "drwxrwxr-x" "$(mode_of "$outside/dir")" "repair leaves a linked directory unchanged"
assert_equal "prw-rw--w-" "$(mode_of "$HYDRA_HOME/shared-pipe")" "repair leaves entries that are not files or directories"

rm -f "$HYDRA_HOME/shared-pipe" "$HYDRA_HOME/link-file" "$HYDRA_HOME/link-dir"
assert_equal "" "$(writable "$HYDRA_HOME")" "no writable state remains after repair"
"$HYDRA_BIN" admission status --json >/dev/null 2>&1
assert_success $? "admission accepts the repaired home"
"$HYDRA_BIN" workflow statistics-data 2>/dev/null |
    awk -F '\t' -v id="$run" '$1 == "R" && $2 == id && $14 != "-" { found = 1 } END { exit !found }'
assert_success $? "statistics count the repaired run record"

"$HYDRA_BIN" kill umask-head >/dev/null 2>&1 || true
"$HYDRA_BIN" kill umask-workflow >/dev/null 2>&1 || true

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
