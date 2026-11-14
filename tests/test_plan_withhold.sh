#!/bin/sh
# Hydra commits what an agent step leaves on its worker head, but never a new
# file that looks like a secret: those stay untracked in the worktree and are
# listed in the attempt's "withheld" file.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home"
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
# shellcheck source=/dev/null
. "$REPO/lib/locks.sh"
# shellcheck source=/dev/null
. "$REPO/lib/state_v2.sh"
# shellcheck source=/dev/null
. "$REPO/lib/workflow_runtime.sh"
# shellcheck source=/dev/null
. "$REPO/lib/workflow_plan.sh"
test_count=0 pass_count=0 fail_count=0
trap 'rm -rf "$root"' EXIT

echo "Testing what Hydra leaves out of an agent step's commit"

for name in .env .env.local config/.env.production deploy.pem server.KEY id_rsa id_ed25519.pub \
    credentials credentials.json .netrc keys/store.p12; do
    if workflow_plan_secret_path "$name"; then status=0; else status=1; fi
    assert_success "$status" "'$name' looks like a secret"
done
for name in tokenizer.c src/secret_sharing.md .env.example config/credentials.example \
    keyboard.c tests/token_test.sh docs/keys.md; do
    if workflow_plan_secret_path "$name"; then status=1; else status=0; fi
    assert_success "$status" "'$name' is ordinary work"
done

tree="$root/worker" run="$root/run" attempt="$root/run/steps/implement/attempt-1"
mkdir -p "$tree" "$run" "$attempt"
(cd "$tree" && git init -q && git config user.name Test && git config user.email test@example.invalid &&
    printf 'base\n' > README && printf 'old\n' > .env.tracked && git add . &&
    git -c commit.gpgSign=false commit -qm base) || exit 1
printf 'plan-1\tadd-greeting\n' > "$run/graph.tsv"
printf 'run_1\n' > "$run/run-id"
mkdir -p "$tree/src" "$tree/config"
printf 'hello\n' > "$tree/src/greeting.c"
printf 'tokens\n' > "$tree/src/tokenizer.c"
printf 'API_KEY=1\n' > "$tree/.env"
printf 'private\n' > "$tree/config/deploy.pem"
printf 'new\n' > "$tree/.env.tracked"

unset HYDRA_WORKFLOW_OUTPUTS_DIR
if workflow_plan_commit "$run" implement "$tree" "$attempt"; then status=0; else status=1; fi
assert_success "$status" "the agent step is committed"
assert_equal 'Hydra plan add-greeting: implement' "$(git -C "$tree" log -1 --format=%s)" "the commit is Hydra's"
committed="$(git -C "$tree" show --name-only --format= HEAD | sort | tr '\n' ' ')"
assert_equal '.env.tracked src/greeting.c src/tokenizer.c ' "$committed" \
    "ordinary new files and changes to tracked files are committed"
assert_equal "$(printf '.env\nconfig/deploy.pem')" "$(sed -n '1,$p' "$attempt/withheld")" \
    "new files that look like secrets are listed as withheld"
assert_equal "$(printf '?? .env\n?? config/')" "$(git -C "$tree" status --porcelain)" \
    "withheld files stay in the worktree, untracked"
assert_equal "$(git -C "$tree" rev-parse HEAD)" "$(sed -n '1p' "$attempt/commit")" \
    "the attempt records the commit"

# A step that leaves only a secret commits nothing and records the head.
attempt2="$root/run/steps/implement/attempt-2"
mkdir -p "$attempt2"
before="$(git -C "$tree" rev-parse HEAD)"
printf 'more\n' > "$tree/id_ed25519"
if workflow_plan_commit "$run" implement "$tree" "$attempt2"; then status=0; else status=1; fi
assert_success "$status" "a step that left only a secret still succeeds"
assert_equal "$before" "$(git -C "$tree" rev-parse HEAD)" "nothing is committed for it"
if grep -qx 'id_ed25519' "$attempt2/withheld"; then status=0; else status=1; fi
assert_success "$status" "the secret is listed as withheld"

printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
