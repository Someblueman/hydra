#!/bin/sh
# Repository trust must cover every executable configuration file.
set -u
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HYDRA_HOME="$fixture/home"
mkdir -p "$fixture/repo/.hydra/workflows"
cd "$fixture/repo" || exit 1
git init -q
cp "$root/examples/workflows/local-review.yml" .hydra/workflows/local.yml
"$root/bin/hydra" init --no-agent --trust >/dev/null || exit 1
"$root/bin/hydra" workflow validate local >/dev/null 2>&1
assert_success $? "approved nested local.yml workflow validates"
sed 's/argv: \[true\]/argv: [false]/' .hydra/workflows/local.yml > "$fixture/changed.yml"
mv "$fixture/changed.yml" .hydra/workflows/local.yml
"$root/bin/hydra" workflow validate local > "$fixture/result" 2>&1
assert_failure $? "nested local.yml change invalidates trust"
"$root/bin/hydra" init --no-agent --trust >/dev/null || exit 1
printf '\n# host-local setting\n' >> .hydra/local.yml
"$root/bin/hydra" workflow validate local >/dev/null 2>&1
assert_success $? "root host-local file remains excluded"
cp .hydra/workflows/local.yml "$fixture/external.yml"
ln -s "$fixture/external.yml" .hydra/workflows/linked.yml
"$root/bin/hydra" workflow validate linked > "$fixture/result" 2>&1
assert_failure $? "adding a linked workflow invalidates trust"
"$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
assert_failure $? "trust refuses linked repository files"
rm .hydra/workflows/linked.yml
mv .hydra/workflows "$fixture/workflows"
ln -s "$fixture/workflows" .hydra/workflows
"$root/bin/hydra" workflow validate local > "$fixture/result" 2>&1
assert_failure $? "workflow IDs cannot bypass trust through a linked directory"
"$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
assert_failure $? "trust refuses linked configuration directories"
rm .hydra/workflows
mv "$fixture/workflows" .hydra/workflows
mkfifo .hydra/pipe
"$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
assert_failure $? "trust refuses special files without blocking"
rm .hydra/pipe
"$root/bin/hydra" init --no-agent --trust >/dev/null || exit 1
"$root/bin/hydra" workflow validate local >/dev/null 2>&1
assert_success $? "regular configuration can be approved again"
mv .hydra "$fixture/config"
ln -s "$fixture/config" .hydra
printf '# preserve this destination\n' >> "$fixture/config/local.yml"
cp "$fixture/config/local.yml" "$fixture/local-before"
"$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
assert_failure $? "trust refuses a linked configuration root"
cmp -s "$fixture/config/local.yml" "$fixture/local-before"
assert_success $? "failed initialization preserves the linked destination"
rm .hydra
mv "$fixture/config" .hydra
bad_name=".hydra/line
break"
printf 'invalid name\n' > "$bad_name"
"$root/bin/hydra" init --no-agent --trust >/dev/null 2>&1
assert_failure $? "trust rejects newline-bearing filenames before serialization"
rm "$bad_name"
printf 'Tests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
