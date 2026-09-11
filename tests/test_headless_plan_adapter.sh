#!/bin/sh
# Public compiled adapter plan, exact artifact, and dependent verifier without tmux.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
fixture="$(mktemp -d)"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1
cleanup() {
    (cd "$fixture/repo" && "$root/bin/hydra" kill plan-smoke --force) >/dev/null 2>&1 || :
    if [ "${passed:-0}" = 1 ]; then rm -rf "$fixture"; else printf 'Headless adapter plan evidence: %s\n' "$fixture" >&2; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
mkdir "$fixture/repo"
cp "$root/tests/fixtures/plan/repo/check.sh" "$root/tests/fixtures/plan/repo/compose.sh" "$fixture/repo/"
printf exact_adapter_artifact > "$fixture/repo/expected.txt"
printf 'ARTIFACT=exact_adapter_artifact\n' > "$fixture/repo/prompt.txt"
cat > "$fixture/profile.json" <<JSON
{"schema_version":1,"executable":"$root/tests/fixtures/agents/echo-worker.sh","argv":["--new",{"input":"session_id"}],"prompt":"stdin","session":"generated","adapter":"canonical-jsonl","probe_argv":["--help"],"probe_tokens":["echo-worker-v1"]}
JSON
"$root/bin/hydra" agent import fixture "$fixture/profile.json" >/dev/null
fixture_json adapter-plan "$root" "$fixture"
cd "$fixture/repo"
git init -q
git config user.name Test
git config user.email test@example.invalid
git add .
git -c commit.gpgSign=false commit -qm fixture
"$root/bin/hydra" init --no-agent --trust >/dev/null
# An omitted mode still requests a terminal and must fail before any head or
# workflow run is created on this receiver.
"$root/bin/hydra" workflow plan compile "$root/tests/fixtures/plan/plan.json" "$root/tests/fixtures/plan/policy.json" "$fixture/interactive.json" > "$fixture/interactive-compile.json"
interactive_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/interactive-compile.json")"
if "$root/bin/hydra" workflow plan run "$fixture/interactive.json" --accept "$interactive_digest" > "$fixture/interactive.out" 2> "$fixture/interactive.err"; then exit 1; fi
grep -q 'tmux is not installed' "$fixture/interactive.err"
[ ! -s "$fixture/interactive.out" ]
[ -z "$(find "$HYDRA_HOME/state/v2/projects" -name terminal-mode)" ]
"$root/bin/hydra" workflow plan compile "$fixture/plan.json" "$fixture/policy.json" "$fixture/compiled.json" > "$fixture/compile.json"
digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$fixture/compile.json")"
"$root/bin/hydra" workflow plan run "$fixture/compiled.json" --accept "$digest" > "$fixture/run.out"
run="$(sed -n '1p' "$fixture/run.out")"
"$root/bin/hydra" workflow plan result "$run" > "$fixture/result.json"
grep -q '"ok":true' "$fixture/result.json"
run_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$run")"
cmp expected.txt "$run_dir/steps/compose/attempt-1/artifacts/report"
grep -q '"verdict":"pass"' "$run_dir/delivery.json"
[ "$(find "$HYDRA_HOME/state/v2/projects" -name terminal-mode -exec cat {} \; | sort -u)" = headless ]
passed=1
printf '%s\n' 'Compiled headless adapter plan: bound profile -> exact artifact -> dependent verifier passed'
