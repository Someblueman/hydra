#!/bin/sh
# hydra init keeps ordinary onboarding out of the source tree: registration is
# host-local, shared config is an explicit opt-in, and the host-local record an
# earlier release left in the tree is ignored where it changes nothing and
# refused by name where it would have decided the worktree root.
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

# --- .hydra/local.yml, the host-local record of Hydra 2.5 or earlier ---
# Releases up to 4.0.0 took the worktree root from it. With a root recorded
# for this host the file changes nothing: init goes on and says nothing.
new_repo "$fixture/legacy"
out_without="$("$HYDRA_BIN" init --no-agent 2>&1)" || exit 1
root_before="$(sed -n '1p' .git/hydra/worktree-root)"
mkdir -p .hydra
printf 'version: 1\nworktree_root: %s\n' "$fixture/legacy-root" > .hydra/local.yml
cp .hydra/local.yml "$fixture/local-before"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init goes on with .hydra/local.yml when a worktree root is recorded"
assert_equal "$out_without" "$out" "init prints what it prints without the file"
assert_equal "$root_before" "$(sed -n '1p' .git/hydra/worktree-root)" "the recorded worktree root is kept, not the one the file names"
assert_cond "the file is left as it was" cmp -s .hydra/local.yml "$fixture/local-before"
# A team repository that committed the file blocks nobody whose root is recorded.
git add .hydra/local.yml
git commit -qm 'a tracked local.yml'
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "init goes on with a tracked .hydra/local.yml when a worktree root is recorded"
assert_equal "$out_without" "$out" "init says nothing about a tracked file either"
assert_equal "" "$(git status --porcelain)" "init leaves the tracked file and the tree unchanged"
json="$("$HYDRA_BIN" init --no-agent --json 2>&1)"
case "$json" in
    '{"schema_version":1,"ok":true,"command":"init","data":{"project_id":"project_'*'","worktree_root":"'"$root_before"'","trusted":false}}') assert_success 0 "init --json succeeds with the recorded root and nothing on stderr" ;;
    *) assert_success 1 "init --json succeeds with the recorded root and nothing on stderr"; echo "  Actual: $json" ;;
esac

# Without a recorded root the file would have decided where worktrees go:
# init stops before it registers anything.
new_repo "$fixture/legacy-unregistered"
mkdir -p .hydra
printf 'version: 1\nworktree_root: %s\n' "$fixture/legacy-root" > .hydra/local.yml
cp .hydra/local.yml "$fixture/local-before"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_equal 1 "$?" "init stops on .hydra/local.yml when no worktree root is recorded"
assert_equal "Error: .hydra/local.yml is a host-local record from Hydra 2.5 or earlier, which this release does not read
Next: run 'hydra init' with Hydra 4.0.0 first, which imports its worktree root, or delete the file" "$out" "the refusal names the file and both ways out, and prints nothing else"
assert_cond "the refused record is left as it was" cmp -s .hydra/local.yml "$fixture/local-before"
assert_cond "a refused first init registers nothing" test ! -e .git/hydra
json="$("$HYDRA_BIN" init --no-agent --trust --json 2>/dev/null)"
assert_equal 1 "$?" "init --json stops on .hydra/local.yml when no worktree root is recorded"
case "$json" in
    '{"schema_version":1,"ok":false,"command":"init","error":{"code":"unsupported_configuration","message":".hydra/local.yml is a host-local record from Hydra 2.5 or earlier, which this release does not read","recovery":'*) assert_success 0 "init --json reports the refusal as one error object" ;;
    *) assert_success 1 "init --json reports the refusal as one error object"; echo "  Actual: $json" ;;
esac
assert_cond "a refused init --json registers nothing" test ! -e .git/hydra
# A root given on the command line leaves the file nothing to decide, as in
# the releases that imported it.
out="$("$HYDRA_BIN" init --no-agent --worktree-root "$fixture/given-root" 2>&1)"
assert_success $? "init --worktree-root goes on with .hydra/local.yml and no recorded root"
assert_equal "$fixture/given-root" "$(sed -n '1p' .git/hydra/worktree-root)" "the given worktree root is recorded"
assert_not_contains "$out" "local.yml" "init --worktree-root says nothing about the file"
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_success $? "later inits go on with the root now recorded"
assert_contains "$out" "Worktrees go in $fixture/given-root." "the recorded root stays in use"

# The file is no part of what a host approves: its presence, content and
# absence leave an approval as it is.
new_repo "$fixture/legacy-trusted"
mkdir -p .hydra
printf 'version: 1\nsetup:\n  - touch "%s/legacy-setup-ran"\n' "$fixture" > .hydra/config.yml
out="$("$HYDRA_BIN" init --no-agent --trust 2>&1)" || exit 1
assert_contains "$out" "Repository config trusted." "the configuration is approved before the file appears"
printf 'version: 1\nworktree_root: %s\n' "$fixture/legacy-root" > .hydra/local.yml
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_contains "$out" "Repository config trusted." "adding .hydra/local.yml leaves the approval standing"
"$HYDRA_BIN" spawn legacy-trusted-head --no-agent --dry-run >/dev/null 2>&1
assert_success $? "spawn accepts the approved configuration beside .hydra/local.yml"
printf 'version: 1\nworktree_root: %s\n' "$fixture/another-root" > .hydra/local.yml
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_contains "$out" "Repository config trusted." "changing .hydra/local.yml leaves the approval standing"
rm .hydra/local.yml
out="$("$HYDRA_BIN" init --no-agent 2>&1)"
assert_contains "$out" "Repository config trusted." "deleting .hydra/local.yml leaves the approval standing"

# The config.yml stub the same releases generated is ordinary configuration:
# it is kept, and it needs approval like any other.
new_repo "$fixture/legacy-stub"
mkdir -p .hydra
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
