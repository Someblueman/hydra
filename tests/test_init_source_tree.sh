#!/bin/sh
# hydra init keeps ordinary onboarding out of the source tree: registration is
# host-local, shared config is an explicit opt-in, and the host-local record an
# earlier release left in the tree is refused by name.
set -u
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
HYDRA_BIN="$root/bin/hydra"

fixture="$(test_mktemp_dir)"
trap 'tmux kill-session -t parsed-head 2>/dev/null || true; rm -rf "$fixture"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HOME="$fixture"
export HYDRA_HOME="$fixture/home"
export HYDRA_NONINTERACTIVE=1
unset HYDRA_ROOT
(umask 077; mkdir -p "$HYDRA_HOME")

assert_contains() {
    case "$1" in
        *"$2"*) assert_success 0 "$3" ;;
        *) assert_success 1 "$3"; echo "  Text does not contain: '$2'"; echo "  Actual: '$1'" ;;
    esac
}

assert_not_contains() {
    case "$1" in
        *"$2"*) assert_success 1 "$3"; echo "  Text contains: '$2'"; echo "  Actual: '$1'" ;;
        *) assert_success 0 "$3" ;;
    esac
}

# Usage: assert_cond <message> <command...>
assert_cond() {
    _ac_message="$1"
    shift
    if "$@"; then assert_success 0 "$_ac_message"; else assert_success 1 "$_ac_message"; fi
}

new_repo() {
    mkdir -p "$1"
    cd "$1" || exit 1
    git init -q
    git config user.name Test
    git config user.email test@example.com
    printf 'base\n' > tracked.txt
    git add tracked.txt
    git commit -qm init
}

echo "Running init source-tree tests..."
echo "================================="

# --- a clean repository stays clean across every flag combination ---
new_repo "$fixture/clean"
exclude_before="$(cat .git/info/exclude)"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init --no-agent succeeds"
assert_contains "$out" "Ready: clean (agent: none)" "init reports readiness with the repository name"
assert_contains "$out" "No repository config." "init without shared config says so"
assert_not_contains "$(printf '%s\n' "$out" | sed -n '1p')" "project id" "primary line carries no project id label"
assert_contains "$out" "  project id: project_" "project id is on the detail line"
assert_equal "" "$(git status --porcelain)" "init --no-agent leaves git status unchanged"
for args in "--no-agent --trust" "--worktree-root $fixture/custom-worktrees" "--no-agent --trust --json" ""; do
    # shellcheck disable=SC2086
    "$HYDRA_BIN" init $args >/dev/null 2>&1
    assert_success $? "init ${args:-(reopen)} succeeds"
    assert_equal "" "$(git status --porcelain)" "init ${args:-(reopen)} leaves git status unchanged"
done
assert_equal "$exclude_before" "$(cat .git/info/exclude)" "init never edits .git/info/exclude"
assert_cond "init creates no .hydra directory" test ! -e .hydra
assert_equal "$fixture/custom-worktrees" "$(sed -n '1p' .git/hydra/worktree-root)" "reopen keeps the configured worktree root"
out="$("$HYDRA_BIN" init --worktree-root "$fixture/custom-worktrees" 2>&1)"
assert_not_contains "$(printf '%s\n' "$out" | sed -n '1p')" "project_" "primary line holds no raw project id with an explicit root"
json="$("$HYDRA_BIN" init --no-agent --json)"
case "$json" in
    '{"schema_version":1,"ok":true,"command":"init","data":{"project_id":"project_'*'","profile":"none","worktree_root":"'*'","trusted":false}}') assert_success 0 "init --json envelope fields are unchanged" ;;
    *) assert_success 1 "init --json envelope fields are unchanged"; echo "  Actual: $json" ;;
esac

# --- committed shared configuration is still parsed and still needs trust ---
new_repo "$fixture/committed"
mkdir -p .hydra
printf 'version: 1\nsetup:\n  - touch %s\n' "$fixture/setup-ran" > .hydra/config.yml
git add .hydra/config.yml
git commit -qm shared-config
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init succeeds with committed shared config"
assert_contains "$out" "Repository config not trusted." "init reports missing trust"
assert_contains "$out" "hydra init --trust" "init names the trust step"
assert_equal "" "$(git status --porcelain)" "init leaves a committed config untouched"
"$HYDRA_BIN" spawn parsed-head --no-agent >/dev/null 2>&1
assert_failure $? "fewer setup steps do not imply trust: untrusted setup refuses to spawn"
assert_cond "untrusted setup commands do not run" test ! -e "$fixture/setup-ran"
git branch -D parsed-head >/dev/null 2>&1 || true
out="$("$HYDRA_BIN" init --no-agent --trust 2>&1)"
assert_success $? "init --trust approves the committed config"
assert_contains "$out" "Repository config trusted." "init reports the approval"
assert_equal "" "$(git status --porcelain)" "trusting leaves git status unchanged"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_contains "$out" "Repository config trusted." "reopen reports the standing approval"
"$HYDRA_BIN" spawn parsed-head --no-agent >/dev/null 2>&1
assert_success $? "trusted committed config spawns"
assert_cond "committed setup commands are still parsed and run" test -e "$fixture/setup-ran"
"$HYDRA_BIN" kill parsed-head >/dev/null 2>&1 || true

# --- the host-local record of Hydra 2.5 or earlier is refused, never imported ---
new_repo "$fixture/legacy"
"$HYDRA_BIN" init --no-agent >/dev/null 2>&1 || exit 1
root_before="$(sed -n '1p' .git/hydra/worktree-root)"
mkdir -p .hydra
printf 'version: 1\nworktree_root: %s\n' "$fixture/legacy-root" > .hydra/local.yml
cp .hydra/local.yml "$fixture/local-before"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_equal 1 "$?" "init stops on .hydra/local.yml"
assert_equal "Error: .hydra/local.yml is a host-local record from Hydra 2.5 or earlier, which this release does not read
Next: run 'hydra init' with Hydra 4.0.0 first, which imports its worktree root, or delete the file" "$out" "the refusal names the file and both ways out, and prints nothing else"
assert_cond "the refused record is left as it was" cmp -s .hydra/local.yml "$fixture/local-before"
assert_equal "$root_before" "$(sed -n '1p' .git/hydra/worktree-root)" "the registered worktree root is unchanged"
json="$("$HYDRA_BIN" init --no-agent --trust --worktree-root "$fixture/other-root" --json 2>/dev/null)"
assert_equal 1 "$?" "init --json stops on .hydra/local.yml whatever the options"
case "$json" in
    '{"schema_version":1,"ok":false,"command":"init","error":{"code":"unsupported_configuration","message":".hydra/local.yml is a host-local record from Hydra 2.5 or earlier, which this release does not read","recovery":'*) assert_success 0 "init --json reports the refusal as one error object" ;;
    *) assert_success 1 "init --json reports the refusal as one error object"; echo "  Actual: $json" ;;
esac
assert_equal "$root_before" "$(sed -n '1p' .git/hydra/worktree-root)" "a refused init registers no other worktree root"
rm .hydra/local.yml
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init succeeds once the file is deleted"
assert_contains "$out" "Worktrees go in $root_before." "the registered worktree root is still in use"
new_repo "$fixture/legacy-unregistered"
mkdir -p .hydra
printf 'version: 1\nworktree_root: %s\n' "$fixture/legacy-root" > .hydra/local.yml
"$HYDRA_BIN" init --no-agent >/dev/null 2>&1
assert_equal 1 "$?" "init stops on .hydra/local.yml in a repository it never registered"
assert_cond "a refused first init registers nothing" test ! -e .git/hydra
# The config.yml stub the same releases generated is ordinary configuration:
# it is kept, and it needs approval like any other.
rm .hydra/local.yml
printf 'version: 1\nprofile: none\nsetup:\n' > .hydra/config.yml
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init accepts a repository with a generated config.yml stub"
assert_cond "the stub is kept" test -f .hydra/config.yml
assert_contains "$out" "Repository config not trusted." "the stub needs approval like any configuration"

# --- shared configuration is an explicit opt-in ---
new_repo "$fixture/shared"
exclude_before="$(cat .git/info/exclude)"
out="$("$HYDRA_BIN" init --no-agent --write-shared-config 2>&1)"
assert_success $? "init --write-shared-config succeeds"
assert_contains "$out" "Writing .hydra/config.yml" "shared config write is previewed"
assert_contains "$out" "commit it" "shared config explains it is meant to be committed"
assert_contains "$out" "  | version: 1" "preview shows the content"
assert_equal "?? .hydra/" "$(git status --porcelain)" "only the shared config appears in git status"
assert_equal "config.yml" "$(ls -A .hydra)" "only config.yml is written"
assert_equal "$exclude_before" "$(cat .git/info/exclude)" "shared config write leaves .git/info/exclude unchanged"
"$HYDRA_BIN" init --no-agent --write-shared-config >/dev/null 2>&1
assert_failure $? "an existing config.yml is not replaced without --force"
json="$("$HYDRA_BIN" init --no-agent --trust --write-shared-config --force --json 2>/dev/null)"
case "$json" in
    '{"schema_version":1,"ok":true,"command":"init","data":{"project_id":"project_'*'","trusted":true}}') assert_success 0 "--force replaces and --trust approves the written config" ;;
    *) assert_success 1 "--force replaces and --trust approves the written config"; echo "  Actual: $json" ;;
esac
"$HYDRA_BIN" spawn shared-head --no-agent --dry-run >/dev/null 2>&1
assert_success $? "the written template is trusted and inert"

printf 'Tests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
