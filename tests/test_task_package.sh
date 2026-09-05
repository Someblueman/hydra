#!/bin/sh
# Actual Git bundles and public CLI; preparation must preserve the source checkout.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
HYDRA_HOME="$fixture/home"
HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
export HYDRA_HOME HYDRA_FLEET_BIN
mkdir "$fixture/source"
git -C "$fixture/source" init -q
git -C "$fixture/source" config user.name 'Task test'
git -C "$fixture/source" config user.email task@example.invalid
printf 'committed source\n' > "$fixture/source/code"
git -C "$fixture/source" add code
git -C "$fixture/source" commit -qm initial
commit="$(git -C "$fixture/source" rev-parse HEAD)"
printf 'local dirty work\n' > "$fixture/source/code"
printf 'selected binary context\000tail\n' > "$fixture/source/context"
printf 'unrelated private context\n' > "$fixture/source/private"
git -C "$fixture/source" status --porcelain > "$fixture/before"
cat > "$fixture/spec" <<EOF
{"schema_version":1,"host":"build","project":"/srv/task project",
"source":{"commit":"$commit"},
"work":{"kind":"exec","argv":["printf","%s","literal ; \$(not-a-command)"]},
"inputs":["context"],"outputs":["result.txt"],"capabilities":["exec"],
"completion":"command-exit",
"limits":{"transport_seconds":30,"queue_seconds":60,"startup_seconds":60,
"execution_seconds":120,"cancellation_seconds":10,"log_bytes":4096,"artifact_bytes":4096}}
EOF
task() { "$root/bin/hydra" fleet task "$@"; }
task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/package" > "$fixture/preview"
grep -q '"transfer_bytes":' "$fixture/preview"
grep -q '"path":"context"' "$fixture/preview"
grep -q 'task project' "$fixture/preview"
if grep -q 'unrelated private context\|local dirty work' "$fixture/package"; then exit 1; fi
task inspect --input "$fixture/package" > "$fixture/inspected"
grep -q '"ok":true' "$fixture/inspected"
git -C "$fixture/source" status --porcelain > "$fixture/after"
cmp "$fixture/before" "$fixture/after"
[ "$(cat "$fixture/source/code")" = 'local dirty work' ]
if task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/package" > "$fixture/error"; then exit 1; fi
grep -q '"code":"io_failed"' "$fixture/error"
# Changing bytes or immutable fields without rebinding checksums must fail.
sed 's/"bundle_hex":"./"bundle_hex":"z/' "$fixture/package" > "$fixture/bad"
if task inspect --input "$fixture/bad" > "$fixture/error"; then exit 1; fi
grep -q '"code":"invalid_package"' "$fixture/error"
sed 's/"input_hex":\["./"input_hex":["f/' "$fixture/package" > "$fixture/bad"
if task inspect --input "$fixture/bad" >/dev/null; then exit 1; fi
cp "$fixture/package" "$fixture/bad"
printf '\000trailing data' >> "$fixture/bad"
if task inspect --input "$fixture/bad" >/dev/null; then exit 1; fi
sed 's/"host":"build"/"host":"other"/' "$fixture/package" > "$fixture/bad"
if task inspect --input "$fixture/bad" >/dev/null; then exit 1; fi
for path in '../outside' '/absolute' '.git/config' 'context/../private'; do
    sed "s|\[\"context\"\]|[\"$path\"]|" "$fixture/spec" > "$fixture/bad-spec"
    if task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/no-package" >/dev/null; then exit 1; fi
done
mv "$fixture/source/context" "$fixture/context"
ln -s "$fixture/context" "$fixture/source/context"
if task prepare --source "$fixture/source" --spec "$fixture/spec" --output "$fixture/no-package" >/dev/null; then exit 1; fi
rm "$fixture/source/context"
mkdir "$fixture/contexts"
cp "$fixture/context" "$fixture/contexts/data"
ln -s "$fixture/contexts" "$fixture/source/context"
sed 's/\["context"\]/["context\/data"]/' "$fixture/spec" > "$fixture/bad-spec"
if task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/no-package" >/dev/null; then exit 1; fi
rm "$fixture/source/context"
cp "$fixture/context" "$fixture/source/context"
# Workflow preparation binds a real regular file without interpreting or running it.
mkdir -p "$fixture/source/.hydra/workflows"
printf 'version: 1\nid: build\n' > "$fixture/source/.hydra/workflows/build.yml"
git -C "$fixture/source" add .hydra/workflows/build.yml
git -C "$fixture/source" commit -qm workflow
workflow_commit="$(git -C "$fixture/source" rev-parse HEAD)"
sed -e "s/$commit/$workflow_commit/" \
    -e '/"work":/c\
"work":{"kind":"workflow","path":".hydra/workflows/build.yml"},' \
    -e 's/command-exit/workflow-success/' "$fixture/spec" > "$fixture/workflow-spec"
task prepare --source "$fixture/source" --spec "$fixture/workflow-spec" --output "$fixture/workflow-package" >/dev/null
task inspect --input "$fixture/workflow-package" >/dev/null
sed 's@workflows/build.yml@workflows/*.yml@' "$fixture/workflow-spec" > "$fixture/bad-spec"
if task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/no-package" >/dev/null; then exit 1; fi
# Gitlinks and LFS pointers are rejected before producing a partial source package.
git -C "$fixture/source" update-index --add --cacheinfo "160000,$commit,dependency"
git -C "$fixture/source" commit -qm gitlink
next="$(git -C "$fixture/source" rev-parse HEAD)"
sed "s/$commit/$next/" "$fixture/spec" > "$fixture/bad-spec"
if task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/no-package" > "$fixture/error"; then exit 1; fi
grep -q 'source_preflight_failed' "$fixture/error"
git -C "$fixture/source" update-index --force-remove dependency
printf 'version https://git-lfs.github.com/spec/v1\noid sha256:abc\nsize 3\n' > "$fixture/source/large"
git -C "$fixture/source" add large
git -C "$fixture/source" commit -qm lfs
next="$(git -C "$fixture/source" rev-parse HEAD)"
sed "s/$commit/$next/" "$fixture/spec" > "$fixture/bad-spec"
if task prepare --source "$fixture/source" --spec "$fixture/bad-spec" --output "$fixture/no-package" > "$fixture/error"; then exit 1; fi
grep -q 'source_preflight_failed' "$fixture/error"
printf 'Task package CLI: exact source, binary inputs, checksums, path safety, preservation, and source preflight passed\n'
