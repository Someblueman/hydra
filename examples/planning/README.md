# Local objective planning examples

These fixed examples exercise Hydra's public planning and independent outcome checks without an authenticated provider. `feature` assembles a missing slug implementation and validates the sealed candidate with a separate build and public CLI checks. `performance` measures a fixed line-count comparison and independently recomputes its result. `research` answers a bounded scheduling question from a supplied trace and accepts a reproducible negative result. `patterns` compares bounded static decompositions.

[Finite manifest maps](manifest-map/README.md) freeze selected and skipped members
before admission. [Staged planning](../../docs/PLAN_STAGED.md) passes an accepted,
source-bound finding into a separately compiled static graph. The
[planner evaluation pilot](evaluation/README.md) retains six frozen candidates,
independent finite cases and incorrect-artifact controls; its observed interface
conformance difference does not establish a planning benefit.

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
For `manifest-map` and `staged`, also copy `examples/planning/native/payload.sh`
into that repository. It supplies their shared fixed worker and join recipes.
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

Write scopes remain declarations, not OS isolation. Performance execution requires an explicitly coordinated measurement window. See the [planner recipe](../../docs/PLANNER_RECIPE.md), historical [provider qualification](../../docs/evidence/plan-qualification.md), and current [feature](../../docs/evidence/9d-feature-outcome.md), [performance](../../docs/evidence/9d-performance-outcome.md), and [research](../../docs/evidence/9d-research-outcome.md) evidence. The retained historical provider runs are not prerequisites for these deterministic examples.
