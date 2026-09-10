# Local objective planning examples

These are agent-authored plans exercised through Hydra's public planning CLI.
`feature` starts with a missing `slugify` implementation and produces an integrated
source archive that a separate head builds and tests. `research` measures a
supplied trace, uses an analyst to interpret it, composes a complete report and
uses a separate assessor session to evaluate the sealed report. Both use the
existing `opencode` profile, which must already be authenticated.

Run each in a **new disposable Git repository**. Do not run these spawn recipes
inside Hydra's implementation checkout. For example, from the Hydra checkout:

```sh
hydra_bin="$PWD/bin/hydra"
example_root="$(mktemp -d)"
mkdir "$example_root/repo"
cp -R examples/planning/feature/. "$example_root/repo/"
mv "$example_root/repo/plan.json" "$example_root/plan.json"
mv "$example_root/repo/policy.json" "$example_root/policy.json"
cd "$example_root/repo"
git init
git add .
git commit -m 'Planning example source'
"$hydra_bin" init --no-agent --trust
"$hydra_bin" workflow plan schema
"$hydra_bin" workflow plan validate ../plan.json ../policy.json
"$hydra_bin" workflow plan compile ../plan.json ../policy.json ../compiled.json
"$hydra_bin" workflow plan show ../compiled.json
```

Review the policy, source recipes, declared writes and exact checker recipe. Copy every example source file into the disposable repository root before compiling; plans bind the copied source and data hashes.
Run `hydra workflow plan run ../compiled.json --accept <reported-sha256>` only
with authorization for that exact scope. Record the returned run ID, inspect
`workflow status <run-id> --json`, and use `workflow plan result <run-id>` to
retrieve reverified final artifacts. Substitute `research` above for the second
example. Build the optional helper with `make build-fleet` first.

The checked-in plans retain the names from qualification. Fresh repositories
have separate identities; repeating in one repository requires fresh head names,
a newly compiled artifact and acceptance. A failed run is retained as evidence;
it does not authorize silent retries or weaker checks. Runtime/provider versions,
source paths and content affect the compiled digest.

The worker returns raw source; composition supplies the fixed public header include and
a final newline before strict compilation. Research prompts embed their sealed input content so an agent does
not need access to external run directories. Write scopes remain declarations,
not OS isolation. See [the planner recipe](../../docs/PLANNER_RECIPE.md) and
[qualification evidence](../../docs/evidence/plan-qualification.md).
