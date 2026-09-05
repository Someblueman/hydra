#!/bin/sh
# Sourced by test_task_acceptance.sh with its disposable receiver and task helper.
: "${fixture:?}" "${root:?}" "${commit:?}"
# Produce an actual committed result through the public task execution path.
sed -e 's|\["true"\]|["sh","-c","printf collected-result > result.txt; git add result.txt; git -c user.name=Collector -c user.email=collector@example.invalid commit -qm result"]|' \
    -e 's/"outputs":\[\]/"outputs":["result.txt"]/' "$fixture/spec" > "$fixture/collection-spec"
task prepare --source "$fixture/source" --spec "$fixture/collection-spec" --output "$fixture/collection-package" > "$fixture/collection-preview"
collection_digest="$(sed -n 's/.*"spec_sha256":"\([^"]*\)".*/\1/p' "$fixture/collection-preview")"
task submit build --input "$fixture/collection-package" --key collection --trust-spec "$collection_digest" > "$fixture/collection-receipt"
collection_task="$(sed -n 's/.*"task_id":"\([^"]*\)".*/\1/p' "$fixture/collection-receipt")"
collection_attempt=0
while [ "$collection_attempt" -lt 100 ]; do
    task status build --id "$collection_task" > "$fixture/collection-status"
    if grep -q '"result_state":"ready"' "$fixture/collection-status"; then break; fi
    if grep -q '"result_state":"unavailable"' "$fixture/collection-status"; then cat "$fixture/collection-status"; exit 1; fi
    sleep 0.1; collection_attempt=$((collection_attempt + 1))
done
grep -q '"result_state":"ready"' "$fixture/collection-status"
task result build --id "$collection_task" --output "$fixture/collection-result" >/dev/null
git clone -q "$fixture/source" "$fixture/collector"
printf 'dirty local work\n' >> "$fixture/collector/task-work.sh"
printf 'private untracked file\n' > "$fixture/collector/personal-note"
printf 'retained fetch evidence\n' > "$fixture/collector/.git/FETCH_HEAD"
git -C "$fixture/collector" status --porcelain=v1 > "$fixture/collection-before"
git -C "$fixture/collector" for-each-ref refs/heads > "$fixture/branches-before"
collection_index="$(git -C "$fixture/collector" write-tree)"
cp "$fixture/collector/task-work.sh" "$fixture/dirty-before"
task collect build --id "$collection_task" --into "$fixture/collector" > "$fixture/collected" || { cat "$fixture/collected"; exit 1; }
collection_id="$(sed -n 's/.*"collection_id":"\([^"]*\)".*/\1/p' "$fixture/collected" | sed 's@\\/@/@g')"
collection_path="$(sed -n 's/.*"path":"\([^"]*\)".*/\1/p' "$fixture/collected" | sed 's@\\/@/@g')"
collection_ref="$(sed -n 's/.*"ref":"\([^"]*\)".*/\1/p' "$fixture/collected" | sed 's@\\/@/@g')"
[ -n "$collection_id" ] && [ -n "$collection_ref" ]
printf collected-result > "$fixture/expected-result"
cmp "$fixture/expected-result" "$collection_path/artifacts/result.txt"
git -C "$fixture/collector" show "$collection_ref:result.txt" > "$fixture/collected-commit"
cmp "$fixture/expected-result" "$fixture/collected-commit"
: > "$fixture/transport/offline"
task collect --input "$fixture/collection-result" --into "$fixture/collector" > "$fixture/collected-again"
cmp "$fixture/collected" "$fixture/collected-again"
rm "$fixture/transport/offline"
git -C "$fixture/collector" status --porcelain=v1 > "$fixture/collection-after"
git -C "$fixture/collector" for-each-ref refs/heads > "$fixture/branches-after"
cmp "$fixture/collection-before" "$fixture/collection-after"
cmp "$fixture/branches-before" "$fixture/branches-after"
cmp "$fixture/dirty-before" "$fixture/collector/task-work.sh"
[ "$collection_index" = "$(git -C "$fixture/collector" write-tree)" ]
grep -q '^retained fetch evidence$' "$fixture/collector/.git/FETCH_HEAD"
grep -q '^private untracked file$' "$fixture/collector/personal-note"
# Linked checkouts share the common Git store but retain their own dirty files.
git -C "$fixture/collector" worktree add --quiet --detach "$fixture/linked-collector" HEAD
printf 'linked dirty work\n' >> "$fixture/linked-collector/task-work.sh"
printf 'linked untracked work\n' > "$fixture/linked-collector/local-note"
cp "$fixture/linked-collector/task-work.sh" "$fixture/linked-before"
task collect --input "$fixture/collection-result" --into "$fixture/linked-collector" > "$fixture/linked-collected"
cmp "$fixture/collected" "$fixture/linked-collected"
cmp "$fixture/linked-before" "$fixture/linked-collector/task-work.sh"
grep -q '^linked untracked work$' "$fixture/linked-collector/local-note"
git -C "$fixture/collector" worktree remove --force "$fixture/linked-collector"
# Incomplete private ref installation is recoverable without repeating remote work.
collection_commit="$(git -C "$fixture/collector" rev-parse "$collection_ref")"
git -C "$fixture/collector" update-ref -d "$collection_ref" "$collection_commit"
if task collected --id "$collection_id" --into "$fixture/collector" >/dev/null; then exit 1; fi
task collect --input "$fixture/collection-result" --into "$fixture/collector" > "$fixture/collected-repaired"
cmp "$fixture/collected" "$fixture/collected-repaired"
# A changed extraction directory must not redirect writes outside the collection.
mv "$collection_path/artifacts" "$collection_path/saved-artifacts"
mkdir "$fixture/collection-outside"
ln -s "$fixture/collection-outside" "$collection_path/artifacts"
if task collect --input "$fixture/collection-result" --into "$fixture/collector" >/dev/null; then exit 1; fi
[ ! -e "$fixture/collection-outside/result.txt" ]
rm "$collection_path/artifacts"
mv "$collection_path/saved-artifacts" "$collection_path/artifacts"
# User changes to private artifacts or refs are refused, not overwritten.
printf changed >> "$collection_path/artifacts/result.txt"
if task collect --input "$fixture/collection-result" --into "$fixture/collector" >/dev/null; then exit 1; fi
cp "$fixture/expected-result" "$collection_path/artifacts/result.txt"
git -C "$fixture/collector" update-ref "$collection_ref" "$commit" "$collection_commit"
if task collect --input "$fixture/collection-result" --into "$fixture/collector" >/dev/null; then exit 1; fi
[ "$(git -C "$fixture/collector" rev-parse "$collection_ref")" = "$commit" ]
git -C "$fixture/collector" update-ref "$collection_ref" "$collection_commit" "$commit"
git -C "$fixture/collector" symbolic-ref "$collection_ref" refs/heads/collection-must-not-create
if task collect --input "$fixture/collection-result" --into "$fixture/collector" >/dev/null; then exit 1; fi
if git -C "$fixture/collector" show-ref --verify --quiet refs/heads/collection-must-not-create; then exit 1; fi
git -C "$fixture/collector" update-ref --no-deref "$collection_ref" "$collection_commit"
task collected --id "$collection_id" --into "$fixture/collector" --format candidates > "$fixture/collection-candidates"
grep -q '^remote_head_' "$fixture/collection-candidates"
printf 'Collection: real result commit/artifact, offline repeat, dirty/index/ref preservation, and partial-install recovery passed\n'

# Remote task candidates use the existing assembly, gate, approval, and promotion flow.
git -C "$fixture/collector" restore task-work.sh
rm "$fixture/collector/personal-note"
git -C "$fixture/collector" config user.name Collector
git -C "$fixture/collector" config user.email collector@example.invalid
git -C "$fixture/collector" config commit.gpgSign false
(cd "$fixture/collector" && "$root/bin/hydra" init --no-agent --trust --worktree-root "$fixture/collection-heads" --json) >/dev/null
git -C "$fixture/collector" add .hydra
git -C "$fixture/collector" commit --allow-empty -qm local-baseline
collection_base="$(git -C "$fixture/collector" rev-parse HEAD)"
collection_target="$(git -C "$fixture/collector" symbolic-ref --short HEAD)"
(cd "$fixture/collector" && "$root/bin/hydra" integrate "task:$collection_id" --base "$collection_base" --target "$collection_target" --dry-run) > "$fixture/collection-plan"
grep -q 'candidate 1:' "$fixture/collection-plan"
collection_run="$(cd "$fixture/collector" && "$root/bin/hydra" integrate "task:$collection_id" --base "$collection_base" --target "$collection_target" --execute --gate 'test -s result.txt')"
[ "$(git -C "$fixture/collector" rev-parse HEAD)" = "$collection_base" ]
if (cd "$fixture/collector" && "$root/bin/hydra" integrate promote "$collection_run") >/dev/null 2>&1; then exit 1; fi
(cd "$fixture/collector" && "$root/bin/hydra" integrate approve "$collection_run" --by tester) >/dev/null
git -C "$fixture/collector" update-ref "$collection_ref" "$commit" "$collection_commit"
if (cd "$fixture/collector" && "$root/bin/hydra" integrate promote "$collection_run") >/dev/null 2>&1; then exit 1; fi
[ "$(git -C "$fixture/collector" rev-parse HEAD)" = "$collection_base" ]
git -C "$fixture/collector" update-ref "$collection_ref" "$collection_commit" "$commit"
(cd "$fixture/collector" && "$root/bin/hydra" integrate promote "$collection_run") >/dev/null
cmp "$fixture/expected-result" "$fixture/collector/result.txt"
(cd "$fixture/collector" && "$root/bin/hydra" integrate cleanup "$collection_run" --apply) >/dev/null
# Uncommitted remote work remains collectable evidence, never an integration candidate.
task collect --input "$fixture/result-package" --into "$fixture/collector" > "$fixture/dirty-collection"
collection_dirty_id="$(sed -n 's/.*"collection_id":"\([^"]*\)".*/\1/p' "$fixture/dirty-collection")"
if task collected --id "$collection_dirty_id" --into "$fixture/collector" --format candidates > "$fixture/dirty-candidates"; then exit 1; fi
grep -q '"code":"integration_ineligible"' "$fixture/dirty-candidates"
printf 'Collected integration: real local gates, explicit approval, stale-ref refusal, promotion, and dirty-result exclusion passed\n'
