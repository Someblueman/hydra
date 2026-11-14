#!/bin/sh
# The short planning flow on the command line: an agent proposes, the user
# shows, approves, follows and reviews, with nothing copied by hand. Approval
# binds the exact revision shown and is refused from an agent's environment.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
root="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
export HYDRA_HOME="$root/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
# Result reviews query Git; container-wide configuration does not belong here.
unset GIT_CONFIG_COUNT GIT_CONFIG_PARAMETERS
# shellcheck source=/dev/null
. "$REPO/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
bundle="$REPO/tests/fixtures/plan-bundle"
cleanup() {
    if [ -x "$root/bin/tmux" ]; then
        for head in planner greeting-work; do
            (cd "$root/repo" && PATH="$root/bin:$PATH" "$HYDRA_BIN" kill "$head" --force) >/dev/null 2>&1
        done
        PATH="$root/bin:$PATH" tmux kill-server >/dev/null 2>&1
    fi
    test_stop_processes_naming "$root" >/dev/null 2>&1 || :
    rm -rf "$root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$root/bin" "$root/repo"
cp "$bundle/codex" "$root/bin/codex"
printf '#!/bin/sh\nexec "%s" -S "%s" -f /dev/null "$@"\n' "$(command -v tmux)" "$root/tmux.sock" > "$root/bin/tmux"
chmod +x "$root/bin/tmux" "$root/bin/codex"
PATH="$root/bin:$PATH"
export PATH
hydra() { "$HYDRA_BIN" "$@"; }
# Answers an interactive prompt on a terminal.
on_terminal() {
    _answer="$1"; shift
    case "$(uname -s)" in
        Darwin) printf '%s\n' "$_answer" | (unset HYDRA_NONINTERACTIVE; script -q "$root/tty.log" "$@") ;;
        *) printf '%s\n' "$_answer" | (unset HYDRA_NONINTERACTIVE; script -q -e -c "$*" "$root/tty.log") ;;
    esac
}
cp "$bundle/repo/Makefile" "$bundle/repo/README" "$root/repo/"
cd "$root/repo" || exit 1
git init -q && git config user.name Test && git config user.email test@example.invalid
git add . && git -c commit.gpgSign=false commit -qm base
hydra init --no-agent --trust >/dev/null
(unset HYDRA_SKIP_AI; hydra spawn planner --profile codex >/dev/null 2>&1)
assert_success $? 'a planning head starts with the codex profile'
project="$(cat .git/hydra/project-id)"
runs="$HYDRA_HOME/state/v2/projects/$project/workflows/runs"
launches="$HYDRA_HOME/state/v2/projects/$project/workflows/launches"
worktree="$(hydra path planner)"
simple="$REPO/tests/fixtures/plan-simple/draft.json"
publish() { (cd "$worktree" && hydra plan propose "$1"); }

hydra plan schema > "$root/schema.txt"
grep -q '"plan": "simple-1"' "$root/schema.txt" && [ "$(wc -l < "$root/schema.txt")" -le 60 ]
assert_success $? 'the short plan format fits on one screen'

# The agent learns what to fix when it publishes.
sed -e 's/"plan": "simple-1",/"plan": "simple-1", "worker": "greeting-work",/' \
    -e 's/"run": \["make", "check"\]/"run": ["npm", "test"]/' "$simple" > "$worktree/plan.json"
publish plan.json > "$root/bad.out"
assert_success $? 'a draft that breaks the policy is still saved'
grep -q 'as revision 1, but it does not pass' "$root/bad.out" &&
    grep -q 'the tool npm is not allowed' "$root/bad.out" && grep -q 'hydra plan policy --allow-tool npm' "$root/bad.out"
assert_success $? 'publication names the tool the policy lacks and how to allow it'
hydra plan show > "$root/bad-show.out" 2>&1
assert_failure $? 'a plan outside the policy cannot be shown for approval'
grep -q 'cannot be approved' "$root/bad-show.out"
assert_success $? 'the user sees why it cannot be approved'
sed 's/"plan": "simple-1",/"plan": "simple-1", "worker": "greeting-work",/' "$simple" > "$worktree/plan.json"
publish plan.json > "$root/good.out"
grep -q 'as revision 2; it passes the project' "$root/good.out"
assert_success $? 'a valid revision is accepted at publication'

hydra plan show > "$root/show.out"
assert_success $? 'the user shows the only proposal without naming it'
grep -q 'Proposed by planner, revision 2' "$root/show.out"
assert_success $? 'the review names the revision'
# The readable plan: steps in words, the worker's instructions in full, the
# command, what may change, limits in minutes and what counts as done.
for text in 'What runs, in order:' '  1. spawn-worker: Create the worker branch greeting-work (no terminal)' \
    '  2. implement: The codex agent works on greeting-work, for at most 30 minutes; then Hydra commits what it changed' \
    '       | Create greeting.txt with a friendly greeting and tests/greeting.sh that fails unless it is non-empty.' \
    '  3. check: Runs make check on greeting-work, for at most 10 minutes' \
    '  done-1  make check passes with the new test  (checked by check)' \
    'Changes: only branch greeting-work, which this plan creates. Your checkout and other branches are not touched.' \
    'Limits: at most 40 minutes in total; 1 head; 1 step at a time; no retries' \
    'Policy (bound to this approval): tools sh, git, make, profile:codex; at most 60 minutes and 4 heads per plan'; do
    grep -Fqx "$text" "$root/show.out" || { printf 'missing summary line: %s\n' "$text" >&2; false; }
    assert_success $? "the plan summary shows: $text"
done
if grep -q 'obligations\|legacy_projection\|Source fingerprint' "$root/show.out"; then check=1; else check=0; fi
assert_success "$check" 'the summary leaves the machinery to the technical view'
hydra plan show --technical | grep -q '^Acceptance digest: [0-9a-f]\{64\}$'
assert_success $? 'the technical preview remains available'
hydra plan show --json > "$root/show.json"
digest="$(sed -n 's/.*"sha256":"\([0-9a-f]\{64\}\)".*/\1/p' "$root/show.json" | head -n 1)"
assert_equal 64 "${#digest}" 'automation reads the revision digest'
grep -q '"schema_version":1,"plan":"add-a-greeting-file-and"' "$root/show.json" &&
    grep -q '"kind":"agent"' "$root/show.json" && grep -q '"only_new_branches":true' "$root/show.json"
assert_success $? 'the summary has a stable machine-readable form'

# The agent cannot approve its own plan: a guard, not a security boundary.
HYDRA_HEAD_ID=head_00000000000000000000 hydra plan approve --accept "$digest" > "$root/agent.out" 2>&1
assert_failure $? 'approval is refused from an agent head environment'
grep -q "inside an agent's head" "$root/agent.out"
assert_success $? 'the refusal says why'
HYDRA_INSTANCE_ID=instance_00000000000000000000 hydra workflow plan run "$root/none.json" --accept "$digest" > "$root/agent-run.out" 2>&1
assert_failure $? 'running a plan is refused from an agent instance environment'
grep -q agent_environment "$root/agent-run.out" || grep -q "inside an agent's head" "$root/agent-run.out"
assert_success $? 'the expert run command gives the same reason'
if [ ! -d "$runs" ] && [ ! -d "$launches" ]; then check=0; else check=1; fi
assert_success "$check" 'nothing was launched'

hydra plan approve < /dev/null > "$root/notty.out" 2>&1
assert_failure $? 'approval without a terminal needs the digest'
hydra plan approve --accept "$(printf '%064d' 0)" > "$root/wrong.out" 2>&1
assert_failure $? 'approval of another digest is refused'
grep -q 'changed since it was reviewed' "$root/wrong.out"
assert_success $? 'the refusal says the plan is not the reviewed one'

# A changed plan invalidates the approval of the earlier revision.
sed -e 's/"plan": "simple-1",/"plan": "simple-1", "worker": "greeting-work",/' \
    -e 's/Add a greeting file and a test for it/Add a greeting file and a test for it, politely/' "$simple" > "$worktree/plan.json"
publish plan.json >/dev/null
hydra plan approve --accept "$digest" > "$root/stale.out" 2>&1
assert_failure $? 'the digest of revision 2 does not approve revision 3'
if [ ! -d "$runs" ]; then check=0; else check=1; fi
assert_success "$check" 'no run exists after refused approvals'

on_terminal n "$HYDRA_BIN" plan approve > "$root/declined.out" 2>&1
grep -q 'Run revision 3 of this plan? \[y/N\]' "$root/declined.out" && grep -q 'Not approved; nothing ran.' "$root/declined.out"
assert_success $? 'declining at the prompt runs nothing'
on_terminal y "$HYDRA_BIN" plan approve > "$root/approved.out" 2>&1
run="$(sed -n 's/.*Run \(run_[0-9a-f]*\) started.*/\1/p' "$root/approved.out" | head -n 1)"
if [ -n "$run" ]; then check=0; else check=1; cat "$root/approved.out" >&2; fi
assert_success "$check" 'one keypress approves the revision shown and starts its run'
tries=0
while [ "$tries" -lt 90 ]; do
    case "$(sed -n '1p' "$runs/$run/state" 2>/dev/null)" in succeeded|failed|cancelled|recovery-required) break ;; esac
    tries=$((tries + 1)); sleep 1
done
assert_equal succeeded "$(sed -n '1p' "$runs/$run/state" 2>/dev/null)" 'the approved plan runs to success in the background'
assert_equal planner "$(sed -n '1p' "$runs/$run/planning-branch" 2>/dev/null)" 'the run belongs to the planning conversation'
hydra plan status > "$root/status.out"
grep -q "Result ready: hydra plan review $run" "$root/status.out"
assert_success $? 'status finds the latest plan run and says the result is ready'
hydra plan review > "$root/review.out"
assert_success $? 'the review needs no identifiers'
grep -q 'verdict PASS (verified)' "$root/review.out" && grep -q 'make check exited 0 on commit' "$root/review.out"
assert_success $? 'the review shows the verified check bound to the worker commit'
on_terminal y "$HYDRA_BIN" plan approve > "$root/again.out" 2>&1
grep -q "already ran as $run" "$root/again.out"
assert_success $? 'an approved revision runs at most once'

# The project's plan policy: shown in words, edited by the user only.
hydra plan policy > "$root/policy.out"
grep -q 'Tools: sh, git, make' "$root/policy.out" && grep -q 'only inside the branches a plan creates' "$root/policy.out"
assert_success $? 'the policy is shown in words'
HYDRA_HEAD_ID=head_00000000000000000000 hydra plan policy --allow-tool npm >/dev/null 2>&1
assert_failure $? 'an agent cannot widen the policy'
hydra plan policy --allow-tool npm >/dev/null
sed -e 's/"plan": "simple-1",/"plan": "simple-1", "worker": "npm-work",/' \
    -e 's/"run": \["make", "check"\]/"run": ["npm", "test"]/' "$simple" > "$worktree/plan.json"
publish plan.json | grep -q 'it passes the project'
assert_success $? 'an allowed tool passes at publication'
hydra plan policy --reset >/dev/null
grep -q '"tools":\["sh","git","make"\]' "$HYDRA_HOME/state/v2/projects/$project/plan-policy.json"
assert_success $? 'reset restores the guided policy'
cd "$root" || exit 1
printf 'Total: %s\nPassed: %s\nFailed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
